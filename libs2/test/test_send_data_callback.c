/* SPDX-License-Identifier: LicenseRef-MSLA
 * SPDX-FileCopyrightText: Silicon Laboratories Inc. https://www.silabs.com
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "ZW_SendDataAppl.h"  /* public API + ts_param_t */
#include "ZW_transport_api.h" /* TRANSMIT_COMPLETE_*, TX_STATUS_TYPE */
#include "node_queue.h"       /* en_queue_state, QS_IDLE */
#include "zw_frame_buffer.h"  /* zw_frame_buffer_element_t */

/*
 * Test-seam declarations
 *
 * These are compiled into ZW_SendDataAppl.c only when -DUNIT_TEST is set
 * (which libs2/test/CMakeLists.txt sets globally with add_definitions).
 * -------------------------------------------------------------------------*/
void ZW_SendDataAppl_set_lock_ll(uint8_t v);
void ZW_SendDataAppl_set_resend_counter(uint8_t v);
uint8_t ZW_SendDataAppl_get_lock_ll(void);
void *ZW_SendDataAppl_alloc_session(void);
void ZW_SendDataAppl_push_session(void *s);
void *ZW_SendDataAppl_list_head(void);
void ZW_SendDataAppl_trigger_fail_for_test(void);

typedef struct {
  void *next;
  zw_frame_buffer_element_t *fb;
  void *user;
  ZW_SendDataAppl_Callback_t callback;
} test_session_t;

static int app_cb_count;
static uint8_t app_cb_status;
static void *app_cb_user;

static void spy_app_callback(uint8_t status, void *user, TX_STATUS_TYPE *tx) {
  (void)tx;
  app_cb_count++;
  app_cb_status = status;
  app_cb_user = user;
}

static int fb_free_count;
static zw_frame_buffer_element_t *fb_free_ptr;

void __wrap_zw_frame_buffer_free(zw_frame_buffer_element_t *e) {
  fb_free_count++;
  fb_free_ptr = e;
}

static int pp_count;
static process_event_t pp_last_event;

#define EXPECTED_SEND_NEXT_LL ((process_event_t)1)

int __wrap_process_post(struct process *p, process_event_t ev, void *data) {
  (void)p;
  (void)data;
  pp_count++;
  pp_last_event = ev;
  return 0;
}

enum en_queue_state __wrap_get_queue_state(void) { return QS_IDLE; }

void __wrap_process_exit(struct process *p) { (void)p; }
void __wrap_process_start(struct process *p, const char *arg) {
  (void)p;
  (void)arg;
}

void __wrap_etimer_stop(struct etimer *t) { (void)t; }
void __wrap_etimer_set(struct etimer *t, unsigned long i) {
  (void)t;
  (void)i;
}
int __wrap_etimer_expired(struct etimer *t) {
  (void)t;
  return 1;
}

void __wrap_sec0_abort_all_tx_sessions(void) {}
void __wrap_ima_send_data_done(uint16_t n, uint8_t s, TX_STATUS_TYPE *t) {
  (void)n;
  (void)s;
  (void)t;
}

static void reset_spies(void) {
  app_cb_count = 0;
  app_cb_status = 0xFF;
  app_cb_user = NULL;

  fb_free_count = 0;
  fb_free_ptr = NULL;

  pp_count = 0;
  pp_last_event = 0xFF;
}

/* -------------------------------------------------------------------------
 * ZW_SendDataAppl_init() sets lock=FALSE, lock_ll=FALSE, clears the list
 * and reinitialises the memb pool.  It also (re)starts the Contiki process,
 * which is stubbed to a no-op here.
 * -------------------------------------------------------------------------*/
void setUp(void) {
  reset_spies();
  ZW_SendDataAppl_init();
}

void tearDown(void) {}

void test_ts_should_pop_session_and_advance_when_transmit_complete_fail_and_resend_zero(void) {
  static zw_frame_buffer_element_t fb_a, fb_b;
  int sentinel = 0xBEEF;

  test_session_t *session_a = (test_session_t *)ZW_SendDataAppl_alloc_session();
  test_session_t *session_b = (test_session_t *)ZW_SendDataAppl_alloc_session();
  TEST_ASSERT_NOT_NULL_MESSAGE(session_a, "memb_alloc failed — session pool exhausted");
  TEST_ASSERT_NOT_NULL_MESSAGE(session_b, "memb_alloc failed — session pool exhausted");

  session_a->fb = &fb_a;
  session_a->user = &sentinel;
  session_a->callback = spy_app_callback;

  session_b->fb = &fb_b;
  session_b->user = NULL;
  session_b->callback = NULL;

  /* head = A, A->next = B */
  ZW_SendDataAppl_push_session(session_b);
  ZW_SendDataAppl_push_session(session_a);

  TEST_ASSERT_EQUAL_PTR_MESSAGE(session_a, ZW_SendDataAppl_list_head(),
                                "Pre-condition: A must be at the head before the failure");

  ZW_SendDataAppl_set_lock_ll(1);
  ZW_SendDataAppl_set_resend_counter(0);

  ZW_SendDataAppl_trigger_fail_for_test();

  TEST_ASSERT_EQUAL_PTR_MESSAGE(session_b, ZW_SendDataAppl_list_head(),
                                "Session B must be the head after session A is popped on FAIL");

  TEST_ASSERT_EQUAL_INT_MESSAGE(1, fb_free_count, "Exactly one buffer must be freed (session A's)");
  TEST_ASSERT_EQUAL_PTR_MESSAGE(&fb_a, fb_free_ptr,
                                "The freed buffer must be session A's, not B's");

  TEST_ASSERT_EQUAL_INT_MESSAGE(1, app_cb_count,
                                "Application callback must be called exactly once");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(TRANSMIT_COMPLETE_FAIL, app_cb_status,
                                  "Application callback must receive TRANSMIT_COMPLETE_FAIL");
  TEST_ASSERT_EQUAL_PTR_MESSAGE(&sentinel, app_cb_user,
                                "Application callback must receive the original user pointer");

  TEST_ASSERT_EQUAL_INT_MESSAGE(1, pp_count,
                                "SEND_EVENT_SEND_NEXT_LL must be posted after A is removed");
  TEST_ASSERT_EQUAL_MESSAGE(EXPECTED_SEND_NEXT_LL, pp_last_event,
                            "The posted event must be SEND_EVENT_SEND_NEXT_LL");
}

void test_ts_should_not_pop_session_when_resend_is_needed() {
  static zw_frame_buffer_element_t fb;

  test_session_t *s = (test_session_t *)ZW_SendDataAppl_alloc_session();
  TEST_ASSERT_NOT_NULL(s);
  s->fb = &fb;
  s->user = NULL;
  s->callback = spy_app_callback;
  ZW_SendDataAppl_push_session(s);

  ZW_SendDataAppl_set_lock_ll(1);
  ZW_SendDataAppl_set_resend_counter(1);

  ZW_SendDataAppl_trigger_fail_for_test();

  TEST_ASSERT_EQUAL_PTR_MESSAGE(s, ZW_SendDataAppl_list_head(),
                                "Session must remain in send_data_list when resend_counter > 0");

  TEST_ASSERT_EQUAL_INT_MESSAGE(0, fb_free_count,
                                "zw_frame_buffer_free must NOT be called when resend_counter > 0");

  TEST_ASSERT_EQUAL_INT_MESSAGE(0, app_cb_count,
                                "Application callback must NOT be called when resend_counter > 0");

  TEST_ASSERT_EQUAL_INT_MESSAGE(0, pp_count,
                                "process_post must NOT be called when resend_counter > 0");

  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, ZW_SendDataAppl_get_lock_ll(),
                                  "lock_ll must be FALSE after the callback (retry path)");
}
