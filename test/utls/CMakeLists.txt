if(NOT APPLE)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror -Wparentheses -Wno-array-bounds -Wall -Wno-unused -Wextra -Wno-sign-compare -Wcomment -Wno-format-extra-args -Wmaybe-uninitialized -Wno-shadow -ggdb")
endif()

add_executable(test_zgw_str test_zgw_str.c
  ${CMAKE_SOURCE_DIR}/src/utls/zgw_str.c
  ${CMAKE_SOURCE_DIR}/test/test_helpers.c
  ${CMAKE_SOURCE_DIR}/test/test_gw_helpers.c
  ${CMAKE_SOURCE_DIR}/contiki/platform/linux/zgw_log_int.c
)

add_test(zgw_utls test_zgw_str)
