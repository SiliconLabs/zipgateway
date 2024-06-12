# the name of the target operating system
set(CMAKE_SYSTEM_NAME Linux)
SET(CMAKE_SYSTEM_VERSION 1)
# Target system Processor
set(CMAKE_SYSTEM_PROCESSOR "i386")

# which compilers to use for C and C++
SET(CMAKE_C_FLAGS             "-m32" CACHE STRING "C compiler flags"   FORCE)
SET(CMAKE_CXX_FLAGS           "-m32" CACHE STRING "C++ compiler flags" FORCE)

# Get OS ID (e.g. ubuntu)
execute_process (
    COMMAND bash -c "awk -F= '/^ID=/{print $2}' /etc/os-release | tr -d 'ID=' | tr -d '\n' | tr -d '\"'"
    OUTPUT_VARIABLE OS_RELEASE_ID
)
# Get OS Version (e.g. 20.04)
execute_process (
    COMMAND bash -c "awk -F= '/^VERSION_ID=/{print $2}' /etc/os-release | tr -d 'VERSION_ID=' |tr -d '\n' | tr -d '\"'"
    OUTPUT_VARIABLE OS_RELEASE_VERSION_ID
)
if(${OS_RELEASE_ID} MATCHES "ubuntu" AND ${OS_RELEASE_VERSION_ID} MATCHES "20.04")
    message(STATUS "Ubuntu 20.04")
    SET(CPACK_FILE_NAME_EXTRA "ubuntu_20_04")
    SET(CPACK_DEBIAN_PACKAGE_DEPENDS "libusb-1.0-0, libssl1.1, radvd:amd64 | i386, parprouted:amd64 | i386, bridge-utils:amd64 | i386, libjson-c2 | libjson-c3 | libjson-c4, net-tools, zip, unzip")
    SET(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/files/scripts/20_04/postinst;${CMAKE_CURRENT_SOURCE_DIR}/files/scripts/config;${CMAKE_CURRENT_SOURCE_DIR}/files/scripts/postrm;${CMAKE_CURRENT_SOURCE_DIR}/files/scripts/templates;${CMAKE_CURRENT_BINARY_DIR}/conffiles" )
endif()
