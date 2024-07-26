import os
import math
from collections import defaultdict
from scipy.stats import t
import matplotlib.pyplot as plt
import numpy as np

# SI prefix multipliers
SI_UNITS = ['', 'k', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y']
SI_MULTIPLIERS = [10**(3*i) for i in range(len(SI_UNITS))]

def calculate_chunk_sizes(directory):
    # Dictionary to hold the sizes of chunks per chunk_id
    chunk_sizes = defaultdict(list)
    segment_sizes = defaultdict(list)
    segments = set()
    
    # Iterate over all files in the given directory
    for root, dirs, files in os.walk(directory):
        for file in files:
            if file.endswith(".f4s"):
                # Extract chunk_id and segment_id from the file name
                parts = file.split('_')
                if len(parts) == 4:
                    segment_id = int(parts[1])
                    chunk_id = int(parts[3].split('.')[0])
                    
                    # Add segment_id to the set of segments
                    segments.add(segment_id)
                    
                    # Get the size of the file
                    file_path = os.path.join(root, file)
                    file_size = os.path.getsize(file_path)
                    
                    # Append the file size to the list for the chunk_id and segment_id
                    chunk_sizes[chunk_id].append(file_size)
                    segment_sizes[segment_id].append((chunk_id, file_size))

    # Calculate the 95% confidence interval per chunk id and average
    results = {}
    for chunk_id, sizes in chunk_sizes.items():
        n = len(sizes)
        mean_size = sum(sizes) / n
        
        # Calculate the standard deviation
        std_dev = math.sqrt(sum((x - mean_size) ** 2 for x in sizes) / (n - 1))
        
        # Calculate the standard error
        stderr = std_dev / math.sqrt(n)
        
        # Calculate the confidence interval
        ci = stderr * t.ppf((1 + 0.95) / 2., n - 1)
        
        results[chunk_id] = {'mean': mean_size, 'ci': ci, 'original_mean': mean_size}
    
    # Determine the appropriate unit
    all_sizes = [result['mean'] for result in results.values()]
    max_size = max(all_sizes)
    
    for unit, multiplier in zip(SI_UNITS, SI_MULTIPLIERS):
        if max_size < multiplier * 1000:
            selected_unit = unit
            selected_multiplier = multiplier
            break
    
    # Adjust the sizes according to the selected unit
    for chunk_id in results:
        results[chunk_id]['mean'] /= selected_multiplier
        results[chunk_id]['ci'] /= selected_multiplier
        results[chunk_id]['original_mean'] /= selected_multiplier
    
    # Adjust segment sizes according to the selected unit
    adjusted_segment_sizes = {}
    for segment_id, sizes in segment_sizes.items():
        adjusted_sizes = [(chunk_id, size / selected_multiplier) for chunk_id, size in sizes]
        adjusted_segment_sizes[segment_id] = adjusted_sizes
    
    return results, adjusted_segment_sizes, len(chunk_sizes), len(segments), selected_unit

def print_chunk_sizes(results, segment_sizes, num_chunks, num_segments, unit_divider):
    # Sort chunk_ids
    sorted_chunk_ids = sorted(results.keys())
    
    unit_name = unit_divider + 'B' if unit_divider else 'bytes'
    
    print(f"Total number of chunks: {num_chunks}")
    print(f"Total number of segments: {num_segments}\n")
    print(f"Chunk ID\tMean Size ({unit_name})\t95% CI\t\tOriginal Average ({unit_name})")
    print("-" * 70)
    
    for chunk_id in sorted_chunk_ids:
        mean_size = results[chunk_id]['mean']
        ci = results[chunk_id]['ci']
        original_mean = results[chunk_id]['original_mean']
        print(f"{chunk_id}\t\t{mean_size:.2f}\t\t±{ci:.2f}\t\t{original_mean:.2f}")
    
    print("\nSizes per Segment:")
    print("-" * 30)
    for segment_id, sizes in segment_sizes.items():
        sizes_str = ", ".join(f"{size:.2f}" for chunk_id, size in sizes)
        print(f"Segment {segment_id}:\t{sizes_str} {unit_name}")

def plot_histogram(segment_sizes, unit_divider):
    # Create a color map for each chunk ID
    color_map = {}
    unique_chunk_ids = set(chunk_id for sizes in segment_sizes.values() for chunk_id, _ in sizes)
    colors = plt.cm.get_cmap('tab20', len(unique_chunk_ids))
    
    fig, ax = plt.subplots(figsize=(10, 6))
    
    # Flatten the segment_sizes to a list of tuples and sort by chunk_id and then by segment_id
    sorted_segments = sorted(
        [(segment_id, chunk_id, size) for segment_id, sizes in segment_sizes.items() for chunk_id, size in sizes],
        key=lambda x: (x[1], x[0])
    )
    
    # Plot each segment
    for segment_id, chunk_id, size in sorted_segments:
        if chunk_id not in color_map:
            color_map[chunk_id] = colors(len(color_map))
        ax.bar(f"Segment {segment_id}, Chunk {chunk_id}", size, color=color_map[chunk_id])
    
    unit_name = unit_divider + 'B' if unit_divider else 'bytes'
    ax.set_ylabel(f'Size ({unit_name})')
    ax.set_title('Chunk Sizes per Segment')
    plt.xticks(rotation=90)
    plt.tight_layout()
    plt.show()

# Example usage
directory = '../server/content/avatarcmaf16/4/5/'
results, segment_sizes, num_chunks, num_segments, unit_divider = calculate_chunk_sizes(directory)
print_chunk_sizes(results, segment_sizes, num_chunks, num_segments, unit_divider)
plot_histogram(segment_sizes, unit_divider)
