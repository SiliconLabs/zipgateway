/* © 2019 Silicon Laboratories Inc. */

/* the basics */
#include "json.h"
#include <stdlib.h>
#include <time.h>
#include <string.h>
/* The stuff we are testing */
#include "zw_data.h"
#include "zgwr_json_parser.h"
#include "zgw_data.h"
#include "zgw_restore_cfg.h"

/* helpers */
#include "sys/clock.h"
#include "uip.h"
#include "lib/zgw_log.h"

#include "ZW_classcmd.h" //Z-Wave/include/
#include "zw_network_info.h" /* MyNodeID */

#include "test_helpers.h"
#include "test_restore_help.h"

/**
   Test plan

   Overview:
   - security keys
   - gateway data
   - node data, endpoint data, aggregated endpoints
   - uid rules
   - default values (uses minimal json file)
   - ip address and mdns cases (node, endpoints)
   - error cases errors in data, missing fields, inconsistent data
   - requirement checks
   - probe state downgrade rules
   - provisioning data


Security Keys
--------------------
1) the actual keys
  - that keys are read correctly
  - that one missing key is handled correctly
  - what happens if there are no keys at all
2) the key flags
  - for a SIS gateway
    - that the key flags are imported correctly
    - that missing key flags are handled correctly
  - for a non-SIS gateway
    - that the key flags are imported correctly
    - that missing key flags are handled correctly
3) the interaction
  - Unit test Handling of KEYS if gateway is SIS.
    - All keys and key flags set
    - All keys present, no key flags set
    - No keys present, all key flags set
    - All keys present, no key flag field (ie, use default key flags)
  - Unit test Handling of KEYS if gateway is not SIS.  
    - E.g., If a key field is not present, but the corresponding
      "grantedKeys" flag is set, that is an error if the gateway is
      not SIS.


Node data, endpoint data, aggregated endpoints
--------------------
  - Unit Test count of aggregated eps - Test aggr endpoints
  - Unit tests for restore eeprom and restore json-parse/eeprom fail on mac.
  - Unit test for versionCap (both hex and int)
  - that key flags are imported correctly for a node
  - what happens if the key flags are missing (but everything else is right)





====================
Gateway rules
====================
- Hard required rules:
From ZGW-1987
    grantedKeys is required for the gateway.

From ZGW-1987
    If no S2 keys is provided, the gateway will generate new S2 keys if its the SIS in the network.

From ZGW-2078
Security
 - If no security keys are provided in json, all keys should be generated if the GW is SIS. 
 - If the S0 key is provided, but no S2 keys, S2 keys should be generated(if SIS).
 - If all keys are provided, those keys should be used.
 - If GW is not SIS and is not granted all keys:
   - after restart GW, it should only get keys that are granted
   - a node in SIS network with a highest key not granted to GW will ignore GW's messages
   - GW should send and get a response from a node on a granted level

====================
Node rules
====================
- Downgrade rules:
From ZGW-1987 Define default behaviour in case of unknown information"
    zgwNodeData.grantedKeys:    -If this field is not, we MUST set probe state to re-interview. 
    grantedKeys is optional for network nodes, If the field is not given, node state will be forced to RE-INTERVIEW.
    nodeProdId: requires the sub properties, if this is not set we MUST re-interview.
    zgwZWNodeData.endpoints: MUST have an endpoint with epid 0, if not we MUST re-interview.
    zgwZWNodeData.endpoints.endpointId: required
    zgwZWNodeData.endpoints.state: "Default 
    zgwZWNodeData.endpoints.installerIcon: default 0
    zgwZWNodeData.endpoints.userIcon: default 0

    zgwZWNodeData.endpoints.endpointInfoFrames: if not defined for an endpoint  MUST re-interview.
    zgwZWNodeData.probeState: default "Re-interview"
    zgwZWNodeData.version_cap: is not defined zws_probe = 0
    zwNodeData.nodeID : required
     
    zgwZWNodeData.liveness.wakeUpInterval: default 4200, if this is set wrong a node wakepu node will have its interval reprobed when it wakes up.

- Default rules:
  From ZGW-1987:
    zgwZWNodeData.endpoints.endpointAggregation: default ""
    zgwZWNodeData.liveness.wakeUpInterval: default 4200  
    zgwZWNodeData.liveness.lastAwake: default 0
    zgwZWNodeData.liveness.lastUpdate: default 0
    zgwZWNodeData.ipData: default "", mdns will figure out the rest.
    grantedKeys is optional for network nodes, default is 0.
    rd_probe_flags should default to 0.

- Derived fields
From ZGW-1987
    The the granted keys is set then RD_NODE_FLAG_ADDED_BY_ME should also be set.
    grantedKeys is optional for network nodes, If the field is given, ADDED_BY_ME flag should be set.
    ep->state should be EP_STATE_PROBE_INFO is the json info is incomplete, otherwise it should be EP_STATE_PROBE_DONE. Right now it does not matter since the GW will change this at boot.
    if ZWavePlusRole is portable, the flag  RD_NODE_FLAG_PORTABLE should be set.
    If node probe state is DONE the we must set RD_NODE_FLAG_PROBE_HAS_COMPLETED in n->probe_flags. Otherwise the flags should be RD_NODE_PROBE_NEVER_STARTED


- Node data consistency rules:
From ZGW-1987
    zgwZWNodeData.endpoints.endpointAggregation: if this field is set
      the endpointInfoFrames should be in the restricted list. The list
      of endpoints must point to defined endpoints.
*/


/* as in contiki */
#define UIP_NTOHL UIP_HTONL
#define test_homeID UIP_NTOHL(0xfab5b0b0)

zgw_log_id_define(tst_js_parse);
zgw_log_id_default_set(tst_js_parse);

zw_controller_t *ctrl;

zw_node_data_t gw_node_data;
zw_node_type_t gw_type = {BASIC_TYPE_STATIC_CONTROLLER, // 2
                          GENERIC_TYPE_STATIC_CONTROLLER, // 2
                          SPECIFIC_TYPE_GATEWAY}; // 7
char* gw_name = "gateway";
zgw_node_probe_state_t gw_probe_state = { STATUS_DONE,
                                         node_do_not_interview,
                                         1, 0, 0, 1,
                                         8, NULL} ;
struct provision gw_provision = {NULL, 0, 0, 0, NULL};
zgw_node_liveness_data_t gw_liveness = {ZGW_NODE_OK,
                                        5000, // wui default
                                        0, // lastAwake
                                        0}; // last probe

/* Node 2 */
zw_node_data_t S0_node_data;
zw_node_type_t sensor_type = {BASIC_TYPE_SLAVE, //3
                              GENERIC_TYPE_SENSOR_MULTILEVEL, // 0x21
                              SPECIFIC_TYPE_NOT_USED}; // 0
char* node2_name = "device";
cc_version_pair_t node2_cc_vers[] = {{COMMAND_CLASS_VERSION, 0x0}, // 0x86, 134
                                     {COMMAND_CLASS_ZWAVEPLUS_INFO, 0x0}, // 0x5e,94
                                     {COMMAND_CLASS_MANUFACTURER_SPECIFIC, 0x0}, //0x72,114
                                     {COMMAND_CLASS_WAKE_UP, 0x0}, // 0x84,132
                                     {COMMAND_CLASS_MULTI_CHANNEL_V4, 0x0}, // 0x60,96
                                     {COMMAND_CLASS_ASSOCIATION, 0x0}, // 0x85, 133
                                     {COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, 0x0}, // 0x8e,142
                                     {0xffff, 0xff}};;
zgw_node_probe_state_t node2_probe_state = {STATUS_DONE,
                                            node_do_not_interview,
                                            1, 0,
                                            RD_NODE_FLAG_ADDED_BY_ME, 1,
                                            8,
                                            node2_cc_vers};
struct provision node_provision = {NULL, 0, 0, 0, NULL};
zgw_node_liveness_data_t node2_liveness = {ZGW_NODE_OK,
                                           1000, // wui
                                           0, // lastAwake
                                           0}; // last probe

zw_node_data_t nonSec_node_data;
zw_node_data_t nonSec_node2_data;
zw_node_data_t nonSec_node3_data;

/* Initializers */
//#define ICON_TYPE_GENERIC_SENSOR_NOTIFICATION 0x0C00   //Sensor Notification Device//#define ICON_TYPE_GENERIC_SENSOR_MULTILEVEL 0x0D00
//#define ICON_TYPE_SPECIFIC_SENSOR_MULTILEVEL_LUMINANCE 0x0D03   //Sensor Multilevel Device Type (Sensor type Luminance)
//#define ICON_TYPE_SPECIFIC_SENSOR_MULTILEVEL_POWER 0x0D04   //Sensor Multilevel Device Type (Sensor type Power)

void setup_node2_data(zgw_node_data_t *exp_data) {
   exp_data->uid_type = node_uid_dsk;

   exp_data->node_gwdata.liveness = node2_liveness;

   exp_data->node_gwdata.zw_node_data.node_type = sensor_type;
   exp_data->node_gwdata.zw_node_data.capability = 1;
   exp_data->node_gwdata.zw_node_data.security = 2;

   exp_data->node_gwdata.security_flags = 0x01; /* S0 */

   exp_data->node_gwdata.probe_state = node2_probe_state;
   exp_data->node_gwdata.node_prod_id.manufacturerID = 1;
   exp_data->node_gwdata.node_prod_id.productType = 0x8181; // 33153
   exp_data->node_gwdata.node_prod_id.productID = 3;
   exp_data->node_gwdata.nEndpoints = 3;
   exp_data->node_gwdata.nAggEndpoints = 1;
}

//#define ICON_TYPE_GENERIC_GATEWAY 0x0500
void setup_gw_node_data(zgw_node_data_t *exp_data) {
   exp_data->uid_type = node_uid_zw_net_id;

   exp_data->node_uid.net_uid.homeID = test_homeID;
   exp_data->node_uid.net_uid.node_id = 2;

   exp_data->node_gwdata.liveness = gw_liveness;

   exp_data->node_gwdata.zw_node_data.neighbors = NULL;
   exp_data->node_gwdata.zw_node_data.capability = 1;
   exp_data->node_gwdata.zw_node_data.security = 1;
   exp_data->node_gwdata.zw_node_data.node_type = gw_type;

   exp_data->node_gwdata.probe_state = gw_probe_state;

   exp_data->node_gwdata.node_prod_id.manufacturerID = 1;
   exp_data->node_gwdata.node_prod_id.productType = 2;
   exp_data->node_gwdata.node_prod_id.productID = 3;

   exp_data->node_gwdata.security_flags = 0x71;

   exp_data->node_gwdata.nEndpoints = 2;
   exp_data->node_gwdata.nAggEndpoints = 1;

   LIST_STRUCT_INIT(&(exp_data->node_gwdata), endpoints);

   exp_data->ip_data.mDNS_node_name_len = 8;
   exp_data->ip_data.mDNS_node_name = gw_name;
   //   exp_data->ip_data.ipv6_address = ;
   exp_data->ip_data.lease = 0;

   exp_data->pvs_data.num_tlvs = 0;
   exp_data->pvs_data.pvs = gw_provision;

}

/*** verifiers ***/
/** Check a node that is in a z-wave network (not just provisioned) */
void check_node_data(uint8_t node_id,
                     const zgw_node_data_t *exp_gw_node_data);

void check_node_data(uint8_t node_id,
                     const zgw_node_data_t *exp_gw_node_data) {
   const zgw_node_data_t *node = zgw_node_data_get(node_id);
   const zgw_node_zw_data_t *gwdata;
   const zgw_node_zw_data_t *exp_gwdata;

   test_print(2, "Checking node %d\n", node_id);

   check_not_null((void*)node,"Found node data\n");
   if (!node) {
      return;
   }
   gwdata =  &(node->node_gwdata);
   exp_gwdata = &(exp_gw_node_data->node_gwdata);
   /* check id */

   check_true(node->uid_type == node_uid_dsk ||
              node->node_uid.net_uid.node_id == node_id,
              "Node has legal uid");

   /* check node_gwdata */
   /* node_gwdata.liveness */
   check_true(gwdata->liveness.liveness_state
              == exp_gwdata->liveness.liveness_state,
              "node liveness state checked");
   test_print(3, "wui: %u\n", gwdata->liveness.wakeUp_interval);
   check_true(gwdata->liveness.wakeUp_interval
              == exp_gwdata->liveness.wakeUp_interval,
              "node wakeUp_interval checked");
   /* lastAwake; Timestamp of the last frame received from
      the node. Not imported, faked during restart. */
   //   check_true(gwdata->liveness.lastAwake //
   check_true(gwdata->liveness.lastUpdate == 0,
              "node lastUpdate set to 0");

   /* node_gwdata.zw_node_data */
   check_true(gwdata->zw_node_data.node_id == node_id,
              "node id is correct Z-Wave node id");
   test_print(3, "basic, generic, specific: %d, %d, %d, exp %d, %d, %d\n",
              gwdata->zw_node_data.node_type.basic,
              gwdata->zw_node_data.node_type.generic,
              gwdata->zw_node_data.node_type.specific,
              exp_gwdata->zw_node_data.node_type.basic,
              exp_gwdata->zw_node_data.node_type.generic,
              exp_gwdata->zw_node_data.node_type.specific);
   check_true(gwdata->zw_node_data.node_type.basic
              == exp_gw_node_data->node_gwdata.zw_node_data.node_type.basic,
              "node type basic checked");
   check_true(gwdata->zw_node_data.node_type.generic
              == exp_gw_node_data->node_gwdata.zw_node_data.node_type.generic,
              "node type generic checked");
   check_true(gwdata->zw_node_data.node_type.specific
              == exp_gw_node_data->node_gwdata.zw_node_data.node_type.specific,
              "node type specific checked");
   check_true(gwdata->zw_node_data.capability
              == exp_gwdata->zw_node_data.capability,
              "node capability checked");
   check_true(gwdata->zw_node_data.security
              == exp_gwdata->zw_node_data.security,
              "node \"security\" checked");

   check_true(gwdata->probe_state.state == STATUS_DONE,
              "Node has state DONE");

   /* node_gwdata.node_prod_id */
   test_print(3, "manufac id %u, prod type %u, prod id %u\n",
              gwdata->node_prod_id.manufacturerID,
              gwdata->node_prod_id.productType,
              gwdata->node_prod_id.productID);
   check_true(gwdata->node_prod_id.manufacturerID
              == exp_gwdata->node_prod_id.manufacturerID,
              "manufacturerID is correct");
   check_true(gwdata->node_prod_id.productType
              == exp_gwdata->node_prod_id.productType,
              "productType is correct");
   check_true(gwdata->node_prod_id.productID
              == exp_gwdata->node_prod_id.productID,
              "productID is correct");

   test_print(3, "Read node security flags: 0x%02x\n", gwdata->security_flags);
   check_true(gwdata->security_flags == exp_gwdata->security_flags,
              "Node security flags set correctly");

   /* check ep 0 exists) */
   check_true(gwdata->nEndpoints > 0, "Node has ep 0");

   for (uint8_t epid = 0; epid < gwdata->nEndpoints; epid++) {
      const zgw_node_ep_data_t *zgw_node_ep_data = zgw_node_ep_data_get(node_id, epid);
      check_not_null((void*)zgw_node_ep_data, "ep data exists\n");
      if (zgw_node_ep_data == NULL) {
         break;
      }
      /* TODO: more ep checks */
      if (epid >= (gwdata->nEndpoints - gwdata->nAggEndpoints)) {
         test_print(2, "ep %u is an aggregation, len %u, 0x%02x etc\n",
                    epid,
                    zgw_node_ep_data->endpoint_aggr_len,
                    zgw_node_ep_data->endpoint_agg[0]);
         /* this is an aggregated ep */
         check_not_null(zgw_node_ep_data->endpoint_agg,
                        "Aggregated ep contains aggregation\n");
         check_true(zgw_node_ep_data->endpoint_aggr_len > 0,
                        "ep aggregation len is set");
         //check_true(zgw_node_ep_data->endpoint_info_len == 0,
         //           "aggr ep info len is 0.");
         //check_null(zgw_node_ep_data->endpoint_info,
         //           "aggr ep NIF ptr is NULL");
      } else {
         test_print(2, "ep %u is not an aggregation\n", epid);
         check_true(zgw_node_ep_data->endpoint_info_len > 0,
                    "ep info len is set.");
         check_not_null(zgw_node_ep_data->endpoint_info,
                        "ep NIF ptr is set");
         check_null(zgw_node_ep_data->endpoint_agg,
                    "Regular ep does not contain aggregation\n");
         check_true(zgw_node_ep_data->endpoint_aggr_len == 0,
                    "ep aggregation len is 0");
      }
   }

/* check pvs data (null or valid) */

/* check ip data */

}

/* TODO */
void test_security_reader() {
/* x 1 Happy case */
/* 2 Too short
 * 3 too long
 * 4 non-(hex)numeric
 * 5 odd length string
 * 6 empty string
 * 7 key not found
 * Deliberately missing keys (eg, only S0, so that gw can generate S2 keys)

 * NULL json object is not supported, so that does not need to be
 * tested (lint can catch that).
 */
   return;
}

/* Test scenarios: */
/* - basic node data */
/* - wakeup node data and mailbox stuff */
/* - endpoints */
/* - aggr endpoints */
/* - import of controller data */

void test_init() {
   /* zero the global */
   uint8_t nodeID = 1;
   uint8_t SUCid = 1;
   uint8_t *cc_list=NULL; /* Not needed, so 0 is OK */

   cfg_init();

   /* Mock what the serial reader does. */
   MyNodeID = nodeID;
   homeID = test_homeID;

   zgw_data_init(NULL,
                 NULL, NULL, NULL);

   ctrl = zw_controller_init(1);
   check_not_null(ctrl, "Initialized controller");
   check_true(check_mock_controller(ctrl) == 0,
              "mock controller generated correctly");

   /* Add the gateway */
   gw_node_data.node_id = 1;
   gw_node_data.capability = 1;
   gw_node_data.security = 1;
   gw_node_data.neighbors = NULL;
   gw_node_data.node_type = gw_type;
   zw_controller_add_node(ctrl, 1, &gw_node_data);


   S0_node_data.node_id = 2;
   S0_node_data.capability = 1;
   S0_node_data.security = 2; /* TODO */
   S0_node_data.neighbors = NULL;
   S0_node_data.node_type = sensor_type;
   zw_controller_add_node(ctrl, 2, &S0_node_data);

   /* re-interview node */
   nonSec_node_data.node_id = 4;
   nonSec_node_data.capability = 1;
   nonSec_node_data.security = 3; /* TODO */
   nonSec_node_data.neighbors = NULL;
   nonSec_node_data.node_type = sensor_type;
   zw_controller_add_node(ctrl, nonSec_node_data.node_id, &nonSec_node_data);

   /* buggy node */
   nonSec_node2_data.node_id = 6;
   nonSec_node2_data.capability = 1;
   nonSec_node2_data.security = 3; /* TODO */
   nonSec_node2_data.neighbors = NULL;
   nonSec_node2_data.node_type = sensor_type;
   zw_controller_add_node(ctrl, nonSec_node2_data.node_id, &nonSec_node2_data);

   /* no eps re-interview node */
   nonSec_node3_data.node_id = 8;
   nonSec_node3_data.capability = 1;
   nonSec_node3_data.security = 3; /* TODO */
   nonSec_node3_data.neighbors = NULL;
   nonSec_node3_data.node_type = sensor_type;
   zw_controller_add_node(ctrl, nonSec_node3_data.node_id, &nonSec_node3_data);
}

/* This function steals the filename */
int test_setup(char* filename) {
   json_object *zgw_backup_obj = NULL;
   int res = 0; /* aka success */

   restore_cfg.json_filename = filename;

   test_print(3, "controller %p, home ID 0x%04x, nodeID %d\n",
              ctrl, homeID, MyNodeID);

   // 1. Read file into json
   zgw_backup_obj = zgw_restore_json_read();

   check_not_null(zgw_backup_obj, "Reading file into json object.");
   if (!zgw_backup_obj) {
      return -1;
   }

   // 2. Parse json into internal data structure
   test_print(2, "Parsing json object\n");
   res = zgw_restore_parse_backup(zgw_backup_obj, ctrl);
   check_true(res == 0,
              "Parsed json object into gateway structure.");

   return res;
}

int main(void) {
   char *file1 = "json_parse.log";
   verbosity = test_case_start_stop;

   /* Start the logging system at the start of main. */
   zgw_log_setup(file1);

   /* Log that we have entered this function. */
   zgw_log_enter();

   /* Log at level 1 */
   zgw_log(1, "ZGWlog test arg %d\n", 7);

   test_init();

   if (ctrl != NULL) {
      check_true(test_setup( TEST_SRC_DIR "/test.json") == 0,
                 "Parsed file1 into internals");
   }

   zip_gateway_backup_data_t * bu1 = zip_gateway_backup_data_unsafe_get();
   check_not_null(bu1, "Accessed global backup data");

   check_true(bu1->manifest.backup_version_major == 1 &&
              bu1->manifest.backup_version_minor == 0 &&
              bu1->manifest.backup_timestamp == 1234567,
              "Backup meta-data imported corerctly");

   const zw_controller_t * zw_ctrl = zgw_controller_zw_get();
   if (zw_ctrl == NULL) {
      check_true(0, "Controller object has been created");
   } else {
      check_true(zw_ctrl->SUCid == 1,
                 "SUC id read correctly");
   }
   zgw_data_t *zgw = &(bu1->zgw);
   const zgw_node_data_t *gw_node_data = zgw_node_data_get(MyNodeID);
   const zgw_node_data_t *node2_data = zgw_node_data_get(2);
   zgw_node_data_t exp_gw_node_data;
   zgw_node_data_t exp_node2_data;
   const zip_lan_ip_data_t *lan_data = zip_lan_ip_data_get();
   const zip_pan_data_t *zw_pan_data = &(zgw->zip_pan_data);
   uint8_t exp_S0_key[16] = {0x99, 0x16,0xFE,0xDC,0xBA,0x12,0x34,0x56
                             ,0x12,0x34,0x56,0x78,0x12,0x34,0x56,0x78};
   uint8_t exp_S2_C0_key[16] = {0xFE, 0x6B, 0x48, 0xD7, 0x4A, 0x43, 0x5C, 0x8C,
                                0xEF, 0x0C, 0xE8, 0xE4, 0x12, 0x7D, 0xF5, 0x1D};
   uint8_t exp_S2_C1_key[16] = {0xAB, 0xCD, 0xEF, 0x00, 0x87, 0x65, 0x43, 0x21,
                                0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x56, 0x78};
   uint8_t exp_S2_C2_key[16] = {0x12, 0x34, 0x56, 0x78, 0x90, 0x12, 0x34, 0x56,
                                0x87, 0x65, 0x43, 0x21, 0xAB, 0xCD, 0xEF, 0x01};
   uint8_t exp_S2_C1_LR_key[16] = {0x4B, 0x63, 0x12, 0xE0, 0x42, 0x22, 0x24, 0xB1,
                                   0xE0, 0x95, 0x98, 0x52, 0x65, 0x06, 0xC9, 0x1C};
   uint8_t exp_S2_C2_LR_key[16] = {0x05, 0xE5, 0x11, 0x15, 0xA6, 0xF7, 0x2C, 0xDE,
                                   0x7C, 0x18, 0x40, 0x67, 0x45, 0xE7, 0x4A, 0xE2};
   setup_gw_node_data(&exp_gw_node_data);
   setup_node2_data(&exp_node2_data);

   check_not_null((void*)gw_node_data, "Accessed gw data");
   if (gw_node_data) {
      check_node_data(MyNodeID,
                      &exp_gw_node_data);
   }
   check_not_null((void*)lan_data, "Accessed lan data");
   check_not_null((void*)zw_pan_data, "Accessed zw net data");
   test_print(3, "Read assigned keys: 0x%02x\n",
              zw_pan_data->zw_security_keys.assigned_keys);
   check_true(zw_pan_data->zw_security_keys.assigned_keys == 0x9F,
              "Imported assigned keys correctly");
   check_true(memcmp(zw_pan_data->zw_security_keys.security_netkey,
                     exp_S0_key, 16) == 0,
              "Imported S0 key correctly");
   check_true(memcmp(zw_pan_data->zw_security_keys.security2_key[0],
                     exp_S2_C0_key, 16) == 0,
              "Imported S2_C0 key correctly");
   check_true(memcmp(zw_pan_data->zw_security_keys.security2_key[1],
                     exp_S2_C1_key, 16) == 0,
              "Imported S2_C1 key correctly");
   check_true(memcmp(zw_pan_data->zw_security_keys.security2_key[2],
                     exp_S2_C2_key, 16) == 0,
              "Imported S2_C2 key correctly");
   check_true(memcmp(zw_pan_data->zw_security_keys.security2_lr_key[0],
                     exp_S2_C1_LR_key, 16) == 0,
              "Imported S2_C1 LR key correctly");
   check_true(memcmp(zw_pan_data->zw_security_keys.security2_lr_key[1],
                     exp_S2_C2_LR_key, 16) == 0,
              "Imported S2_C2 LR key correctly");

   check_not_null((void*)node2_data, "Accessed device data");
   if (gw_node_data) {
      check_node_data(2, &exp_node2_data);
   }

   check_true(zip_gateway_backup_data_sanitize() == false,
              "Sanitizer found no errors in bu data.");

   close_run();

   /* Close down the logging system at the end of main(). */
   zgw_log_teardown();

   return numErrs;
}
