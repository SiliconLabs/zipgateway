/* © 2019 Silicon Laboratories Inc.  */
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "test_helpers.h"
#include "zgw_str.h"

/**
 * \defgroup test_zgw_str Z/IP GW string utility function unittest
 *
 * Test plan
 *
 * Test if is_valid_utf8_string gives expected result
 *
 * Valid UTF-8 byte sequences
 * - Feed all ASCII charcters
 * - Feed few n-byte(n being 2, 3, and 4) UTF-8 encoding
 *
 * Invalid UTF-8 byte sequences
 * - Overlong UTF-8 encoding
 * - Illegal continuation byte sequence
 * - Byte sequence that is reserved for UTF-16 surrogate halves
 * - Byte sequence that standard guarantees to be non-character
 *
 * Test if is_valid_mdns_name gives expected result
 *
 * Test if is_valid_mdns_location gives expected result
 */

FILE* log_strm = NULL;

void test_valid_utf8_byte_sequence()
{
  test_print_suite_title(1, "Valid UTF-8 byte sequence testing");
  start_case("Valid ASCII string", log_strm);
  uint8_t i = 0;
  char ascii[129] = {0};
  while (i <= 127) {
    ascii[i] = i;
    i++;
  }
  check_true(is_valid_utf8_string(ascii, strlen(ascii)), "All valid ASCII string should pass");
  close_case("Valid ASCII string");

  start_case("Valid 2-byte UTF-8", log_strm);
  char utf8_2byte[100] = "¢£¿ÅÿΘΩλμϢϿЖҞӔӿՐզս֏◌֘ב״ר";
  check_true(is_valid_utf8_string(utf8_2byte, strlen(utf8_2byte)), "2-byte UTF-8 sequence testing - Greek, Arabic, Armenian");
  close_case("Valid 2-byte UTF-8");

  start_case("Valid 3-byte UTF-8", log_strm);
  char utf8_3byte[100] = "丹麥デンマーク덴마크";
  check_true(is_valid_utf8_string(utf8_3byte, strlen(utf8_3byte)), "3-byte UTF-8 sequence testing - Chinese, Japanese, Korean");
  close_case("Valid 3-byte UTF-8");

  start_case("Valid 4-byte UTF-8", log_strm);
  char utf8_4byte[100] = "😀😁😍😠🙈🙉🙊🙏🎵";
  check_true(is_valid_utf8_string(utf8_4byte, strlen(utf8_4byte)), "4-byte UTF-8 sequence testing - Emoji");
  close_case("Valid 4-byte UTF-8");
}


void test_invalid_utf8_byte_sequence()
{
  test_print_suite_title(1, "Invalid UTF-8 byte sequence testing");
  start_case("Overlong UTF-8 encoding", log_strm);
  char overlong_utf8_1[10] = {0xC0, 0x81};
  check_true(is_valid_utf8_string(overlong_utf8_1, strlen(overlong_utf8_1)) == false, "2 byte overlong UTF-8");
  char overlong_utf8_2[10] = {0xE0, 0x81, 0xBF};
  check_true(is_valid_utf8_string(overlong_utf8_2, strlen(overlong_utf8_2)) == false, "3 byte overlong UTF-8");
  char overlong_utf8_3[10] = {0xF0, 0x81, 0x8A, 0xBF};
  check_true(is_valid_utf8_string(overlong_utf8_3, strlen(overlong_utf8_3)) == false, "4 byte overlong UTF-8");
  close_case("Overlong UTF-8 encoding");

  start_case("Illegal continuation byte sequence", log_strm);
  char illegal_conti_byte1[10] = {0xCC, 0x71};
  check_true(is_valid_utf8_string(illegal_conti_byte1, strlen(illegal_conti_byte1)) == false, "2 byte illegal continuation byte");
  char illegal_conti_byte2[10] = {0xE2, 0xA2, 0x71};
  check_true(is_valid_utf8_string(illegal_conti_byte2, strlen(illegal_conti_byte2)) == false, "3 byte illegal continuation byte");
  char illegal_conti_byte3[10] = {0xF2, 0xA2, 0xB2, 0x71};
  check_true(is_valid_utf8_string(illegal_conti_byte3, strlen(illegal_conti_byte3)) == false, "4 byte illegal continuation byte");
  close_case("Illegal continuation byte sequence");

  start_case("Reserved UTF-16 surrogate", log_strm);
  char reserved_utf16_surrogate1[50] = {0xED, 0xA0, 0x81};
  check_true(is_valid_utf8_string(reserved_utf16_surrogate1, strlen(reserved_utf16_surrogate1)) == false, "U+D801 reserved for UTF-16 surrogate halves");
  char reserved_utf16_surrogate2[50] = {0xED, 0xAE, 0x85};
  check_true(is_valid_utf8_string(reserved_utf16_surrogate2, strlen(reserved_utf16_surrogate2)) == false, "U+DB85 reserved for UTF-16 surrogate halves");
  char reserved_utf16_surrogate3[50] = {0xED, 0xBF, 0xBF};
  check_true(is_valid_utf8_string(reserved_utf16_surrogate3, strlen(reserved_utf16_surrogate3)) == false, "U+DFFF reserved for UTF-16 surrogate halves");
  close_case("Reserved UTF-16 surrogate");

  start_case("UTF-8 Non-character", log_strm);
  char utf8_non_character[50] = {0xEF, 0xBF, 0xBF, 0xEF, 0xBF, 0xBE};
  check_true(is_valid_utf8_string(utf8_non_character, strlen(utf8_non_character)) == false, "The code point standard guarantees to be invalid");
  close_case("UTF-8 Non-character");

  start_case("Corner cases", log_strm);
  char *utf8_corner_case1 = NULL;
  check_true(is_valid_utf8_string(utf8_corner_case1, 0) == false, "Null string is designed to return false");
  char utf8_corner_case2[10] = {0xFF};
  check_true(is_valid_utf8_string(utf8_corner_case2, strlen(utf8_corner_case2)) == false, "No valid leading byte sequence");
  close_case("Corner cases");
}

void test_mdns_name()
{
  test_print_suite_title(1, "mDNS name testing");
  start_case("Various mDNS name input", log_strm);
  char mdns_name1[10] = "1.2.4";
  check_true(is_valid_mdns_name(mdns_name1, strlen(mdns_name1)) == false, "mDNS name contains dots should be invalid");
  char mdns_name2[10] = "1_2_4";
  check_true(is_valid_mdns_name(mdns_name2, strlen(mdns_name2)) == false, "mDNS name contains underscore should be invalid");
  char mdns_name3[10] = "-";
  check_true(is_valid_mdns_name(mdns_name3, strlen(mdns_name3)) == false, "mDNS name ends with dash should be invalid");
  char mdns_name4[10] = "";
  check_true(is_valid_mdns_name(mdns_name4, strlen(mdns_name4)) == true, "Empty mDNS name should be allowed");
  char mdns_name5[10] = {0x01, '\0'};
  check_true(is_valid_mdns_name(mdns_name5, strlen(mdns_name5) + 1) == false, "mDNS name ends with \\0 should not be allowed");
  char mdns_name6[10] = {0xFF, 0xFF};
  check_true(is_valid_mdns_name(mdns_name6, strlen(mdns_name6)) == false, "mDNS name with invalid UTF-8 encoding should not be allowed");
  close_case("Various mDNS name input");
}

void test_mdns_location()
{
  test_print_suite_title(1, "mDNS location testing");
  start_case("Various mDNS location input", log_strm);
  char mdns_location1[10] = ".123";
  check_true(is_valid_mdns_location(mdns_location1, strlen(mdns_location1)) == false, "mDNS location starts with dots should be invalid");
  char mdns_location2[10] = "123.";
  check_true(is_valid_mdns_location(mdns_location2, strlen(mdns_location2)) == false, "mDNS location ends with dots should be invalid");
  char mdns_location3[10] = "1_2_3";
  check_true(is_valid_mdns_location(mdns_location3, strlen(mdns_location3)) == false, "mDNS location contains underscore should be invalid");
  char mdns_location4[10] = "123-.456";
  check_true(is_valid_mdns_location(mdns_location4, strlen(mdns_location4)) == false, "mDNS sub-location ends with underscore should be invalid");
  char mdns_location5[10] = "123.456";
  check_true(is_valid_mdns_location(mdns_location5, strlen(mdns_location5)) == true, "mDNS sub-location contains dots should be valid");
  char mdns_location6[10] = {0xFF, 0xFF};
  check_true(is_valid_mdns_location(mdns_location6, strlen(mdns_location6)) == false, "mDNS location with invalid UTF-8 encoding should not be allowed");
  char mdns_location7[10] = "";
  check_true(is_valid_mdns_location(mdns_location7, strlen(mdns_location7)) == true, "Empty mDNS location should be allowed");
  close_case("Various mDNS location input");
}

int main()
{
  test_valid_utf8_byte_sequence();
  test_invalid_utf8_byte_sequence();
  char s[2] = "";
  check_true(is_valid_utf8_string(s, strlen(s)), "Empty string testing");

  test_mdns_name();
  test_mdns_location();

  close_run();
  return numErrs;
}
