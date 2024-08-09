# flex does not like unused and sign-compare
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused -Wno-sign-compare")

set(PVL_SRC
  ${CMAKE_SOURCE_DIR}/src/pvs_parse_help.c
  ${CMAKE_SOURCE_DIR}/src/provisioning_list.c
  ${CMAKE_SOURCE_DIR}/src/zwdb.c
  ${CMAKE_SOURCE_DIR}/src/utls/zgw_str.c
  ${CMAKE_SOURCE_DIR}/contiki/core/lib/assert.c
  ${CMAKE_SOURCE_DIR}/contiki/core/lib/list.c
  ${CMAKE_SOURCE_DIR}/test/mock_rd.c
  )

set(TEST_HELPERS
  ${CMAKE_SOURCE_DIR}/test/test_helpers.c
  ${CMAKE_SOURCE_DIR}/test/test_utils.c
  ${CMAKE_SOURCE_DIR}/test/test_gw_helpers.c
)

#add_definitions(-DUIP_CONF_IPV6=1)

# project-conf.h will set the UIP_CONF_BYTE_ORDER properly, so that the platform's endianness is configured correctly. 
# If contiki is not included, the project-conf.h needs to be set.
add_definitions(-DUIP_CONF_IPV6=1 -DAUTOSTART_ENABLE -DUIP_CONF_ROUTER=1 -DZIPGW_UNITTEST -DPROJECT_CONF_H=\"project-conf.h\")

include_directories(
  ${CMAKE_CURRENT_BINARY_DIR}/..  
  ..
  ${CMAKE_SOURCE_DIR}/systools/zgw_eeprom_to_sqlite
  ${CMAKE_SOURCE_DIR}/src/
  ${CMAKE_SOURCE_DIR}/src/serialapi
  ${CMAKE_SOURCE_DIR}/src/transport
  ${CMAKE_SOURCE_DIR}/src/utls
  ${CMAKE_SOURCE_DIR}/contiki/platform/linux/
  ${CMAKE_SOURCE_DIR}/contiki/core
  ${CMAKE_SOURCE_DIR}/contiki/core/sys
  ${CMAKE_SOURCE_DIR}/contiki/core/lib # list.h
  ${CMAKE_SOURCE_DIR}/contiki/core/net # uip.h
  ${CMAKE_SOURCE_DIR}/contiki/cpu/native/
  ${CMAKE_SOURCE_DIR}/Z-Wave/include
  ${CMAKE_SOURCE_DIR}/test
)

add_subdirectory(contiki)

add_subdirectory(pvs)

add_subdirectory(CC_provisioning_list)

add_subdirectory(rd)

add_subdirectory(ZIP_router)

add_subdirectory( zwlog )

add_subdirectory(CC_Indicator)

add_subdirectory(CC_Time)

add_subdirectory(multicast_group_manager)

add_subdirectory(multicast_tlv)

add_subdirectory(test_multicast_auto)

add_subdirectory(CC_fw_upd)

add_executable(eeprom-printer ${CMAKE_SOURCE_DIR}/test/eeprom-printer.c)

add_subdirectory(S0)

add_subdirectory(utls)

add_subdirectory(DataStore)

add_subdirectory( mDNS )

add_subdirectory(command_class_validator)

add_subdirectory(temp_associations)

add_subdirectory(zgw_state)
add_subdirectory(serialapi)
add_subdirectory(print_frame)

add_custom_target(src_gcov
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND gcov -b -c ${GCOV_OBJECTS}
  )
