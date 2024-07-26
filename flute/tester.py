import ctypes
import random
import string
import shutil
import os
import time
import math
import concurrent.futures
import numpy as np

LIBFLUTE_TESTER_PATH = "./build/examples/libflute_tester.so"
DEFAULT_EXPERIMENT_FILE_PATH = "/dev/shm/flute_tester/"

class LibFlute:
    def __init__(self, lib_path):
        self.lib_path = lib_path
        self.lib = ctypes.CDLL(lib_path)

        self.has_started = False

        self.flute_setup = self.lib.setup
        self.flute_setup.argtypes = [ctypes.c_uint16, ctypes.POINTER(ctypes.c_char_p)]

        self.flute_set_thread_name = self.lib.set_thread_name
        self.flute_set_thread_name.argtypes = [ctypes.c_char_p]

        self.flute_start = self.lib.start
        self.flute_start.argtypes = []

        self.flute_stop = self.lib.stop
        self.flute_stop.argtypes = []

        self.flute_latest_bandwidth = self.lib.latest_bandwidth
        self.flute_latest_bandwidth.restype = ctypes.c_uint64
        self.flute_latest_bandwidth.argtypes = []

        self.flute_send_file = self.lib.send_file
        self.flute_send_file.restype = ctypes.c_int
        self.flute_send_file.argtypes = [ctypes.c_char_p, ctypes.c_uint64, ctypes.c_char_p, ctypes.c_bool]

        self.flute_add_stream = self.lib.add_stream
        self.flute_add_stream.restype = ctypes.c_bool
        self.flute_add_stream.argtypes = [ctypes.c_uint32, ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32]

        self.flute_send_to_stream = self.lib.send_to_stream
        self.flute_send_to_stream.restype = ctypes.c_int
        self.flute_send_to_stream.argtypes = [ctypes.c_uint32, ctypes.c_char_p, ctypes.c_uint32]
        
        self.flute_set_rate_limit = self.lib.set_rate_limit
        self.flute_set_rate_limit.restype = ctypes.c_int
        self.flute_set_rate_limit.argtypes = [ctypes.c_uint32]
        
        self.flute_current_total_file_size = self.lib.current_total_file_size
        self.flute_current_total_file_size.restype = ctypes.c_uint64
        self.flute_current_total_file_size.argtypes = []
        
        self.flute_retrieve = self.lib.retrieve
        self.flute_retrieve.restype = ctypes.c_ulong
        self.flute_retrieve.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
        
        self.flute_symbol_count = self.lib.symbol_count
        self.flute_symbol_count.restype = ctypes.c_uint64
        self.flute_symbol_count.argtypes = [ctypes.c_char_p]
        
        self.flute_length = self.lib.length
        self.flute_length.restype = ctypes.c_uint64
        self.flute_length.argtypes = [ctypes.c_char_p]

    # Wrapper function for setup
    def setup(self, arguments: list):
        argv = [ctypes.c_char_p(arg.encode('utf-8')) for arg in arguments]
        argv.insert(0, ctypes.c_char_p(self.lib_path.encode('utf-8')))
        argv_c = (ctypes.c_char_p * len(argv))(*argv)

        return self.flute_setup(ctypes.c_uint16(len(argv)), argv_c)

    # Wrapper function for start
    def start(self):
        if self.has_started:
            return -1
        self.has_started = True
        self.flute_start()
        return 0

    # Wrapper function for stop
    def stop(self):
        if not self.has_started:
            return -1
        self.has_started = False
        self.flute_stop()
        return 0
    
    # Wrapper function for set_thread_name
    def set_thread_name(self, name):
        return self.flute_set_thread_name(name.encode('utf-8'))

    def latest_bandwidth(self):
        return self.flute_latest_bandwidth()
    
    # Wrapper function for send_file
    def send_file(self, filename: str, deadline: int, content_type="application/octet-stream", wait_for_reception=False) -> int:
        return self.flute_send_file(filename.encode('utf-8'), ctypes.c_uint64(deadline), content_type.encode('utf-8'), ctypes.c_bool(wait_for_reception))

    # Wrapper function for add_stream
    def add_stream(self, stream_id: int, content_type: str ="application/octet-stream", max_source_block_length: int = 1, file_length: int = 100) -> bool:
        return self.flute_add_stream(ctypes.c_uint32(stream_id), content_type.encode('utf-8'), ctypes.c_uint32(max_source_block_length), ctypes.c_uint32(file_length))

    # Wrapper function for send_to_stream
    def send_to_stream(self, stream_id: int, content: str) -> int:
        return self.flute_send_to_stream(ctypes.c_uint32(stream_id), content.encode('utf-8'), ctypes.c_uint32(len(content)))
    
    # Wrapper function for set_rate_limit
    def set_rate_limit(self, limit):
        limit = int(limit)
        return self.flute_set_rate_limit(ctypes.c_uint32(limit))

def get_random_str(length):
    if length <= 0:
        return ""
    return ''.join(random.choice(string.ascii_letters) for i in range(length))

def get_looped_alphabet(length):
    if length <= 0:
        return ""
    # This function generates a-z and loops back to a
    return ''.join([chr(97 + (i % 26)) for i in range(length)])

def create_file(length, path=DEFAULT_EXPERIMENT_FILE_PATH, generator=get_random_str):
    try:
        # Create the directory if it does not exist
        os.makedirs(path, exist_ok=True)
        # Create a random filename = 10 random characters + .txt
        filename = path + get_random_str(10) + ".txt"
        with open(filename, 'w') as f:  # Specify mode 'w' for writing
            # Write random ascii characters to the file
            f.write(generator(length))

        return filename
    except OSError as e:
        print(f"Error: {e}")

    return ""

def delete_file(filename):
    if len(filename) == 0:
        return False
    try:
        os.remove(filename)
        return True
    except OSError as e:
        print(f"Error: {e}")

    return False

def get_all_files_in_directory(directory=DEFAULT_EXPERIMENT_FILE_PATH):
    if not os.path.exists(directory):
        return []
    return [f for f in os.listdir(directory) if os.path.isfile(os.path.join(directory, f))]


def get_deadline(duration_s, negative_offset_ms=0, positive_offset_ms=0):
    return int(time.time_ns() / 1_000_000 + round(duration_s * 1000 - negative_offset_ms + positive_offset_ms)) # [ms]

def has_deadline_passed(deadline):
    if deadline == 0:
        return True
    return time.time_ns() / 1_000_000 > deadline

def transmission_time(file_size, rate_limit):
    """Calculates the time in seconds to transmit a file.

    Args:
    file_size: The size of the file in bytes.
    rate_limit: The rate limit in bits per second.

    Returns:
    The time in seconds to transmit the file.
    """

    # Convert rate limit to Megabytes per second
    rate_limit_bps = rate_limit / 8.0

    # Calculate transmission time in seconds
    return file_size / rate_limit_bps


# Run main
if __name__ == "__main__":
    try:
        lib = LibFlute(LIBFLUTE_TESTER_PATH)
        fec = 0
        mtu = 1500
        rate_limit = 100 * 1000 # 100 Mbps
        log_level = 0
        loss_percentage = 0
        lib.setup(["-f", f"{fec}", "-t", f"{mtu}", "-r", f"{rate_limit}", "-l", f"{log_level}", "-o", f"{round(loss_percentage)}"])
        lib.start()
        time.sleep(1)

        stream_tests = False

        # Number of threads to use and thus the number of files to send concurrently
        num_threads = 1 # 3
        num_transmits_per_thread_per_test = 3
        file_sizes = [1, 10, 1_00, 1_000, 10_000, 100_000, 1_000_000, 10_000_000, 100_000_000] if not stream_tests else [50] # [bytes]
        message_size = ['equal'] if not stream_tests else [26] # [bytes]

        generator = get_random_str if not stream_tests else get_looped_alphabet

        def transmit_files(thread_number=0, file_size=0, message_size=0, generator=get_random_str):
            lib.set_thread_name(f"transmit_files_thread_{thread_number}")
            bandwidth_array = []

            if file_size <= 0:
                return 0

            for i in range(num_transmits_per_thread_per_test):
                filename = generator(file_size, path=DEFAULT_EXPERIMENT_FILE_PATH)
                if len(filename) == 0:
                    print(f"File could not be created.")
                    continue
                if not os.path.exists(filename):
                    print(f"File {filename} does not exist.")
                    continue
                # Read the file to obtain the size
                f_size = os.path.getsize(filename) # [bytes]
                # Check if f_size is equal to file_size
                if f_size != file_size:
                    print(f"File size {f_size} is not equal to file_size {file_size}.")
                    delete_file(filename)
                    continue

                # Caculate how many s it will take to send the file (float)
                time_to_send_s = transmission_time(f_size, rate_limit * 1_000) # [s]
                print(f"File size: {f_size} bytes, Transmission time: {time_to_send_s} + 0.250 s")
                # Calculate the deadline
                deadline = get_deadline(time_to_send_s, negative_offset_ms=0, positive_offset_ms=250) if loss_percentage > 0.0 else 0
                toi = lib.send_file(filename, deadline, content_type="", wait_for_reception=True)
                if toi > 0:
                    latest_bandwidth = lib.latest_bandwidth()
                    bandwidth_array.append(latest_bandwidth)

                # check if the filename starts with DEFAULT_EXPERIMENT_FILE_PATH
                if filename.startswith(DEFAULT_EXPERIMENT_FILE_PATH):
                    if not delete_file(filename):
                        # If the file could not be deleted, stop the test to avoid filling up the disk
                        print(f"File {filename} could not be deleted.")
                        break

            average_bandwidth = (sum(bandwidth_array) / len(bandwidth_array)) if len(bandwidth_array) > 0 else 0
            return average_bandwidth


        def transmit_over_stream(thread_number=0, file_size=0, message_size=0, generator=get_random_str):
            lib.set_thread_name(f"transmit_over_stream_thread_{thread_number}")
            bandwidth_array = []
            stream_id = thread_number + 1

            if file_size <= 0 or message_size <= 0:
                print(f"File size or message size is less than or equal to 0.")
                return 0

            if not lib.add_stream(stream_id, file_length=file_size):
                print(f"Stream {stream_id} could not be added.")
                return 0

            for i in range(num_transmits_per_thread_per_test):
                # Create a random string of message_size
                message = f"M{i} "
                message = message + generator(message_size - len(message))
                print(f"Message size: {len(message)} bytes.")
                print(message)
                # Add the message to the stream
                pushed_length = lib.send_to_stream(stream_id, message) > 0

                if pushed_length > 0:
                    latest_bandwidth = lib.latest_bandwidth()
                    bandwidth_array.append(latest_bandwidth)

                    time_to_send_s = transmission_time(pushed_length, rate_limit * 1_000) # [s]
                    # Wait for the transmission to complete
                    time.sleep(time_to_send_s)


            average_bandwidth = (sum(bandwidth_array) / len(bandwidth_array)) if len(bandwidth_array) > 0 else 0
            return average_bandwidth

        test_fn = transmit_over_stream if stream_tests else transmit_files

        # A nested dict of file_size -> message_size -> bandwidth
        bandwidth_dict = {}
        for f_i, f_sizes in enumerate(file_sizes):
            for m_i, m_size in enumerate(message_size):
                print(f"File size: {f_sizes} bytes, Message size: {m_size} bytes.")
                # Create a ThreadPoolExecutor
                with concurrent.futures.ThreadPoolExecutor(max_workers=num_threads) as executor:
                    test_id = (f_i * len(file_sizes) + m_i) * num_threads - num_threads + 1
                    # Submit the transmit_files function to run concurrently
                    futures = [executor.submit(test_fn, thread_number=thread_number + test_id, file_size=f_sizes, message_size=m_size, generator=generator) for thread_number in range(num_threads)]

                    results = []

                    # Wait for all futures to complete
                    for future in concurrent.futures.as_completed(futures):
                        # Retrieve any exceptions raised during execution
                        try:
                            result = future.result()
                            if result > 0:
                                results.append(result)
                        except Exception as e:
                            print(f"Exception: {e}")

                    average_bandwidth = (sum(results) / len(results)) if len(results) > 0 else 0
                    print(f"Average bandwidth: {average_bandwidth} kbps for file size: {f_sizes} bytes and message size: {m_size} bytes.")
                    # Check if the file_size key exists in the dict
                    if f_sizes not in bandwidth_dict:
                        bandwidth_dict[f_sizes] = {}
                    bandwidth_dict[f_sizes][m_size] = average_bandwidth

            time.sleep(10)

        print(bandwidth_dict)

    except KeyboardInterrupt:
        print("KeyboardInterrupt")
        all_files = get_all_files_in_directory()
        for f in all_files:
            delete_file(f)
    except Exception as e:
        print(f"Exception: {e}")
        all_files = get_all_files_in_directory()
        for f in all_files:
            delete_file(f)


    #lib.send_file("test.abc_large.txt", 0)
    #lib.send_file("test.abc_large.txt", 0)
    #lib.send_file("test.abc_large.txt", 0)
    #lib.send_file("test.abc.txt", 0)
    #lib.send_file("tester.py", 0)

    # Create a file in shared memory (on ramdisk)
    #lib.send_file("/dev/shm/test.abc.txt", 0)
    # The file should have a random name and a random content of a given length
    # filename = create_file(1000)
    # lib.send_file(filename, 0)

    # Remove the directory

    # time.sleep(25)
    # shutil.rmtree("/dev/shm/flute_tester/")


    lib.stop()
    time.sleep(4)

    # Force stop the program
    exit(0)


