/* © 2019 Silicon Laboratories Inc.  */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "test_helpers.h"
#include "test_restore_help.h"
#include "zgwr_json_parser_helpers.h"

zgw_log_id_define(tst_js_help);
zgw_log_id_default_set(tst_js_help);

/*****************************************************************************/
static void check_result(const char *in, uint8_t *expected_out, size_t expected_len)
{
  char tc_desc_buf[300] = {};
  size_t       len = 0;
  json_object *jso = (in) ? json_object_new_string(in) : NULL;
  uint8_t     *out = create_DSK_uint8_ptr_from_json(jso, &len);

  // printf("OUT: was=%s, exp=%s  LEN: was=%02d, exp=%02d\n", (out) ? "non-null" : "null", (expected_out) ? "non-null" : "null", len, expected_len);
  if ((NULL == out) || (NULL == expected_out))
  {
    sprintf(tc_desc_buf, "Decode byte array (null-check): \"%s\"", (in) ? in : "(null)");
    check_true((out == expected_out) && (len == expected_len), tc_desc_buf);
  }
  else
  {
    // Need to check both length and content, but do not want two "PASS" messages, so we only invoke one of the helpers
    if (len == expected_len)
    {
      sprintf(tc_desc_buf, "Decode byte array (content): \"%s\"", (in) ? in : "(null)");
      check_mem(expected_out, out, len, "Unexpected value for element %02d. Expected %02x, was %02x", tc_desc_buf);
    }
    else
    {
      sprintf(tc_desc_buf, "Decode byte array (length): \"%s\"", (in) ? in : "(null)");
      check_true((len == expected_len), tc_desc_buf);
    }
  }
  if (out)
  {
    free(out);
  }
}


/*****************************************************************************/
static void test_HexFormats(void)
{
  const char *tc_name = "Decode HEX strings";
  start_case(tc_name, 0);

  check_result(NULL, NULL, 0); // null-pointer value

  check_result("", NULL, 0);  // Empty value

  check_result("0x", NULL, 0);  // Invalid length

  check_result("_0x", NULL, 0);  // Invalid characters

  check_result("0xWW", NULL, 0);  // Invelid characters and length

  check_result("0x12", NULL, 0); // Invalid length (must be multiple of 16 bits)

  uint8_t h1[] = {0x00, 0x00};
  check_result("0x0000", h1, sizeof(h1)); // OK

  uint8_t h2[] = {0xA0, 0x45};
  check_result("0xA045", h2, sizeof(h2)); // OK

  uint8_t h3[] = {0xFF, 0xFF};
  check_result("0xFFFF", h3, sizeof(h3)); // OK

  check_result("0xA045__", NULL, 0); // Invalid characters

  uint8_t h4[] = {0xA0, 0x45, 0x01, 0x02};
  check_result("0xA0450102", h4, sizeof(h4)); // OK

  uint8_t h5[] = {0xA0, 0x45, 0x01, 0x02, 0xfe, 0xff, 0x67, 0x98, 0x7F, 0x69, 0x02, 0x4a, 0xd7, 0xbe, 0x87, 0x98};
  check_result("0xA0450102feff67987F69024ad7be8798", h5, sizeof(h5)); // OK

  check_result("0xA0450102feff67ZZZZ987F69024ad7be8798", NULL, 0); // Invalid characters ZZZZ

  check_result("0xA0450102feff67987F69024ad7be87", NULL, 0); // Invalid length (not multiple of 16 bits)

  close_case(tc_name);
}

/*****************************************************************************/
static void test_QrCodeFormats(void)
{
  const char *tc_name = "Decode QR code strings";
  start_case(tc_name, 0);

  check_result("QR:", NULL, 0);  // Invalid length

  check_result("_QR:", NULL, 0);  // Invalid characters

  check_result("QR:WW", NULL, 0);  // Invelid characters and length

  check_result("QR:1234", NULL, 0); // Invalid length (must be multiple of 5 digits)

  check_result("QR:--1234", NULL, 0); // Invalid length (must be multiple of 5 digits)

  // TODO The next one should cause the converter to fail, but it does not
  // (something to do with strtok not handling empty fields correctly)
  // Need to look into it (could use strsep(), but it's not C99)
  // check_result("QR:12345--54321", NULL, 0); // Invalid length
  
  check_result("QR:12345-5432", NULL, 0); // Invalid length

  uint8_t q1[] = {0x00, 0x00};
  check_result("QR:00000", q1, sizeof(q1)); // OK

  uint8_t q2[] = {0xFF, 0xFF};
  check_result("QR:65535", q2, sizeof(q2)); // OK

  check_result("QR:65536", NULL, 0); // Range error (too big)

  check_result("QR:99999", NULL, 0); // Range error (too big)

  check_result("QR:1000F", NULL, 0); // Invalid character

  check_result("QR:9999999999999", NULL, 0); // Length error (and range error)

  uint8_t q3[] = {0xDD, 0x45, 0xFC, 0x2B, 0x27, 0x10, 0x85, 0xAB, 0xDC, 0x7D, 0xFB, 0xC7, 0x28, 0xA0, 0x86, 0x73};
  check_result("QR:56645-64555-10000-34219-56445-64455-10400-34419", q3, sizeof(q3));

  check_result("QR:56645-64555-10000-34219-56445-64455-10400-344-", NULL, 0); // Length error

  close_case(tc_name);
}

/*****************************************************************************/
int main(int argc, char ** argv)
{
   char *file1 = "json_parser_helper.log";
   verbosity = test_case_start_stop;

   /* Start the logging system at the start of main. */
   zgw_log_setup(file1);

  test_HexFormats();
  test_QrCodeFormats();

  close_run();

   /* Close down the logging system at the end of main(). */
   zgw_log_teardown();
  return numErrs;
}
