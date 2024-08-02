add_library(libzipgateway_unity STATIC ${CMAKE_CURRENT_SOURCE_DIR}/unity.c)
target_include_directories(libzipgateway_unity PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
