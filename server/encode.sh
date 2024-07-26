#!/bin/bash

# Sets the locale for numeric formatting to the default C locale, which uses a period as the decimal separator.
LC_NUMERIC=C

# default values for the base directory and the segment duration
base_dir="./content/"
crf=18
fps=25
seg_dur=4
frag_dur=0.1

# extract options for base directory, CRF, FPS or segment duration
while getopts "b:c:f:r:s:" opt; do
        case $opt in
        	b) base_dir=${OPTARG};;
        	c) crf=${OPTARG};;
        	f) frag_dur=${OPTARG};;
        	r) fps=${OPTARG};;
        	s) seg_dur=${OPTARG};;
                *)
                        echo 'Error in command line parsing'
                        exit 1
        esac
done
shift $((OPTIND-1))

# file name is a required argument
if [ $# -lt 1 ]; then
	echo "Please provide a file name"
	exit
fi

# Enable fail-fast behavior for critical section
set -e
set -u

# Convert seg_dur and frag_dur to floats for comparison
seg_dur_float=$(printf "%.2f" $seg_dur)
frag_dur_float=$(printf "%.2f" $frag_dur)

# process file and video name
file_path=$1
file_name=$(basename -- "$file_path")
video_name=${file_name%.*}

# base directory of the video
video_dir="$base_dir/$video_name"
video_dir=$(echo "$video_dir" | sed 's|//|/|g')
mkdir -p $video_dir

# make a copy of the video
cp $file_path $video_dir/

# determine gop length
gop=$((seg_dur * fps))

# segment directory
seg_dir="$video_dir/$seg_dur"
mkdir $seg_dir

# Disable fail-fast behavior for the rest of the script
set +e
set +u


mpd_file="$seg_dir/live.mpd"

# ffmpeg
if [ "$(echo "$seg_dur_float == $frag_dur_float" | bc -l)" -ne 0 ];
then
	ffmpeg \
	-i $file_path \
	-c:v libx264 \
	-r $fps \
	-x264-params fps=$fps:min-keyint=$gop:keyint=$gop:scenecut=-1 \
	-preset slow \
	-tune psnr \
	-crf $crf \
	-strict -2 \
	-an \
	-map v:0 \
	-s:0 480x204 \
	-map v:0 \
	-s:1 640x272 \
	-map v:0 \
	-s:2 960x408 \
	-map v:0 \
	-s:3 1280x544 \
	-map v:0 \
	-s:4 1920x816 \
	-use_template 1 \
	-use_timeline 0 \
	-adaptation_sets "id=0,streams=v" \
	-seg_duration $seg_dur \
	-utc_timing_url "https://time.akamai.com/?iso" \
	-f dash $mpd_file
	
else
	ffmpeg \
	-i $file_path \
	-c:v libx264 \
	-r $fps \
	-x264-params fps=$fps:min-keyint=$gop:keyint=$gop:scenecut=-1 \
	-preset slow \
	-tune psnr \
	-crf $crf \
	-strict -2 \
	-an \
	-map v:0 \
	-s:0 480x204 \
	-map v:0 \
	-s:1 640x272 \
	-map v:0 \
	-s:2 960x408 \
	-map v:0 \
	-s:3 1280x544 \
	-map v:0 \
	-s:4 1920x816 \
	-ldash 1 \
	-streaming 1 \
	-use_template 1 \
	-use_timeline 0 \
	-adaptation_sets "id=0,streams=v" \
	-seg_duration $seg_dur \
	-frag_duration $frag_dur \
	-frag_type duration \
	-utc_timing_url "https://time.akamai.com/?iso" \
	-f dash $mpd_file
fi

mkdir -p "$seg_dir/1" "$seg_dir/2" "$seg_dir/3" "$seg_dir/4" "$seg_dir/5"

# Move files to respective directories based on stream ID and rename them
for file in "$seg_dir"/chunk-stream* "$seg_dir"/init-stream*; do
  base_name=$(basename "$file")
  stream_number=$(echo "$base_name" | grep -oP 'stream\K[0-9]+')
  stream_number_plus_one=$((stream_number + 1))


  new_name=$(echo "$base_name" | sed 's/-stream[0-9]\+//' | tr '-' '_')

  # Replace 'chunk' with 'segment' if seg_dur equals frag_dur
  if (( $(echo "$seg_dur_float == $frag_dur_float" | bc -l) )); then
    new_name=$(echo "$new_name" | sed 's/chunk/segment/')
  fi

  # Get the file extension
  ext="${new_name##*.}"
  new_name_base="${new_name%.*}"
  
  # Append '_$seg_dur' before the extension
  new_name="${new_name_base}_${stream_number_plus_one}.${ext}"
  
  # Move the file to the respective directory
  mv "$file" "$seg_dir/$stream_number_plus_one/$new_name"
done

# Run Python script if seg_dur and frag_dur are different
if [ "$(echo "$seg_dur_float != $frag_dur_float" | bc -l)" -ne 0 ]; then
  python3 extract_cmaf.py -d "$seg_dir"
fi

# Update Representation ID and attributes in the MPD file
for i in {0..4}; do
  new_id=$((i + 1))
  sed -i -E "s|Representation id=\"$i\"|Representation id=\"$new_id\"|g" "$mpd_file"
done

if [ "$(echo "$seg_dur_float == $frag_dur_float" | bc -l)" -ne 0 ]; then
    # If the condition is true, replace both 'chunk' and 'segment' with 'segment' in the media template
    sed -i 's|media="chunk-stream$RepresentationID$-$Number%05d$\.\([a-zA-Z0-9]\{1,5\}\)"|media="$RepresentationID$/segment_$Number%05d$_$RepresentationID$\.\1"|g' "$mpd_file"
else
    # If the condition is false, replace both 'chunk' and 'segment' with 'chunk' in the media template
    sed -i 's|media="chunk-stream$RepresentationID$-$Number%05d$\.\([a-zA-Z0-9]\{1,5\}\)"|media="$RepresentationID$/chunk_$Number%05d$_$RepresentationID$\.\1"|g' "$mpd_file"
fi

# The initialization replacement remains the same
sed -i 's|initialization="init-stream$RepresentationID$\.\([a-zA-Z0-9]\{1,5\}\)"| availabilityTimeComplete="false" initialization="$RepresentationID$/init_$RepresentationID$\.\1"|g' "$mpd_file"

# Make the MPD file dynamic, we want to support live streaming
sed -i 's|type="static"|type="dynamic" availabilityStartTime="2024-01-01T00:00:00.0Z" minimumUpdatePeriod="P60S"|g' "$mpd_file"

# Remove the timing, we the client will use the server time
sed -i '/<UTCTiming schemeIdUri="urn:mpeg:dash:utc:http-xsdate:2014" value="https:\/\/time\.akamai\.com\/\?iso"\/>/d' "$mpd_file"

