#!/bin/bash

id=$1
index=$2

# cd to the directory of this script
cd "$(dirname "$0")"

# Check if the log exists, if not, then exit
if [ ! -f client_$id.log ]; then
    echo "Log file does not exist"
    exit 0
fi

# Write to the log that the experiment is stopped, this is appended to the log
echo "Experiment stopped" >> client_$id.log

# Create directory for logs if it does not exist
mkdir -p ../evaluation/logs/$index

# Move logs
mv client_$id.log ../evaluation/logs/$index/client_$id.log

# Move metrics
mv client_$id.metric.log ../evaluation/logs/$index/client_$id.metric.log

mv client_$id.metrics ../evaluation/logs/$index/client_$id.metrics