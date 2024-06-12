/* © 2019 Silicon Laboratories Inc. */
#ifndef _NVM3_HELPERS_H
#define _NVM3_HELPERS_H

#include <stdbool.h>
#include "nvm3.h"

typedef struct {
  nvm3_ObjectKey_t key;
  uint32_t         num_keys;  // If this nvm3 key is used as base value for multiple objects
  size_t           size;
  const char *     name;
  bool             optional;  // If true it will not be part of NVM validation
} nvm3_file_descriptor_t;

void reset_nvm3_error_status(void);
bool was_nvm3_error_detected(void);

const char * lookup_filename(nvm3_ObjectKey_t key,
                             const nvm3_file_descriptor_t *filelist,
                             uint32_t filelist_size);

bool nvm3_check_files(nvm3_Handle_t *nvm3_handle,
                      const nvm3_file_descriptor_t *filelist,
                      uint32_t filelist_size);

void nvm3_dump_keys_with_filename(nvm3_Handle_t *h,
                                  const nvm3_file_descriptor_t *filelist,
                                  uint32_t filelist_size);

Ecode_t nvm3_readData_print_error(nvm3_Handle_t *h,
                                  nvm3_ObjectKey_t key,
                                  void *value,
                                  size_t maxLen,
                                  const nvm3_file_descriptor_t *filelist,
                                  uint32_t filelist_size);

Ecode_t nvm3_writeData_print_error(nvm3_Handle_t *h,
                                  nvm3_ObjectKey_t key,
                                  const void *value,
                                  size_t len,
                                  const nvm3_file_descriptor_t *filelist,
                                  uint32_t filelist_size);

#endif // _NVM3_HELPERS_H