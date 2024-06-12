add_definitions( -D_GNU_SOURCE )
set(ZIPGW_SRC
  ${CMAKE_SOURCE_DIR}/src
  )
set(SMALLOC_SRC
  ${ZIPGW_SRC}/smalloc.c
  )
include_directories(
  ${ZIPGW_SRC}
  )
add_executable( zgw_eeprom_2_25_to_2_61 zgw_eeprom_2_25_to_2_61.c ${SMALLOC_SRC})
install(TARGETS zgw_eeprom_2_25_to_2_61 RUNTIME DESTINATION bin )