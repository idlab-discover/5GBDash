#!/bin/bash

# default values for the base directory and the segment duration
base_dir="content"
fps=24000/1001
seg_dur=4
file_name=""

# extract options for base directory, CRF, FPS or segment duration
while getopts "b:c:r:s:f:" opt; do
        case $opt in
        	b) base_dir=${OPTARG};;
        	r) fps=${OPTARG};;
        	s) seg_dur=${OPTARG};;
			f) file_name=${OPTARG};;
                *)
                        echo 'Error in command line parsing'
                        exit 1
        esac
done
shift $((OPTIND-1))


echo $base_dir
echo $fps
echo $seg_dur
echo $file_name


video_name=${file_name%.*}

# base directory of the video
video_dir="$base_dir/$video_name"
mkdir -p $video_dir

# determine gop length
gop=$((seg_dur * fps))

# extract video width, height and duration
eval $(ffprobe -v error -show_entries stream=width,height,duration -of default=noprint_wrappers=1 $file_name)
duration=$(echo "scale=0; $duration/1" | tr -d $'\r' | bc -l )
width=$(echo $width | tr -d $'\r')
height=$(echo $height | tr -d $'\r')
duration=$(echo $duration | tr -d $'\r')
echo $width
echo $height
echo $duration

# segment directory
seg_dir="$video_dir/$seg_dur"
mkdir $seg_dir

# MPD file
mpd="$seg_dir/live.mpd"

# generate MPD header
echo "<?xml version=\"1.0\" ?>
<MPD mediaPresentationDuration=\"PT${duration}S\" minBufferTime=\"PT${seg_dur}S\" profiles=\"urn:mpeg:dash:profile:isoff-live:2011\" type=\"dynamic\" xmlns=\"urn:mpeg:dash:schema:mpd:2011\" availabilityStartTime=\"2021-01-01T00:00:00Z\" minimumUpdatePeriod=\"P${duration}S\">
<Period start=\"PT0S\">
<BaseURL></BaseURL>
<AdaptationSet segmentAlignment=\"true\">" > $mpd

# set representation ID
rep_id=1

# generate segmented video for different widths
#for custom_width in 412 640 960 1280 1920; do
custom_width=1920
for crf in 20 30 51; do

	# determine corresponding height
	custom_height=$((custom_width * height / width))
	if [ $((custom_height % 2)) -eq 1 ]; then
		((custom_height += 1))
	fi

	echo $custom_width
	echo $custom_height

	# encode the video using ffmpeg and x264, using inserted key frames
	# ffmpeg -i $video_dir/$file_name -c:v libx264 -r $fps -direct-pred none -bf 3 -b_strategy 0 -x264-params fps=$fps:min-keyint=$gop:keyint=$gop:scenecut=-1:b-pyramid=strict -filter:v scale=$custom_width:$custom_height -preset slow -tune psnr -crf $crf -c:a aac -strict -2 -y output_$crf.mp4
	ffmpeg -i $file_name -filter:v scale=$custom_width:$custom_height -y output_$crf.y4m -hide_banner -loglevel error
	./x264 --preset veryfast --fps $fps --crf $crf --direct none --bframes 3 --b-adapt 0 --keyint $gop --min-keyint $gop --no-scenecut --b-pyramid strict -o output_$crf.h264 output_$crf.y4m
	
	((rep_id += 1))
done

#extract I P B
ffprobe -i output_20.h264 -v quiet -select_streams v -print_format json -show_frames > output_20.h264.json
python ../nalu/ffprobe_to_naluprocessing.py output_20.h264.json
> output_25.h264
> output_35.h264
#Inject IP 
# output_35         contains B  from 51   and IP  from 30
../nalu/NALUProcessing/build/src/App/StreamProcessing/StreamProcessing output_51.h264 output_30.h264 output_35.h264 output_20.h264.json.IP.txt 0

# output_25         contains B  from 30   and IP from 20
../nalu/NALUProcessing/build/src/App/StreamProcessing/StreamProcessing output_30.h264 output_20.h264 output_25.h264 output_20.h264.json.IP.txt 0

# output_27         contains B  from 51   and IP  from 20
../nalu/NALUProcessing/build/src/App/StreamProcessing/StreamProcessing output_51.h264 output_20.h264 output_27.h264 output_20.h264.json.IP.txt 0

#rm output_20.h264
#mv output_20b.h264 output_20.h264
#convert to mp4 again
for crf in 20 25 27 30 35 51; do
	echo output_$crf.h264
	ffmpeg -i output_$crf.h264 -i $file_name -r $fps -map 0:v:0 -map 1:a:0 -vcodec copy -y output_$crf.mp4 -hide_banner -loglevel error
done
	

# set representation ID
rep_id=1
for crf in 51 35 30 25 20; do

	# determine corresponding height
	custom_height=$((custom_width * height / width))
	if [ $((custom_height % 2)) -eq 1 ]; then
		((custom_height += 1))
	fi
	
	# segment the video using MP4Box
	MP4Box -profile "dashavc264:live" -dash $((seg_dur * 1000)) -rap -base-url ./ -bs-switching no -segment-name segment_ -segment-ext mp4 output_$crf.mp4
	
	# directory of custom rep ID
	rep_dir=$video_dir/$seg_dur/$rep_id
	mkdir $rep_dir
	
	# move all segments
	mv segment_init.mp4 $rep_dir/init_$rep_id.mp4
	for seg in segment*.mp4; do
		i=${seg%.*}
		i=${i##*_}
		mv $seg $rep_dir/segment_$(printf "%04d" $i)_$rep_id.mp4
	done
	
	# determine the required bandwidth and remove the generated MPD
	bw=$(grep "bandwidth=" output_$crf\_dash.mpd | sed 's/.*bandwidth="//g' | sed 's/">//g')
	
	# update the MPD
	echo "<Representation audioSamplingRate=\"48000\" bandwidth=\"$bw\" codecs=\"avc1.640028,mp4a.40.2\" frameRate=\"$fps\" height=\"$custom_height\" id=\"$rep_id\" mimeType=\"video/mp4\" sar=\"1:1\" startWithSAP=\"1\" width=\"$custom_width\">
<SegmentTemplate duration=\"$seg_dur\" initialization=\"$rep_id/init_$rep_id.mp4\" media=\"$rep_id/segment_\$Number%04u\$_$rep_id.mp4\" startNumber=\"1\"/>
</Representation>" >> $mpd

	# remove files that are no longer required
	rm output_$crf\_dash.mpd
	rm output_$crf.y4m
	rm output_$crf.mp4
	((rep_id += 1))
done

rm output_20.h264.json
rm output_20.h264.json.IP.txt
rm output_20.h264.json.B.txt
# rm output_51.y4m
# mv output_25_result.h264 $video_dir/$seg_dur/output_25_result.h264
for crf in 20 25 51; do
	rm output_$crf.h264
done
# generate MPD footer
echo '</AdaptationSet>
</Period>
</MPD>' >> $mpd