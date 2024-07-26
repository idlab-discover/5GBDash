import os
import csv
import subprocess
import time
import argparse
import threading

# Function to apply tc settings
def apply_tc_settings(interface, bandwidth):
    qdisc = subprocess.getoutput(f"sudo tc qdisc show dev {interface}")

    if "noqueue" in qdisc or "htb" in qdisc:
        # Remove existing qdisc and class
        subprocess.run(f"sudo tc qdisc del dev {interface} root", shell=True)

    # Set qdisc
    subprocess.run(f"sudo tc qdisc add dev {interface} root handle 1: htb default 11", shell=True)
    subprocess.run(f"sudo tc class add dev {interface} parent 1: classid 1:1 htb rate {bandwidth}mbit", shell=True)
    subprocess.run(f"sudo tc class add dev {interface} parent 1:1 classid 1:11 htb rate {bandwidth}mbit", shell=True)

def apply_row(interface, bandwidth_row, time_row):
    print(f"Applying row to interface {interface}")
    offset = 0
    for bandwidth, time in zip(bandwidth_row, time_row):
        try:
            # If bandwidth is not an int, change it into an int
            if not bandwidth.isdigit():
                bandwidth = int(float(bandwidth))
            else:
                bandwidth = int(bandwidth)

            # Bandwidth is in bits per second, we want megabits per second
            bandwidth = bandwidth // 1_000_000

            apply_tc_settings(interface, bandwidth)
            print(f"Applied {bandwidth} mbit bandwidth to interface {interface}")
        except:
            print("Error applying tc settings")

        # Introduce a waiting period (in seconds) before the next iteration
        # Our time should always be around 1000 ms
        new_time = int(time) // 1_000 # Convert from milliseconds to seconds
        current_time = time.time_ns() / 1_000_000
        if new_time - offset > 0:
            time.sleep(new_time - offset)
        actual_sleep_time = (time.time_ns() / 1_000_000) - current_time
        offset =  actual_sleep_time - new_time # We will wait this much less the next time

    # the traces are done, let's set it now at 100 mbit/s
    apply_tc_settings(interface, 100)


def main(interfaces, rows):
    # Check if rows is an instance of int
    try:
        row = int(rows)
        if row <=0:
            print("Not applying traces")
            return 
        rows = [row]
    except:
        # Check if rows is an instance of str
        if isinstance(rows, str):
            # split on comma
            rows = rows.split(',')
            # convert to int
            rows = [int(row) if len(row) > 0 else 0 for row in rows]

    if len(rows) == 0:
        print("Not applying traces")
        return

    # Check if all rows are 0
    if all(row == 0 for row in rows):
        print("Not applying traces")
        return

    print((f"Applying traces to rows {rows} and interfaces {interfaces}"))
    # Define your directory and CSV file paths
    directory = './'
    bandwidth_csv_file = 'traces.csv'
    time_csv_file = 'trace_times.csv'

    # Convert the interfaces string to an array
    interfaces = interfaces.split(',')
    # Check if rows is shorter than interfaces, if so append with the first element
    while len(rows) < len(interfaces):
        rows.append(rows[0])

    # Read and process data from CSV files
    with open(os.path.join(directory, bandwidth_csv_file), 'r') as bandwidth_file, \
        open(os.path.join(directory, time_csv_file), 'r') as time_file:
        bandwidth_reader = csv.reader(bandwidth_file)
        time_reader = csv.reader(time_file)

        threads = []

        counter = 0
        for i, (bandwidth_row, time_row) in enumerate(zip(bandwidth_reader, time_reader)):
            # Check if i is in rows
            if i not in rows:
                continue

            # Create a thread for each interface
            thread = threading.Thread(target=apply_row_in_thread, args=(interfaces[counter], bandwidth_row, time_row))
            counter += 1
            
            # Start the thread
            thread.start()

            # Add the thread to the list of threads
            threads.append(thread)

        # Wait for all threads to finish
        for thread in threads:
            thread.join()

            


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Apply TC settings to specified network interfaces and row")
    parser.add_argument("--interfaces", help="Comma-separated list of network interfaces")
    parser.add_argument("--row", default="0", help="Select the row to process (default is 0)")
    args = parser.parse_args()

    try:
        main(args.interfaces, args.row)
    except:
        print("Error applying tc settings in main")