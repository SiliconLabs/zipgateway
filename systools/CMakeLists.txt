
add_subdirectory( libs )
add_subdirectory( zgw_restore )
add_subdirectory( zw_nvm_converter )
add_subdirectory( zw_programmer )
add_subdirectory( zgw_eeprom_to_sqlite )
add_subdirectory( zgw_eeprom_2_25_to_2_61 )

add_subdirectory( doc )

if( ${CMAKE_SYSTEM_NAME} MATCHES "Linux" )
    add_subdirectory( udprelay )
endif()
