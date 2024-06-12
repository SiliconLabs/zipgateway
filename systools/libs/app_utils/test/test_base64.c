/* © 2019 Silicon Laboratories Inc. */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <lib/zgw_log.h>
#include "test_helpers.h"
#include "base64.h"

#define ENCODE true
#define DECODE false

/*****************************************************************************/
static const char* str2hex(const uint8_t *in, size_t len)
{
  static char hexbuf[2000];
  uint32_t offset = 0;

  hexbuf[0] = 0;

  for (size_t i = 0; i < len; i++)
  {
    uint8_t c = in[i];
    int32_t buf_remaining = sizeof(hexbuf) - offset;
    if (buf_remaining > 0)
    {
      if (isprint(c))
      {
        offset += snprintf(&hexbuf[offset], buf_remaining, "%c", c);
      }
      else
      {
        offset += snprintf(&hexbuf[offset], buf_remaining, "[0x%02x]", c);
      }
    }
  }
  hexbuf[sizeof(hexbuf) - 1] = 0;
  return hexbuf;
}

/*****************************************************************************/
static void b64_conv_test(const uint8_t *in, size_t in_len, const uint8_t *expected, size_t expected_len, bool encode)
{
  test_print(test_verbose, "IN  (%02zd): %s\n", in_len, str2hex(in, in_len));

  size_t out_len = 0;
  void *out_mem = NULL;
  if (encode)
  {
    // ENCODE (output is base64 string)
    char *out = base64_encode(in, in_len, 0);
    out_len = (out) ? strlen(out) : 0;
    test_print(test_verbose, "OUT (%02zd): %s\n", out_len, (out) ? out : "(NULL)");
    out_mem = out;
  }
  else
  {
    // DECODE (output is byte array)
    uint8_t *out = base64_decode((const char*) in, in_len, &out_len);
    test_print(test_verbose, "OUT (%02zd): %s\n", out_len, str2hex(out, out_len));
    out_mem = out;
  }
  
  bool test_passed = false;
  if ((out_len == expected_len) && ((out_mem && !memcmp(out_mem, expected, out_len)) || (out_mem == 0 && in_len == 0)))
  {
    test_passed = true;
  }
  
  check_true(test_passed, "");

  if (out_mem)
  {
    free(out_mem);
  }
}


/*****************************************************************************/
static void test_2_ways_str(const char *text, const char *base64)
{
  const char *tc_name = "Base64 encode and decode text";
  start_case(tc_name, 0);

  test_print(test_comment, "Encode text\n");
  b64_conv_test((const uint8_t *)text,   strlen(text),   (const uint8_t *)base64, strlen(base64), ENCODE);

  test_print(test_comment, "Decode base64 string\n");
  b64_conv_test((const uint8_t *)base64, strlen(base64), (const uint8_t *)text,   strlen(text), DECODE);

  close_case(tc_name);
}


/*****************************************************************************/
static void test_TextMultipleLenghtAndBase64Padding(void)
{
  // Test strings taken from https://en.wikipedia.org/wiki/Base64
  test_2_ways_str("", "");
  test_2_ways_str("d", "ZA==");
  test_2_ways_str("de", "ZGU=");
  test_2_ways_str("det", "ZGV0");
  test_2_ways_str("deta", "ZGV0YQ==");
  test_2_ways_str("any carnal pleasure.", "YW55IGNhcm5hbCBwbGVhc3VyZS4=");
  test_2_ways_str("any carnal pleasure", "YW55IGNhcm5hbCBwbGVhc3VyZQ==");
  test_2_ways_str("any carnal pleasur", "YW55IGNhcm5hbCBwbGVhc3Vy");
  test_2_ways_str("any carnal pleasu", "YW55IGNhcm5hbCBwbGVhc3U=");
  test_2_ways_str("any carnal pleas", "YW55IGNhcm5hbCBwbGVhcw==");
  test_2_ways_str("pleasure.", "cGxlYXN1cmUu");
  test_2_ways_str("leasure.", "bGVhc3VyZS4=");
  test_2_ways_str("easure.", "ZWFzdXJlLg==");
  test_2_ways_str("asure.", "YXN1cmUu");
  test_2_ways_str("sure.", "c3VyZS4=");
  test_2_ways_str("If there is only one significant input octet, or when the last input group contains "
                  "only one octet, all 8 bits will be captured in the first two Base64 digits (12 bits); "
                  "the four least significant bits of the last content-bearing 6-bit block will turn out "
                  "to be zero, and discarded on"
                  ,
                  "SWYgdGhlcmUgaXMgb25seSBvbmUgc2lnbmlmaWNhbnQgaW5wdXQgb2N0ZXQsIG9yIHdoZW4gdGhl"
                  "IGxhc3QgaW5wdXQgZ3JvdXAgY29udGFpbnMgb25seSBvbmUgb2N0ZXQsIGFsbCA4IGJpdHMgd2ls"
                  "bCBiZSBjYXB0dXJlZCBpbiB0aGUgZmlyc3QgdHdvIEJhc2U2NCBkaWdpdHMgKDEyIGJpdHMpOyB0"
                  "aGUgZm91ciBsZWFzdCBzaWduaWZpY2FudCBiaXRzIG9mIHRoZSBsYXN0IGNvbnRlbnQtYmVhcmlu"
                  "ZyA2LWJpdCBibG9jayB3aWxsIHR1cm4gb3V0IHRvIGJlIHplcm8sIGFuZCBkaXNjYXJkZWQgb24=");
}


/*****************************************************************************/
static void test_Base64StringWithInvalidChars(void)
{
  const char *text = "In computer science Base64 is a group of binary-to-text encoding schemes that represent "
                     "binary data in an ASCII string format by translating it into a radix-64 representation.";
                          
  const char *base64_clean = "SW4gY29tcHV0ZXIgc2NpZW5jZSBCYXNlNjQgaXMgYSBncm91cCBvZiBiaW5hcnktdG8tdGV4dCBl"
                          "bmNvZGluZyBzY2hlbWVzIHRoYXQgcmVwcmVzZW50IGJpbmFyeSBkYXRhIGluIGFuIEFTQ0lJIHN0"
                          "cmluZyBmb3JtYXQgYnkgdHJhbnNsYXRpbmcgaXQgaW50byBhIHJhZGl4LTY0IHJlcHJlc2VudGF0"
                          "aW9uLg==";

  // Invalid base64 characters (white-space etc.) in the base64 input string should be ignored
  const char *base64_with_invalid_chars = "SW4gY29tcHV0ZXIgc2NpZW5jZSBC\n\nYXNlNjQgaXMgYSBncm91cCBvZiBiaW5hcnktdG8tdGV4dCBl\n"
                          "bmNvZGluZyBzY2hlbWVzIHRoYXQgcmVw  cmVzZ W50IGJpbmFyeSBkYXRhIGluIGFuIEFTQ0lJIHN0\n"
                          "cmluZyBmb3JtYXQgYnkgd\r\n\tHJhbnNsYXRpbmcgaXQg aW50byBhIHJhZGl4LTY0IHJlcHJlc2VudGF0\n"
                          "aW9uLg==";

  const char *tc_name = "Base64 encode text";
  start_case(tc_name, 0);
  b64_conv_test((const uint8_t *)text, strlen(text),
                (const uint8_t *)base64_clean, strlen(base64_clean),
                ENCODE);
  close_case(tc_name);

  tc_name = "Decode base64 string with invalid characters";
  start_case(tc_name, 0);
  b64_conv_test((const uint8_t *)base64_with_invalid_chars, strlen(base64_with_invalid_chars),
                (const uint8_t *)text, strlen(text),
                DECODE);
  close_case(tc_name);
}


/*****************************************************************************/
static void test_BinaryEncodeDecode(void)
{
  const uint8_t  binary_data[] = { 255, 0, 45, 10, 1, 0, 0, 0, 254, 187, 200 };
  const char    *b64encoded    = "/wAtCgEAAAD+u8g=";

  const char *tc_name = "Base64 encode binary (non-ASCII) data";
  start_case(tc_name, 0);
  b64_conv_test(binary_data,                 sizeof(binary_data),
                (const uint8_t*)b64encoded, strlen(b64encoded),
                ENCODE);
  close_case(tc_name);

  tc_name = "Base64 decode to binary (non-ASCII) data";
  start_case(tc_name, 0);
  b64_conv_test((const uint8_t*)b64encoded, strlen(b64encoded),
                binary_data,                sizeof(binary_data),
                DECODE);
  close_case(tc_name);
}


/*****************************************************************************/
int main(int argc, char ** argv)
{
  test_TextMultipleLenghtAndBase64Padding();
  test_Base64StringWithInvalidChars();
  test_BinaryEncodeDecode();

  close_run();
  return numErrs;
}
