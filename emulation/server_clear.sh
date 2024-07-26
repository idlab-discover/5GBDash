#!/bin/bash

index=$1

# cd to the directory of this script
cd "$(dirname "$0")"

cd ../server

# Create directory for logs if it does not exist
mkdir -p ../evaluation/logs/$index

# Move logs
mv server_http.log ../evaluation/logs/$index/server_http.log
mv server_multicast.log ../evaluation/logs/$index/server_multicast.log
mv server_interface.log ../evaluation/logs/$index/server_interface.log

# Move metrics
mv server.metrics ../evaluation/logs/$index/server.metrics
mv server_http.metric.log ../evaluation/logs/$index/server_http.metric.log
mv server_multicast.metric.log ../evaluation/logs/$index/server_multicast.metric.log
mv "server_interface_0.metric.log" "../evaluation/logs/$index/server_interface_multicast.metric.log"
mv "server_interface_1.metric.log" "../evaluation/logs/$index/server_interface_unicast.metric.log"

mv "server_interface_0.received.metrics" "../evaluation/logs/$index/server_interface_multicast.received.metrics"
mv "server_interface_0.transmitted.metrics" "../evaluation/logs/$index/server_interface_multicast.transmitted.metrics"
mv "server_interface_1.received.metrics" "../evaluation/logs/$index/server_interface_unicast.received.metrics"
mv "server_interface_1.transmitted.metrics" "../evaluation/logs/$index/server_interface_unicast.transmitted.metrics"

