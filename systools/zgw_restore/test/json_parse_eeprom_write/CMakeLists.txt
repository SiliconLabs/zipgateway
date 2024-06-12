

add_executable(test_json_parse_eeprom_write
  test_json_parse_eeprom_write.c
  ${COMMON_TEST_SRC}
  )

target_link_libraries(test_json_parse_eeprom_write zgw_restore-lib)
add_test(restore_json_parse_eeprom_write test_json_parse_eeprom_write)

# Test the json files we give as examples.

add_definitions(-DEXAMPLE_JSON_ROOT_PATH="${CMAKE_SOURCE_DIR}/systools/doc")
add_executable(test_restore_example_json 
  test_restore_example_json.c
  ${COMMON_TEST_SRC}
)

target_link_libraries(test_restore_example_json zgw_restore-lib )
add_test(restore_example_json test_restore_example_json)

FILE(GLOB RESTORE_JSON_EEPROM_GCOV_OBJECTS
  ABSOLUTE "${CMAKE_CURRENT_BINARY_DIR}/*.o"
  "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/test_*.dir/__/__/*.o"
  "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/test_*.dir/__/__/__/__/src/*.o")

add_custom_target(json_eeprom_gcov
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND gcov -b ${RESTORE_JSON_EEPROM_GCOV_OBJECTS}
  )

SET(GCOV_OBJECTS ${GCOV_OBJECTS}  ${RESTORE_JSON_EEPROM_GCOV_OBJECTS} PARENT_SCOPE)
