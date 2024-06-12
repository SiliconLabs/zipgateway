/* © 2019 Silicon Laboratories Inc.
 */

#ifndef ZGW_UTF8_H_
#define ZGW_UTF8_H_

#include <stdbool.h>
#include <stdint.h>

/**
 * UTF-8 string validation
 * This function will scan over the string for all possible UTF-8 encoding. If
 * any invalid byte sequence is found, return false. Otherwise, return true.
 *
 * @param s char pointer pointing to the beginning of the string
 * @param len the length of the given string
 * @return True if s is a valid UTF-8 string. Otherwise, return false. Null string is considered invalid
 */
bool is_valid_utf8_string(const char *s, uint32_t len);

/**
 * Validate mDNS name based on SDS13782
 * @param name       mDNS name
 * @param len        length of mDNS name
 * @return True if valid name. Otherwise, return false. Empty string is considered valid
 */
bool is_valid_mdns_name(const char* name, uint8_t len);

/**
 * Validate mDNS location based on SDS13782
 * @param location   mDNS location
 * @param len        length of mDNS location
 * @return True if valid name. Otherwise, return false. Empty string is considered valid
 */
bool is_valid_mdns_location(const char* location, uint8_t len);

#endif
