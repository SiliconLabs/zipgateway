
add_executable(test_restore_eeprom test_restore_eeprom.c test_restore_eeprom_helper.c
  ${COMMON_TEST_SRC}
)

target_link_libraries(test_restore_eeprom zgw_restore-lib)
add_test(restore_eeprom test_restore_eeprom)

