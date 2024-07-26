#!/bin/bash

url=$1
client_name=$2
sleep_time=$3
threaded=False
headless=$4
tli_enabled=$5
force_fake_client=$6

# Set headless to "True" if not set
if [ -z "$headless" ]; then
    headless=True
fi

# tli_enabled is either 1 or 0, we should use 0 by default
if [ -z "$tli_enabled" ]; then
    tli_enabled=0
fi


# force_fake_client is either 1 or 0, we should use 0 by default
if [ -z "$force_fake_client" ]; then
    force_fake_client=0
fi

# cd to the directory of this script
cd "$(dirname "$0")"

#echo $HOME > test.log
#echo $XAUTHORITY > test2.log
export XAUTHORITY=$HOME/.Xauthority
#echo $XAUTHORITY > test3.log
truncate -s 0 $client_name.log 2>/dev/null
truncate -s 0 $client_name.metric.log 2>/dev/null

selected_browser="client_firefox_browser.py"
# If tli is enabled, then use client_fake_browser.py
#if [ $tli_enabled -eq 1 ]; then
#    selected_browser="client_fake_browser.py"
#fi

# If force_fake_client is enabled, then use client_fake_browser.py
if [ $force_fake_client -eq 1 ]; then
    selected_browser="client_fake_browser.py"
fi


python3 -u $selected_browser $url $client_name $sleep_time $threaded $headless &>> $client_name.log &
