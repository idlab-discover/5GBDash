# Proxy

The proxy component provides two functionalities: content delivery through an HTTP server and content reception through a multicast receiver.

## Requirements

### Python Packages
Install the following Python packages:
```commandline
pip install watchdog natsort
```

### System Packages
Install the following packages for video encoding:
```commandline
sudo apt install ffmpeg gpac
```

## HTTP Server

The proxy acts as the client's contact point and accepts incoming GET requests. Upon receiving a request, the proxy checks if the content is cached locally. If cached, the content is returned to the client; otherwise, a GET request is issued to the server.

### Running the HTTP Server
Each subnetwork has a proxy with a unique ID. The `proxy_http.py` script is called from the `setup` directory during emulation, using this proxy ID as an argument. You can also run it locally:
```commandline
python3 proxy_http.py 1
```
Create a directory `cache_1` and copy some example files. In your browser, you can now retrieve those files by going to `localhost:3333/<file_name>`. If a file is not available, the proxy will try to contact the server at the hardcoded address `192.168.1.100:8000`.

## Multicast Receiver

To enable multicast-based DASH, the FLUTE protocol is used. To start receiving multicast content on the emulated network, run:
```commandline
chmod +x proxy_multicast.sh
./proxy_multicast.sh <proxy_id>
```
This command allows the proxy to receive all files and store them in the local `cache_<proxy_id>` directory.

## Temporal Layer Injection
When a client requests a video segment, it will try to retrieve the segment from the cache. If the segment is not available, but a segment from a neighboring quality representation is available, the proxy will generate the segment by injecting the temporal layer requested over unicast in the neighboring segment. This process is called Temporal Layer Injection (TLI). The temporal layer will be requested from the server and injected into the segment before being sent to the client. As such, this process works transparently to the client. 