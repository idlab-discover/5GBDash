#!/usr/bin/env python
# coding: utf-8
import json
import sys
import fcntl

def flatten_json(y):
    out = {}
 
    def flatten(x, name=''):
 
        # If the Nested key-value
        # pair is of dict type
        if type(x) is dict:
 
            for a in x:
                flatten(x[a], name + a + '_')
 
        # If the Nested key-value
        # pair is of list type
        elif type(x) is list:
 
            i = 0
 
            for a in x:
                flatten(a, name + str(i) + '_')
                i += 1
        else:
            out[name[:-1]] = x
 
    flatten(y)
    return out

json_file = sys.argv[1]

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

output = []
flattened = flatten_json(data["format"])
for frmt in flattened:
    output.append(f"{frmt}: {flattened[frmt]}")

with open(json_file+'.info.txt', mode='wt', encoding='utf-8') as f:
    locked = False
    while not locked:
        try:
            fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
            locked = True
        except IOError:
            print("File is locked by another process, we need to wait for it before we can write to it.")
            # Wait a little bit before trying again
            time.sleep(0.001) # 1 ms

    f.write('\n'.join(output))

    # Unlock the file
    fcntl.flock(f, fcntl.LOCK_UN)