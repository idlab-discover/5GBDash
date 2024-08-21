#!/bin/bash
current_dir="$1"
current_quality=$(echo "$2 "| bc -l)
current_file="$3"
current_segment="$4"
seg_dur=$(echo "$5 "| bc -l) # Define the segment duration
temp_dir="${6:-temp}/" # Define the directory for the temporary files, default is 'temp/'
video_length=$(echo "$7" | bc -l) # Define the video length
current_segment_id=$(echo "$8" | bc -l) # Define the segment id
# Add random subdir to temp_dir to allow for parallel execution
temp_dir="${temp_dir}$(cat /dev/urandom | tr -dc 'a-zA-Z0-9' | fold -w 32 | head -n 1)/"
fps=24000/1001 # TODO: mention that this is hardcoded

# Log the start time in ms
start_time=$(($(date +%s%N)/1000000))

previous_quality=$((current_quality - 1))
next_quality=$((current_quality + 1))

# Check if the current directory is odd: 1 if odd, 0 if even
is_odd=$((current_quality % 2))

# Get the parent directory of the current directory
parent_dir=$(dirname "$current_dir")

# Check if the directory already exists
if [ ! -d "$temp_dir" ]; then
     mkdir -p "$temp_dir"
fi

if [ "$current_quality" != "1" ] && [ ! -d "$parent_dir/$previous_quality" ]; then
     mkdir -p "$parent_dir/$previous_quality"
fi

if [ ! -d "$parent_dir/$next_quality" ]; then
     mkdir -p "$parent_dir/$next_quality"
fi

if [ ! -d "$parent_dir/info" ]; then
     mkdir -p "$parent_dir/info"
fi

function has_init_file() {
    local quality="$1"

    if [ -f "$parent_dir/$quality/init_$quality.mp4" ]; then
        # This quality has an init file
        return 0 # 0 means OK, 1 means not OK
    else
        return 1
    fi
}

function has_segment_file() {
    local quality="$1"

    if [ -f "$parent_dir/$quality/${current_segment}_${quality}.mp4" ]; then
        # This quality has a segment file
        return 0
    else
        return 1
    fi
}

function has_init_and_segment_file() {
    local quality="$1"    

    if has_init_file "$quality" && has_segment_file "$quality"; then
        # This quality has both an init file and a segment file
        return 0
    else
        return 1
    fi
}

function find_base_file {
    local exclude="$1"

    # Loop over all qualities
    for quality in "$parent_dir"/*/; do
        quality=$(basename "$quality")  # Extract the directory name
        # Skip the exclude quality
        if [ "$quality" == "$exclude" ]; then
            continue
        fi
        if has_init_and_segment_file "$quality"; then
            # This quality has both an init file and a segment file
            echo "$quality"
            return 0
        fi
    done

    # No init file found
    echo "0"
    return 1
}

function has_segment_IPB_file {
    local quality="$1"

    if [ -f "$parent_dir/$quality/${current_segment}_${quality}.mp4.IPB.h264" ]; then
        # This quality has a segment IPB file
        return 0
    else
        return 1
    fi
    
}

function find_base_IPB_file {
    local exclude="$1"

    # Loop over all qualities
    for quality in "$parent_dir"/*/; do
        quality=$(basename "$quality")  # Extract the directory name
        # Skip the exclude quality
        if [ "$quality" == "$exclude" ]; then
            continue
        fi
        if has_segment_IPB_file "$quality"; then
            # This quality has a segment IPB file
            echo "$quality"
            return 0
        fi
    done

    # No init file found
    echo "0"
    return 1
}

function get_frame_info {
    local base_file="$1"
    local lock_fd

    local json_file="$parent_dir/info/${current_segment}.base.json"

    # Check if $current_segment.base.json exists in $parent_dir/info
    # If not, then generate it
    if [ ! -f "$json_file" ]; then
        # The file that we are going to create needs to be locked
        touch "$json_file"
        exec {lock_fd}<> "$json_file"
        while ! flock -xn $lock_fd; do
            sleep 0.001 # Sleep for 1 ms to try again
        done

        # The basefile needs to be locked as well
        touch "$base_file"
        exec {lock_fd_base}<> "$base_file"
        while ! flock -xn $lock_fd_base; do
            sleep 0.001 # Sleep for 1 ms to try again
        done

        # Extract information about the video stream from the input file "base.h264".
        # The output is in JSON format and contains information about each frame in the video.
        # The output is then saved to the file "base.json" in the temporary directory.
        ../nalu/NALUProcessing/build/src/App/StreamInfo/StreamInfo "$base_file" 0 > "$json_file"

        # Unlock the base file
        flock -u $lock_fd_base
        exec {lock_fd_base}>&-

        # Unlock the file
        flock -u $lock_fd
        exec {lock_fd}>&-
    fi

    get_nalu_info
    # get_info
}

function get_nalu_info {
    # Check if $current_segment.base.json.B.txt exists in $parent_dir/info
    # If not, then generate it
    if [ ! -f "$parent_dir/info/${current_segment}.base.json.B.txt" ]; then
            # Extract from base.json which frame numbers belong to I and P frames (base.json.IP.txt),
            # and which numbers belong to B frames (base.json.B.txt).
            # This script is file lock safe
            python3 ../nalu/ffprobe_to_naluprocessing.py "$parent_dir/info/${current_segment}.base.json"
    fi

    cp "$parent_dir/info/${current_segment}.base.json.B.txt" "${temp_dir}base.json.B.txt"
    cp "$parent_dir/info/${current_segment}.base.json.IP.txt" "${temp_dir}base.json.IP.txt"
}

function get_info {
    # Check if $current_segment.base.json.info.txt exists in $parent_dir/info
    # If not, then generate it
    if [ ! -f "$parent_dir/info/${current_segment}.base.json.info.txt" ]; then
            # Extract from base.json which frame numbers belong to I and P frames (base.json.IP.txt),
            # and which numbers belong to B frames (base.json.B.txt).
            # This script is file lock safe
            python3 ../nalu/ffprobe_to_info.py "$parent_dir/info/${current_segment}.base.json"
    fi

    cp "$parent_dir/info/${current_segment}.base.json.info.txt" "${temp_dir}base.json.info.txt"

}

# Generate IPB file if needed
if [[ "$current_file" == *.mp4 ]]; then
    # Check if "init_$current_quality.mp4" exists in the current directory
    # if so, then generate "${$current_file}.IPB.h264"
    if [ -f "$current_dir/init_$current_quality.mp4" ]; then
        # Check if "${current_file}.IPB.h264" does not exists
        if [ ! -f "${current_file}.IPB.h264" ]; then
            # Lock the current file
            touch "$current_file"
            exec {lock_fd_MP4}<> "$current_file"
            while ! flock -xn $lock_fd_MP4; do
                sleep 0.001 # Sleep for 1 ms to try again
            done

            > "${temp_dir}base.mp4" # The lower quality video stream
            cat $current_dir/init_$current_quality.mp4 $current_file > "${temp_dir}base.mp4"

            # Unlock the file
            flock -u $lock_fd_MP4
            exec {lock_fd_MP4}>&-

            # Extract the video stream from the base file and save it as "base.h264" in the temporary directory.
            ffmpeg -i "${temp_dir}base.mp4" -vcodec copy -bsf h264_mp4toannexb -y "${temp_dir}base.h264" -hide_banner -loglevel error

            # The file that we are going to create needs to be locked
            touch "${current_file}.IPB.h264"
            exec {lock_fd_IPB}<> "${current_file}.IPB.h264"
            while ! flock -xn $lock_fd_IPB; do
                sleep 0.001 # Sleep for 1 ms to try again
            done

            # Copy the base file to the current directory
            cp "${temp_dir}base.h264" "${current_file}.IPB.h264"

            # Unlock the file
            flock -u $lock_fd_IPB
            exec {lock_fd_IPB}>&-

        fi
    fi
# Generate mp4, IP and/or B files if needed
elif [[ "$current_file" == *.IPB.h264 ]]; then
    # Using find_base_file, find another quality that has the same segment as init and mp4 
    # (note: can actually be the same quality, so we pass 0 as the exclude parameter)
    other_quality=$(find_base_file "0")
    # Check if other_quality is not 0
    if [ "$other_quality" != "0" ]; then
        # Store this filename in a variable
        other_file="$parent_dir/$other_quality/${current_segment}_$other_quality.mp4"
        # Lock the other quality file
        touch "$other_file"
        exec {lock_fd_other_quality}<> "$other_file"
        while ! flock -xn $lock_fd_other_quality; do
            sleep 0.001 # Sleep for 1 ms to try again
        done        

        # Get base.mp4 and augment.h264
        cat "$parent_dir/$other_quality/init_$other_quality.mp4" "$parent_dir/$other_quality/${current_segment}_$other_quality.mp4" >> "${temp_dir}base.mp4"

        # Unlock the file
        flock -u $lock_fd_other_quality
        exec {lock_fd_other_quality}>&-

        # Lock the current file
        touch "$current_file"
        exec {lock_fd_current_file}<> "$current_file"
        while ! flock -xn $lock_fd_current_file; do
            sleep 0.001 # Sleep for 1 ms to try again
        done

        cp "$current_file" "${temp_dir}augment.h264"

        # Unlock the file
        flock -u $lock_fd_current_file
        exec {lock_fd_current_file}>&-

        # Generate mp4 file if it does not exists yet.
        if [ ! -f "$current_dir/${current_segment}_${current_quality}.mp4" ]; then
            # This command uses FFmpeg to generate an intermediate file by injecting temporal layers
            # It takes two input files: "concatenated.h264" and "base.mp4".
            # The video stream from "concatenated.h264" is mapped to the output file, while the audio stream from "base.mp4" is mapped to the output file.
            # The output file is saved as "concatenated.mp4" in the temporary directory specified by the variable "temp_dir".
            # The "-vcodec copy" option is used to copy the video stream without re-encoding, which is faster and preserves the original quality.
            # The "-y" option is used to overwrite the output file if it already exists.
            # The "-hide_banner" and "-loglevel error" options are used to suppress unnecessary output messages.
            # Read the framerate from the augmented file

            # Concatenate the shorter video to itself multiple times to match the length of the longer video
            segment_count=$(echo "scale=0; $video_length / $seg_dur / 1" | bc -l)
            #ffmpeg -stream_loop $segment_count -i "${temp_dir}augment.h264" -c copy -y "${temp_dir}looped_augment.h264"

            # Concatenate the longer video and the looped shorter video
            # ffmpeg -i "${temp_dir}base.mp4" -i "${temp_dir}augment.h264" -map 0 -map 1:v:0 -map 0:a:0 -vcodec copy -y "${temp_dir}result.mp4" -hide_banner -loglevel error

            # determine gop length
            gop=$((seg_dur * fps))

            ffmpeg -i "${temp_dir}augment.h264" -i "${temp_dir}base.mp4" -map 0:v:0 -map 1:a:0 -vcodec copy -video_track_timescale 24000 -y "${temp_dir}concatenated.mp4" -hide_banner -loglevel error

            # This command uses MP4Box to create a DASH manifest and segments from a concatenated MP4 file.
            # The resulting segments will have a duration of seg_dur (in milliseconds) and will be named using the format "${temp_dir}split_<segment_number>.mp4".
            # The DASH manifest will be named "stream.mpd" and will be located in the same directory as the segments.
            MP4Box -profile "dashavc264:live" -dash $((seg_dur * 1000)) -rap -base-url ./ -bs-switching no -segment-name "${temp_dir}split_" -moof-sn $current_segment_id -segment-ext mp4 "${temp_dir}concatenated.mp4" -quiet

            # Modify the mp4 to be compatible with the expected segment
            # The generated segment is not compatible with the expected segment, so we need to copy some mp4 metadata from a valid segment to the generated segment
            # The valid segment ($other_file) is the segment that has the same segment number as the generated segment and has the same timestamp in the stream.
            python3 copy_box_values.py "$other_file" ${temp_dir}split_1.mp4 # TODO: lock other_file first

            new_file="$current_dir/${current_segment}_${current_quality}.mp4"
            # Lock the new segment file
            touch "$new_file"
            exec {lock_fd_segment}<> "$new_file"
            while ! flock -xn $lock_fd_segment; do
                sleep 0.001 # Sleep for 1 ms to try again
            done

            cp ${temp_dir}split_1.mp4 "$new_file"

            # Unlock the file
            flock -u $lock_fd_segment
            exec {lock_fd_segment}>&-

            # Check if the init file does not exists yet
            if [ ! -f "$current_dir/init_${current_quality}.mp4" ]; then
                cp ${temp_dir}split_init.mp4 "$current_dir/init_${current_quality}.mp4" 
            fi
        fi

        # Get info about the frames
        get_frame_info "${temp_dir}augment.h264"

        # Check if the current segment does not exists in the previous quality directory
        # Only do this if the quality is not the base quality
        if [ "$current_quality" != "1" ] && [ ! -f "$parent_dir/$previous_quality/${current_segment}_${previous_quality}.mp4" ] && [ ! -f "$parent_dir/$previous_quality/${current_segment}_${previous_quality}.mp4.IPB.h264" ]; then
            # Add IP or B frames if needed
            if [ $is_odd -eq 1 ] && [ ! -f "$parent_dir/$previous_quality/${current_segment}_${previous_quality}.mp4.IP.h264" ]; then
                # Generate IP.h264 file
                > "${temp_dir}/IP.h264" # Create the file
                ../nalu/NALUProcessing/build/src/App/StreamReducer/StreamReducer $temp_dir/augment.h264 $temp_dir/IP.h264 $temp_dir/base.json.IP.txt # Fill in the file

                ip_file="$parent_dir/$previous_quality/${current_segment}_${previous_quality}.mp4.IP.h264"
                # Lock the new file
                touch "$ip_file"
                exec {lock_fd_IP}<> "$ip_file"
                while ! flock -xn $lock_fd_IP; do
                    sleep 0.001 # Sleep for 1 ms to try again
                done

                cp "${temp_dir}/IP.h264" "$ip_file"

                # Unlock the file
                flock -u $lock_fd_IP
                exec {lock_fd_IP}>&-

            elif [ -f "$parent_dir/$previous_quality/${current_segment}_${previous_quality}.mp4.B.h264" ]; then
                # Generate B.h264 file
                > "${temp_dir}/B.h264" # Create the file
                ../nalu/NALUProcessing/build/src/App/StreamReducer/StreamReducer $temp_dir/augment.h264 $temp_dir/B.h264 $temp_dir/base.json.B.txt # Fill in the file

                # Lock the new B file
                B_file="$parent_dir/$previous_quality/${current_segment}_${previous_quality}.mp4.B.h264"
                touch "$B_file"
                exec {lock_fd_B}<> "$B_file"
                while ! flock -xn $lock_fd_B; do
                    sleep 0.001 # Sleep for 1 ms to try again
                done

                cp "${temp_dir}/B.h264" "$B_file"

                # Unlock the file
                flock -u $lock_fd_B
                exec {lock_fd_B}>&-
            fi
        fi

        # Check if the current segment does not exists in the next quality directory
        if [ ! -f "$parent_dir/$next_quality/${current_segment}_${next_quality}.mp4" ] && [ ! -f "$parent_dir/$previous_quality/${current_segment}_${previous_quality}.mp4.IPB.h264" ]; then
            # Add IP or B frames if needed
            if [ $is_odd -eq 1 ] && [ ! -f "$parent_dir/$next_quality/${current_segment}_${next_quality}.mp4.B.h264" ]; then
                # Generate B.h264 file
                > "${temp_dir}/B.h264" # Create the file
                ../nalu/NALUProcessing/build/src/App/StreamReducer/StreamReducer $temp_dir/augment.h264 $temp_dir/B.h264 $temp_dir/base.json.B.txt # Fill in the file

                # Lock the new B file
                B_file="$parent_dir/$next_quality/${current_segment}_${next_quality}.mp4.B.h264"
                touch "$B_file"
                exec {lock_fd_B}<> "$B_file"
                while ! flock -xn $lock_fd_B; do
                    sleep 0.001 # Sleep for 1 ms to try again
                done

                cp "${temp_dir}/B.h264" "$B_file"

                # Unlock the file
                flock -u $lock_fd_B
                exec {lock_fd_B}>&-

            elif [ -f "$parent_dir/$next_quality/${current_segment}_${next_quality}.mp4.IP.h264" ]; then
                # Generate IP.h264 file
                > "${temp_dir}/IP.h264" # Create the file
                ../nalu/NALUProcessing/build/src/App/StreamReducer/StreamReducer $temp_dir/augment.h264 $temp_dir/IP.h264 $temp_dir/base.json.IP.txt # Fill in the file

                # Lock the new IP file
                ip_file="$parent_dir/$next_quality/${current_segment}_${next_quality}.mp4.IP.h264"
                touch "$ip_file"
                exec {lock_fd_IP}<> "$ip_file"
                while ! flock -xn $lock_fd_IP; do
                    sleep 0.001 # Sleep for 1 ms to try again
                done

                cp "${temp_dir}/IP.h264" "$ip_file" 

                # Unlock the file
                flock -u $lock_fd_IP
                exec {lock_fd_IP}>&-
            fi
        fi

    fi
# Generate IPB and/or IP files if needed
elif [[ "$current_file" == *.IP.h264 ]]; then
    # Check if we need to generate IPB.h264
    if [ ! -f "$current_dir/${current_segment}_${current_quality}.mp4" ] && [ ! -f "$current_dir/${current_segment}_${current_quality}.mp4.IPB.h264" ]  && [ -f "$current_dir/${current_segment}_${current_quality}.mp4.B.h264" ]; then
        # Using find_base_file, find another quality that has the same segment as init and mp4 
        # (note: can actually be the same quality, so we pass 0 as the exclude parameter)
        other_quality=$(find_base_IPB_file "$current_quality") # This does not need a file lock yet
        # Check if other_quality is not 0
        if [ "$other_quality" != "0" ]; then

            # Lock the base file
            base_h264="$parent_dir/$other_quality/${current_segment}_$other_quality.mp4.IPB.h264"
            touch "$base_h264"
            exec {lock_fd_base}<> "$base_h264"
            while ! flock -xn $lock_fd_base; do
                sleep 0.001 # Sleep for 1 ms to try again
            done
            # Copy the base file to the temporary directory as the base file
            cp "$base_h264" "${temp_dir}base.h264"
            # Unlock the file
            flock -u $lock_fd_base
            exec {lock_fd_base}>&-

            # Lock the current file
            touch "$current_file"
            exec {lock_fd_current_file}<> "$current_file"
            while ! flock -xn $lock_fd_current_file; do
                sleep 0.001 # Sleep for 1 ms to try again
            done
            # Copy the current file to the temporary directory as the augment file
            cp "$current_file" "${temp_dir}augment.h264"
            # Unlock the file
            flock -u $lock_fd_current_file
            exec {lock_fd_current_file}>&-

            # Get info about the frames
            get_frame_info "${temp_dir}base.h264"

            ../nalu/NALUProcessing/build/src/App/StreamConcat/StreamConcat "$current_dir/${current_segment}_${current_quality}.mp4.B.h264" "${temp_dir}augment.h264" "${temp_dir}concatenated.h264" "${temp_dir}base.json.IP.txt"  0

            
            # The file that we are going to create needs to be locked
            new_ipb_file="$current_dir/${current_segment}_${current_quality}.mp4.IPB.h264"
            touch "$new_ipb_file"
            exec {lock_fd_IPB}<> "$new_ipb_file"
            while ! flock -xn $lock_fd_IPB; do
                sleep 0.001 # Sleep for 1 ms to try again
            done
            
            # Copy the concatenated file to the current directory
            cp "${temp_dir}concatenated.h264" "$new_ipb_file"

            # Unlock the file
            flock -u $lock_fd_IPB
            exec {lock_fd_IPB}>&-
        fi
    fi

    # Copy to next or prev quality directory if needed
    if [ $is_odd -eq 1 ]; then
        # Check if it does not have a IPB, mp4 and IP file
        if [ ! -f "$parent_dir/$prev_quality/${current_segment}_${prev_quality}.mp4" ] && [ ! -f "$parent_dir/$prev_quality/${current_segment}_${prev_quality}.mp4.IPB.h264" ] && [ ! -f "$parent_dir/$prev_quality/${current_segment}_${prev_quality}.mp4.IP.h264" ]; then

            # Lock both the current file and the future IP file
            touch "$current_file"
            exec {lock_fd_current_file}<> "$current_file"
            while ! flock -xn $lock_fd_current_file; do
                sleep 0.001 # Sleep for 1 ms to try again
            done

            new_ip_file="$parent_dir/$prev_quality/${current_segment}_${prev_quality}.mp4.IP.h264"
            touch "$new_ip_file"
            exec {lock_fd_IP}<> "$new_ip_file"
            while ! flock -xn $lock_fd_IP; do
                sleep 0.001 # Sleep for 1 ms to try again
            done

            cp "$current_file" "$new_ip_file"

            # Unlock the files
            flock -u $lock_fd_current_file
            exec {lock_fd_current_file}>&-

            flock -u $lock_fd_IP
            exec {lock_fd_IP}>&-
        fi
    # Check if it does not have a IPB, mp4 and IP file
    elif [ ! -f "$parent_dir/$next_quality/${current_segment}_${next_quality}.mp4" ] && [ ! -f "$parent_dir/$next_quality/${current_segment}_${next_quality}.mp4.IPB.h264" ] && [ -f "$parent_dir/$next_quality/${current_segment}_${next_quality}.mp4.IP.h264" ]; then

        # Lock both the current file and the future IP file
        touch "$current_file"
        exec {lock_fd_current_file}<> "$current_file"
        while ! flock -xn $lock_fd_current_file; do
            sleep 0.001 # Sleep for 1 ms to try again
        done

        new_ip_file="$parent_dir/$next_quality/${current_segment}_${next_quality}.mp4.IP.h264"
        touch "$new_ip_file"
        exec {lock_fd_IP}<> "$new_ip_file"
        while ! flock -xn $lock_fd_IP; do
            sleep 0.001 # Sleep for 1 ms to try again
        done

        cp "$current_file" "$new_ip_file"

        # Unlock the files
        flock -u $lock_fd_current_file
        exec {lock_fd_current_file}>&-

        flock -u $lock_fd_IP
        exec {lock_fd_IP}>&-
    fi
# Generate IPB and/or B files if needed
elif [[ "$current_file" == *.B.h264 ]]; then
    # Check if we need to generate IPB.h264
    if [ ! -f "$current_dir/${current_segment}_${current_quality}.mp4" ] && [ ! -f "$current_dir/${current_segment}_${current_quality}.mp4.IPB.h264" ]  && [ -f "$current_dir/${current_segment}_${current_quality}.mp4.IP.h264" ]; then
        # Using find_base_file, find another quality that has the same segment as init and mp4 
        # (note: can actually be the same quality, so we pass 0 as the exclude parameter)
        other_quality=$(find_base_IPB_file "$current_quality") # This does not need a file lock yet
        # Check if other_quality is not 0
        if [ "$other_quality" != "0" ]; then
            # Lock the base file
            base_h264="$parent_dir/$other_quality/${current_segment}_$other_quality.mp4.IPB.h264"
            touch "$base_h264"
            exec {lock_fd_base}<> "$base_h264"
            while ! flock -xn $lock_fd_base; do
                sleep 0.001 # Sleep for 1 ms to try again
            done
            # Copy the base file to the temporary directory as the base file
            cp "$base_h264" "${temp_dir}base.h264"
            # Unlock the file
            flock -u $lock_fd_base
            exec {lock_fd_base}>&-

            # Lock the current file
            touch "$current_file"
            exec {lock_fd_current_file}<> "$current_file"
            while ! flock -xn $lock_fd_current_file; do
                sleep 0.001 # Sleep for 1 ms to try again
            done
            # Copy the current file to the temporary directory as the augment file
            cp "$current_file" "${temp_dir}augment.h264"
            # Unlock the file
            flock -u $lock_fd_current_file
            exec {lock_fd_current_file}>&-


            # Get info about the frames
            get_frame_info "${temp_dir}base.h264"

            ../nalu/NALUProcessing/build/src/App/StreamConcat/StreamConcat "${current_file}" "$current_dir/${current_segment}_${current_quality}.mp4.IP.h264" "${temp_dir}concatenated.h264" "${temp_dir}base.json.IP.txt"  0 # TODO: verify if we are using the correct files, why did we get the ip file as the base file?


            # The file that we are going to create needs to be locked
            new_ipb_file="$current_dir/${current_segment}_${current_quality}.mp4.IPB.h264"
            touch "$new_ipb_file"
            exec {lock_fd_IPB}<> "$new_ipb_file"
            while ! flock -xn $lock_fd_IPB; do
                sleep 0.001 # Sleep for 1 ms to try again
            done

            # Copy the concatenated file to the current directory
            cp "${temp_dir}concatenated.h264" "$new_ipb_file"

            # Unlock the file
            flock -u $lock_fd_IPB
            exec {lock_fd_IPB}>&-
        fi
    fi

    # Copy to next or prev quality directory if needed
    if [ $is_odd -eq 1 ]; then
        # Check if it does not have a IPB, mp4 and B file
        if [ ! -f "$parent_dir/$next_quality/${current_segment}_${next_quality}.mp4" ] && [ ! -f "$parent_dir/$next_quality/${current_segment}_${next_quality}.mp4.IPB.h264" ] && [ ! -f "$parent_dir/$next_quality/${current_segment}_${next_quality}.mp4.B.h264" ]; then

            # Lock both the current file and the future B file
            touch "$current_file"
            exec {lock_fd_current_file}<> "$current_file"
            while ! flock -xn $lock_fd_current_file; do
                sleep 0.001 # Sleep for 1 ms to try again
            done

            new_b_file="$parent_dir/$next_quality/${current_segment}_${next_quality}.mp4.B.h264"
            touch "$new_b_file"
            exec {lock_fd_B}<> "$new_b_file"
            while ! flock -xn $lock_fd_B; do
                sleep 0.001 # Sleep for 1 ms to try again
            done

            cp "$current_file" "$new_b_file"

            # Unlock the files
            flock -u $lock_fd_current_file
            exec {lock_fd_current_file}>&-

            flock -u $lock_fd_B
            exec {lock_fd_B}>&-
        fi
    # Check if it does not have a IPB, mp4 and B file
    elif [ ! -f "$parent_dir/$prev_quality/${current_segment}_${prev_quality}.mp4" ] && [ ! -f "$parent_dir/$prev_quality/${current_segment}_${prev_quality}.mp4.IPB.h264" ] && [ -f "$parent_dir/$prev_quality/${current_segment}_${prev_quality}.mp4.B.h264" ]; then

        # Lock both the current file and the future B file
        touch "$current_file"
        exec {lock_fd_current_file}<> "$current_file"
        while ! flock -xn $lock_fd_current_file; do
            sleep 0.001 # Sleep for 1 ms to try again
        done

        new_b_file="$parent_dir/$prev_quality/${current_segment}_${prev_quality}.mp4.B.h264"
        touch "$new_b_file"
        exec {lock_fd_B}<> "$new_b_file"
        while ! flock -xn $lock_fd_B; do
            sleep 0.001 # Sleep for 1 ms to try again
        done

        cp "$current_file" "$new_b_file"

        # Unlock the files
        flock -u $lock_fd_current_file
        exec {lock_fd_current_file}>&-

        flock -u $lock_fd_B
        exec {lock_fd_B}>&-
    fi
fi

# calculate the difference between the start time and the current time
# end_time=$(($(date +%s%N)/1000000))
# time_diff=$((end_time - start_time))
# Write it to a file in the temp directory
# echo "$time_diff" > "${temp_dir}time_diff.txt"

#if [[ "$current_file" != *.IPB.h264 ]]; then
rm -rf "$temp_dir"
#fi
# This removes the concatenated_dash.mpd file which was created by MP4Box.

if [ -f "concatenated_dash.mpd" ]; then
    rm concatenated_dash.mpd
fi