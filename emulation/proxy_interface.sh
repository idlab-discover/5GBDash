#!/bin/bash

id=$1

# cd to the directory of this script
cd "$(dirname "$0")"

cd ../proxy
echo "Starting proxy interface measurements..." > proxy_interface_$id.log
./proxy_interface.sh $id 0 &>> proxy_interface_$id.log & # Multicast
./proxy_interface.sh $id 1 &>> proxy_interface_$id.log & # Client
./proxy_interface.sh $id 2 &>> proxy_interface_$id.log & # Unicast
