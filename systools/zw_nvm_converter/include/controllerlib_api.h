/* © 2019 Silicon Laboratories Inc. */
#ifndef _CONTROLLERLIB_API_H
#define _CONTROLLERLIB_API_H

#include <stdint.h>
#include <stdbool.h>
#include <json.h>

typedef uint32_t (*nvmlib_init_func_t)         (void);
typedef void     (*nvmlib_term_func_t)         (void);
typedef bool     (*nvmlib_is_nvm_valid_func_t) (const uint8_t *nvm_ptr, size_t nvm_size);
typedef bool     (*nvmlib_nvm_to_json_func_t)  (const uint8_t *nvm_buf, size_t nvm_size, json_object **jo_out);
typedef bool     (*nvmlib_is_json_valid_func_t)(json_object *jo);
typedef size_t   (*nvmlib_json_to_nvm_func_t)  (json_object *jo, uint8_t **nvm_buf_ptr, size_t *nvm_size);

typedef struct {
  const char                 *lib_id;
  const char                 *nvm_desc;
  const char                 *json_desc;
  nvmlib_init_func_t          init;
  nvmlib_term_func_t          term;
  nvmlib_is_nvm_valid_func_t  is_nvm_valid;
  nvmlib_nvm_to_json_func_t   nvm_to_json;
  nvmlib_is_json_valid_func_t is_json_valid;
  nvmlib_json_to_nvm_func_t   json_to_nvm;
} nvmlib_interface_t;

// Known controller libs
extern nvmlib_interface_t controllerlib_800s_718;
extern nvmlib_interface_t controllerlib_800s_717;
extern nvmlib_interface_t controllerlib_700s_718;
extern nvmlib_interface_t controllerlib_700s_717;
extern nvmlib_interface_t controllerlib716;
extern nvmlib_interface_t controllerlib715;
extern nvmlib_interface_t controllerlib712;
extern nvmlib_interface_t controllerlib711;
extern nvmlib_interface_t controllerlib68b;
extern nvmlib_interface_t controllerlib67b;
extern nvmlib_interface_t controllerlib66b;
extern nvmlib_interface_t controllerlib68s;
extern nvmlib_interface_t controllerlib67s;
extern nvmlib_interface_t controllerlib66s;

#endif // _CONTROLLERLIB_API_H
