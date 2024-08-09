
# The contiki scheduler, utilities, and IP stack
set(CONTIKI_CORE_SRC
core/dev/serial-line.c
core/lib/crc16.c
core/lib/list.c
core/lib/memb.c
core/lib/random.c
core/lib/ringbuf.c
core/net/resolv.c
core/net/tcpip.c
core/net/uip-debug.c
core/net/uip-ds6.c
core/net/uip-icmp6.c
core/net/uip-nd6.c
core/net/uip-packetqueue.c
core/net/uip-split.c
core/net/uip-udp-packet.c
core/net/uip6.c
core/net/uiplib.c
core/sys/autostart.c
core/sys/ctimer.c
core/sys/etimer.c
core/sys/process.c
core/sys/procinit.c
core/sys/stimer.c
core/sys/timer.c
platform/linux/./clock.c
platform/linux/./random.c
core/lib/assert.c
core/net/tcpip_ipv4.c
core/net/uip-ipv4-split.c
core/net/uip-ipv4-udp-packet.c
core/net/uip.c
core/net/uip_arp.c
)

set(CONTIKI_INC
platform/linux 
platform/linux/dev 
cpu/native 
cpu/native/net 
core/dev 
core/lib 
core/net 
core/sys 
core 
)

set(CONTIKI_SRC
  ${CONTIKI_CORE_SRC}
)
add_library(contiki-core ${CONTIKI_CORE_SRC} )

include_directories(
  ${CMAKE_CURRENT_BINARY_DIR}/..
)
  
target_include_directories(contiki-core PUBLIC
  ${CONTIKI_INC}
  ..
  ../Z-Wave/include
  ../src
  ../src/transport
  ../src/utls
)

# Add the ZGW-specific stuff to the basic contiki stuff
target_compile_definitions(contiki-core
  PUBLIC
  -DPROJECT_CONF_H=\"project-conf.h\"
  -DCONTIKI_TARGET_LINUX
  -DUIP_CONF_IPV6=1
  -DAUTOSTART_ENABLE
  )

target_compile_options( contiki-core PUBLIC -Wno-address-of-packed-member )
if (NOT ANDROID)
    # Add the linux platform stuff if we are on linux
    list (APPEND CONTIKI_SRC
        cpu/native/net/tapdev-drv.c
        platform/linux/parse_config.c
        platform/linux/RF_Region_Set_Validator.c
        cpu/native/net/tapdev6.c
        cpu/native/linux-serial.c
        platform/linux/zgw_backup_ipc.c
    )
  add_library(
    contiki-main
    platform/linux/contiki-main.c
  )
  target_include_directories(contiki-main PUBLIC
    ${CONTIKI_INC}
    ..
    ../src
  )
    target_compile_definitions(contiki-main PUBLIC -DPROJECT_CONF_H=\"project-conf.h\" -DCONTIKI_TARGET_LINUX -DUIP_CONF_IPV6=1 -DAUTOSTART_ENABLE )
endif()

add_library(contiki ${CONTIKI_SRC} )

target_include_directories(contiki PUBLIC ${CONTIKI_INC})
target_compile_definitions(contiki PUBLIC -DPROJECT_CONF_H=\"project-conf.h\" -DCONTIKI_TARGET_LINUX -DUIP_CONF_IPV6=1 -DAUTOSTART_ENABLE )

if( NOT APPLE )
target_link_libraries(contiki rt)
endif()

target_link_libraries(contiki zipgateway-lib)
