/* © 2018 Silicon Laboratories Inc.  */

#include "RD_probe_cc_version.h"
#include "RD_DataStore.h"
#include "RD_types.h"
#include "ZW_classcmd_ex.h"
#include "test_helpers.h"
#include "ZW_SendDataAppl.h"
#include <string.h>
#include <stdlib.h>
#include <lib/zgw_log.h>
#include "provisioning_list.h"

/**
\defgroup probe_cc_version_test CC Version probing unit test.

Test Plan

Test state transition and version get/set helper

Basic
- Given versions and NIF, see if state transits correctly
  and the required version are all probed
- get/set version for controlled CC
- get/set version for non controlled CC

*/

/* stub */
struct pvs_tlv * provisioning_list_tlv_dsk_get(uint8_t dsk_len,
                                               const uint8_t *dsk, uint8_t type) {
   return NULL;
}

/* Global */
ZW_VERSION_COMMAND_CLASS_REPORT_FRAME zw_v_cc_report = {COMMAND_CLASS_VERSION,
                                                        VERSION_COMMAND_CLASS_REPORT,
                                                        0, 0};
ZW_VERSION_CAPABILITIES_REPORT_V3_FRAME zw_v_cap_report = {COMMAND_CLASS_VERSION,
                                                           VERSION_CAPABILITIES_REPORT,
                                                           0x07};
ZW_VERSION_ZWAVE_SOFTWARE_REPORT_V3_FRAME zw_v_zws_report = {COMMAND_CLASS_VERSION,
                                                             VERSION_ZWAVE_SOFTWARE_REPORT,
                                                             0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

BOOL is_callback_called = FALSE;

/* Index for the global test data */
rd_node_database_entry_t node1 = {0};
rd_ep_database_entry_t ep1 = {0};
cc_version_pair_t cc_version_case1[] = {{COMMAND_CLASS_VERSION, 0x03},
                                        {COMMAND_CLASS_ZWAVEPLUS_INFO, 0x02},
                                        {COMMAND_CLASS_MANUFACTURER_SPECIFIC, 0x02},
                                        {COMMAND_CLASS_WAKE_UP, 0x02},
                                        {COMMAND_CLASS_MULTI_CHANNEL_V4, 0x04},
                                        {COMMAND_CLASS_ASSOCIATION, 0x02},
                                        {COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, 0x03},
                                        {0xffff, 0xff}};

void cc_version_callback(BYTE txStatus, BYTE rxStatus, ZW_APPLICATION_TX_BUFFER *pCmd, WORD cmdLength, void* user);
void version_capabilities_callback(BYTE txStatus, BYTE rxStatus, ZW_APPLICATION_TX_BUFFER *pCmd, WORD cmdLength, void* user);
void version_zwave_software_callback(BYTE txStatus, BYTE rxStatus, ZW_APPLICATION_TX_BUFFER *pCmd, WORD cmdLength, void* user);

/* Mock */

void ts_set_std(ts_param_t* p, nodeid_t dnode)
{
}

int rd_ep_class_support(rd_ep_database_entry_t* ep, uint16_t cls)
{
  return 1;
}

typedef void( *ZW_SendRequst_Callback_t)(
        BYTE txStatus,
        BYTE rxStatus,
        ZW_APPLICATION_TX_BUFFER *pCmd,
        WORD cmdLength,
        void* user) CC_REENTRANT_ARG;

BYTE ZW_SendRequest(ts_param_t* p, const BYTE *pData,
    BYTE dataLength, BYTE responseCmd, WORD timeout,
    void* user, ZW_SendRequst_Callback_t callback)
{
  return TRUE;
}

void probe_cc_version_callback(void *n, uint8_t status_code)
{
  is_callback_called = TRUE;
}

void test_state_transition_init(rd_ep_database_entry_t *ep, rd_node_database_entry_t *n)
{
  n->nodeid = 0x0f;
  n->mode = 0;
  n->node_version_cap_and_zwave_sw = 0;
  n->probe_flags = 0;
  is_callback_called = FALSE;
  n->node_cc_versions_len = controlled_cc_v_size();
  n->node_cc_versions = malloc(n->node_cc_versions_len);
  rd_node_cc_versions_set_default(n);
  ep->node = n;
}

void test_state_transition_teardown(rd_ep_database_entry_t *ep, rd_node_database_entry_t *n)
{
  free(n->node_cc_versions);
}

FILE *log_strm = NULL;

static void test_state_transition(void);

int main()
{
    test_state_transition();

    close_run();
    return numErrs;
}

static void cc_version_callback_wrapper(cc_version_pair_t version_list[], uint8_t command_class)
{
  zw_v_cc_report.requestedCommandClass = command_class;
  zw_v_cc_report.commandClassVersion = 0;
  int idx = 0;
  while(version_list[idx].command_class != 0xffff) {
    if(version_list[idx].command_class == command_class) {
      zw_v_cc_report.commandClassVersion = version_list[idx].version;
      break;
    }
    idx++;
  }
  cc_version_callback(TRANSMIT_COMPLETE_OK, 0, (ZW_APPLICATION_TX_BUFFER *)&zw_v_cc_report, 0, &(ep1));
}

static void test_state_transition(void) {

   start_case("Basic state transition", log_strm);
   test_state_transition_init(&ep1, &node1);

   rd_ep_probe_cc_version(&ep1, probe_cc_version_callback);
   ProbeCCVersionState_t *pcvs = node1.pcvs;

   check_true(pcvs->state == PCV_SEND_VERSION_CC_GET, "Expect the state to be PCV_SEND_VERSION_CC_GET");
   check_true(pcvs->probe_cc_idx == 0, "Expect the requested CMD to be VERSION_CC_GET(VERSION)");
   check_true(node1.probe_flags == RD_NODE_FLAG_PROBE_STARTED, "Expect the probe_flag to be RD_NODE_FLAG_PROBE_STARTED");
   cc_version_callback_wrapper(cc_version_case1, COMMAND_CLASS_VERSION);

   check_true(pcvs->state == PCV_SEND_VERSION_CC_GET, "Expect the state to be PCV_SEND_VERSION_CC_GET");
   check_true(pcvs->probe_cc_idx == 1, "Expect the requested CMD to be VERSION_CC_GET(ZWAVEPLUS_INFO)");
   cc_version_callback_wrapper(cc_version_case1, COMMAND_CLASS_ZWAVEPLUS_INFO);

   check_true(pcvs->state == PCV_SEND_VERSION_CC_GET, "Expect the state to PCV_SEND_VERSION_CC_GET");
   check_true(pcvs->probe_cc_idx == 2, "Expect the requested CMD to be VERSION_CC_GET(MANUFACTURER_SPECIFIC)");
   cc_version_callback_wrapper(cc_version_case1, COMMAND_CLASS_MANUFACTURER_SPECIFIC);

   check_true(pcvs->state == PCV_SEND_VERSION_CC_GET, "Expect the state to be PCV_SEND_VERSION_CC_GET");
   check_true(pcvs->probe_cc_idx == 3, "Expect the requested CMD to be VERSION_CC_GET(WAKE_UP)");
   cc_version_callback_wrapper(cc_version_case1, COMMAND_CLASS_WAKE_UP);

   check_true(pcvs->state == PCV_SEND_VERSION_CC_GET, "Expect the state to be PCV_SEND_VERSION_CC_GET");
   check_true(pcvs->probe_cc_idx == 4, "Expect the requested CMD to be VERSION_CC_GET(MULTI_CHANNEL_V4)");
   cc_version_callback_wrapper(cc_version_case1, COMMAND_CLASS_MULTI_CHANNEL_V4);

   check_true(pcvs->state == PCV_SEND_VERSION_CC_GET, "Expect the state to be PCV_SEND_VERSION_CC_GET");
   check_true(pcvs->probe_cc_idx == 5, "Expect the requested CMD to be VERSION_CC_GET(ASSOCIATION)");
   cc_version_callback_wrapper(cc_version_case1, COMMAND_CLASS_ASSOCIATION);

   check_true(pcvs->state == PCV_SEND_VERSION_CC_GET, "Expect the state to be PCV_SEND_VERSION_CC_GET");
   check_true(pcvs->probe_cc_idx == 6, "Expect the requested CMD to be VERSION_CC_GET(MULTI_CHANNEL_ASSOCIATION_V3)");
   cc_version_callback_wrapper(cc_version_case1, COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3);

   check_true(pcvs->state == PCV_SEND_VERSION_CAP_GET, "Expect the state to be PCV_SEND_VERSION_CAP_GET");
   check_true(pcvs->probe_cc_idx == 7, "The controlled command list should have reached the last one");
   version_capabilities_callback(TRANSMIT_COMPLETE_OK, 0, (ZW_APPLICATION_TX_BUFFER *)&zw_v_cap_report, 0, &(ep1));

   check_true(pcvs->state == PCV_SEND_VERSION_ZWS_GET, "State should be at PCV_SEND_VERSION_ZWS_GET and wait for VERSION_ZWS_REPORT sending back");
   version_zwave_software_callback(TRANSMIT_COMPLETE_OK, 0, (ZW_APPLICATION_TX_BUFFER *)&zw_v_zws_report, 0, &(ep1));

   check_mem((uint8_t*)cc_version_case1, (uint8_t*)node1.node_cc_versions, sizeof(cc_version_case1), "Unexpected version stored", "");
   check_true(pcvs->state == PCV_VERSION_PROBE_DONE, "State should be finished at PCV_VERSION_PROBE_DONE");
   check_true(node1.node_version_cap_and_zwave_sw == 0x07, "Expect the stored version zwave software to be 0x07");
   check_true(node1.probe_flags == RD_NODE_FLAG_PROBE_HAS_COMPLETED, "Expect the probe_flag to be RD_NODE_FLAG_PROBE_HAS_COMPLETED");
   check_true(is_callback_called, "Check if the callback comes back");

   test_state_transition_teardown(&ep1, &node1);
   close_case("Basic state transition");
}
