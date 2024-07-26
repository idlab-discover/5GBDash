#!/usr/bin/env python
# coding: utf-8
import json
import sys
import fcntl

# Get the JSON file from the command line
json_file = sys.argv[1]
# Open the JSON file
with open(json_file) as f:
    locked = False
    while not locked:
        try:
            fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
            locked = True
        except IOError:
            print("File is locked by another process, we need to wait for it before we can read it.")
            # Wait a little bit before trying again
            time.sleep(0.001) # 1 ms

    data = json.load(f)

    # Unlock the file
    fcntl.flock(f, fcntl.LOCK_UN)


# Get al a list of all the B-frames indices
B_frames = [str(frame["coded_picture_number"]) for frame in data["frames"] if frame["pict_type"] == "B"]
# Write the list of B-frames indices to a file
with open(json_file+'.B.txt', mode='wt', encoding='utf-8') as f:
    locked = False
    while not locked:
        try:
            fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
            locked = True
        except IOError:
            print("File is locked by another process, we need to wait for it before we can write to it.")
            # Wait a little bit before trying again
            time.sleep(0.001) # 1 ms

    f.write('\n'.join(B_frames))

    # Unlock the file
    fcntl.flock(f, fcntl.LOCK_UN)

# Get al a list of all the I- & P-frames indices
other_frames = [str(frame["coded_picture_number"]) for frame in data["frames"] if frame["pict_type"] != "B"]
# Write the list of I- & P-frames indices to a file
with open(json_file+'.IP.txt', mode='wt', encoding='utf-8') as f:
    locked = False
    while not locked:
        try:
            fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
            locked = True
        except IOError:
            print("File is locked by another process, we need to wait for it before we can write to it.")
            # Wait a little bit before trying again
            time.sleep(0.001) # 1 ms

    f.write('\n'.join(other_frames))

    # Unlock the file
    fcntl.flock(f, fcntl.LOCK_UN)