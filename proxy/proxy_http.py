# proxy_cache.py

"""
    Implements a simple cache proxy
"""

import argparse
import mimetypes
import os
import pathlib
import re
import asyncio
import select
import socket
import sys
import time
import threading
import traceback
import hashlib
import fcntl
from natsort import natsorted


from datetime import datetime, timedelta
from urllib.request import Request, urlopen, HTTPError
from urllib.parse import urlparse

from metric import Gauge

print(time.time_ns() / 1_000_000) # [ms]

MAX_CONTENT_LENGTH = 10 * 1024 * 1024 # 10 MB
proxy_id = 0
server_url = 'http://12.0.0.0:8000'
fec = 0
tli_enabled = False
seg_dur = 4 # Mostly ignored, because the code is dynamic where possible
low_latency_chunks = 4 # Mostly ignored, because the code is dynamic where possible
metrics = {}
# Dictionary to store locks for each file
file_locks = {}
not_found_files = []

def setup_gauges(proxy_id):
    metric_log_file = f"proxy_http_{proxy_id}.metric.log"
    http_requests = Gauge('http_requests', 'Total number of HTTP requests', metric_log_file)
    files_sent = Gauge('files_sent', 'Total number of files sent', metric_log_file)
    files_not_found = Gauge('files_not_found', 'Total number of 404s', metric_log_file)
    cache_used = Gauge('cache_used', 'Total number of files sent from cache', metric_log_file)
    files_fetched = Gauge('files_fetched', 'Total number of files retrieved from the server', metric_log_file)
    http_request_handle_duration = Gauge('http_request_handle_duration', 'Total time it took to handle a request', metric_log_file)
    server_fetch_duration = Gauge('server_fetch_duration', 'Total time it took to fetch a file from the server', metric_log_file)
    broken_pipes = Gauge('broken_pipes', 'Client connections that closed unexpectedly', metric_log_file)
    connection_resets = Gauge('connection_resets', 'Client connections that closed forcefully', metric_log_file)

    return {
        http_requests.name(): http_requests,
        files_sent.name(): files_sent,
        files_not_found.name(): files_not_found,
        cache_used.name(): cache_used,
        files_fetched.name(): files_fetched,
        http_request_handle_duration.name(): http_request_handle_duration,
        server_fetch_duration.name(): server_fetch_duration,
        broken_pipes.name(): broken_pipes,
        connection_resets.name(): connection_resets
    }

def print_with_time(text):
    print(f"[{datetime.utcnow().strftime('%H:%M:%S.%f')[:-3]}] {text}")

def acquire_file_lock(filename):
    global file_locks

    # Using a global dictionary to store locks for each file
    if filename not in file_locks:
        file_locks[filename] = threading.Lock()

    file_lock = file_locks[filename]
    file_lock.acquire()
    return file_lock

def release_file_lock(file_lock):
    file_lock.release()

def get_segment_duration(segment):
    # The folder structure is like this:
    # $parent_dir/$seg_dur/$current_quality/$current_segment_$current_quality.$fileextension
    # So we need to get the $seg_dur part
    # This has to be safe, in case the segment it's path is not in the expected format
    try:
        return int(segment.split('/')[-3])
    except:
        return 0

def fetch_from_cache(proxy_id, filename, attempt=1):
    content = None

    try:
        # Check if we have this file locally
        with open(f'cache_{proxy_id}' + filename, 'rb') as f:
            locked = False
            while not locked:
                try:
                    fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    locked = True
                except IOError:
                    print("File is locked by another process, we need to wait for it before we can read it.")
                    # Wait a little bit before trying again
                    time.sleep(0.001) # 1 ms

            content = f.read()

            # Unlock the file
            fcntl.flock(f, fcntl.LOCK_UN)

        # If we have it, let's send it
        if content:
            return content
    except IOError:
        # Silently ignore the error
        pass

    if attempt > 1:
        # Too many attempts, let's give up
        print("Too many attempts, giving up")
        return None

    if filename.endswith('.mp4') and 'init_' not in filename:
        segment_duration = get_segment_duration(filename)
        start_time_ms = int(time.time_ns() / 1_000_000)
        # If the proxy has the IPB file, it can generate the mp4 file
        IPB_file = fetch_from_cache(proxy_id, filename + '.IPB.h264')
        print("Fetching IPB took", int(time.time_ns() / 1_000_000) - start_time_ms, "ms")       
        if IPB_file:
            start_time_ms = int(time.time_ns() / 1_000_000)
            # Wait for the proxy to generate the mp4 file
            # Do this using a while loop
            time_to_wait = segment_duration * 1000 * 0.75 # in milliseconds
            while time_to_wait > 0:
                if os.path.isfile(f'cache_{proxy_id}' + filename):
                    print("File has been generated")
                    print("Generation took", int(time.time_ns() / 1_000_000) - start_time_ms, "ms")  
                    return fetch_from_cache(proxy_id, filename)
                time.sleep(0.001)
                time_to_wait -= 1
            return fetch_from_cache(proxy_id, filename, attempt + 1)

        # Check if filename + '.IP.h264' exists
        start_time_ms = int(time.time_ns() / 1_000_000)
        IP_file = fetch_from_cache(proxy_id, filename + '.IP.h264')
        print("Fetching IP took", int(time.time_ns() / 1_000_000) - start_time_ms, "ms") 
        if IP_file:
            start_time_ms = int(time.time_ns() / 1_000_000)
            print("IP file found")
            # If so, we need to fetch the B file
            B_file = fetch_file(proxy_id,filename + '.B.h264')
            print("Fetching B took", int(time.time_ns() / 1_000_000) - start_time_ms, "ms") 
            if not B_file:
                return None
            # Wait for the proxy to generate the mp4 file
            start_time_ms = int(time.time_ns() / 1_000_000)
            time_to_wait = segment_duration * 1000 * 0.75 # in milliseconds
            while time_to_wait > 0:
                if os.path.isfile(f'cache_{proxy_id}' + filename):
                    print("File has been generated")
                    print("Generation took", int(time.time_ns() / 1_000_000) - start_time_ms, "ms")  
                    return fetch_from_cache(proxy_id, filename)
                time.sleep(0.001)
                time_to_wait -= 1
            return fetch_from_cache(proxy_id, filename, attempt + 1)

        start_time_ms = int(time.time_ns() / 1_000_000)
        # Check if filename + '.B.h264' exists
        B_file = fetch_from_cache(proxy_id, filename + '.B.h264')
        print("Fetching B took", int(time.time_ns() / 1_000_000) - start_time_ms, "ms") 
        if B_file:
            start_time_ms = int(time.time_ns() / 1_000_000)
            print("B file found")
            # If so, we only need to fetch the IP file
            IP_file = fetch_file(proxy_id, filename + '.IP.h264')
            print("Fetching IP took", int(time.time_ns() / 1_000_000) - start_time_ms, "ms") 
            if not IP_file:
                return None
            # Wait for the proxy to generate the mp4 file
            start_time_ms = int(time.time_ns() / 1_000_000)
            time_to_wait = segment_duration * 1000 * 0.75 # in milliseconds
            while time_to_wait > 0:
                if os.path.isfile(f'cache_{proxy_id}' + filename):
                    print("File has been generated")
                    print("Generation took", int(time.time_ns() / 1_000_000) - start_time_ms, "ms")  
                    return fetch_from_cache(proxy_id, filename)
                time.sleep(0.001)
                time_to_wait -= 1
            return fetch_from_cache(proxy_id, filename, attempt + 1)
    return None

def fetch_from_server(filename, method = 'GET'):
    start_time = datetime.now()
    url = server_url + filename
    q = Request(url, method=method) 

    try:
        response = urlopen(q)
        # Grab the header and content from the server req
        response_headers = response.info()
        content = response.read()
        metrics['server_fetch_duration'].set((datetime.now() - start_time).microseconds)
        return content
    except HTTPError:
        metrics['server_fetch_duration'].set((datetime.now() - start_time).microseconds)
        return None

def save_in_cache(proxy_id, filename, content):
    print_with_time(f'Saving a copy of {filename} in the cache')
    os.makedirs(os.path.dirname(f'cache_{proxy_id}' + filename), exist_ok=True)
    with open(f'cache_{proxy_id}' + filename, 'wb') as f:
        f.write(content)

def fetch_file(proxy_id, filename):
    lock = acquire_file_lock(filename)
    try:
        # Let's try to read the file locally first
        file_from_cache = fetch_from_cache(proxy_id, filename)

        if file_from_cache:
            print_with_time(f'{filename} fetched from cache')
            metrics['cache_used'].inc()
            return file_from_cache
        elif filename not in not_found_files:
            print_with_time(f'{filename} not in cache, fetching from server...')
            file_from_server = fetch_from_server(filename)

            if file_from_server:
                metrics['files_fetched'].inc()
                save_in_cache(proxy_id, filename, file_from_server)
                return file_from_server

            # The file was not found in the server either, prevent future requests
            not_found_files.append(filename)
    finally:
        release_file_lock(lock)

    return None

def get_mime_type(extension):
    mimetypes.types_map['.mpd'] = 'document'
    mimetypes.types_map['.m4s'] = mimetypes.types_map['.mp4']
    mimetypes.types_map['.f4s'] = mimetypes.types_map['.mp4']
    # Check if the extension is in the mimetypes
    if extension in mimetypes.types_map:
        # If so, then use it
        return mimetypes.types_map[extension]
    # Otherwise, use the default
    return 'application/octet-stream'

def handle_request(headers: dict, body: str) -> (bytearray, str):
    method = get_method(headers)
    url = get_path(headers)
    parsed_url = urlparse(url)
    path = parsed_url.path

    result = bytearray()

    streaming_path = ""

    # Index check
    if path == '/':
        path = '/index.html'
        url = path
        # If index.html would differ depending on the query, then url should be the following:
        # url = path + '?' + parsed_url.query

    if 'time' in path:
        print_with_time('Time request received')
        result += 'HTTP/1.1 200 OK\r\n'.encode()
        result += 'Access-Control-Allow-Origin: *\r\n'.encode()
        result += 'Content-Type: text/plain\r\n\r\n'.encode()
        result += f"{datetime.utcnow().strftime('%Y-%m-%dT%H:%M:%S.%f')[:-3]}Z".encode()
    elif '.m4s' in path and method == 'GET' and not 'init_' in path:
        print_with_time(f'Request received for {url}')
        result += 'HTTP/1.1 200 OK\r\n'.encode()
        # Remove query from the path
        extension = '.' + path.split('.')[-1]
        mime_type = get_mime_type(extension)
        result += f'Content-Type: {mime_type}\r\n'.encode()
        result += f'Transfer-Encoding: chunked\r\n'.encode()
        result += 'Accept-Ranges: bytes\r\n\r\n'.encode()

        # Define a regular expression pattern to match the desired part
        pattern = r'chunk_(\d+)_\d\.m4s'
        # Use re.search to find the match in the path
        match = re.search(pattern, path)
        if match:
            # Extract the matched part
            extracted_number = match.group(1)
            streaming_path = f'cache_{proxy_id}' + re.sub(pattern, extracted_number, path)
        else:
            print("Pattern not found in the path.")
            
    else:
        print_with_time(f'Request received for {url}')
        if 'live.mpd' in path or method != 'GET':
            # If tli is enabled and we are on subnet 1, then we should replace '_low_' with '_lowmid_' and '_mid_' with '_midhigh_' in the url
            if tli_enabled and proxy_id == 1:
                url = url.replace('_low_', '_lowmid_').replace('_mid_', '_midhigh_')
            content = fetch_from_server(url, method)
            metrics['files_fetched'].inc()
        else:
            content = fetch_file(proxy_id, url)

        # If we have the file, return it, otherwise 404
        if content:
            result += 'HTTP/1.1 200 OK\r\n'.encode()
            # Remove query from the path
            extension = '.' + path.split('.')[-1]
            mime_type = get_mime_type(extension)
            result += f'Content-Type: {mime_type}\r\n'.encode()
            result += 'Accept-Ranges: bytes\r\n\r\n'.encode()
            result += content
            metrics['files_sent'].inc()
        else:
            result += 'HTTP/1.1 404 NOT FOUND\r\n File Not Found'.encode()
            metrics['files_not_found'].inc()

    return (result, streaming_path)

async def handle_streaming_chunks(directory: str, writer: asyncio.StreamWriter):
    global low_latency_chunks
    global seg_dur

    # Create a variable about when the request started in ms
    start_streaming_time = datetime.now()
    start_offset = timedelta(seconds=0.75) if fec > 0 else timedelta(seconds=1.0)
    start_streaming_time -= start_offset

    n_chunks = low_latency_chunks
    seg_dur_s = seg_dur
    interval_dur = seg_dur_s / n_chunks

    async def send_content(content):
        nonlocal writer
        output = bytearray()
        output += f'{len(content):x}\r\n'.encode()
        if len(content) > 0:
            output += content
        output += b'\r\n'
        writer.write(output)
        #temp_storage.append(output)
        await writer.drain()

    async def send_closing_chunk():
        print("Sending closing chunk")
        await send_content("")

    async def send_chunk_file(directory, filename):
        filepath = os.path.join(directory, filename)
        last_content = b''  # Initialize with an empty bytes object
        current_content = b''  # Initialize with an empty bytes object
        content_change_counter = 0

        with open(filepath, 'rb') as f:
            locked = False
            while not locked:
                try:
                    fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    locked = True
                except IOError:
                    print("File is locked by another process, we need to wait for it before we can read it.")
                    # Wait a little bit before trying again
                    time.sleep(0.001) # 1 ms

            current_content = f.read()

            # Unlock the file
            fcntl.flock(f, fcntl.LOCK_UN)
        
        # The loop here is a dirty hack to make sure the file is fully written before we read it, this is necessary because some programs that write to the cache might not lock the file while writing to it.
        different = True
        time.sleep(0.005) # 5 ms
        while different:
            # Read again until the content is the same
            last_content = current_content
            with open(filepath, 'rb') as f:
                current_content = f.read()
            content_change_counter += 1
            different = current_content != last_content
            if different:
                print("Content changed")
                time.sleep(0.01) # 10 ms

        if content_change_counter > 1:
            print(f"Content changed {content_change_counter - 1} times")

        # Do not send empty chunks or our chunked stream will end
        if len(current_content) > 0:
            print("Sending", filename)
            # Send the content
            await send_content(current_content)

    def extract_chunk_ids(filenames):
        # Regular expression to extract the chunk id from the filename
        pattern = r'chunk_(\d+)_(\d+)_(\d+)\.f4s'
        chunk_ids = []
        for filename in filenames:
            match = re.match(pattern, filename)
            if match:
                chunk_id = int(match.group(3))
                chunk_ids.append(chunk_id)
        return chunk_ids

    def filter_chunk_ids(chunk_ids):
        # This only keeps the chunk ids in the ordered array until the first missing chunk (excluded)
        chunk_ids.sort()
        filtered_ids = []
        for i in range(len(chunk_ids)):
            if chunk_ids[i] == i + 1:
                filtered_ids.append(chunk_ids[i])
            else:
                break
        return filtered_ids

    parts = directory.split('/')
    # Get the proxy id from the path
    proxy_id = parts[0].split('_')[1]
    # Remove the first part of the path
    parts = parts[1:]

    segment_id = parts[-1]
    rep_id = parts[-2]

    if segment_id == '00025': # TODO: Hardcoded fix for the last segment
        n_chunks = 4
        interval_dur = seg_dur_s / n_chunks

    server_path = '/'.join(parts)
    expected_files = [f'chunk_{segment_id}_{rep_id}_{n}.f4s' for n in range(1, n_chunks + 1)]
    expected_times = [start_streaming_time + timedelta(seconds=interval_dur * n) for n in range(1, n_chunks + 1)]

    # Replace the last part with 'chunk_[number]_[number].m4s'
    parts[-1] = f'chunk_{segment_id}_{rep_id}.m4s'
    # Join the parts back together
    url = '/'.join(parts)


    # check if the chunk directory exists
    if not os.path.isdir(directory):
        print("Directory not found", directory)
        # If not, then we use the usual fetch_file method to attempt fetching the whole segment
        content = fetch_file(proxy_id, '/' + url)
        if content:
            await send_content(content)
        else:
            print("File not found")
        # Weather or not the file was found, we send the closing chunk to end the stream
        await send_closing_chunk()
        #print(directory, hash(b"".join(temp_storage)))
        #for index, item in enumerate(temp_storage):
        #    hash_value = hashlib.sha256(item).hexdigest()
        #    print(f"Hash of item {index + 1}: {hash_value[:8]} with length {len(item)}")
        return

    # Get the list of files in the directory
    files = []

    # Loop and get the list of files in the directory, exclude the files that have already been sent
    while len(files) < n_chunks:
        # Get the list of files in the directory
        current_files = os.listdir(directory)
        # Sort the files by name using natural sorting
        # This can cope with numbers in the filenames
        current_files = natsorted(current_files)
        # Filter on files that are .m4s files
        current_files = list(filter(lambda x: '.f4s' in x, current_files))
        # Extract chunk ids from filenames
        chunk_ids = extract_chunk_ids(current_files)
        # Filter the chunk ids
        filtered_ids = filter_chunk_ids(chunk_ids)
        filtered_files = [filename for filename, chunk_id in zip(current_files, chunk_ids) if chunk_id in filtered_ids]        

        # Iterate over the files
        for filename in filtered_files:
            # Skip the files that have already been sent
            if filename in files:
                continue
            # Send it to the client
            await send_chunk_file(directory, filename)

        # Update the list of files
        files = filtered_files

        if len(files) < n_chunks:
            missing_indices = [i for i, filename in enumerate(expected_files) if filename not in files]
            now = datetime.now()
            # Check if the file should have arrived by now
            if len(missing_indices) > 0 and expected_times[missing_indices[0]] < now:
                # The file should have arrived by now, but isn't here yet, so fetch it
                expected_file = expected_files[missing_indices[0]]
                content = fetch_file(proxy_id, '/' + server_path + '/' + expected_file)
                print("Expected file fetched:", expected_file)
                # The content will have been saved automatically
            else:
                # Wait for a bit
                await asyncio.sleep(0.001)


    print("All files sent")
    await send_closing_chunk()
    #print(directory, hash(b"".join(temp_storage)))
    #for index, item in enumerate(temp_storage):
    #    hash_value = hashlib.sha256(item).hexdigest()
    #    print(f"Hash of item {index + 1}: {hash_value[:8]} with length {len(item)}")
    # await writer.drain() # Already done when sending the closing chunk

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

async def receive_request(reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
    global start_bytes_count
    global interface
    request = bytearray()
    headers_received = False
    content_length = 0
    headers_end = -1

    metrics['http_requests'].inc()
    start_time = datetime.now()

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
        streaming_path = ""

        # Handle the request
        if (len(request_str) > 0):
            headers, body = parse_request(request_str)
            result = handle_request(headers, body)
            response = result[0]
            streaming_path = result[1]
        else:
            response = 'HTTP/1.1 400 BAD REQUEST\r\n\r\nBad Request\r\n'.encode()
        
        # Respond
        writer.write(response)

        await writer.drain()


        if streaming_path != "":
            # Handle the streaming request
            await handle_streaming_chunks(streaming_path, writer)
            metrics['files_sent'].inc()

        await writer.drain()

        # Calculate the duration
        metrics['http_request_handle_duration'].set((datetime.now() - start_time).microseconds)
        print(f'Handled request in {(datetime.now() - start_time).microseconds} microseconds')
    
    except BrokenPipeError:
        print("Client connection was closed unexpectedly.")
        metrics['broken_pipes'].inc()
    except ConnectionResetError:
        print("Client connection was reset.")
        metrics['connection_resets'].inc()
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

async def run_server(host, port):
    server = await asyncio.start_server(receive_request, host, port)
    print_with_time('Cache proxy is listening on port %s ...' % port)
    async with server:
        await server.serve_forever()

def main():
    # Define socket host and port
    SERVER_HOST = '0.0.0.0'
    SERVER_PORT = 33333

    global metrics
    metrics = setup_gauges(proxy_id)

    asyncio.run(run_server(SERVER_HOST, SERVER_PORT))


if __name__ == '__main__':
    parser = argparse.ArgumentParser("HTTP Proxy")
    parser.add_argument("-i", "--proxy_id", help="An integer to identify the proxy", type=int)
    parser.add_argument("-s", "--server_url", help="The url of the server")
    parser.add_argument("-f", "--fec", help="fec", type=int, default=0)
    parser.add_argument("-t", "--tli", help="tli", type=int, default=0) 
    parser.add_argument("-d", "--seg_dur", help="segment duration (Mostly ignored because code is dynamic where possible)", type=int, default=4)
    parser.add_argument("-l", "--low_latency_chunks", help="Number of CMAF Chunks per segment (ignored when not a cmaf stream)", type=int, default=0)

    args = parser.parse_args()
    if args.proxy_id is not None:
        proxy_id = args.proxy_id
    if args.server_url is not None:
        server_url = args.server_url
    if args.fec is not None:
        fec = args.fec
    if args.tli is not None:
        tli_enabled = args.tli == 1
    if args.seg_dur is not None:
        seg_dur = args.seg_dur
    if args.low_latency_chunks is not None:
        low_latency_chunks = args.low_latency_chunks
    main()
