set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ggdb")

set(MOCK_CONTIKI_DEFAULT
  # Modify this component mock to affect the configuration
  ${CMAKE_SOURCE_DIR}/test/mock_parse_config.c

  # Include this component mock if you want to use the LAN side.
  #${CMAKE_SOURCE_DIR}/test/mock_tapdev.c

  # Include these component mocks if you want to use the PAN side
  # This mocks the serial IF (at an uncomfortably low level)
  ${CMAKE_SOURCE_DIR}/test/mock_conhandle.c
  # This mocks the protocol
  ${CMAKE_SOURCE_DIR}/test/mock_bridge_controller.c

  # The next three files set up the contiki test framework
  ${CMAKE_SOURCE_DIR}/test/mock_contiki_main.c
  ${CMAKE_SOURCE_DIR}/test/mock_contiki_init.c
  ${CMAKE_SOURCE_DIR}/test/mock_contiki_loop.c
)

add_executable(test_contiki
  test_contiki.c
  # The list of contiki processes that should be in the test system.
  mock_contiki_autostart.c
  # The configuration settings for this test case (what goes into struct router_config).
  mock_config.c
  ${MOCK_CONTIKI_DEFAULT}
  ${CMAKE_SOURCE_DIR}/test/test_helpers.c
  ${CMAKE_SOURCE_DIR}/test/test_utils.c
  ${CMAKE_SOURCE_DIR}/test/zipgateway_main_stubs.c
#  ${CMAKE_SOURCE_DIR}/src/ZIP_Router.c
  ${CMAKE_SOURCE_DIR}/contiki/platform/linux/zgw_log_int.c
)

target_compile_definitions(test_contiki PUBLIC
  # Use ZGW logging to file
  ZGW_LOG ZGW_LOG_LVL_INIT=5 ZGW_LOG_LOG_TO_FILE ZGW_LOG_COLOR
  # Old ZGW flag that keeps the application NVM area on the file
  # system instead - very useful for testing.
  NO_ZW_NVM
  TEST_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/"
  AUTOSTART_ENABLE
  )

target_include_directories(test_contiki
  PUBLIC
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CONTIKI_INC}
  "${CMAKE_SOURCE_DIR}/libs2/transport_service"
  "${CMAKE_SOURCE_DIR}/libs2/include")

# gateway lib for the test
list (APPEND GW_SRC
#    ${CMAKE_SOURCE_DIR}/src/zwpgrmr/linux_usb_interface.c
    ${CMAKE_SOURCE_DIR}/src/ZWFirmwareUpdate.c
    ${CMAKE_SOURCE_DIR}/src/ZW_tcp_client.c
)

add_library(zipgateway-contiki-test ${GW_SRC})

add_definitions(-DZIP_ND6)

target_include_directories(zipgateway-contiki-test PUBLIC 
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

target_link_libraries(zipgateway-contiki-test PUBLIC
  ZWaveAnalyzer
  contiki
  ${S2_LIBRARIES}
  )

if(${DISABLE_DTLS})
    message("Gateway if configured without DTLS.")
    target_compile_definitions(zipgateway-contiki-test PUBLIC -DDEBUG_ALLOW_NONSECURE -DDISABLE_DTLS )
endif()

add_definitions( -DSUPPORTS_MDNS)

target_compile_definitions(zipgateway-contiki-test PUBLIC
  ZGW_LOG ZGW_LOG_LVL_INIT=5
  NO_ZW_NVM
  )
target_compile_options( zipgateway-contiki-test PUBLIC )

add_library(nvm-file-mock2 ${CMAKE_SOURCE_DIR}/src/zw_appl_nvm.c
  hello.c)
target_include_directories(nvm-file-mock2 PUBLIC 
  ${CMAKE_CURRENT_BINARY_DIR} 
  ${TS2_INCLUDE_DIRS}
  ${ZWAVE_INCLUDE_DIR}
  ${LibUSB_INCLUDE_DIRS}
  serialapi
  ${CMAKE_SOURCE_DIR}/src-gen
  zwpgrmr
  transport
  utls
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  )
target_compile_definitions(nvm-file-mock2 PUBLIC
  ZGW_LOG ZGW_LOG_LVL_INIT=5
  NO_ZW_NVM
  )

# end

target_link_libraries(test_contiki
  contiki-core
  nvm-file-mock2
  zipgateway-contiki-test
  )

FILE(GLOB ZSTATE_GCOV_OBJECTS
  ABSOLUTE "${CMAKE_CURRENT_BINARY_DIR}/*.a"
  "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/test_*.dir/__/src/*.o"
  "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/test_*.dir/*.o"
  )

add_custom_target(contiki_gcov
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND gcov -b -c ${ZSTATE_GCOV_OBJECTS}
)

SET(GCOV_OBJECTS ${GCOV_OBJECTS}  ${ZSTATE_GCOV_OBJECTS} PARENT_SCOPE)

add_test(contiki_test test_contiki)
