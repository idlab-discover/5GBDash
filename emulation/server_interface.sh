#!/bin/bash

# cd to the directory of this script
cd "$(dirname "$0")"

cd ../server
echo "Starting proxy interface measurements..." > server_interface.log
./server_interface.sh 0 &>> server_interface.log & # Multicast
./server_interface.sh 1 &>> server_interface.log & # Unicast
