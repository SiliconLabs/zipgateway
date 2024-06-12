ExternalProject_Add(
  libpcap 
  URL https://www.tcpdump.org/release/libpcap-1.5.3.tar.gz
  CONFIGURE_COMMAND ./configure CC=${CMAKE_C_COMPILER} LDFLAGS=${CMAKE_C_FLAGS} CFLAGS=${CMAKE_C_FLAGS}  --host=${CONFIGURE_HOST} --with-pcap=linux --enable-canusb=no --enable-dbus=no --without-flex --without-libnl --prefix=${CMAKE_BINARY_DIR}/stage
  BUILD_IN_SOURCE 1
)

add_definitions( -D_GNU_SOURCE )

include_directories( ${CMAKE_BINARY_DIR}/stage/include )
link_directories( ${CMAKE_BINARY_DIR}/stage/lib )

add_executable( udprelay udprelay.c)
target_link_libraries( udprelay libpcap.a )
add_dependencies(udprelay libpcap)

install(TARGETS udprelay RUNTIME DESTINATION sbin )
