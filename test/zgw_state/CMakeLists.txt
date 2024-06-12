set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ggdb")

set(MOCK_CONTIKI
  ${CMAKE_SOURCE_DIR}/test/mock_tapdev.c
  ${CMAKE_SOURCE_DIR}/test/mock_conhandle.c
  ${CMAKE_SOURCE_DIR}/test/mock_parse_config.c
  ${CMAKE_SOURCE_DIR}/test/mock_bridge_controller.c
  ${CMAKE_SOURCE_DIR}/test/mock_contiki_main.c
  ${CMAKE_SOURCE_DIR}/test/mock_contiki_init.c
  ${CMAKE_SOURCE_DIR}/test/mock_contiki_loop.c
)

add_executable(test_zgw_state test_zgw_state.c
  mock_dhcp.c stub_zw_fw_upd.c
  mock_config.c
  ${MOCK_CONTIKI}
  ${CMAKE_SOURCE_DIR}/test/test_helpers.c
  ${CMAKE_SOURCE_DIR}/test/test_utils.c
# Until we find a better fix for the zgw_backup signal issues, this test case cannot use the generic stubs.
#  ${CMAKE_SOURCE_DIR}/test/zipgateway_main_stubs.c
  ${CMAKE_SOURCE_DIR}/src/ZIP_Router.c
  ${CMAKE_SOURCE_DIR}/contiki/platform/linux/zgw_log_int.c
  ${CMAKE_SOURCE_DIR}/contiki/platform/linux/RF_Region_Set_Validator.c
)

target_compile_definitions(test_zgw_state PUBLIC
  ZGW_LOG ZGW_LOG_LVL_INIT=5 ZGW_LOG_COLOR
  NO_ZW_NVM
  TEST_BACKUP_DIR="${CMAKE_CURRENT_BINARY_DIR}/thebackup/"
  TEST_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/"
  # To run in ctest context, the gateway has to fake signal sending by mocking the signal sending logic of zgw_backup.c
  NO_SIGNALS
  # whether the first call to protocol should report no virtual nodes
  FIRST_RUN
  AUTOSTART_ENABLE
  )
#  ZGW_LOG ZGW_LOG_LOG_TO_FILE ZGW_LOG_LVL_INIT=5)

target_include_directories(test_zgw_state
  PUBLIC
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CONTIKI_INC}
  "${CMAKE_SOURCE_DIR}/libs2/transport_service"
  "${CMAKE_SOURCE_DIR}/libs2/include")

# gateway lib for the test
list (APPEND GW_SRC
    ${CMAKE_SOURCE_DIR}/src/zwpgrmr/linux_usb_interface.c
    ${CMAKE_SOURCE_DIR}/src/ZWFirmwareUpdate.c
    ${CMAKE_SOURCE_DIR}/src/ZW_tcp_client.c
)

add_library(zipgateway-lib-mock ${GW_SRC})

add_definitions(-DZIP_ND6)

target_include_directories(zipgateway-lib-mock PUBLIC 
  ${CMAKE_CURRENT_BINARY_DIR}
  ${LibUSB_INCLUDE_DIRS}
  ${TS2_INCLUDE_DIRS}
  ${ZWAVE_INCLUDE_DIR}
  serialapi
  ${CMAKE_SOURCE_DIR}/src-gen
  zwpgrmr
  transport
  utls
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  )
target_link_libraries(zipgateway-lib-mock PUBLIC
  ZWaveAnalyzer
  contiki
  ${S2_LIBRARIES}
  )

if(${DISABLE_DTLS})
    message("Gatewway if configured without DTLS surrpot.")
    target_compile_definitions(zipgateway-lib-mock PUBLIC -DDEBUG_ALLOW_NONSECURE -DDISABLE_DTLS )
endif()

add_definitions( -DSUPPORTS_MDNS)

target_compile_definitions(zipgateway-lib-mock PUBLIC
  ZGW_LOG ZGW_LOG_LVL_INIT=5
  NO_ZW_NVM
  )
target_compile_options( zipgateway-lib-mock PUBLIC )

add_library(nvm-file-mock ${CMAKE_SOURCE_DIR}/src/zw_appl_nvm.c)
target_include_directories(nvm-file-mock PUBLIC 
  ${CMAKE_CURRENT_BINARY_DIR} 
  ${TS2_INCLUDE_DIRS}
  ${ZWAVE_INCLUDE_DIR}
  ${LibUSB_INCLUDE_DIRS}
  serialapi
  ../src-gen
  zwpgrmr
  transport
  utls
  .. .
  )
target_compile_definitions(nvm-file-mock PUBLIC
  ZGW_LOG ZGW_LOG_LVL_INIT=5
  NO_ZW_NVM
  )

# end

target_link_libraries(test_zgw_state contiki-core nvm-file-mock zipgateway-lib-mock)

FILE(GLOB ZSTATE_GCOV_OBJECTS
  ABSOLUTE "${CMAKE_CURRENT_BINARY_DIR}/*.o"
  "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/test_*.dir/__/__/src/*.o")
SET(GCOV_OBJECTS ${GCOV_OBJECTS}  ${ZSTATE_GCOV_OBJECTS} PARENT_SCOPE)

add_custom_target(zgw_state_gcov
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND gcov -b -c ${ZSTATE_GCOV_OBJECTS}
)

add_test(zgw_state test_zgw_state)
