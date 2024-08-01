# Build Instructions for Other Platforms

## MacOS

Builds on macOS have been tested with Homebrew packages.

**CMake:**

The recommended way of building libzwaveip and the example applications for
macOS is using CMake. Building with CMake requires that you provide the
dependencies yourself. The supported method of doing this is with
[Homebrew](http://brew.sh/). Once you have Homebrew installed, you can download
all the required libraries with this command:

```bash
brew install cmake openssl doxygen
```

Once you have the dependencies installed, you can build the project using CMake
with the following commands (from inside the project’s directory):

``` bash
mkdir build
cd build
cmake -DOPENSSL_ROOT_DIR=/usr/local/opt/openssl/ -DOPENSSL_LIBRARIES=/usr/local/opt/openssl/lib/ ..
make
```

Make sure to supply the paths to the OpenSSL installed with Homebrew to CMake.

# Ubuntu 18.04 LTS 64-bit

Building libzwaveip with sample applications on Ubuntu is no different than on
Raspberry Pi.

```bash
sudo apt-get update
sudo apt-get install cmake libssl-dev libavahi-client-dev libxml2-dev libbsd-dev libncurses5-dev gcc python zip
unzip libzwaveip-*.release-rpi.zip
cd libzwaveip-*.release-rpi
mkdir build
cd build
cmake ..
make
```
