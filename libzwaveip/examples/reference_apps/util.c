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

#include "util.h"
#include <libgen.h>
#include <limits.h>
#include <string.h>

/* Buffer used for building XML file path.
 * Pointer to this buffer is returned by find_xml_file() */
static char xmlpath[PATH_MAX];

const char* find_xml_file(char *argv0) {
  const char xml_filename[] = "/ZWave_custom_cmd_classes.xml";
  strncpy(xmlpath, dirname(argv0), PATH_MAX);
  xmlpath[PATH_MAX - 1] = 0;
  strncat(xmlpath, xml_filename, PATH_MAX - strlen(xmlpath));
  xmlpath[PATH_MAX - 1] = 0;
  return xmlpath;
}

static int hex2int(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  } else if (c >= 'a' && c <= 'f') {
    return c - 'a' + 0xa;
  } else if (c >= 'A' && c <= 'F') {
    return c - 'A' + 0xa;
  } else {
    return -1;
  }
}

/* TODO There's also an asciihex_to_bin() in hexchar.c. Check that it works and
 * use that instead.
 */
uint8_t hexstr2bin(const char *hexstr, uint8_t *buf, uint8_t buflen) {
  int val;
  uint8_t bytes_written = 0;
  const char *s = hexstr;
  while (*s && bytes_written < buflen) {
    val = hex2int(*s++);
    if (val < 0) break;
    buf[bytes_written] = ((val) & 0xf) << 4;

    val = hex2int(*s++);
    if (val < 0) break;
    buf[bytes_written] |= (val & 0xf);

    bytes_written++;
  }
  return bytes_written;
}
