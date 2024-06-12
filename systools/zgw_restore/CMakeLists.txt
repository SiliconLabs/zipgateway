pkg_check_modules(JSON_C REQUIRED IMPORTED_TARGET json-c)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror -Wparentheses -Wall -Wcomment -Wuninitialized")

#  -Wextra
# Serialapi.c does not compile with Wextra

# -Wundef does not work with project-conf.h because of MIPSEB
set (ZGW_RESTORE_DIR ${CMAKE_CURRENT_SOURCE_DIR} )

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused -Wno-unused-parameter" )

add_library( zgw_restore-lib
  zgwr_eeprom.c
  zgwr_membuf_write.c
  zgwr_pvl.c
  zgwr_serial_read.c
  zgwr_json_parser.c
#  zgwr_json_parser_zw.c
  zgwr_json_parser_helpers.c
  zgw_data.c
  zw_data.c
  zgw_restore_cfg.c
)
target_include_directories(zgw_restore-lib PUBLIC . )
target_link_libraries(zgw_restore-lib PUBLIC PkgConfig::JSON_C zipgateway-lib  )

add_executable(zgw_restore zgw_restore.c   ${CMAKE_SOURCE_DIR}/test/zipgateway_main_stubs.c)
target_link_libraries(zgw_restore PUBLIC zgw_restore-lib)


if (NOT DEFINED SKIP_TESTING)
  add_subdirectory( test )
  message(STATUS "Adding unit test for zgw_restore")
endif ()


install(TARGETS zgw_restore DESTINATION bin)
