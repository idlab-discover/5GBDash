# FLUTE

The FLUTE component provides a heavilly modified fork of the [5G Media Action Group (MAG) implementation of FLUTE](https://github.com/5G-MAG/rt-libflute), a protocol used for broadcast-based content delivery. Many new features have been added to this fork, such as support for packet recovery, FDT recovery, FEC and multithreading. This enables the FLUTE protocol to be used in a more robust and efficient manner.


## Requirements

The following packages are required to run libflute:
- libnl-3-dev

Additionally, these packages are required to build libflute:
- cmake
- ninja-build
- clang-tidy
- libconfig++-dev
- libboost-all-dev

## Installation Guide

### Step 1: Build Setup
Create a build directory and configure the build using CMake:
```commandline
mkdir build && cd build
cmake -GNinja -DCMAKE_BUILD_TYPE=Release ..
```
Optionally, you can build a debug version of libflute by using `-DCMAKE_BUILD_TYPE=Debug` to the CMake command.

### Step 2: Building
Compile the project using Ninja:
```commandline
ninja
```