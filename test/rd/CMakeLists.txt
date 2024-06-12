add_definitions(-DUIP_CONF_IPV6=1 -DPROVISIONING_LIST_SIZE=232 -DTEST_PROBE_CC_VERSION)
add_definitions(-DCMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}" -DCMAKE_CURRENT_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}")

if(NOT APPLE)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror -Wparentheses -Wno-array-bounds -Wall -Wno-unused -Wextra -Wno-sign-compare -Wcomment -Wno-format-extra-args -Wmaybe-uninitialized -Wno-shadow -ggdb")
endif()

set(RD_BASIC_SRC
  ${CMAKE_SOURCE_DIR}/src/RD_internal.c
  ${CMAKE_SOURCE_DIR}/src/RD_DataStore_Sqlite.c
  ${CMAKE_SOURCE_DIR}/src/utls/zgw_str.c
  ${CMAKE_SOURCE_DIR}/src/zwdb.c
  ${CMAKE_SOURCE_DIR}/contiki/core/lib/assert.c
  ${CMAKE_SOURCE_DIR}/contiki/core/lib/list.c
  ${CMAKE_SOURCE_DIR}/contiki/core/lib/memb.c

  )

add_executable(test_rd_pvl_link test_rd_pvl_link.c
  ${CMAKE_SOURCE_DIR}/test/zipgateway_main_stubs.c
  ${CMAKE_SOURCE_DIR}/test/test_helpers.c
  ${CMAKE_SOURCE_DIR}/contiki/platform/linux/zgw_log_int.c
  )
target_compile_definitions(test_rd_pvl_link PRIVATE
  PROVISIONING_LIST_STORE_FILENAME_DEFAULT="provisioning_list_store.dat"
  ZGW_LOG ZGW_LOG_LOG_TO_FILE ZGW_LOG_LVL_INIT=5)
target_link_libraries(test_rd_pvl_link zipgateway-lib)


add_executable(test_rd_probe_cc_version test_rd_probe_cc_version.c
  ${RD_BASIC_SRC}
  ${CMAKE_SOURCE_DIR}/test/eeprom-stub.c
  ${CMAKE_SOURCE_DIR}/test/serialapi-stub.c
  ${CMAKE_SOURCE_DIR}/test/zipgateway_main_stubs.c
  ${CMAKE_SOURCE_DIR}/src/RD_probe_cc_version.c
  ${CMAKE_SOURCE_DIR}/test/test_helpers.c
  ${CMAKE_SOURCE_DIR}/test/test_gw_helpers.c
  ${CMAKE_SOURCE_DIR}/contiki/platform/linux/zgw_log_int.c
 )
target_link_libraries(test_rd_probe_cc_version sqlite3 s2_controller)


add_test(rd_pvl_link test_rd_pvl_link)
add_test(rd_probe_cc_version test_rd_probe_cc_version)
