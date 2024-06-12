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
#include "zgwr_eeprom.h"
#include "dev/eeprom.h"

/* helpers */
#include "sys/clock.h"
#include "lib/zgw_log.h"

#include "ZW_classcmd.h" //Z-Wave/include/
#include "RD_DataStore.h"
#include "RD_internal.h"
#include "DataStore.h"

#include "zw_network_info.h"

#include "test_helpers.h"
#include "test_restore_help.h"

#define test_HomeID UIP_HTONL(0xFAB5b0b0)
/* Used by eeprom_init() implemented in contiki/platform/linux/File_eeprom.c */
const char* linux_conf_eeprom_file;
const char* linux_conf_database_file;

zgw_log_id_define(tst_js_eeprom);
zgw_log_id_default_set(tst_js_eeprom);

extern zip_gateway_backup_data_t backup_rep;
extern ZGW_restore_config_t restore_cfg;

zw_controller_t *ctrl;

zw_node_data_t gw_node_data;
zw_node_type_t gw_type = {BASIC_TYPE_STATIC_CONTROLLER,
                          GENERIC_TYPE_STATIC_CONTROLLER,
                          SPECIFIC_TYPE_GATEWAY};
uint8_t node_id_1_endpoint_info[] = {0x9F, 0x98, 0x5E, 0x68, 0x23, 0xF1, 0x00, 0x52, 0x4D, 0x34, 0x8F};

zw_node_data_t node_id_2_data;
zw_node_type_t sensor_type = {BASIC_TYPE_SLAVE,
                              GENERIC_TYPE_SENSOR_MULTILEVEL,
                              SPECIFIC_TYPE_NOT_USED};
uint8_t node_id_2_endpoint0_info[] = {0x98, 0x68, 0x23, 0xF1, 0x00, 0x21, 0x4d, 0x22, 0x23};
uint8_t node_id_2_endpoint_info[] = {0x98, 0xF1, 0x00, 0x21, 0x4d, 0x22, 0x23};
uint8_t node_id_2_ep4_endpoint_aggr[] = {0x01, 0x02};
uint8_t node_id_2_dsk[] = {0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF, 0x22, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF};

zw_node_data_t node_id_8_data;
zw_node_type_t switch_binary_type = {	BASIC_TYPE_SLAVE,
																			GENERIC_TYPE_SWITCH_BINARY,
																			SPECIFIC_TYPE_NOT_USED};
uint8_t node_id_8_endpoint_info[] = {0x9F, 0x98, 0x5E, 0x68, 0x23, 0xF1, 0x00, 0x52, 0x4D, 0x34, 0x8F, 0x72};

zw_node_data_t node_id_10_data;
uint8_t node_id_10_endpoint_info[] = {0x21, 0x68, 0x23, 0xF1, 0x00, 0x4d, 0x22, 0x23};
uint8_t node_id_10_dsk[] = {0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF, 0x22, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF};

void test_init() {
   uint8_t SUCid = 1;
   uint8_t *cc_list=NULL; /* Not needed, so 0 is OK */

   cfg_init();

   zgw_data_init(NULL,
                 NULL, NULL, NULL);

   homeID = test_HomeID;
   MyNodeID = 1;
   ctrl = zw_controller_init(SUCid);
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

   /* Add our first network node  */
   node_id_2_data.node_id = 2;
   node_id_2_data.capability = 1;
   node_id_2_data.security = 1; /* TODO */
   node_id_2_data.neighbors = NULL;
   node_id_2_data.node_type = sensor_type;
   zw_controller_add_node(ctrl, 2, &node_id_2_data);

	 /* Add our second network node  */
   node_id_8_data.node_id = 8;
   node_id_8_data.capability = 1;
   node_id_8_data.security = 1; /* TODO */
   node_id_8_data.neighbors = NULL;
   node_id_8_data.node_type = switch_binary_type;
   zw_controller_add_node(ctrl, 8, &node_id_8_data);

   /* Add re-interview node */
   node_id_10_data.node_id = 10;
   node_id_10_data.capability = 0;
   node_id_10_data.security = 1; /* TODO */
   node_id_10_data.neighbors = NULL;
   node_id_10_data.node_type = switch_binary_type;
   zw_controller_add_node(ctrl, 10, &node_id_10_data);

  // Init gor the restore eeprom
  restore_cfg.data_path = "./";
}

/* This function steals the filename */
int test_setup(char* filename)
{
   json_object *zgw_backup_obj = NULL;
   int res = 0; /* aka success */

   restore_cfg.json_filename = filename;

   printf("controller %p\n", ctrl);

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

/* Verify against network_test_2_nodes.json */
void verify_eeprom_file()
{
   nodeid_t nodeid = 0;
   uint8_t epid = 0;
   check_true(rd_zgw_homeid_get() == test_HomeID, "home ID");
   check_true(rd_zgw_nodeid_get() == MyNodeID, "node ID");
   size_t temp_assoc_virtual_nodeid_count = 0;
   nodeid_t temp_assoc_virtual_nodeids[PREALLOCATED_VIRTUAL_NODE_COUNT] = {0};
   nodeid_t expected_virtual_nodeids[PREALLOCATED_VIRTUAL_NODE_COUNT] = {14, 15, 16};
   temp_assoc_virtual_nodeid_count = rd_datastore_unpersist_virtual_nodes(
       temp_assoc_virtual_nodeids, PREALLOCATED_VIRTUAL_NODE_COUNT);
   check_true(temp_assoc_virtual_nodeid_count == 3,
              "Temporary association virtual node id count");
   check_mem((uint8_t*)expected_virtual_nodeids, (uint8_t*)temp_assoc_virtual_nodeids,
             PREALLOCATED_VIRTUAL_NODE_COUNT,
             "Temporary association virtual node ids mismatch",
             "Temporary association virtual node ids match");
   while (nodeid < ZW_MAX_NODES) {
     rd_node_database_entry_t *n = rd_data_store_read(nodeid);
     if (n != NULL) {
       switch (n->nodeid) {
         case 1:
           check_true(n->nodeid == 1, "Node 1");
           check_true(n->wakeUp_interval == 5000, "Wake Up interval");
           check_true(n->lastAwake == 0, "last awake");
           check_true(n->security_flags == 0x7, "security flags");
           check_true(n->state == STATUS_DONE, "state");
           check_true(n->manufacturerID == 0x1, "Manufacturer ID");
           check_true(n->productType == 0x2, "Product type");
           check_true(n->productID == 0x3, "Product ID");
           check_true(n->nodeType == BASIC_TYPE_STATIC_CONTROLLER, "Node type");
           check_true(n->refCnt == 0, "Reference count");
           check_true(n->mode == MODE_ALWAYSLISTENING, "mode");
           check_true(n->nEndpoints == 1, "Number of endpoints");
           check_true(n->nAggEndpoints == 0, "Number of aggregated endpoints");
           check_true(n->nodeNameLen == strlen("gateway"), "Node name length");
           check_true(!strncmp("gateway", n->nodename, n->nodeNameLen), "Node name");
           check_true(n->node_is_zws_probed == 1, "isZwsProbed");
           check_true(n->node_properties_flags == 0, "Node properties flags");
           check_true(n->node_version_cap_and_zwave_sw == 0, "Version capability report");
           check_true(n->node_cc_versions[0].command_class == COMMAND_CLASS_VERSION, "CC versions - COMMAND_CLASS_VERSION");
           check_true(n->node_cc_versions[0].version == 0, "CC versions - version of COMMAND_CLASS_VERSION");
           check_true(n->node_cc_versions[1].command_class == COMMAND_CLASS_ZWAVEPLUS_INFO, "CC versions - COMMAND_CLASS_ZWAVEPLUS_INFO");
           check_true(n->node_cc_versions[1].version == 2, "CC versions - version of COMMAND_CLASS_ZWAVEPLUS_INFO");
           check_true(n->node_cc_versions[2].command_class == COMMAND_CLASS_MANUFACTURER_SPECIFIC, "CC versions - COMMAND_CLASS_MANUFACTURER_SPECIFIC");
           check_true(n->node_cc_versions[2].version == 0, "CC versions - version of COMMAND_CLASS_MANUFACTURER_SPECIFIC,");
           check_true(n->node_cc_versions[3].command_class == COMMAND_CLASS_WAKE_UP, "CC versions - COMMAND_CLASS_WAKE_UP");
           check_true(n->node_cc_versions[3].version == 0, "CC versions - version of COMMAND_CLASS_WAKE_UP");
           check_true(n->node_cc_versions[4].command_class == COMMAND_CLASS_MULTI_CHANNEL_V4, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_V4");
           check_true(n->node_cc_versions[4].version == 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_V4");
           check_true(n->node_cc_versions[5].command_class == COMMAND_CLASS_ASSOCIATION, "CC versions - COMMAND_CLASS_ASSOCIATION");
           check_true(n->node_cc_versions[5].version == 0, "CC versions - version of COMMAND_CLASS_ASSOCIATION");
           check_true(n->node_cc_versions[6].command_class == COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
           check_true(n->node_cc_versions[6].version == 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
           check_true(n->node_cc_versions[7].command_class == 0xffff, "CC versions - ending mark");
           check_true(n->node_cc_versions[7].version == 0xff, "CC versions - ending mark in version");
           check_true(n->dskLen == 0, "DSK length");
           if (n->nEndpoints > 0) {
             check_not_null(n->endpoints, "Endpoints");
             check_true(list_length(n->endpoints) == n->nEndpoints, "Length of endpoints");
             epid = 0;
             rd_ep_database_entry_t *e = list_head(n->endpoints);
             while(epid < n->nEndpoints) {
               check_true(e->endpoint_id == epid, "Endpoint ID");
               check_true(e->installer_iconID == ICON_TYPE_GENERIC_GATEWAY, "installer icon ID");
               check_true(e->user_iconID == ICON_TYPE_GENERIC_GATEWAY, "user icon ID");
               check_true(e->state == EP_STATE_PROBE_DONE, "Endpoint state");
               check_true(e->endpoint_loc_len == 0, "Endpoint location length");
               check_true(e->endpoint_name_len == strlen("gateway"), "Endpoint name len");
               check_true(!strncmp("gateway", e->endpoint_name, e->endpoint_name_len), "Endpoint name");
               check_true(e->endpoint_info_len == (sizeof(node_id_1_endpoint_info)/sizeof(node_id_1_endpoint_info[0])),
                          "Endpoint info length");
               check_mem(node_id_1_endpoint_info, e->endpoint_info, e->endpoint_info_len, "Endpoint info mismatch", "Endpoint info match");
               check_true(e->endpoint_aggr_len == 0, "Endpoint aggr length");
               e = list_item_next(e);
               epid++;
             }
           }
           break;
         case 2:
           check_true(n->nodeid == 2, "Node 2");
           check_true(n->wakeUp_interval == 1000, "Wake Up interval");
           check_true(n->lastAwake == 0, "last awake");
           check_true(n->security_flags == 0, "security flags");
           check_true(n->state == STATUS_DONE , "state");
           check_true(n->manufacturerID == 1, "Manufacturer ID");
           check_true(n->productType == 33153, "Product type");
           check_true(n->productID == 3, "Product ID");
           check_true(n->nodeType == BASIC_TYPE_SLAVE, "Node type");
           check_true(n->refCnt == 0, "Reference count");
           check_true(n->mode == MODE_FREQUENTLYLISTENING, "mode");
           check_true(n->nEndpoints == 4, "Number of endpoints");
           check_true(n->nAggEndpoints == 1, "Number of aggregated endpoints");
           check_true(n->nodeNameLen == strlen("device"), "Node name length");
           check_true(!strncmp("device", n->nodename, n->nodeNameLen), "Node name");
           check_true(n->node_is_zws_probed == 1, "isZwsProbed");
           check_true(n->node_properties_flags == 4, "Node properties flags");
           check_true(n->node_version_cap_and_zwave_sw == 0x03, "Version capability report");
           check_true(n->node_cc_versions[0].command_class == COMMAND_CLASS_VERSION, "CC versions - COMMAND_CLASS_VERSION");
           check_true(n->node_cc_versions[0].version == 0, "CC versions - version of COMMAND_CLASS_VERSION");
           check_true(n->node_cc_versions[1].command_class == COMMAND_CLASS_ZWAVEPLUS_INFO, "CC versions - COMMAND_CLASS_ZWAVEPLUS_INFO");
           check_true(n->node_cc_versions[1].version == 0, "CC versions - version of COMMAND_CLASS_ZWAVEPLUS_INFO");
           check_true(n->node_cc_versions[2].command_class == COMMAND_CLASS_MANUFACTURER_SPECIFIC, "CC versions - COMMAND_CLASS_MANUFACTURER_SPECIFIC");
           check_true(n->node_cc_versions[2].version == 0, "CC versions - version of COMMAND_CLASS_MANUFACTURER_SPECIFIC,");
           check_true(n->node_cc_versions[3].command_class == COMMAND_CLASS_WAKE_UP, "CC versions - COMMAND_CLASS_WAKE_UP");
           check_true(n->node_cc_versions[3].version == 0, "CC versions - version of COMMAND_CLASS_WAKE_UP");
           check_true(n->node_cc_versions[4].command_class == COMMAND_CLASS_MULTI_CHANNEL_V4, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_V4");
           check_true(n->node_cc_versions[4].version == 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_V4");
           check_true(n->node_cc_versions[5].command_class == COMMAND_CLASS_ASSOCIATION, "CC versions - COMMAND_CLASS_ASSOCIATION");
           check_true(n->node_cc_versions[5].version == 0, "CC versions - version of COMMAND_CLASS_ASSOCIATION");
           check_true(n->node_cc_versions[6].command_class == COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
           check_true(n->node_cc_versions[6].version == 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
           check_true(n->node_cc_versions[7].command_class == 0xffff, "CC versions - ending mark");
           check_true(n->node_cc_versions[7].version == 0xff, "CC versions - ending mark in version");
           check_true(n->dskLen == 16, "DSK length");
           check_mem(node_id_2_dsk, n->dsk, n->dskLen, "DSK mismatch", "DSK match");
           if (n->nEndpoints > 0) {
             check_not_null(n->endpoints, "Endpoints");
             check_true(list_length(n->endpoints) == n->nEndpoints, "Length of endpoints");
             epid = 0;
             rd_ep_database_entry_t *e = list_head(n->endpoints);
             while(epid < n->nEndpoints) {
               check_true(e->endpoint_id == epid, "Endpoint ID");
               if (epid == 0) {
                 check_true(e->endpoint_info_len == (sizeof(node_id_2_endpoint0_info)/sizeof(node_id_2_endpoint0_info[0])),
                            "Endpoint 0 info length");
                 check_mem(node_id_2_endpoint0_info, e->endpoint_info, e->endpoint_info_len,
                           "Endpoint info 0 mismatch",
                           "Endpoint 0 info match");
               } else 
               if (epid == 3) {
                 check_true(e->endpoint_info_len == 0, "Endpoint 3 info length");
               } else {
                 check_true(e->endpoint_info_len == (sizeof(node_id_2_endpoint_info)/sizeof(node_id_2_endpoint_info[0])), "Endpoint not-3 info length");
                 check_mem(node_id_2_endpoint_info, e->endpoint_info, e->endpoint_info_len, "Endpoint info mismatch", "Endpoint info match");
               }
               switch(epid) {
                 case 0:
                   check_true(e->installer_iconID == 3328, "installer icon ID");
                   check_true(e->user_iconID == 8, "user icon ID");
                   check_true(e->state == EP_STATE_PROBE_DONE, "Endpoint state");
                   check_true(e->endpoint_loc_len == strlen("Hiding in the corner"), "Endpoint location length");
                   check_true(!strncmp("Hiding in the corner", e->endpoint_location, e->endpoint_loc_len), "Endpoint location");
                   check_true(e->endpoint_name_len == strlen("Sensor"), "Endpoint name len");
                   check_true(!strncmp("Sensor", e->endpoint_name, e->endpoint_name_len), "Endpoint name");
                   check_true(e->endpoint_aggr_len == 0, "Endpoint aggr length");
                   break;
                 case 1:
                   check_true(e->installer_iconID == 7, "installer icon ID");
                   check_true(e->user_iconID == 8, "user icon ID");
                   check_true(e->state == EP_STATE_PROBE_DONE, "Endpoint state");
                   check_true(e->endpoint_loc_len == strlen("slot 2"), "Endpoint location length");
                   check_true(!strncmp("slot 2", e->endpoint_location, e->endpoint_loc_len), "Endpoint location");
                   check_true(e->endpoint_name_len == strlen("2"), "Endpoint name len");
                   check_true(!strncmp("2", e->endpoint_name, e->endpoint_name_len), "Endpoint name");
                   check_true(e->endpoint_aggr_len == 0, "Endpoint aggr length");
                   break;
                 case 2:
                   check_true(e->installer_iconID == 7, "installer icon ID");
                   check_true(e->user_iconID == 8, "user icon ID");
                   check_true(e->state == EP_STATE_PROBE_DONE, "Endpoint state");
                   check_true(e->endpoint_loc_len == strlen("slot 1"), "Endpoint location length");
                   check_true(!strncmp("slot 1", e->endpoint_location, e->endpoint_loc_len), "Endpoint location");
                   check_true(e->endpoint_name_len == strlen("first"), "Endpoint name len");
                   check_true(!strncmp("first", e->endpoint_name, e->endpoint_name_len), "Endpoint name");
                   check_true(e->endpoint_aggr_len == 0, "Endpoint aggr length");
                   break;
                 case 3:
                   check_true(e->installer_iconID == 7, "installer icon ID");
                   check_true(e->user_iconID == 8, "user icon ID");
                   check_true(e->state == EP_STATE_PROBE_DONE, "Endpoint state");
                   check_true(e->endpoint_loc_len == strlen("slots"), "Endpoint location length");
                   check_true(!strncmp("slots", e->endpoint_location, e->endpoint_loc_len), "Endpoint location");
                   check_true(e->endpoint_name_len == strlen("aggregation"), "Endpoint name len");
                   check_true(!strncmp("aggregation", e->endpoint_name, e->endpoint_name_len), "Endpoint name");
                   check_true(e->endpoint_aggr_len == 2, "Endpoint aggr length");
                   check_mem(node_id_2_ep4_endpoint_aggr, e->endpoint_agg, e->endpoint_aggr_len, "Endpoint aggr mismatch", "Endpoint aggr match");
                   break;
               }
               e = list_item_next(e);
               epid++;
             }
           }
           break;
         case 8:
           check_true(n->nodeid == 8, "Node 8");
           check_true(n->wakeUp_interval == 0, "Wake Up interval");
           check_true(n->lastAwake == 0, "last awake");
           check_true(n->security_flags == 0x03, "security flags");
           check_true(n->state == STATUS_DONE, "state");
           check_true(n->manufacturerID == 1, "Manufacturer ID");
           check_true(n->productType == 2, "Product type");
           check_true(n->productID == 3, "Product ID");
           check_true(n->nodeType == BASIC_TYPE_SLAVE, "Node type");
           check_true(n->refCnt == 0, "Reference count");
           check_true(n->mode == MODE_NONLISTENING, "mode");
           check_true(n->nEndpoints == 1, "Number of endpoints");
           check_true(n->nAggEndpoints == 0, "Number of aggregated endpoints");
           check_true(n->nodeNameLen == strlen("device 8"), "Node name length");
           check_true(!strncmp("device 8", n->nodename, n->nodeNameLen), "Node name");
           check_true(n->node_is_zws_probed == 1, "isZwsProbed");
           check_true(n->node_properties_flags == 4, "Node properties flags");
           check_true(n->node_version_cap_and_zwave_sw == 0x03, "Version capability report");
           check_true(n->node_cc_versions[0].command_class == COMMAND_CLASS_VERSION, "CC versions - COMMAND_CLASS_VERSION");
           check_true(n->node_cc_versions[0].version == 0, "CC versions - version of COMMAND_CLASS_VERSION");
           check_true(n->node_cc_versions[1].command_class == COMMAND_CLASS_ZWAVEPLUS_INFO, "CC versions - COMMAND_CLASS_ZWAVEPLUS_INFO");
           check_true(n->node_cc_versions[1].version == 2, "CC versions - version of COMMAND_CLASS_ZWAVEPLUS_INFO");
           check_true(n->node_cc_versions[2].command_class == COMMAND_CLASS_MANUFACTURER_SPECIFIC, "CC versions - COMMAND_CLASS_MANUFACTURER_SPECIFIC");
           check_true(n->node_cc_versions[2].version == 2, "CC versions - version of COMMAND_CLASS_MANUFACTURER_SPECIFIC,");
           check_true(n->node_cc_versions[3].command_class == COMMAND_CLASS_WAKE_UP, "CC versions - COMMAND_CLASS_WAKE_UP");
           check_true(n->node_cc_versions[3].version == 0, "CC versions - version of COMMAND_CLASS_WAKE_UP");
           check_true(n->node_cc_versions[4].command_class == COMMAND_CLASS_MULTI_CHANNEL_V4, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_V4");
           check_true(n->node_cc_versions[4].version == 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_V4");
           check_true(n->node_cc_versions[5].command_class == COMMAND_CLASS_ASSOCIATION, "CC versions - COMMAND_CLASS_ASSOCIATION");
           check_true(n->node_cc_versions[5].version == 0, "CC versions - version of COMMAND_CLASS_ASSOCIATION");
           check_true(n->node_cc_versions[6].command_class == COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
           check_true(n->node_cc_versions[6].version == 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
           check_true(n->node_cc_versions[7].command_class == 0xffff, "CC versions - ending mark");
           check_true(n->node_cc_versions[7].version == 0xff, "CC versions - ending mark in version");
           check_true(n->dskLen == 0, "DSK length");
           if (n->nEndpoints > 0) {
             check_not_null(n->endpoints, "Endpoints");
             check_true(list_length(n->endpoints) == n->nEndpoints, "Length of endpoints");
             epid = 0;
             rd_ep_database_entry_t *e = list_head(n->endpoints);
             while(epid < n->nEndpoints) {
               check_true(e->endpoint_id == epid, "Endpoint ID");
               check_true(e->installer_iconID == ICON_TYPE_GENERIC_GATEWAY, "installer icon ID");
               check_true(e->user_iconID == ICON_TYPE_GENERIC_GATEWAY, "user icon ID");
               check_true(e->state == EP_STATE_PROBE_DONE, "Endpoint state");
               check_true(e->endpoint_loc_len == 0, "Endpoint location length");
               check_true(e->endpoint_name_len == strlen("Device 8"), "Endpoint name len");
               check_true(!strncmp("Device 8", e->endpoint_name, e->endpoint_name_len), "Endpoint name");
               check_true(e->endpoint_aggr_len == 0, "Endpoint aggr length");
               e = list_item_next(e);
               epid++;
             }
           }
           break;
         case 10:
           check_true(n->nodeid == 10, "Node 10");
           check_true(n->wakeUp_interval == 4200, "Wake Up interval");
           check_true(n->lastAwake == 0, "last awake");
           check_true(n->security_flags == 0, "security flags");
           check_true(n->state == STATUS_CREATED, "state");
           check_true(n->manufacturerID == 0, "Manufacturer ID");
           check_true(n->productType == 0, "Product type");
           check_true(n->productID == 0, "Product ID");
           check_true(n->nodeType == BASIC_TYPE_SLAVE, "Node type");
           check_true(n->refCnt == 0, "Reference count");
           check_true(n->mode == MODE_MAILBOX, "mode");
           check_true(n->nEndpoints == 1, "Number of endpoints");
           check_true(n->nAggEndpoints == 0, "Number of aggregated endpoints");
           check_true(n->nodeNameLen == 0, "Node name length");
           check_true(n->node_is_zws_probed == 0, "isZwsProbed");
           check_true(n->node_properties_flags == 0, "Node properties flags");
           check_true(n->node_version_cap_and_zwave_sw == 0, "Version capability report");
           check_true(n->node_cc_versions[0].command_class == COMMAND_CLASS_VERSION, "CC versions - COMMAND_CLASS_VERSION");
           check_true(n->node_cc_versions[0].version == 0, "CC versions - version of COMMAND_CLASS_VERSION");
           check_true(n->node_cc_versions[1].command_class == COMMAND_CLASS_ZWAVEPLUS_INFO, "CC versions - COMMAND_CLASS_ZWAVEPLUS_INFO");
           check_true(n->node_cc_versions[1].version == 0, "CC versions - version of COMMAND_CLASS_ZWAVEPLUS_INFO");
           check_true(n->node_cc_versions[2].command_class == COMMAND_CLASS_MANUFACTURER_SPECIFIC, "CC versions - COMMAND_CLASS_MANUFACTURER_SPECIFIC");
           check_true(n->node_cc_versions[2].version == 0, "CC versions - version of COMMAND_CLASS_MANUFACTURER_SPECIFIC,");
           check_true(n->node_cc_versions[3].command_class == COMMAND_CLASS_WAKE_UP, "CC versions - COMMAND_CLASS_WAKE_UP");
           check_true(n->node_cc_versions[3].version == 0, "CC versions - version of COMMAND_CLASS_WAKE_UP");
           check_true(n->node_cc_versions[4].command_class == COMMAND_CLASS_MULTI_CHANNEL_V4, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_V4");
           check_true(n->node_cc_versions[4].version == 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_V4");
           check_true(n->node_cc_versions[5].command_class == COMMAND_CLASS_ASSOCIATION, "CC versions - COMMAND_CLASS_ASSOCIATION");
           check_true(n->node_cc_versions[5].version == 0, "CC versions - version of COMMAND_CLASS_ASSOCIATION");
           check_true(n->node_cc_versions[6].command_class == COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
           check_true(n->node_cc_versions[6].version == 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
           check_true(n->node_cc_versions[7].command_class == 0xffff, "CC versions - ending mark");
           check_true(n->node_cc_versions[7].version == 0xff, "CC versions - ending mark in version");
           check_true(n->dskLen == 16, "DSK length");
           check_mem(node_id_10_dsk, n->dsk, n->dskLen, "DSK mismatch", "DSK match");
           if (n->nEndpoints > 0) {
             check_not_null(n->endpoints, "Endpoints");
             check_true(list_length(n->endpoints) == n->nEndpoints, "Length of endpoints");
             epid = 0;
             rd_ep_database_entry_t *e = list_head(n->endpoints);
             while(epid < n->nEndpoints) {
               check_true(e->endpoint_id == epid, "Endpoint ID");
               check_true(e->installer_iconID == 0, "installer icon ID");
               check_true(e->user_iconID == 0, "user icon ID");
               check_true(e->state == EP_STATE_PROBE_INFO, "Endpoint state");
               e = list_item_next(e);
               epid++;
             }
           }
           break;
         default:
           /* Should not reach here */
           check_true(false, "Unexpected node exists");
       }
     } else {
         check_true(n == NULL, "Empty node");
     }
     nodeid++;
   }
}

int main(void) {
   char * log_file = "test_json_parse_eeprom_write.log";
   verbosity = test_case_start_stop;

   /* Start the logging system at the start of main. */
   zgw_log_setup(log_file);

   /* Log that we have entered this function. */
   zgw_log_enter();

   /* Log at level 1 */
   zgw_log(1, "ZGWlog test arg %d\n", 7);

   /* Initialize ctrl variable */
   test_init();

   if (NULL != ctrl)
   {
      check_true(
         test_setup(TEST_SRC_DIR "/json_parse_eeprom_write/network_test_2_nodes.json") == 0,
         "Parsed log_file into internals");
   }

   /* Get all the JSON data in memory */
   zip_gateway_backup_data_t * bu1 = zip_gateway_backup_data_unsafe_get();
   const zw_controller_t * zw_ctrl = zgw_controller_zw_get();
   zgw_data_t *zgw = &(bu1->zgw);
   const zgw_node_data_t *gw_node_data = zgw_node_data_get(MyNodeID);
   const zip_lan_ip_data_t *lan_data = zip_lan_ip_data_get();
   const zip_pan_data_t *zw_pan_data = &(zgw->zip_pan_data);

   /* Remove database before running restore */
   remove(RESTORE_EEPROM_FILENAME);
	 /* Call the eeprom_writer with the data loaded from the JSON file */
	 zgw_restore_eeprom_file();

	 /* Now we need to verify that the eeprom.dat file matches the JSON file data */
   /* TODO: See ZGW-2145 I Could not finish this due to some funny things happening with eeprom.dat. 
      If you run:
      sudo test/eeprom-printer systools/zgw_restore/test/json_parse_eeprom_write/eeprom.dat
      you need to ensure that all the printed data is according to JSON input file data the and that the GW can run with the eeprom.dat file.
   */
   verify_eeprom_file();

   close_run();

   /* Close down the logging system at the end of main(). */
   zgw_log_teardown();

   return numErrs;
}
