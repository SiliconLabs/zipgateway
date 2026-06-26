/* © 2017 Silicon Laboratories Inc.
 */

/*
 * Test of a tiny part of ZIP_Router.c. In particular, test the detection of
 * Wake Up Notifications inside multi cmd encapsulations.
 */
/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/
#include <unity.h>
#include "RD_internal.h"
#include "ZIP_Router.h"
#include "RD_types.h"
#include "RF_Region_Set_Validator.h"

extern unsigned is_linklayer_addr_zero(uip_lladdr_t const *a);

/****************************************************************************/
/*                      PRIVATE TYPES and DEFINITIONS                       */
/****************************************************************************/

/****************************************************************************/
/*                              EXPORTED DATA                               */
/****************************************************************************/

/****************************************************************************/
/*                              PRIVATE DATA                                */
/****************************************************************************/
nodeid_t sleeping_node_id_fail_state = 21;
nodeid_t sleeping_node_id_done_state = 10;
/****************************************************************************/
/*                               MOCKS FUNCTIONS                            */
/****************************************************************************/
static struct {
  nodeid_t node;
  uint8_t is_broadcast;
} mb_wakeup_event_args = {0};

static struct {
  int call_count;
  uint8_t channels[3];
  int8_t thresholds[3];
  uint8_t ret;
} lbt_calls = {0};

void
__wrap_mb_wakeup_event(nodeid_t node, uint8_t is_broadcast)
{
  mb_wakeup_event_args.node = node;
  mb_wakeup_event_args.is_broadcast = is_broadcast;
}

security_scheme_t
__wrap_highest_scheme(uint8_t scheme_mask)
{
  return 0x3; /* NO_SCHEME */
}

uint8_t __wrap_GetCacheEntryFlag(nodeid_t nodeid)
{
  return 0;
}

uint8_t __wrap_ZW_SetListenBeforeTalkThreshold(uint8_t bChannel,
                                               int8_t bThreshold)
{
  int idx = lbt_calls.call_count;
  if (idx < 3) {
    lbt_calls.channels[idx] = bChannel;
    lbt_calls.thresholds[idx] = bThreshold;
  }
  lbt_calls.call_count++;
  return lbt_calls.ret;
}

uint16_t __wrap_rd_get_node_probe_flags(nodeid_t nodeid)
{
  if (nodeid == sleeping_node_id_fail_state) {
    return RD_NODE_PROBE_NEVER_STARTED;
  }
  return RD_NODE_FLAG_PROBE_HAS_COMPLETED;
}

rd_node_state_t __wrap_rd_get_node_state(nodeid_t nodeid)
{
  if (nodeid == sleeping_node_id_fail_state) {
    return STATUS_PROBE_FAIL;
  }
  return STATUS_DONE;
}

/****************************************************************************/
/*                              TEST FUNCTIONS                              */
/****************************************************************************/

static void init_test()
{
  memset(&mb_wakeup_event_args, 0, sizeof mb_wakeup_event_args);
  memset(&lbt_calls, 0, sizeof lbt_calls);
  lbt_calls.ret = TRUE;
}


static nodeid_t did_register_node=0;
static void (*probe_callback)(rd_ep_database_entry_t* ep, void* user);
static void* probe_callback_user;

void __wrap_rd_register_new_node(nodeid_t node,uint8_t node_properties_flag) {
  did_register_node = node;
}

int __wrap_rd_register_node_probe_notifier(
    nodeid_t node_id,
    void* user,
    void (*callback)(rd_ep_database_entry_t* ep, void* user)) {
      probe_callback = callback;
      probe_callback_user=user;
}



/**
 * Test handling of WUN in some error scenarios:
 * - Non-secure WUN to a device in STATUS_PROBE_FAIL.
 * - Wrong security-scheme WUN to a device in STATUS_DONE.
 */
void test_ApplicationCommandHandlerZIP_WUN()
{
  /* Sleeping device sends WUN unsecurely with STATUS_PROBE_FAIL state */
  init_test();
  uint8_t cmd1[] = {COMMAND_CLASS_WAKE_UP, WAKE_UP_NOTIFICATION,};
  cfg.mb_conf_mode = ENABLE_MAILBOX_SERVICE;
  ts_param_t tsp1 = {sleeping_node_id_fail_state, 0, 0, 0, 0, 0, 3};
  ApplicationCommandHandlerZIP(&tsp1, (ZW_APPLICATION_TX_BUFFER *)cmd1, sizeof cmd1);

  TEST_ASSERT_EQUAL(did_register_node,sleeping_node_id_fail_state);
  TEST_ASSERT_NOT_NULL(probe_callback);
  TEST_ASSERT_NOT_NULL(probe_callback_user);

  uint8_t failing_node_back = sleeping_node_id_fail_state;
  sleeping_node_id_fail_state = 0xff; //Make the node not failing
  probe_callback(0,probe_callback_user);

  /*"WUN received and bypassed security check\n");*/
  TEST_ASSERT_EQUAL(mb_wakeup_event_args.node,failing_node_back);

  /* Sleeping device sends WUN with STATUS_DONE state and wrong security scheme */
  init_test();
  ts_param_t tsp2 = {sleeping_node_id_done_state, 0, 0, 0, 0, 0, 2};
  ApplicationCommandHandlerZIP(&tsp2, (ZW_APPLICATION_TX_BUFFER *)cmd1, sizeof cmd1);
  /* "WUN received but dropped because of wrong security scheme\n" */
  TEST_ASSERT_EQUAL(mb_wakeup_event_args.node,0);
}

void test_zip_lbt_resolve_threshold()
{
  uint8_t wire = 0xAA;

  /* 700/800 (Gecko): ZWLBT is signed dBm. */
  wire = 0xAA;
  TEST_ASSERT_TRUE(zip_lbt_resolve_threshold(7, -65, &wire));
  TEST_ASSERT_EQUAL_HEX8(0xBF, wire);

  wire = 0xAA;
  TEST_ASSERT_TRUE(zip_lbt_resolve_threshold(8, -80, &wire));
  TEST_ASSERT_EQUAL_HEX8(0xB0, wire);

  /* Positive/out-of-range on Gecko is rejected (would defeat LBT). */
  wire = 0xAA;
  TEST_ASSERT_FALSE(zip_lbt_resolve_threshold(7, 64, &wire));
  TEST_ASSERT_EQUAL_HEX8(0xAA, wire);

  wire = 0xAA;
  TEST_ASSERT_FALSE(zip_lbt_resolve_threshold(7, 0, &wire));
  TEST_ASSERT_EQUAL_HEX8(0xAA, wire);

  wire = 0xAA;
  TEST_ASSERT_FALSE(zip_lbt_resolve_threshold(7, -129, &wire));
  TEST_ASSERT_EQUAL_HEX8(0xAA, wire);

  /* 500-series: ZWLBT keeps the legacy 34-78 unsigned encoding. */
  wire = 0xAA;
  TEST_ASSERT_TRUE(zip_lbt_resolve_threshold(5, 64, &wire));
  TEST_ASSERT_EQUAL_HEX8(64, wire);

  wire = 0xAA;
  TEST_ASSERT_TRUE(zip_lbt_resolve_threshold(5, 50, &wire));
  TEST_ASSERT_EQUAL_HEX8(50, wire);

  wire = 0xAA;
  TEST_ASSERT_FALSE(zip_lbt_resolve_threshold(5, 33, &wire));
  TEST_ASSERT_EQUAL_HEX8(0xAA, wire);

  wire = 0xAA;
  TEST_ASSERT_FALSE(zip_lbt_resolve_threshold(5, 79, &wire));
  TEST_ASSERT_EQUAL_HEX8(0xAA, wire);

  /* Unknown chip type: skip the override entirely. */
  wire = 0xAA;
  TEST_ASSERT_FALSE(zip_lbt_resolve_threshold(0, -65, &wire));
  TEST_ASSERT_EQUAL_HEX8(0xAA, wire);
}

void test_zip_lbt_configure_threshold_skips_when_not_configured()
{
  init_test();
  TEST_ASSERT_TRUE(zip_lbt_configure_threshold(7, JP, 0, -80));
  TEST_ASSERT_EQUAL(0, lbt_calls.call_count);
}

void test_zip_lbt_configure_threshold_applies_gecko_override()
{
  init_test();
  TEST_ASSERT_TRUE(zip_lbt_configure_threshold(7, KR, 1, -65));
  TEST_ASSERT_EQUAL(3, lbt_calls.call_count);
  TEST_ASSERT_EQUAL_UINT8(0, lbt_calls.channels[0]);
  TEST_ASSERT_EQUAL_UINT8(1, lbt_calls.channels[1]);
  TEST_ASSERT_EQUAL_UINT8(2, lbt_calls.channels[2]);
  TEST_ASSERT_EQUAL_INT8(-65, lbt_calls.thresholds[0]);
  TEST_ASSERT_EQUAL_INT8(-65, lbt_calls.thresholds[1]);
  TEST_ASSERT_EQUAL_INT8(-65, lbt_calls.thresholds[2]);
}

void test_zip_lbt_configure_threshold_ignores_invalid_gecko_override()
{
  init_test();
  TEST_ASSERT_TRUE(zip_lbt_configure_threshold(7, KR, 1, 64));
  TEST_ASSERT_EQUAL(0, lbt_calls.call_count);
}

void test_zip_lbt_configure_threshold_applies_500_series_override()
{
  init_test();
  TEST_ASSERT_TRUE(zip_lbt_configure_threshold(ZW_CHIP_TYPE, EU, 1, 64));
  TEST_ASSERT_EQUAL(2, lbt_calls.call_count);
  TEST_ASSERT_EQUAL_UINT8(0, lbt_calls.channels[0]);
  TEST_ASSERT_EQUAL_UINT8(1, lbt_calls.channels[1]);
  TEST_ASSERT_EQUAL_INT8(64, lbt_calls.thresholds[0]);
  TEST_ASSERT_EQUAL_INT8(64, lbt_calls.thresholds[1]);
}

static void test_is_linklayer_addr_zero()
{
  uip_lladdr_t uip_lladdr; /* Mocked from uip6.c */
  TEST_ASSERT_EQUAL(6,sizeof uip_lladdr.addr == 6);
  uint8_t test_mac[6];
  char err_msg[200];

  for(int i=0; i<6; i++) {
    memset(&uip_lladdr.addr, 0, sizeof uip_lladdr.addr);
    uip_lladdr.addr[i] = 0x01;  
    TEST_ASSERT_EQUAL(0,is_linklayer_addr_zero(&uip_lladdr));
  }


  uint8_t mac_all_ones[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  memcpy(&uip_lladdr.addr, mac_all_ones, sizeof uip_lladdr.addr);
  TEST_ASSERT_EQUAL(0,is_linklayer_addr_zero(&uip_lladdr));
  
  memset(&uip_lladdr.addr, 0, sizeof uip_lladdr.addr);
  TEST_ASSERT_EQUAL(1,is_linklayer_addr_zero(&uip_lladdr));
  }
