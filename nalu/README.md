# NALUProcessing

NALUProcessing is a tool for manipulating Network Abstraction Layer Units (NALU) of video streams encoded using the H.264/AVC, H.265/HEVC, or H.266/VVC standards. This tool enables the manipulation of video streams without re-encoding, thus avoiding quality loss.
The component included in this repository is a fork of the original [NALUProcessing tool](https://github.com/IDLabMedia/NALUProcessing) to fit the specific needs of this project.

## Features
- Extracting frames based on their index from an H.264 file.
- Replacing specific frames with those from a different video stream.
- Concatenating frames from different quality representations to create an intermediate quality representation.

## Supported Codecs
- H.264/AVC
- H.265/HEVC (unsupported in this fork)
- H.266/VVC (unsupported in this fork)

## Installation Guide

### Step 1: Build Setup
Create a build directory and configure the build using CMake:
```commandline
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
```
Optionally, you can build a debug version of libflute by using `-DCMAKE_BUILD_TYPE=Debug` to the CMake command.

### Step 2: Building
Compile the project using make:
```commandline
make
```