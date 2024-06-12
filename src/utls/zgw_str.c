/* © 2019 Silicon Laboratories Inc.
 */
#include <stdlib.h>
#include <stdio.h>
#include "zgw_str.h"
/*
 * UTF-8 string validation
 *
 * This function loops over the given string for all the possible byte
 * sequences, i.e. 1, 2, 3, and 4 bytes sequence, and in each possible byte
 * sequence, identify if one invalid encoding exists in the given string.
 *
 * The function is capable of indetifying the following invalid UTF-8 encoding.
 * - E1: Overlong encoding, e.g. ASCII 'A' padded with leading 0 and becomes 2 bytes sequence
 * - E2: Illegal continuation byte, e.g. All the continuing bytes should begin with 0b10 (0x80)
 * - E3: Reserved for UTF-16 surrogate halves, e.g. U+D800 - U+DFFF
 * - E4: Noncharacters, e.g. U+FFFF and U+FFFE, the code points that are guarnateed to be non-character.
 */
bool is_valid_utf8_string(const char *ss, uint32_t len)
{
  const unsigned char *s = (unsigned char*)ss;

  if (s == NULL)
    return false;

  while(*s && (len > 0)) {
    if ((s[0] <= 127) && (len >= 1)) {
      /* ASCII - 1 byte encoding */
      s++;
      len--;
    } else if (((s[0] & 0xe0) == 0xc0) && (len >= 2)) {
      /* 110xxxxx 10xxxxxx - 2 bytes encoding */

      if (((s[0] & 0xfe) == 0xc0) /* E1 */
          || (s[1] & 0xc0) != 0x80) { /* E2 */
        return false;
      }
      s+=2;
      len-=2;
    } else if (((s[0] & 0xf0) == 0xe0) && (len >= 3)) {
      /* 1110xxxx 10xxxxxx 10xxxxxx - 3 bytes encoding */

      if (((s[0] == 0xe0) && ((s[1] & 0xe0) == 0x80)) /* E1 */
          || ((s[1] & 0xc0) != 0x80) /* E2 */
          || ((s[2] & 0xc0) != 0x80) /* E2 */
          || ((s[0] == 0xed) && ((s[1] & 0xe0) == 0xa0)) /* E3 */
          || ((s[0] == 0xef) && (s[1] == 0xbf) && ((s[2] & 0xfe) == 0xbe))) { /* E4 */
        return false;
      }
      s+=3;
      len-=3;
    } else if (((s[0] & 0xf8) == 0xf0) && (len >= 4)) {
      /* 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx - 4 bytes encoding */

      if (((s[0] == 0xf0) && ((s[1] & 0xf0) == 0x80)) /* E1 */
          || ((s[1] & 0xc0) != 0x80) /* E2 */
          || ((s[2] & 0xc0) != 0x80) /* E2 */
          || ((s[3] & 0xc0) != 0x80) /* E2 */
          || (((s[0] == 0xf4) && (s[1] > 0x8f)) || (s[0] > 0xf4))) { /* E3 */
        return false;
      }
      s+=4;
      len-=4;
    } else {
      /* No valid leading byte sequence is found or byte sequence does not match with the length */

      return false;
    }
  }

  /* Empty string */
  return true;
}

/**
 * Validate the mDNS name based on spec SDS13782 Z/IP Naming and Location CC
 * - The name MUST NOT contain the dot (also known as period) character "."
 * - The name MUST NOT contain the underscore "_"
 * - The name MUST NOT end with the dash character "-"
 */
bool is_valid_mdns_name(const char* name, uint8_t len)
{
  if(len == 0)
    return true;

  uint8_t i = 0;
  while (i < len) {
    if (name[i] == '.' || name[i] == '_') {
      return false;
    }
    i++;
  }

  if (name[len - 1] == '-') {
    return false;
  }
  if (name[len - 1] == '\0') {
     return false;
  }
  if (is_valid_utf8_string(name, len) == false) {
    return false;
  }

  return true;
}

/**
 * Validate the mDNS location based on spec SDS13782 Z/IP Naming and Location CC
 * - The location may contain . but not start or end with them
 * - The location string MUST NOT contain the underscore character "_"
 * - Each location sub-string (separated by the dot) MUST NOT end with dash
 */
bool is_valid_mdns_location(const char* location, uint8_t len)
{
  if (len == 0)
    return true;

  uint8_t i = 0;
  if (location[0] == '.' || location[len - 1] == '.')
    return false;
  for (i = 0; i < len; i++) {
    if (location[i] == '_')
      return false;

    if ((i >= 1) && (i <= (len - 1))
        && location[i] == '.'
        && location[i - 1] == '-') {
      return false;
    }
  }
  if (is_valid_utf8_string(location, len) == false)
    return false;

  return true;
}
