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

#ifndef XML_ZW_CMD_TOOL_H_
#define XML_ZW_CMD_TOOL_H_

#include <stdint.h>
#include <stdio.h>
#include "zresource.h"

struct zw_enum {
  const char* name;
  int value;
};

struct zw_bitparam {
  int mask;

  const char* name;
  struct zw_enum enums[];
};
struct zw_parameter {
  // int offset; //Byte Offset in package where this parameter is located
  int length;  // Length of this parameter, 0 means dynamic length. In this case
               // length location will be given.

  const struct zw_parameter* length_location;  // Parameter in this package
                                               // where this length is given 0
                                               // mean fixed length
  int length_location_mask;  // Mask of length location

  int mask;  // Mask of this parameter
  enum {
    DISPLAY_DECIMAL,
    DISPLAY_HEX,
    DISPLAY_ASCII,
    DISPLAY_BITMASK,
    DISPLAY_ENUM,
    DISPLAY_ENUM_EXCLUSIVE,
    DISPLAY_STRUCT
  } display;

  const char* name;
  // Optional parameter
  const struct zw_parameter* optionaloffs;  // Parameter of bits indicating if
                                            // this parameter is present
  int optionalmask;  // Mask of precnese bits

  //  struct zw_bitparam* bitparam[];   //Bit parameters
  struct zw_enum* enums;
  const struct zw_parameter* subparams[];
};

struct zw_command {

  int cmd_number;
  int cmd_mask;
  const char* name;
  const char* help;

  const struct zw_parameter* params[];
};

struct zw_command_class {
  int cmd_class_number;
  int cmd_class_number_version;

  const char* name;
  const char* help;
  const struct zw_command* commands[];
};

struct zw_spec_device_class {
  int spec_dev_class_number;

  const char* name;
  const char* help;
  const char* comment;
};

struct zw_gen_device_class {
  int gen_dev_class_number;

  const char* name;
  const char* help;
  const char* comment;
  const struct zw_spec_device_class* spec_devices[];
};

typedef struct {
  const char *name;
  uint8_t number;
} cc_name_map_t;

struct zw_param_data {
  const struct zw_parameter* param;
  const uint8_t* data;
  int len;
  int index;
};

/**
 * Get command class number from command class name
 * 
 * \param cc_name Command class name
 * 
 * \return command class number. 0 if unknown name.
 */
uint8_t zw_cmd_tool_class_name_to_num(const char *cc_name);

/**
 * Lookup generic and specific device class names from class ids
 *
 * \param gen_class_id Generic class id
 *
 * \param spec_class_id Specific class id. If 0, only the generic class name
 *                      will be fetched.
 *
 * \param gen_class_name address of generic class name string pointer. If
 *                       non-NULL it will be modified on return to point to
 *                       const string containing device generic class name.
 *
 * \param spec_class_name address of specific class name string pointer. If
 *                        non-NULL it will be modified on return to point to
 *                        const string containing device specific class name.
 *
 * \return 1 if gen_class_id is known, 0 otherwise,
 */
int zw_cmd_tool_get_device_class_by_id(int gen_class_id,  // Generic class id
                                       int spec_class_id, // Specific class id
                                       const char **gen_class_name,
                                       const char **spec_class_name);

/**
 * Fill array with pointers to all known command class names and their value
 *
 * Iterates the global array variable zw_cmd_classes and generates an internal
 * static array mapping unique command class names to command class values.
 */
void zw_cmd_tool_fill_cc_name_map(void);

/**
 * Lookup command class structure
 *
 * \param service The service containing the list of valid command classes. If
 *                non-NULL then only search command classes supported by the
 *                service. If the service provided knows the version of its
 *                supported command class that version is used to lookup the
 *                command class structure. If not the command class structure
 *                with the highest version is fetched. If service is NULL then
 *                search all existing Z-Wave command classes.
 *
 * \param ccNumber Command class number.
 *
 * \param ccVersion Command class version. Used to specify the command class
 *                    version in case service is NULL. If ccVersion is 0 the
 *                    highest known version command class will be returned.
 *
 * \return Pointer to command class structure. NULL if not found.
 */
const struct zw_command_class* zw_cmd_tool_get_cmd_class(struct zip_service *service,
                                                         uint8_t ccNumber,
                                                         uint8_t ccVersion);

/**
 * Progressively give the next command class name struct in a list
 *
 * Set *index to 0 on first call to return the first matching item in the list.
 * Then continue to call the function with index until NULL is returned.
 *
 * E.g. this will iterate all command classes where name starts with
 * "COMMAND_CLASS_S":
 *
 * @code{.c}
 * int i = 0;
 * while (c = zw_cmd_tool_match_class_name("S", 1, &i)) {
 *   // use c
 * }
 * @endcode
 *
 * \param cc_name_part name (possibly incomplete) of the command class to
 *                     lookup. The name string is matched from the beginning of
 *                     a command class name and also from the point following
 *                     "COMMAND_CLASS_" (i.e "SWITCH" will match
 *                     "COMMAND_CLASS_SWITCH").
 *
 * \param part_len length of string in cc_part_name to use (a length is needed
 *                 since the string could come from the readline completion
 *                 engine where the string is not guaranteed to be
 *                 zero-terminated at the expected location)
 *
 * \param index    pointer to index value. The value must be zero on first call.
 *                 On following calls it must be re-used unmodified by the
 *                 called (the function takes ownership of it until all elements
 *                 in the list has been returned)
 *
 * \return pointer to next item in the list. NULL if no more items.
 */
cc_name_map_t* zw_cmd_tool_match_class_name(const char *cc_name_part,
                                            int part_len,
                                            int *index);

/**
 * Fill array with pointers to all command names for a command class
 * 
 * \param names  Pointer to array of string pointers to fill
 * \param len    Length of names array
 * 
 * \return Number of command names in array
 */
size_t zw_cmd_tool_get_cmd_names(const struct zw_command_class* c, 
                                 const char** names,
                                 size_t len);

/**
 * Retrieve a command structure by its name an the command class
 */
const struct zw_command* zw_cmd_tool_get_cmd_by_name(
    const struct zw_command_class* cls, const char* name);

/**
 * Fill array with pointers to all parameter names for a command
 * 
 * \param names  Pointer to array of string pointers to fill
 * \param len    Length of names array
 * 
 * \return Number of parameter names in array
 */
size_t zw_cmd_tool_get_param_names(const struct zw_command* cmd,
                                   const char** names,
                                   size_t len);

/**
 * Get parameter by its name
 */
const struct zw_parameter* zw_cmd_tool_get_param_by_name(
    const struct zw_command* cmd, const char* name);

/**
 * Print parameter definition
 * 
 * @param f FILE handle to print to
 * @param p The parameter definition to print
 * @param indent Number of spaces to indent the output
 */
void zw_cmd_tool_print_parameter(FILE* f,
                                 const struct zw_parameter* p,
                                 int indent);

/**
 * Construct a z-wave command
 * @param dst destination buffer
 * @param cmd command to generate
 * @prarm param_name_list list of parameternames
 */
int zw_cmd_tool_create_zw_command(uint8_t* dst, int dst_len,
                                  const char* cmdClass, const char* cmd,
                                  struct zw_param_data* data[]);

#endif /* XML_ZW_CMD_TOOL_H_ */
