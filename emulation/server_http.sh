#!/bin/bash

# cd to the directory of this script
cd "$(dirname "$0")"

cd ../server
echo "Starting HTTP server..." > server_http.log
python3 -u server_http.py "$@" &>> server_http.log &
