/* © 2019 Silicon Laboratories Inc. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "json_helpers.h"
#include "user_message.h"
#include "endian_wrap.h"

//#define TRACE(...) fprintf(stderr, __VA_ARGS__)
#define TRACE(...) do {} while(0)


/* Is set in case any of the functions detects a parse error */
static bool parse_error_detected = false;

/* The root node of the json tree. Must be registered with
 * register_json_root() for json_get_object_path() to work */
static json_object *json_root = NULL;

#define MAX_PATH_ITEM_COUNT 10
#define MAX_PATH_ITEM_SIZE 50

typedef struct
{
  char name[MAX_PATH_ITEM_SIZE];
} json_path_item_t;

// ---------------------------------------------------------------------------
// From ZWave/API/ZW_transport_api.h
// ---------------------------------------------------------------------------
/* Max number of nodes in a Z-wave system */
#define ZW_LR_MAX_NODE_ID     4000
#define ZW_LR_MIN_NODE_ID     256
#define ZW_MAX_NODES          ZW_LR_MAX_NODE_ID
#define ZW_CLASSIC_MAX_NODES  232
#define MAX_CLASSIC_NODEMASK_LENGTH (ZW_CLASSIC_MAX_NODES/8)
#define HOMEID_LENGTH      4
// ---------------------------------------------------------------------------

#define MAX_NODEMASK_LENGTH   (ZW_MAX_NODES/8)

/*****************************************************************************/
static void ZW_NodeMaskSetBit(uint8_t* pMask, uint16_t bNodeID)
{
  bNodeID--;
  *(pMask+(bNodeID>>3)) |= (0x1 << (bNodeID & 7));
}

/*****************************************************************************/
static void ZW_NodeMaskClear(uint8_t* pMask, uint16_t bLength)
{
  /* Clear entire node mask */
  if (bLength)
  {
    do
    {
      *pMask = 0;
      pMask++;
    } while (--bLength);
  }
}

/*****************************************************************************/
static uint8_t ZW_NodeMaskNodeIn(const uint8_t* pMask, uint16_t bNode)
{
  bNode--;
  return ( ((*(pMask+(bNode>>3)) >> (bNode & 7)) & 0x01) );
}



/*****************************************************************************/
/*****************************************************************************/


/*****************************************************************************/
void set_json_parse_error_flag(bool status)
{
  parse_error_detected = status;
}

/*****************************************************************************/
bool json_parse_error_detected(void)
{
  return parse_error_detected;
}

/*****************************************************************************/
void json_add_int(json_object* jo, const char *key, int32_t val)
{
  json_object_object_add(jo, key, json_object_new_int(val));
}

/*****************************************************************************/
void json_add_bool(json_object* jo, const char *key, bool val)
{
  json_object_object_add(jo, key, json_object_new_boolean(val));
}

/*****************************************************************************/
void json_add_string(json_object* jo, const char *key, const char *val)
{
  json_object_object_add(jo, key, json_object_new_string(val));
}

/*****************************************************************************/
void json_add_nodemask(json_object* jo, const char *key, const uint8_t *node_mask_val)
{
  json_object_object_add(jo, key, nodemask_to_json(node_mask_val));
}

/*****************************************************************************/
void json_add_byte_array(json_object* jo, const char *key, const uint8_t *array, uint32_t len)
{
  json_object* jo_array  = json_object_new_array();

  for (uint32_t i = 0; i < len; i++)
  {
    json_object_array_add(jo_array , json_object_new_int(array[i]));
  }

  json_object_object_add(jo, key, jo_array);
}

/*****************************************************************************/
json_object* home_id_to_json(const uint8_t * home_id_buf)
{
  // Convert homeid in uint8_t[] to hex string (0xFFFFFFFF)
  char strbuf[2 * HOMEID_LENGTH + 3];

  /* The home id is stored in a four byte byte array with MSB at index 0
   * (i.e. big endian) this is true both for 500 series and 700 series
   * controller (even though 700 series is a little endian platform)
   */
  uint32_t home_id = be32toh(*((uint32_t*) home_id_buf)); 

  snprintf(strbuf, sizeof(strbuf), "0x%0*X", HOMEID_LENGTH * 2, home_id);
  strbuf[sizeof(strbuf) - 1] = 0;

  return json_object_new_string(strbuf);
}

/**
 * @brief Convert a 32-bit (700-series) version to a json string
 * 
 * The json string contains Major, minor and patch as "MM.mm.pp"
 * 
 * @param version Major, minor and patch encoded in a 32-bit value.
 *                (this is how 700 series store the version)
 *                The caller is responsible for converting the 32-bit
 *                integer to host endianess (if needed) before calling
 *                this function.
 * @return json_object* A json string formatted like MM.mm.pp.
 */
json_object* version_to_json(uint32_t version)
{
  uint8_t major = (version >> 16) & 0xFF;
  uint8_t minor = (version >> 8) & 0xFF;
  uint8_t patch = (version) & 0xFF;

  char version_buf[12];

  sprintf(version_buf, "%02d.%02d.%02d", major, minor, patch);

  return json_object_new_string(version_buf);
}

/*****************************************************************************/
json_object* nodemask_to_json(const uint8_t *node_mask)
{
  json_object* jo  = json_object_new_array();

  for (uint16_t node_id = 1; node_id <= ZW_CLASSIC_MAX_NODES; node_id++)
  {
    if (ZW_NodeMaskNodeIn((uint8_t *)node_mask, node_id))
    {
      json_object_array_add(jo , json_object_new_int(node_id));
    }
  }
  return jo;
}

/*****************************************************************************/
void json_register_root(json_object* obj)
{
  json_root = obj;
}

/*****************************************************************************/
/* Local helper function to recursively search a json tree for a specific
 * json object. If the object is found all key values leading to the found
 * object are copied to the path_items string array as the call stack unwinds.
 */
static bool search_object(json_object *jso,
                          const json_object *jso_find,  // The object to find
                          json_path_item_t path_items[],
                          uint32_t path_item_count,
                          uint32_t current_path_item_index)
{
  if ((NULL == jso) || (NULL == jso_find))
  {
    return false;
  }

  if (jso == jso_find)
  {
    return true;
  }

  if (current_path_item_index >= MAX_PATH_ITEM_COUNT)
  {
    return false;
  }

  json_type current_obj_type = json_object_get_type(jso);

  if (json_type_object == current_obj_type)
  {
    json_object_object_foreach(jso, key, child)
    {
      if (search_object(child, jso_find, path_items, path_item_count, current_path_item_index + 1))
      {
        snprintf(path_items[current_path_item_index].name, MAX_PATH_ITEM_SIZE, "%s", key);
        path_items[current_path_item_index].name[MAX_PATH_ITEM_SIZE - 1] = 0;
        return true;
      }
    }
  }
  else if (json_type_array == current_obj_type)
  {
    size_t array_len = json_object_array_length(jso);
    for (size_t i = 0; i < array_len; i++)
    {
      json_object *child = json_object_array_get_idx(jso, i);
      if (search_object(child, jso_find, path_items, path_item_count, current_path_item_index + 1))
      {
        sprintf(path_items[current_path_item_index].name, "%zd", i);
        return true;
      }
    }
  }
  return false;
}

/*****************************************************************************/
const char * json_get_object_path(const json_object* obj)
{
  static char path[501];
  static json_path_item_t path_items[MAX_PATH_ITEM_COUNT];

  memset(path_items, 0, sizeof(path_items));

  if (json_root)
  {
    if (search_object(json_root, obj, path_items, MAX_PATH_ITEM_COUNT, 0))
    {
      memset(path, 0, sizeof(path));
      for (int i = 0; i < MAX_PATH_ITEM_COUNT && 0 != path_items[i].name[0]; i++)
      {
        size_t cur_path_len = strlen(path);
        snprintf(&path[cur_path_len], sizeof(path) - cur_path_len, "/%s", path_items[i].name);
        path[sizeof(path) - 1] = 0;
      }
      return path;
    }
  }
  else
  {
    user_message(MSG_ERROR, "Implementation error: json_get_object_path() failed. "
                            "json_root must be registered with json_register_root().");
  }
  
  return "(unknown path)";
}

/*****************************************************************************/
bool json_get_object_error_check(json_object* obj,
                                 const char *key,
                                 json_object **value,
                                 enum json_type expected_type,
                                 bool is_required)
{
  bool obj_found = json_object_object_get_ex(obj, key, value);

  if (obj_found)
  {
    enum json_type actual_type = json_object_get_type(*value);
    if ((expected_type!=json_type_null) && (expected_type != actual_type))
    {
      user_message(MSG_ERROR, "ERROR: Invalid value type (%s) for key \"%s/%s\". Must be %s.\n",
                                      json_type_to_name(actual_type),
                                      json_get_object_path(obj),
                                      key,
                                      json_type_to_name(expected_type));
      parse_error_detected = true;  
    }
  }
  else
  {
    message_severity_t msg_severity = MSG_INFO;
    const char *msg_prefix = "INFO: Optional";
    if (is_required)
    {
      msg_prefix = "ERROR: Required";
      msg_severity = MSG_ERROR;
      parse_error_detected = true;
    }
    user_message(msg_severity, "%s key not found: \"%s/%s\".\n", msg_prefix, json_get_object_path(obj), key);
  }

  return obj_found;
}

/*****************************************************************************/
bool json_get_nodemask(json_object* parent, const char *key, uint8_t *node_mask, bool is_required)
{
  json_object* jo_array = NULL;

  ZW_NodeMaskClear(node_mask, MAX_CLASSIC_NODEMASK_LENGTH);

  if (json_get_object_error_check(parent, key, &jo_array, json_type_array, is_required))
  {
    for (int i = 0; (i < json_object_array_length(jo_array)) && (i < ZW_CLASSIC_MAX_NODES); i++)
    {
      ZW_NodeMaskSetBit(node_mask, json_object_get_int(json_object_array_get_idx(jo_array, i)));
    }
    return true;
  }
  return false;
}

/*****************************************************************************/
uint8_t json_get_bytearray(json_object* parent, const char *key, uint8_t *array, uint32_t length, bool is_required)
{
  json_object* jo_array = NULL;

  if (json_get_object_error_check(parent, key, &jo_array, json_type_array, is_required))
  {
    // TODO: Should the caller clear array? Should value be anything else than zero?
    memset(array, 0, length);
    int i = 0;
    for (i = 0; (i < json_object_array_length(jo_array)) && (i < length); i++)
    {
      array[i] = json_object_get_int(json_object_array_get_idx(jo_array, i));
      TRACE("%s[%d] = %d\n", key, i, array[i]);
    }
    return i;  // Number of elements added to array
  }
  return 0;
}

/*****************************************************************************/
uint32_t json_get_home_id(json_object* parent, const char *key, uint8_t *home_id_buf, uint32_t default_val, bool is_required)
{
  uint32_t     home_id = default_val;
  json_object *jo_home_id = NULL;
  const char  *str_home_id = "";

  if (json_get_object_error_check(parent, key, &jo_home_id, json_type_string, is_required))
  {
    str_home_id = json_object_get_string(jo_home_id);
    if (strlen(str_home_id) > 0)
    {
      char *endptr;
      /* We must use strtoll() here instead of strtol() since the latter
       * only supports values up to LONG_MAX (7FFFFFFF). HomeIds can be
       * larger than that.
       */
      long long int int_val = strtoll(str_home_id, &endptr, 0);

      // NB: ('\0' == *endptr) signals if the full string has been converted
      if (('\0' == *endptr) && (errno != ERANGE) && (int_val > 0) && (int_val < 0xFFFFFFFF))
      {
        home_id = (uint32_t) int_val;
      }
    }
  }

  if (home_id_buf)
  {
    /* The home id must be stored in a four byte byte array with MSB at
     * index 0 (i.e. big endian) this is true both for 500 series and
     * 700 series controller (even though 700 series is a little endian
     * platform)
     */
    uint32_t home_id_be = htobe32(home_id);
    memcpy(home_id_buf, &home_id_be, HOMEID_LENGTH);
  }
  TRACE("%s: %s (%d)\n", key, str_home_id, home_id);
  return home_id;
}

/*****************************************************************************/
int32_t json_get_int(json_object* parent, const char *key, int32_t default_val, bool is_required)
{
  json_object *jo_val = NULL;
  int32_t ret_val = default_val;
  if (json_get_object_error_check(parent, key, &jo_val, json_type_int, is_required))
  {
    ret_val = json_object_get_int(jo_val);
  }
  TRACE("%s: %d\n", key, ret_val);
  return ret_val;
}

/*****************************************************************************/
bool json_get_bool(json_object* parent, const char *key, bool default_val, bool is_required)
{
  json_object *jo_val = NULL;
  bool ret_val = default_val;
  if (json_get_object_error_check(parent, key, &jo_val, json_type_boolean, is_required))
  {
    ret_val = json_object_get_boolean(jo_val);
  }
  TRACE("%s: %s\n", key, ret_val ? "true" : "false");
  return ret_val;
}

/*****************************************************************************/
const char * json_get_string(json_object* parent, const char *key, const char *default_val, bool is_required)
{
  json_object *jo_val = NULL;
  const char *ret_val = default_val;
  if (json_get_object_error_check(parent, key, &jo_val, json_type_string, is_required))
  {
    ret_val = json_object_get_string(jo_val);
  }
  TRACE("%s: %s\n", key, ret_val);
  return ret_val;
}

/*****************************************************************************/
json_object* json_get_object(json_object* parent, const char *key, json_object* default_val, bool is_required)
{
  json_object *jo_val = NULL;
  json_object *ret_val = default_val;
  if (json_get_object_error_check(parent, key, &jo_val, json_type_null, is_required))
  {
    ret_val = jo_val;
  }
  TRACE("%s: %s\n", key, ret_val);
  return ret_val;
}

json_object* json_object_get_from_path(json_object* top, const char* path) {
  char *sep = ".";
  char *key, *phrase, *brkt;
  char *path_copy = strdup(path);

  json_object* parent = top;
  for (key = strtok_r(path_copy, sep, &brkt);
      key;
      key = strtok_r(NULL, sep, &brkt))
  {

    //Detect array index on the form xxxx[yy]
    int i  = strlen(key)-1;
    int j = i ;
    int idx = -1;
    if(key[i ] == ']') {
      while(j>0 && (key[j] != '[')) j--;
      key[j] =  0;
      key[i] =  0;

      if((i-j)<2) break; // parse error
      idx = atoi( &key[j+1] );
    }

    json_object* next_level;
    if(! json_object_object_get_ex( parent,key , &next_level  )) {
      break; //Child not found
    }

    if(idx>=0)
    {
      if(json_object_get_type(next_level) != json_type_array) {
        break; //Not an array
      }
      if( idx >= (int)json_object_array_length(next_level) ) {
        break; //not enough elements
      }
      next_level = json_object_array_get_idx( next_level,idx );
    }

    parent = next_level;
  }

  free(path_copy);

  //Did we parse all the tokens ?
  if(key == NULL) {
    return parent;
  } else {
    return NULL;
  }
}
