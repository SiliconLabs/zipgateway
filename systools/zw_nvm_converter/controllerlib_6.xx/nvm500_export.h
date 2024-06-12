/* © 2019 Silicon Laboratories Inc. */
#if !defined(NVM500_EXPORT_H)
#define NVM500_EXPORT_H
#include<stdbool.h>
#include<stdlib.h>
#include<json.h>

bool nvmlib6xx_nvm_to_json(const uint8_t *nvm_image, size_t nvm_image_size, json_object **jo_out);

bool nvmlib6xx_is_nvm_valid(const uint8_t *nvm_image, size_t nvm_image_size) ;
#endif // NVM500_EXPORT_H
