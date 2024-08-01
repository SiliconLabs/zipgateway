# Build doxygen documentation

## Dependencies

- Doxygen 1.8.18 or later (http://www.doxygen.nl/index.html)

## Instructions

1. Get doxygen 1.8.18
```bash
apt-get update
apt-get install -y cmake libssl-dev libavahi-client-dev libxml2-dev \
   libbsd-dev libncurses5-dev gcc ninja-build python zip curl flex bison
curl -L http://doxygen.nl/files/doxygen-1.8.18.src.tar.gz --output /tmp/doxygen.tar.gz
cd /tmp/ && tar -xvf doxygen.tar.gz
cd /tmp/doxygen*/
mkdir build && cd build
cmake -GNinja ..
ninja && ninja install
```

2. Build the doxygen doc
```bash
unzip libzwaveip-*.release-rpi.zip
cd libzwaveip-*.release-rpi
mkdir build
cd build
cmake ..
make doc
```

## Alternative

One docker file, /docker/x86_64_ubuntu_18_04/Dockerfile, is provided to
prepare the environment for building documentation as well as the project.
