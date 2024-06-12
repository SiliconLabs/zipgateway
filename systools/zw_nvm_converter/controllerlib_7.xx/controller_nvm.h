/* © 2019 Silicon Laboratories Inc. */
#ifndef _NVMLIB711_H
#define _NVMLIB711_H

#include <stdbool.h>
#include <json.h>

typedef enum {NVM3_700s, NVM3_800s} nvmLayout_t;

bool check_controller_nvm(const uint8_t *nvm_image, size_t nvm_image_size, nvmLayout_t nvm_layout);
bool open_controller_nvm(const uint8_t *nvm_image, size_t nvm_image_size, nvmLayout_t nvm_layout);
void close_controller_nvm(void);

json_object* controller_nvm711_get_json(void);
json_object* controller_nvm715_get_json(void);
bool controller_parse_json(json_object *jo,uint32_t target_version);
size_t get_controller_nvm_image(uint8_t **image_buf);

void dump_controller_nvm_keys(void);

json_object* protocol_nvm_get_json(void);

json_object* app_nvm_get_json(void);

#endif // _NVMLIB711_H
