add_definitions( -DTEST_MULTICAST_TX)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-undef -ggdb")

include_directories(${CMAKE_SOURCE_DIR}/contiki/core/sys  ${CMAKE_SOURCE_DIR}/Z-Wave/include
${CMAKE_SOURCE_DIR}/contiki/cpu/native ${CMAKE_SOURCE_DIR}/src/ ${CMAKE_SOURCE_DIR}/test)

add_executable(test_multicast_tlv
  ${CMAKE_SOURCE_DIR}/test/multicast_tlv/test_multicast_tlv.c
  ${CMAKE_SOURCE_DIR}/src/multicast_tlv.c
  ${CMAKE_SOURCE_DIR}/src/utls/zgw_nodemask.c
  ${CMAKE_SOURCE_DIR}/test/test_helpers.c
  ${CMAKE_SOURCE_DIR}/test/test_gw_helpers.c
  ${CMAKE_SOURCE_DIR}/contiki/core/lib/assert.c
)

add_test(multicast_tlv test_multicast_tlv)
