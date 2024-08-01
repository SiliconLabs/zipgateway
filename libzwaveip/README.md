# libzwaveip - Control Z-Wave devices from your IP network

libzwaveip is a library that makes it easy to control Z-Wave devices from your IP network via a Z/IP Gateway.

In addition to the library, two sample applications are provided: reference_client and reference_listener. The Z-Wave reference_client is a command line client for adding, removing and sending commands to Z-Wave nodes, whereas the Z-Wave reference_listener listens for notifications from the Z-Wave network so that when a notification arrives, it is decoded and pretty-printed.

## Key features

- Z-Wave over IP library includes API for
  - Z/IP service and network management
  - DTLS / non-DTLS connection establishment and teardown
  - mDNS resource discovery
  - Fine severity level of logging with timestamp
- Example applications built on top of Z/IP library
  - reference client provides a command-line interface for classic network
  management, SmartStart node provisioning, and sending frames to Z-Wave nodes
  - reference listener offers decoded and pretty-printed notifications from
  Z-Wave network

## Requirements

### Hardware

You'll need a few pieces of hardware in order to be able to work with Z-Wave
Over IP:

- Raspberry Pi 3B+ (Stretch)
- SD Card (at least 2GB)
- Bridge Controller (UZB dongle or Z-Wave development board)
- Devices that speak Z-Wave

### Software

In order to talk to Z-Wave network over IP, having a Z/IP Gateway is necessary.
  To access Z/IP Gateway, please use Simplicity Studio, available from [Silicon
Labs](https://www.silabs.com)

### Supported target platform

- Ubuntu 18.04 LTS 64-bit
- Raspberry Pi 3B+ Raspbian GNU/Linux 9.3 stretch

## Quickstart

### Build instructions for Raspberry Pi

Follow these instructions to build the libzwaveip together with sample
applications:

```bash
sudo apt-get update
sudo apt-get install cmake libssl-dev libavahi-client-dev libxml2-dev \
      libbsd-dev libncurses5-dev git python3 build-essential
unzip libzwaveip-*.release-rpi.zip
cd libzwaveip-*.release-rpi
mkdir build
cd build
cmake ..
make
```

To test the reference_client, make sure Z/IP Gateway is running and connect to
it:

```bash
./reference_client -s <IP of ZIP Gateway>
```

### Other references

- [Using the example Applications](documentation/Using-the-Example-Applications.md)
- [Build instructions for MacOS and Ubuntu](documentation/Build-osx-ubuntu.md)
- [Build instructions for doxygen documentation](documentation/Build-doxygen-documentation.md)

## Questions or need help

Questions? Please use the discussion forum at
[https://www.silabs.com/community/wireless/z-wave/forum](https://www.silabs.com/community/wireless/z-wave/forum).

## Copyright and licence

Copyright 2020 Silicon Laboratories Inc. under Apache License, Version 2.0
