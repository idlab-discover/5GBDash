import argparse
import os

def get_chunks(filename: str):
    print(f"Reading file: {filename}")
    try:
        with open(filename, 'rb') as f:
            data = f.read()
    except FileNotFoundError as e:
        print(f"File not found: {filename}")
        return []
    except Exception as e:
        print(f"Error reading file: {filename}")
        return []

    positions = []
    last_box_index = -1

    # moof and mdat are the only two required boxes in a chunk
    # styp is required per segment
    # ftyp is required per video header
    # but we will assume here that none of the boxes are required
    box_order = ['ftyp', 'moov','styp', 'prft', 'emsg', 'moof', 'mdat']
    # The logic we folow here is that if a box which we find during the iteration is listed earlier in the box_order than the last box we found, we will consider it as the start of a new chunk.
    # Unknown boxes are considered belonging to the same chunk as the last known box, because they won't be recognized by the box_order list.
    for i in range(len(data)):
        # Get the box name
        box_name = data[i:i+4]
        try:
            box_name = box_name.decode('utf-8')
        except UnicodeDecodeError as e:
            # This part is not a box name
            continue
        # Is the current box a known box?
        if box_name in box_order:
            # If the last box index is not set,
            # or the current box is listed before the last box in the box_order list,
            # then we have found a new chunk
            if last_box_index == -1 or box_order.index(box_name) < last_box_index:
                # Store the position of the new chunk
                positions.append(i)
            # Update the last box index
            last_box_index = box_order.index(box_name)

    print(f"Found {len(positions)} chunks")

    # Extract the chunks
    chunks = []
    for i in range(len(positions)):
        # Make the first chunk start from the beginning of the data
        begin_position = positions[i] if i > 0 else 0
        # Make the last chunk end at the end of the data
        end_position = positions[i+1] if i < len(positions) - 1 else len(data)
        # Extract the chunk
        chunks.append(data[begin_position:end_position])

    return chunks


def extract_chunks(start_dir: str):
    # Iterate over all files in the (sub)directories
    for dirpath, dirnames, filenames in os.walk(start_dir):
        for filename in filenames:
            if filename.endswith('.m4s') and not filename.startswith('init'):
                current_path = os.path.join(dirpath, filename)
                chunks = get_chunks(current_path)
                print(f"Found {len(chunks)} chunks in {filename}")

                # Split the current path into parts
                parts = current_path.split('/')
                filename_parts = parts[-1].split('_')
                # Get the segment number
                segment_number = filename_parts[1]
                # Insert the segment number into the path, where the filename is currently located
                parts[-1] = segment_number
                # Join the parts back together
                new_path = '/'.join(parts)
                # Create the new directory
                os.makedirs(new_path, exist_ok=True)
                # Write the chunks to the new directory
                for i, chunk in enumerate(chunks):
                    # The new filename replaces .m4s with _(i+1).f4s
                    new_filename = filename.replace('.m4s', f'_{i+1}.f4s')
                    print(f"Writing {new_path}/{new_filename}")
                    with open(f"{new_path}/{new_filename}", 'wb') as f:
                        f.write(chunk)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Extract chunks from CMAF files')
    parser.add_argument('-d', '--start_dir', type=str, help='The directory to start the extraction from')
    args = parser.parse_args()
    extract_chunks(args.start_dir)


            
