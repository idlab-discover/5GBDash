#!/bin/bash

# cd to the directory of this script
cd "$(dirname "$0")"

cd ../server
echo "Starting multicast server..." > server_multicast.log
truncate -s 0 server_multicast.metric.log 2>/dev/null
./server_multicast.sh "$@" &>> server_multicast.log &
