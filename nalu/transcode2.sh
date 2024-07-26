#!/bin/bash
fps=24000/1001 # This will be overwritten by the actual framerate of the augmented video
seg_dur=$(echo $7 | bc -l) # Define the segment duration
temp_dir="${6:-temp}/" # Define the directory for the temporary files, default is 'temp/'
output_file="$5" # The output file
script_dir="$(dirname "$0")"

# Check if the directory already exists
if [ ! -d "$temp_dir" ]; then
    # Create the directory and its parent directories if necessary
    if mkdir -p "$temp_dir"; then
        echo "Directory '$temp_dir' created successfully."
    else
        echo "Failed to create directory '$temp_dir'."
        exit 1
    fi
fi

# Clear the temporary files
> "${temp_dir}base.mp4" # The lower quality video stream
> "${temp_dir}augment.mp4" # The higher quality video stream

# Create the base and augmentation files
cat $1 $2 > "${temp_dir}base.mp4"
if [[ "$4" == *.mp4 ]]; then # If the augmentation file is .h264, then it is already in the correct format
    cat $3 $4 > "${temp_dir}augment.mp4"
fi

# Read the framerate from the augmented file
temp_fps=$(ffmpeg -i "${temp_dir}augment.mp4" 2>&1 | sed -n "s/.*,\(.*\)tbr.*/\1/p")
if [ -n "$temp_fps" ]; then
    # Convert the output to a float and save it in a variable
    fps=$(echo "$temp_fps" | bc -l)
fi

# This script converts the H264 mp4 video streams to the Annex B format without re-encoding.
# It uses the ffmpeg tool to copy the video codec and apply the h264_mp4toannexb bitstream filter.
# The resulting video streams are saved as base.h264 and augment.h264 in the temporary directory.
ffmpeg -i "${temp_dir}base.mp4" -vcodec copy -bsf h264_mp4toannexb -y "${temp_dir}base.h264" -hide_banner -loglevel error
if [[ "$4" == *.mp4 ]]; then # If the augmentation file is .h264, then it is already in the correct format
    ffmpeg -i "${temp_dir}augment.mp4" -vcodec copy -bsf h264_mp4toannexb -y "${temp_dir}augment.h264" -hide_banner -loglevel error
else
    cp $4 "${temp_dir}augment.h264"
fi

# Use ffprobe to extract information about the video stream from the input file "base.h264".
# The output is in JSON format and contains information about each frame in the video.
# The output is then saved to the file "base.json" in the temporary directory.
ffprobe -i "${temp_dir}base.h264" -v quiet -select_streams v -print_format json -show_frames > "${temp_dir}base.json"

# Extract from base.json which frame numbers belong to I and P frames (base.json.IP.txt),
# and which numbers belong to B frames (base.json.B.txt).
python3 $script_dir/ffprobe_to_naluprocessing.py "${temp_dir}base.json"

# Create an empty .h264 file
> "${temp_dir}concatenated.h264"
# Fill .h264 file with frames build with Temporal Layer Injection.
$script_dir/NALUProcessing/build/src/App/StreamProcessing/StreamProcessing "${temp_dir}base.h264" "${temp_dir}augment.h264" "${temp_dir}concatenated.h264" "${temp_dir}base.json.IP.txt"  0

# This command uses FFmpeg to generate an intermediate file by injecting temporal layers
# It takes two input files: "concatenated.h264" and "base.mp4".
# The video stream from "concatenated.h264" is mapped to the output file, while the audio stream from "base.mp4" is mapped to the output file.
# The output file is saved as "concatenated.mp4" in the temporary directory specified by the variable "temp_dir".
# The frame rate of the output file is set to the value of the variable "fps".
# The "-vcodec copy" option is used to copy the video stream without re-encoding, which is faster and preserves the original quality.
# The "-y" option is used to overwrite the output file if it already exists.
# The "-hide_banner" and "-loglevel error" options are used to suppress unnecessary output messages.
ffmpeg -i "${temp_dir}concatenated.h264" -i "${temp_dir}base.mp4" -r $fps -map 0:v:0 -map 1:a:0 -vcodec copy -y "${temp_dir}concatenated.mp4" -hide_banner -loglevel error

# This command uses MP4Box to create a DASH manifest and segments from a concatenated MP4 file.
# The resulting segments will have a duration of seg_dur (in milliseconds) and will be named using the format "${temp_dir}split_<segment_number>.mp4".
# The DASH manifest will be named "stream.mpd" and will be located in the same directory as the segments.
MP4Box -profile "dashavc264:live" -dash $((seg_dur * 1000)) -rap -base-url ./ -bs-switching no -segment-name "${temp_dir}split_" -segment-ext mp4 "${temp_dir}concatenated.mp4" -quiet

# Move the generated files to the output directory
output_directory=$(dirname "$output_file")
# Check if the directory already exists
if [ ! -d "$output_directory" ]; then
    # Create the directory and its parent directories if necessary
    if mkdir -p "$output_directory"; then
        echo "Directory '$output_directory' created successfully."
    else
        echo "Failed to create directory '$output_directory'."
        exit 1
    fi
fi
mv "${temp_dir}split_init.mp4" "${output_directory}/init_2.mp4"
mv "${temp_dir}split_1.mp4" "$output_file"

# This removes the concatenated_dash.mpd file which was created by MP4Box.
rm concatenated_dash.mpd

# Clear the temporary files
# No need for these files to hold any data anymore.
> "${temp_dir}augment.mp4"
> "${temp_dir}augment.h264"
> "${temp_dir}base.h264"
> "${temp_dir}base.json"
> "${temp_dir}base.json.B.txt"
> "${temp_dir}base.json.IP.txt"
> "${temp_dir}base.mp4"
> "${temp_dir}concatenated.h264"
> "${temp_dir}concatenated.mp4"

