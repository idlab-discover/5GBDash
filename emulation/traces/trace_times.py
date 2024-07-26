import os
import pandas as pd


def process_trace_times(directory="traces", n_samples=60, min_time=50):
    traces = []
    with os.scandir(directory) as it:
        entries = sorted(it, key=lambda entry: entry.name)
        # Loop over entries and get id,
        for i, entry in enumerate(entries):
            print(f"Processing {entry.name} ({i+1}/{len(entries)})")
            samples = []
            with open(entry.path, 'r') as f:
                for line in f.readlines()[:n_samples]:
                    v = line.split()
                    time = max(int(v[5]), min_time)
                    samples.append(time)
            traces.append(samples)
    df = pd.DataFrame(traces)
    df.to_csv("trace_times.csv", index=False)


process_trace_times(n_samples=150, min_time=50)
