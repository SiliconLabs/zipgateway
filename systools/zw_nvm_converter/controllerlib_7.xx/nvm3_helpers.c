/* © 2019 Silicon Laboratories Inc. */
#include <stdio.h>
#include <string.h>
#include "nvm3_helpers.h"
#include "user_message.h"

#include "zwave_controller_network_info_storage.h"

static bool nvm3_error_detected = false;

/*****************************************************************************/
void reset_nvm3_error_status(void)
{
  nvm3_error_detected = false;
}

/*****************************************************************************/
bool was_nvm3_error_detected(void)
{
  return nvm3_error_detected;
}

/*****************************************************************************/
bool nvm3_check_files(nvm3_Handle_t *nvm3_handle, const nvm3_file_descriptor_t *filelist, uint32_t filelist_size)
{
  Ecode_t  ec;
  uint32_t type;
  size_t   size;
  const nvm3_file_descriptor_t *fdesc;
  bool all_files_ok_status = true;

  for (uint32_t i = 0; i < filelist_size; i++)
  {
    fdesc = &filelist[i];
    if (fdesc->optional)
    {
      continue;  // Simply skip optional files
    }

    ec = nvm3_getObjectInfo(nvm3_handle, fdesc->key, &type, &size);
    if (ECODE_NVM3_OK != ec)
    {
      user_message(MSG_ERROR, "ERROR nvm3 file %s (0x%x) not found\n", fdesc->name, fdesc->key);
      all_files_ok_status = false;
    }
    else if (NVM3_OBJECTTYPE_DATA != type)
    {
      user_message(MSG_ERROR, "ERROR nvm3 file %s (0x%x) not of type DATA\n", fdesc->name, fdesc->key);
      all_files_ok_status = false;
    }
    else if (fdesc->size != size)
    {
      user_message(MSG_ERROR, "ERROR nvm3 file %s (0x%x) size is %d. The expected size is %d\n", fdesc->name, fdesc->key, size, fdesc->size);
      all_files_ok_status = false;
    }
  }
  return all_files_ok_status;
}

/*****************************************************************************/
static int key_compare(const void *key1, const void *key2)
{
  return *(nvm3_ObjectKey_t*)key1 - *(nvm3_ObjectKey_t*)key2;
}


/*****************************************************************************/
const char * lookup_filename(nvm3_ObjectKey_t key, const nvm3_file_descriptor_t *filelist, uint32_t filelist_size)
{
  static char buf[100];

  buf[0] = '\0';

  for (uint32_t i = 0; i < filelist_size; i++)
  {
    if ((filelist[i].key == key) && (filelist[i].num_keys == 0))
    {
      strncpy(buf, filelist[i].name, sizeof(buf));
      break;
    }
    else if ((filelist[i].key <= key) && (key < (filelist[i].key + filelist[i].num_keys)))
    {
      uint8_t node_id = key - filelist[i].key + 1;
      snprintf(buf, sizeof(buf), "%s (node_id: %d)", filelist[i].name, node_id);
      break;
    }
  }
  buf[sizeof(buf) - 1] = 0;

  return buf;
}

/*****************************************************************************/
void nvm3_dump_keys_with_filename(nvm3_Handle_t *h, const nvm3_file_descriptor_t *filelist, uint32_t filelist_size)
{
  nvm3_ObjectKey_t keys[500];
  
  size_t num_objs  = 0;

  num_objs = nvm3_enumObjects(h, keys,
                              sizeof(keys)/sizeof(keys[0]),
                              NVM3_KEY_MIN,
                              NVM3_KEY_MAX);

  qsort(keys, num_objs, sizeof(keys[0]), key_compare);

  for (int i = 0; i < num_objs; i++) 
  {
    const char * filename = lookup_filename(keys[i], filelist, filelist_size);

    user_message(MSG_DIAG, "0x%06x (%d) %s\n", keys[i], keys[i], filename);
  }
}

bool key_is_NOUDEROUTECACHE_V1(nvm3_ObjectKey_t key)
{
  if (FILE_ID_NODEROUTECAHE_V1 > key)
  {
    return false;
  }
  if ((FILE_ID_NODEROUTECAHE_V1 + NUMBER_OF_NODEROUTECACHE_V1_FILES - 1) < key)
  {
    return false;
  }
  return true;
}

/*****************************************************************************/
Ecode_t nvm3_readData_print_error(nvm3_Handle_t *h,
                                  nvm3_ObjectKey_t key,
                                  void *value,
                                  size_t maxLen,
                                  const nvm3_file_descriptor_t *filelist,
                                  uint32_t filelist_size)
{
  Ecode_t ec = nvm3_readData(h, key, value, maxLen);
  message_severity_t  msg_severity = MSG_DIAG;
  const char         *msg_prefix   = "SUCCESS:";

  if (ECODE_NVM3_ERR_KEY_NOT_FOUND == ec && key_is_NOUDEROUTECACHE_V1(key))
  {
    //Special case for missing NOUDEROUTECACHE_V1 files.
	//Dont set ERROR but WARNING since files are not always saved by the SerialAPIController application.
    msg_severity = MSG_WARNING;
    msg_prefix   = "WARNING:";
  }
  else if (ECODE_NVM3_OK != ec)
  {
    msg_severity = MSG_ERROR;
    msg_prefix   = "ERROR:";
    nvm3_error_detected = true;
  }

  user_message(msg_severity,
               "%s nvm3_readData(key=0x%x/%d, len=%d) returned 0x%x",
               msg_prefix, key, key, maxLen, ec);
  if (filelist && is_message_severity_enabled(msg_severity))
  {
    const char * filename = lookup_filename(key, filelist, filelist_size);
    user_message(msg_severity, " - filename: %s", filename);
  }
  user_message(msg_severity, "\n");

  return ec;
}

/*****************************************************************************/
Ecode_t nvm3_writeData_print_error(nvm3_Handle_t *h,
                                   nvm3_ObjectKey_t key,
                                   const void *value,
                                   size_t len,
                                   const nvm3_file_descriptor_t *filelist,
                                   uint32_t filelist_size)
{
  Ecode_t             ec = nvm3_writeData(h, key, value, len);
  message_severity_t  msg_severity = MSG_DIAG;
  const char         *msg_prefix   = "SUCCESS:";

  if (ECODE_NVM3_OK != ec)
  {
    msg_severity = MSG_ERROR;
    msg_prefix   = "ERROR:";
    nvm3_error_detected = true;
  }

  user_message(msg_severity,
               "%s nvm3_writeData(key=0x%x/%d, len=%d) returned 0x%x",
               msg_prefix, key, key, len, ec);
  if (filelist && is_message_severity_enabled(msg_severity))
  {
    const char * filename = lookup_filename(key, filelist, filelist_size);
    user_message(msg_severity, " - filename: %s", filename);
  }
  user_message(msg_severity, "\n");

  return ec;
}
