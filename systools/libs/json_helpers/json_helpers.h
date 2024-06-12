/* © 2019 Silicon Laboratories Inc. */
#ifndef _JSON_HELPERS_H
#define _JSON_HELPERS_H

#include <stdint.h>
#include <stdbool.h>
#include <json.h>

/**
 * @brief A set of wrappers for json-c
 * 
 * Apart from a somewhat simplified API, the functions adds validation
 * of existence and value type. Any errors will be printed on stdout
 * with a path to the erroneous JSON object.
 */

/** Use these as the "is_required" parameter with the json_get_xxx
 * functions for better readability
 */
#define JSON_REQUIRED true
#define JSON_OPTIONAL false

/**
 * @brief Set the json parse error status flag
 * 
 * Sets the parse error status
 * Should usually be called as set_json_parse_error_flag(false) one time
 * before parsing a JSON file. After the JSON file has been parsed the final
 * status can be queried with json_parse_error_detected().
 * 
 * Most of the JSON helpers will call set_json_parse_error_flag(true) if
 * a required key is missing or if the value is not of the requested type.
 * Applications should therefore only call set_json_parse_error_flag(true)
 * if any application level JSON errors are detected.
 * 
 * @param parse_error_detected true if a parse error was detected. false
 *                             resets the error state (so only call once
 *                             with false)
 */
void set_json_parse_error_flag(bool parse_error_detected);

/**
 * @brief Query the JSON parse error status flag
 * 
 * @return true A JSON error was detected (since
 *              set_json_parse_error_flag(false) was last called)
 * @return false No errors detected so far.
 */
bool json_parse_error_detected(void);

// Generating JSON

void json_add_int(json_object* jo, const char *key, int32_t val);
void json_add_bool(json_object* jo, const char *key, bool val);
void json_add_string(json_object* jo, const char *key, const char *val);
void json_add_nodemask(json_object* jo, const char *key, const uint8_t *nodemask_val);
void json_add_byte_array(json_object* jo, const char *key, const uint8_t *array, uint32_t len);

json_object* home_id_to_json(const uint8_t * home_id_buf);
json_object* version_to_json(uint32_t version);
json_object* nodemask_to_json(const uint8_t *nodemask);

// Parsing JSON

bool json_get_object_error_check(json_object* obj,
                                 const char *key,
                                 json_object **value,
                                 enum json_type expected_type,
                                 bool is_required);
int32_t json_get_int(json_object* parent, const char *key, int32_t default_val, bool is_required);
bool json_get_bool(json_object* parent, const char *key, bool default_val, bool is_required);
const char * json_get_string(json_object* parent, const char *key, const char *default_val, bool is_required);
bool json_get_nodemask(json_object* parent, const char *key, uint8_t *nodemask, bool is_required);
uint8_t json_get_bytearray(json_object* parent, const char *key, uint8_t *array, uint32_t length, bool is_required);
uint32_t json_get_home_id(json_object* parent, const char *key, uint8_t *home_id_buf, uint32_t default_val, bool is_required);
json_object* json_get_object(json_object* parent, const char *key, json_object* default_val, bool is_required);

/**
 * @brief Generate a printable JSON path to the specified JSON object
 *        within a JSON object tree.
 * 
 * To help users diagnose JSON parse errors this function can generate an
 * easy to read JSON path like "/root_obj/obj2/obj3/obj".
 * 
 * @note json_register_root() must be called before calling this function
 *       (or any of the json_get_xxx() helpers)
 * 
 * @param obj  The JSON object to print the path to.
 * @return The object path or the string "(unknown path)". The returned string
 *         pointer references a static buffer, hence the returned string is only
 *         valid until json_get_object_path() is called the next time.
 */
const char * json_get_object_path(const json_object* obj);

/**
 * @brief Register the root object of a JSON file
 * 
 * The root is used by json_get_object_path() to generate printable
 * JSON paths to a specific json object.
 * 
 * @param obj The root of a JSON object tree.
 */
void json_register_root(json_object* obj);


/**
 * Find a json element from a given path. of the form
 * key1.key2[n].key3
 * 
 * Where key1 is an element of the top object, key2 is an element of the key1 object etc. 
 * If a key is an array, an element of that array can be selecet with the [ ] operator.
 * 
 * @param top  Top json structure
 * @param path Path of the element on the form key1.key2[n].key3
 * @return     Returns a json object if the element can be found, NULL if the element cannot be found.
 */
json_object* json_object_get_from_path(json_object* top, const char* path);

#endif // _JSON_HELPERS_H