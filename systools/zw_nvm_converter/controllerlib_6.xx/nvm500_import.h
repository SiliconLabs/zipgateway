/* © 2019 Silicon Laboratories Inc. */
#if !defined(NVM500_IMPORT_H)
#define NVM500_IMPORT_H
#include<json.h>
#include<stdbool.h>
#include<stdlib.h>

size_t nvmlib6xx_json_to_nvm(json_object *jo, uint8_t **nvm_buf_ptr, size_t *nvm_size);

#endif // NVM500_IMPORT_H

