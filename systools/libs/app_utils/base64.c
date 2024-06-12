/* © 2019 Silicon Laboratories Inc. */
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "base64.h"

// Implemented according to https://en.wikipedia.org/wiki/Base64

// The base64 alphabet  (RFC 2045)
// NB: The table is created as a string, hence the last element
//     will be '\0' and the length will be 65.
static const uint8_t b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Markers values used by base64_decode()
#define INVALID_CHAR_INDEX 0xFF
#define BASE64_PAD         0xFE

char * base64_encode(const uint8_t *in, size_t in_len, size_t max_line_len)
{
  // For every block of three input bytes a 4-byte output block is generated
  // (an extra four byte output block is added to account for integer
  // division truncation)
  size_t out_buf_size = in_len / 3 * 4 + 4;
  
  if (max_line_len > 0)
  {
    // Account for line breaks
    out_buf_size += out_buf_size / max_line_len;
  }

  // Account for the string terminator '\0'
  out_buf_size += 1;

  char *out_buf = malloc(out_buf_size);

  if (NULL == out_buf)
  {
    return NULL;
  }

  size_t enc_len    = 0;
  size_t line_len   = 0;
  uint8_t  pad_buf[3] = {0};

  for (size_t i = 0; i < in_len; i += 3)
  {
    // Number of input bytes remaining
    size_t remaining = in_len - i;
    const uint8_t *inblock; // Point to next input block of 24-bits (i.e. three bytes)
    if (remaining < 3)
    {
      // Ensure inblock always points to at least three bytes
      memcpy(pad_buf, &in[i], remaining);
      inblock = pad_buf;
    }
    else
    {
      inblock = &in[i];
    }
    // Extract the 6-bit indexes into the base64 table
    uint8_t b64_index1 = inblock[0] >> 2;
    uint8_t b64_index2 = ((inblock[0] & 0x3) << 4) | (inblock[1] >> 4);
    uint8_t b64_index3 = ((inblock[1] & 0x0f) << 2) | (inblock[2] >> 6);
    uint8_t b64_index4 = inblock[2] & 0x3f;

    out_buf[enc_len++] = b64_table[b64_index1];
    out_buf[enc_len++] = b64_table[b64_index2];
    if (remaining >= 3)
    {
      out_buf[enc_len++] = b64_table[b64_index3];
      out_buf[enc_len++] = b64_table[b64_index4];
    }
    if (remaining == 2)
    {
      out_buf[enc_len++] = b64_table[b64_index3];
      out_buf[enc_len++] = '='; // padding
    }
    if (remaining == 1)
    {
      out_buf[enc_len++] = '='; // padding
      out_buf[enc_len++] = '='; // padding
    }
    line_len += 4;

    if ((max_line_len > 0) && (line_len >= max_line_len))
    {
      out_buf[enc_len++] = '\n';
      line_len = 0;
    }
  }

  out_buf[enc_len] = '\0';

  return out_buf;
}


uint8_t * base64_decode(const char *in, size_t in_len, size_t *out_len)
{
  static uint8_t char_to_b64_index[256] = {0};
  static bool index_table_initialized = false;

  if (!index_table_initialized)
  {
    // Generate ascii lookup table going from characters to indexes
    // into the base64 table. Characters that are not part of the base64
    // alphabet will have an index value of INVALID_CHAR_INDEX.
    // The special pad marker '=' is indicated in the table with BASE64_PAD.

    memset (char_to_b64_index, INVALID_CHAR_INDEX, sizeof(char_to_b64_index));

    // Fill char_to_b64_index from b64_table
    // (Subtracting one from b64_table size to skip the string terminator)
    for (size_t i = 0; i < (sizeof(b64_table) - 1); i++) 
    {
      uint8_t b64_char = b64_table[i];
      char_to_b64_index[b64_char] = i;
    }
    char_to_b64_index['='] = BASE64_PAD;
    index_table_initialized = true;
  }

  // Count valid input characters (those that are mapped to a valid
  // index into the b64_table)
  size_t valid_char_count = 0;
  for (size_t i = 0; i < in_len; i++)
  {
    if (INVALID_CHAR_INDEX != char_to_b64_index[in[i]])
    {
      valid_char_count++;
    }
  }

  // We expect the input to consist of four-byte blocks
  if ((0 == valid_char_count) || ((valid_char_count % 4) != 0))
  {
    return NULL;
  }

  // When decoding each block of four chars is mapped to three bytes.
  // (if the final block is padded it will be decoded into less than three bytes)
  size_t   out_buf_size = valid_char_count / 4 * 3;
  uint8_t *out_buf = malloc(out_buf_size);
  size_t   dec_len = 0;

  // Process the input in four-byte blocks
  uint8_t inblock[4] = {0};
  uint8_t inblock_elem_count = 0;
  bool padding_detected = false;

  for (size_t i = 0; i < in_len; i++)
  {
    // Only process valid base64 chars
    uint8_t b64_index = char_to_b64_index[in[i]];
    if (INVALID_CHAR_INDEX != b64_index)
    {
      inblock[inblock_elem_count++] = b64_index;
      if (4 == inblock_elem_count)
      {
        if (padding_detected)
        {
          // Padding was detected in a previous block, and now we're
          // processing yet another block. Since padding is only allowed
          // in the final block this is an error.
          // (Alternatively we could have ended the conversion successfully
          // when the padding was detected the first time - for now we treat
          // it as an error)
          free(out_buf);
          return NULL;
        }

        bool block_is_ok = false;

        // First two characters cannot be a padding
        if (BASE64_PAD != inblock[0] && BASE64_PAD != inblock[1])
        {
          out_buf[dec_len++] = (inblock[0] << 2) | (inblock[1] >> 4);

          // Check each allowed combinations of padding for the last two charactes

          if (BASE64_PAD != inblock[2] && BASE64_PAD != inblock[3]) // No padding - the "normal" case
          {
            out_buf[dec_len++] = (inblock[1] << 4) | (inblock[2] >> 2);
            out_buf[dec_len++] = (inblock[2] << 6) | inblock[3];
            block_is_ok = true;
          }
          else if (BASE64_PAD != inblock[2] && BASE64_PAD == inblock[3])  // Last inblock is padding
          {
            out_buf[dec_len++] = (inblock[1] << 4) | (inblock[2] >> 2);
            block_is_ok = true;
            padding_detected = true;
          }
          else if (BASE64_PAD == inblock[2] && BASE64_PAD == inblock[3])  // Last two sextets are padding
          {
            block_is_ok = true;
            padding_detected = true;
          }
        }

        if (!block_is_ok)
        {
          free(out_buf);
          return NULL;;
        }
        inblock_elem_count = 0;
      }
    }
  } // for (size_t i = 0; i < in_len; i++)

  *out_len = dec_len;
  return out_buf;
}
