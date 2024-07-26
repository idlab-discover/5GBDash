import argparse
import time
import os
import subprocess
import math
import ctypes
import threading

from metric import Gauge
from interface import get_total_bytes_tx

LIBFLUTE_SENDER_PATH = '../flute/build/examples/libflute_sender.so'

# Setup FLUTE sender
libflute_sender = ctypes.CDLL(LIBFLUTE_SENDER_PATH)
flute_setup = libflute_sender.setup # Obtain the library function
flute_setup.argtypes = [ctypes.c_uint16, ctypes.POINTER(ctypes.c_char_p)]

flute_set_thread_name = libflute_sender.set_thread_name # Obtain the library function
flute_set_thread_name.argtypes = [ctypes.c_char_p] # Set the argument types

flute_send_file = libflute_sender.send_file # Obtain the library function
flute_send_file.restype = ctypes.c_uint64 # Set the return type
flute_send_file.argtypes = [ctypes.c_char_p, ctypes.c_uint64] # Set the argument types

flute_set_rate_limit = libflute_sender.set_rate_limit # Obtain the library function
flute_set_rate_limit.restype = ctypes.c_int # Set the return type
flute_set_rate_limit.argtypes = [ctypes.c_uint32] # Set the argument types

flute_current_total_file_size = libflute_sender.current_total_file_size # Obtain the library function
flute_current_total_file_size.restype = ctypes.c_uint64 # Set the return type
flute_current_total_file_size.argtypes = [] # Set the argument types

start_time = time.time_ns() / 1_000_000 # [ms]
print(start_time)

base_dir = "content"
videos = ["alpha"]
seg_dur = 4
rep_ids = ['5']
rep_extensions = ['.mp4']
sleep_time = 18
fec = 0
low_latency_chunks = 0 # Number of low latency chunks per segment
disabled = 0
sender_has_started = False
log_level = 2

# Global variables
interface = "server-eth0"
# How many ms before the end of a segment that it should be received.
max_deadline_ms_before_segment_end = 500 # ms
max_rate = 100 * 1000 # [kbps]
size_multiplier = 1
send_threads = []
threads_array_lock = threading.Lock()

METRIC_LOG_FILE = "server_multicast.metric.log"
total_file_bytes_mc = Gauge('total_file_bytes_mc','Total number of file bytes that need to be send over mc, incl FEC overhead', METRIC_LOG_FILE)
total_bytes_mc_interface = Gauge('total_bytes_mc_interface','Total number of bytes send over mc interface', METRIC_LOG_FILE)
is_multicasting = Gauge('is_multicasting','Wether or not the server is multicasting', METRIC_LOG_FILE)

def get_deadline_ms_before_segment_end(segment_duration, fec, low_latency_chunks, high_loss = True):
    segment_or_chunk_duration = segment_duration / max(1, low_latency_chunks) * 1.0

    # The key is the threshold in seconds, this is the real deadline for when the file should be received and handled by the client.
    # The value is a tuple with the deadline for high loss and low loss in ms
    thresholds = {
        0.25: (50, 50),
        0.3: (190, 190) if fec else (190, 190),
        0.5: (250, 150) if fec else (250, 150),
        1.0: (400, 350) if fec else (400, 300),
        4.0: (1000, 1000) if fec else (600, 600),
    }

    for threshold, values in thresholds.items():
        if segment_or_chunk_duration <= threshold:
            return values[1] if high_loss else values[0]

    return 1000

def log_byte_count(start_amount = 0):
    global interface
    end_amount = get_total_bytes_tx(interface)
    total_bytes_sent = end_amount - start_amount
    total_bytes_mc_interface.set(total_bytes_sent)

def setup_sender(instance_id_start = 1, toi_start = 1, rate = 1000, fec = 0, log_level = 2):
    argv = [LIBFLUTE_SENDER_PATH, "-m", "239.0.0.1", "-p", "16000", "-r", str(rate), "-l", str(log_level), "-o", str(toi_start) , "-i", str(instance_id_start) ,"-d", str(0), "-f", str(fec)]
    argv_encoded = [arg.encode('utf-8') for arg in argv]
    argv_c = (ctypes.c_char_p * len(argv_encoded))(*argv_encoded)
    libflute_sender.setup(ctypes.c_uint16(len(argv)), argv_c) # Setup FLUTE sender

def start_sender():
    global sender_has_started
    if sender_has_started:
        return
    sender_has_started = True
    print("Starting FLUTE sender...")
    libflute_sender.start() # Start FLUTE sender

def stop_sender():
    global sender_has_started
    if not sender_has_started:
        return
    sender_has_started = False
    print("Stopping FLUTE sender...")
    libflute_sender.stop() # Stop FLUTE sender

def clear_sender():
    libflute_sender.clear_files.restype = ctypes.c_uint64 # Set the return type
    unsend_files = libflute_sender.clear_files() # Clear FLUTE sender
    if unsend_files > 0:
        print(f"Unsend files: {unsend_files}")

def set_rate_limit(rate):
    if rate > max_rate:
        rate = max_rate # Limit the rate
    if rate < 0:
        rate = 0 # Disable rate limiting
    flute_set_rate_limit(ctypes.c_uint32(math.ceil(rate)))

def send_files(file_list, segment_or_chunk_duration, stop_current_transmission = False):
    global max_deadline_ms_before_segment_end
    global disabled

    if file_list is None or len(file_list) == 0:
        return

    flute_set_thread_name(file_list[0].encode('utf-8'))
    # print(file_list[0])

    # Get the current time in nanoseconds since the epoch and convert to milliseconds
    current_time_ms = int(time.time_ns() / 1_000_000)
    #print(f"Current time (ms): {current_time_ms}")

    # Calculate the segment duration in milliseconds and adjust it by subtracting the max deadline offset
    adjusted_segment_duration_ms = round(segment_or_chunk_duration * 1000 - max_deadline_ms_before_segment_end)
    #print(f"Segment duration (ms) adjusted: {adjusted_segment_duration_ms}")

    # Calculate the deadline by adding the adjusted segment duration to the current time
    deadline = current_time_ms + adjusted_segment_duration_ms

    # Print the calculated deadline
    #print(f"Deadline: {deadline}")


    #deadline += 4000 # Add 4 seconds to the deadline, just temporary
    
    # If disabled is 1, we don't send the files over multicast
    if disabled == 0:

        if stop_current_transmission:
            # Stop unfinished files from being transmitted, otherwise they would claim bandwidth that the new file transmission needs.
            clear_sender()

        # Send the files over multicast using libflute_sender, we don't await the transmission, the lib will handle that for us
        for file in file_list:
            flute_send_file(file.encode('utf-8'), ctypes.c_uint64(deadline))

def thread_loop(thead_lock, base_dir, rep_ids, rep_extensions, seg_dur, sleep_time, videos, thread_number, thread_start_time, low_latency_chunks):
    thread_id = threading.current_thread().ident

    video = videos[thread_number]
    sleep_time = sleep_time[thread_number]
    rep_id = rep_ids[thread_number]
    rep_extension = rep_extensions[thread_number]

    segment_or_chunk_duration = seg_dur / max(1, low_latency_chunks) * 1.0 # [s]

    print(f"Thread {thread_id} started for video: {video}.\r\nSegment or chunk duration is {segment_or_chunk_duration}.\r\nSleeping for {sleep_time} seconds...\r\n")
    time.sleep(sleep_time)
    # Remove the sleep time from the calculation from now on
    thread_start_time += sleep_time * 1000
    # Add a small offset to the thread start time, so that the threads don't start at the same time
    thread_start_time += ((thread_number - (thread_number % seg_dur)) / seg_dur) % 99 # 0-99 [ms]
    
    # Mark our multicasting as started
    is_multicasting.set(1)

    # If disabled is 1, we don't send the files over multicast
    if disabled == 0:
        start_sender() # Start FLUTE sender

    flute_set_thread_name(f"{rep_id}/{video}{rep_extension}".encode('utf-8'))


    def get_next_segment(video, seg_dur, rep_id, rep_extension, segment_id, video_alternative):
        # Returns a tuple with the segment filename and the size of the file
        file = base_dir + "/" + video + "/" + str(seg_dur) + "/" + str(rep_id) + "/segment_" + "{:04d}_".format(int(segment_id)) + str(rep_id) + rep_extension

        # Check if the file exists
        if os.path.isfile(file):
            size = os.path.getsize(file)
            return (file, size)

        if video_alternative is None or video_alternative == "":
            return (None, 0)

        # The file does not exist in this directory, try with our hacky way
        file_real = base_dir + "/" + video_alternative + "/" + str(seg_dur) + "/" + str(rep_id) + "/segment_" + "{:04d}_".format(int(segment_id)) + str(rep_id) + rep_extension
        # Does the file exist now?
        if os.path.isfile(file_real):
            size = os.path.getsize(file_real)
            return (file, size)

        return (None, 0)


    def get_next_chunk(video, seg_dur, rep_id, rep_extension, segment_id, chunk_id, video_alternative):
        sub_dir = "/{:05d}".format(int(segment_id)) if "f4s" in rep_extension else ""
        chunk_id_text = "_" + str(chunk_id) if "f4s" in rep_extension else ""

        # Returns a tuple with the segment filename and the size of the file
        file = base_dir + "/" + video + "/" + str(seg_dur) + "/" + str(rep_id) + sub_dir + "/chunk_" + "{:05d}_".format(int(segment_id)) + str(rep_id) + chunk_id_text + rep_extension


        # Check if the file exists
        if os.path.isfile(file):
            size = os.path.getsize(file)
            return (file, size)


        if video_alternative is None or video_alternative == "":
            return (None, 0)
        

        # The file does not exist in this directory, try with our hacky way
        file_real = base_dir + "/" + video_alternative + "/" + str(seg_dur) + "/" + str(rep_id) + sub_dir + "/chunk_" + "{:05d}_".format(int(segment_id)) + str(rep_id) + chunk_id_text + rep_extension


        # Does the file exist now?
        if os.path.isfile(file_real):
            size = os.path.getsize(file_real)
            return (file, size)

        return (None, 0)

        
    def get_next_file(video, seg_dur, rep_id, rep_extension, segment_id, chunk_id, video_alternative, cmaf_enabled):
        if cmaf_enabled or "m4s" in rep_extension:
            return get_next_chunk(video, seg_dur, rep_id, rep_extension, segment_id, chunk_id, video_alternative)
        return get_next_segment(video, seg_dur, rep_id, rep_extension, segment_id, video_alternative)

        
    def get_next_segment_id_and_chunk_id(current_segment_id, current_chunk_id, low_latency_chunks):
        # Returns a tuple with the segment id and the chunk id
        if low_latency_chunks > 0:
            # We are using CMAF, so we need to return the next chunk id
            current_chunk_id += 1
            if current_chunk_id > low_latency_chunks:
                # We have reached the end of the chunks, so we need to return the next segment id
                current_chunk_id = 1
                current_segment_id += 1
        else:
            # We are not using CMAF, so we need to return the next segment id
            current_segment_id += 1
        return (current_segment_id, current_chunk_id)


    # Send the segments
    segment_id = 1
    chunk_id = 1
    while True:
        total_size = 0 # [bytes]
        # Split so, we can do a little video duplication trick
        video_parts = video.split("_")
        # Get the segment file
        selected_file, selected_size = get_next_file(video, seg_dur, rep_id, rep_extension, segment_id, chunk_id, video_parts[0] if len(video_parts) > 1 else None, low_latency_chunks > 1)

        if selected_size > 0:
            # Create a thread for the function
            thread = threading.Thread(target=send_files, args=([selected_file], segment_or_chunk_duration))

            # Start the thread
            thread.start()

            # Save the thread in our array
            with threads_array_lock:
                send_threads.append(thread)

            # Calculate offset
            current_time = time.time_ns() / 1_000_000 # [ms]
            offset = max(0, current_time - thread_start_time)
            offset = (offset % (segment_or_chunk_duration * 1000)) / 1000  # [s]
            print("Waiting for " + str(segment_or_chunk_duration - offset) + " seconds...")
            # Sleep for the remaining time
            if segment_or_chunk_duration - offset > 0:
                time.sleep(segment_or_chunk_duration - offset)

            # Keep track of the total amount of bytes that we are sending over multicast
            if disabled == 0:
                total_file_bytes_mc.inc(selected_size)

            # Increase segment_id
            # Get the next segment id and chunk id
            segment_id, chunk_id = get_next_segment_id_and_chunk_id(segment_id, chunk_id, low_latency_chunks)
        else:
            break

def run_multicast(base_dir, rep_ids, rep_extensions, seg_dur, sleep_times, videos, fec, low_latency_chunks, log_level):
    global start_time
    global size_multiplier

    flute_set_thread_name("python3: server_multicast : main".encode('utf-8'))
    
    rate = max_rate # [kbps]
    setup_sender(instance_id_start = 1, toi_start = 1, rate = rate, fec = fec, log_level = log_level)

    # Get the amount of bytes send over the interface, this will be used to calculate the total bytes send over the interface
    start_amount = get_total_bytes_tx(interface)
    total_bytes_mc_interface.set(0)

    thread_lock = threading.Lock()

    threads = [threading.Thread(target=thread_loop, args=(thread_lock, base_dir, rep_ids, rep_extensions, seg_dur, sleep_times, videos, n, start_time, low_latency_chunks)) for n in range(len(videos))]

    # Start all threads
    for thread in threads:
        thread.start()

    # Check if any of the threads are still running
    while any([thread.is_alive() for thread in threads]):
        # Get one send_thread if there is one, otherwise wait for 1 second. We do this while holding the lock as short as possible.
        thread_to_join = None
        with threads_array_lock:
            if len(send_threads) > 0:
                thread_to_join = send_threads.pop(0)
        if thread_to_join is not None:
            # print(f"Waiting for thread {thread_to_join.ident} to finish...")
            thread_to_join.join()
        else:
            time.sleep(0.5)

    # Wait for all threads to complete
    for thread in threads:
        thread.join()

    log_byte_count(start_amount)
    is_multicasting.set(1)
    print("All segments have been queued for sending.")

    clear_sender() # Clear FLUTE sender
    # If disabled is 1, we don't send the files over multicast
    if disabled == 0:
        stop_sender() # Stop FLUTE sender

    time_since_start = (time.time_ns() / 1_000_000) - start_time
    print(f"Multicasting took {time_since_start} ms.")
    # Mark our multicasting as ended
    is_multicasting.set(0)
    log_byte_count(start_amount)
    print("All videos sent over multicast, terminating...")
    time.sleep(1)
    is_multicasting.set(0)
    log_byte_count(start_amount)

if __name__ == "__main__":
    # Extract options for base directory, video, quality or segment duration
    parser = argparse.ArgumentParser(description="Send files over multicast using flute_sender")
    parser.add_argument("-i", "--interface", help="The main interface through which client traffic passes", default="server-eth0")
    parser.add_argument("-b", "--base_dir", help="base directory", default="content")
    parser.add_argument("-q", "--rep_ids", help="quality (comma seperated)", default='5')
    parser.add_argument("-e", "--rep_extensions", help="extensions (comma seperated)", default='.mp4')
    parser.add_argument("-s", "--seg_dur", help="segment duration", type=int, default=4)
    parser.add_argument("-t", "--sleep_time", help="sleep time  (comma seperated)", default=18)
    parser.add_argument("-v", "--videos", help="videos (comma seperated)", default="alpha")
    parser.add_argument("-f", "--fec", help="fec", type=int, default=0)
    parser.add_argument("-l", "--low_latency_chunks", help="Number of CMAF Chunks per segment", type=int, default=0)
    parser.add_argument("-d", "--disabled", help="disable multicast", type=int, default=0)
    parser.add_argument("-r", "--max_rate", help="max rate (Mb/s)", type=int, default=100)
    args = parser.parse_args()

    if args.interface:
        interface = args.interface
    if args.base_dir:
        base_dir = args.base_dir
    if args.rep_ids:
        rep_ids = args.rep_ids.split(",")
    if args.rep_extensions:
        rep_extensions = args.rep_extensions.split(",")
    if args.seg_dur:
        seg_dur = args.seg_dur
    if args.sleep_time:
        sleep_time = args.sleep_time
    if args.videos:
        # check if args.videos is a string
        if isinstance(args.videos, str):
            videos = args.videos.split(",")
    if args.fec:
        fec = args.fec
        if fec > 0:
            size_multiplier = 1.15 # Increase the size of the files by 15% to account for the FEC overhead
    if args.low_latency_chunks:
        low_latency_chunks = args.low_latency_chunks
    if args.disabled:
        disabled = args.disabled
    if args.max_rate and args.max_rate > 0:
        max_rate = args.max_rate * 1000

    # Convert to list
    if isinstance(sleep_time, str) and ',' in sleep_time:
        sleep_time = sleep_time.split(',')
    else:
        sleep_time = [sleep_time]
    # Convert to int
    if len(sleep_time) > 0 and isinstance(sleep_time[0], str):
        sleep_time = [int(n) for n in sleep_time]
    # If the length of the list is shorter then the amount of videos, then we need to add some values (take the first value)
    while len(sleep_time) < len(videos):
        sleep_time.append(sleep_time[0])
    while len(rep_ids) < len(videos):
        rep_ids.append(rep_ids[0])
    while len(rep_extensions) < len(videos):
        rep_extensions.append(rep_extensions[0])

    print(args)

    max_deadline_ms_before_segment_end = get_deadline_ms_before_segment_end(seg_dur, fec, low_latency_chunks)

    run_multicast(base_dir, rep_ids, rep_extensions, seg_dur, sleep_time, videos, fec, low_latency_chunks, log_level)

    print("All videos sent over multicast, terminating...")

