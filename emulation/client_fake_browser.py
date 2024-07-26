import math
import sys
import time
import logging
import requests
import threading
from urllib.parse import urlparse, urlencode, parse_qs
from os import path
from xml.etree import ElementTree
from datetime import datetime, timedelta

start_time = time.time_ns() / 1_000_000 # Start time in milliseconds
print(start_time)

url = '10.0.0.100:33333'
client_name = 'client'
sleep_time = 0
threaded=True
headless=True

print('Starting headless browser')

for index, arg in enumerate(sys.argv[1:], start=1):
    if index == 1:
        url = arg
    elif index == 2:
        client_name = arg
    elif index == 3:
        sleep_time = float(arg)
    elif index == 4:
        threaded = arg.lower() == 'true'
    elif index == 5:
        headless = arg.lower() == 'true' or True # Always headless
    


print(f'URL: {url}')
print(f'Client name: {client_name}')
print(f'Sleep time: {sleep_time}')

logger = logging.getLogger('client')
logger.setLevel(logging.DEBUG)
file_handler = logging.FileHandler(f'{client_name}.metric.log')
formatter = logging.Formatter('%(asctime)s;%(message)s')
formatter_with_time = logging.Formatter('%(asctime)s;client_running;%(message)s')
file_handler.setFormatter(formatter_with_time)
logger.addHandler(file_handler)
logger.info("0")

def process_url(input_url):
    # Parse the input URL
    parsed_url = urlparse(input_url)

    # Extract query parameters
    query_params = parse_qs(parsed_url.query)

    # Extract specific parameters
    seg_dur = query_params.get('seg_dur', [''])[0]
    video = query_params.get('video', [''])[0]
    is_live = query_params.get('live', [''])[0]

    # Extract the URL base
    url_base = f"{parsed_url.scheme}://{parsed_url.netloc}"

    # Construct the desired URL
    processed_url = f"{url_base}/{video}/{seg_dur}/{'live' if is_live else 'vod'}.mpd"

    return url_base, processed_url, seg_dur, video

def get_representations(mpd_content):
    root = ElementTree.fromstring(mpd_content)

    # Get the total video length
    media_representation_duraton = root.attrib['mediaPresentationDuration'] # = "PT97S"
    # Convert to seconds
    media_representation_duraton = float(media_representation_duraton[2:-1])


    periods = []
    for item in root:
        if item.tag.endswith('Period'):
            periods.append(item)

    adaptation_sets = []
    for item in periods[0]:
        if item.tag.endswith('AdaptationSet'):
            adaptation_sets.append(item)

    representations = []
    for item in adaptation_sets[0]:
        if item.tag.endswith('Representation'):
            representations.append(item)

    print(f'Found {len(representations)} representations')

    # create a list of tuples (bandwidth, representation)
    ordered_representations = [(int(item.attrib['bandwidth']), item) for item in representations]
    # sort the list by bandwidth, in descending order
    ordered_representations.sort(key=lambda tup: tup[0], reverse=True)

    # Get the representation with the highest bandwidth
    max_bandwidth = ordered_representations[0][0]
    representation = ordered_representations[0][1]
    print(f"Max bandwidth: {max_bandwidth}")

    segment_templates = []
    for bw, rep in ordered_representations:
        for item in rep:
            if item.tag.endswith('SegmentTemplate'):
                segment_templates.append(item)
                break

    print(f'Found {len(segment_templates)} segment templates')
    
    
    # Extract base URL, initialization, and segment template
    #base_url = root.find(".//BaseURL").text
    initializations = [segment_template.attrib['initialization'] for segment_template in segment_templates]
    media_templates = [segment_template.attrib['media'] for segment_template in segment_templates]
    
    # Extract segment duration from the template
    segment_durations = [int(segment_template.attrib['duration']) for segment_template in segment_templates]

    availability_start_time = root.attrib['availabilityStartTime']
    
    return initializations, media_templates, segment_durations, availability_start_time, media_representation_duraton

def download_video_segment(base_url, video, segment_duration, media_template, segment_number):
    segment_url = f"{base_url}/{video}/{segment_duration}/"
    segment_url += f"{media_template.replace('$Number%04u$', str(segment_number).zfill(4))}"
    
    print(f"Downloading segment {segment_number}")
    response = requests.get(segment_url)
    
    if response.status_code != 200:
        print(f"Failed to download segment {segment_number}. Status code: {response.status_code}")
        return -1

    real_representaion_id = media_template.split('/')[0]
    
    print(f"Downloaded segment {segment_number} {real_representaion_id}")
    return 0

def download_video_segment_threaded(base_url, video, segment_duration, media_template, segment_number, results, lock):
    status = download_video_segment(base_url, video, segment_duration, media_template, segment_number)
    
    # Acquire the lock before updating the shared results list
    with lock:
        results.append((segment_number, status))

def wait_until_start_time(start_time):
    current_time = datetime.utcnow()
    start_time = datetime.strptime(start_time, "%Y-%m-%dT%H:%M:%S.%fZ")
    
    # Wait until it's time to start
    while current_time < start_time:
        time.sleep(0.1)
        current_time = datetime.utcnow()

    print(f"Current time: {current_time}")

try:
    ms_since_start = round((time.time_ns() / 1_000_000) - start_time) # in milliseconds
    time_to_sleep = (sleep_time * 1000) - ms_since_start # in milliseconds (sleep_time is in seconds)

    if time_to_sleep > 0:
        print(f"Sleeping for {time_to_sleep} milliseconds to get to {sleep_time} seconds")
        time.sleep(time_to_sleep / 1000)
    else:
        print(f"Sleep time is negative: {time_to_sleep} milliseconds!")

    # Log that we have started
    logger.info("1")
    file_handler.setFormatter(formatter)
    logger.info(f"segments_requested;{0}")

    url_base, url_mpd, seg_dur, video = process_url(url)

    # Load page
    print(f'Opening: {url}')
    page_response = requests.get(url)
    if page_response.status_code != 200:
        raise Exception(f'Failed to get page: {page_response.status_code}')
    

    ms_since_start = round((time.time_ns() / 1_000_000) - start_time) # in milliseconds
    print(f'Page loaded after {ms_since_start} milliseconds since start, including {sleep_time} seconds of sleep time')

    print('Loading MPD')
    # Make an initial request to get MPD content
    mpd_response = requests.get(url_mpd)
    if mpd_response.status_code != 200:
        raise Exception(f'Failed to get MPD file: {mpd_response.status_code}')

    mpd_content = mpd_response.text
    initializations, media_templates, segment_durations, availability_start_time, media_representation_duration = get_representations(mpd_content)

    initialization = initializations[0]
    media_template = media_templates[0]
    segment_duration = segment_durations[0]

    # Check if seg_dur is the same as the segment duration in the MPD
    if seg_dur != str(segment_duration):
        raise Exception(f"Segment duration in MPD ({segment_duration}) does not match seg_dur ({seg_dur})")

    
    print(f'Availability start time: {availability_start_time}')
    print(f'Segment duration: {segment_duration}')
    print(f'Initialization: {initialization}')
    print(f'Media template: {media_template}')

    print('Doing time sync')
    requests.get(f"{url_base}/time")
    requests.get(f"{url_base}/time")
    requests.get(f"{url_base}/time")

    # Fetching initial segment
    print('Fetching initial segment')
    print(f"{url_base}/{video}/{seg_dur}/{initialization}")
    init_response = requests.get(f"{url_base}/{video}/{seg_dur}/{initialization}")
    if init_response.status_code != 200:
        raise Exception(f'Failed to get initial segment: {init_response.status_code}')
    

    print('Waiting for video stream to be loaded')
    wait_until_start_time(availability_start_time)
    
    print('Starting playback')
    ms_since_start = round((time.time_ns() / 1_000_000) - start_time) # in milliseconds
    print(f'Playback started at {ms_since_start} milliseconds since start, including {sleep_time} seconds of sleep time')
    segment_number = 1
    max_segments = math.ceil(media_representation_duration / segment_duration)

    threads = []
    results = []
    results_lock = threading.Lock()

    
    current_tim_in_ns = time.time_ns()
    representation_id = 0 # Not the real representation ID, just a counter, lower is higher bandwidth
    while segment_number <= max_segments:
        media_template = media_templates[representation_id]
        segment_duration = segment_durations[representation_id]
        
        if threaded:
            thread = threading.Thread(target=download_video_segment_threaded,
                                    args=(url_base, video, segment_duration, media_template, segment_number, results, results_lock))
            thread.start()
            threads.append(thread)
        else:
            status = download_video_segment(url_base, video, segment_duration, media_template, segment_number)
            results.append((segment_number, status))

        logger.info(f"segments_requested;{segment_number}")
        temp_time_in_ns = time.time_ns()
        # Calculate the difference between the current time and the time when the segment was downloaded
        diff_time_in_ns = temp_time_in_ns - current_tim_in_ns
        if diff_time_in_ns <= 0:
            diff_time_in_ns = 0
        # Sleep for the remaining time
        time_to_sleep = ((segment_duration * 1000) - (diff_time_in_ns / 1_000_000)) / 1000
        # If the sleep time is lower
        if time_to_sleep > 0:
            time.sleep(time_to_sleep)
        else:
            print(f"Sleep time is negative: {time_to_sleep} seconds!")

        # If the segment was downloaded with only 5% of the buffer left, change the representation for the next segment
        # Here, lower id means higher bandwidth
        if diff_time_in_ns > (segment_duration * 1000 * 0.95 * 1_000_000):
            representation_id = min(representation_id + 1, len(media_templates))
        else:
            representation_id = max(representation_id - 1, 0)

        current_tim_in_ns = time.time_ns()
        segment_number += 1

        # Check if any of the results is < 0
        with results_lock:
            for result in results:
                if result[1] < 0:
                    # One of the segments failed to download
                    segment_number = max_segments + 1 # Force the loop to end

    # Wait for all the threads to finish
    for thread in threads:
        thread.join()


    print('Playback ended')


except Exception as e:
    print('# Error')
    print(e)

file_handler.setFormatter(formatter_with_time)
logger.info("0")
print('# Done')
