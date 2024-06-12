# NB: The nvm3 folder structure has been maintained when copied from
#     the zw-protocol repository to make it easy to do a folder compare
#     to sync up with changes

set(EMDRV_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/platform/emdrv")
set(NVM3DRV_ROOT "${EMDRV_ROOT}/nvm3")

set(NVM3LIB_SRC_FILES
  "${NVM3DRV_ROOT}/src/nvm3.c"
  "${NVM3DRV_ROOT}/src/nvm3_cache.c"
  "${NVM3DRV_ROOT}/src/nvm3_hal_ram.c"
  "${NVM3DRV_ROOT}/src/nvm3_lock.c"
  "${NVM3DRV_ROOT}/src/nvm3_object.c"
  "${NVM3DRV_ROOT}/src/nvm3_page.c"
  "${NVM3DRV_ROOT}/src/nvm3_utils.c"
)

add_library(nvm3lib ${NVM3LIB_SRC_FILES})

target_include_directories(nvm3lib PUBLIC
                              "${EMDRV_ROOT}/common/inc"
                              "${NVM3DRV_ROOT}/inc"
                              "${NVM3DRV_ROOT}/config"
                              ../../libs/json_helpers
                          )

target_compile_definitions(nvm3lib PUBLIC NVM3_HOST_BUILD FLASH_PAGE_SIZE=2048 NVM3_DEFAULT_CACHE_SIZE=400)
