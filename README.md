# 5GBDash

This repository provides the tools required to set up an emulated network that allows for hybrid unicast- and broadcast-based content delivery. Specifically, it supports dynamic adaptive streaming over HTTP (DASH) through the dash.js web player.

## Getting Started

To run the demo, make sure to go through all components and install all the required packages. Once all packages are installed, you can run the demo as described in the emulation component.

## Components

Currently, four major components can be discerned:

- [Emulation](emulation): Provides network emulation through Mininet and the required scripts to start servers, proxies, and clients. Additionally, it offers a demo application for easy experimentation with the network setup.

- [FLUTE](flute): Contains a fork of the 5G Media Action Group (MAG) implementation of FLUTE, a protocol used for broadcast-based content delivery.

- [NALU](nalu): Contains a fork of [NALUProcessing](https://github.com/IDLabMedia/NALUProcessing), which allows processing of Network Abstraction Layer Units (NALUs) in video segments to enable Temporal Layer Injection (TLI).

- [Proxy](proxy): Provides the proxy logic, which caches incoming video segments sent over broadcast and serves them to the client through a custom HTTP server.

- [Server](server): Contains server logic, including preprocessing (e.g., video encoding), broadcast delivery, and a custom HTTP server for unicast delivery.

Additionally, the [Evaluation](evaluation) component contains notebooks used to evaluate the performance of the other components.

Each component has a separate README document for detailed instructions. It is advisable to read these documents first.

## Contact

If you have any questions or concerns, please feel free to contact us at [casper.haems@ugent.be](mailto:casper.haems@ugent.be) or [jeroen.vanderhooft@ugent.be](mailto:jeroen.vanderhooft@ugent.be).

# References

If you use (parts of) this code, please cite the following paper:
```bibtex
@inproceedings{10.1145/3651863.3651880,
    author = {Haems, Casper and {van der Hooft}, Jeroen and Mareen, Hannes and Steenkiste, Peter and Van Wallendael, Glenn and Wauters, Tim and De Turck, Filip},
    title = {Enabling adaptive and reliable video delivery over hybrid unicast/broadcast networks},
    year = {2024},
    isbn = {9798400706134},
    publisher = {Association for Computing Machinery},
    address = {New York, NY, USA},
    url = {https://doi.org/10.1145/3651863.3651880},
    doi = {10.1145/3651863.3651880},
    abstract = {The increasing demand for high-quality video streaming, coupled with the necessity for low-latency delivery, presents significant challenges in today's multimedia landscape. In response to these challenges, this research explores the optimization of adaptive video streaming by integrating 5G terrestrial broadcasting with over-the-top (OTT) streaming methods. A comprehensive integration of forward error correction (FEC), temporal layer injection (TLI), and broadcast techniques enhance the robustness and efficiency of content delivery over broadcast networks and reduce unicast bandwidth to zero in low loss environments. Multiple strategies are compared through an extensive emulation setup for reducing latency in the end-to-end video delivery chain to sub 3-second live latency, demonstrating the effectiveness of a hybrid unicast-broadcast approach in achieving low-latency while maintaining high-quality video streaming performance with significantly reduced bandwidth. For 62.99\% of viewers, unicast bandwidth can be reduced to as low as zero when broadcasting the top 3 TV channels.},
    booktitle = {Proceedings of the 34th Edition of the Workshop on Network and Operating System Support for Digital Audio and Video},
    pages = {29–35},
    numpages = {7},
    keywords = {video delivery, multimedia streaming, 5G terrestrial broadcast, low latency},
    location = {Bari, Italy},
    series = {NOSSDAV '24}
}
```
