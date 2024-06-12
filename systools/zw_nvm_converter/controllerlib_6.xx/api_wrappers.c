/* © 2019 Silicon Laboratories Inc. */
#include <stdio.h>
#include <string.h>
#include "controllerlib_api.h"
#include "nvm500_export.h"
#include "nvm500_import.h"
#include "nvm500_common.h"

/*****************************************************************************/
/* Utility functions */


/*****************************************************************************/
static uint32_t nvmlib68b_init(void)
{
  set_tartget_protocol_by_id(BRIDGE_6_8);
  return 0;
}
static uint32_t nvmlib67b_init(void)
{
  set_tartget_protocol_by_id(BRIDGE_6_7);
  return 0;
}
static uint32_t nvmlib66b_init(void)
{
  set_tartget_protocol_by_id(BRIDGE_6_6);
  return 0;
}
static uint32_t nvmlib68s_init(void)
{
  set_tartget_protocol_by_id(STATIC_6_8);
  return 0;
}
static uint32_t nvmlib67s_init(void)
{
  set_tartget_protocol_by_id(STATIC_6_7);
  return 0;
}
static uint32_t nvmlib66s_init(void)
{
  set_tartget_protocol_by_id(STATIC_6_6);
  return 0;
}


/*****************************************************************************/
static void nvmlib6xx_term(void)
{
}

/*****************************************************************************/
static bool nvmlib6xx_is_json_valid(json_object *jo)
{
  return true;
}

/*****************************************************************************/
/* thes only symbols to be exported from this module */
nvmlib_interface_t controllerlib68b = {
  .lib_id        = "NVM Converter for Z-Wave Bridge 6.80",
  .nvm_desc      = "bridge6.8",
  .json_desc     = "JSON",
  .init          = nvmlib68b_init,
  .term          = nvmlib6xx_term,
  .is_nvm_valid  = nvmlib6xx_is_nvm_valid,
  .nvm_to_json   = nvmlib6xx_nvm_to_json,
  .is_json_valid = nvmlib6xx_is_json_valid,
  .json_to_nvm   = nvmlib6xx_json_to_nvm
};

nvmlib_interface_t controllerlib67b = {
  .lib_id        = "NVM Converter for Z-Wave Bridge 6.70",
  .nvm_desc      = "bridge6.7",
  .json_desc     = "JSON",
  .init          = nvmlib67b_init,
  .term          = nvmlib6xx_term,
  .is_nvm_valid  = nvmlib6xx_is_nvm_valid,
  .nvm_to_json   = nvmlib6xx_nvm_to_json,
  .is_json_valid = nvmlib6xx_is_json_valid,
  .json_to_nvm   = nvmlib6xx_json_to_nvm
};

nvmlib_interface_t controllerlib66b = {
  .lib_id        = "NVM Converter for Z-Wave Bridge 6.60",
  .nvm_desc      = "bridge6.6",
  .json_desc     = "JSON",
  .init          = nvmlib66b_init,
  .term          = nvmlib6xx_term,
  .is_nvm_valid  = nvmlib6xx_is_nvm_valid,
  .nvm_to_json   = nvmlib6xx_nvm_to_json,
  .is_json_valid = nvmlib6xx_is_json_valid,
  .json_to_nvm   = nvmlib6xx_json_to_nvm
};

nvmlib_interface_t controllerlib68s = {
  .lib_id        = "NVM Converter for Z-Wave Static 6.80",
  .nvm_desc      = "static6.8",
  .json_desc     = "JSON",
  .init          = nvmlib68s_init,
  .term          = nvmlib6xx_term,
  .is_nvm_valid  = nvmlib6xx_is_nvm_valid,
  .nvm_to_json   = nvmlib6xx_nvm_to_json,
  .is_json_valid = nvmlib6xx_is_json_valid,
  .json_to_nvm   = nvmlib6xx_json_to_nvm
};

nvmlib_interface_t controllerlib67s = {
  .lib_id        = "NVM Converter for Z-Wave Static 6.70",
  .nvm_desc      = "static6.7",
  .json_desc     = "JSON",
  .init          = nvmlib67s_init,
  .term          = nvmlib6xx_term,
  .is_nvm_valid  = nvmlib6xx_is_nvm_valid,
  .nvm_to_json   = nvmlib6xx_nvm_to_json,
  .is_json_valid = nvmlib6xx_is_json_valid,
  .json_to_nvm   = nvmlib6xx_json_to_nvm
};

nvmlib_interface_t controllerlib66s = {
  .lib_id        = "NVM Converter for Z-Wave Static 6.60",
  .nvm_desc      = "static6.6",
  .json_desc     = "JSON",
  .init          = nvmlib66s_init,
  .term          = nvmlib6xx_term,
  .is_nvm_valid  = nvmlib6xx_is_nvm_valid,
  .nvm_to_json   = nvmlib6xx_nvm_to_json,
  .is_json_valid = nvmlib6xx_is_json_valid,
  .json_to_nvm   = nvmlib6xx_json_to_nvm
};
