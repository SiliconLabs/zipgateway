set(CONTROLLERLIB_7xx_SRC_FILES
  api_wrappers.c
  controller_nvm.c
  nvm3_helpers.c
  zwave/ZW_nodemask.c
)

add_library(controllerlib_7xx ${CONTROLLERLIB_7xx_SRC_FILES})

target_link_libraries(controllerlib_7xx controllerlib_api json_helpers app_utils nvm3lib PkgConfig::JSON_C)

target_include_directories(controllerlib_7xx
                            PUBLIC
                              ${JSON_C_INCLUDE_DIRS}
                            PRIVATE
                              ${CMAKE_CURRENT_SOURCE_DIR}
                              ${CMAKE_CURRENT_SOURCE_DIR}/zwave
                          )
