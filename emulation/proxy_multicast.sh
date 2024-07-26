#!/bin/bash

id=$1
recover_url=$2
video_list=$3

# cd to the directory of this script
cd "$(dirname "$0")"

cd ../proxy
mkdir -p cache_$id
rm -rf cache_$id/*
echo "Starting multicast receiver..." > proxy_multicast_$id.log
./proxy_multicast.sh $id $recover_url $video_list &>> proxy_multicast_$id.log &
