add_definitions( -DMDNS_TEST )
include_directories( ${CMAKE_SOURCE_DIR}/src/ )
include_directories( 
.
${CMAKE_SOURCE_DIR}Z-Wave/include/ 
${CMAKE_SOURCE_DIR}contiki/core/
${CMAKE_SOURCE_DIR}contiki/platform/linux/
${CMAKE_SOURCE_DIR}contiki/cpu/native/
${CMAKE_SOURCE_DIR}src/serialapi/
${CMAKE_SOURCE_DIR}libs2/include/
${CMAKE_SOURCE_DIR}contiki/core/sys/
)
remove_definitions(-Werror -Wparentheses -Wno-array-bounds -Wall -Wextra -Wunused -Wcomment 
  -Wformat -Wmaybe-uninitialized -Wshadow)

add_unity_test(mdns_test
  mdns_test.c
  mdns_test_stub.c
  ${CMAKE_SOURCE_DIR}/src/mDNSService.c
  ${CMAKE_SOURCE_DIR}/contiki/core/lib/list.c
  ${CMAKE_SOURCE_DIR}/contiki/core/lib/memb.c
  ${CMAKE_SOURCE_DIR}/contiki/core/lib/assert.c
  ${CMAKE_SOURCE_DIR}/contiki/platform/linux/clock.c
  ${CMAKE_SOURCE_DIR}/src/zwdb.c
)

