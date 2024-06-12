# this one is important
SET(CMAKE_SYSTEM_NAME Linux)
SET(CMAKE_SYSTEM_PROCESSOR mips)
SET(CONFIGURE_HOST mips-linux)
#this one not so much
SET(CMAKE_SYSTEM_VERSION 1)

# specify the cross compiler
SET(CMAKE_C_COMPILER   mips-linux-gnu-gcc)
SET(CMAKE_CXX_COMPILER mips-linux-gnu-g++)

SET(CPACK_DEBIAN_PACKAGE_ARCHITECTURE mips)
SET(CPACK_FILE_NAME_EXTRA stretch)

# Configure pkg-config to only find target libs
SET(PKG_CONFIG_EXECUTABLE "/usr/bin/mips-linux-gnu-pkg-config" CACHE PATH "pkg-config")
SET(ENV{PKG_CONFIG_PATH} "/usr/lib/mips-linux-gnu/pkgconfig")
