

add_library(controllerlib_6xx     
    nvm500_export.c
    nvm500_import.c
    nvm500_common.c
    api_wrappers.c
)

target_link_libraries(controllerlib_6xx controllerlib_api json_helpers ${JSON_C_LDFLAGS})

target_include_directories(controllerlib_6xx
                            PUBLIC
                              ${JSON_C_INCLUDE_DIRS}
                            PRIVATE
                              ${CMAKE_CURRENT_SOURCE_DIR} ${ZWAVE_INCLUDE_DIR}
                              )


#target_include_directories( nvm500_tool PUBLIC ${JSON_C_INCLUDE_DIRS}  ${ZWAVE_INCLUDE_DIR}) 
