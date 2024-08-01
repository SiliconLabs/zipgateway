/*
 * Copyright 2020 Silicon Laboratories Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "zw_cmd_tool.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "zresource.h"

/* The next two arrays are generated at build time by generate_c_decoder.py. The
 * (generated) definitions are in zw_gen_hdr.c
 */
extern struct zw_command_class* zw_cmd_classes[];
extern struct zw_gen_device_class* zw_dev_classes[];


// Currently there are around 130 unique command class names defined.
// 200 should be safe for some time.
#define CC_NAME_MAP_SIZE 200
static cc_name_map_t cc_name_map[CC_NAME_MAP_SIZE];

int zw_cmd_tool_get_device_class_by_id(int gen_class_id,  // Generic class id
                                       int spec_class_id, // Specific class id
                                       const char **gen_class_name,
                                       const char **spec_class_name)
{
  for (int i = 0; zw_dev_classes[i] != NULL; i++) {
    if (zw_dev_classes[i]->gen_dev_class_number == gen_class_id) {
      const struct zw_gen_device_class  *gd = zw_dev_classes[i];
      const struct zw_spec_device_class *sd = NULL;

      /* A spec_class_id of zero indicates that the caller don't want to look up
       * specific device class.
       */
      if (spec_class_id >= 0) {
        for (int j = 0; gd->spec_devices[j] != NULL; j++) {
          if (gd->spec_devices[j]->spec_dev_class_number == spec_class_id) {
            sd = gd->spec_devices[j];
            break;
          }
        }
      }

      if (gen_class_name) {
        *gen_class_name =  gd->name;
      }

      if (sd && spec_class_name) {
        *spec_class_name = sd->name;
      }
      return 1;
    }
  }
  return 0;
}

void zw_cmd_tool_fill_cc_name_map(void) {
  struct zw_command_class** c;
  size_t n = 0;
  for (c = zw_cmd_classes; *c != NULL && n < CC_NAME_MAP_SIZE; c++) {
    // Don't add same name more than once (assumes zw_cmd_classes is sorted)
    // NB: Some command class names share the same numerical value!!
    if (n == 0 || strcmp(cc_name_map[n - 1].name, (*c)->name) != 0) {
      cc_name_map[n].name = (*c)->name;
      cc_name_map[n].number = (*c)->cmd_class_number;
      // printf("0x%02X %s\n", cc_map[n].number, cc_map[n].name);
      n++;
    }
  }
}

uint8_t zw_cmd_tool_class_name_to_num(const char *cc_name) {
  if (!cc_name_map[0].name) {
    zw_cmd_tool_fill_cc_name_map();
  }

  for (int i = 0; i < CC_NAME_MAP_SIZE && cc_name_map[i].name; i++) {
    if (strcmp(cc_name, cc_name_map[i].name) == 0) {
      return cc_name_map[i].number;
    }
  }
  return 0;
}

cc_name_map_t * zw_cmd_tool_match_class_name(const char *cc_name_part, int part_len, int *index) {
  if (!cc_name_map[0].name) {
    zw_cmd_tool_fill_cc_name_map();
  }

  cc_name_map_t *found_item = NULL;
  int i = *index;

  // Continue the search from where we left off
  while (i < CC_NAME_MAP_SIZE && cc_name_map[i].name) {
    // Try to match the complete command class name and also the part
    // after COMMAND_CLASS_ (len = 14)
    if (strncmp(cc_name_map[i].name, cc_name_part, part_len) == 0 ||
        (strncmp(cc_name_map[i].name + 14, cc_name_part, part_len) == 0)) {
      found_item = &cc_name_map[i];
      break;
    }
    i++;
  }
  *index = i + 1;
  return found_item;
}

cc_ver_info_t* zw_cmd_tool_get_cc_info_by_name(struct zip_service *service,
                                               const char* cc_name) {
  uint8_t cc_number = zw_cmd_tool_class_name_to_num(cc_name);
  if (cc_number > 0) {
    return zresource_get_cc_info(service, cc_number);
  }
  return NULL;
}

const struct zw_command_class* zw_cmd_tool_get_cmd_class(struct zip_service *service,
                                                         uint8_t ccNumber,
                                                         uint8_t ccVersion) {
  struct zw_command_class** c;
  struct zw_command_class* selected = NULL;
  cc_ver_info_t *cc_info = NULL;

  if (ccNumber == 0) {
    return NULL;
  }

  for (c = zw_cmd_classes; *c; c++) {
    if ((*c)->cmd_class_number == ccNumber) {

      // If a service was provided check if this cc is supported by the service
      if (service && !cc_info) {
        cc_info = zresource_get_cc_info(service, ccNumber);
        if (!cc_info) {
          break; // Not supported, bail out now
        }
      }
      
      if (service && cc_info->version) {
        // A service was provided and it has a version info for the cc.
        // We only want the cc with that version
        if (cc_info->version == (*c)->cmd_class_number_version) {
          selected = *c;
          break;
        }
      } else if (ccVersion) {
        // A specific version was requested
        // (should normally not be used when calling with service != NULL)
        if (ccVersion == (*c)->cmd_class_number_version) {
          selected = *c;
          break; // We have a match, no need to search the rest
        }
      } else {
        // We fall back to providing the highest version - search all command classes
        if (selected == NULL) {
          selected = *c;
        } else {
          if (selected->cmd_class_number_version < (*c)->cmd_class_number_version) {
            selected = *c;
          }
        }
      }
    }
  }
  return selected;
}

size_t zw_cmd_tool_get_cmd_names(const struct zw_command_class *cls, 
                                 const char **names,
                                 size_t len) {
  size_t n = 0;
  if (cls) {
    for (n = 0; n < len && cls->commands[n] != 0; n++) {
      *names++ = cls->commands[n]->name;
    }
  }
  return n;
}

const struct zw_command* zw_cmd_tool_get_cmd_by_name(const struct zw_command_class* cls,
                                                     const char* name) {
  const struct zw_command* const* c;
  if (cls) {
    for (c = cls->commands; *c; c++) {
      if (strcmp(name, (*c)->name) == 0) {
        return *c;
      }
    }
  }
  return 0;
}

size_t zw_cmd_tool_get_param_names(const struct zw_command* cmd,
                                   const char** names,
                                   size_t len) {
  size_t n = 0;
  if (cmd) {
    for (n = 0; n < len && cmd->params[n] != 0; n++) {
      *names++ = cmd->params[n]->name;
    }
  }
  return n;
}

const struct zw_parameter* zw_cmd_tool_get_param_by_name(
    const struct zw_command* cmd, const char* name) {
  const struct zw_parameter* const* c;
  int n = 0;
  for (c = cmd->params; *c; c++) {
    if (strcmp(name, (*c)->name) == 0) {
      return *c;
    }
    n++;
  }
  return 0;
}

static struct zw_param_data* get_paramter_data(
    const struct zw_parameter* param, int index,
    struct zw_param_data* data_list[]) {
  struct zw_param_data** p;

  for (p = data_list; *p; p++) {
    if (((*p)->param == param) && (*p)->index == index) {
      return *p;
    }
  }
  return 0;
}

/**
 * Count traling 0 bits
 */
static int mask_shift(int d) {
  int n;
  n = 0;
  while ((d & 1) == 0) {
    d = d >> 1;
    n++;
  }
  return n;
}

struct param_insert_info {
  struct param_insert_info* next;
  uint8_t* location;
  const struct zw_parameter* param;
  int index;
};

/**
 *  Write parameter into frame data.
 */
static int process_parameter(struct param_insert_info** _pii,
                             uint8_t* frame_start, uint8_t* parameter_start,
                             const struct zw_parameter* param,
                             struct zw_param_data* parameter_data[],
                             int param_index) {
  struct zw_param_data* p_data;
  const struct zw_parameter* const* sub_parameter;
  int length = param->length;

  struct param_insert_info* pii =
      (struct param_insert_info*)malloc(sizeof(struct param_insert_info));

  if (length == 255) {
    length = 0;
  }

  pii->next = *_pii;
  pii->param = param;
  pii->location = parameter_start;
  pii->index = param_index;
  *_pii = pii;

  p_data = get_paramter_data(param, param_index, parameter_data);

  int index_cnt = 0;
  int n, n_sum;
  n_sum = 0;
  do {
    n = 0;
    for (sub_parameter = param->subparams; *sub_parameter; sub_parameter++) {
      n += process_parameter(_pii, frame_start, parameter_start + n_sum + n,
                             *sub_parameter, parameter_data, index_cnt);
    }
    index_cnt++;
    n_sum +=
        n;  // Subparamter did not add extra bytes to the parameter, then exit
  } while (n > 0);
  length += n_sum;

  // printf("Processing %s len %i index %i %p\n",
  // param->name,length,param_index,parameter_start);

  if (p_data) {
    if (param->length == 255) {  // If this is a dynamic length parameter, set
                                 // the length of the parameter
      length = p_data->len;
    }

    /*This is an optional parameter, mark its presence */
    if (param->optionaloffs) {
      for (pii = *_pii; pii; pii = pii->next) {
        if (pii->param == param->optionaloffs) {
          *pii->location |= param->optionalmask;
          break;
        }
      }
      assert(pii);
    }
    // printf("Assigning mask 0x%x %p\n", param->mask,parameter_start -
    // frame_start);

    /*This paramter is the whole byte or more, just add the data */
    if (param->mask == 255) {
      memcpy(parameter_start, p_data->data, length);
    } else {
      uint8_t value = *((uint8_t*)p_data->data);

      *parameter_start &= ~param->mask;
      *parameter_start |= ((value << mask_shift(param->mask)) & param->mask);

      // printf("Masked assign %x %x\n",value,*parameter_start);
    }
  } else {
    if (param_index > 0) {
      return 0;
    }
  }

  if (param->length_location != 0) {  // This parameter has its length value
                                      // written somewhere else in the message
    for (pii = *_pii; pii; pii = pii->next) {
      if (pii->param == param->length_location) {
        *pii->location &= ~param->length_location_mask;
        *pii->location |= (length << mask_shift(param->length_location_mask)) &
                          param->length_location_mask;
        break;
      }
    }
    assert(pii);
  }
  return length;
}

int zw_cmd_tool_create_zw_command(uint8_t* dst, int dst_len,
                                  const char* cmdClass, const char* cmd,
                                  struct zw_param_data* data[]) {
  uint8_t* p;
  struct param_insert_info* pii = 0;
  const struct zw_command* p_cmd;
  const struct zw_command_class* p_class;
  const struct zw_parameter* const* p_paramter;
  uint8_t cc_number = zw_cmd_tool_class_name_to_num(cmdClass);

  memset(dst, 0, dst_len);
  p = dst;

  if (cc_number) {
    p_class = zw_cmd_tool_get_cmd_class(NULL, cc_number, 0);
    p_cmd = zw_cmd_tool_get_cmd_by_name(p_class, cmd);

    *p++ = p_class->cmd_class_number;
    *p++ = p_cmd->cmd_number;

    for (p_paramter = p_cmd->params; *p_paramter; p_paramter++) {
      int n = process_parameter(&pii, dst, p, *p_paramter, data, 0);
      p += n;
    }

    struct param_insert_info* next;
    while (pii) {
      next = pii->next;
      free(pii);
      pii = next;
    }
  }

  return p - dst;
}

void zw_cmd_tool_print_parameter(FILE* f, const struct zw_parameter* p, int indent) {
  const struct zw_parameter* const* sub_parameter;

  for (int i = 0; i < indent; i++) fputc('-', f);

  /*print variable name */
  fprintf(f, " %s = ", p->name);

  /*right shift mask until we have no trailing zeros*/
  int mask = p->mask;
  if (mask) {
    while ((mask & 1) == 0) {
      mask = mask >> 1;
    }
  }

  if (p->length == 255) fprintf(f, "[ ");

  if (p->display == DISPLAY_ENUM || p->display == DISPLAY_ENUM_EXCLUSIVE) {

    if (p->display == DISPLAY_ENUM) {  // This parameter can be an enum as well
                                       // as any other type
      fprintf(f, "0-0x%X", mask);
    }
    fprintf(f, "  ");
    const struct zw_enum* e;
    for (e = p->enums; e->name; e++) {
      fprintf(f, "%s(%i)|", e->name, e->value);
    }
  } else {
    /* ARRAY Types with fixed length */
    if (p->length > 1 && p->length < 255) {
      for (int i = 0; i < p->length; i++) {
        fprintf(f, "00");
      }
    } else if (p->display == DISPLAY_HEX) {
      fprintf(f, "0-0x%X", mask);
    } else if (p->display == DISPLAY_DECIMAL) {
      fprintf(f, "0-%i", mask);
    } else if (p->display == DISPLAY_ASCII) {
      fprintf(f, "\"....\"");
    }
  }

  if (p->length == 255) fprintf(f, ",...]");

  fprintf(f, "\n");

  if (p->display == DISPLAY_STRUCT) {
    for (int i = 0; i < indent + 2; i++) fputc('-', f);
    fprintf(f, " [ \n");

    for (sub_parameter = p->subparams; *sub_parameter; sub_parameter++) {
      zw_cmd_tool_print_parameter(f, *sub_parameter, indent + 2);
    }
    for (int i = 0; i < indent + 2; i++) putchar('-');

    if (p->length > 1) fprintf(f, ", ...");
    fprintf(f, " ]\n");
  }
}
