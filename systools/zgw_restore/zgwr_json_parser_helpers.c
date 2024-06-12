/* © 2019 Silicon Laboratories Inc. */

#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "zgwr_json_parser_helpers.h"
#include "uiplib.h"
#include <string.h>

/** Copy the_number to slot if the_number is a valid uint16 and key is exp_key.
 *
 * \param[in] the_number An int32.
 * \param[out] slot Pointer to a uint16_t return slot.
 * \param[in] key Null terminated string.
 * \param[in] exp_key Null terminated string.
 * \return True if key and exp_key match and the_number is valid, false otherwise.
 */
static bool uint16_set_from_int(int32_t the_number,
                                uint16_t *slot,
                                char *key,
                                char *exp_key);

bool uint8_set_from_intval(int32_t the_number,
                           uint8_t *slot) {
   if ((the_number <= UCHAR_MAX) && (the_number >= 0)) {
      VERBOSE_PRINT("  Setting uint8_t %d (0x%04x) at %p\n",
             the_number, the_number, slot);
      *slot = (uint8_t) the_number;
      return true;
   } else {
      printf("Illegal integer: %d, 0x%x, expected uint8_t\n", the_number, the_number);
      return false;
   }
}

bool uint16_set_from_intval(int32_t the_number,
                            uint16_t *slot) {
   if ((the_number <= UINT16_MAX) && (the_number >= 0)) {
      VERBOSE_PRINT("  Setting uint16_t %d (0x%04x) at %p\n",
             the_number, the_number, slot);
      *slot = (uint16_t) the_number;
      return true;
   } else {
      printf("Illegal integer: %d, 0x%x, expected uint16_t\n", the_number, the_number);
      return false;
   }
}

bool uint32_set_from_intval(int64_t the_number,
                            uint32_t *slot) {
   if ((the_number <= UINT32_MAX) && (the_number >= 0)) {
      VERBOSE_PRINT("  Setting uint32_t to 0x%04llx at %p\n",
             the_number, slot);
      *slot = (uint32_t) the_number;
      return true;
   } else {
      printf("Illegal integer: 0x%" PRIx64 ", expected uint32_t\n", the_number);
      return false;
   }
}

static bool uint16_set_from_int(int32_t the_number,
                                uint16_t *slot,
                                char *key,
                                char *exp_key) {
   if (parse_key_match(key, exp_key)) {
      if (uint16_set_from_intval(the_number, slot)) {
         VERBOSE_PRINT("  Set %s to 0x%04x\n", exp_key, *slot);
         return true;
      } else {
         printf("Illegal %s: 0x%x\n", exp_key, the_number);
      }
   }
   return false;
}

bool uint8_set_from_json(json_object *obj, uint8_t *slot) {
   if (obj == NULL || json_object_get_type(obj) != json_type_int) {
      printf("Expected integer object, got %s\n",
             json_object_get_string(obj));
      return false;
   }
   int the_number = json_object_get_int(obj);
   if ((the_number <= UCHAR_MAX) && (the_number >= 0)) {
      *slot = (uint8_t) the_number;
   } else {
      printf("Illegal uint8: 0x%04x\n",
             the_number);
      return false;
   }
   return true;
}

bool uint16_set_from_json(json_object *obj, uint16_t *slot) {
   if (obj == NULL || json_object_get_type(obj) != json_type_int) {
      printf("Expected integer object, got %s\n",
             json_object_get_string(obj));
      return false;
   }
   int the_number = json_object_get_int(obj);
   if ((the_number <= UINT16_MAX) && (the_number >= 0)) {
      *slot = (uint16_t) the_number;
   } else {
      printf("Illegal uint16: 0x%02x\n",
             the_number);
      return false;
   }
   return true;
}

/* helpers */
bool parse_key_match(const char *key, const char *exp_key) {
   size_t exp_len = strlen(exp_key);
   int res;

   if (strlen(key) != exp_len) {
      //      printf("Found %s, expected %s\n", key, exp_key);
      return false;
   }
   res = strncmp(key, exp_key, exp_len);
   return (res == 0);
}

bool find_uint16_field(int32_t the_number, zgwr_key_t the_key,
                      int num_keys, zgwr_key_t *keys,
                      uint16_t **field) {
   for (int ii = 0; ii < num_keys; ii++) {
      if (uint16_set_from_int(the_number, field[ii], the_key,  keys[ii])) {
         return true;
      }
   }
   return false;
}

#define MAX_QR_CODE_LEN 100
#define QR_CODE_PREFIX "QR:"
#define QR_CODE_PREFIX_LEN 3
#define HEX_PREFIX "0x"
#define HEX_PREFIX_LEN 2

uint8_t* create_DSK_uint8_ptr_from_json(json_object *dsk_obj, size_t *out_len)
{
   *out_len = 0;

   if (json_object_get_type(dsk_obj) != json_type_string)
   {
      return NULL;
   }

   uint8_t *out_buf = NULL;
   size_t   out_buf_size = 0;
   size_t   out_buf_cnt = 0;

   const char *in_str_val = json_object_get_string(dsk_obj);
   size_t      in_str_len = json_object_get_string_len(dsk_obj);

   // Detect input format
   if (strncmp(in_str_val, HEX_PREFIX, HEX_PREFIX_LEN) == 0)
   {
      // Hex format "0xA0450102feff67987F69024ad7be8798"

      // Every two hex digits result in one byte
      out_buf_size = (in_str_len - HEX_PREFIX_LEN) / 2;
      if (out_buf_size > 0)
      {
         out_buf = malloc(out_buf_size);
         if (out_buf)
         {
            // Process 16 bits (four hex digits) at a time
            for (size_t i = HEX_PREFIX_LEN; (i + 4) <= in_str_len; i += 4)
            {
               char *endptr = 0;
               char token[5] = {};
               strncpy(token, &in_str_val[i], 4);

               // printf("token=\"%s\" (i=%02d, in_str_len=%02d)\n", token, i, in_str_len);

               errno = 0;
               long int num_val = strtol(token, &endptr, 16);
               if ((0 == errno) && ('\0' == *endptr) && (num_val >= 0) && (num_val <= 0xFFFF))
               {
                  out_buf[out_buf_cnt++] = (uint8_t) ((num_val >> 8) & 0xFF);
                  out_buf[out_buf_cnt++] = (uint8_t) (num_val & 0xFF);
               }
               else
               {
                  free(out_buf);
                  return NULL;
               }
            }
         }
      }
   }
   else if (strncmp(in_str_val, QR_CODE_PREFIX, QR_CODE_PREFIX_LEN) == 0)
   {
      // QR-code format: "QR:56645-64555-10000-34219-56445-64455-10400-34419"

      // Each five digit sections results in two bytes
      out_buf_size = ((in_str_len - QR_CODE_PREFIX_LEN + 1) / 6) * 2;
      if (out_buf_size > 0)
      {
         out_buf = malloc(out_buf_size);
         if (out_buf)
         {
            if ((in_str_len - QR_CODE_PREFIX_LEN) < MAX_QR_CODE_LEN)
            {
               char *token  = 0;
               char *endptr = 0;
               char  qr_code[MAX_QR_CODE_LEN] = {};
               // Copy to non-const buffer for strtok()
               strcpy(qr_code, &in_str_val[QR_CODE_PREFIX_LEN]);
               token = strtok(qr_code, "-");
               while (token)
               {
                  if (strlen(token) == 5)
                  {
                     // printf("token=\"%s\"\n", token);

                     errno = 0;
                     long int num_val = strtol(token, &endptr, 10);
                     if ((0 == errno) && ('\0' == *endptr) && (num_val >= 0) && (num_val <= 0xFFFF))
                     {
                        out_buf[out_buf_cnt++] = (uint8_t) ((num_val >> 8) & 0xFF);
                        out_buf[out_buf_cnt++] = (uint8_t) (num_val & 0xFF);

                        token = strtok(NULL, "-");

                        continue; // Process next token
                     }
                  }

                  // Only here if errors was detected
                  free(out_buf);
                  return NULL;
               }
            }
         }
      }
   }

   if (out_buf_cnt != out_buf_size)
   {
      free(out_buf);
      return NULL;
   }

   *out_len = out_buf_cnt;
   return out_buf;
}
