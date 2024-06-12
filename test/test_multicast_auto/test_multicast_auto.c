/*
 * test_multicast_auto.c
 *
 *  Created on: 19 Jun 2018
 *      Author: anesbens
 */

#include "ZIP_Router.h"
#include "S2_multicast_auto.h"
#include "unity.h"
#include "NodeCache.h"
#include "zgw_nodemask.h"
BYTE cb_status;
void* cb_user;
TX_STATUS_TYPE* cb_txStatEx;
int cb_count = 0;

const uint8_t testframe[] = "Hello world";
int testframe_len = sizeof(testframe);

static nodemask_t c0, c1, c2;

static void callback(BYTE txStatus, void* user, TX_STATUS_TYPE *txStatEx) {
  cb_status = txStatus;
  cb_user = user;
  cb_txStatEx = txStatEx;
  cb_count++;
}

ts_param_t *sec2_send_multicast_p;
const uint8_t *sec2_send_multicast_data;
uint8_t sec2_send_multicast_data_len;
BOOL sec2_send_multicast_send_sc_followups;
ZW_SendDataAppl_Callback_t sec2_send_multicast_callback;
void *sec2_send_multicast_user;
uint8_t sec2_send_multicast_return_value = 1;
int sec2_send_multicast_nc_call = 0;

uint8_t sec2_send_multicast(ts_param_t *p, const uint8_t *data,
    uint8_t data_len, BOOL send_sc_followups,
    ZW_SendDataAppl_Callback_t callback, void *user) {
  sec2_send_multicast_p = p;
  sec2_send_multicast_data = data;
  sec2_send_multicast_data_len = data_len;
  sec2_send_multicast_send_sc_followups = send_sc_followups;
  sec2_send_multicast_callback = callback;
  sec2_send_multicast_user = user;

  sec2_send_multicast_nc_call++;

  return sec2_send_multicast_return_value;
}

uint8_t GetCacheEntryFlag(nodeid_t node) {
  int flags = 0;

  if (nodemask_test_node(node, c2))
    flags |= NODE_FLAG_SECURITY2_ACCESS;
  if (nodemask_test_node(node, c1))
    flags |= NODE_FLAG_SECURITY2_AUTHENTICATED;
  if (nodemask_test_node(node, c0))
    flags |= NODE_FLAG_SECURITY2_UNAUTHENTICATED;
  return flags;;
}

void nodemask_from_list(nodemask_t mask, const uint8_t* nodelist, uint8_t len) {
  nodemask_clear(mask);

  for (int i = 0; i < len; i++) {
    nodemask_add_node(nodelist[i], mask);
  }
}

static void reset_book_keeping() {
  nodemask_from_list(c0, (uint8_t[] ) { 10, 11, 12, 13, 14 }, 5);
  nodemask_from_list(c1, (uint8_t[] ) { 20, 21, 22, 23, 24 }, 5);
  nodemask_from_list(c2, (uint8_t[] ) { 30, 31, 32, 33, 34 }, 5);

  sec2_send_multicast_auto_init();
  sec2_send_multicast_return_value = 1;
  sec2_send_multicast_nc_call = 0;
  cb_count = 0;

}

void test_one_multicast_3_groups() {
  ts_param_t p;

  /*Reset book keeping parameters*/
  reset_book_keeping();

  /*
   * Send multicast to 15 nodes, first 5 nodes are in c0 ,second 5 nodes are in c1 and last 5 nodes are in c2
   */
  nodemask_from_list(p.node_list, (uint8_t[] ) { 4, 10, 11, 12, 13, 14, 20, 21,
          22, 23, 24, 30, 31, 32, 33, 34 }, 1 + 3 * 5);
  sec2_send_multicast_auto_split(&p, testframe, testframe_len, 1, callback,
      (void*) 0x42);

  /* Verify transmission to all classes with no followup */
  TEST_ASSERT_EQUAL(1, sec2_send_multicast_nc_call);
  TEST_ASSERT_EQUAL(SECURITY_SCHEME_2_UNAUTHENTICATED,
      sec2_send_multicast_p->scheme);
  TEST_ASSERT_TRUE(nodemask_equal( sec2_send_multicast_p->node_list,c0 ) ==0);
  TEST_ASSERT_FALSE(sec2_send_multicast_send_sc_followups);

  sec2_send_multicast_callback(TRANSMIT_COMPLETE_OK, 0, 0);

  TEST_ASSERT_EQUAL(2, sec2_send_multicast_nc_call);
  TEST_ASSERT_EQUAL(SECURITY_SCHEME_2_AUTHENTICATED,
      sec2_send_multicast_p->scheme);
  TEST_ASSERT_TRUE(nodemask_equal( sec2_send_multicast_p->node_list,c1 ) ==0);
  TEST_ASSERT_FALSE(sec2_send_multicast_send_sc_followups);

  sec2_send_multicast_callback(TRANSMIT_COMPLETE_OK, 0, 0);

  TEST_ASSERT_EQUAL(3, sec2_send_multicast_nc_call);
  TEST_ASSERT_EQUAL(SECURITY_SCHEME_2_ACCESS, sec2_send_multicast_p->scheme);
  TEST_ASSERT_TRUE(nodemask_equal( sec2_send_multicast_p->node_list,c2 ) ==0);
  TEST_ASSERT_FALSE(sec2_send_multicast_send_sc_followups);

  sec2_send_multicast_callback(TRANSMIT_COMPLETE_OK, 0, 0);

  /* Verify transmission to all classes with followup */
  TEST_ASSERT_EQUAL(4, sec2_send_multicast_nc_call);
  TEST_ASSERT_EQUAL(SECURITY_SCHEME_2_UNAUTHENTICATED,
      sec2_send_multicast_p->scheme);
  TEST_ASSERT_TRUE(nodemask_equal( sec2_send_multicast_p->node_list,c0 ) ==0);
  TEST_ASSERT_TRUE(sec2_send_multicast_send_sc_followups);

  sec2_send_multicast_callback(TRANSMIT_COMPLETE_OK, 0, 0);

  TEST_ASSERT_EQUAL(5, sec2_send_multicast_nc_call);
  TEST_ASSERT_EQUAL(SECURITY_SCHEME_2_AUTHENTICATED,
      sec2_send_multicast_p->scheme);
  TEST_ASSERT_TRUE(nodemask_equal( sec2_send_multicast_p->node_list,c1 ) ==0);
  TEST_ASSERT_TRUE(sec2_send_multicast_send_sc_followups);

  sec2_send_multicast_callback(TRANSMIT_COMPLETE_OK, 0, 0);

  TEST_ASSERT_EQUAL(6, sec2_send_multicast_nc_call);
  TEST_ASSERT_EQUAL(SECURITY_SCHEME_2_ACCESS, sec2_send_multicast_p->scheme);
  TEST_ASSERT_TRUE(nodemask_equal( sec2_send_multicast_p->node_list,c2 ) ==0);
  TEST_ASSERT_TRUE(sec2_send_multicast_send_sc_followups);

  /*Chekc that the callback arrives at the right place*/
  TEST_ASSERT_EQUAL(0, cb_count);

  sec2_send_multicast_callback(TRANSMIT_COMPLETE_OK, 0, 0);

  TEST_ASSERT_EQUAL(1, cb_count);

}
