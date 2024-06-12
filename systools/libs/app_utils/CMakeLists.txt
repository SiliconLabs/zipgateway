add_library(app_utils user_message.c base64.c)

target_include_directories(app_utils PUBLIC
                              "${CMAKE_CURRENT_SOURCE_DIR}"
                          )

if (NOT DEFINED SKIP_TESTING)
  message(STATUS "Adding unit test for app_utils")
  add_subdirectory( test )
endif()
