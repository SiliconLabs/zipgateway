/* © 2019 Silicon Laboratories Inc. */

#ifndef _ZGWR_JSON_PARSER_HELPERS_H
#define _ZGWR_JSON_PARSER_HELPERS_H

#include <stdint.h>
#include <stdbool.h>
#include "uiplib.h"
#include "json.h"

typedef char* zgwr_key_t;

#if 0
  #define VERBOSE_PRINT printf
  #define VERBOSE_PRINT_ENABLED
#else
  #define VERBOSE_PRINT(fmt,...)
#endif

/**
 * \defgroup json-reader-helpers Helper functions for ZGW Data File Reader
 * \ingroup json-reader
@{
 */

/* Type converters from ZGW Data File (JSON) types to internal ZGW types. */

/** Validate that the int32 is a uint8 and store it.
 * \param[in]  the_number An int32.
 * \param[out] slot Pointer to a uint8_t return slot.
 * \return false if the number is out of range, true otherwise.
 */
bool uint8_set_from_intval(int32_t the_number,
                           uint8_t *slot);

/** Validate that the int64 is a uint32 and store it.
 * \param[in] the_number An int64.
 * \param[out] slot Pointer to return slot.
 * \return false if the number is out of range, true otherwise.
 */
bool uint32_set_from_intval(int64_t the_number,
                            uint32_t *slot);


/* ZGW Data File (JSON) parsing helpers */

/** Check that key is exp_key.
 *
 * \param[in] key Null terminated string.
 * \param[in] exp_key Null terminated string.
 * \return True if key and exp_key have the same length and the same bytes, false otherwise.
 */
bool parse_key_match(const char *key, const char *exp_key);

/** Match a key and an integer from a JSON object against a list of legal keys.
 *
 * If a match is found and the integer is a valid uint16, write the
 * uint16 in the matching slot in the return array.
 *
 * \param[in] the_number  An int32.
 * \param[in] the_key JSON key corresponding to the_number.
 * \param[in] num_keys Length of the keys array.
 * \param[in] keys Array of expected keys.
 * \param[out] field Pointer to an array of integers of length num_keys.
 *
 * \return True if the_key is one of the expected keys and the number is a uint16, false otherwise.
 */
bool find_uint16_field(int32_t the_number, zgwr_key_t the_key,
                       int num_keys, zgwr_key_t *keys,
                       uint16_t **field);

/** Validate that the obj contains a uint8_t and write the integer to slot.
 * \param[in] obj A json object.
 * \param[out] slot The return argument.
 * \return false if obj does not contain a valid uint8.
 */
bool uint8_set_from_json(json_object *obj, uint8_t *slot);

/** Validate that the obj contains a uint16_t and write the integer to slot.
 * \param[in] obj A json object.
 * \param[out] slot The return argument.
 * \return false if obj does not contain a valid uint8.
 */
bool uint16_set_from_json(json_object *obj, uint16_t *slot);

/** Generate a ZGW DSK representation from a ZGW Data File (JSON) object.
 *
 * The JSON object must be a string containing the DSK in one of
 * these formats (any length with multiples of 16-bit values):
 *  - Hex format 0xA0450102feff67987F69024ad7be8798
 *  - QR-code format: 56645-64555-10000-34219-56445-64455-10400-34419
 *
 * The format is automatically detected by inspecting the first two
 * characters.
 * 
 * \param[in] dsk_obj A json object of string type with the correct JSON key for a DSK.
 * \param[out] out_len Pointer to the length of the final DSK object.
 * \return Pointer to a DSK object. Caller must free() the object after use.
 */
uint8_t* create_DSK_uint8_ptr_from_json(json_object *dsk_obj, size_t *out_len);

/**
@}
*/

#endif
