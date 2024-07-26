# Evaluation

The evaluation component contains notebooks used to evaluate the performance of the different parameters. This component is not necessary for setting up the hybrid unicast- and multicast-based content delivery system but is useful for performance analysis.

## Requirements

Ensure the following Python packages are installed:

- matplotlib
- numpy
- ipykernel
- pandas
- numba

## Metrics

The table below lists all the captured metrics:

| Metric | Unit | Component(s) | Description |
| ------ | ---- | ------------ | ----------- |
| alcs_ignored | ALCs | Proxy (FLUTE) | Number of Application Layer Control Symbols ignored upon reception. Reasons: 1. Unknown packet due to missing FDT. 2. Packet belongs to a completed file. |
| alcs_received | ALCS | Proxy (FLUTE) | Number of Application Layer Control Symbols received, including ignored ones. |
| broken_pipes | pipes | Proxy (HTTP), Server (HTTP) | Client connections that closed unexpectedly. |
| buffer_length | s | Client | Length of the buffer used for storing media data before playback. |
| bytes_received | bytes | Proxy | Amount of bytes received on the network interface. Three separate interfaces exist per proxy: server (multicast) -> proxy, server (unicast) -> proxy, client (unicast) -> proxy. |
| bytes_transmitted | bytes | Proxy | Amount of bytes transmitted on the network interface. Three separate interfaces exist per proxy: proxy -> server (multicast), proxy -> server (unicast), proxy -> client (unicast). |
| cache_used | files | Proxy (HTTP) | Number of files sent from cache. |
| check_md5_time | ms | Proxy (FLUTE) | Time taken to check the MD5 of a file. |
| connection_resets | resets | Proxy (HTTP), Server (HTTP) | Client connections that closed forcefully. |
| download_average | s | Client | Average download time for the last 4 segments. |
| download_high | s | Client | Maximum download time for the last 4 segments. |
| download_low | s | Client | Minimum download time for the last 4 segments. |
| dropped_frames | frames | Client | Number of frames dropped during playback, impacting smoothness. |
| emit_missing_symbols | recovery attempts | Proxy (FLUTE) | Number of symbol recovery attempts. |
| etp | kbps | Client | Throughput between the server (proxy) and client over the negotiated transport, estimated at the start of the response. |
| fdt_received | FDTs | Proxy (FLUTE) | Number of File Delivery Tables received over multicast. |
| file_hash_mismatches | files | Proxy (FLUTE) | Number of files with hash mismatches when completely received. |
| fetcher_bandwidth | kbps | Proxy (FLUTE) | Bandwidth used for fetching missing symbols between the server and the proxy. |
| fetcher_latency | μs | Proxy (FLUTE) | Latency experienced when fetching missing symbols. |
| files_fetched | files | Proxy (HTTP) | Number of files retrieved from the server. |
| files_not_found | 404s | Server (HTTP), Proxy (HTTP) | Number of 404 errors. |
| files_sent | files | Server (HTTP), Proxy (HTTP) | Number of files sent over HTTP. |
| framerate | frames/s | Client | Number of frames displayed per second during playback. |
| http_request_handle_duration | μs | Server (HTTP), Proxy (HTTP) | Total time taken to process a request on the server. |
| http_requests | requests | Server (HTTP), Proxy (HTTP) | Number of HTTP requests received. |
| latency_average | ms | Client | Average latency over the last 4 requested segments (time from request to receipt of first byte). |
| latency_high | ms | Client | Maximum latency over the last 4 requested segments (time from request to receipt of first byte). |
| latency_low | ms | Client | Minimum latency over the last 4 requested segments (time from request to receipt of first byte). |
| live_latency | s | Client | Current live stream latency (difference between current time and playback head time). |
| max_quality | - | Client | Maximum quality index available in the stream. |
| missing_symbols_gauge | symbols | Proxy (FLUTE) | Total number of symbols not handled when the deadline triggers. |
| mtp | kbps | Client | Average measured throughput. |
| multicast_bytes_received | bytes | Proxy (FLUTE) | Number of bytes received over multicast. |
| multicast_fdt_sent | FDTs | Server (FLUTE) | Number of File Delivery Tables sent over multicast. |
| multicast_files_received | files | Proxy (FLUTE) | Number of files received over multicast, including those partially recovered over unicast. |
| multicast_files_sent | files | Server (FLUTE) | Number of files sent over multicast. |
| multicast_packets_sent | ALCs | Server (FLUTE) | Number of ALC packets sent over multicast. |
| multicast_reception_time_before_deadline | ms | Proxy (FLUTE) | Time between completion and deadline. |
| multicast_reception_time_after_deadline | ms | Proxy (FLUTE) | Time between completion and deadline. |
| multicast_symbols_sent | symbols | Server (FLUTE) | Number of symbols sent over multicast. |
| multicast_transmission_time | ms | Server (FLUTE) | Time taken to transmit a file over multicast from queuing. |
| partial_parts | symbols | Server (HTTP) | Number of file symbols requested over HTTP. |
| partial_processing_duration | μs | Server (HTTP) | Time taken to process one partial request on the server. |
| partial_requests | requests | Server (HTTP) | Number of partial requests received by the server. |
| playback_rate | - | Client | Current rate of stream playback (1 = normal speed). |
| playing | - | Client | 1 if the client browser is playing the stream, 0 if not. |
| symbol_processing_time | ms | Proxy (FLUTE) | Time taken to process a symbol. |
| quality | - | Client | Quality index for the current segment in the stream. |
| ratio_average | - | Client | Average ratio of segment playback time to total download time over the last 4 segments. |
| ratio_high | - | Client | Maximum ratio of segment playback time to total download time over the last 4 segments. |
| ratio_low | - | Client | Minimum ratio of segment playback time to total download time over the last 4 segments. |
| reported_bitrate | kbps | Client | Bitrate of the video. |
| resolution_height | px | Client | Height of the stream. |
| resolution_width | px | Client | Width of the stream. |
| server_fetch_duration | μs | Proxy (HTTP) | Total time taken to fetch a file from the server. |
| symbols_received | symbols | Proxy (FLUTE) | Number of symbols received via multicast or recovered via unicast, excluding ignored symbols. |
