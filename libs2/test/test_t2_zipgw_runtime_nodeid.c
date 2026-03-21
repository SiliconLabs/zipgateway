/* Copyright Silicon Laboratories Inc.
 *
 * Build Transport Service with ZIPGW and without NEW_TEST_T2, then verify
 * that LR node IDs are preserved on the TS -> ZW_SendData_Bridge call path.
 */

#include <stdint.h>
#include <string.h>

// clang-format off
#include "unity.h"
#include "MockZW_transport_api.h"
// clang-format on

#include "S2.h"
#include "transport_service2.h"

BYTE MyNodeID = 1;

void setUp(void) { MockZW_transport_api_Init(); }

void tearDown(void) {
  MockZW_transport_api_Verify();
  MockZW_transport_api_Destroy();
}

struct ctimer;
typedef unsigned long clock_time_t;
void ctimer_set(struct ctimer *c, clock_time_t t, void (*f)(void *), void *ptr) {
  (void)c;
  (void)t;
  (void)f;
  (void)ptr;
}

void ctimer_stop(struct ctimer *c) { (void)c; }

clock_time_t clock_time(void) { return 0; }

uint16_t zgw_crc16(uint16_t crc16, uint8_t *data, unsigned long data_len) {
  (void)data;
  (void)data_len;
  return crc16;
}

static void app_cmd_handler(ts_param_t *p, ZW_APPLICATION_TX_BUFFER *pCmd, uint16_t cmdLength) {
  (void)p;
  (void)pCmd;
  (void)cmdLength;
}

static void status_cb(uint8_t txStatus, TX_STATUS_TYPE *txStatusReport) {
  (void)txStatus;
  (void)txStatusReport;
}

void test_zipgw_ts_preserves_lr_destination_nodeid(void) {
  ts_param_t p;
  uint8_t payload[64];

  memset(&p, 0, sizeof(p));
  memset(payload, 0xAA, sizeof(payload));

  p.snode = 0x00ff;
  p.dnode = 257;
  p.tx_flags = TRANSMIT_OPTION_ACK | TRANSMIT_OPTION_AUTO_ROUTE | TRANSMIT_OPTION_EXPLORE;

  ZW_SendData_Bridge_ExpectAndReturn(p.snode, p.dnode, NULL, 0, 0, NULL, 1);
  ZW_SendData_Bridge_IgnoreArg_pData();
  ZW_SendData_Bridge_IgnoreArg_dataLength();
  ZW_SendData_Bridge_IgnoreArg_txOptions();
  ZW_SendData_Bridge_IgnoreArg_completedFunc();

  ZW_TransportService_Init(app_cmd_handler);
  TEST_ASSERT_TRUE_MESSAGE(ZW_TransportService_SendData(&p, payload, sizeof(payload), status_cb),
                           "ZW_TransportService_SendData returned false");
}
