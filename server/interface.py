import re

def get_total_bytes_tx(interface):
    with open("/proc/net/dev", "r") as f:
        for line in f:
            if interface in line:
                return int(re.split(r"\s+", line.strip())[9])
    
    return 0