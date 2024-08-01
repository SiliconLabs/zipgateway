include_directories(
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/libzwaveip
)
add_executable(test_zresource test_zresource.c ${CMAKE_SOURCE_DIR}/libzwaveip/zresource.c ${CMAKE_SOURCE_DIR}/libzwaveip/libzw_log.c)
target_link_libraries(test_zresource unity)
add_test(zresource test_zresource)
