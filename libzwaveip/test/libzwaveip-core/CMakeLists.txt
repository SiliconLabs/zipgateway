add_executable(test_zresource
  test_zresource.c
  ../../libzwaveip/zresource.c
  ../../libzwaveip/libzw_log.c
)

target_include_directories(test_zresource
  PUBLIC
  ../../include
  ../../libzwaveip
)

target_link_libraries(test_zresource libzipgateway_unity)
add_test(zresource test_zresource)
