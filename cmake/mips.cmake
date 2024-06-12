set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR mips)
set(CONFIGURE_HOST mips-linux)
set(CMAKE_SYSTEM_VERSION 1)
set(OPENWRT 1)

# Search for programs in the custom directories ONLY
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# for libraries and headers in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(OPENSSL_USE_STATIC_LIBS TRUE)
set(OPENSSL_ROOT_DIR ${OPENSSL_SRC})
set(CUSTOM_PKG_CONFIG_PATH "${LIBUSB_SRC}/lib/pkgconfig:${JSON_C_SRC}/lib/pkgconfig")
set(CMAKE_C_COMPILER "mips-openwrt-linux-musl-gcc")
