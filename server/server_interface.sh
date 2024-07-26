#!/bin/bash

id=$1

# Device interface
interface="server-eth${id}"

# If we have $2, then overwrite the interface
if [ -n "$2" ]; then
    interface="$2"
fi

# Output file
output_file="server_interface_${id}.metric.log"

# Clear previous data in the output file
> "$output_file"

# Get initial bytes
initial_bytes_received=$(grep "$interface" /proc/net/dev | awk '{print $2}')
initial_bytes_transmitted=$(grep "$interface" /proc/net/dev | awk '{print $10}')

# If the interface does not exist, then the initial bytes will be empty
if [ -z "$initial_bytes_received" ]; then
    initial_bytes_received=0
fi
if [ -z "$initial_bytes_transmitted" ]; then
    initial_bytes_transmitted=0
fi

# Loop to log bandwidth every second
while true; do
    current_datetime=$(date "+%Y-%m-%d %H:%M:%S,%3N")
    bytes_received=$(grep "$interface" /proc/net/dev | awk '{print $2}')
    bytes_transmitted=$(grep "$interface" /proc/net/dev | awk '{print $10}')
    
    if [ -n "$bytes_received" ]; then
        # Subtract initial bytes from current bytes
        bytes_received=$((bytes_received - initial_bytes_received))
        echo "$current_datetime;bytes_received;$bytes_received" >> "$output_file"

        # Create/overwrite a new file called "server_interface_${id}.received.log"
        echo "bytes_received;$bytes_received" > "server_interface_${id}.received.metrics"
    fi

    if [ -n "$bytes_transmitted" ]; then
        bytes_transmitted=$((bytes_transmitted - initial_bytes_transmitted))
        echo "$current_datetime;bytes_transmitted;$bytes_transmitted" >> "$output_file"

        # Create/overwrite a new file called "server_interface_${id}.transmitted.log"
        echo "bytes_transmitted;$bytes_transmitted" > "server_interface_${id}.transmitted.metrics"
    fi

    sleep .25
done