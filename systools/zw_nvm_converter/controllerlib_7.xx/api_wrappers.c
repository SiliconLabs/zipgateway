/* © 2019 Silicon Laboratories Inc. */
#include <stdio.h>
#include <string.h>
#include "controllerlib_api.h"
#include "controller_nvm.h"


/*****************************************************************************/
/* Utility functions */


/*****************************************************************************/
static uint32_t nvmlib711_init(void)
{
  return 0;
}

/*****************************************************************************/
static void nvmlib711_term(void)
{
}

/*****************************************************************************/
// static bool nvmlib711_load_nvm(const char *bin_file_name)
// {
//   if (0 != nvm3_storage_address)
//   {
//     Ecode_t ec = nvm3_halLoadBin(bin_file_name);
//     if (ECODE_NVM3_OK == ec)
//     {
//       return true;
//     }
//   }
//   return false;
// }

/*****************************************************************************/
// static bool nvmlib711_save_nvm(const char *bin_file_name)
// {
//   if (0 != nvm3_storage_address)
//   {
//     Ecode_t ec = nvm3_halSaveBin(bin_file_name);
//     if (ECODE_NVM3_OK == ec)
//     {
//       return true;
//     }
//   }
//   return false;
// }

// /*****************************************************************************/
// static uint32_t nvmlib711_load_json(const char *json_file_name)
// {
//   // json_object* jo = json_object_new_object();
//   return 0;
// }

/*****************************************************************************/
static bool nvmlib711_nvm_to_json(const uint8_t *nvm_image, size_t nvm_image_size, json_object **jo_out)
{
  if (open_controller_nvm(nvm_image, nvm_image_size, NVM3_700s))
  {
    dump_controller_nvm_keys();

    *jo_out = controller_nvm711_get_json();

    close_controller_nvm();

    return true;
  }
  return false;
}

/*****************************************************************************/
static bool nvmlib715_nvm_to_json(const uint8_t *nvm_image, size_t nvm_image_size, json_object **jo_out, nvmLayout_t nvm_layout)
{
  if (open_controller_nvm(nvm_image, nvm_image_size, nvm_layout))
  {
    dump_controller_nvm_keys();

    *jo_out = controller_nvm715_get_json();

    close_controller_nvm();

    return true;
  }
  return false;
}

/*****************************************************************************/
static bool nvmlib700s_715_nvm_to_json(const uint8_t *nvm_image, size_t nvm_image_size, json_object **jo_out)
{
  return nvmlib715_nvm_to_json(nvm_image, nvm_image_size, jo_out, NVM3_700s);
}

/*****************************************************************************/
static bool nvmlib800s_715_nvm_to_json(const uint8_t *nvm_image, size_t nvm_image_size, json_object **jo_out)
{
  return nvmlib715_nvm_to_json(nvm_image, nvm_image_size, jo_out, NVM3_800s);
}

/*****************************************************************************/
static bool nvmlib700s_is_nvm_valid(const uint8_t *nvm_image, size_t nvm_image_size)
{
  return check_controller_nvm(nvm_image, nvm_image_size, NVM3_700s);
}

/*****************************************************************************/
static bool nvmlib800s_is_nvm_valid(const uint8_t *nvm_image, size_t nvm_image_size)
{
  return check_controller_nvm(nvm_image, nvm_image_size, NVM3_800s);
}

/*****************************************************************************/
static bool nvmlib711_is_json_valid(json_object *jo)
{
  return true;
}

/*****************************************************************************/
static size_t nvmlib_json_to_nvm_common(json_object *jo, uint8_t **nvm_buf_ptr, size_t *nvm_size, nvmLayout_t nvm_layout, int target_version)
{
  size_t bin_size = 0;
  uint8_t *buf_ptr = 0;

  /* TODO Implement this */
  if (true)
  {
    open_controller_nvm(NULL, 0, nvm_layout);

    bool json_parsed = controller_parse_json(jo,target_version);
    if (true == json_parsed)
    {
      bin_size = get_controller_nvm_image(&buf_ptr);
    }

    close_controller_nvm();
  }

  if (bin_size > 0)
  {
    *nvm_size    = bin_size;
    *nvm_buf_ptr = buf_ptr;
  }

  return bin_size;
}

static size_t nvmlib711_json_to_nvm(json_object *jo, uint8_t **nvm_buf_ptr, size_t *nvm_size) {
  return nvmlib_json_to_nvm_common(jo, nvm_buf_ptr, nvm_size, NVM3_700s, 711);
}

static size_t nvmlib712_json_to_nvm(json_object *jo, uint8_t **nvm_buf_ptr, size_t *nvm_size)
{
  return nvmlib_json_to_nvm_common(jo, nvm_buf_ptr, nvm_size, NVM3_700s, 712);
}

static size_t nvmlib715_json_to_nvm(json_object *jo, uint8_t **nvm_buf_ptr, size_t *nvm_size)
{
  return nvmlib_json_to_nvm_common(jo, nvm_buf_ptr, nvm_size, NVM3_700s, 715);
}

static size_t nvmlib716_json_to_nvm(json_object *jo, uint8_t **nvm_buf_ptr, size_t *nvm_size)
{
  return nvmlib_json_to_nvm_common(jo, nvm_buf_ptr, nvm_size, NVM3_700s ,716);
}

static size_t nvmlib700s_717_json_to_nvm(json_object *jo, uint8_t **nvm_buf_ptr, size_t *nvm_size)
{
  return nvmlib_json_to_nvm_common(jo, nvm_buf_ptr, nvm_size, NVM3_700s, 717);
}

static size_t nvmlib800s_717_json_to_nvm(json_object *jo, uint8_t **nvm_buf_ptr, size_t *nvm_size)
{
  return nvmlib_json_to_nvm_common(jo, nvm_buf_ptr, nvm_size, NVM3_800s, 717);
}

static size_t nvmlib700s_718_json_to_nvm(json_object *jo, uint8_t **nvm_buf_ptr, size_t *nvm_size)
{
  return nvmlib_json_to_nvm_common(jo, nvm_buf_ptr, nvm_size, NVM3_700s, 718);
}

static size_t nvmlib800s_718_json_to_nvm(json_object *jo, uint8_t **nvm_buf_ptr, size_t *nvm_size)
{
  return nvmlib_json_to_nvm_common(jo, nvm_buf_ptr, nvm_size, NVM3_800s, 718);
}
/*****************************************************************************/
nvmlib_interface_t controllerlib711 = {
  .lib_id        = "NVM Converter for Z-Wave Bridge 7.11",
  .nvm_desc      = "bridge7.11",
  .json_desc     = "JSON",
  .init          = nvmlib711_init,
  .term          = nvmlib711_term,
  .is_nvm_valid  = nvmlib700s_is_nvm_valid,
  .nvm_to_json   = nvmlib711_nvm_to_json,
  .is_json_valid = nvmlib711_is_json_valid,
  .json_to_nvm   = nvmlib711_json_to_nvm
};

nvmlib_interface_t controllerlib712 = {
  .lib_id        = "NVM Converter for Z-Wave Bridge 7.12",
  .nvm_desc      = "bridge7.12",
  .json_desc     = "JSON",
  .init          = nvmlib711_init,
  .term          = nvmlib711_term,
  .is_nvm_valid  = nvmlib700s_is_nvm_valid,
  .nvm_to_json   = nvmlib711_nvm_to_json,
  .is_json_valid = nvmlib711_is_json_valid,
  .json_to_nvm   = nvmlib712_json_to_nvm
};

nvmlib_interface_t controllerlib715 = {
  .lib_id        = "NVM Converter for Z-Wave Bridge 7.15",
  .nvm_desc      = "bridge7.15",
  .json_desc     = "JSON",
  .init          = nvmlib711_init,
  .term          = nvmlib711_term,
  .is_nvm_valid  = nvmlib700s_is_nvm_valid,
  .nvm_to_json   = nvmlib700s_715_nvm_to_json,
  .is_json_valid = nvmlib711_is_json_valid,
  .json_to_nvm   = nvmlib715_json_to_nvm
};

nvmlib_interface_t controllerlib716 = {
  .lib_id        = "NVM Converter for Z-Wave Bridge 7.16",
  .nvm_desc      = "bridge7.16",
  .json_desc     = "JSON",
  .init          = nvmlib711_init,
  .term          = nvmlib711_term,
  .is_nvm_valid  = nvmlib700s_is_nvm_valid,
  .nvm_to_json   = nvmlib700s_715_nvm_to_json,
  .is_json_valid = nvmlib711_is_json_valid,
  .json_to_nvm   = nvmlib716_json_to_nvm
};

nvmlib_interface_t controllerlib_700s_717 = {
  .lib_id        = "NVM Converter for Z-Wave Bridge 7.17",
  .nvm_desc      = "bridge_700s_7.17",
  .json_desc     = "JSON",
  .init          = nvmlib711_init,
  .term          = nvmlib711_term,
  .is_nvm_valid  = nvmlib700s_is_nvm_valid,
  .nvm_to_json   = nvmlib700s_715_nvm_to_json,
  .is_json_valid = nvmlib711_is_json_valid,
  .json_to_nvm   = nvmlib700s_717_json_to_nvm
};

nvmlib_interface_t controllerlib_800s_717 = {
  .lib_id        = "NVM Converter for Z-Wave Bridge 7.17",
  .nvm_desc      = "bridge_800s_7.17",
  .json_desc     = "JSON",
  .init          = nvmlib711_init,
  .term          = nvmlib711_term,
  .is_nvm_valid  = nvmlib800s_is_nvm_valid,
  .nvm_to_json   = nvmlib800s_715_nvm_to_json,
  .is_json_valid = nvmlib711_is_json_valid,
  .json_to_nvm   = nvmlib800s_717_json_to_nvm
};

nvmlib_interface_t controllerlib_700s_718 = {
  .lib_id        = "NVM Converter for Z-Wave Bridge 7.18",
  .nvm_desc      = "bridge_700s_7.18",
  .json_desc     = "JSON",
  .init          = nvmlib711_init,
  .term          = nvmlib711_term,
  .is_nvm_valid  = nvmlib700s_is_nvm_valid,
  .nvm_to_json   = nvmlib700s_715_nvm_to_json,
  .is_json_valid = nvmlib711_is_json_valid,
  .json_to_nvm   = nvmlib700s_718_json_to_nvm
};

nvmlib_interface_t controllerlib_800s_718 = {
  .lib_id        = "NVM Converter for Z-Wave Bridge 7.18",
  .nvm_desc      = "bridge_800s_7.18",
  .json_desc     = "JSON",
  .init          = nvmlib711_init,
  .term          = nvmlib711_term,
  .is_nvm_valid  = nvmlib800s_is_nvm_valid,
  .nvm_to_json   = nvmlib800s_715_nvm_to_json,
  .is_json_valid = nvmlib711_is_json_valid,
  .json_to_nvm   = nvmlib800s_718_json_to_nvm
};
