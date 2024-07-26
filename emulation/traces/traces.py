import os
import pandas as pd


def process_traces(directory="traces", n_samples=60, min_bandwidth=15000000):
    traces = []
    with os.scandir(directory) as it:
        entries = sorted(it, key=lambda entry: entry.name)
        for entry in entries:
            samples = []
            with open(entry.path, 'r') as f:
                for line in f.readlines()[:n_samples]:
                    v = line.split()
                    bandwidth = max(int(v[4]) * 8000 // int(v[5]), min_bandwidth)
                    samples.append(bandwidth)
            traces.append(samples)
    df = pd.DataFrame(traces)
    df.to_csv("traces.csv", index=False)


process_traces(n_samples=150, min_bandwidth=1000000)
