add_unity_test(NAME test_smalloc FILES test_smalloc ${ZGW_SRC_DIR}/smalloc.c )
target_include_directories(test_smalloc PRIVATE ${ZGW_SRC_DIR})
target_compile_options(test_smalloc PUBLIC -UNDEBUG )


add_unity_test(test_datastore test_datastore.c ${CMAKE_SOURCE_DIR}/src/RD_DataStore_Sqlite.c
)
target_link_libraries(test_datastore sqlite3 contiki )

FILE(GLOB DATASTORE_GCOV_OBJECTS
  ABSOLUTE "${CMAKE_CURRENT_BINARY_DIR}/*.o"
  "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/test_*.dir/__/__/src/*.o")

add_custom_target(datastore_gcov
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND gcov -b -c ${DATASTORE_GCOV_OBJECTS}
  DEPENDS test_smalloc
  )

SET(GCOV_OBJECTS ${GCOV_OBJECTS} ${DATASTORE_GCOV_OBJECTS} PARENT_SCOPE)
