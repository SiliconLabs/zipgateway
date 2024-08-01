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

#include <stdio.h>
#include <string.h>
#include <libxml/parser.h>
#include <assert.h>

#include "parse_xml.h"
#include "libzw_log.h"

#define APPEND 1
#define DONT_APPEND 0

xmlNodePtr root;

struct cmd {
  xmlNodePtr n;
  struct cmd *next;
};
struct cc {
  xmlNodePtr n;
  struct cmd *m;
  struct cc *next;
};

enum show_flags {
  HEX,
  CMD_CLASS,
  DEC
};
struct cc cc_table[255] = {{0}};

enum param_type {
  BYTE,
  CONST,
  ENUM,
  ENUM_ARRAY,
  WORD,
  DWORD,
  MULTI_ARRAY,
  ARRAY,
  BITMASK,
  VARIANT,
  STRUCT_BYTE,
  BIT_24,
  MARKER,
};

struct marker {
  uint8_t len;
  uint8_t val[2];
};

static int snprintf_append(char *buf, size_t buf_size, const char *fmt, ...)
{
  int ret = 0;
  if (buf) {
    size_t len = strlen(buf);

    va_list args;
    va_start(args, fmt);
    ret = vsnprintf(buf + len, buf_size - len, fmt, args);
    va_end(args);
  }
  return ret;
}

int name_matches(xmlNodePtr node, const char *str) {
  // printf("Matching name %s with %s\n", node->name, str);
  if (xmlStrcmp((node->name), (const xmlChar *)str) == 0) {
    return 1;
  }
  return 0;
}

/* If property with _prop_name_ of _node_ matches the str string passed
return 1
else
return 0
*/
int prop_matches(xmlNodePtr node, const char *prop_name, const char *str) {
  xmlChar *prop;
  int ret = 0;

  prop = xmlGetProp(node, (const xmlChar *)prop_name);
  //    printf("Matching property %s matched to %s\n", prop, str);
  if (xmlStrcmp(prop, (const xmlChar *)str) == 0) {
    LOG_TRACE("The property %s matched to the string %s\n", prop, str);
    ret = 1;
  }
  xmlFree(prop);
  return ret;
}

enum param_type get_param_type(xmlNodePtr param) {
  xmlChar *type;
  int ret = BYTE;

  type = xmlGetProp(param, (const xmlChar *)"type");
  if (xmlStrcmp(type, (const xmlChar *)"BYTE") == 0) {
    ret = BYTE;
  } else if (xmlStrcmp(type, (const xmlChar *)"CONST") == 0) {
    ret = CONST;
  } else if (xmlStrcmp(type, (const xmlChar *)"STRUCT_BYTE") == 0) {
    ret = STRUCT_BYTE;
  } else if (xmlStrcmp(type, (const xmlChar *)"ENUM") == 0) {
    ret = ENUM;
  } else if (xmlStrcmp(type, (const xmlChar *)"WORD") == 0) {
    ret = WORD;
  } else if (xmlStrcmp(type, (const xmlChar *)"DWORD") == 0) {
    ret = DWORD;
  } else if (xmlStrcmp(type, (const xmlChar *)"BITMASK") == 0) {
    ret = BITMASK;
  } else if (xmlStrcmp(type, (const xmlChar *)"ENUM_ARRAY") == 0) {
    ret = ENUM_ARRAY;
  } else if (xmlStrcmp(type, (const xmlChar *)"MULTI_ARRAY") == 0) {
    ret = MULTI_ARRAY;
  } else if (xmlStrcmp(type, (const xmlChar *)"ARRAY") == 0) {
    ret = ARRAY;
  } else if (xmlStrcmp(type, (const xmlChar *)"VARIANT") == 0) {
    ret = VARIANT;
  } else if (xmlStrcmp(type, (const xmlChar *)"BIT_24") == 0) {
    ret = BIT_24;
  } else if (xmlStrcmp(type, (const xmlChar *)"MARKER") == 0) {
    ret = MARKER;
  }
  xmlFree(type);
  return ret;
}

/* cur2: node to take property from
   output: array of strings where output will be written
   pre: Add prefix for the property (grooming)
   prop_name: property name for libxml API
   i: index of output where property value will be written
   post: postfix for the property value (grooming)
   append: should the string be written as append or new line */
void print_prop_to_strings(xmlNodePtr cur2,
                           char output[][MAX_LEN_CMD_CLASS_NAME],
                           const char *pre, const char *prop_name, int i,
                           const char *post, int append) {
  xmlChar *name;

  name = xmlGetProp(cur2, (const xmlChar *)prop_name);

  if (append) {
    if (prop_matches(cur2->parent, "name", "Status")) {
      if (!xmlStrcmp(name, (const xmlChar *)"NODE_ADD_STATUS_DONE") ||
          !xmlStrcmp(name, (const xmlChar *)"NODE_REMOVE_STATUS_DONE")) {
        snprintf_append(output[i], sizeof(output[0]),
                        "%s\033[32;1m %s \033[0m %s",
                        pre, name, post);
      } else if (!xmlStrcmp(name, (const xmlChar *)"NODE_ADD_STATUS_FAILED") ||
                 !xmlStrcmp(name, (const xmlChar *)"NODE_ADD_STATUS_SECURITY_FAILED") ||
                 !xmlStrcmp(name, (const xmlChar *)"NODE_REMOVE_STATUS_FAILED")) {
        snprintf_append(output[i], sizeof(output[0]),
                        "%s\033[31;1m %s \033[0m %s",
                        pre, name, post);
      } else {
        snprintf_append(output[i], sizeof(output[0]), "%s%s%s", pre, name, post);
      }
    } else {
      snprintf_append(output[i], sizeof(output[0]), "%s%s%s", pre, name, post);
    }
  } else {
    snprintf(output[i], sizeof(output[i]), "%s %s %s", pre, name, post);
  }
  LOG_TRACE("The string read from the xml node property %d is %s\n", i, output[i]);
  xmlFree(name);
}
int get_prop_as_hex(xmlNodePtr node, const char *prop_name) {
  int ret = 0;
  xmlChar *prop, *tprop;

  char *ptr;

  if (!xmlHasProp(node, (const xmlChar *)prop_name)) assert(0);

  tprop = prop = xmlGetProp(node, (const xmlChar *)prop_name);
  while (*prop == '0' || *prop == 'x') prop++;

#if 0
    number[0] = prop[2];
    number[1] = prop[3];
    number[2] = '\0';
#endif
  //    printf("Converting %s to hex\n", number);
  ret = (unsigned int)strtol((const char *)prop, &ptr, 16);

  if (ptr == (const char *)prop) {
    LOG_TRACE("The xml key does not correspond to any digits\n");
    ret = 0;
  }

  xmlFree(tprop);
  return ret;
}

void get_marker(xmlNodePtr cur2, struct marker *m) {
  xmlNodePtr node = cur2;
  xmlNodePtr marker_node;
  m->len = 0;
  for (; node; node = node->next) {
    if (xmlNodeIsText(node)) continue;

    if (!prop_matches(node, "type", "MARKER")) continue;

    for (marker_node = node->xmlChildrenNode; marker_node;
         marker_node = marker_node->next) {
      if (xmlNodeIsText(marker_node)) continue;

      m->len++;
      m->val[m->len - 1] = get_prop_as_hex(marker_node, "flagmask");
    }
  }
}

uint8_t num_fixed_size_items(xmlNodePtr *param_i) {
  xmlNodePtr f;
  uint8_t ret = 0;

  for (f = (*param_i)->next; f; f = f->next) {
    if (xmlNodeIsText(f)) continue;

    switch (get_param_type(f)) {
      case BYTE:
        ret++;
        break;
      case MARKER:
      case VARIANT:
        (*param_i) = (*param_i)->next->next;
        break;
      default:
        LOG_WARN("The xml param type is unknown :%d\n", get_param_type(f));
        assert(0);
    }
  }
  return ret;
}

int decode(uint8_t *input, uint8_t no_of_bytes,
           char output[][MAX_LEN_CMD_CLASS_NAME], int *r_no_of_strings) {
  uint8_t paramoffs;
  xmlNodePtr cmd_i, param_i, ptype_i, f, vg = NULL;
  struct cc *cc_i;
  char a_number[5];
  char par_nochild[20];
  int parameter_index[25] = {0};
  int parameter_index_in_variant_group[15]={0};
  uint8_t optionaloffs;
  uint8_t param;
  uint8_t VG_PARENT_REFERENCE_BIT = 0x80;
  int line_no = 0, j = 0;
  int i;
  int index = 0;
  xmlChar *prop;
  uint8_t shifter;
  uint8_t len;
  uint8_t vg_counter = 0;
  uint8_t mask, key;
  uint8_t sizeoffs;
  int16_t multi_array_target_key;
  int valuei;
  uint8_t cmd_mask;
  uint8_t tmp_byte;
  unsigned long long_value;
  struct marker m;
  uint8_t showhex_flag = 0;
  uint8_t marker_matched = 0;
  char your_bytes[MAX_LEN_CMD_CLASS_NAME] = "bytestream:";
  uint8_t zwave_udp = 0;
  uint8_t show_as;

  LOG_DEBUG("no_of_bytes = %d", no_of_bytes);
  for (i = 0; i < no_of_bytes; i++) {
    snprintf_append(your_bytes, sizeof(your_bytes), " %02x", input[i]);
  }

start_zwave:
  if (zwave_udp) {
    index = zwave_udp;
    LOG_TRACE("index = %d", index);
  }
  cc_i = &cc_table[input[index++]];

  if (cc_i != NULL) {
    print_prop_to_strings(cc_i->n, output, "cmd_class: ", (const char *)"name",
                          line_no, "", DONT_APPEND);
    print_prop_to_strings(cc_i->n, output, " v", (const char *)"version",
                          line_no++, "", APPEND);

    for (cmd_i = cc_i->n->xmlChildrenNode; cmd_i; cmd_i = cmd_i->next) {
      if (xmlNodeIsText(cmd_i)) continue;

      if (name_matches(cmd_i, "cmd")) /* find section named cmd */
      {
        /* if <cmd> section specifices "cmd_mask" the byte needs to masked to
           get
            the cmd number */
        /*  Notice that there is no incrementing of index*/
        if (xmlHasProp(cmd_i, (const xmlChar *)"cmd_mask")) {
          cmd_mask = get_prop_as_hex(cmd_i, "cmd_mask");
          if (cmd_mask) {
            sprintf(a_number, "0x%02X", input[index] & cmd_mask);
          }
        } else {
          sprintf(a_number, "0x%02X", input[index]);
        }

        /* Find section matching the cmd number we have in input */
        if (prop_matches(cmd_i, "key", a_number)) {
          index++;
          print_prop_to_strings(cmd_i, output, "cmd: ", (const char *)"name",
                                line_no++, "", DONT_APPEND);

          if (cmd_i->xmlChildrenNode) {
            param_i = cmd_i->xmlChildrenNode;
            /* If xml node does not have any more children.
               Just return whatever strings we have*/
          } else {
            sprintf(output[line_no++], "%s",
                    "There is no <param> field for this <cmd> in XML?");
            continue;
          }

        restart_param:

          for (; param_i && (index < no_of_bytes); param_i = param_i->next) {
            if (xmlNodeIsText(param_i)) continue;

            if (!name_matches(param_i, "param") &&
                !name_matches(param_i, "variant_group")) {
              assert(0);
            }

            key = get_prop_as_hex(param_i, "key");
            /* The numbering of parameter key values start from zero inside a variant group, and
               parameters inside a variant group may refer to other parameter inside or outside the variant_group.
               To handle such cases we are using two index-tables */
            if(vg){
                parameter_index_in_variant_group[key] = index;
            }else{
                parameter_index[key] = index;
            }

            if (name_matches(param_i, "variant_group")) {
              paramoffs = get_prop_as_hex(param_i, "paramOffs");
              if(paramoffs != 255 ){
            	  vg_counter = input[parameter_index[paramoffs]];
                  mask = get_prop_as_hex(param_i, "sizemask");
                  sizeoffs = get_prop_as_hex(param_i, "sizeoffs");
                  vg_counter = (vg_counter & mask) >> sizeoffs;
              }
              else
              {
                 vg_counter = 1; // Fix me please, here we are just assuming the number of variant group elements is 1
              }
              if (vg_counter > 0)
            	  vg_counter -= 1;
              else
                continue;

              vg = param_i;
              param_i = param_i->xmlChildrenNode;
              continue;
            }


            if (!param_i->xmlChildrenNode)
            {
              switch (get_param_type(param_i)){
              case WORD:
            	  sprintf(par_nochild, "0x%02X%02X", input[index], input[index + 1]);
                index += 2;
            	  break;
              case BIT_24:
            	  sprintf(par_nochild, "0x%02X%02X%02X", input[index], input[index + 1], input[index + 2]);
                index += 3;
            	  break;
              case DWORD:
            	  sprintf(par_nochild, "0x%02X%02X%02X%02X", input[index], input[index + 1], input[index + 2], input[index + 3]);
                index += 4;
            	  break;
              case BYTE:
              default:  /* fall through */
            	  sprintf(par_nochild, "0x%02X", input[index]);
                index += 1;
            	  break;
              }
              print_prop_to_strings(param_i, output, "param: ", "name",
                                    line_no++, par_nochild, DONT_APPEND);
              continue;
            }
            print_prop_to_strings(param_i, output, "param: ", "name",
                                  line_no++, " > ", DONT_APPEND);

            if (xmlHasProp(param_i, (const xmlChar *)"cmd_mask")) {
              /* If the param is in the same byte as cmd */
              cmd_mask = get_prop_as_hex(param_i, "cmd_mask");
              if (cmd_mask) input[index] &= cmd_mask;
            }

            /* section checking if the param is optional */
            /*  - optionaloffs is "key" of another "param" in the XML
                who decides if this param is present.
                - optionalmask is mask to find out if the param is
                present */

            if (xmlHasProp(param_i, (const xmlChar *)"optionaloffs")) {
              if (!xmlHasProp(param_i, (const xmlChar *)"optionalmask"))
                assert(0); /* Both of them should be present */

              optionaloffs = get_prop_as_hex(param_i, "optionaloffs");
              mask = get_prop_as_hex(param_i, "optionalmask");

              tmp_byte = input[parameter_index[optionaloffs]];
              if (tmp_byte & mask) {
                LOG_TRACE("-----------Skipping to next param\n");
                continue; /* skip to next parameter */
              }
            }
            switch (get_param_type(param_i)) {
              case BYTE: /* fully done as per spec*/
                LOG_TRACE("---------__BYTE: %X\n", input[index]);
                for (ptype_i = param_i->xmlChildrenNode; ptype_i;
                     ptype_i = ptype_i->next) {
                  if (xmlNodeIsText(ptype_i)) continue;

                  if (!name_matches(ptype_i, "bitflag")) assert(0);

                   valuei = get_prop_as_hex(ptype_i, "flagmask");
                   if (input[index] == valuei) {
                       print_prop_to_strings(ptype_i, output, "\t", "flagname",
                                              line_no, "        ", APPEND);
                      }
                  }
                index++;
                line_no++;
                break;
              case CONST:
                LOG_TRACE("------------CONST:%x\n", input[index]);
                for (ptype_i = param_i->xmlChildrenNode; ptype_i;
                     ptype_i = ptype_i->next) {
                  if (xmlNodeIsText(ptype_i)) continue;

                  mask = get_prop_as_hex(ptype_i, "flagmask");
                  if (input[index] == mask) {
                    print_prop_to_strings(ptype_i, output, "\t", "flagname",
                                          line_no, "", APPEND);
                  }
                }
                index++;
                line_no++;
                break;
              case ENUM_ARRAY:
                LOG_TRACE("------------ENUM_ARRAY\n");
                /* Length is determined by length of packet */
                len = no_of_bytes - index;
                for (j = 0; j < len; j++) {
                  for (ptype_i = param_i->xmlChildrenNode; ptype_i;
                       ptype_i = ptype_i->next) {
                    if (xmlNodeIsText(ptype_i)) continue;

                    key = get_prop_as_hex(ptype_i, "key");
                    if (key == input[index]) {
                      print_prop_to_strings(ptype_i, output, "\t", "name",
                                            line_no++, "", DONT_APPEND);
                    }
                  }
                  index++;
                }
                break;
              case ENUM:
                LOG_TRACE("------------ENUM:%x\n", input[index]);
                for (ptype_i = param_i->xmlChildrenNode; ptype_i;
                     ptype_i = ptype_i->next) {
                  if (xmlNodeIsText(ptype_i)) continue;

                  key = get_prop_as_hex(ptype_i, "key");
                  if (key == input[index]) {
                    print_prop_to_strings(ptype_i, output, "\t", "name",
                                          line_no, " ", APPEND);
                  }
                }
                line_no++;
                break;
              case WORD: /* fully done as per spec*/
                for (ptype_i = param_i->xmlChildrenNode; ptype_i;
                     ptype_i = ptype_i->next) {
                  if (xmlNodeIsText(ptype_i)) continue;

                  /* Converting two bytes into integer*/
                  valuei = input[index++] << 8;
                  valuei = valuei + input[index++];

                  if (prop_matches(ptype_i, "showhex", "true")) {
                    snprintf_append(output[line_no], sizeof(output[0]),
                                    " %04x", valuei);
                  } else {
                    snprintf_append(output[line_no], sizeof(output[0]),
                                    " %05d", valuei);
                  }
                }
                LOG_TRACE("The reading WORD param type string %d is %s\n", line_no, output[line_no]);
                line_no++;
                break;
              case DWORD: /* fully done as per spec*/
                for (ptype_i = param_i->xmlChildrenNode; ptype_i;
                     ptype_i = ptype_i->next) {
                  if (xmlNodeIsText(ptype_i)) continue;

                  /* Converting four bytes into integer*/
                  long_value = input[index++] << 24;
                  long_value = long_value + (input[index++] << 16);
                  long_value = long_value + (input[index++] << 8);
                  long_value = long_value + input[index++];

                  if (prop_matches(param_i->xmlChildrenNode, "showhex", "true"))
                    snprintf_append(output[line_no], sizeof(output[0]),
                                    " %08lx", long_value);
                  else
                    snprintf_append(output[line_no], sizeof(output[0]),
                                    " %010lu", long_value);
                }
                LOG_TRACE("The reading DWORD param type string %d is %s\n", line_no, output[line_no]);
                line_no++;
                break;
              case BIT_24: /* fully done as per spec*/
                for (ptype_i = param_i->xmlChildrenNode; ptype_i;
                     ptype_i = ptype_i->next) {
                  if (xmlNodeIsText(ptype_i)) continue;

                  /* Converting three bytes into integer*/
                  long_value = input[index++] << 16;
                  long_value = long_value + (input[index++] << 8);
                  long_value = long_value + input[index++];

                  if (prop_matches(ptype_i, "showhex", "true"))
                    snprintf_append(output[line_no], sizeof(output[0]),
                                    " %08lx", long_value);
                  else
                    snprintf_append(output[line_no], sizeof(output[0]),
                                    " %010lu", long_value);
                }
                LOG_TRACE("The reading BIT_24 param type string %d is %s\n", line_no, output[line_no]);
                line_no++;
                break;
              case MULTI_ARRAY:
                LOG_TRACE("----------MULTI_ARRAY: Byte: %x\n", input[index]);
                multi_array_target_key = -1;
                for (ptype_i = param_i->xmlChildrenNode; ptype_i;
                     ptype_i = ptype_i->next) {
                  if (xmlNodeIsText(ptype_i)) continue;

                  for (f = ptype_i->xmlChildrenNode; f; f = f->next) {
                    if (xmlNodeIsText(f)) continue;

                    if (name_matches(f, "paramdescloc")) {
                      /*TODO consolidate this into get_prop_as_hex() */
                      prop = xmlGetProp(f, (const xmlChar *)"param");
                      param = atoi((const char *)prop);
                      if(vg){
                          multi_array_target_key = input[parameter_index_in_variant_group[param]];
                      }else{
                          multi_array_target_key = input[parameter_index[param]];
                      }

                    } else if (name_matches(f, "bitflag")) {
                      key = get_prop_as_hex(f, "key");
                      if (multi_array_target_key == key) {
                        mask = get_prop_as_hex(f, "flagmask");
                        if (input[index] == mask) {
                          prop = xmlGetProp(f, (const xmlChar *)"flagname");
                          snprintf_append(output[line_no], sizeof(output[0]),
                                          " %s:", prop);
                          xmlFree(prop);
                        }
                      } else {
                        continue;
                      }
                    }
                  }
                }
                LOG_TRACE("The reading MULTI_ARRAY param type string %d is %s\n", line_no, output[line_no]);
                line_no++;
                break;
              case ARRAY: /* TODO: Partially done as per spec*/
                          /* FIXME: Not handling len==255*/
                for (ptype_i = param_i->xmlChildrenNode; ptype_i;
                     ptype_i = ptype_i->next) {
                  if (xmlNodeIsText(ptype_i)) continue;

                  // as per spec?
                  if (!name_matches(ptype_i, "arrayattrib")) continue;

                  if (!prop_matches(ptype_i, "key", "0x00"))
                    assert(0); /* has to be 0*/

                  prop = xmlGetProp(ptype_i, (const xmlChar *)"len");
                  LOG_TRACE("------------len:%s\n", prop);
                  len = atoi((const char *)prop);
                  LOG_TRACE("------------len:%d\n", len);

                  if (xmlHasProp(ptype_i, (const xmlChar *)"showhex")) {
                    if (prop_matches(ptype_i, "showhex", "true")) {
                      showhex_flag = 1;
                    }
                  }
                  if (len <= 254)  // as per spec
                  {
                    for (j = 0; j < len; j++) {
                      if (showhex_flag) {
                        snprintf_append(output[line_no], sizeof(output[0]),
                                        " %02x", input[index++]);
                      } else {
                        snprintf_append(output[line_no], sizeof(output[0]),
                                        " %03d", input[index++]);
                      }
                    }
                  } else {
                    sprintf(output[line_no],
                            "There was no xml tag"
                            " in our xml file for ARRAY with len==255");
                  }
                }
                LOG_TRACE("The reading ARRAY param type string %d is %s\n", line_no, output[line_no]);
                line_no++;
                break;
              case BITMASK:
                LOG_TRACE("----BITMASK: %x\n", input[index]);
                len = 0;
                for (ptype_i = param_i->xmlChildrenNode; ptype_i;
                     ptype_i = ptype_i->next) {
                  if (xmlNodeIsText(ptype_i)) continue;

                  if (name_matches(ptype_i, "bitmask")) {
                    prop = xmlGetProp(ptype_i, (const xmlChar *)"paramoffs");
                    param = atoi((const char *)prop);
                    xmlFree(prop);
                    if (param == 255) {
                      if (no_of_bytes < index) {
                        assert(0);
                      }
                      if (xmlHasProp(ptype_i, (const xmlChar *)"len")) {
                        prop = xmlGetProp(ptype_i, (const xmlChar *)"len");
                        len = atoi((const char *)prop);
                        xmlFree(prop);
                      } else {
                        len = no_of_bytes - index;
                      }
                    } else {
                    	if (vg && (param & VG_PARENT_REFERENCE_BIT)) {
                    	  len = input[parameter_index_in_variant_group[param & ~VG_PARENT_REFERENCE_BIT]];
                    	} else {
                    	  len = input[parameter_index[param]];
                    	}
                      mask = get_prop_as_hex(ptype_i, "lenmask");
                      if(xmlGetProp(ptype_i, (const xmlChar *)"lenoffs"))
                    	  shifter = get_prop_as_hex(ptype_i, "lenoffs");
                      else
                    	  shifter = 0;
                      len = (len & mask) >> shifter;
                    }
                  }
                }
                LOG_TRACE("----------len: %d\n", len);
                for (j = 0; j < len; j++) {
                  snprintf_append(output[line_no], sizeof(output[0]),
                                  " value: %02x", input[index]);
                  line_no++;
                  for (ptype_i = param_i->xmlChildrenNode; ptype_i;
                       ptype_i = ptype_i->next) {
                    if (xmlNodeIsText(ptype_i)) continue;

                    if (name_matches(ptype_i, "bitflag")) {
                      mask = get_prop_as_hex(ptype_i, "flagmask");
                      mask = 1 << mask;
                      if (mask & input[index]) {
                        print_prop_to_strings(ptype_i, output, " ",
                                              (const char *)"flagname", line_no,
                                              ":", APPEND);
                      }
                      mask = 0;
                    }
                  }
                  index++;
                }
                line_no++;
                break;
              case STRUCT_BYTE:
                LOG_TRACE("----STRUCT_BYTE: %x\n", input[index]);
                for (ptype_i = param_i->xmlChildrenNode; ptype_i;
                     ptype_i = ptype_i->next) {
                  if (xmlNodeIsText(ptype_i)) continue;

                  if (name_matches(ptype_i, "fieldenum")) {
                    print_prop_to_strings(ptype_i, output, "\t",
                                          (const char *)"fieldname", line_no,
                                          "", APPEND);
                    mask = get_prop_as_hex(ptype_i, "fieldmask");
                    prop = xmlGetProp(ptype_i, (const xmlChar *)"shifter");
                    if(prop)
                    	shifter = atoi((const char *)prop);
                    else
                    	shifter = 0;
                    xmlFree(prop);
                    valuei = ((input[index] & mask) >> shifter);
                    f = ptype_i->xmlChildrenNode;
                    j = 0;
                    while (f) {
                      if (xmlNodeIsText(f)) goto skip1;

                      if (j == valuei) {
                        print_prop_to_strings(f, output, "\t",
                                              (const char *)"value", line_no,
                                              ":", APPEND);
                      }
                      j++;
                    skip1:
                      f = f->next;
                    }
                  } else if (name_matches(ptype_i, "bitflag")) {
                    mask = get_prop_as_hex(ptype_i, "flagmask");
                    print_prop_to_strings(ptype_i, output, "\t",
                                          (const char *)"flagname", line_no, "",
                                          DONT_APPEND);
                    if (input[index] & mask) {
                      snprintf_append(output[line_no], sizeof(output[0]), ": true");
                    } else {
                      snprintf_append(output[line_no], sizeof(output[0]), ": false");
                    }
                    line_no++;
                  } else if (name_matches(ptype_i, "bitfield")) {
                    mask = get_prop_as_hex(ptype_i, "fieldmask");
                    prop = xmlGetProp(ptype_i, (const xmlChar *)"shifter");
                    if(prop)
                    	shifter = atoi((const char *)prop);
                    else
                    	shifter = 0;
                    xmlFree(prop);
                    valuei = ((input[index] & mask) >> shifter);
                    print_prop_to_strings(ptype_i, output, "\t",
                                          (const char *)"fieldname", line_no,
                                          ":", APPEND);
                    snprintf_append(output[line_no], sizeof(output[0]),
                                    ": %02x", valuei);
                    line_no++;
                  }
                }
                line_no++;
                index++; /* This was only one byte being handled */
                break;
              case VARIANT:
                prop = xmlGetProp(param_i, (const xmlChar *)"name");
                if (xmlStrstr(prop, (const xmlChar *)"Command Class") ||
                    prop_matches(param_i, "encaptype", "CMD_CLASS_REF")) {
                  show_as = CMD_CLASS;
                }else{
                	show_as = HEX;
                }
                xmlFree(prop);

                if (prop_matches(param_i, "name", "Z-Wave command")) {
                  zwave_udp = index;
                  LOG_TRACE("goto start_zwave");
                  goto start_zwave; /*FIXME this is hack */
                }

                for (ptype_i = param_i->xmlChildrenNode; ptype_i;
                     ptype_i = ptype_i->next) {
                  if (xmlNodeIsText(ptype_i)) continue;

                  prop = xmlGetProp(ptype_i, (const xmlChar *)"paramoffs");
                  LOG_TRACE("len: %s\n", prop);
                  paramoffs =
                      atoi((const char *)prop); /* this is always represented in decimal?*/

                  if (paramoffs <= 254) {
                    /* need to find the param with key = len*/
                	  if (vg && (paramoffs & VG_PARENT_REFERENCE_BIT)) {
                	    len = input[parameter_index_in_variant_group[paramoffs & ~VG_PARENT_REFERENCE_BIT]];
                	  } else {
                	    len = input[parameter_index[paramoffs]];
                	  }
                    mask = get_prop_as_hex(ptype_i, "sizemask");
                    len = (len & mask);

                    if(xmlGetProp(ptype_i, (const xmlChar *)"sizechange"))
                    {
                    int sizechange = get_prop_as_hex(ptype_i, "sizechange");
                    len = len + sizechange;
                    }


                    if (show_as != CMD_CLASS) show_as = HEX;

                    for (j = 0; j < len; j++) {
                      switch (show_as) {
                        case HEX:
                          snprintf_append(output[line_no], sizeof(output[0]),
                                          " 0x%02x", input[index++]);
                          break;
                        case CMD_CLASS:
                          print_prop_to_strings(cc_table[input[index++]].n,
                                                output, "\t",
                                                (const char *)"name", line_no++,
                                                ":", DONT_APPEND);
                          break;
                        case DEC:
                          snprintf_append(output[line_no], sizeof(output[0]),
                                          " %03d", input[index++]);
                          break;
                      }
                      LOG_TRACE("The reading VARIANT param type string %d is %s\n", line_no, output[line_no]);
                    }
                  } else if (paramoffs ==
                             255)  // len depends on the message size or markers
                  {
                    get_marker(param_i, &m);
                    /* calculate size on remaining bytes*/
                    /* THis happened while parsing Zwave UDP command headers
                       saying there
                        is z-wave command included but in reality there was just
                        z-wave udp command byte stream */
                    if (no_of_bytes < index) {
                      assert(0);
                    }
                    len = no_of_bytes - index;
                    if (xmlHasProp(param_i, (const xmlChar *)"encaptype")) {
                        if (show_as != CMD_CLASS) show_as = HEX;
                      }


                    len -= num_fixed_size_items(&param_i);
                    for (j = 0; j < len; j++) {
                      switch (m.len) {
                        case 0:
                          break;
                        case 1:
                          if (input[index] == m.val[0]) {
                            marker_matched = 1;
                          }
                          break;
                        case 2:
                          if ((input[index] == m.val[0]) &&
                              (input[index + 1] == m.val[1])) {
                            marker_matched = 1;
                          }
                          break;
                        default:
                          sprintf(output[line_no++],
                                  "marker len more than 2"
                                  "not implemented ");
                          break;
                      }

                      if (marker_matched) {
                        break;  // break the inner for loop
                      }
                      switch (show_as) {
                        case HEX:
                          snprintf_append(output[line_no], sizeof(output[0]),
                                          " 0x%02x ", input[index++]);
                          break;
                        case CMD_CLASS:
                          print_prop_to_strings(cc_table[input[index++]].n,
                                                output, "\t",
                                                (const char *)"name", line_no++,
                                                ":", DONT_APPEND);
                          break;
                        case DEC:
                          snprintf_append(output[line_no], sizeof(output[0]),
                                          " %03d ", input[index++]);
                          break;
                      }
                      LOG_TRACE("The reading VARIANT param typestring %d is %s\n", line_no, output[line_no]);
                    }
                  }
                }
                line_no++;
                break;
              case MARKER:
                for (ptype_i = param_i->xmlChildrenNode; ptype_i;
                     ptype_i = ptype_i->next) {
                  if (xmlNodeIsText(ptype_i)) continue;

                  if (marker_matched) marker_matched = 0;

                  snprintf_append(output[line_no], sizeof(output[0]),
                                  " %02x ", input[index++]);
                }
                line_no++;
                break;
              default:
                break;
            }
          }
          /* If the Variant Group len is still not 0 match all the <param>
           * inside it*/
          if (vg_counter) {
            vg_counter--;
            param_i =
                vg->xmlChildrenNode; /* restart the variant group matching */
            goto restart_param;
          } else {   /* Variant group is over */
            if (vg) {/* But there are still <param> outside variant groups for
                        e.g. SCHEDULE_SUPPORTED_REPORT */
              param_i = vg->next; /* goto next param */
              vg = NULL; /* so that we dont end up in this condition again */
              if (param_i) goto restart_param;
            }
          }

          LOG_TRACE("no_of_bytes=%d, index=%d, line_no=%d", no_of_bytes, index, line_no);

          if (index < no_of_bytes) {
            snprintf_append(output[line_no],
                            sizeof(output[line_no]),
                            ": More data in bytestream than needed?\n");
            line_no++;
            index = 0;
          }

          if (no_of_bytes < index) {
            snprintf_append(output[line_no],
                            sizeof(output[line_no]),
                            ": truncated bytestream?\n");
            line_no++;
          }

          /* We have matched and decoded a command. Stop iterating cc commands */
          break; 
        }
      }
    }
  }

  sprintf(output[line_no++], "%s", your_bytes);
  *r_no_of_strings = line_no;
  return 1;
}

void help_all_cmd_classes(char strings[][MAX_LEN_CMD_CLASS_NAME],
                          int *no_strings) {
  int i = 0;
  xmlNodePtr cur;
  xmlChar *name;
  xmlChar *version;

  cur = root->xmlChildrenNode;
  for (cur = root->xmlChildrenNode; cur; cur = cur->next) {
    if (xmlNodeIsText(cur)) continue;

    if (name_matches(cur, "cmd_class")) {
      name = xmlGetProp(cur, (const xmlChar *)"name");
      version = xmlGetProp(cur, (const xmlChar *)"version");
      sprintf(strings[i], "%s(v%s)", name, version);
      xmlFree(name);
      xmlFree(version);
      i++;
    }
  }
  *no_strings = i;
  return;
}

uint8_t get_cmd_class_number(const char *cmd_class_name) {
  for (int i = 0; i < 255; i++) {
    if (cc_table[i].n)
      if (prop_matches(cc_table[i].n, "name", cmd_class_name)) return i;
  }
  return 0;
}

#define iterate_all_children(child, parent) \
  for (child = parent->xmlChildrenNode; child; child = child->next)
  
uint8_t get_cmd_number(const char *cmd_class, const char *cmd,
                       uint8_t optional_cmd_class_num) {
  int i;
  xmlNodePtr cmd_i;

  if (optional_cmd_class_num) {
    for (cmd_i = cc_table[optional_cmd_class_num].n->xmlChildrenNode; cmd_i;
         cmd_i = cmd_i->next)
      if (prop_matches(cmd_i, "name", cmd))
        return get_prop_as_hex(cmd_i, "key");

  } else {
    for (i = 0; i < 255; i++) {
      if (cc_table[i].n) {
        if (prop_matches(cc_table[i].n, "name", cmd_class)) {
          iterate_all_children(cmd_i, cc_table[i].n) {
            if (xmlNodeIsText(cmd_i)) continue;

            if (prop_matches(cmd_i, "name", cmd))
              return get_prop_as_hex(cmd_i, "key");
          }
        }
      }
    }
  }
  return 0;
}

int get_cmd_class_name(uint8_t number, char *r_name, uint8_t r_len) {
  xmlNodePtr cur;
  xmlChar *key, *name;
  char a_number[5];

  sprintf(a_number, "0x%x", number);
  cur = root->xmlChildrenNode;
  while (cur) {
    if (xmlStrcmp((cur->name), (const xmlChar *)"cmd") == 0) {
      key = xmlGetProp(cur, (const xmlChar *)"key");
      if (xmlStrcmp(key, (const xmlChar *)a_number) == 0) {
        name = xmlGetProp(cur, (const xmlChar *)"name");
        memcpy(r_name, name, strlen((const char *)name));
        r_len = strlen((const char *)name);
        xmlFree(name);
      }
      return 1;
    }
    cur = cur->next;
  }
  return 0;
}

void help_get_cmds_for_class(char output[][MAX_LEN_CMD_CLASS_NAME],
                             int *line_no, const char *class_name) {
  xmlNodePtr cur, cmd;

  cur = root->xmlChildrenNode;
  for (cur = root->xmlChildrenNode; cur; cur = cur->next) {
    if (xmlNodeIsText(cur)) continue;

    if (name_matches(cur, "cmd_class") &&
        prop_matches(cur, "name", class_name)) {
      *line_no = 0;
      for (cmd = cur->xmlChildrenNode; cmd; cmd = cmd->next) {
        if (xmlNodeIsText(cmd)) continue;

        print_prop_to_strings(cmd, output, " ", (const char *)"name",
                              (*line_no)++, " ", DONT_APPEND);
      }
    }
  }
}

void generate_table() {

  xmlNodePtr cur;
  uint8_t slot;
  /* For some reason gcc emits -Wunused-but-set-variable on "prop" (strange
   * since it's used by printf). Tell it to stop that warning*/
  xmlChar *prop ATTR_UNUSED;

  memset(cc_table, 0, sizeof(cc_table));

  for (cur = root->xmlChildrenNode; cur; cur = cur->next) {
    if (xmlNodeIsText(cur)) {
      continue;
    }

    if (name_matches(cur, "cmd_class")) /* Look for sections with name cmd_class*/
    {
      slot = get_prop_as_hex(cur, "key");
      cc_table[slot].n = cur;
      cc_table[slot].next = NULL;
      prop = xmlGetProp(cur, (const xmlChar *)"name");
      LOG_TRACE("--------class: %s\n", prop);
    }
  }
}

xmlDocPtr doc;
int initialize_xml(const char *xml_filename) {

  doc = xmlParseFile(xml_filename);
  //    doc = xmlReadDoc(NULL, xml_filename, XML_PARSE_NOBLANKS):
  if (!doc) {
    LOG_WARN("The xml document not parsed successfully. \n");
    return 0;
  }

  root = xmlDocGetRootElement(doc);
  if (!root) {
    LOG_DEBUG("The xml file is empty? \n");
    return 0;
  }
  if (xmlStrcmp(root->name, (const xmlChar *)"zw_classes")) {
    fprintf(stderr, "document of the wrong type?");
    xmlFreeDoc(doc);
    return 0;
  }
  generate_table();
  return 1;
}

void deinitialize_xml() {
  /*free the document */
  if (doc) xmlFreeDoc(doc);
}
