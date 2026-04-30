/* Copyright Silicon Laboratories Inc.
 *
 */

#include <string.h>
#include <stdint.h>

#include "contiki/core/sys/ctimer.h"
#include "contiki/core/sys/clock.h"

#include "unity.h"

#include "ZW_SendRequest.h"
#include "ZW_SendDataAppl.h"
#include "ZW_transport_api.h"

/*
 * UNIT_TEST seams provided by ZW_SendRequest.c
 */
void *ZW_SendRequest_inject_waiting(nodeid_t snode,
                                    ZW_SendRequst_Callback_t callback,
                                    void *user);
int   ZW_SendRequest_pending_count(void);

void ctimer_set(struct ctimer *c, clock_time_t t, void (*f)(void *), void *ptr)
{ (void)c; (void)t; (void)f; (void)ptr; }
void ctimer_stop(struct ctimer *c) { (void)c; }
clock_time_t clock_time(void) { return 0; }

/* ZW_Abort_SendRequest does not call ZW_SendDataAppl, ts_param_make_reply,
 * or ts_param_cmp.  The symbols must still resolve at link time because
 * ZW_SendRequest() and SendRequest_ApplicationCommandHandler() (in the
 * same translation unit) reference them. */
uint8_t ZW_SendDataAppl(ts_param_t *p, const void *pData, uint16_t dataLength,
                        ZW_SendDataAppl_Callback_t callback, void *user)
{
  (void)p; (void)pData; (void)dataLength; (void)callback; (void)user;
  return 0;
}

void ts_param_make_reply(ts_param_t *dst, const ts_param_t *src)
{
  (void)dst; (void)src;
}

uint8_t ts_param_cmp(ts_param_t *a1, const ts_param_t *a2)
{
  (void)a1; (void)a2; return 0;
}

/* ------------------------------------------------------------------ */
/* Test bookkeeping                                                    */
/* ------------------------------------------------------------------ */
typedef struct {
  int     called;
  uint8_t status;
  void   *user;
} cb_record_t;

static int test_callback(BYTE txStatus, BYTE rxStatus,
                         ZW_APPLICATION_TX_BUFFER *pCmd, WORD cmdLength,
                         void *user)
{
  (void)rxStatus; (void)pCmd; (void)cmdLength;
  cb_record_t *r = (cb_record_t *)user;
  r->called++;
  r->status = txStatus;
  r->user   = user;
  return 0;
}

static cb_record_t cb_lr;
static cb_record_t cb_classic;

void setUp(void)
{
  memset(&cb_lr, 0, sizeof(cb_lr));
  memset(&cb_classic, 0, sizeof(cb_classic));
  ZW_SendRequest_init();
}

void tearDown(void) {}

/*
 * LR snode (257) must match an abort issued with the same nodeid_t value.
 * Pre-fix:  uint8_t param truncates 257 -> 1 -> no match -> no callback,
 *           list size stays at 2.
 * Post-fix: nodeid_t param matches, the LR session's callback fires with
 *           TRANSMIT_COMPLETE_FAIL, and the list shrinks to 1.
 */
void test_abort_matches_lr_snode_without_truncation(void)
{
  nodeid_t lr_node = 257;
  nodeid_t classic_node = 42;

  void *s_lr = ZW_SendRequest_inject_waiting(lr_node, test_callback, &cb_lr);
  void *s_classic = ZW_SendRequest_inject_waiting(classic_node, test_callback, &cb_classic);

  TEST_ASSERT_NOT_NULL_MESSAGE(s_lr, "memb_alloc for LR session failed");

  TEST_ASSERT_NOT_NULL_MESSAGE(s_classic, "memb_alloc for classic session failed");

  TEST_ASSERT_EQUAL_INT_MESSAGE(2, ZW_SendRequest_pending_count(),
                                "Pre-condition: two sessions queued");

  ZW_Abort_SendRequest(lr_node);

  TEST_ASSERT_EQUAL_INT_MESSAGE(1, cb_lr.called,
    "LR (snode=257) session must be aborted exactly once. "
    "Pre-fix: uint8_t truncates 257 -> 1, no match, callback never fires.");

  TEST_ASSERT_EQUAL_UINT8_MESSAGE(TRANSMIT_COMPLETE_FAIL, cb_lr.status,
    "Aborted session callback must receive TRANSMIT_COMPLETE_FAIL");

  TEST_ASSERT_EQUAL_INT_MESSAGE(0, cb_classic.called,
    "Classic (snode=42) session must NOT be aborted by ZW_Abort_SendRequest(257)");

  TEST_ASSERT_EQUAL_INT_MESSAGE(1, ZW_SendRequest_pending_count(),
    "Exactly one session must remain in reqs_list after the LR abort");
}
