

add_executable(zgw_eeprom_to_sqlite zgw_eeprom_to_sqlite.c
  RD_DataStore_Eeprom.c
  RD_DataStore_Eeprom20.c
  zgw_smalloc_utils.cpp
  ${CMAKE_SOURCE_DIR}/test/zipgateway_main_stubs.c
  ${CMAKE_SOURCE_DIR}/test/test_helpers.c
  ${CMAKE_SOURCE_DIR}/contiki/platform/linux/zgw_log_int.c
  ${CMAKE_SOURCE_DIR}/contiki/platform/linux/File_eeprom.c
  )

target_link_libraries(zgw_eeprom_to_sqlite zipgateway-lib)
target_include_directories(zgw_eeprom_to_sqlite PRIVATE ${CMAKE_SOURCE_DIR}/test)
install(TARGETS zgw_eeprom_to_sqlite DESTINATION bin)
