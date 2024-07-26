import os
import struct
import sys

def find_boxes(f, start_offset=0, end_offset=float("inf")):
    """Returns a dictionary of all the data boxes and their absolute starting
    and ending offsets inside the mp4 file.

    Specify a start_offset and end_offset to read sub-boxes.
    """
    s = struct.Struct(">I4s") 
    boxes = {}
    offset = start_offset

    # Get the file size
    f.seek(0, os.SEEK_END)
    file_size = f.tell()

    # Adjust end_offset to be the minimum of file_size and provided end_offset
    end_offset = min(end_offset, file_size)

    f.seek(offset, os.SEEK_SET)
    while offset < end_offset:
        data = f.read(8)  # read box header
        if data == b"": 
            break  # EOF
        length, text = s.unpack(data)
        
        # Ensure the box length is reasonable
        if length < 8:
            break

        if text not in boxes:
            boxes[text] = []
        boxes[text].append((offset, offset + length))
        
        offset += length

        # Move to the next box
        f.seek(offset, os.SEEK_SET)

    return boxes

def get_earliest_presentation_time(f, offset):
    f.seek(offset, os.SEEK_SET)
    f.seek(8, os.SEEK_CUR)  # skip box header

    data = f.read(1)  # read version number
    version = int.from_bytes(data, "big")
    
    f.seek(3, os.SEEK_CUR)  # skip flags
    
    reference_id = int.from_bytes(f.read(4), "big")
    timescale = int.from_bytes(f.read(4), "big")

    if version == 0:
        earliest_presentation_time = int.from_bytes(f.read(4), "big")
        first_offset = int.from_bytes(f.read(4), "big")
    else:
        earliest_presentation_time = int.from_bytes(f.read(8), "big")
        first_offset = int.from_bytes(f.read(8), "big")

    return earliest_presentation_time

def get_base_media_decode_time(f, offset):
    f.seek(offset, os.SEEK_SET)
    f.seek(8, os.SEEK_CUR)  # skip box header

    data = f.read(1)  # read version number
    version = int.from_bytes(data, "big")

    f.seek(3, os.SEEK_CUR)  # skip flags

    if version == 0:
        base_media_decode_time = int.from_bytes(f.read(4), "big")
    else:
        base_media_decode_time = int.from_bytes(f.read(8), "big")

    return base_media_decode_time

def set_earliest_presentation_time(f, offset, new_value):
    f.seek(offset, os.SEEK_SET)
    f.seek(8, os.SEEK_CUR)  # skip box header

    data = f.read(1)  # read version number
    version = int.from_bytes(data, "big")

    f.seek(3, os.SEEK_CUR)  # skip flags
    
    f.seek(8, os.SEEK_CUR)  # skip reference_id and timescale

    if version == 0:
        f.write(new_value.to_bytes(4, 'big'))
    else:
        f.write(new_value.to_bytes(8, 'big'))

def set_base_media_decode_time(f, offset, new_value):
    f.seek(offset, os.SEEK_SET)
    f.seek(8, os.SEEK_CUR)  # skip box header

    data = f.read(1)  # read version number
    version = int.from_bytes(data, "big")

    f.seek(3, os.SEEK_CUR)  # skip flags

    if version == 0:
        f.write(new_value.to_bytes(4, 'big'))
    else:
        f.write(new_value.to_bytes(8, 'big'))

def examine_mp4(filename):
    
    with open(filename, "rb") as f:
        boxes = find_boxes(f)

        if b"sidx" not in boxes:
            return None, None
        sidx_box = boxes[b"sidx"][0]
        earliest_presentation_time = get_earliest_presentation_time(f, sidx_box[0])

        if b"moof" not in boxes:
            return earliest_presentation_time, []

        moof_box = boxes[b"moof"][0]
        moof_child_boxes = find_boxes(f, moof_box[0] + 8, moof_box[1])

        base_media_decode_times = []
        if b"traf" in moof_child_boxes:
            for traf_box in moof_child_boxes[b"traf"]:
                traf_child_boxes = find_boxes(f, traf_box[0] + 8, traf_box[1])
                if b"tfdt" in traf_child_boxes:
                    for tfdt_box in traf_child_boxes[b"tfdt"]:
                        base_media_decode_time = get_base_media_decode_time(f, tfdt_box[0])
                        base_media_decode_times.append(base_media_decode_time)

        return earliest_presentation_time, base_media_decode_times

def update_mp4(source_filename, target_filename):
    # Get values from source file
    earliest_presentation_time, base_media_decode_times = examine_mp4(source_filename)

    if earliest_presentation_time is None:
        print("No sidx box found in the source file.")
        return

    # Update values in target file
    with open(target_filename, "r+b") as f:
        boxes = find_boxes(f)

        if b"sidx" in boxes:
            for sidx_box in boxes[b"sidx"]:
                set_earliest_presentation_time(f, sidx_box[0], earliest_presentation_time)

        if b"moof" in boxes:
            moof_box = boxes[b"moof"][0]
            moof_child_boxes = find_boxes(f, moof_box[0] + 8, moof_box[1])
            tfdt_counter = 0
            if b"traf" in moof_child_boxes:
                for traf_box in moof_child_boxes[b"traf"]:
                    traf_child_boxes = find_boxes(f, traf_box[0] + 8, traf_box[1])
                    if b"tfdt" not in traf_child_boxes:
                        continue
                    for tfdt_box in traf_child_boxes[b"tfdt"]:
                        if tfdt_counter < len(base_media_decode_times):
                            set_base_media_decode_time(f, tfdt_box[0], base_media_decode_times[tfdt_counter])
                            tfdt_counter += 1

def main():
    if len(sys.argv) != 3:
        print("Usage: python script.py <source_filename> <target_filename>")
        sys.exit(1)

    source_filename = sys.argv[1]
    target_filename = sys.argv[2]

    update_mp4(source_filename, target_filename)
    print(f"Updated {target_filename} based on {source_filename}")

if __name__ == "__main__":
    main()
