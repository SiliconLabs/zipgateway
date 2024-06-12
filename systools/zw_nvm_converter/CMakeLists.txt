pkg_check_modules(JSON_C REQUIRED IMPORTED_TARGET json-c)

add_library (controllerlib_api INTERFACE)
target_include_directories(controllerlib_api INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include" ${JSON_C_INCLUDE_DIRS})

add_subdirectory( nvm3lib )
add_subdirectory( controllerlib_7.xx )
add_subdirectory( controllerlib_6.xx )

add_executable( zw_nvm_converter nvmconv_main.c )
target_link_libraries( zw_nvm_converter
                       controllerlib_api
                       controllerlib_7xx
                       controllerlib_6xx
                     )

install(TARGETS zw_nvm_converter DESTINATION bin)
