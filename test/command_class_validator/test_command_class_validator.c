/* © 2019 Silicon Laboratories Inc. */

/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/
#include "ZIP_Router.h"
#include "ZW_command_validator.h"
#include "test_helpers.h"

/****************************************************************************/
/*                              TEST HELPERS                                */
/****************************************************************************/

static const char * validator_res2str(validator_result_t res)
{
  switch (res)
  {
    case UNKNOWN_CLASS:     return "UNKNOWN_CLASS"; break;
    case UNKNOWN_COMMAND:   return "UNKNOWN_COMMAND"; break;
    case UNKNOWN_PARAMETER: return "UNKNOWN_PARAMETER"; break;
    case PARSE_FAIL:        return "PARSE_FAIL"; break;
    case PARSE_OK:          return "PARSE_OK"; break;
    default:                return "(invalid validator_result_t enum)";
  }
}

static void test_validator(uint8_t *input,
                           int len,
                           const char *class_str,
                           const char *cmd_str,
                           validator_result_t expected_result,
                           int expected_version,
                           int line_num)
{
  if (len >= 2)
  {
    test_print(test_comment, "-- Testing %s(0x%02x) : %s(0x%02x) len=%d (line:%d)\n",
                             class_str,
                             input[0],
                             cmd_str,
                             input[1],
                             len,
                             line_num);
  }

  int actual_version = -1;

  /* Call the Validator! */
  validator_result_t actual_result = ZW_command_validator(input, len, &actual_version);

  test_print(test_verbose, "Result = %d (%s). Detected version = %d\n",
                           actual_result,
                           validator_res2str(actual_result),
                           actual_version);

  check_equal(actual_result, expected_result, "Validator result code");
  if (expected_version > 0)
  {
    check_equal(actual_version, expected_version, "Detected CC version");
  }
}

#define DO_TEST(res, ver, cc, cmd, ...) { uint8_t packet[] = {cc, cmd, __VA_ARGS__}; test_validator(packet, sizeof(packet), #cc, #cmd, (res), (ver), __LINE__); }

/****************************************************************************/
/*                              TEST CASES                                  */
/****************************************************************************/

/**
 * A few tests for meter report that contains an optional variant (Previous
 * meter value)
 *
 * Created as a result of bug report ZGW-2146
 */
void test_command_class_validator_meter_report(void)
{
  char *name = "Validate Meter Report packages";

  start_case(name, NULL);

  DO_TEST(PARSE_OK, 1,
          COMMAND_CLASS_METER,
          METER_REPORT,
          0x01, // Meter Type
          0x35, // Properties 2 (size = 5)
          0x00, 0x00, 0x01, 0x99, 0x33  // Meter Value
          );

  DO_TEST(PARSE_OK, 3,
          COMMAND_CLASS_METER,
          METER_REPORT,
          0x21, // Properties 1
          0x34, // Properties 2 (size = 4)
          0x00, 0x00, 0x01, 0x99,  // Meter Value
          0x00, 0x00 // Delta Time
          );

  DO_TEST(PARSE_FAIL, 0,
          COMMAND_CLASS_METER,
          METER_REPORT,
          0x21, // Properties 1
          0x34, // Properties 2 (size = 4)
          0x00, 0x00, 0x01,  // Meter Value (one byte less than expected)
          0x00, 0x00 // Delta Time
          );

  DO_TEST(PARSE_FAIL, 0,
          COMMAND_CLASS_METER,
          METER_REPORT,
          0x21, // Properties 1
          0x34, // Properties 2 (size = 4)
          0x00, 0x00, 0x01, 0x99,  // Meter Value
          0x00, 0xFF // Delta Time
          // Previous meter values expected here since delta != 0
          );

  DO_TEST(PARSE_OK, 3,
          COMMAND_CLASS_METER,
          METER_REPORT,
          0x21, // Properties 1
          0x34, // Properties 2 (size = 4)
          0x00, 0x00, 0x01, 0x99,  // Meter Value
          0x00, 0xFF, // Delta Time
          0x11, 0x22, 0x33, 0x44   // Previous Meter Value
          );

  /* NOTE: The next package should really fail, because it is longer than
   * expected.
   *
   * But currently the validator accepts messages if the beginning of the
   * message satisfies one of the parsers - even if it leaves extra data at the
   * end.
   *
   * The below message is detected as version 5 (and 4) since the first byte of
   * the "Previous meter value" is parsed as the "Scale 2" value leaving the
   * remaining 3 bytes un-inspected.
   */
  DO_TEST(PARSE_OK, 6,  // PARSE_FAIL could be expected here - see comment above
          COMMAND_CLASS_METER,
          METER_REPORT,
          0x21, // Properties 1
          0x34, // Properties 2 (size = 4)
          0x00, 0x00, 0x01, 0x99,  // Meter Value
          0x00, 0x00, // Delta Time
          0x11, 0x22, 0x33, 0x44   // Previous Meter Value (unexpected since delta == 0)
          );

  DO_TEST(PARSE_OK, 6,
          COMMAND_CLASS_METER,
          METER_REPORT,
          0x21, // Properties 1
          0x34, // Properties 2 (size = 4)
          0x00, 0x00, 0x01, 0x99,  // Meter Value
          0x00, 0x00, // Delta Time
          // (empty) Previous Meter Value
          0x01 // Scale 2
          );

  DO_TEST(PARSE_OK, 6,
          COMMAND_CLASS_METER,
          METER_REPORT,
          0x21, // Properties 1
          0x34, // Properties 2 (size = 4)
          0x00, 0x00, 0x01, 0x99,  // Meter Value
          0x00, 0x01, // Delta Time
          0x11, 0x22, 0x33, 0x44,   // Previous Meter Value
          0x01 // Scale 2
          );

  DO_TEST(PARSE_FAIL, 0,
          COMMAND_CLASS_METER,
          METER_REPORT,
          );

  DO_TEST(PARSE_OK, 4,
          COMMAND_CLASS_ZIP,
          COMMAND_ZIP_PACKET,
          0x21, // Properties 1 (don't care)
          0xC0, // Properties 2 Flag:0x40 (Z-Wave cmd included) | Flag:0x80 (Header ext included)
          0x12, // Seq No
          0x00, // Src endpoint
          0x00, // Dest Endpoint
          0x04, // Header ext (length of this and data) (optional variant)
          0x01, 0x02, 0x03, // Header ext (data)
          0x11, 0x12, 0x13, 0x14 // Command (optional byte)
          );

  DO_TEST(PARSE_OK, 4,
          COMMAND_CLASS_ZIP,
          COMMAND_ZIP_PACKET,
          0x21, // Properties 1 (don't care)
          0x40, // Properties 2 Flag:0x40 (Z-Wave cmd included) | Flag:0x80 (Header ext included)
          0x12, // Seq No
          0x00, // Src endpoint
          0x00, // Dest Endpoint
          0x11, 0x12, 0x13, 0x14 // Command (optional byte)
          );

  DO_TEST(PARSE_OK, 4,
          COMMAND_CLASS_ZIP,
          COMMAND_ZIP_PACKET,
          0x21, // Properties 1 (don't care)
          0x80, // Properties 2 Flag:0x40 (Z-Wave cmd included) | Flag:0x80 (Header ext included)
          0x12, // Seq No
          0x00, // Src endpoint
          0x00, // Dest Endpoint
          0x01, // Header ext (length of this and data) (optional byte)
          0xAA  // Header ext (data) (optional variant)
          );

  DO_TEST(PARSE_OK, 4,
          COMMAND_CLASS_ZIP,
          COMMAND_ZIP_PACKET,
          0x21, // Properties 1 (don't care)
          0x00, // Properties 2 Flag:0x40 (Z-Wave cmd included) | Flag:0x80 (Header ext included)
          0x12, // Seq No
          0x00, // Src endpoint
          0x00 // Dest Endpoint
          );

  // Test variant group and paramoffs with bit 0x80 set
  DO_TEST(PARSE_OK, 4,
          COMMAND_CLASS_CONFIGURATION,
          CONFIGURATION_BULK_REPORT_V3,
          0x11, 0x22, // Parameter Offset
          0x02, // Number of parameters/groups
          0x12, // Reports to follow
          0x03, // Properties1 (mask 0x07: size of each group)
          // Variant group 1
          0x11,  // Param 1 Val 1
          0x12,  // Param 1 Val 2
          0x13,  // Param 1 Val 3
          // Variant group 2
          0x24,  // Param 2 Val 1
          0x25,  // Param 2 Val 2
          0x25   // Param 2 Val 3
          );

  DO_TEST(PARSE_FAIL, 0,
          COMMAND_CLASS_CONFIGURATION,
          CONFIGURATION_BULK_REPORT_V3,
          0x11, 0x22, // Parameter Offset
          0x02, // Number of parameters/groups
          0x12, // Reports to follow
          0x03, // Properties1 (mask 0x07: size of each group)
          // Variant group 1
          0x11,  // Param 1 Val 1
          0x12,  // Param 1 Val 2
          0x13,  // Param 1 Val 3
          // Variant group 2
          0x24,  // Param 2 Val 1
          0x25   // Param 2 Val 2
          // Missing val 3
          );

  // Test MULTI_ARRAY
  DO_TEST(PARSE_OK, 3,
          COMMAND_CLASS_ASSOCIATION_GRP_INFO,
          ASSOCIATION_GROUP_INFO_REPORT,
          0x83, // Properties 1 (mask 0x80: List mode bit, mask 0x3F: group count) 
          // Group 1
          0x11, // Grouping identifier
          0x00, // Mode = 0
          0x20, // Profile MSB
          0x11, // Profile LSB
          0x00, // Reserved = 0
          0x00, // Event code MSB = 0
          0x00, // Event Code LSB = 0
          // Group 2
          0x22, // Grouping identifier
          0x00, // Mode = 0
          0x71, // Profile MSB
          0x03, // Profile LSB
          0x00, // Reserved = 0
          0x00, // Event code MSB = 0
          0x00, // Event Code LSB = 0
          // Group 3
          0x33, // Grouping identifier
          0x00, // Mode = 0
          0x6B, // Profile MSB
          0x20, // Profile LSB
          0x00, // Reserved = 0
          0x00, // Event code MSB = 0
          0x00 // Event Code LSB = 0
          );

  DO_TEST(UNKNOWN_PARAMETER, 0,
          COMMAND_CLASS_ASSOCIATION_GRP_INFO,
          ASSOCIATION_GROUP_INFO_REPORT,
          0x81, // Properties 1 (mask 0x80: List mode bit, mask 0x3F: group count) 
          // Group 1
          0x11, // Grouping identifier
          0x00, // Mode = 0
          0x20, // Profile MSB
          0x33, // Profile LSB (invalid LSB value for MSB 0x20)
          0x00, // Reserved = 0
          0x00, // Event code MSB = 0
          0x00 // Event Code LSB = 0
          );

  DO_TEST(PARSE_FAIL, 0,
          COMMAND_CLASS_ASSOCIATION_GRP_INFO,
          ASSOCIATION_GROUP_INFO_REPORT,
          0x83, // Properties 1 (mask 0x80: List mode bit, mask 0x3F: group count) 
          // Group 1
          0x11, // Grouping identifier
          0x00, // Mode = 0
          0x20, // Profile MSB
          0x01, // Profile LSB
          0x00, // Reserved = 0
          0x00, // Event code MSB = 0
          0x00  // Event Code LSB = 0
          // Group 2+3 missing
          );

#ifndef EXTENDED_USER_CODE_REPORT
#define EXTENDED_USER_CODE_REPORT 0x0D
#endif

  // Test variant group and const
  DO_TEST(PARSE_OK, 2,
          COMMAND_CLASS_USER_CODE,
          EXTENDED_USER_CODE_REPORT,
          0x03, // Number of user codes
          // User 1
          0x11, // User identifier MSB
          0x12, // User identifier LSB
          0x03, // User ID status (const checked)
          0x04, // mask 0xF0:reserved  mask 0x0F: user code length (N=4)
          0x01, 0x02, 0x03, 0x04, // User code 1-4
          // User 2
          0x21, // User identifier MSB
          0x22, // User identifier LSB
          0xFE, // User ID status (const checked)
          0x06, // mask 0xF0:reserved  mask 0x0F: user code length (N=6)
          0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // User code 1-6
          // User 3
          0x31, // User identifier MSB
          0x32, // User identifier LSB
          0x04, // User ID status (const checked)
          0x05, // mask 0xF0:reserved  mask 0x0F: user code length (N=6)
          0x01, 0x02, 0x03, 0x04, 0x05, // User code 1-5
          0x00, 0x00 // Next user identifier MSB, LSB
          );

  // Test variant group and const
  DO_TEST(PARSE_FAIL, 0,
          COMMAND_CLASS_USER_CODE,
          EXTENDED_USER_CODE_REPORT,
          0x03, // Number of user codes
          // User 1
          0x11, // User identifier MSB
          0x12, // User identifier LSB
          0x03, // User ID status (const checked)
          0x04, // mask 0xF0:reserved  mask 0x0F: user code length (N=4)
          0x01, 0x02, 0x03, 0x04, // User code 1-4
          // User 2 +3 missing here
          0x00, 0x00 // Next user identifier MSB, LSB
          );

  // Test variant group and const
  DO_TEST(PARSE_FAIL, 0,
          COMMAND_CLASS_USER_CODE,
          EXTENDED_USER_CODE_REPORT,
          0x01, // Number of user codes
          // User 1
          0x11, // User identifier MSB
          0x12, // User identifier LSB
          0x03, // User ID status (const checked)
          0x04, // mask 0xF0:reserved  mask 0x0F: user code length (N=4)
          0x01, 0x02, 0x03,  // User code 1-3 (SIZE ERROR: #4 is missing)
          0x00, 0x00 // Next user identifier MSB, LSB
          );

  // Test variant group and const
  DO_TEST(UNKNOWN_PARAMETER, 0,
          COMMAND_CLASS_USER_CODE,
          EXTENDED_USER_CODE_REPORT,
          0x01, // Number of user codes
          // User 1
          0x11, // User identifier MSB
          0x12, // User identifier LSB
          0xDD, // User ID status (const checked) <-- INVALID VALUE HERE
          0x04, // mask 0xF0:reserved  mask 0x0F: user code length (N=4)
          0x01, 0x02, 0x03, 0x04, // User code 1-4
          0x00, 0x00 // Next user identifier MSB, LSB
          );

  close_case(name);
}


void test_command_class_validator_network_management_proxy(void)
{
  char *name = "Validate Network Management proxy packages";

  start_case(name, NULL);

  DO_TEST(PARSE_OK, 4,
          COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY,
          FAILED_NODE_LIST_GET,
          0x01, //SEQ
          );
  close_case(name);
}

/**
 * Test begins here
 */
int main()
{
  test_command_class_validator_meter_report();
  test_command_class_validator_network_management_proxy();

  close_run();
  return numErrs;
}
