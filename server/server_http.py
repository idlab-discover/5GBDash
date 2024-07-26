# proxy_cache.py

"""
    Implements a server for live streaming
"""

import mimetypes
import os
import re
import asyncio
import ctypes
import traceback
import time
import argparse
import threading
import fcntl

from datetime import datetime, timedelta

from metric import Gauge
from interface import get_total_bytes_tx

print(time.time_ns() / 1_000_000) # [ms]


MAX_CONTENT_LENGTH = 10 * 1024 * 1024 # 10 MB
METRIC_LOG_FILE = "server_http.metric.log"
log_level = 2

# Setup FLUTE retriever
libflute_retriever = ctypes.CDLL('../flute/build/examples/libflute_retriever.so')
flute_setup = libflute_retriever.setup # Obtain the library function
flute_setup.argtypes = [ctypes.c_uint16]  # Set the argument types
flute_setup(ctypes.c_uint16(log_level)) # Setup the library

flute_retriever = libflute_retriever.retrieve # Obtain the library function
flute_retriever.restype = ctypes.c_ulong # Set the return type
flute_retriever.argtypes = [ctypes.c_char_p, ctypes.c_uint16, ctypes.c_char_p] # Set the argument types

flute_symbol_count = libflute_retriever.symbol_count # Obtain the library function
flute_symbol_count.restype = ctypes.c_uint64 # Set the return type
flute_symbol_count.argtypes = [ctypes.c_char_p] # Set the argument types

flute_length = libflute_retriever.length # Obtain the library function
flute_length.restype = ctypes.c_uint64 # Set the return type
flute_length.argtypes = [ctypes.c_char_p] # Set the argument types

# Create metrics
requests_counter = Gauge('http_requests', 'Total number of HTTP requests', METRIC_LOG_FILE)
files_counter = Gauge('files_sent', 'Total number of files sent', METRIC_LOG_FILE)
segment_counter = Gauge('segments_sent', 'Total number of segments sent', METRIC_LOG_FILE)
chunks_counter = Gauge('chunks_sent', 'Total number of chunks sent', METRIC_LOG_FILE)
symbols_counter = Gauge('partial_parts','Total number of file symbols requested', METRIC_LOG_FILE)
files_not_found_counter = Gauge('files_not_found', 'Total number of 404s', METRIC_LOG_FILE)
partial_request_counter = Gauge('partial_requests', 'Total number of partial requests', METRIC_LOG_FILE)
partial_processing_duration = Gauge('partial_processing_duration', 'Time it takes for one partial request to be processed', METRIC_LOG_FILE)
http_request_handle_duration = Gauge('http_request_handle_duration', 'Total time it took to handle a request', METRIC_LOG_FILE)
broken_pipes = Gauge('broken_pipes', 'Client connections that closed unexpectedly', METRIC_LOG_FILE)
connection_resets = Gauge('connection_resets', 'Client connections that closed forcefully', METRIC_LOG_FILE)
total_bytes_uc_interface = Gauge('total_bytes_uc_interface','Total number of bytes send over uc interface', METRIC_LOG_FILE)
total_file_bytes_uc = Gauge('total_file_bytes_uc','Total number of file bytes that need to be send over uc, excl ALCs', METRIC_LOG_FILE)
total_fdt_bytes_uc = Gauge('total_fdt_bytes_uc','Total number of fdt bytes that need to be send over uc', METRIC_LOG_FILE)
total_partial_bytes_uc = Gauge('total_partial_bytes_uc','Total number of partial bytes that need to be send over uc', METRIC_LOG_FILE)


start_bytes_count = 0
interface = "server-eth1"

metrics = [
    requests_counter,
    files_counter,
    segment_counter,
    files_not_found_counter,
    symbols_counter,
    chunks_counter,
    partial_request_counter,
    partial_processing_duration,
    http_request_handle_duration,
    broken_pipes,
    connection_resets,
    total_bytes_uc_interface,
    total_file_bytes_uc,
    total_fdt_bytes_uc,
    total_partial_bytes_uc
]

class BasicMetricsStore:
    def __init__(self):
        self.metrics = {}

    def update(self):
        for metric in metrics:
            self.metrics[metric.name()] = metric.get()
        self.save('server.metrics')

    def save(self, filename):
        output = ""
        for key, value in self.metrics.items():
            output += f"{key};{value}\n"
        with open(filename, 'w') as file:
            file.write(output)

basic_metrics_store = BasicMetricsStore()

def fetch(filename: str):
    try:
        # Check if we have this file locally
        with open(filename, 'rb') as f:
            locked = False
            while not locked:
                try:
                    fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    locked = True
                except IOError:
                    print("File is locked by another process, we need to wait for it before we can read it.")
                    time.sleep(0.001)
            # Read the content of the file
            content = f.read()

            # Release the lock
            fcntl.flock(f, fcntl.LOCK_UN)
        # If we have it, let's send it
        return content
    except IOError:
        return None
    
def get_missing_packages(json_string: str) -> bytes:
    # Keep track of the amount of symbols that have been fetched.
    symbol_count = flute_symbol_count(ctypes.c_char_p(json_string.encode('utf-8')))
    symbols_counter.inc(symbol_count)

    # First we calculate a size prediction. The output is an approximation that is at least the final size.
    buffer_size = flute_length(ctypes.c_char_p(json_string.encode('utf-8')))
    # Create a buffer to store the result to.
    result = ctypes.create_string_buffer(buffer_size)
    # Fill the buffer with the requested data.
    length = flute_retriever(ctypes.c_char_p(json_string.encode('utf-8')), ctypes.c_uint16(1500), result)
    total_partial_bytes_uc.inc(length)
    # Return only the part that has been filled.
    return ctypes.string_at(result, length)

    
def handle_request(headers: dict, body: str) -> bytearray:
    method = get_method(headers)
    path = get_path(headers)

    result = bytearray()

    if 'time' in path:
        print('Time request received')
        result += 'HTTP/1.1 200 OK\r\n'.encode()
        result += 'Access-Control-Allow-Origin: *\r\n'.encode()
        result += 'Content-Type: text/plain\r\n\r\n'.encode()
        result += f"{datetime.utcnow().strftime('%Y-%m-%dT%H:%M:%S.%f')[:-3]}Z".encode()

    elif path.startswith('/partial'):
        if method != 'POST' or len(body) <= 1:
            print('Unsuported partial request received')
            result += 'HTTP/1.1 404 NOT FOUND\r\n\r\nFile Not Found\r\n'.encode()
        else:
            # We print this from the retriever library.
            # print('Partial request received')

            partial_request_counter.inc()
            start_time = datetime.now()
            
            result += 'HTTP/1.1 200 OK\r\n'.encode()
            result += 'Access-Control-Allow-Origin: *\r\n'.encode()
            result += 'Content-Type: text/plain\r\n\r\n'.encode()

            result += get_missing_packages(body)

            partial_processing_duration.set((datetime.now() - start_time).microseconds)


    elif path.startswith('/metric'):
        result = 'HTTP/1.1 200 OK\r\n'.encode()
        result += 'Access-Control-Allow-Origin: *\r\n'.encode()
        result += 'Content-Type: text/plain\r\n\r\n'.encode()
        for metric in metrics:
            result += f'# HELP {metric.name()} {metric.doc()}\r\n'.encode()
            result += f'# TYPE {metric.name()} {metric.type()}\r\n'.encode()
            result += f'{metric.name()} {metric.get()}\r\n'.encode()

    else:
        print(f'{datetime.now().strftime("[%H:%M:%S.%f]")} Request received for {path}')

        filename = path.split('?')[0]
        # Index check
        if filename == '/':
            filename = '/index.html'

        is_fdt = filename == '/fdt'
        if is_fdt:
            filename = 'last.fdt'
        else:

            # Remove '/' if it starts with it
            # We will add it back later
            if filename.startswith('/'):
                filename = filename[1:]

            # Directory hack, to make multiple video's link to the same directory
            filename_parts = filename.split('/')
            if len(filename_parts) > 1:
                # Get the directory
                directory = filename_parts[0]

                # Check if the directory contains an underscore
                directory_parts = directory.split('_')
                # If the length is greater then 1, then it is in a directory
                if len(directory_parts) > 1:
                    # Check if the directory exists
                    if not os.path.isdir("content/" + directory):
                        # If not, then remove everything from the underscore in the directory name
                        # This allows us to link to the same directory
                        filename_parts[0] = directory_parts[0]
                        filename = '/'.join(filename_parts)

                        # Check if the file end with .mpd
                        if filename.endswith('.mpd'):
                            # If so, then then we do the opposite as above
                            # We will add everything from the underscore in the directory name to the filename (last part)
                            # new_filename is the filename without the extension (remove last part of split '.')
                            dot_split = filename.split('.')
                            # Remove the extension
                            new_filename = '.'.join(dot_split[:-1])
                            # Add the directory name to the filename (everything after the first underscore)
                            new_filename += '_' + '_'.join(directory_parts[1:])
                            # Add the extension
                            new_filename += '.' + dot_split[-1]
                            # Check if the file exists
                            if os.path.isfile("content/" + new_filename):
                                # If so, then we use this file instead
                                # This allows us to use a different mpd file for each video, if needed
                                filename = new_filename
            
            # All files are located in the content directory
            filename = "content/" + filename

            # Check if it is an f4s file
            if filename.endswith('.f4s'):
                chunks_counter.inc()
            elif filename.endswith('.m4s'):
                segment_counter.inc()
            elif filename.endswith('.mp4') and 'segment_' in filename:
                segment_counter.inc()


        # Fetch the file content
        content = fetch(filename)

        # If we have the file, return it, otherwise 404
        if content:
            if is_fdt:
                # Content is xml, with the following:
                # <FDT-Instance Expires="1697033575" ...
                # Check if the file is expired
                expires = re.search('Expires="([0-9]+)"', content.decode('utf-8'))
                if expires:
                    expires = int(expires.group().split('"')[1]) * 1000
                    # Expires is now a timestamp in ms since epoch
                    # Check if the fdt is expired
                    if expires < time.time_ns() / 1_000_000:
                        print('FDT expired')
                        result += 'HTTP/1.1 404 NOT FOUND\r\n\r\nFile Not Found\r\n'.encode()
                        files_not_found_counter.inc()
                        return result

            # Get the mime type based on the file extension
            extension = '.' + filename.split('.')[-1]
            mimetypes.types_map['.mpd'] = 'document'
            mimetypes.types_map['.h264'] = 'video/h264'
            # Check if the extension is in the mimetypes
            if extension in mimetypes.types_map:
                # If so, then use it
                mime_type = mimetypes.types_map[extension]
            else:
                # Otherwise, use the default
                mime_type = 'application/octet-stream'

            result += 'HTTP/1.1 200 OK\r\n'.encode()
            result += f'Content-Type: {mime_type}\r\n'.encode()
            result += 'Accept-Ranges: bytes\r\n\r\n'.encode()
            result += content
            files_counter.inc()
            if is_fdt:
                total_fdt_bytes_uc.inc(len(content))
            else:
                total_file_bytes_uc.inc(len(content))

            if is_fdt:
                result += '\r\n\r\n'.encode()
        else:
            result += 'HTTP/1.1 404 NOT FOUND\r\n\r\nFile Not Found\r\n'.encode()
            files_not_found_counter.inc()

    return result

def parse_request(request_str: str) -> tuple[dict[str, str], str]:
    headers, body = request_str.split('\r\n\r\n', 1)
    headers = headers.split('\r\n')
    headers_dict = {
        "top": headers[0]
    }
    for header in headers[1:]:
        key, value = header.split(': ', 1)
        headers_dict[key] = value
    return headers_dict, body

def get_method(headers: dict) -> str:
    top_header = headers.get('top')
    if top_header:
        return top_header.split()[0]
    return ''

def get_path(headers: dict) -> str:
    top_header = headers.get('top')
    if top_header:
        parts = top_header.split()
        if len(parts) > 1:
            return parts[1]
    return '/'

def get_content_length(headers: dict) -> int:
    content_length_header = headers.get('Content-Length')
    if content_length_header:
        return int(content_length_header)
    return 0

async def receive_request(reader, writer):
    global start_bytes_count
    global interface
    request = bytearray()
    headers_received = False
    content_length = 0
    headers_end = -1

    requests_counter.inc()
    start_time = datetime.now()

    end_amount = get_total_bytes_tx(interface)
    total_bytes_sent = end_amount - start_bytes_count
    total_bytes_uc_interface.set(total_bytes_sent)

    try:
        # Read incomming request untill no more data is received.
        while True:
            chunk = await reader.read(512)
            if not chunk:
                break  # No more data to read
            request.extend(chunk)

            # Check if we may stop reading data
            if not headers_received:
                # Did we receive all the headers?
                headers_end = request.find(b'\r\n\r\n')
                if headers_end != -1: # True if we received all the headers
                    headers_received = True
                    headers, body = parse_request(request.decode('utf-8'))
                    content_length = get_content_length(headers) # Read the content length in the headers

                    # Check if the body length exceeds the maximum content length
                    if content_length >= MAX_CONTENT_LENGTH:
                        response = 'HTTP/1.1 413 REQUEST ENTITY TOO LARGE\r\n\r\nRequest Entity Too Large\r\n'.encode()
                        writer.write(response)
                        await writer.drain()
                        # writer close is called by try finally
                        return

                    body_start = headers_end + 4 # + 4 because of '\r\n\r\n'
                    body_length = len(request) - body_start
                    # Stop if the body is at least as long as the requested content length
                    if body_length >= content_length:
                        break
            else:
                # We received the headers in a previous iteration
                body_start = headers_end + 4 # + 4 because of '\r\n\r\n'
                body_length = len(request) - body_start
                # Stop if the body is at least as long as the requested content length
                if body_length >= content_length:
                    break

        # Create variables used for handling the request
        request_str = request.decode('utf-8')
        response = bytearray(b'')

        # Handle the request
        if (len(request_str) > 0):
            headers, body = parse_request(request_str)
            response = handle_request(headers, body)
        else:
            response = 'HTTP/1.1 400 BAD REQUEST\r\n\r\nBad Request\r\n'.encode()
        
        # Respond
        writer.write(response)
        await writer.drain()
        http_request_handle_duration.set((datetime.now() - start_time).microseconds)
    
    except BrokenPipeError:
        print("Client connection was closed unexpectedly.")
        broken_pipes.inc()
    except ConnectionResetError:
        print("Client connection was reset.")
        connection_resets.inc()
    except ConnectionError as e:
        print("Connection error:", e)
    except Exception as e:
        response = 'HTTP/1.1 500 INTERNAL SERVER ERROR\r\n\r\nInternal Server Error\r\n'.encode()
        writer.write(response)
        await writer.drain()
        # printing error and stack trace
        print("Error processing request:", e)
        traceback.print_exc()
    finally:
        writer.close()

    end_amount = get_total_bytes_tx(interface)
    total_bytes_sent = end_amount - start_bytes_count
    total_bytes_uc_interface.set(total_bytes_sent)

async def run_server(host, port):
    server = await asyncio.start_server(receive_request, host, port)
    async with server:
        await server.serve_forever()

def init_livestreams(live_start, extra_offset=0.0, videos=""):
    if videos == "" or videos == "all" or videos is None:
        # Get a list of all the videos
        videos = [f.name for f in os.scandir('content') if f.is_dir()]
        # Check if live_start is a string
        if isinstance(live_start, str):
            # If it contains a comma, then only take the first part
            if ',' in live_start:
                live_start = live_start.split(',')[0]
            # Convert to int
            live_start = float(live_start)
        # Convert to list where each element is the same, so we can use it for each video
        live_start = [live_start for _ in range(len(videos))]
    else:
        # Convert to list
        videos = videos.split(',')
        # Check if live_start is a string
        if isinstance(live_start, str):
            # If it contains a comma, then split it
            if ',' in live_start:
                live_start = live_start.split(',')
            else:
                live_start = [live_start]
            # Convert to int
            live_start = [float(n) for n in live_start]
            # If the length of the list is shorter then the amount of videos, then we need to add some values (take the first value)
            while len(live_start) < len(videos):
                live_start.append(live_start[0])
                # Check if live_start is an int
        elif isinstance(live_start, float):
            # Convert to list where each element is the same, so we can use it for each video
            live_start = [live_start for _ in range(len(videos))]
        # Check if live_start is an int
        elif isinstance(live_start, int):
            # Convert to list where each element is the same, so we can use it for each video
            live_start = [live_start * 1.0 for _ in range(len(videos))]
        else:
            print("Error: live_start should be a string if videos or one integer is not empty")

    video_extensions = []
    for video in videos:
        video_parts = video.split("_")
        if len(video_parts) > 1:
            video_extensions.append("_".join(video_parts[1:]))
        else:
            video_extensions.append("")

    print(f"Live start: {live_start}")
    print(f"Video extensions: {video_extensions}")

    def get_avs(live_start, extra_offset, i=0):
        start_time = live_start[i] + extra_offset
        start_time = datetime.utcnow() + timedelta(seconds=start_time)
        avs = start_time.strftime('%Y-%m-%dT%H:%M:%S.%fZ')
        return avs

    for i, video in enumerate(videos):
        video_parts = video.split("_")
        if len(video_parts) > 1:
            video_base = video_parts[0]
            # Check if the base exists in the list of videos
            # if not, then we append it
            if video_base not in videos:
                videos.append(video_base)
                live_start.append(live_start[i])
                video_extensions.append("")

    # Enumerate over the videos
    for i, video in enumerate(videos):
        extra_video_extension = ""
        video_parts = video.split("_")
        if len(video_parts) > 1:
            extra_video_extension = "_".join(video_parts[1:])

        avs = get_avs(live_start, extra_offset, i)
        for root, dirs, files in os.walk(f"content/{video}"):
            for file in files:
                # Check if the file is a mpd file
                if file.endswith('.mpd'):
                    # Check if the file is the live.mpd file
                    modify = file == 'live.mpd'
                    temp_avs = avs

                    if not modify:
                        if len(extra_video_extension) > 1 and file == f'live_{extra_video_extension}.mpd':
                            modify = True

                    if not modify:
                        # Get the file parts (without extension)
                        file_parts = '.'.join(file.split('.')[:-1]).split('_')
                        if len(file_parts) > 1 and file_parts[0] == 'live':
                            video_extension = "_".join(file_parts[1:])
                            # Check if the video extension is in the list of video extensions and get the index
                            if len(video_extension) >  0:
                                index = video_extensions.index(video_extension) if video_extension in video_extensions else i
                                temp_avs = get_avs(live_start, extra_offset, index)
                                modify = True

                    if modify:
                        print(f"Modifying {root}/{file}")
                        path = os.path.join(root, file)
                        with open(path, 'r') as f:
                            content = f.read()
                        content = re.sub('availabilityStartTime=.*min',
                                        f'availabilityStartTime="{temp_avs}" min',
                                        content)
                        with open(path, 'w') as f:
                            f.write(content)

def main(live_start, extra_offset=0.0, videos=""):
    # Initialize live streams
    init_livestreams(live_start, extra_offset, videos)
    
    # Define socket host and port
    SERVER_HOST = '0.0.0.0'
    SERVER_PORT = 8000

    asyncio.run(run_server(SERVER_HOST, SERVER_PORT))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Async HTTP server for live streaming")
    parser.add_argument("-m", "--live_start", help="How many seconds untill the livestream starts (comma seperated)", default='22')
    parser.add_argument("-n", "--live_start_extra", help="How many extra seconds untill the livestream starts", default=0) # For testing purposes
    parser.add_argument('-v', '--videos', help='videos (comma seperated)', default='')
    parser.add_argument("-i", "--interface", help="The main interface through which client traffic passes", default="server-eth1")
    parser.add_argument("-f", "--fec", help="fec", type=int, default=0)
    args = parser.parse_args()

    live_start = 22.0
    if args.live_start:
        live_start = args.live_start

    live_start_extra = 0.0
    if args.live_start_extra:
        live_start_extra = float(args.live_start_extra)

    # live_start_extra += 2 # For testing purposes

    videos = ""
    if args.videos:
        videos = args.videos

    if args.interface:
        interface = args.interface

    print(args)

    # Set the initial byte count, so we can calculate the total bytes sent
    start_bytes_count = get_total_bytes_tx(interface)


    # A seperate thread that runs basic_metrics_store.update() every second
    # Using the threading module
    run_thread = True
    def update_metrics():
        while run_thread:
            basic_metrics_store.update()
            time.sleep(1)
    t = threading.Thread(target=update_metrics)
    t.start()

    main(live_start, live_start_extra, videos)

    # Stop the thread
    run_thread = False

    # Wait for the thread to finish
    t.join()
