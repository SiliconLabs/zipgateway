/* © 2019 Silicon Laboratories Inc. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "endian_wrap.h"
#include "controller_nvm.h"
#include "ZW_nodemask_api.h"
#include "json_helpers.h"
#include "nvm3_helpers.h"
#include "nvm3_hal_ram.h"
#include "user_message.h"
#include "base64.h"

#include "zwave_controller_network_info_storage.h"
#include "zwave_controller_application_info_storage.h"

#define UNUSED(x) (void)(x)
#define sizeof_array(ARRAY) ((sizeof ARRAY) / (sizeof ARRAY[0]))

#define APP_CACHE_SIZE          250
#define PROTOCOL_CACHE_SIZE     250

#define FLASH_PAGE_SIZE_700s    (2  * 1024)
#define APP_NVM_SIZE_700s       (12 * 1024)
#define PROTOCOL_NVM_SIZE_700s  (36 * 1024)

#define FLASH_PAGE_SIZE_800s    (8  * 1024)
#define APP_NVM_SIZE_800s       (24 * 1024)
#define PROTOCOL_NVM_SIZE_800s  (40 * 1024)


/* This array is not page aligned. Since the nvm3 lib requires
   its memory to be page aligned we allocate one extra page here
   in order to make a page aligned pointer into this area */
#define NVM3_STORAGE_SIZE  (72 * 1024)
static uint8_t nvm3_storage[NVM3_STORAGE_SIZE];

static struct protocol_version {
  uint8_t format;
  uint8_t major;
  uint8_t minor;
  uint8_t patch;
} target_protocol_version;

static nvm3_file_descriptor_t* nvm3_current_protocol_files;
static size_t nvm3_current_protocol_files_size;

// Page aligned storage address
static nvm3_HalPtr_t       nvm3_storage_address;

static const nvm3_HalHandle_t *halHandle;
static const nvm3_HalConfig_t *halConfig;

static nvm3_Handle_t       nvm3_app_handle;
static nvm3_CacheEntry_t   nvm3_app_cache[APP_CACHE_SIZE];

static nvm3_Handle_t       nvm3_protocol_handle;
static nvm3_CacheEntry_t   nvm3_protocol_cache[PROTOCOL_CACHE_SIZE];

static nvm3_file_descriptor_t nvm3_protocol_files_v0[] = {
  { .key = FILE_ID_ZW_VERSION, .size = FILE_SIZE_ZW_VERSION, .name = "ZW_VERSION"},
  { .key = FILE_ID_NODEINFO, .size = FILE_SIZE_NODEINFO, .name = "NODEINFO",.optional=true, .num_keys = ZW_CLASSIC_MAX_NODES},
  { .key = FILE_ID_NODEROUTECAHE, .size = FILE_SIZE_NODEROUTECAHE, .name = "NODEROUTECAHE",.optional=true, .num_keys = ZW_CLASSIC_MAX_NODES},
  { .key = FILE_ID_PREFERREDREPEATERS, .size = FILE_SIZE_PREFERREDREPEATERS, .name = "PREFERREDREPEATERS", .optional = true},
  { .key = FILE_ID_SUCNODELIST, .size = FILE_SIZE_SUCNODELIST, .name = "SUCNODELIST"},
  { .key = FILE_ID_CONTROLLERINFO, .size = FILE_SIZE_CONTROLLERINFO_NVM711, .name = "CONTROLLERINFO"},
  { .key = FILE_ID_NODE_STORAGE_EXIST, .size = FILE_SIZE_NODE_STORAGE_EXIST, .name = "NODE_STORAGE_EXIST"},
  { .key = FILE_ID_NODE_ROUTECACHE_EXIST, .size = FILE_SIZE_NODE_ROUTECACHE_EXIST, .name = "NODE_ROUTECACHE_EXIST"},
  { .key = FILE_ID_APP_ROUTE_LOCK_FLAG, .size = FILE_SIZE_APP_ROUTE_LOCK_FLAG, .name = "APP_ROUTE_LOCK_FLAG"},
  { .key = FILE_ID_ROUTE_SLAVE_SUC_FLAG, .size = FILE_SIZE_ROUTE_SLAVE_SUC_FLAG, .name = "ROUTE_SLAVE_SUC_FLAG"},
  { .key = FILE_ID_SUC_PENDING_UPDATE_FLAG, .size = FILE_SIZE_SUC_PENDING_UPDATE_FLAG, .name = "SUC_PENDING_UPDATE_FLAG" },
  { .key = FILE_ID_BRIDGE_NODE_FLAG, .size = FILE_SIZE_BRIDGE_NODE_FLAG, .name = "BRIDGE_NODE_FLAG"},
  { .key = FILE_ID_PENDING_DISCOVERY_FLAG, .size = FILE_SIZE_PENDING_DISCOVERY_FLAG, .name = "PENDING_DISCOVERY_FLAG"},
  { .key = FILE_ID_S2_KEYS, .size = FILE_SIZE_S2_KEYS, .name = "S2_KEYS", .optional = true},
  { .key = FILE_ID_S2_KEYCLASSES_ASSIGNED, .size = FILE_SIZE_S2_KEYCLASSES_ASSIGNED, .name = "S2_KEYCLASSES_ASSIGNED", .optional = true},
  { .key = FILE_ID_S2_MPAN, .size = FILE_SIZE_S2_MPAN, .name = "S2_MPAN", .optional = true},
  { .key = FILE_ID_S2_SPAN, .size = FILE_SIZE_S2_SPAN, .name = "S2_SPAN", .optional = true},
};


static nvm3_file_descriptor_t nvm3_protocol_files_v1[] = {
  { .key = FILE_ID_ZW_VERSION, .size = FILE_SIZE_ZW_VERSION, .name = "ZW_VERSION"},
  { .key = FILE_ID_NODEINFO_V1, .size = FILE_SIZE_NODEINFO_V1, .name = "NODEINFO_V1", .optional = true, .num_keys = ZW_CLASSIC_MAX_NODES / NODEINFOS_PER_FILE},
  { .key = FILE_ID_NODEROUTECAHE_V1, .size = FILE_SIZE_NODEROUTECAHE_V1, .name = "NODEROUTECAHE_V1", .optional = true, .num_keys = ZW_CLASSIC_MAX_NODES / NODEROUTECACHES_PER_FILE},
  { .key = FILE_ID_PREFERREDREPEATERS, .size = FILE_SIZE_PREFERREDREPEATERS, .name = "PREFERREDREPEATERS", .optional = true},
  { .key = FILE_ID_SUCNODELIST, .size = FILE_SIZE_SUCNODELIST, .name = "SUCNODELIST"},
  { .key = FILE_ID_CONTROLLERINFO, .size = FILE_SIZE_CONTROLLERINFO_NVM711, .name = "CONTROLLERINFO"},
  { .key = FILE_ID_NODE_STORAGE_EXIST, .size = FILE_SIZE_NODE_STORAGE_EXIST, .name = "NODE_STORAGE_EXIST"},
  { .key = FILE_ID_NODE_ROUTECACHE_EXIST, .size = FILE_SIZE_NODE_ROUTECACHE_EXIST, .name = "NODE_ROUTECACHE_EXIST"},
  { .key = FILE_ID_APP_ROUTE_LOCK_FLAG, .size = FILE_SIZE_APP_ROUTE_LOCK_FLAG, .name = "APP_ROUTE_LOCK_FLAG"},
  { .key = FILE_ID_ROUTE_SLAVE_SUC_FLAG, .size = FILE_SIZE_ROUTE_SLAVE_SUC_FLAG, .name = "ROUTE_SLAVE_SUC_FLAG"},
  { .key = FILE_ID_SUC_PENDING_UPDATE_FLAG, .size = FILE_SIZE_SUC_PENDING_UPDATE_FLAG, .name = "SUC_PENDING_UPDATE_FLAG" },
  { .key = FILE_ID_BRIDGE_NODE_FLAG, .size = FILE_SIZE_BRIDGE_NODE_FLAG, .name = "BRIDGE_NODE_FLAG"},
  { .key = FILE_ID_PENDING_DISCOVERY_FLAG, .size = FILE_SIZE_PENDING_DISCOVERY_FLAG, .name = "PENDING_DISCOVERY_FLAG"},
  { .key = FILE_ID_S2_KEYS, .size = FILE_SIZE_S2_KEYS, .name = "S2_KEYS", .optional = true},
  { .key = FILE_ID_S2_KEYCLASSES_ASSIGNED, .size = FILE_SIZE_S2_KEYCLASSES_ASSIGNED, .name = "S2_KEYCLASSES_ASSIGNED", .optional = true},
  { .key = FILE_ID_S2_MPAN, .size = FILE_SIZE_S2_MPAN, .name = "S2_MPAN", .optional = true},
  { .key = FILE_ID_S2_SPAN, .size = FILE_SIZE_S2_SPAN, .name = "S2_SPAN", .optional = true},
};

static nvm3_file_descriptor_t nvm3_protocol_files_v2[] = {
  { .key = FILE_ID_ZW_VERSION, .size = FILE_SIZE_ZW_VERSION, .name = "ZW_VERSION"},
  { .key = FILE_ID_NODEINFO_V1, .size = FILE_SIZE_NODEINFO_V1, .name = "NODEINFO_V1", .optional = true, .num_keys = ZW_CLASSIC_MAX_NODES / NODEINFOS_PER_FILE},
  { .key = FILE_ID_NODEINFO_LR, .size = FILE_SIZE_NODEINFO_LR, .name = "NODEINFO_LR", .optional = true, .num_keys = ZW_LR_MAX_NODES / LR_NODEINFOS_PER_FILE},
  { .key = FILE_ID_LR_TX_POWER_V2, .size = FILE_SIZE_LR_TX_POWER, .name = "LR_TX_POWER_V2", .optional = true, .num_keys = ZW_LR_MAX_NODES / LR_TX_POWER_PER_FILE_V2},
  { .key = FILE_ID_NODEROUTECAHE_V1, .size = FILE_SIZE_NODEROUTECAHE_V1, .name = "NODEROUTECAHE_V1", .optional = true, .num_keys = ZW_CLASSIC_MAX_NODES / NODEROUTECACHES_PER_FILE},
  { .key = FILE_ID_PREFERREDREPEATERS, .size = FILE_SIZE_PREFERREDREPEATERS, .name = "PREFERREDREPEATERS", .optional = true},
  { .key = FILE_ID_SUCNODELIST, .size = FILE_SIZE_SUCNODELIST, .name = "SUCNODELIST"},
  { .key = FILE_ID_CONTROLLERINFO, .size = FILE_SIZE_CONTROLLERINFO_NVM715, .name = "CONTROLLERINFO"},
  { .key = FILE_ID_NODE_STORAGE_EXIST, .size = FILE_SIZE_NODE_STORAGE_EXIST, .name = "NODE_STORAGE_EXIST"},
  { .key = FILE_ID_NODE_ROUTECACHE_EXIST, .size = FILE_SIZE_NODE_ROUTECACHE_EXIST, .name = "NODE_ROUTECACHE_EXIST"},
  { .key = FILE_ID_LRANGE_NODE_EXIST, .size = FILE_SIZE_LRANGE_NODE_EXIST, .name = "LRANGE_NODE_EXIST"},
  { .key = FILE_ID_APP_ROUTE_LOCK_FLAG, .size = FILE_SIZE_APP_ROUTE_LOCK_FLAG, .name = "APP_ROUTE_LOCK_FLAG"},
  { .key = FILE_ID_ROUTE_SLAVE_SUC_FLAG, .size = FILE_SIZE_ROUTE_SLAVE_SUC_FLAG, .name = "ROUTE_SLAVE_SUC_FLAG"},
  { .key = FILE_ID_SUC_PENDING_UPDATE_FLAG, .size = FILE_SIZE_SUC_PENDING_UPDATE_FLAG, .name = "SUC_PENDING_UPDATE_FLAG" },
  { .key = FILE_ID_BRIDGE_NODE_FLAG, .size = FILE_SIZE_BRIDGE_NODE_FLAG, .name = "BRIDGE_NODE_FLAG"},
  { .key = FILE_ID_PENDING_DISCOVERY_FLAG, .size = FILE_SIZE_PENDING_DISCOVERY_FLAG, .name = "PENDING_DISCOVERY_FLAG"},
};

static nvm3_file_descriptor_t nvm3_protocol_files_v3[] = {
  { .key = FILE_ID_ZW_VERSION, .size = FILE_SIZE_ZW_VERSION, .name = "ZW_VERSION"},
  { .key = FILE_ID_NODEINFO_V1, .size = FILE_SIZE_NODEINFO_V1, .name = "NODEINFO_V1", .optional = true, .num_keys = ZW_CLASSIC_MAX_NODES / NODEINFOS_PER_FILE},
  { .key = FILE_ID_NODEINFO_LR, .size = FILE_SIZE_NODEINFO_LR, .name = "NODEINFO_LR", .optional = true, .num_keys = ZW_LR_MAX_NODES / LR_NODEINFOS_PER_FILE},
  { .key = FILE_ID_LR_TX_POWER_V3, .size = FILE_SIZE_LR_TX_POWER, .name = "LR_TX_POWER_V3", .optional = true, .num_keys = ZW_LR_MAX_NODES / LR_TX_POWER_PER_FILE_V3},
  { .key = FILE_ID_NODEROUTECAHE_V1, .size = FILE_SIZE_NODEROUTECAHE_V1, .name = "NODEROUTECAHE_V1", .optional = true, .num_keys = ZW_CLASSIC_MAX_NODES / NODEROUTECACHES_PER_FILE},
  { .key = FILE_ID_PREFERREDREPEATERS, .size = FILE_SIZE_PREFERREDREPEATERS, .name = "PREFERREDREPEATERS", .optional = true},
  { .key = FILE_ID_SUCNODELIST, .size = FILE_SIZE_SUCNODELIST, .name = "SUCNODELIST"},
  { .key = FILE_ID_CONTROLLERINFO, .size = FILE_SIZE_CONTROLLERINFO_NVM715, .name = "CONTROLLERINFO"},
  { .key = FILE_ID_NODE_STORAGE_EXIST, .size = FILE_SIZE_NODE_STORAGE_EXIST, .name = "NODE_STORAGE_EXIST"},
  { .key = FILE_ID_NODE_ROUTECACHE_EXIST, .size = FILE_SIZE_NODE_ROUTECACHE_EXIST, .name = "NODE_ROUTECACHE_EXIST"},
  { .key = FILE_ID_LRANGE_NODE_EXIST, .size = FILE_SIZE_LRANGE_NODE_EXIST, .name = "LRANGE_NODE_EXIST"},
  { .key = FILE_ID_APP_ROUTE_LOCK_FLAG, .size = FILE_SIZE_APP_ROUTE_LOCK_FLAG, .name = "APP_ROUTE_LOCK_FLAG"},
  { .key = FILE_ID_ROUTE_SLAVE_SUC_FLAG, .size = FILE_SIZE_ROUTE_SLAVE_SUC_FLAG, .name = "ROUTE_SLAVE_SUC_FLAG"},
  { .key = FILE_ID_SUC_PENDING_UPDATE_FLAG, .size = FILE_SIZE_SUC_PENDING_UPDATE_FLAG, .name = "SUC_PENDING_UPDATE_FLAG" },
  { .key = FILE_ID_BRIDGE_NODE_FLAG, .size = FILE_SIZE_BRIDGE_NODE_FLAG, .name = "BRIDGE_NODE_FLAG"},
  { .key = FILE_ID_PENDING_DISCOVERY_FLAG, .size = FILE_SIZE_PENDING_DISCOVERY_FLAG, .name = "PENDING_DISCOVERY_FLAG"},
};

static nvm3_file_descriptor_t nvm3_protocol_files_v4[] = {
  { .key = FILE_ID_ZW_VERSION, .size = FILE_SIZE_ZW_VERSION, .name = "ZW_VERSION"},
  { .key = FILE_ID_NODEINFO_V1, .size = FILE_SIZE_NODEINFO_V1, .name = "NODEINFO_V1", .optional = true, .num_keys = ZW_CLASSIC_MAX_NODES / NODEINFOS_PER_FILE},
  { .key = FILE_ID_NODEINFO_LR, .size = FILE_SIZE_NODEINFO_LR, .name = "NODEINFO_LR", .optional = true, .num_keys = ZW_LR_MAX_NODES / LR_NODEINFOS_PER_FILE},
  { .key = FILE_ID_NODEROUTECAHE_V1, .size = FILE_SIZE_NODEROUTECAHE_V1, .name = "NODEROUTECAHE_V1", .optional = true, .num_keys = ZW_CLASSIC_MAX_NODES / NODEROUTECACHES_PER_FILE},
  { .key = FILE_ID_PREFERREDREPEATERS, .size = FILE_SIZE_PREFERREDREPEATERS, .name = "PREFERREDREPEATERS", .optional = true},
  { .key = FILE_ID_SUCNODELIST, .size = FILE_SIZE_SUCNODELIST, .name = "SUCNODELIST"},
  { .key = FILE_ID_CONTROLLERINFO, .size = FILE_SIZE_CONTROLLERINFO_NVM715, .name = "CONTROLLERINFO"},
  { .key = FILE_ID_NODE_STORAGE_EXIST, .size = FILE_SIZE_NODE_STORAGE_EXIST, .name = "NODE_STORAGE_EXIST"},
  { .key = FILE_ID_NODE_ROUTECACHE_EXIST, .size = FILE_SIZE_NODE_ROUTECACHE_EXIST, .name = "NODE_ROUTECACHE_EXIST"},
  { .key = FILE_ID_LRANGE_NODE_EXIST, .size = FILE_SIZE_LRANGE_NODE_EXIST, .name = "LRANGE_NODE_EXIST"},
  { .key = FILE_ID_APP_ROUTE_LOCK_FLAG, .size = FILE_SIZE_APP_ROUTE_LOCK_FLAG, .name = "APP_ROUTE_LOCK_FLAG"},
  { .key = FILE_ID_ROUTE_SLAVE_SUC_FLAG, .size = FILE_SIZE_ROUTE_SLAVE_SUC_FLAG, .name = "ROUTE_SLAVE_SUC_FLAG"},
  { .key = FILE_ID_SUC_PENDING_UPDATE_FLAG, .size = FILE_SIZE_SUC_PENDING_UPDATE_FLAG, .name = "SUC_PENDING_UPDATE_FLAG" },
  { .key = FILE_ID_BRIDGE_NODE_FLAG, .size = FILE_SIZE_BRIDGE_NODE_FLAG, .name = "BRIDGE_NODE_FLAG"},
  { .key = FILE_ID_PENDING_DISCOVERY_FLAG, .size = FILE_SIZE_PENDING_DISCOVERY_FLAG, .name = "PENDING_DISCOVERY_FLAG"},
};

static nvm3_file_descriptor_t nvm3_app_files_prior_7_15_3[] = {
    {.key = ZAF_FILE_ID_APP_VERSION, .size = ZAF_FILE_SIZE_APP_VERSION, .name = "APP_VERSION"},
    {.key = FILE_ID_APPLICATIONSETTINGS, .size = FILE_SIZE_APPLICATIONSETTINGS, .name = "APPLICATIONSETTINGS"},
    {.key = FILE_ID_APPLICATIONCMDINFO, .size = FILE_SIZE_APPLICATIONCMDINFO, .name = "APPLICATIONCMDINFO"},
    {.key = FILE_ID_APPLICATIONCONFIGURATION, .size = FILE_SIZE_APPLICATIONCONFIGURATION_prior_7_15_3, .name = "APPLICATIONCONFIGURATION"},
    {.key = FILE_ID_APPLICATIONDATA, .size = FILE_SIZE_APPLICATIONDATA, .name = "APPLICATIONDATA"}
};

static nvm3_file_descriptor_t nvm3_app_files_prior_7_18_1[] = {
    {.key = ZAF_FILE_ID_APP_VERSION, .size = ZAF_FILE_SIZE_APP_VERSION, .name = "APP_VERSION"},
    {.key = FILE_ID_APPLICATIONSETTINGS, .size = FILE_SIZE_APPLICATIONSETTINGS, .name = "APPLICATIONSETTINGS"},
    {.key = FILE_ID_APPLICATIONCMDINFO, .size = FILE_SIZE_APPLICATIONCMDINFO, .name = "APPLICATIONCMDINFO"},
    {.key = FILE_ID_APPLICATIONCONFIGURATION, .size = FILE_SIZE_APPLICATIONCONFIGURATION_prior_7_18_1, .name = "APPLICATIONCONFIGURATION"},
    {.key = FILE_ID_APPLICATIONDATA, .size = FILE_SIZE_APPLICATIONDATA, .name = "APPLICATIONDATA"}
};

static nvm3_file_descriptor_t nvm3_app_files[] = {
    {.key = ZAF_FILE_ID_APP_VERSION, .size = ZAF_FILE_SIZE_APP_VERSION, .name = "APP_VERSION"},
    {.key = FILE_ID_APPLICATIONSETTINGS, .size = FILE_SIZE_APPLICATIONSETTINGS, .name = "APPLICATIONSETTINGS"},
    {.key = FILE_ID_APPLICATIONCMDINFO, .size = FILE_SIZE_APPLICATIONCMDINFO, .name = "APPLICATIONCMDINFO"},
    {.key = FILE_ID_APPLICATIONCONFIGURATION, .size = FILE_SIZE_APPLICATIONCONFIGURATION, .name = "APPLICATIONCONFIGURATION"},
    {.key = FILE_ID_APPLICATIONDATA, .size = FILE_SIZE_APPLICATIONDATA, .name = "APPLICATIONDATA"}
};

#define READ_PROT_NVM(file_id, dest_var) nvm3_readData_print_error(&nvm3_protocol_handle, \
                                                                   (file_id),             \
                                                                   &(dest_var),           \
                                                                   sizeof(dest_var),      \
                                                                   nvm3_current_protocol_files,   \
                                                                   nvm3_current_protocol_files_size)

#define READ_APP_NVM_PRIOR_7_15_3(file_id, dest_var) nvm3_readData_print_error(&nvm3_app_handle, \
                                                                              (file_id),        \
                                                                              &(dest_var),      \
                                                                              sizeof(dest_var), \
                                                                              nvm3_app_files_prior_7_15_3,   \
                                                                              sizeof_array(nvm3_app_files_prior_7_15_3))

#define READ_APP_NVM(file_id, dest_var) nvm3_readData_print_error(&nvm3_app_handle, \
                                                                  (file_id),        \
                                                                  &(dest_var),      \
                                                                  sizeof(dest_var), \
                                                                  nvm3_app_files,   \
                                                                  sizeof_array(nvm3_app_files))

#define WRITE_PROT_NVM(file_id, src_var) nvm3_writeData_print_error(&nvm3_protocol_handle, \
                                                                    (file_id),             \
                                                                    &(src_var),            \
                                                                    sizeof(src_var),       \
                                                                    nvm3_current_protocol_files,   \
                                                                    nvm3_current_protocol_files_size)

#define WRITE_APP_NVM_PRIOR_7_15_3(file_id, src_var) nvm3_writeData_print_error(&nvm3_app_handle, \
                                                                   (file_id),        \
                                                                   &(src_var),       \
                                                                   sizeof(src_var),  \
                                                                   nvm3_app_files_prior_7_15_3,   \
                                                                   sizeof_array(nvm3_app_files_prior_7_15_3))

#define WRITE_APP_NVM(file_id, src_var) nvm3_writeData_print_error(&nvm3_app_handle, \
                                                                   (file_id),        \
                                                                   &(src_var),       \
                                                                   sizeof(src_var),  \
                                                                   nvm3_app_files,   \
                                                                   sizeof_array(nvm3_app_files))

/*****************************************************************************/
/****************************  NVM Basic Handling  ***************************/
/*****************************************************************************/

static bool set_target_version(uint32_t protocol_version) {
  switch(protocol_version) {
    case 711:
      target_protocol_version.format = 0;
      target_protocol_version.major = 7;
      target_protocol_version.minor = 11;
      target_protocol_version.patch = 0;
      nvm3_current_protocol_files = nvm3_protocol_files_v0;
      nvm3_current_protocol_files_size = sizeof_array(nvm3_protocol_files_v0);
      return true;
    case 712:
      target_protocol_version.format = 1;
      target_protocol_version.major = 7;
      target_protocol_version.minor = 12;
      target_protocol_version.patch = 0;
      nvm3_current_protocol_files = nvm3_protocol_files_v1;
      nvm3_current_protocol_files_size = sizeof_array(nvm3_protocol_files_v1);
      return true;
    case 715:
      target_protocol_version.format = 2;
      target_protocol_version.major = 7;
      target_protocol_version.minor = 15;
      target_protocol_version.patch = 2;
      nvm3_current_protocol_files = nvm3_protocol_files_v2;
      nvm3_current_protocol_files_size = sizeof_array(nvm3_protocol_files_v2);
      return true;
    case 716:
      target_protocol_version.format = 3;
      target_protocol_version.major = 7;
      target_protocol_version.minor = 16;
      target_protocol_version.patch = 1;
      nvm3_current_protocol_files = nvm3_protocol_files_v3;
      nvm3_current_protocol_files_size = sizeof_array(nvm3_protocol_files_v3);
      return true;
    case 717:
      target_protocol_version.format = 4;
      target_protocol_version.major = 7;
      target_protocol_version.minor = 17;
      target_protocol_version.patch = 1;
      nvm3_current_protocol_files = nvm3_protocol_files_v4;
      nvm3_current_protocol_files_size = sizeof_array(nvm3_protocol_files_v4);
      return true;
    case 718:
      target_protocol_version.format = 4;
      target_protocol_version.major = 7;
      target_protocol_version.minor = 18;
      target_protocol_version.patch = 1;
      nvm3_current_protocol_files = nvm3_protocol_files_v4;
      nvm3_current_protocol_files_size = sizeof_array(nvm3_protocol_files_v4);
      return true;
    default:
      return false;
  }
}

static bool app_nvm_is_pre_v7_15_3(uint8_t major, uint8_t minor, uint8_t patch)
{
  if ((major == 7) && (minor >= 16)) {
    return false;
  }
  else if ((major == 7) && (minor == 15) && (patch >= 3)) {
    return false;
  }
  return true;
}

static bool app_nvm_is_pre_v7_18_1(uint8_t major, uint8_t minor, uint8_t patch)
{
  if ((major == 7) && (minor >= 19)) {
    return false;
  }
  else if ((major == 7) && (minor == 18) && (patch >= 1)) {
    return false;
  }
  return true;
}

/*****************************************************************************/
bool check_controller_nvm(const uint8_t *nvm_image, size_t nvm_image_size, nvmLayout_t nvm_layout)
{
  uint32_t prot_version_le = 0;
  uint32_t app_version_le  = 0;
  uint8_t major = 0, minor = 0, patch = 0;
  bool check_pass = false;

  if(!open_controller_nvm(nvm_image, nvm_image_size, nvm_layout)) return false;

  READ_PROT_NVM(FILE_ID_ZW_VERSION, prot_version_le);
  major = (le32toh(prot_version_le) >> 16) & 0xff;
  minor = (le32toh(prot_version_le) >> 8) & 0xff;
  patch = (le32toh(prot_version_le) >> 0) & 0xff;
  //READ_APP_NVM(ZAF_FILE_ID_APP_VERSION, app_version_le);

  uint8_t protocol_version_format = (le32toh(prot_version_le) >> 24) & 0xff;

  if( protocol_version_format == 0 ) {
    check_pass = nvm3_check_files( &nvm3_protocol_handle, nvm3_protocol_files_v0,sizeof_array(nvm3_protocol_files_v0));
  } else if( protocol_version_format==1) {
    check_pass = nvm3_check_files( &nvm3_protocol_handle, nvm3_protocol_files_v1,sizeof_array(nvm3_protocol_files_v1));
  } else if( protocol_version_format==2) {
    check_pass = nvm3_check_files( &nvm3_protocol_handle, nvm3_protocol_files_v2,sizeof_array(nvm3_protocol_files_v2));
  } else if( protocol_version_format==3) {
    check_pass = nvm3_check_files( &nvm3_protocol_handle, nvm3_protocol_files_v3,sizeof_array(nvm3_protocol_files_v3));
  } else if( protocol_version_format==4) {
    check_pass = nvm3_check_files( &nvm3_protocol_handle, nvm3_protocol_files_v4,sizeof_array(nvm3_protocol_files_v4));
  } else {
    user_message(MSG_ERROR, "ERROR: Conversion of protocol file system v:%u is not supported\n", protocol_version_format);
    check_pass = false;
  }

  if (app_nvm_is_pre_v7_15_3(major, minor, patch)) {
    check_pass &= nvm3_check_files(&nvm3_app_handle,nvm3_app_files_prior_7_15_3,sizeof_array(nvm3_app_files_prior_7_15_3));
  } else if (app_nvm_is_pre_v7_18_1(major, minor, patch)) {
    check_pass &= nvm3_check_files(&nvm3_app_handle,nvm3_app_files_prior_7_18_1,sizeof_array(nvm3_app_files_prior_7_18_1));
  } else {
    check_pass &= nvm3_check_files(&nvm3_app_handle,nvm3_app_files,sizeof_array(nvm3_app_files) );
  }

  close_controller_nvm();
  return check_pass;
}

/*****************************************************************************/
bool open_controller_nvm(const uint8_t *nvm_image, size_t nvm_image_size, nvmLayout_t nvm_layout)
{
  Ecode_t ec;
  size_t   flash_page_size;
  uint32_t app_nvm_size;
  uint32_t protocol_nvm_size;
  uint32_t total_nvm_size;

  switch(nvm_layout) {
    case NVM3_700s :
      flash_page_size   = FLASH_PAGE_SIZE_700s;
      app_nvm_size      = APP_NVM_SIZE_700s;
	  protocol_nvm_size = PROTOCOL_NVM_SIZE_700s;
      break;

    case NVM3_800s :
      flash_page_size   = FLASH_PAGE_SIZE_800s;
      app_nvm_size      = APP_NVM_SIZE_800s;
      protocol_nvm_size = PROTOCOL_NVM_SIZE_800s;
      break;

    default : /* Optional */
    user_message(MSG_ERROR, "ERROR: nvm_layout not recognized\n");
    return false;
  }

  total_nvm_size = app_nvm_size + protocol_nvm_size;

  // Align on FLASH_PAGE_SIZE boundary
  nvm3_storage_address = (nvm3_HalPtr_t)(((size_t) nvm3_storage & ~(flash_page_size - 1)) + flash_page_size);
  memset(nvm3_storage_address, 0xff, total_nvm_size);

  halHandle = &nvm3_halRamHandle;
  halConfig = &nvm3_halRamConfig;

  nvm3_halSetPageSize(halConfig, flash_page_size);

  ec = nvm3_halOpen(halHandle, nvm3_storage_address, total_nvm_size);
  if (ECODE_NVM3_OK != ec)
  {
    user_message(MSG_ERROR, "ERROR: nvm3_halOpen() returned 0x%x\n", ec);
    return false;
  }

  if (NULL != nvm_image)
  {
    ec = nvm3_halRamSetBin(nvm_image, nvm_image_size);
    if (ECODE_NVM3_OK != ec)
    {
      user_message(MSG_ERROR, "ERROR: nvm3_halSetBin() returned 0x%x\n", ec);
      return false;
    }
  }

  nvm3_HalPtr_t nvm3_app_address      = nvm3_storage_address;
  nvm3_HalPtr_t nvm3_protocol_address = ((uint8_t *) nvm3_app_address) + app_nvm_size;

  nvm3_Init_t nvm3_app_init = (nvm3_Init_t) {
                                .nvmAdr          = nvm3_app_address,
                                .nvmSize         = app_nvm_size,
                                .cachePtr        = nvm3_app_cache,
                                .cacheEntryCount = sizeof(nvm3_app_cache) / sizeof(nvm3_CacheEntry_t),
                                .maxObjectSize   = NVM3_MAX_OBJECT_SIZE,
                                .repackHeadroom  = 0,
                                .halHandle       = halHandle
                              };

  nvm3_Init_t nvm3_protocol_init = (nvm3_Init_t) {
                                .nvmAdr          = nvm3_protocol_address,
                                .nvmSize         = protocol_nvm_size,
                                .cachePtr        = nvm3_protocol_cache,
                                .cacheEntryCount = sizeof(nvm3_protocol_cache) / sizeof(nvm3_CacheEntry_t),
                                .maxObjectSize   = NVM3_MAX_OBJECT_SIZE,
                                .repackHeadroom  = 0,
                                .halHandle       = halHandle
                              };

  Ecode_t ec_app = nvm3_open(&nvm3_app_handle, &nvm3_app_init);
  Ecode_t ec_prot = nvm3_open(&nvm3_protocol_handle, &nvm3_protocol_init);

  if ((ECODE_NVM3_OK != ec_app) || (ECODE_NVM3_OK != ec_prot))
  {
    user_message(MSG_ERROR, "nvm3_open(app_nvm) returned 0x%x\n"
                            "nvm3_open(protocol_nvm) returned 0x%x\n",
                            ec_app, ec_prot);
    return false;
  }

  return true;
}

/*****************************************************************************/
size_t get_controller_nvm_image(uint8_t **image_buf)
{
  if (nvm3_app_handle.hasBeenOpened && nvm3_protocol_handle.hasBeenOpened)
  {
    return nvm3_halRamGetBin(image_buf);
  }
  return 0;
}

/*****************************************************************************/
void close_controller_nvm()
{
  if (nvm3_app_handle.hasBeenOpened)
  {
    nvm3_close(&nvm3_app_handle);
    memset(&nvm3_app_handle, 0, sizeof(nvm3_app_handle));
  }

  if (nvm3_protocol_handle.hasBeenOpened)
  {
    nvm3_close(&nvm3_protocol_handle);
    memset(&nvm3_protocol_handle, 0, sizeof(nvm3_protocol_handle));
  }

  nvm3_storage_address  = 0;
}

/*****************************************************************************/
void dump_controller_nvm_keys(void)
{
  if (nvm3_app_handle.hasBeenOpened)
  {
    user_message(MSG_DIAG, "## Nvm3 object keys in application instance:\n");

    if (app_nvm_is_pre_v7_15_3(target_protocol_version.major, target_protocol_version.minor, target_protocol_version.patch)) {
      nvm3_dump_keys_with_filename(&nvm3_app_handle,
                                 nvm3_app_files_prior_7_15_3,
                                 sizeof_array(nvm3_app_files_prior_7_15_3));
    } else if (app_nvm_is_pre_v7_18_1(target_protocol_version.major, target_protocol_version.minor, target_protocol_version.patch)) {
      nvm3_dump_keys_with_filename(&nvm3_app_handle,
                                 nvm3_app_files_prior_7_18_1,
                                 sizeof_array(nvm3_app_files_prior_7_18_1));
    } else {
      nvm3_dump_keys_with_filename(&nvm3_app_handle,
                                   nvm3_app_files,
                                   sizeof_array(nvm3_app_files));
    }
  }
  if (nvm3_protocol_handle.hasBeenOpened)
  {
    user_message(MSG_DIAG, "## Nvm3 object keys in protocol instance:\n");
    nvm3_dump_keys_with_filename(&nvm3_protocol_handle,
                                 nvm3_current_protocol_files,nvm3_current_protocol_files_size);
  }
}

/*****************************************************************************/
/*******************************  NVM --> JSON  ******************************/
/*****************************************************************************/


/*****************************************************************************/
static json_object* backup_info_to_json(void)
{
  json_object *jo           = json_object_new_object();
  time_t       current_time = time(NULL);
  struct tm   *tm_info      = gmtime(&current_time);

  // 700 series is little endian
  uint32_t prot_version_le = 0;
  uint32_t app_version_le  = 0;

  READ_PROT_NVM(FILE_ID_ZW_VERSION, prot_version_le);
  READ_APP_NVM(ZAF_FILE_ID_APP_VERSION, app_version_le);

  char datetime_buf[25];
  strftime(datetime_buf, sizeof_array(datetime_buf), "%Y-%m-%dT%H-%M-%SZ", tm_info);

  json_add_int(jo,           "backupFormatVersion",   1);
  json_object_object_add(jo, "sourceProtocolVersion", version_to_json(le32toh(prot_version_le)));
  json_object_object_add(jo, "sourceAppVersion",      version_to_json(le32toh(app_version_le)));
  json_add_string(jo,        "date",                  datetime_buf);

  target_protocol_version.format = (le32toh(prot_version_le) >> 24) & 0xff;
  target_protocol_version.major = (le32toh(prot_version_le) >> 16) & 0xff;
  target_protocol_version.minor = (le32toh(prot_version_le) >> 8) & 0xff;
  target_protocol_version.patch = (le32toh(prot_version_le) >> 0) & 0xff;

  return jo;  
}

/*****************************************************************************/
static json_object* app_settings_to_json(void)
{
  SApplicationSettings as = {};

  json_object* jo = json_object_new_object();

  READ_APP_NVM(FILE_ID_APPLICATIONSETTINGS, as);

  json_add_int(jo, "listening", as.listening);
  json_add_int(jo, "generic",   as.generic);
  json_add_int(jo, "specific",  as.specific);

  return jo;
}

/*****************************************************************************/
static json_object* app_config_to_json(void)
{

  json_object* jo = json_object_new_object();
  if (app_nvm_is_pre_v7_15_3(target_protocol_version.major, target_protocol_version.minor, target_protocol_version.patch)) {
    SApplicationConfiguration_prior_7_15_3 ac = {};
    READ_APP_NVM(FILE_ID_APPLICATIONCONFIGURATION, ac);

    json_add_int(jo, "rfRegion",          ac.rfRegion);
    json_add_int(jo, "txPower",           ac.iTxPower);
    json_add_int(jo, "power0dbmMeasured", ac.ipower0dbmMeasured);
  } else if (app_nvm_is_pre_v7_18_1(target_protocol_version.major, target_protocol_version.minor, target_protocol_version.patch)) {
    SApplicationConfiguration_prior_7_18_1 ac = {};
    READ_APP_NVM(FILE_ID_APPLICATIONCONFIGURATION, ac);

    json_add_int(jo, "rfRegion",          ac.rfRegion);
    json_add_int(jo, "txPower",           ac.iTxPower);
    json_add_int(jo, "power0dbmMeasured", ac.ipower0dbmMeasured);
    json_add_int(jo, "enablePTI",         ac.enablePTI);
    json_add_int(jo, "maxTxPower",        ac.maxTxPower);
  } else {
    SApplicationConfiguration ac = {};
    READ_APP_NVM(FILE_ID_APPLICATIONCONFIGURATION, ac);

    json_add_int(jo, "rfRegion",          ac.rfRegion);
    json_add_int(jo, "txPower",           ac.iTxPower);
    json_add_int(jo, "power0dbmMeasured", ac.ipower0dbmMeasured);
    json_add_int(jo, "enablePTI",         ac.enablePTI);
    json_add_int(jo, "maxTxPower",        ac.maxTxPower);
  }

  return jo;
}

/*****************************************************************************/
static json_object* suc_node_list_to_json(void)
{
  SSucNodeList snl = {};

  READ_PROT_NVM(FILE_ID_SUCNODELIST, snl);

  json_object* jo_suc_node_list = json_object_new_array();

  for (int upd = 0; upd < SUC_MAX_UPDATES; upd++)
  {
    SUC_UPDATE_ENTRY_STRUCT *suc_entry = &snl.aSucNodeList[upd];
    json_object             *jo_entry  = json_object_new_object();

    json_add_int(jo_entry, "nodeId", suc_entry->NodeID);
    // changeType: SUC_ADD(1), SUC_DELETE(2), SUC_UPDATE_RANGE(3)
    json_add_int(jo_entry, "changeType", suc_entry->changeType);

    json_object* jo_nparam_list = json_object_new_array();

    for (int npar = 0; npar < SUC_UPDATE_NODEPARM_MAX; npar++)
    {
      if (0 != suc_entry->nodeInfo[npar])
      {
        json_object_array_add(jo_nparam_list, json_object_new_int(suc_entry->nodeInfo[npar]));
      }
    }

    json_object_object_add(jo_entry, "nodeInfo", jo_nparam_list);

    json_object_array_add(jo_suc_node_list, jo_entry);
  }

  return jo_suc_node_list;
}


/*****************************************************************************/
static json_object* route_cache_line_to_json(const ROUTECACHE_LINE *rcl)
{
  json_object* jo = json_object_new_object();
 
  json_add_int(jo, "routecacheLineConf", rcl->routecacheLineConfSize);
  json_add_byte_array(jo, "repeaters", rcl->repeaterList, MAX_REPEATERS);

  return jo;
}

/*****************************************************************************/
static json_object* node_table_to_json(void)
{
  CLASSIC_NODE_MASK_TYPE node_info_exists         = {};
  CLASSIC_NODE_MASK_TYPE bridge_node_flags        = {};
  CLASSIC_NODE_MASK_TYPE suc_pending_update_flags = {};
  CLASSIC_NODE_MASK_TYPE pending_discovery_flags  = {};

  CLASSIC_NODE_MASK_TYPE node_routecache_exists   = {};
  CLASSIC_NODE_MASK_TYPE app_route_lock_flags     = {};
  CLASSIC_NODE_MASK_TYPE route_slave_suc_flags    = {};

  LR_NODE_MASK_TYPE      lr_node_info_exists      = {0};

  SNodeInfo       node_info_buf[NODEINFOS_PER_FILE] = {};
  SNodeInfo      *node_info;
  SLRNodeInfo     lr_node_info_buf[LR_NODEINFOS_PER_FILE] = {};
  SLRNodeInfo    *lr_node_info;
  uint8_t         lr_tx_power_buf[FILE_SIZE_LR_TX_POWER] = {};
  uint8_t        *lr_tx_power;

  SNodeRouteCache route_cache_buf[NODEROUTECACHES_PER_FILE] = {};
  SNodeRouteCache* route_cache;
  Ecode_t ec;

  READ_PROT_NVM(FILE_ID_NODE_STORAGE_EXIST,      node_info_exists);
  if (target_protocol_version.format >= 2)
  {
    READ_PROT_NVM(FILE_ID_LRANGE_NODE_EXIST,       lr_node_info_exists);
  }

  READ_PROT_NVM(FILE_ID_SUC_PENDING_UPDATE_FLAG, suc_pending_update_flags);
  READ_PROT_NVM(FILE_ID_BRIDGE_NODE_FLAG,        bridge_node_flags);
  READ_PROT_NVM(FILE_ID_PENDING_DISCOVERY_FLAG,  pending_discovery_flags);

  READ_PROT_NVM(FILE_ID_NODE_ROUTECACHE_EXIST,   node_routecache_exists);
  READ_PROT_NVM(FILE_ID_APP_ROUTE_LOCK_FLAG,     app_route_lock_flags); 
  READ_PROT_NVM(FILE_ID_ROUTE_SLAVE_SUC_FLAG,    route_slave_suc_flags);

  json_object *jo_node_list = json_object_new_array();

  for (uint8_t node_id = 1; node_id <= ZW_CLASSIC_MAX_NODES; node_id++)
  {
    if (ZW_NodeMaskNodeIn(node_info_exists, node_id))
    {
      /* FILE_ID_NODEINFO is the nvm3 object key for the nodeId 1 file
       * FILE_ID_NODEINFO+1 is the key for the nodeId 2 file, etc. */
      uint8_t node_index = node_id -1;
      if(target_protocol_version.format>0) {
        ec = READ_PROT_NVM(FILE_ID_NODEINFO_V1 + node_index / NODEINFOS_PER_FILE, node_info_buf);
        node_info = &node_info_buf[node_index % NODEINFOS_PER_FILE ];
      } else {
        ec = READ_PROT_NVM(FILE_ID_NODEINFO + node_index, node_info_buf[0]);
        node_info = &node_info_buf[0];
      }

      if (ECODE_NVM3_OK == ec)
      {
        json_object *jo_node = json_object_new_object();

        json_add_int(jo_node,      "nodeId", node_id);
        json_add_bool(jo_node,     "virtualNode", ZW_NodeMaskNodeIn(bridge_node_flags, node_id) ? true : false);
        json_add_bool(jo_node,     "pendingUpdate", ZW_NodeMaskNodeIn(suc_pending_update_flags, node_id) ? true : false);
        json_add_bool(jo_node,     "pendingDiscovery", ZW_NodeMaskNodeIn(pending_discovery_flags, node_id) ? true : false);
        json_add_bool(jo_node,     "routeSlaveSuc", ZW_NodeMaskNodeIn(route_slave_suc_flags, node_id) ? true : false);
        // Index into SUC updates table or one of: 0, SUC_UNKNOWN_CONTROLLER(0xFE), SUC_OUT_OF_DATE(0xFF)
        json_add_int(jo_node,      "controllerSucUpdateIndex", node_info->ControllerSucUpdateIndex);
        json_add_nodemask(jo_node, "neighbours", node_info->neighboursInfo);

        json_object* jo_node_info = json_object_new_object();
        json_add_int(jo_node_info, "capability", node_info->NodeInfo.capability);
        json_add_int(jo_node_info, "security",   node_info->NodeInfo.security);
        json_add_int(jo_node_info, "reserved",   node_info->NodeInfo.reserved);
        json_add_int(jo_node_info, "generic",    node_info->NodeInfo.generic);
        json_add_int(jo_node_info, "specific",   node_info->NodeInfo.specific);
        json_object_object_add(jo_node, "nodeInfo", jo_node_info);

        /* ---- ROUTE CACHE BEGIN ---- */

        json_object *jo_route_cache = json_object_new_object();

        json_add_bool(jo_route_cache, "applock",       ZW_NodeMaskNodeIn(app_route_lock_flags, node_id) ? true : false);

        if (ZW_NodeMaskNodeIn(node_routecache_exists, node_id))
        {
          if(target_protocol_version.format>0) {
            ec = READ_PROT_NVM(FILE_ID_NODEROUTECAHE_V1 + node_index / NODEROUTECACHES_PER_FILE, route_cache_buf);
            route_cache = &route_cache_buf[node_index % NODEROUTECACHES_PER_FILE ];
          } else {
            ec = READ_PROT_NVM(FILE_ID_NODEROUTECAHE + node_index, route_cache_buf[0]);
            route_cache = &route_cache_buf[0];
          }

          if (ECODE_NVM3_OK == ec)
          {
            json_object_object_add(jo_route_cache, "LWR",  route_cache_line_to_json(&route_cache->routeCache));
            json_object_object_add(jo_route_cache, "NLWR", route_cache_line_to_json(&route_cache->routeCacheNlwrSr));
          }
        }
        json_object_object_add(jo_node, "routeCache", jo_route_cache);

        /* ---- ROUTE CACHE END ---- */

        json_object_array_add(jo_node_list, jo_node);
      }
    }

  } // for()

  if(target_protocol_version.format >= 2) {
    for (uint16_t node_id = 1; node_id <= ZW_LR_MAX_NODES; node_id++)
    {
      if (ZW_NodeMaskNodeIn(lr_node_info_exists, node_id))
      {
        uint16_t node_index = node_id -1;
        json_object *jo_node = json_object_new_object();
        json_add_int(jo_node, "nodeId", node_id + ZW_LR_MIN_NODE_ID - 1);

        /* LR node info */
        ec = READ_PROT_NVM(FILE_ID_NODEINFO_LR + node_index / LR_NODEINFOS_PER_FILE, lr_node_info_buf);
        lr_node_info = &lr_node_info_buf[node_index % LR_NODEINFOS_PER_FILE ];
        if (ECODE_NVM3_OK == ec)
        {
          json_add_int(jo_node, "packedInfo", lr_node_info->packedInfo);
          json_add_int(jo_node, "generic", lr_node_info->generic);
          json_add_int(jo_node, "specific", lr_node_info->specific);
        }

        /* LR Tx Power */
        if (target_protocol_version.format == 2)
        {
          ec = READ_PROT_NVM(FILE_ID_LR_TX_POWER_V2 + node_index / LR_TX_POWER_PER_FILE_V2, lr_tx_power_buf);
          lr_tx_power = &lr_tx_power_buf[(node_index % LR_TX_POWER_PER_FILE_V2)/2];
          if (ECODE_NVM3_OK == ec)
          {
            if ((node_id -1) & 1)
            {
              json_add_int(jo_node, "txPower", (int32_t)(*lr_tx_power & 0xf0));
            }
            else
            {
              json_add_int(jo_node, "txPower", (int32_t)(*lr_tx_power & 0x0f));
            }
          }
        }
        if (target_protocol_version.format == 3)
        {
          ec = READ_PROT_NVM(FILE_ID_LR_TX_POWER_V3 + node_index / LR_TX_POWER_PER_FILE_V3, lr_tx_power_buf);
          lr_tx_power = &lr_tx_power_buf[node_index % LR_TX_POWER_PER_FILE_V3];
          if (ECODE_NVM3_OK == ec)
          {
            json_add_int(jo_node, "txPower", (int32_t)(*lr_tx_power));
          }
        }
        json_object_array_add(jo_node_list, jo_node);
      }
    }
  }


  return jo_node_list;
}

/*****************************************************************************/
static json_object* nvm711_controller_info_to_json(void)
{
  json_object* jo = json_object_new_object();

  SControllerInfo_NVM711 ci = {};
  SApplicationCmdClassInfo ai = {};
  SApplicationData ad = {};

  READ_PROT_NVM(FILE_ID_CONTROLLERINFO, ci);
  READ_APP_NVM(FILE_ID_APPLICATIONCMDINFO, ai);
  READ_APP_NVM(FILE_ID_APPLICATIONDATA, ad);

  json_add_int(jo, "nodeId", ci.NodeID);
  json_object_object_add(jo, "ownHomeId", home_id_to_json(ci.HomeID));
  json_object_object_add(jo, "learnedHomeId", home_id_to_json(ci.HomeID));
  json_add_int(jo, "lastUsedNodeId", ci.LastUsedNodeId);
  json_add_int(jo, "staticControllerNodeId", ci.StaticControllerNodeId);
  // json_add_int(jo, "lastIndex", ci.SucLastIndex); // Moved to SUC
  json_add_int(jo,  "controllerConfiguration", ci.ControllerConfiguration);
  // json_add_bool(jo, "sucAwarenessPushNeeded",  ci.SucAwarenessPushNeeded); // Currently not used. Don't save to JSON
  // json_add_int(jo, "maxNodeId", ci.MaxNodeId);  // Don't save. Will be derived when reading the node table
  // json_add_int(jo, "reservedId", ci.ReservedId); // Currently not used. Don't save to JSON
  json_add_int(jo,  "systemState", ci.SystemState);

  /* No need to persist app settings - they can be extracted from the controller
   * node info in the node table */
  // json_object_object_add(jo, "appSettings", app_settings_to_json());

  json_add_byte_array(jo, "cmdClassList", ai.UnSecureIncludedCC, ai.UnSecureIncludedCCLen);
  // The following two are empty for a gateway
  // json_add_byte_array(jo, "SecureIncludedUnSecureCC", ai.SecureIncludedUnSecureCC, ai.SecureIncludedUnSecureCCLen);
  // json_add_byte_array(jo, "SecureIncludedSecureCC",   ai.SecureIncludedSecureCC, ai.SecureIncludedSecureCCLen);

  json_object_object_add(jo, "nodeTable",                   node_table_to_json());

  json_object *jo_suc = json_object_new_object();

  json_add_int(jo_suc,           "lastIndex",      ci.SucLastIndex);
  json_object_object_add(jo_suc, "updateNodeList", suc_node_list_to_json());

  json_object_object_add(jo, "sucState", jo_suc);

  /* Search backwards in the application data section to skip trailing zero
   * values (typically the data is stored at the beginning leaving a large
   * unused section at the end - there's no need to save all those zeros
   * to JSON)
   */
  int n = APPL_DATA_FILE_SIZE;
  while ((n > 0) && (0 == ad.extNvm[n - 1]))
  {
    --n;
  }

  if (n > 0)
  {
    char *b64_encoded_string = base64_encode(ad.extNvm, n, 0);
    if (b64_encoded_string)
    {
      json_add_string(jo, "applicationData", b64_encoded_string);
      free(b64_encoded_string);
    }
  }

  return jo;
}

/*****************************************************************************/
static json_object* nvm715_controller_info_to_json(void)
{
  json_object* jo = json_object_new_object();

  SControllerInfo_NVM715 ci = {};
  SApplicationCmdClassInfo ai = {};
  SApplicationData ad = {};
  
  READ_PROT_NVM(FILE_ID_CONTROLLERINFO, ci);
  READ_APP_NVM(FILE_ID_APPLICATIONCMDINFO, ai);
  READ_APP_NVM(FILE_ID_APPLICATIONDATA, ad);

  json_add_int(jo, "nodeId", ci.NodeID);
  json_object_object_add(jo, "ownHomeId", home_id_to_json(ci.HomeID));
  json_object_object_add(jo, "learnedHomeId", home_id_to_json(ci.HomeID));
  json_add_int(jo, "lastUsedNodeId", ci.LastUsedNodeId);
  json_add_int(jo, "lastUsedNodeIdLR", ci.LastUsedNodeId_LR);
  json_add_int(jo, "staticControllerNodeId", ci.StaticControllerNodeId);
  // json_add_int(jo, "lastIndex", ci.SucLastIndex); // Moved to SUC
  json_add_int(jo,  "controllerConfiguration", ci.ControllerConfiguration);
  // json_add_bool(jo, "sucAwarenessPushNeeded",  ci.SucAwarenessPushNeeded); // Currently not used. Don't save to JSON
  // json_add_int(jo, "maxNodeId", ci.MaxNodeId);  // Don't save. Will be derived when reading the node table
  // json_add_int(jo, "reservedId", ci.ReservedId); // Currently not used. Don't save to JSON
  json_add_int(jo,  "systemState", ci.SystemState);
  json_add_int(jo, "primaryLongRangeChannelId", ci.PrimaryLongRangeChannelId);
  json_add_int(jo, "dcdcConfig", ci.DcdcConfig);

  /* No need to persist app settings - they can be extracted from the controller
   * node info in the node table */
  // json_object_object_add(jo, "appSettings", app_settings_to_json());

  json_add_byte_array(jo, "cmdClassList", ai.UnSecureIncludedCC, ai.UnSecureIncludedCCLen);
  // The following two are empty for a gateway
  // json_add_byte_array(jo, "SecureIncludedUnSecureCC", ai.SecureIncludedUnSecureCC, ai.SecureIncludedUnSecureCCLen);
  // json_add_byte_array(jo, "SecureIncludedSecureCC",   ai.SecureIncludedSecureCC, ai.SecureIncludedSecureCCLen);

  json_object_object_add(jo, "nodeTable",                   node_table_to_json());

  json_object *jo_suc = json_object_new_object();

  json_add_int(jo_suc,           "lastIndex",      ci.SucLastIndex);
  json_object_object_add(jo_suc, "updateNodeList", suc_node_list_to_json());

  json_object_object_add(jo, "sucState", jo_suc);

  /* Search backwards in the application data section to skip trailing zero
   * values (typically the data is stored at the beginning leaving a large
   * unused section at the end - there's no need to save all those zeros
   * to JSON)
   */
  int n = APPL_DATA_FILE_SIZE;
  while ((n > 0) && (0 == ad.extNvm[n - 1]))
  {
    --n;
  }

  if (n > 0)
  {
    char *b64_encoded_string = base64_encode(ad.extNvm, n, 0);
    if (b64_encoded_string)
    {
      json_add_string(jo, "applicationData", b64_encoded_string);
      free(b64_encoded_string);
    }
  }

  return jo;
}

/*****************************************************************************/
json_object* controller_nvm711_get_json()
{
  json_object* jo = json_object_new_object();

  json_object_object_add(jo, "backupInfo",   backup_info_to_json());
  json_object_object_add(jo, "zwController", nvm711_controller_info_to_json());
  json_object_object_add(jo, "appConfig", app_config_to_json());

  return jo;
};

json_object* controller_nvm715_get_json()
{
  json_object* jo = json_object_new_object();

  json_object_object_add(jo, "backupInfo",   backup_info_to_json());
  json_object_object_add(jo, "zwController", nvm715_controller_info_to_json());
  json_object_object_add(jo, "appConfig", app_config_to_json());

  return jo;
};

/*****************************************************************************/
/*******************************  JSON --> NVM  ******************************/
/*****************************************************************************/


/*****************************************************************************/
static bool parse_route_cache_line_json(json_object *jo_rcl, ROUTECACHE_LINE *rcl)
{
  rcl->routecacheLineConfSize = json_get_int(jo_rcl, "routecacheLineConf", 0, JSON_REQUIRED);
  json_get_bytearray(jo_rcl, "repeaters", rcl->repeaterList, MAX_REPEATERS, JSON_REQUIRED);

  return true;
}

/*****************************************************************************/
static bool parse_node_table_json(json_object *jo_ntbl, uint8_t ctrl_node_id, uint8_t *max_node_id, uint16_t *max_node_id_lr)
{
  CLASSIC_NODE_MASK_TYPE node_info_exists         = {};
  CLASSIC_NODE_MASK_TYPE bridge_node_flags        = {};
  CLASSIC_NODE_MASK_TYPE suc_pending_update_flags = {};
  CLASSIC_NODE_MASK_TYPE pending_discovery_flags  = {};
  CLASSIC_NODE_MASK_TYPE route_slave_suc_flags    = {};

  CLASSIC_NODE_MASK_TYPE node_routecache_exists   = {};
  CLASSIC_NODE_MASK_TYPE app_route_lock_flags     = {};

  LR_NODE_MASK_TYPE      lr_node_info_exists      = {0};

  *max_node_id = 0;
  *max_node_id_lr = 0;

  bool controller_node_found = false;
  for (int i = 0; (i < json_object_array_length(jo_ntbl)) && (i < ZW_LR_MAX_NODE_ID); i++)
  {
    json_object *jo_node = json_object_array_get_idx(jo_ntbl, i);
    uint16_t      node_id = json_get_int(jo_node,  "nodeId", 0, JSON_REQUIRED);
    if (node_id > 0 && node_id <= ZW_CLASSIC_MAX_NODES)
    {
      SNodeInfo ni_buf[NODEINFOS_PER_FILE] = {};
      SNodeInfo* ni;
      if (target_protocol_version.format > 0) {
        uint32_t file_type;
        size_t file_len;
        nvm3_ObjectKey_t file_id = FILE_ID_NODEINFO_V1 + (node_id - 1)/NODEINFOS_PER_FILE;
        Ecode_t ec = nvm3_getObjectInfo(&nvm3_protocol_handle, file_id, &file_type, &file_len);
        if (ec == ECODE_NVM3_OK) {
          READ_PROT_NVM(file_id, ni_buf);
        }
        ni = &ni_buf[ (node_id - 1) % NODEINFOS_PER_FILE ];
      } else {
        ni = &ni_buf[0];
      }

      if (json_get_bool(jo_node, "virtualNode", false, JSON_OPTIONAL))
      {
        ZW_NodeMaskSetBit(bridge_node_flags, node_id);
      }
      if (json_get_bool(jo_node, "pendingUpdate", false, JSON_OPTIONAL))
      {
        ZW_NodeMaskSetBit(suc_pending_update_flags, node_id);
      }
      if (json_get_bool(jo_node, "pendingDiscovery", false, JSON_OPTIONAL))
      {
        ZW_NodeMaskSetBit(pending_discovery_flags, node_id);
      }
      if (json_get_bool(jo_node, "routeSlaveSuc", false, JSON_OPTIONAL))
      {
        ZW_NodeMaskSetBit(route_slave_suc_flags, node_id);
      }

      ni->ControllerSucUpdateIndex = json_get_int(jo_node,  "controllerSucUpdateIndex", 0, JSON_OPTIONAL);

      json_get_nodemask(jo_node, "neighbours", ni->neighboursInfo, JSON_OPTIONAL);

      json_object *jo_node_info;
      if (json_get_object_error_check(jo_node, "nodeInfo", &jo_node_info, json_type_object, JSON_OPTIONAL))
      {
        ni->NodeInfo.capability = json_get_int(jo_node_info, "capability", 0, JSON_REQUIRED);
        ni->NodeInfo.security   = json_get_int(jo_node_info, "security",   0, JSON_REQUIRED);
        ni->NodeInfo.reserved   = json_get_int(jo_node_info, "reserved",   0, JSON_OPTIONAL);
        ni->NodeInfo.generic    = json_get_int(jo_node_info, "generic",    0, JSON_REQUIRED);
        ni->NodeInfo.specific   = json_get_int(jo_node_info, "specific",   0, JSON_REQUIRED);
      }

      ZW_NodeMaskSetBit(node_info_exists, node_id);

      if(target_protocol_version.format>0) {
        WRITE_PROT_NVM(FILE_ID_NODEINFO_V1 + (node_id - 1)/NODEINFOS_PER_FILE, ni_buf);      
      } else {
        WRITE_PROT_NVM(FILE_ID_NODEINFO + node_id - 1, ni_buf[0]);
      }

        /* The SerialAPI app settings can be extracted from the controller node in the node table */
      if (node_id == ctrl_node_id)
      {
        SApplicationSettings as = {};

        as.generic  = ni->NodeInfo.generic;
        as.specific = ni->NodeInfo.specific;

        /* Map node info flags to application settings */
        if (ni->NodeInfo.capability & ZWAVE_NODEINFO_LISTENING_SUPPORT)
        {
          as.listening |= APPLICATION_NODEINFO_LISTENING;
        }
        if (ni->NodeInfo.security & ZWAVE_NODEINFO_OPTIONAL_FUNC)
        {
          as.listening |= APPLICATION_NODEINFO_OPTIONAL_FUNC;
        }

        WRITE_APP_NVM(FILE_ID_APPLICATIONSETTINGS, as);

        controller_node_found = true;
      }

      /* ---- ROUTE CACHE BEGIN ---- */

      json_object *jo_node_rc;
      if (json_get_object_error_check(jo_node, "routeCache", &jo_node_rc, json_type_object, JSON_OPTIONAL))
      {
        SNodeRouteCache node_rc_buf[NODEROUTECACHES_PER_FILE] = {};
        SNodeRouteCache* node_rc;

        if(target_protocol_version.format>0) {
          uint32_t file_type; 
          size_t file_len;
          nvm3_ObjectKey_t file_id = FILE_ID_NODEROUTECAHE_V1 + (node_id - 1) / NODEROUTECACHES_PER_FILE;
          Ecode_t ec = nvm3_getObjectInfo(&nvm3_protocol_handle, file_id, &file_type, &file_len);
          if (ec == ECODE_NVM3_OK) {
            READ_PROT_NVM(file_id, node_rc_buf);
          }
          node_rc = &node_rc_buf[ (node_id - 1) % NODEROUTECACHES_PER_FILE ];
        }else {
          node_rc = &node_rc_buf[0];
        }

        if (json_get_bool(jo_node_rc, "applock", false, JSON_OPTIONAL))
        {
          ZW_NodeMaskSetBit(app_route_lock_flags, node_id);
        }

        json_object *jo_lwr;
        if (json_get_object_error_check(jo_node_rc, "LWR", &jo_lwr, json_type_object, JSON_OPTIONAL))
        {
          parse_route_cache_line_json(jo_lwr, &node_rc->routeCache);
          ZW_NodeMaskSetBit(node_routecache_exists, node_id);
        }

        json_object *jo_nlwr;
        if (json_get_object_error_check(jo_node_rc, "NLWR", &jo_nlwr, json_type_object, JSON_OPTIONAL))
        {
          parse_route_cache_line_json(jo_nlwr, &node_rc->routeCacheNlwrSr);
          ZW_NodeMaskSetBit(node_routecache_exists, node_id);
        }

        if (ZW_NodeMaskNodeIn(node_routecache_exists, node_id))
        {

          if(target_protocol_version.format>0) {
            WRITE_PROT_NVM(FILE_ID_NODEROUTECAHE_V1 + (node_id - 1)/NODEINFOS_PER_FILE, node_rc_buf);      
          } else {
            WRITE_PROT_NVM(FILE_ID_NODEROUTECAHE + node_id - 1, node_rc_buf[0]);
          }
        }
      }

      /* ---- ROUTE CACHE END ---- */

      if (*max_node_id < node_id)
      {
        *max_node_id = node_id;
      }

    } else if (node_id >= ZW_LR_MIN_NODE_ID
            && node_id <= ZW_LR_MAX_NODE_ID) {
      SLRNodeInfo ni_buf[LR_NODEINFOS_PER_FILE] = {0};
      SLRNodeInfo* ni;
      uint8_t lr_tx_power_buf[FILE_SIZE_LR_TX_POWER] = {0};
      uint8_t *lr_tx_power;
      if (*max_node_id_lr < node_id)
      {
        *max_node_id_lr = node_id;
      }
      if (target_protocol_version.format >= 2) {
        uint32_t file_type;
        size_t file_len;

        /* LR Node Info */
        nvm3_ObjectKey_t file_id = FILE_ID_NODEINFO_LR + (node_id - ZW_LR_MIN_NODE_ID)/LR_NODEINFOS_PER_FILE;
        Ecode_t ec = nvm3_getObjectInfo(&nvm3_protocol_handle, file_id, &file_type, &file_len);
        if (ec == ECODE_NVM3_OK) {
          READ_PROT_NVM(file_id, ni_buf);
        }
        ZW_NodeMaskSetBit(lr_node_info_exists, node_id - ZW_LR_MIN_NODE_ID + 1);
        ni = &ni_buf[ (node_id - ZW_LR_MIN_NODE_ID) % LR_NODEINFOS_PER_FILE ];
        ni->packedInfo = json_get_int(jo_node, "packedInfo", 0, JSON_REQUIRED);
        ni->generic = json_get_int(jo_node, "generic", 0, JSON_REQUIRED);
        ni->specific = json_get_int(jo_node, "specific", 0, JSON_REQUIRED);
        WRITE_PROT_NVM(file_id, ni_buf);

        /* LR Tx Power */
        if (target_protocol_version.format == 2) {
          file_id = FILE_ID_LR_TX_POWER_V2 + (node_id - ZW_LR_MIN_NODE_ID)/LR_TX_POWER_PER_FILE_V2;
          ec = nvm3_getObjectInfo(&nvm3_protocol_handle, file_id, &file_type, &file_len);
          if (ec == ECODE_NVM3_OK) {
            READ_PROT_NVM(file_id, lr_tx_power_buf);
          }
          lr_tx_power = &lr_tx_power_buf[((node_id - ZW_LR_MIN_NODE_ID) % LR_TX_POWER_PER_FILE_V2) / 2];
          /* Odd and even node ID take first and second half of lr_tx_power respectively */
          if ((node_id - ZW_LR_MIN_NODE_ID) & 1)
          {
            *lr_tx_power |= ((uint8_t)json_get_int(jo_node, "txPower", 0, JSON_REQUIRED) & 0xf0);
          }
          else
          {
            *lr_tx_power |= ((uint8_t)json_get_int(jo_node, "txPower", 0, JSON_REQUIRED) & 0x0f);
          }
        }
        if (target_protocol_version.format == 3) {
          file_id = FILE_ID_LR_TX_POWER_V3 + (node_id - ZW_LR_MIN_NODE_ID)/LR_TX_POWER_PER_FILE_V3;
          ec = nvm3_getObjectInfo(&nvm3_protocol_handle, file_id, &file_type, &file_len);
          if (ec == ECODE_NVM3_OK) {
            READ_PROT_NVM(file_id, lr_tx_power_buf);
          }
          lr_tx_power = &lr_tx_power_buf[(node_id - ZW_LR_MIN_NODE_ID) % LR_TX_POWER_PER_FILE_V3];
          *lr_tx_power = (uint8_t)json_get_int(jo_node, "txPower", 0, JSON_REQUIRED);
        }
        WRITE_PROT_NVM(file_id, lr_tx_power_buf);
      }
    }    
  }

  if (false == controller_node_found)
  {
    user_message(MSG_ERROR,
                 "ERROR: No entry for controller node (nodeId: %d) found at %s.\n",
                 ctrl_node_id,
                 json_get_object_path(jo_ntbl));
  }

  /* Write all the node mask files */
  WRITE_PROT_NVM(FILE_ID_NODE_STORAGE_EXIST,      node_info_exists);
  if (target_protocol_version.format >= 2)
  {
    WRITE_PROT_NVM(FILE_ID_LRANGE_NODE_EXIST,       lr_node_info_exists);
  }

  WRITE_PROT_NVM(FILE_ID_BRIDGE_NODE_FLAG,        bridge_node_flags);
  WRITE_PROT_NVM(FILE_ID_SUC_PENDING_UPDATE_FLAG, suc_pending_update_flags);
  WRITE_PROT_NVM(FILE_ID_PENDING_DISCOVERY_FLAG,  pending_discovery_flags);

  WRITE_PROT_NVM(FILE_ID_NODE_ROUTECACHE_EXIST,   node_routecache_exists);
  WRITE_PROT_NVM(FILE_ID_APP_ROUTE_LOCK_FLAG,     app_route_lock_flags);
  WRITE_PROT_NVM(FILE_ID_ROUTE_SLAVE_SUC_FLAG,    route_slave_suc_flags);
  return controller_node_found;
}

/*****************************************************************************/
static bool parse_suc_state_json(json_object *jo_suc, uint8_t *last_suc_index)
{
  SSucNodeList suc_node_list = {};

  json_object* jo_suc_updates = NULL;

  if (json_get_object_error_check(jo_suc, "updateNodeList", &jo_suc_updates, json_type_array, JSON_REQUIRED))
  {
    for (int i = 0; (i < json_object_array_length(jo_suc_updates)) && (i < SUC_MAX_UPDATES); i++)
    {
      json_object             *jo_suc_upd = json_object_array_get_idx(jo_suc_updates, i);
      SUC_UPDATE_ENTRY_STRUCT *suc_entry = &suc_node_list.aSucNodeList[i];

      suc_entry->NodeID     = json_get_int(jo_suc_upd, "nodeId", 0, JSON_REQUIRED);
      suc_entry->changeType = json_get_int(jo_suc_upd, "changeType", 0, JSON_REQUIRED);

      json_get_bytearray(jo_suc_upd, "nodeInfo", suc_entry->nodeInfo, SUC_UPDATE_NODEPARM_MAX, JSON_REQUIRED);
    }
  }

  *last_suc_index = json_get_int(jo_suc, "lastIndex", 0, JSON_REQUIRED);

  WRITE_PROT_NVM(FILE_ID_SUCNODELIST, suc_node_list);

  return true;
}

/*****************************************************************************/
static bool parse_app_config_json(json_object *jo_app_config, uint8_t major_app_version
                                                            , uint8_t minor_app_version
                                                            , uint8_t patch_app_version)
{

  /* Only read in these appConfig properties if the JSON is generated from a
   * controllerwith application version 7 (the application config properties
   * for e.g. version 6 are not compatible with version 7, so we simply skip
   * them and assume the gateway will re-initialize the correct settings via
   * serial API on startup)
   */
  if (app_nvm_is_pre_v7_15_3(major_app_version, minor_app_version, patch_app_version))
  {
    SApplicationConfiguration_prior_7_15_3 ac = {};
    ac.rfRegion = json_get_int(jo_app_config, "rfRegion", 0, JSON_OPTIONAL);
    ac.iTxPower = json_get_int(jo_app_config, "txPower", 0,  JSON_OPTIONAL);
    ac.ipower0dbmMeasured = json_get_int(jo_app_config, "power0dbmMeasured", 0, JSON_OPTIONAL);
    WRITE_APP_NVM(FILE_ID_APPLICATIONCONFIGURATION, ac);
  } else if (app_nvm_is_pre_v7_18_1(major_app_version, minor_app_version, patch_app_version)) {
    SApplicationConfiguration_prior_7_18_1 ac = {};
    ac.rfRegion = json_get_int(jo_app_config, "rfRegion", 0, JSON_OPTIONAL);
    ac.iTxPower = json_get_int(jo_app_config, "txPower", 0,  JSON_OPTIONAL);
    ac.ipower0dbmMeasured = json_get_int(jo_app_config, "power0dbmMeasured", 0, JSON_OPTIONAL);
    ac.enablePTI = json_get_int(jo_app_config, "enablePTI", 0, JSON_OPTIONAL);
    ac.maxTxPower = json_get_int(jo_app_config, "maxTxPower",140 , JSON_OPTIONAL);
    WRITE_APP_NVM(FILE_ID_APPLICATIONCONFIGURATION, ac);
  } else {
    SApplicationConfiguration ac = {};
    ac.rfRegion = json_get_int(jo_app_config, "rfRegion", 0, JSON_OPTIONAL);
    ac.iTxPower = json_get_int(jo_app_config, "txPower", 0,  JSON_OPTIONAL);
    ac.ipower0dbmMeasured = json_get_int(jo_app_config, "power0dbmMeasured", 0, JSON_OPTIONAL);
    ac.enablePTI = json_get_int(jo_app_config, "enablePTI", 0, JSON_OPTIONAL);
    ac.maxTxPower = json_get_int(jo_app_config, "maxTxPower",140 , JSON_OPTIONAL);
    WRITE_APP_NVM(FILE_ID_APPLICATIONCONFIGURATION, ac);
  }

  return true;
}

/*****************************************************************************/
static uint32_t file_version(uint8_t format,uint8_t major, uint8_t minor, uint8_t patch)
{
  uint32_t version_host = (format) << 24 | (major << 16) | (minor << 8) | patch;
  // 700 series is little endian, so that's what we're storing to the nvm3 file
  return htole32(version_host);
}

/*****************************************************************************/
static bool create_nvm3_version_files(void)
{
  /* We're not really parsing the JSOON file descriptor section here since
   * it contains versions information related to the source device.
   * We're instead generating version files for NVM that contains the
   * series 700 versions this converter is built with.
   * 
   * NB: It is assumed the JSON file descriptor is validated with
   *     is_json_file_supported()
   */

  uint32_t prot_version = file_version(target_protocol_version.format,target_protocol_version.major,target_protocol_version.minor,target_protocol_version.patch);   
  uint32_t app_version = file_version(0,APP_VERSION, APP_REVISION, APP_PATCH);
  WRITE_PROT_NVM(FILE_ID_ZW_VERSION, prot_version);
  WRITE_APP_NVM(ZAF_FILE_ID_APP_VERSION, app_version);

  return true;
}

/*****************************************************************************/
static bool is_json_file_supported(json_object *jo_filedesc,
                                   uint8_t *major_prot_version,
                                   uint8_t *major_app_version,
                                   uint8_t *minor_app_version,
                                   uint8_t *patch_app_verion)
{
  int32_t json_file_version = json_get_int(jo_filedesc, "backupFormatVersion", 0, JSON_REQUIRED);

  if (1 != json_file_version)
  {
    user_message(MSG_ERROR, "ERROR: Unsupported backupFormatVersion: %d. Must be 1.\n", json_file_version);
    return false;
  }

  const char *prot_ver = json_get_string(jo_filedesc, "sourceProtocolVersion", "00.00.00", JSON_REQUIRED);
  unsigned int major;
  unsigned int minor;
  unsigned int patch;

  if (sscanf(prot_ver, "%u.%u.%u", &major, &minor, &patch) != 3)
  {
    user_message(MSG_ERROR, "ERROR: Incorrectly formatted sourceProtocolVersion: \"%s\". Must be like \"dd.dd.dd\" (d:0-9).\n", prot_ver);
    return false;
  }
  *major_prot_version = major;

  const char *app_ver = json_get_string(jo_filedesc, "sourceAppVersion", "00.00.00", JSON_REQUIRED);

  if (sscanf(app_ver, "%u.%u.%u", &major, &minor, &patch) != 3)
  {
    user_message(MSG_ERROR, "ERROR: Incorrectly formatted sourceAppVersion: \"%s\". Must be like \"dd.dd.dd\" (d:0-9).\n");
    return false;
  }
  *major_app_version = major;
  *minor_app_version = minor;
  *patch_app_verion = patch;

  /* NB: We're not validating the actual versions of the protocol and
   *  application sources. Currently it does not make any difference. */

  // We made it all the way to the end without errors :-)
  return true;
}

/*****************************************************************************/
static bool parse_controller_nvm711_json(json_object *jo_ctrl)
{
  SControllerInfo_NVM711 ci = {};
  SApplicationCmdClassInfo ai = {};
  SApplicationData ad = {};

  ci.NodeID                  = json_get_int(jo_ctrl,  "nodeId", 0, JSON_REQUIRED);
  ci.StaticControllerNodeId  = json_get_int(jo_ctrl,  "staticControllerNodeId", 0, JSON_OPTIONAL);
  ci.ControllerConfiguration = json_get_int(jo_ctrl,  "controllerConfiguration", 0, JSON_REQUIRED);
  // SucAwarenessPushNeeded is not used anywhere in the protocol source code
  // ci.SucAwarenessPushNeeded  = json_get_bool(jo_ctrl, "sucAwarenessPushNeeded", false, JSON_OPTIONAL);
  // We don't write ReservedId to the JSON file, so no need to try reading it back
  // ci.ReservedId              = json_get_int(jo_ctrl,  "reservedId", 0, JSON_OPTIONAL);
  ci.SystemState             = json_get_int(jo_ctrl,  "systemState", 0, JSON_OPTIONAL);  // SmartStart state defaults to "idle" (0)
  ai.UnSecureIncludedCCLen   = json_get_bytearray(jo_ctrl, "cmdClassList", ai.UnSecureIncludedCC, APPL_NODEPARM_MAX, JSON_REQUIRED);

  if (ci.ControllerConfiguration & CONTROLLER_ON_OTHER_NETWORK)
  {
    /* The controller has been included into another network by another controller.
     * learnedHomeId is the home id of the joined network and hence the "effective"
     * home id of the controller
     */
    json_get_home_id(jo_ctrl, "learnedHomeId", ci.HomeID, 0, JSON_REQUIRED);

    /* In this scenario the node id cannot be zero */
    if (0 == ci.NodeID)
    {
      user_message(MSG_ERROR, "ERROR: nodeId of controller is zero while controllerConfiguration "
                              "has flag CONTROLLER_ON_OTHER_NETWORK (0x02) set.\n");
      return false;
    }
  }
  else
  {
    /* This is the primary controller. We can safely set the node id to 1 */
    json_get_home_id(jo_ctrl, "ownHomeId", ci.HomeID, 0, JSON_REQUIRED);
    ci.NodeID = 1;
  }
  json_object* jo_node_table = NULL;
  if (json_get_object_error_check(jo_ctrl, "nodeTable", &jo_node_table, json_type_array, JSON_REQUIRED))
  {
    uint16_t tmp;
    if (false == parse_node_table_json(jo_node_table, ci.NodeID, &ci.MaxNodeId, &tmp))
    {
      /* Here we simply record that the JSON is bad and keep going to reveal
       * any oyther errors. The final status will be "failed" due to this.
       */
      set_json_parse_error_flag(true);
    }
  }
  ci.LastUsedNodeId = json_get_int(jo_ctrl, "lastUsedNodeId", ci.MaxNodeId, JSON_OPTIONAL);

  json_object* jo_suc_state = NULL;
  if (json_get_object_error_check(jo_ctrl, "sucState", &jo_suc_state, json_type_object, JSON_REQUIRED))
  {
    parse_suc_state_json(jo_suc_state, &ci.SucLastIndex);
  }

  const char * b64_app_data_string = json_get_string(jo_ctrl, "applicationData", "", JSON_OPTIONAL);

  if (strlen(b64_app_data_string) > 0)
  {
    size_t app_data_len = 0;
    uint8_t *app_data_bin = base64_decode(b64_app_data_string,
                                          strlen(b64_app_data_string),
                                          &app_data_len);

    if (NULL == app_data_bin)
    {
      user_message(MSG_ERROR, "ERROR: Could not base64 decode \"applicationData\".\n");
      return false;
    }

    size_t bytes_to_copy = app_data_len;

    if (bytes_to_copy > APPL_DATA_FILE_SIZE)
    {
      // Only generate the warning if we are throwing away non-zero data
      size_t non_null_bytes_to_copy = bytes_to_copy;
      // Search backwards for non-zero values
      while (non_null_bytes_to_copy > 0 && app_data_bin[non_null_bytes_to_copy - 1] == 0)
      {
        --non_null_bytes_to_copy;
      }

      if (non_null_bytes_to_copy > APPL_DATA_FILE_SIZE)
      {
        user_message(MSG_WARNING, "WARNING: \"applicationData\" will be truncated. "
                                "Bytes with non-zero values: %zu. Max application data size in generated NVM image: %zu.\n",
                                non_null_bytes_to_copy,
                                APPL_DATA_FILE_SIZE);

      }

      bytes_to_copy = APPL_DATA_FILE_SIZE;
    }

    memcpy(ad.extNvm, app_data_bin, bytes_to_copy);
    free(app_data_bin);
  }

  WRITE_PROT_NVM(FILE_ID_CONTROLLERINFO, ci);
  WRITE_APP_NVM(FILE_ID_APPLICATIONCMDINFO, ai);
  WRITE_APP_NVM(FILE_ID_APPLICATIONDATA, ad);

  return true;
}

static bool parse_controller_nvm715_json(json_object *jo_ctrl)
{
  SControllerInfo_NVM715 ci = {};
  SApplicationCmdClassInfo ai = {};
  SApplicationData ad = {};

  ci.NodeID                  = json_get_int(jo_ctrl,  "nodeId", 0, JSON_REQUIRED);
  ci.StaticControllerNodeId  = json_get_int(jo_ctrl,  "staticControllerNodeId", 0, JSON_OPTIONAL);
  ci.ControllerConfiguration = json_get_int(jo_ctrl,  "controllerConfiguration", 0, JSON_REQUIRED);
  // SucAwarenessPushNeeded is not used anywhere in the protocol source code
  // ci.SucAwarenessPushNeeded  = json_get_bool(jo_ctrl, "sucAwarenessPushNeeded", false, JSON_OPTIONAL);
  // We don't write ReservedId to the JSON file, so no need to try reading it back
  // ci.ReservedId              = json_get_int(jo_ctrl,  "reservedId", 0, JSON_OPTIONAL);
  ci.SystemState             = json_get_int(jo_ctrl,  "systemState", 0, JSON_OPTIONAL);  // SmartStart state defaults to "idle" (0)
  ai.UnSecureIncludedCCLen   = json_get_bytearray(jo_ctrl, "cmdClassList", ai.UnSecureIncludedCC, APPL_NODEPARM_MAX, JSON_REQUIRED);

  if (ci.ControllerConfiguration & CONTROLLER_ON_OTHER_NETWORK)
  {
    /* The controller has been included into another network by another controller.
     * learnedHomeId is the home id of the joined network and hence the "effective"
     * home id of the controller
     */
    json_get_home_id(jo_ctrl, "learnedHomeId", ci.HomeID, 0, JSON_REQUIRED);

    /* In this scenario the node id cannot be zero */
    if (0 == ci.NodeID)
    {
      user_message(MSG_ERROR, "ERROR: nodeId of controller is zero while controllerConfiguration "
                              "has flag CONTROLLER_ON_OTHER_NETWORK (0x02) set.\n");
      return false;
    }
  }
  else
  {
    /* This is the primary controller. We can safely set the node id to 1 */
    json_get_home_id(jo_ctrl, "ownHomeId", ci.HomeID, 0, JSON_REQUIRED);
    ci.NodeID = 1;
  }

  json_object* jo_node_table = NULL;
  if (json_get_object_error_check(jo_ctrl, "nodeTable", &jo_node_table, json_type_array, JSON_REQUIRED))
  {
    if (false == parse_node_table_json(jo_node_table, ci.NodeID, &ci.MaxNodeId, &ci.MaxNodeId_LR))
    {
      /* Here we simply record that the JSON is bad and keep going to reveal
       * any oyther errors. The final status will be "failed" due to this.
       */
      set_json_parse_error_flag(true);
    }
  }
  ci.LastUsedNodeId = json_get_int(jo_ctrl, "lastUsedNodeId", ci.MaxNodeId, JSON_OPTIONAL);
  ci.PrimaryLongRangeChannelId = json_get_int(jo_ctrl, "primaryLongRangeChannelId", 0, JSON_OPTIONAL);
  ci.DcdcConfig = json_get_int(jo_ctrl, "dcdcConfig", 0, JSON_OPTIONAL);
  ci.LastUsedNodeId_LR = json_get_int(jo_ctrl, "lastUsedNodeIdLR", ci.MaxNodeId_LR, JSON_OPTIONAL);

  json_object* jo_suc_state = NULL;
  if (json_get_object_error_check(jo_ctrl, "sucState", &jo_suc_state, json_type_object, JSON_REQUIRED))
  {
    parse_suc_state_json(jo_suc_state, &ci.SucLastIndex);
  }

  const char * b64_app_data_string = json_get_string(jo_ctrl, "applicationData", "", JSON_OPTIONAL);

  if (strlen(b64_app_data_string) > 0)
  {
    size_t app_data_len = 0;
    uint8_t *app_data_bin = base64_decode(b64_app_data_string,
                                          strlen(b64_app_data_string),
                                          &app_data_len);

    if (NULL == app_data_bin)
    {
      user_message(MSG_ERROR, "ERROR: Could not base64 decode \"applicationData\".\n");
      return false;
    }

    size_t bytes_to_copy = app_data_len;

    if (bytes_to_copy > APPL_DATA_FILE_SIZE)
    {
      // Only generate the warning if we are throwing away non-zero data
      size_t non_null_bytes_to_copy = bytes_to_copy;
      // Search backwards for non-zero values
      while (non_null_bytes_to_copy > 0 && app_data_bin[non_null_bytes_to_copy - 1] == 0)
      {
        --non_null_bytes_to_copy;
      }

      if (non_null_bytes_to_copy > APPL_DATA_FILE_SIZE)
      {
        user_message(MSG_WARNING, "WARNING: \"applicationData\" will be truncated. "
                                "Bytes with non-zero values: %zu. Max application data size in generated NVM image: %zu.\n",
                                non_null_bytes_to_copy,
                                APPL_DATA_FILE_SIZE);

      }

      bytes_to_copy = APPL_DATA_FILE_SIZE;
    }

    memcpy(ad.extNvm, app_data_bin, bytes_to_copy);
    free(app_data_bin);
  }

  WRITE_PROT_NVM(FILE_ID_CONTROLLERINFO, ci);
  WRITE_APP_NVM(FILE_ID_APPLICATIONCMDINFO, ai);
  WRITE_APP_NVM(FILE_ID_APPLICATIONDATA, ad);

  return true;
}

/*****************************************************************************/
bool controller_parse_json(json_object *jo, uint32_t protocol_version)
{
  json_object* jo_ref = NULL;
  uint8_t major_prot_version = 0;
  uint8_t major_app_version = 0;
  uint8_t minor_app_version = 0;
  uint8_t patch_app_version = 0;

  set_json_parse_error_flag(false);
  reset_nvm3_error_status();

  /* We need this to provide detailed error messages */
  json_register_root(jo);

  if (false == json_get_object_error_check(jo, "backupInfo", &jo_ref, json_type_object, JSON_REQUIRED))
  {
    return false;
  }

  if(false == set_target_version( protocol_version) ) {
    return false;
  }

  if (false == is_json_file_supported(jo_ref, &major_prot_version, &major_app_version, &minor_app_version, &patch_app_version))
  {
    return false;
  }

  if (false == create_nvm3_version_files())
  {
    return false;
  }

  if (false == json_get_object_error_check(jo, "zwController", &jo_ref, json_type_object, JSON_REQUIRED))
  {
    return false;
  }

  if (protocol_version >= 715)
  {
    if (false == parse_controller_nvm715_json(jo_ref))
    {
      return false;
    }
  }
  else
  {
    if (false == parse_controller_nvm711_json(jo_ref))
    {
      return false;
    }
  }

  if (true == json_get_object_error_check(jo, "appConfig", &jo_ref, json_type_object, JSON_OPTIONAL))
  {
    parse_app_config_json(jo_ref, major_app_version, minor_app_version, patch_app_version);
  }

  if (was_nvm3_error_detected() || json_parse_error_detected())
  {
    return false;
  }

  return true;
}
