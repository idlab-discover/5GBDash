# Server

The server component provides video encoding, DASH over unicast, and DASH over multicast functionalities.

## Requirements

### System Packages
Install the following package for video encoding:
```commandline
sudo apt install ffmpeg
```

## Video Encoding

To enable DASH-based video streaming, the provided video must be encoded using multiple quality representations. Add a full-HD video of your choice (e.g., a movie trailer) to the `server` directory, preferably using a lowercase name. Then, run:
```commandline
chmod +x encode.sh
./encode.sh <video_file>
```

### Optional Arguments
You can provide the following optional arguments:
- Base directory, default `content`
- Constant rate factor (CRF), default `18`
- Frame rate (FPS), default `25`
- Segment duration (s), default `4`
- Chunk duration (s), default `0.5`

### Script Actions
The script performs the following actions:
- Calculates the group of pictures (GOP) length based on the segment duration and frame rate.
- Encodes the video at five different resolutions: 1920, 1280, 960, 640, and 480. Segments the video using the defined segment duration. If chunk duration is smaller, CMAF-CTE is used for low-latency delivery.
- Creates a single media presentation description (MPD) file containing all metadata, including the average bitrate of each quality representation.

Once the script has finished executing, the `content` directory will contain a subdirectory with the movie name, containing all quality representations and segment configurations.

### Compiling for TLI
To enable Temporal Layer Injection (TLI), the video should be encoded using the `encode_tli.sh` script instead. The chunk duration parameter is not supported here as the TLI implementation has not been made compatible with CMAF yet. Additionally, a modified version of x264 is required, which is found in this directory. If you want to compile x264 by yourself, you should patch the source with the `x264.patch` file.

## DASH over Unicast

To enable unicast-based DASH, start an HTTP server from within the `content` directory. The `server_http.py` script is called from the `setup` directory during emulation, but you can also test it locally:
```commandline
python3 server_http.py
```
In your browser, go to `localhost:8000/index.html?video=<video_name>&seg_dur=<segment_duration>` to start the video.

You can provide multiple videos and encode them using various segment durations. Simply update the query parameters to play different videos.

The server also provides a timing functionality for live video synchronization.

## DASH over Multicast

To enable multicast-based DASH, the FLUTE protocol is used. To start multicasting on the emulated network, run:
```commandline
chmod +x server_multicast.sh
./server_multicast.sh
```
Ensure that all videos in the provided argument list are available in the `content` directory.
The arguments are listed in the `server_multicast.py` script.