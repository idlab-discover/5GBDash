#!/bin/bash

dev=$1
rate=$2
delay=$3
loss=$4
first=$5

if [[ first -eq 1 ]]; then
	# Check if the qdisc is already set
	qdisc=$(sudo tc qdisc show dev $dev)
	if [[ "$qdisc" == *"noqueue"* ]]; then
		# remove qdisc and class
		sudo tc qdisc del dev $dev root
	elif [[ "$qdisc" == *"htb"* ]]; then
		# remove qdisc and class
		sudo tc qdisc del dev $dev root
	fi

	tc qdisc add dev $dev root handle 1: htb default 11
	tc class add dev $dev parent 1: classid 1:1 htb rate $rate
	tc class add dev $dev parent 1:1 classid 1:11 htb rate $rate
	tc qdisc add dev $dev parent 1:11 handle 10: netem delay $delay loss $loss
else
	tc class change dev $dev parent 1: classid 1:1 htb rate $rate
	tc class change dev $dev parent 1:1 classid 1:11 htb rate $rate
	tc qdisc change dev $dev parent 1:11 handle 10: netem delay $delay loss $loss
fi
