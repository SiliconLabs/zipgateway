# this one is important
SET(CMAKE_SYSTEM_NAME Linux)
SET(CMAKE_SYSTEM_PROCESSOR armhf) 
SET(CONFIGURE_HOST arm-linux)
#this one not so much
SET(CMAKE_SYSTEM_VERSION 1)

# specify the cross compiler
SET(CMAKE_C_COMPILER   arm-linux-gnueabihf-gcc)
SET(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

# where is the target environment
#SET(CMAKE_FIND_ROOT_PATH /usr/arm-linux-gnueabihf)
SET(CMAKE_FIND_ROOT_PATH $ENV{CROSS_ENV_ROOT})

#message(ROOT ${CMAKE_FIND_ROOT_PATH})

# search for programs in the build host directories
SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# for libraries and headers in the target directories
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

SET(LIBSSL_DEBIAN_PACKAGE "libssl1.0.0")
SET(CPACK_DEBIAN_PACKAGE_ARCHITECTURE armhf)
