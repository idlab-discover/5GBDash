#!/bin/bash

id=$1
index=$2

# cd to the directory of this script
cd "$(dirname "$0")"

cd ../proxy
rm -rf ./cache_$id

# Create directory for logs if it does not exist
mkdir -p ../evaluation/logs/$index

# Move logs
mv proxy_http_$id.log ../evaluation/logs/$index/proxy_http_$id.log
mv proxy_multicast_$id.log ../evaluation/logs/$index/proxy_multicast_$id.log
mv proxy_interface_$id.log ../evaluation/logs/$index/proxy_interface_$id.log
mv proxy_nalu_$id.log ../evaluation/logs/$index/proxy_nalu_$id.log
mv proxy_static_$id.log ../evaluation/logs/$index/proxy_static_$id.log

# Move metrics
mv proxy_http_$id.metric.log ../evaluation/logs/$index/proxy_http_$id.metric.log
mv proxy_multicast_cache_$id.metric.log ../evaluation/logs/$index/proxy_multicast_$id.metric.log
mv "proxy_interface_${id}_0.metric.log" "../evaluation/logs/$index/proxy_interface_multicast_${id}.metric.log"
mv "proxy_interface_${id}_1.metric.log" "../evaluation/logs/$index/proxy_interface_client_${id}.metric.log"
mv "proxy_interface_${id}_2.metric.log" "../evaluation/logs/$index/proxy_interface_unicast_${id}.metric.log"