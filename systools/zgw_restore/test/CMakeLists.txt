# We can put Wextra here, because we are not including Serialapi.c
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wextra -Wformat-extra-args -Warray-bounds")
if(APPLE)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter" )
endif()

add_definitions(-DZGW_LOG -DZGW_LOG_LOG_TO_FILE -DZGW_LOG_LVL_INIT=5)

include_directories( ${CMAKE_SOURCE_DIR}/test )
set(COMMON_TEST_SRC 
  ${ZGW_RESTORE_DIR}/test/test_restore_help.c
  ${CONTIKI_DIR}/platform/linux/zgw_log_int.c
  ${CMAKE_SOURCE_DIR}/test/test_helpers.c
  ${CMAKE_SOURCE_DIR}/test/zipgateway_main_stubs.c
)

include_directories( . )
add_definitions( -DTEST_SRC_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\" )
add_subdirectory(json_parse)

add_subdirectory(json_parse_eeprom_write)
add_subdirectory(restore_eeprom)
add_subdirectory(restore_pvl)

add_custom_target(zgwr_gcov
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND gcov -b -c ${GCOV_OBJECTS}
  )

  add_unity_test(test_state_down_grade test_state_down_grade.c   ${CMAKE_SOURCE_DIR}/test/zipgateway_main_stubs.c  )
  target_link_libraries(test_state_down_grade zgw_restore-lib json_helpers )