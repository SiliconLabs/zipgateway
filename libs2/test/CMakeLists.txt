
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
endif()

add_definitions( -DRANDLEN=64 )
add_unity_test(NAME test_ctr_dbrg FILES test_ctr_dbrg.c ../crypto/ctr_drbg/ctr_drbg.c ../crypto/aes/aes.c)

add_unity_test(NAME test_kderiv FILES test_kderiv.c ../crypto/kderiv/kderiv.c ../crypto/aes-cmac/aes_cmac.c ../crypto/aes/aes.c)

# Disabling unit test for now. Not sure if it works on C51.
#add_definitions ( -DNO_MEM_FUNCTIONS )
#add_definitions ( -DLINUX_TEST )
#add_executable( test_key_scramble test_key_scramble.c ../crypto/key_scramble/ZW_s2_key_scrambler.c ../crypto/aes/aes.c)
#target_link_libraries(test_key_scramble) 
