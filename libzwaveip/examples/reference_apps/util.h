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

#ifndef UTIL_H_
#define UTIL_H_

#include <stdint.h>

/**
 * Locates the XML file defining all Z-Wave command classes.
 *
 * Currently just assumes it is located in the same folder as the executable.
 * It furthermore assumes that argv[0] contains the full path to executable.
 *
 * \param[in] argv0 The 0th argument the program was invoked with. Usually
 * contains the full path to executable.
 *
 */
const char* find_xml_file(char *argv0);

/**
 * Convert a string of hex characters to an binary array
 * 
 * @param hexstr String of hex characters. e.g. "B21234AB4F"
 * @param buf Buffer to place converted values in.
 * @param buflen Length of buf (in bytes)
 * @return Number of bytes used in buf.
 */
uint8_t hexstr2bin(const char *hexstr, uint8_t *buf, uint8_t buflen);

#endif /* UTIL_H_ */
