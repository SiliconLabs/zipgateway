# database
add_executable(test_restore_api test_restore_api.c ${COMMON_TEST_SRC})
target_link_libraries(test_restore_api  zgw_restore-lib)
add_test(restore_api test_restore_api)
 
# parser and database
add_executable(test_json_parse test_json_parse.c ${COMMON_TEST_SRC})
target_link_libraries(test_json_parse  zgw_restore-lib)
add_test(restore_json_parse test_json_parse)

# helpers
add_executable(test_json_parser_helpers  test_json_parser_helpers.c ${COMMON_TEST_SRC} )
target_link_libraries(test_json_parser_helpers  zgw_restore-lib )
add_test(restore_json_parser_helpers test_json_parser_helpers)



FILE(GLOB RESTORE_JSON_GCOV_OBJECTS
  ABSOLUTE "${CMAKE_CURRENT_BINARY_DIR}/*.o"
  "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/test_*.dir/__/__/*.o"
  "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/test_*.dir/__/__/__/__/src/*.o")

add_custom_target(restore_json_gcov
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND gcov -b ${RESTORE_JSON_GCOV_OBJECTS}
  )

SET(GCOV_OBJECTS ${GCOV_OBJECTS}  ${RESTORE_JSON_GCOV_OBJECTS} PARENT_SCOPE)
