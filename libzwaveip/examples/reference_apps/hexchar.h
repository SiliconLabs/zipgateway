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

#ifndef HEXCHAR_H_
#define HEXCHAR_H_

#include <inttypes.h>

/**
 * Convert an asciihex-encoded string to a binary array.
 *
 * As an example, the asciihex string '2002' will be converted to the binary
 * array {0x20, 0x02}.
 * Note: Asciihex strings are zero terminated.
 *
 * Will output at most maxlen binary bytes.
 * \param[in] ptr The ascii encoded hex string.
 * \param[out] The resulting binary array.
 * \param[in] The maximum output length in bytes.
 * \returns Number of bytes written to binary_out. Negative number indicates
 * error in conversion.
 */
int asciihex_to_bin(const char *asciihex_str, uint8_t *binary_out,
                    int maxlen);

#endif /* HEXCHAR_H_ */
