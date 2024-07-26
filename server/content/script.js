/**
 * A simple string buffer that delays writing when something is reading.
 * 
 */
class StringBuffer {
    constructor() {
        this._buffer = '';
        this._writeQueue = [];
        this._reading = false;
    }

    async write(input) {
        const res = this._enqueueWrite(input);
        this._processQueues();
        return res;
    }

    async read() {
        if (this._reading) {
            // Return an empty string, when another read request is
            // still happening. Pro: no queue for reading needed.
            // Con: the newer read request might be
            // returned earlier then the older one.
            return '';
        }
        this._reading = true;
        const result = this._buffer;
        this._buffer = '';
        this._reading = false;
        this._processQueues();
        return result;
    }

    async _enqueueWrite(input) {
        await new Promise(resolve => {
            this._writeQueue.push(() => {
                this._buffer += input;
                resolve();
            });
        });
    }

    _processQueues() {
        if (this._reading) {
            return;
        }
        const writeFunc = this._writeQueue.shift();

        if (writeFunc) {
            writeFunc();
            this._processQueues();
        }
    }
}

/**
 * Initialisation values.
 */
let player;
let video_element;
let latency_element;
let firstLoad = true;
let canPlayThrough = false;
let finished = false;
let logBuffer = new StringBuffer();
let eventMetricsInterval = 0;
let lastDecodedByteCount = 0;
let playing = false;

/**
 * Initialises the videoplayer.
 */
function init(seg_dur, chunk_count, live, low_latency, live_latency) {
    seg_dur = parseFloat(seg_dur);
    chunk_count = parseInt(chunk_count);
    seg_chunk_dur = seg_dur / chunk_count;
    video_element = document.querySelector("video");
    latency_element = document.querySelector("#live-latency");
    video_element.addEventListener('canplaythrough', () => {
        writeLog('canplaythrough');
        canPlayThrough = true;
    }, {});
    video_element.addEventListener('canplay', () => {
        writeLog('canplay');
        attempt_to_continue();
        /* setTimeout(() => {
            canPlayThrough = true;
        }, 1000); // Fall back for when canplaythrough is not triggered */
    }, {});
    video_element.addEventListener('play', () => {
        let liveLatency = player.getCurrentLiveLatency();
        writeLog(`live_latency: ${isNaN(liveLatency) ? 0 : liveLatency}`);

        let playhead_time = player.time();
        writeLog(`playhead_time: ${isNaN(playhead_time) ? 0 : playhead_time}`);

        writeMetric('playing', 1);
        playing = true;

        writeLog('Low latency enabled: ' + player.getLowLatencyModeEnabled());
    }, {});

    player = dashjs.MediaPlayer().create();
    // Round up
    let buffer_goal = Math.ceil(seg_chunk_dur);
    if (buffer_goal == 1) {
        buffer_goal = 0.75;
    }
    writeLog(`Buffer goal: ${buffer_goal}`);
    player.updateSettings({
        debug: { logLevel: dashjs.Debug.LOG_LEVEL_DEBUG },
        errors: {
            recoverAttempts: {
                mediaErrorDecode: 50,
            }
        },
        streaming: {
            // cacheInitSegments: true,
            gaps:{
                enableStallFix: true,
            },
            lowLatencyEnabled: low_latency,
            delay: {
                liveDelay: live_latency,
            },
            liveCatchup: {
                    enabled: true,
                    maxDrift: 0.5,
                    playbackRate: low_latency ? {min: -0.3, max: 0.3} : {min: -0.3, max: 0.3},
                    playbackBufferMin: 0.5,
                    latencyThreshold: 20,
                    mode: low_latency ? 'liveCatchupModeLOLP' : 'liveCatchupModeDefault',
            },
            buffer: { // https://reference.dashif.org/dash.js/latest/samples/buffer/buffer-target.html
                bufferTimeAtTopQuality: buffer_goal,
                bufferTimeAtTopQualityLongForm: buffer_goal,
                stableBufferTime: buffer_goal,
                longFormContentDurationThreshold: 600,
            },
            utcSynchronization: {
                defaultTimingSource: {
                    scheme: 'urn:mpeg:dash:utc:http-xsdate:2014',
                    value: `${window.location.origin}/time`
                }
            },
            abr: low_latency ? {
                ABRStrategy: 'abrLoLP',
                fetchThroughputCalculationMode: 'abrFetchThroughputCalculationMoofParsing',
                additionalAbrRules: {
                    insufficientBufferRule: false,
                    switchHistoryRule: true,
                    droppedFramesRule: true,
                    abandonRequestsRule: true
                },
                autoSwitchBitrate: {
                    audio: true,
                    video: true
                },
            } : {}
          }
        });
    player.on(dashjs.MediaPlayer.events['PLAYBACK_ENDED'],() => {
        writeMetric('playing', 0);
        writeMetric('client_running', 0);
        let liveLatency = player.getCurrentLiveLatency();
        writeLog(`live_latency: ${isNaN(liveLatency) ? 0 : liveLatency}`);
        let playhead_time = player.time();
        writeLog(`playhead_time: ${isNaN(playhead_time) ? 0 : playhead_time}`);
        finished = true;
        clearInterval(eventMetricsInterval);
        playing = false;
    });

    player.on('error', (e) => {
        console.log(e);
        try  {
            writeLog(`Player error: ${(typeof e === 'object') ? JSON.stringify(e) : e}`);    
            if (e.error && e.error.code && e.error.code === 27) {
                finished = true; // A 404 error can be thrown when the video is finished.
            }
        } catch (e) {
            console.log(e);
        }
    });

    var origOpen = XMLHttpRequest.prototype.open;
    XMLHttpRequest.prototype.open = function() {
        var url = arguments[1];   
        writeLog(`XMLHttpRequest: started -> ${url}`);   
        this.addEventListener('load', function() {
            console.log(this.readyState); //will always be 4 (ajax is completed successfully)
            if (this.responseType === 'text' && this.responseText.length > 0) {
                writeLog(`XMLHttpRequest: result: ${this.responseText}`)
            } else {
                writeLog(`XMLHttpRequest: result type: ${this.responseType}`)
            }
        });
        origOpen.apply(this, arguments);
    };
}



function attempt_to_continue(attempt=0) {
    if (!playing || finished || !player || attempt >= 20) {
        return;
    }
    setTimeout(() => {
        if (isPaused()) {
            writeLog('Paused, attempting to start playing again.');
            player.play();
            attempt_to_continue(attempt + 1);
        } else {
            writeLog('Started playing again!');
        }
    }, 50);
}

/**
 * Loads a video mpd.
 * @param {string} video 
 * @param {string} seg_dur 
 * @param {boolean} live 
 */
function load(video, seg_dur, live = false) {
    let url = `${video}/${seg_dur}/${live ? 'live' : 'vod'}.mpd`;

    if (!firstLoad)
    {
        canPlayThrough = false;
        player.attachSource(url);
    }
    else
    {
        firstLoad = false;
        player.initialize(document.querySelector("video"), url, false);
    }
}

/**
 * Based on: http://reference.dashif.org/dash.js/latest/samples/advanced/monitoring.html
 * And: https://github.com/Dash-Industry-Forum/dash.js/blob/30441c6443789eeeeab226424087c2c9531c578d/samples/dash-if-reference-player/app/main.js#L2025
 */
function pollEventMetrics() {
    if (!player || !player.isReady() || !player.getActiveStream()) {
        return;
    }
    const streamInfo = player.getActiveStream().getStreamInfo();
    const dashMetrics = player.getDashMetrics();
    const dashAdapter = player.getDashAdapter();

    if (dashMetrics && streamInfo) {
        const periodIdx = streamInfo.index;

        const maxIndex = dashAdapter.getMaxIndexForBufferType('video', periodIdx);
        writeMetric('max_quality', maxIndex);

        const bufferLevel = dashMetrics.getCurrentBufferLevel('video', true);
        writeMetric('buffer_length', bufferLevel); // secs

        const droppedFramesMetrics = dashMetrics.getCurrentDroppedFrames('video', true);
        writeMetric('dropped_frames', droppedFramesMetrics ? droppedFramesMetrics.droppedFrames : 0);

        const repSwitch = dashMetrics.getCurrentRepresentationSwitch('video', true);

        const bitrate = repSwitch ? Math.round(dashAdapter.getBandwidthForRepresentation(repSwitch.to, periodIdx) / 1000) : 0;
        writeMetric('reported_bitrate', bitrate); // kbps

        const adaptation = dashAdapter.getAdaptationForType(periodIdx, 'video', streamInfo);
        const currentRep = adaptation.Representation_asArray.find((rep) => rep.id === repSwitch.to);
        writeMetric('framerate', currentRep.frameRate); // fps
        writeMetric('resolution_width', currentRep.width); // px
        writeMetric('resolution_height', currentRep.height); // px

        const quality = repSwitch && repSwitch.quality ? repSwitch.quality : player.getQualityFor('video');
        writeMetric('quality', quality + 1);

        let liveLatency = player.getCurrentLiveLatency();
        writeMetric('live_latency', isNaN(liveLatency) ? 0 : liveLatency);

        let playheadTime = player.time();
        writeMetric('playhead_time', isNaN(playheadTime) ? 0 : playheadTime);

        let playbackRate = parseFloat(player.getPlaybackRate().toFixed(2));
        writeMetric('playback_rate', playbackRate);

        let httpMetrics = calculateHTTPMetrics('video', dashMetrics.getHttpRequests('video'));
        if (httpMetrics) {
            let mtp = player.getAverageThroughput('video');
            writeMetric('mtp', parseFloat((mtp / 1000).toFixed(3)));

            writeMetric('download_low', parseFloat(httpMetrics.download['video'].low.toFixed(2)));
            writeMetric('download_average', parseFloat(httpMetrics.download['video'].average.toFixed(2)));
            writeMetric('download_high', parseFloat(httpMetrics.download['video'].high.toFixed(2)));

            writeMetric('latency_low', parseFloat(httpMetrics.latency['video'].low.toFixed(2)));
            writeMetric('latency_average', parseFloat(httpMetrics.latency['video'].average.toFixed(2)));
            writeMetric('latency_high', parseFloat(httpMetrics.latency['video'].high.toFixed(2)));

            writeMetric('ratio_low', parseFloat(httpMetrics.ratio['video'].low.toFixed(2)));
            writeMetric('ratio_average', parseFloat(httpMetrics.ratio['video'].average.toFixed(2)));
            writeMetric('ratio_high', parseFloat(httpMetrics.ratio['video'].high.toFixed(2)));

            writeMetric('etp', parseFloat((httpMetrics.etp['video'] / 1000).toFixed(3)));
            
        }
    }
}


/**
 * Based on: https://github.com/Dash-Industry-Forum/dash.js/blob/30441c6443789eeeeab226424087c2c9531c578d/samples/dash-if-reference-player/app/main.js#L1885
 * @param {*} type 
 * @param {*} requests 
 * @returns 
 */
function calculateHTTPMetrics(type, requests) {
    let latency = {},
        download = {},
        ratio = {},
        etp = {};

    let requestWindow = requests.slice(-20).filter((req) => {
        return req.responsecode >= 200 && req.responsecode < 300 && req.type === 'MediaSegment' && req._stream === type && !!req._mediaduration;
    }).slice(-4);

    if (requestWindow.length > 0) {
        let latencyTimes = requestWindow.map((req) => {
            return Math.abs(req.tresponse.getTime() - req.trequest.getTime()) / 1000;
        });

        latency[type] = {
            average: latencyTimes.reduce((l, r) => {
                return l + r;
            }) / latencyTimes.length,
            high: latencyTimes.reduce((l, r) => {
                return l < r ? r : l;
            }),
            low: latencyTimes.reduce((l, r) => {
                return l < r ? l : r;
            }),
            count: latencyTimes.length
        };

        let downloadTimes = requestWindow.map((req) => {
            return Math.abs(req._tfinish.getTime() - req.tresponse.getTime()) / 1000;
        });

        download[type] = {
            average: downloadTimes.reduce((l, r) => {
                return l + r;
            }) / downloadTimes.length,
            high: downloadTimes.reduce((l, r) => {
                return l < r ? r : l;
            }),
            low: downloadTimes.reduce((l, r) => {
                return l < r ? l : r;
            }),
            count: downloadTimes.length
        };

        let durationTimes = requestWindow.map((req) => {
            return req._mediaduration;
        });

        ratio[type] = {
            average: (durationTimes.reduce((l, r) => {
                return l + r;
            }) / downloadTimes.length) / download[type].average,
            high: durationTimes.reduce((l, r) => {
                return l < r ? r : l;
            }) / download[type].low,
            low: durationTimes.reduce((l, r) => {
                return l < r ? l : r;
            }) / download[type].high,
            count: durationTimes.length
        };

        const request = requestWindow[requestWindow.length - 1];
        etp[type] = request.cmsd && request.cmsd.dynamic && request.cmsd.dynamic.etp ? request.cmsd.dynamic.etp : 0;

        return {
            latency: latency,
            download: download,
            ratio: ratio,
            etp: etp
        };

    }
    return null;
}

/**
 * Generates an object of all allowed query parameters.
 * Optional parameters can have a default value.
 * @returns parameter object
 */
function getParams() {
    const queryString = window.location.search;
    const urlParams = new URLSearchParams(queryString);
    const result = {
        video: "alpha",
        seg_dur: "4",
        chunk_count: 1,
        live: false,
        low_latency: false,
        live_latency: 8.1
    };

    for (const [key, value] of Object.entries(result)) {
        if (urlParams.has(key)) {
            const newValue = urlParams.get(key);
            if (typeof value === 'boolean') {
                result[key] = newValue === 'true';
            } else if (typeof value === 'number') {
                result[key] = newValue.includes('.') ? parseFloat(newValue) : parseInt(newValue);
            } else {
                result[key] = newValue;
            }
        }

        writeLog(`${key}: ${result[key]}`);
    }

    return result;
} 

/**
 * Initialised and starts the video.
 */
function start() {
    writeMetric('client_running', 1);
    const {video, chunk_count, seg_dur, live, low_latency, live_latency} = getParams();
    init(seg_dur, chunk_count, live, low_latency, live_latency);
    load(video, seg_dur, live);

    eventMetricsInterval = setInterval(pollEventMetrics, 100);
}

/**
 * Returns whether or not the video player is paused or not.
 * @returns boolean
 */
function isPaused() {
    return player.isPaused();
}

/**
 * Returns wheater or not the video has been loaded enough to start streaming.
 * @returns boolean
 */
function isLoaded() {
    return canPlayThrough;
}


/**
 * Writes a metric with a timestamp to the log buffer.
 * @param {string} event 
 * @param {any} value 
 */
function writeMetric(event, value) {
    if (typeof value === 'number' && !isNaN(value)) {
        if (!isFinite(value)) {
          value = 0; // Just to prevent errors with the ratio metric
        }
    }
    const now = new Date();
    const timestamp = new Date(now.getTime() - now.getTimezoneOffset() * 60000).toISOString().slice(0,-1).replace('T', ' ').replace('.', ',');
    logBuffer.write(`${timestamp};${event};${value}\r\n`);
    
    if (event === 'live_latency') {
        latency_element.innerHTML = "Live latency: " + value + " seconds";
    }
    
}

function writeLog(input) {
    console.log(input);
    logBuffer.write(`# ${input.replace(/(\r\n|\n|\r)/gm, "")}\r\n`);
}

/**
 * Reads the current log buffer and clears it.
 * Concurrent writes are postponed untill the end of this function.
 * @returns {string} current log buffer
 */
function readLog() {
    return logBuffer.read().then(result => {
        if (result.length === 0 && finished) {
            return 'STOP';
        }
        return result;
    }).catch((e) => {
        console.log(e);
        return 'STOP';
    });
}