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

#include <string.h>
#include "hexchar.h"

/* Convert ch from a hex digit to an int */
int8_t hex(uint8_t ch) {
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

/* See header file for description */
int asciihex_to_bin(const char *asciihex_str, uint8_t *binary_out,
                    int maxlen) {
  int numChars = 0;
  int8_t hexValue;
  uint8_t tmp;
  uint8_t byteValue = 0;
  const char *ptr = asciihex_str;
  size_t chars_to_convert = strlen(ptr);

  if (0 != (chars_to_convert % 2)) {
    /* Ascii hex string must have even number of chars */
    return -1;
  }
  /* Dont overflow output array */
  if (chars_to_convert > 2 * maxlen) {
    chars_to_convert = 2 * maxlen;
  }

  while (numChars < chars_to_convert) {
    hexValue = hex(*ptr);
    if (hexValue < 0) break;

    tmp = ((byteValue) << 4);
    byteValue = tmp | (hexValue & 0xF);

    numChars++;
    if (0 == (numChars % 2)) {
      *(binary_out++) = byteValue;
      byteValue = 0;
    }

    ptr++;
  }
  return (numChars / 2);
}
