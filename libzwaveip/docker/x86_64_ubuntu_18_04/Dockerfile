FROM ubuntu:18.04

RUN apt-get update \
    && apt-get install -y cmake libssl-dev libavahi-client-dev libxml2-dev \
       libbsd-dev libncurses5-dev gcc ninja-build python zip curl flex bison git \
    && rm -rf /var/lib/apt/lists/*

# Install doxygen 1.8.18 to fix issues with preprocessor defines in code blocks in markdown files
RUN curl -L https://github.com/doxygen/doxygen/archive/Release_1_8_20.zip --output /tmp/doxygen.zip \
    && cd /tmp/ \
    && echo 1d31d5dd3b486aef55ba77d16ef3307a9d936518725b639c5709a1d859f0f732a4e9a13f457340d4f18bd8a07bd1c96ba5cc91abaedd69896142331e19fc8540 doxygen.zip > /tmp/doxygen.zip.sha512 \
    && sha512sum -c doxygen.zip.sha512 \
    && unzip doxygen.zip \
    && cd /tmp/doxygen*/ \
    && mkdir build && cd build \
    && cmake -GNinja .. \
    && ninja && ninja install \
    && rm -rf /tmp/doxygen*