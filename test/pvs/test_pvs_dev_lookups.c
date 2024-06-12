/* © 2014 Silicon Laboratories Inc.
 */

#include "provisioning_list.h"
#include <string.h>
#include "test_helpers.h"

/**
\ingroup pvs_test 
* @{
*
*/

FILE *log_strm = NULL;

static uint8_t dsk5[] =       {10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
static uint8_t challenge5[] = { 0,  0, 12, 13, 14, 15, 16, 17, 18, 19, 20};
static uint8_t dsk6[] =       {10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 51, 62, 73, 76};
static uint8_t challenge6[] = {12,  0, 12, 13, 14, 15, 16, 17, 18, 19, 20};
static uint8_t challenge7[] = {12,  0, 12, 13, 14, 15, 16, 17, 18, 19, 20, 22};
static uint8_t dskregular[] = {0xff,0, 12, 13, 14, 15, 16, 17, 18, 19, 20, 0xAB, 0x00, 0xFF, 0x17, 0x06};
static uint8_t challengexl[]= {12,  0, 12, 13, 14, 15, 16, 17, 18, 19, 20, 0xAB, 0x00, 0xFF, 0x17, 0x06, 0xCC, 0xC0};
static uint8_t *name3 = (uint8_t *)"Node3";
static uint8_t *location3 = (uint8_t *)"Location3";

static void test_provisioning_list_dev_match_challenge(void);

/* Test that we can match a S2 inclusion challenge with a provisioning_list DSK.
 * In this case, the first two bytes of the challenge may be zeroed out. */
static void test_provisioning_list_dev_match_challenge(void)
{
  struct provision *pvs = provisioning_list_dev_add(sizeof(dsk5), dsk5, PVS_BOOTMODE_S2);
  struct provision *pvsreg = NULL;

  test_print_suite_title(1, "Challenge lookup");

  start_case("Lookup device by challenge ---\n    Add DSK dsk5\n", log_strm);
  provisioning_list_tlv_set(pvs, PVS_TLV_TYPE_NAME, (uint8_t)strlen((char*)name3)+1, name3);
  provisioning_list_tlv_set(pvs, PVS_TLV_TYPE_LOCATION, (uint8_t)strlen((char*)location3)+1, location3);

  test_print(3, "Test with challenge 5, size %u\n", sizeof(challenge5));
  check_not_null((void*)provisioning_list_dev_match_challenge(sizeof(challenge5), challenge5),
                 "Challenge5 should match DSK5");

  check_not_null((void*)provisioning_list_dev_match_challenge(sizeof(challenge6), challenge6),
                 "Challenge6 should match DSK5");
  check_not_null((void*)provisioning_list_dev_match_challenge(11, challenge7),
                 "11 bytes from challenge7 should match DSK5");
  close_case("Lookup device by challenge ---\n    Add DSK dsk5\n");

  start_case("Challenge too long", log_strm);
  check_null((void*)provisioning_list_dev_match_challenge(sizeof(challenge7), challenge7),
             "Challenge 7 should not match DSK5 (too long)");
  check_not_null((void*)provisioning_list_dev_match_challenge(11, challengexl),
                 "11 bytes from challengexl should match DSK5");
  check_null((void*)provisioning_list_dev_match_challenge(sizeof(challengexl), challengexl),
             "16 bytes from challengexl should match DSK5");

  pvsreg = provisioning_list_dev_add(sizeof(dskregular), dskregular, PVS_BOOTMODE_S2);
  check_true(pvsreg == provisioning_list_dev_match_challenge(sizeof(challengexl), challengexl),
             "16 bytes from challengexl should match dskregular");

  pvs = provisioning_list_dev_add(sizeof(dsk6), dsk6, PVS_BOOTMODE_SMART_START);
  check_not_null((void*)provisioning_list_dev_match_challenge(sizeof(challenge5), challenge5),
                 "Challenge5 should match DSK6");
  check_not_null((void*)provisioning_list_dev_match_challenge(sizeof(challenge6), challenge6),
                 "Challenge6 should match DSK6");
  check_null((void*)provisioning_list_dev_match_challenge(sizeof(challenge7), challenge7),
             "Challenge 7 should not match DSK6 (character mismatch)");
  close_case("Challenge too long");

  start_case("Challenge too short", log_strm);
  check_null((void*)provisioning_list_dev_match_challenge(0, challenge6),
             "0 bytes from challenge6 should NOT match DSK6");
  check_null((void*)provisioning_list_dev_match_challenge(2, challenge6),
             "2 bytes from challenge6 should NOT match DSK6");
  close_case("Challenge too short");
}

int main()
{
    log_strm = test_create_log(TEST_CREATE_LOG_NAME);

    provisioning_list_init(NULL, NULL);

    provisioning_list_clear();

    test_provisioning_list_dev_match_challenge();

    close_run();
    fclose(log_strm);

    return numErrs;
}

/**
 * @}
 */
