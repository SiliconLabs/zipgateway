add_executable(test_base64 test_base64.c ${CMAKE_SOURCE_DIR}/test/test_helpers.c)
target_link_libraries(test_base64 app_utils)
target_include_directories(test_base64 PRIVATE
                             ${CMAKE_SOURCE_DIR}/test
                             ${CMAKE_SOURCE_DIR}/contiki/core # for lib/zgw_log.h
                          )

add_test(test_libs_app_utils_base64 test_base64)
