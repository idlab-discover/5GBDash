#!/bin/bash
seg_dur=$1
quality_level=$2
quality_dir="./content/avatar/$seg_dur/$quality_level"
temp_dir="./temp"
mkdir -p "$temp_dir"
# Loop through all mp4 files of the specified quality level, except the init
for file in $quality_dir/*_$quality_level.mp4; do
    file_basename="$(basename "$file")"
    # Check if file_basename is not "init_3.mp4"
    if [ -f "$file" ] && [ "$file_basename" != "init_$quality_level.mp4" ]; then        
        # Find the corresponding file in quality 1
        file1=$(echo "$file" | sed "s/\/$seg_dur\/$quality_level\//\/$seg_dur\/1\//; s/$quality_level.mp4$/1.mp4/")  
      
        # Check if the corresponding quality 1 file exists
        if [ -f "$file1" ]; then
            # Store the paths in variables
            file_init="$quality_dir/init_$quality_level.mp4"
            file1_init="$(dirname "$file1")/init_1.mp4"

            # Clear the temporary files
            > "${temp_dir}/base.mp4"
            > "${temp_dir}/augment.mp4"
            > "${temp_dir}/augment.h264"
            > "${temp_dir}/base.h264"
            > "${temp_dir}/base.json"
            > "${temp_dir}/base.json.B.txt"
            > "${temp_dir}/base.json.IP.txt"

            # Create the base and augmentation files
            cat $file1_init $file1 > $temp_dir/base.mp4    
            cat $file_init $file > $temp_dir/augment.mp4                 
            
            # This script converts the H264 mp4 video streams to the Annex B format without re-encoding.
            # It uses the ffmpeg tool to copy the video codec and apply the h264_mp4toannexb bitstream filter.
            # The resulting video streams are saved as base.h264 and augment.h264 in the temporary directory.
            ffmpeg -i $temp_dir/base.mp4 -vcodec copy -bsf h264_mp4toannexb -y $temp_dir/base.h264 -hide_banner -loglevel error
            ffmpeg -i $temp_dir/augment.mp4 -vcodec copy -bsf h264_mp4toannexb -y $temp_dir/augment.h264 -hide_banner -loglevel error
            ../nalu/NALUProcessing/build/src/App/StreamInfo/StreamInfo "${temp_dir}/augment.h264" 0 > "${temp_dir}/base.json"

            # Extract from base.json which frame numbers belong to I and P frames (base.json.IP.txt),
            # and which numbers belong to B frames (base.json.B.txt).
            python3 ../nalu/ffprobe_to_naluprocessing.py "${temp_dir}/base.json"

            # Create an .h264 file which contains the IP frames from augment.h264 (excludes B frames)
            ../nalu/NALUProcessing/build/src/App/StreamReducer/StreamReducer $temp_dir/augment.h264 $temp_dir/IP.h264 $temp_dir/base.json.IP.txt
            # Create an .h264 file which contains the B frames from augment.h264 (excludes IP frames)
            ../nalu/NALUProcessing/build/src/App/StreamReducer/StreamReducer $temp_dir/augment.h264 $temp_dir/B.h264 $temp_dir/base.json.B.txt
            
            mv $temp_dir/IP.h264 $file.IP.h264
            mv $temp_dir/B.h264 $file.B.h264
            mv $temp_dir/augment.h264 $file.IPB.h264
            # mv $temp_dir/base.json.IP.txt $file.IP.txt
        fi
    fi
done
rm -rf "$temp_dir"
