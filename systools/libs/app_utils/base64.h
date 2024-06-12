/* © 2019 Silicon Laboratories Inc. */
#ifndef _APP_UTILS_BASE64_H
#define _APP_UTILS_BASE64_H

#include <stdint.h>

/**
 * @brief Base64 encode a byte array
 * 
 * The base64 encoded string will be broken into user defined line lengths
 * by inserting '\n' into the string.
 * 
 * @param in       The byte array to encode
 * @param in_len   Length of the byte array
 * @param line_len The line length of the generated base64 string. If zero
 *                 no line breaks will be inserted. MIME (RFC 2045) specifies
 *                 a max line length of 76, so that will be a good choice if
 *                 MIME compliance is required.
 * @return         Pointer to the base64 encoded string. NULL if an error
 *                 occured. The caller must free() the buffer after use.
 */
char * base64_encode(const uint8_t *in, size_t in_len, size_t max_line_len);

/**
 * @brief Decode a base64 string
 * 
 * @param in        The base64 string to decode.
 * @param in_len    Length of the base64 string (not counting the \0 string
 *                  terminator)
 * @param out_len   Pointer to variable that will receive the length of the
 *                  decoded byte array.
 * @return          Pointer to the decoded byte array. NULL if an error
 *                  occured. The caller must free() the buffer after use.
 */
uint8_t * base64_decode(const char *in, size_t in_len, size_t *out_len);

#endif // _APP_UTILS_BASE64_H