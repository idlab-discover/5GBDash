#!/bin/bash

id=0
base_dir="../server/content"
videos="alpha,avatar"
seg_dur=4
rep_ids='5'


# extract options for base directory, video, quality or segment duration
while getopts "b:q:s:v:i:" opt; do
  case $opt in
    b) base_dir=${OPTARG};;
    q) rep_ids=${OPTARG};;
    s) seg_dur=${OPTARG};;
    v) videos=${OPTARG};;
    i) id=${OPTARG};;
    *)
        echo 'Error in command line parsing' > proxy_static_$id.log
        exit 1
  esac
done
shift $((OPTIND-1))


# cd to the directory of this script
cd "$(dirname "$0")"

cd ../proxy
sleep 1
echo "Copying static files to proxy..." > proxy_static_$id.log

# Create a video array from the videos string
IFS=',' read -r -a videos <<< "$videos"
# Print the videos array
echo ${videos[@]} >> proxy_static_$id.log

# Function to copy file to cache directory
copy_to_cache() {
  local file="$1"

  # Array to store matching elements
  matching_elements=()
  current_video=""

  # Remove base_dir from the file path
  path=${file#$base_dir/}
  # Remove the file name from the path
  path_temp=${path%/*}
  # If the path equals to path_temp, then the file is in the base directory
  if [ "$path" == "$path_temp" ]; then
    path=""
  else
    path=$path_temp
    # Get the first part of the path (the video name)
    current_video=${path%%/*}
    # Loop through the array
    for element in "${videos[@]}"; do
        # Check if the element starts with $video and is followed by an underscore and any characters
        if [[ "$element" == "$current_video"_* ]]; then
            matching_elements+=("$element")
        fi
    done
  fi  

  # echo "Copying $file to cache_$id/$path" >> "proxy_static_$id.log"
  # Create the directory structure in the proxy
  mkdir -p "cache_$id/$path"
  # Copy the file to the proxy
  cp "$file" "cache_$id/$path"

  # If $matching_elements is not empty, then copy the file to the cache directories of the other videos
  for matching_element in "${matching_elements[@]}"; do
    # Replace the current video name with the selected duplication video name
    new_path=${path/$current_video/$matching_element}
    # echo "Copying $file to cache_$id/$new_path" >> "proxy_static_$id.log"
    # Create the directory structure in the proxy
    mkdir -p "cache_$id/$new_path"

    # Get the file name from the path
    filename=${file##*/}

    # Check if the file does not exists yet in the proxy
    if [ ! -f "cache_$id/$new_path/$filename" ]; then
      # Copy the file to the proxy
      cp "$file" "cache_$id/$new_path/$filename"
    fi

    # echo "Copying $file to cache_$id/$new_path" >> "proxy_static_$id.log"

    # If the file is an mpd file and it ends with _extra.mpd where $extra is the last part of the matching_element, then also copy it as .mpd without the _extra part
    if [[ "$filename" == *.mpd ]] && [[ "$filename" == *_${matching_element##*_}.mpd ]]; then
      # Replace _${matching_element##*_}.mpd with .mpd
      filename=${filename/_${matching_element##*_}.mpd/.mpd}
      echo "Copying $file to cache_$id/$new_path/$filename" >> "proxy_static_$id.log" 
      # Copy the file to the proxy
      cp "$file" "cache_$id/$new_path/$filename"
    fi
    
  done

}

# Create the cache directory for this proxy
mkdir -p cache_$id

# Get website scripts, stylesheets, images, etc.
for file in "$base_dir"/*; do
	# Check if the current item is a file (not a directory)
  if [ -f "$file" ]; then
		copy_to_cache "$file"
	fi
done

# Search for all init and mpd files
find "$base_dir" -type f | sort | while IFS= read -r file; do
  # Check if the current item is a file (not a directory)
  if [ -f "$file" ]; then
    # Check if the file ends with .mpd
    if [[ $file == *.mpd ]]; then
      copy_to_cache "$file"
    # Else, check if file ends with init_x.mp4, where x could be a number
    elif [[ $file == *init_[0-9]*.mp4 ]]; then
      copy_to_cache "$file"
    # Else, check if file ends with init_x.m4s, where x could be a number
    elif [[ $file == *init_[0-9]*.m4s ]]; then
      copy_to_cache "$file"
    fi
  fi
done 
