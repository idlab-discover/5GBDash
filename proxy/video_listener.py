import os
import time
import argparse
import subprocess
import fcntl
import select

from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler

def generate(directory, file, quality, segment, segment_length, video_length=97, segment_id=1):
    process = subprocess.Popen([
        '/bin/bash',
        './transcode.sh',
        directory,
        str(quality),
        file,
        str(segment),
        str(segment_length),
        directory + '/../../../temp',
        str(video_length),
        str(segment_id)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
    
    '''
    print(f'Generation based on {file}')

    stdout_fd = process.stdout.fileno()
    stderr_fd = process.stderr.fileno()

    # Wait for the process to finish
    while True:
        reads = [stdout_fd, stderr_fd]
        ret = select.select(reads, [], [])
        
        for fd in ret[0]:
            if fd == stdout_fd:
                output = process.stdout.readline()
                if output:
                    print(output, end='')
            if fd == stderr_fd:
                error = process.stderr.readline()
                if error:
                    print(error, end='')
        
        if process.poll() is not None:
            break

    # Ensure all remaining output is printed after the process ends
    for output in process.stdout.readlines():
        print(output, end='')
    for error in process.stderr.readlines():
        print(error, end='')
    '''
    print(f'Generated based on {file}')

def wait_until_file_is_fully_written(filepath):
    last_content = b''  # Initialize with an empty bytes object
    current_content = b''  # Initialize with an empty bytes object
    content_change_counter = 0

    with open(filepath, 'rb') as f:
        locked = False
        # We only start reading the file when it is not locked
        while not locked:
            try:
                fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
                locked = True
            except IOError:
                print("File is locked by another process, we need to wait for it before we can read it.")
                print(f"File path: {filepath}")
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
            #print("Content changed")
            time.sleep(0.005) # 5 ms

    if content_change_counter > 1:
        print(f"Content changed {content_change_counter - 1} time(s)")


class MovieEventHandler(FileSystemEventHandler):

    def __init__(self, directory, video_length=97):
        self.directory = directory
        self.video_length = video_length

    def on_created(self, event):
        if not event.is_directory and (event.src_path.endswith('.mp4') or event.src_path.endswith('.h264')):
            # A new segment was added
            segment_path = event.src_path
            # Get the filename
            filename = os.path.basename(segment_path)

            # Ignore init files.
            # Note: if the init files are missing, no intermediate quality can be generated.
            # If the init files are only received after a segment's base and augmented quality
            # files are received, the intermediate quality will not be generated.
            if (filename.startswith('init_')):
                return

            # Ignore files that do not have 'avatar' in their path
            if not 'avatar' in segment_path:
                return
            try:
                wait_until_file_is_fully_written(segment_path)

                # Get the segment number
                segment_number = filename.split('_')[1]
                segment = 'segment_' + segment_number

                # In what directory is the file located?
                file_directory = os.path.dirname(segment_path)
                # Get the quality level
                quality_level = os.path.basename(file_directory)
                
                # In what directory are the qualities saved?
                qualities_directory = os.path.dirname(file_directory)
                # Get the segment length
                segment_length = os.path.basename(qualities_directory)

                # Get the video title
                video_title = os.path.basename(os.path.dirname(qualities_directory))

                generate(file_directory, event.src_path, quality_level, segment, segment_length, video_length=self.video_length, segment_id=int(segment_number))

            except Exception as e:
                print(str(e))




def run(directory, video_length=97):
    event_handler = MovieEventHandler(directory, video_length)
    observer = Observer()
    observer.schedule(event_handler, path=directory, recursive=True)
    observer.start()

    try:
        while True:
             # Adjust the polling interval as needed
            time.sleep(0.0001) # 0.1 ms
    except KeyboardInterrupt:
        observer.stop()
    observer.join()


if __name__ == '__main__':
    parser = argparse.ArgumentParser("Video listener")
    parser.add_argument("dir", help="The path to the cache directory which we need to watch.", type=str)
    parser.add_argument("video_length", help="The length of the video in seconds.", type=int, default=97)
    args = parser.parse_args()
    print(args.dir, args.video_length)
    run(args.dir)

    