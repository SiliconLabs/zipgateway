set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-undef")

include_directories(${CMAKE_SOURCE_DIR}/contiki/core/sys  ${CMAKE_SOURCE_DIR}/Z-Wave/include
${CMAKE_SOURCE_DIR}/contiki/cpu/native ${CMAKE_SOURCE_DIR}/src/transport  ${CMAKE_SOURCE_DIR}/test)

add_executable(test_CC_Indicator test_CC_Indicator.c
  ${CMAKE_SOURCE_DIR}/src/CC_Indicator.c
  ${TEST_HELPERS}
  ${CMAKE_SOURCE_DIR}/test/test_CC_helpers.c
  ${CMAKE_SOURCE_DIR}/contiki/core/lib/assert.c
)

add_test(CC_Indicator test_CC_Indicator)
