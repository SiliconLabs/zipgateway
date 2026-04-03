
add_definitions( -DUNIT_TEST )

#include_directories( ../crypto/aes )
#add_executable( test_ccm test_ccm.c ../crypto/ccm/ccm.c ../crypto/aes/aes.c )
#add_test( test_ccm test_ccm )

# Add test for protocol
add_unity_test(NAME test_protocol FILES test_protocol.c LIBRARIES s2_controller s2crypto aes)

# Add test for AES-CMAC
add_unity_test(NAME test_aes_cmac FILES test_aes_cmac.c LIBRARIES s2crypto aes)

# Add test for Curve25519
include_directories(.)
add_unity_test(NAME test_curve25519 FILES wc_util.c test_curve25519.c LIBRARIES s2crypto aes)

# Add test for CCM
add_unity_test(NAME test_ccm FILES test_ccm.c ../crypto/ccm/ccm.c ../crypto/aes/aes.c)

if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "C51")
 	add_definitions( -DNEW_TEST_T2 )
 	add_executable(new_test_t2
          new_test_t2.c
          clock_time.c
          ${CMAKE_CURRENT_SOURCE_DIR}/../transport_service/transport_service2.c
          ${CMAKE_CURRENT_SOURCE_DIR}/../transport_service/transport2_fsm.c)
 	include_directories(
          ${CMAKE_CURRENT_SOURCE_DIR}/../transport_service/
          ${CMAKE_CURRENT_SOURCE_DIR}/../include/ )
  if (${CMAKE_PROJECT_NAME} MATCHES "zipgateway")
    target_link_libraries(new_test_t2 zipgateway-lib)
  endif()
	add_test(test_transport_service2 new_test_t2)

  set(CMOCK_RB "" CACHE FILEPATH "Path to CMock lib/cmock.rb used for local mock regeneration")
  set(CMOCK_VERSION "2.4.6" CACHE STRING "CMock version used for regeneration")
  set(CMOCK_ARCHIVE "${CMAKE_BINARY_DIR}/tools/cmock-v${CMOCK_VERSION}.tar.gz")
  set(CMOCK_ARCHIVE_SHA256 "7b4b0a1ca9ecd4d461e3a442b4b2c98c37e451d8966e636483142fdf49667ed7"
      CACHE STRING "Expected SHA256 for CMock archive")
  set(CMOCK_ROOT "${CMAKE_BINARY_DIR}/tools/CMock-${CMOCK_VERSION}")
  set(CMOCK_RB_DEFAULT "${CMOCK_ROOT}/lib/cmock.rb")
  if(CMOCK_RB STREQUAL "")
    if(NOT EXISTS "${CMOCK_RB_DEFAULT}")
      file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tools")
      if(NOT EXISTS "${CMOCK_ARCHIVE}")
        message(STATUS "Downloading CMock v${CMOCK_VERSION} for mock regeneration")
        file(DOWNLOAD
                "https://github.com/ThrowTheSwitch/CMock/archive/refs/tags/v${CMOCK_VERSION}.tar.gz"
                "${CMOCK_ARCHIVE}"
                SHOW_PROGRESS
                STATUS CMOCK_DOWNLOAD_STATUS
                TLS_VERIFY ON)
        list(GET CMOCK_DOWNLOAD_STATUS 0 CMOCK_DOWNLOAD_CODE)
        list(GET CMOCK_DOWNLOAD_STATUS 1 CMOCK_DOWNLOAD_MESSAGE)
        if(NOT CMOCK_DOWNLOAD_CODE EQUAL 0)
          message(FATAL_ERROR "Failed to download CMock archive: ${CMOCK_DOWNLOAD_MESSAGE}")
        endif()
      endif()

      file(SHA256 "${CMOCK_ARCHIVE}" CMOCK_ARCHIVE_SHA256_ACTUAL)
      if(NOT CMOCK_ARCHIVE_SHA256_ACTUAL STREQUAL CMOCK_ARCHIVE_SHA256)
        message(FATAL_ERROR
                "CMock archive SHA256 mismatch. Expected ${CMOCK_ARCHIVE_SHA256}, got ${CMOCK_ARCHIVE_SHA256_ACTUAL}")
      endif()

      execute_process(
              COMMAND ${CMAKE_COMMAND} -E tar xzf "${CMOCK_ARCHIVE}"
              WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
              RESULT_VARIABLE CMOCK_EXTRACT_RESULT)
      if(NOT CMOCK_EXTRACT_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to extract ${CMOCK_ARCHIVE}")
      endif()

      file(MAKE_DIRECTORY "${CMOCK_ROOT}/vendor/unity/auto")
      file(COPY "${CMAKE_SOURCE_DIR}/libs2/TestFramework/unity/auto/type_sanitizer.rb"
              DESTINATION "${CMOCK_ROOT}/vendor/unity/auto")
    endif()
    set(CMOCK_RB "${CMOCK_RB_DEFAULT}" CACHE FILEPATH "Path to CMock lib/cmock.rb used by regen_mock_zw_transport_api" FORCE)
  endif()

  set(CMOCK_RUNTIME_C "${CMOCK_ROOT}/src/cmock.c"
      CACHE FILEPATH "Path to CMock runtime cmock.c used by unit tests")
  if(NOT EXISTS "${CMOCK_RUNTIME_C}")
    message(FATAL_ERROR "CMock runtime source not found: ${CMOCK_RUNTIME_C}")
  endif()

  set(CMOCK_GEN_DIR "${CMAKE_BINARY_DIR}/cmock_gen")
  set(CMOCK_INPUT_DIR "${CMAKE_BINARY_DIR}/cmock-input")
  set(CMOCK_INPUT_HEADER "${CMOCK_INPUT_DIR}/ZW_transport_api.h")
  set(MOCK_ZW_TRANSPORT_API_C "${CMOCK_GEN_DIR}/MockZW_transport_api.c")
  set(MOCK_ZW_TRANSPORT_API_H "${CMOCK_GEN_DIR}/MockZW_transport_api.h")

  add_custom_command(
          OUTPUT "${CMOCK_INPUT_HEADER}"
          COMMAND ${CMAKE_COMMAND} -E make_directory "${CMOCK_INPUT_DIR}"
          COMMAND "${CMAKE_C_COMPILER}" -E -P
                  -DZW_CONTROLLER_BRIDGE
                  -I"${CMAKE_SOURCE_DIR}/Z-Wave/include"
                  "${CMAKE_SOURCE_DIR}/Z-Wave/include/ZW_transport_api.h"
                  -o "${CMOCK_INPUT_HEADER}"
          DEPENDS "${CMAKE_SOURCE_DIR}/Z-Wave/include/ZW_transport_api.h"
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
          COMMENT "Preprocessing ZW_transport_api.h for CMock generation")

  add_custom_command(
          OUTPUT "${MOCK_ZW_TRANSPORT_API_C}" "${MOCK_ZW_TRANSPORT_API_H}"
          COMMAND ${CMAKE_COMMAND} -E make_directory "${CMOCK_GEN_DIR}"
          COMMAND ${CMAKE_COMMAND} -E env ruby "${CMOCK_RB}"
                  -o"${CMAKE_CURRENT_SOURCE_DIR}/cmock/cmock_config.yml"
                  "${CMOCK_INPUT_HEADER}"
          DEPENDS "${CMOCK_INPUT_HEADER}" "${CMAKE_CURRENT_SOURCE_DIR}/cmock/cmock_config.yml"
          WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
          COMMENT "Generating MockZW_transport_api in build directory")
  set_source_files_properties("${MOCK_ZW_TRANSPORT_API_C}" "${MOCK_ZW_TRANSPORT_API_H}" PROPERTIES GENERATED TRUE)

  add_custom_target(regen_mock_zw_transport_api
          DEPENDS "${MOCK_ZW_TRANSPORT_API_C}" "${MOCK_ZW_TRANSPORT_API_H}")

  add_unity_test(NAME test_t2_zipgw_runtime_nodeid FILES
          test_t2_zipgw_runtime_nodeid.c
          "${CMOCK_RUNTIME_C}"
          "${MOCK_ZW_TRANSPORT_API_C}"
          ${CMAKE_CURRENT_SOURCE_DIR}/../transport_service/transport_service2.c
          ${CMAKE_CURRENT_SOURCE_DIR}/../transport_service/transport2_fsm.c)
  add_dependencies(test_t2_zipgw_runtime_nodeid regen_mock_zw_transport_api)
  target_compile_definitions(test_t2_zipgw_runtime_nodeid PRIVATE ZIPGW)

  # Explicitly undefine NEW_TEST_T2 here so this regression test follows the
  # production TS code path.
  target_compile_options(test_t2_zipgw_runtime_nodeid PRIVATE -U NEW_TEST_T2)
  # Generated CMock code may reference legacy Unity detail macros that are
  # not available in the Unity variant used by this tree.
  target_compile_options(test_t2_zipgw_runtime_nodeid PRIVATE
          -include "${CMAKE_CURRENT_SOURCE_DIR}/cmock/cmock_legacy_unity_compat.h")
  target_include_directories(test_t2_zipgw_runtime_nodeid PRIVATE
          ${CMAKE_BINARY_DIR}
          "${CMOCK_GEN_DIR}"
          "${CMOCK_ROOT}/src"
          ${CMAKE_CURRENT_SOURCE_DIR}/../transport_service/
          ${CMAKE_CURRENT_SOURCE_DIR}/../include/
          ${CMAKE_SOURCE_DIR}/src
          ${CMAKE_SOURCE_DIR}/src/transport
          ${CMAKE_SOURCE_DIR}/src/utls
          ${CMAKE_SOURCE_DIR}/contiki/core
          ${CMAKE_SOURCE_DIR}/contiki/core/sys
          ${CMAKE_SOURCE_DIR}/contiki/platform/linux
          ${CMAKE_SOURCE_DIR}/Z-Wave/include)
endif()

# ZW_SendDataAppl.c is compiled directly into the test (not taken from the
# pre-built zipgateway-lib) so that the -DUNIT_TEST flag (set globally by
# add_definitions at the top of this file) is in effect.  This is required
# to compile the UNIT_TEST seam functions that expose private state.
#
# The remaining zipgateway-lib symbols that ZW_SendDataAppl.c needs are
# provided either by linking zipgateway-lib or by --wrap stubs in the test.
# ---------------------------------------------------------------------------
if (${CMAKE_PROJECT_NAME} MATCHES "zipgateway")
  add_unity_test(NAME test_send_data_callback
          FILES test_send_data_callback.c
                ${CMAKE_SOURCE_DIR}/src/transport/ZW_SendDataAppl.c
                ${CMAKE_SOURCE_DIR}/test/zipgateway_main_stubs.c
          LIBRARIES zipgateway-lib)

  set_target_properties(test_send_data_callback PROPERTIES LINK_FLAGS
    "-Wl,-wrap=process_post \
     -Wl,-wrap=process_exit \
     -Wl,-wrap=process_start \
     -Wl,-wrap=get_queue_state \
     -Wl,-wrap=zw_frame_buffer_free \
     -Wl,-wrap=etimer_stop \
     -Wl,-wrap=etimer_set \
     -Wl,-wrap=etimer_expired \
     -Wl,-wrap=sec0_abort_all_tx_sessions \
     -Wl,-wrap=ima_send_data_done")
endif()

add_definitions( -DRANDLEN=64 )
add_unity_test(NAME test_ctr_dbrg FILES test_ctr_dbrg.c ../crypto/ctr_drbg/ctr_drbg.c ../crypto/aes/aes.c)

add_unity_test(NAME test_kderiv FILES test_kderiv.c ../crypto/kderiv/kderiv.c ../crypto/aes-cmac/aes_cmac.c ../crypto/aes/aes.c)

# Disabling unit test for now. Not sure if it works on C51.
#add_definitions ( -DNO_MEM_FUNCTIONS )
#add_definitions ( -DLINUX_TEST )
#add_executable( test_key_scramble test_key_scramble.c ../crypto/key_scramble/ZW_s2_key_scrambler.c ../crypto/aes/aes.c)
#target_link_libraries(test_key_scramble) 
