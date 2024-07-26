#!/bin/bash

id=$1
video_length=$2

# By default, the video length is 97 seconds
if [ -z "$video_length" ]; then
    video_length=97
fi

# cd to the directory of this script
cd "$(dirname "$0")"

cd ../proxy
echo "Starting NALU Processing..." > proxy_nalu_$id.log
python3 -u ./video_listener.py "cache_$id" "$video_length" &>> proxy_nalu_$id.log &