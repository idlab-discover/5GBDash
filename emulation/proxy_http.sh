#!/bin/bash

id=$1
server_url=$2
fec=$3
tli=$4
seg_dur=$5
n_chunks=$6


# cd to the directory of this script
cd "$(dirname "$0")"

cd ../proxy
echo "Starting HTTP proxy..." > proxy_http_$id.log
# Print all the arguments as a single string
echo "Arguments: $*" >> proxy_http_$id.log


python3 -u proxy_http.py -i $id -s $server_url -f $fec -t $tli -d $seg_dur -l $n_chunks &>> proxy_http_$id.log &
