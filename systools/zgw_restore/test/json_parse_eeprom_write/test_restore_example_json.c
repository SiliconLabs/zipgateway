/* © 2019 Silicon Laboratories Inc. */

#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "sys/clock.h"
#include "json.h"
#include "dev/eeprom.h"

#include "ZW_typedefs.h"
#include "ZW_classcmd.h"

#include "zw_data.h"
#include "zgw_data.h"
#include "zgw_restore_cfg.h"
#include "zgwr_json_parser.h"
#include "zgwr_eeprom.h"

#include "lib/zgw_log.h"

#include "RD_DataStore.h"

#include "RD_internal.h"
#include "DataStore.h"
#include "zw_network_info.h"
#include "RD_types.h"

#include "test_helpers.h"
#include "test_restore_help.h"


static const zw_node_type_t basic_type_slave = {BASIC_TYPE_SLAVE, 0, 0};
static const zw_node_type_t basic_type_routing_slave = {BASIC_TYPE_ROUTING_SLAVE, 0, 0};
static const zw_node_type_t basic_type_static_controller = {BASIC_TYPE_STATIC_CONTROLLER, 0, 0};


zgw_log_id_define(tst_rest_exmpl);
zgw_log_id_default_set(tst_rest_exmpl);

// Remove intellisense warning
#ifndef EXAMPLE_JSON_ROOT_PATH
#define EXAMPLE_JSON_ROOT_PATH ""
#endif

// NetToHostOrder
#define UIP_NTOHL UIP_HTONL

void free_ctrl(zw_controller_t**ctrl)
{
   // Free Nodes
   int i = 0;
   for (i = 0; i < ZW_MAX_NODES; i++)
   {
      if ((*ctrl)->included_nodes[i] != NULL)
      {
         free((*ctrl)->included_nodes[i]);
         (*ctrl)->included_nodes[i] = NULL;
      }
   }
   free(*ctrl);
   *ctrl = NULL;
}

/* This function steals the filename */
int test_setup(char* filename, const zw_controller_t *ctrl)
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
   check_equal(res, 0, "Parsed json object into gateway structure.");
   /* Call the eeprom_writer with the data loaded from the JSON file */
   data_store_exit();
   remove(RESTORE_EEPROM_FILENAME);
   zgw_restore_eeprom_file();
   return res;
}

void test_init_controller_json(zw_controller_t **ctrl) {
   /* zero the global */
   uint8_t SUCid = 1;

   cfg_init();

   zgw_data_init(NULL,
         NULL, NULL, NULL);

   *ctrl = zw_controller_init(SUCid);
   check_not_null(*ctrl, "Initialized controller");

   /* Add the gateway */
   zw_node_data_t *gw_node_data = malloc(sizeof(zw_node_data_t));
   gw_node_data->node_id = 1;
   gw_node_data->capability = 1;
   gw_node_data->security = 1;
   gw_node_data->neighbors = NULL;
   gw_node_data->node_type = basic_type_static_controller;
   check_true(zw_controller_add_node(*ctrl, 1, gw_node_data), "Add Node 1");

   /* Add our first network node  */
   zw_node_data_t *node_id_2_data = malloc(sizeof(zw_node_data_t));;
   node_id_2_data->node_id = 2;
   node_id_2_data->capability = 1;
   node_id_2_data->security = 1;
   node_id_2_data->neighbors = NULL;
   node_id_2_data->node_type = basic_type_slave;
   check_true(zw_controller_add_node(*ctrl, 2, node_id_2_data), "Add Node 2");

   /* Add our second network node  */
   zw_node_data_t *node_id_3_data = malloc(sizeof(zw_node_data_t));;
   node_id_3_data->node_id = 3;
   node_id_3_data->capability = 1;
   node_id_3_data->security = 1;
   node_id_3_data->neighbors = NULL;
   node_id_3_data->node_type = basic_type_routing_slave;
   check_true(zw_controller_add_node(*ctrl, 3, node_id_3_data), "Add Node 3");


   /* Add our third network node */
   zw_node_data_t *node_id_4_data = malloc(sizeof(zw_node_data_t));;
   node_id_4_data->node_id = 4;
   node_id_4_data->capability = 0x80;
   node_id_4_data->security = 1;
   node_id_4_data->neighbors = NULL;
   node_id_4_data->node_type = basic_type_routing_slave;
   check_true(zw_controller_add_node(*ctrl, 4, node_id_4_data), "Add Node 4");

   /* Add our third network node */
   zw_node_data_t *node_id_5_data = malloc(sizeof(zw_node_data_t));;
   node_id_5_data->node_id = 5;
   node_id_5_data->capability = 0x80;
   node_id_5_data->security = 1;
   node_id_5_data->neighbors = NULL;
   node_id_5_data->node_type = basic_type_slave;
   check_true(zw_controller_add_node(*ctrl, 5, node_id_5_data), "Add Node 5");

   // Init the restore eeprom
   restore_cfg.data_path = "./";
}


/******************************************************************************
 * NoProbe.json
 *****************************************************************************/

/* Verify against network_test_2_nodes.json */
void verify_eeprom_file_NoProbe_json()
{
   uint8_t epid = 0;
   nodeid_t nodeid = 0;
   // NB: Home id is stored in EEPROM in network byte order (big endian)
   check_equal(UIP_NTOHL(rd_zgw_homeid_get()), 0xE1DB3EDC, "home ID");
   check_equal(rd_zgw_nodeid_get(), 1, "node ID");
   size_t temp_assoc_virtual_nodeid_count = 0;
   nodeid_t temp_assoc_virtual_nodeids[PREALLOCATED_VIRTUAL_NODE_COUNT] = {0};
   nodeid_t expected_virtual_nodeids[PREALLOCATED_VIRTUAL_NODE_COUNT] = {6, 7, 8, 9};
   temp_assoc_virtual_nodeid_count = rd_datastore_unpersist_virtual_nodes(
       temp_assoc_virtual_nodeids, PREALLOCATED_VIRTUAL_NODE_COUNT);
   check_equal(temp_assoc_virtual_nodeid_count, 4,
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
            check_equal(n->nodeid, 1, "Node 1");
            check_equal(n->wakeUp_interval, 5000, "Wake Up interval");
            check_equal(n->lastAwake, 0, "last awake");
            check_equal(n->security_flags, 113, "security flags");

            check_equal(n->state, STATUS_DONE, "state");
            check_equal(n->manufacturerID, 0x1, "Manufacturer ID");
            check_equal(n->productType, 0x1, "Product type");
            check_equal(n->productID, 0x2, "Product ID");
            check_equal(n->nodeType, BASIC_TYPE_STATIC_CONTROLLER, "Node type");
            check_equal(n->refCnt, 0, "Reference count");
            check_equal(n->mode, MODE_ALWAYSLISTENING, "mode");
            check_equal(n->nEndpoints, 1, "Number of endpoints");
            check_equal(n->nAggEndpoints, 0, "Number of aggregated endpoints");
            check_equal(n->nodeNameLen, strlen(""), "Node name length");
            check_equal(n->node_version_cap_and_zwave_sw, 0x07, "Version capability report");
            check_equal(n->node_cc_versions[0].command_class, COMMAND_CLASS_VERSION, "CC versions - COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[0].version, 1, "CC versions - version of COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[1].command_class, COMMAND_CLASS_ZWAVEPLUS_INFO, "CC versions - COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[1].version, 1, "CC versions - version of COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[2].command_class, COMMAND_CLASS_MANUFACTURER_SPECIFIC, "CC versions - COMMAND_CLASS_MANUFACTURER_SPECIFIC");
            check_equal(n->node_cc_versions[2].version, 1, "CC versions - version of COMMAND_CLASS_MANUFACTURER_SPECIFIC,");
            check_equal(n->node_cc_versions[3].command_class, COMMAND_CLASS_WAKE_UP, "CC versions - COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[3].version, 0, "CC versions - version of COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[4].command_class, COMMAND_CLASS_MULTI_CHANNEL_V4, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[4].version, 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[5].command_class, COMMAND_CLASS_ASSOCIATION, "CC versions - COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[5].version, 1, "CC versions - version of COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[6].command_class, COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[6].version, 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[7].command_class, 0xffff, "CC versions - ending mark");
            check_equal(n->node_cc_versions[7].version, 0xff, "CC versions - ending mark in version");

            check_equal(n->dskLen, 0, "DSK length");
            if (n->nEndpoints > 0) {
               const uint8_t node_id_endpoint_info[] = {0x10,0x01,0x5e, 0x86, 0x72, 0x7a, 0x73, 0x22, 0x85, 0x59, 0x70, 0x56, 0x5a, 0x6c, 0x55, 0x7a, 0x74, 0x98, 0x9f,
                                                        0x68, 0x23,
                                                        0xf1, 0x00,
                     0x86, 0x72, 0x73, 0x59, 0x85, 0x7a};
               check_not_null(n->endpoints, "Endpoints");
               check_equal(list_length(n->endpoints), n->nEndpoints, "Length of endpoints");
               epid = 0;
               rd_ep_database_entry_t *e = list_head(n->endpoints);
               while(epid < n->nEndpoints) {
                  check_equal(e->endpoint_id, epid, "Endpoint ID");
                  check_equal(e->installer_iconID, ICON_TYPE_GENERIC_GATEWAY, "installer icon ID");
                  check_equal(e->user_iconID, ICON_TYPE_GENERIC_GATEWAY, "user icon ID");
                  check_equal(e->endpoint_loc_len, 0, "Endpoint location length");
                  check_equal(e->endpoint_name_len, strlen("gateway"), "Endpoint name len");
                  check_true(!strncmp("gateway", e->endpoint_name, e->endpoint_name_len), "Endpoint name");
                  check_equal(e->endpoint_info_len, (sizeof(node_id_endpoint_info)/sizeof(node_id_endpoint_info[0])), "Endpoint info length");
                  check_mem(node_id_endpoint_info, e->endpoint_info, e->endpoint_info_len, "Endpoint info mismatch", "Endpoint info match");
                  check_equal(e->endpoint_aggr_len, 0, "Endpoint aggr length");
                  e = list_item_next(e);
                  epid++;
               }
            }
            break;
         case 2:
         {
            const char node_name[] = "Sensor Device";
            check_equal(n->nodeid, 2, "Node 2");
            check_equal(n->wakeUp_interval, 4200, "Wake Up interval");
            check_equal(n->lastAwake, 0, "last awake");
            check_equal(n->lastUpdate, 0, "last update");
            check_equal(n->security_flags, 1, "security flags");
            check_equal(n->state, STATUS_DONE, "state");
            check_equal(n->manufacturerID, 0, "Manufacturer ID");
            check_equal(n->productType, 3, "Product type");
            check_equal(n->productID, 3, "Product ID");
            check_equal(n->nodeType, BASIC_TYPE_SLAVE, "Node type"); // Not set in JSON file
            check_equal(n->refCnt, 0, "Reference count");
            check_equal(n->mode, MODE_FREQUENTLYLISTENING, "mode");
            check_equal(n->nEndpoints, 1, "Number of endpoints");
            check_equal(n->nAggEndpoints, 0, "Number of aggregated endpoints");
            check_equal(n->nodeNameLen, strlen(node_name), "Node name length");
            check_true(!strncmp(node_name, n->nodename, n->nodeNameLen), "Node name");
            check_equal(n->node_properties_flags, 4, "Node properties flags");
            check_equal(n->node_version_cap_and_zwave_sw, 0x03, "Version capability report");
            check_equal(n->node_cc_versions[0].command_class, COMMAND_CLASS_VERSION, "CC versions - COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[0].version, 2, "CC versions - version of COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[1].command_class, COMMAND_CLASS_ZWAVEPLUS_INFO, "CC versions - COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[1].version, 2, "CC versions - version of COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[2].command_class, COMMAND_CLASS_MANUFACTURER_SPECIFIC, "CC versions - COMMAND_CLASS_MANUFACTURER_SPECIFIC");
            check_equal(n->node_cc_versions[2].version, 2, "CC versions - version of COMMAND_CLASS_MANUFACTURER_SPECIFIC,");
            check_equal(n->node_cc_versions[3].command_class, COMMAND_CLASS_WAKE_UP, "CC versions - COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[3].version, 2, "CC versions - version of COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[4].command_class, COMMAND_CLASS_MULTI_CHANNEL_V4, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[4].version, 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[5].command_class, COMMAND_CLASS_ASSOCIATION, "CC versions - COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[5].version, 2, "CC versions - version of COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[6].command_class, COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[6].version, 3, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[7].command_class, 0xffff, "CC versions - ending mark");
            check_equal(n->node_cc_versions[7].version, 0xff, "CC versions - ending mark in version");
            check_equal(n->dskLen, 0, "No DSK");
            if (n->nEndpoints > 0) {
               check_not_null(n->endpoints, "Endpoints");
               check_equal(list_length(n->endpoints), n->nEndpoints, "Length of endpoints");
               epid = 0;
               rd_ep_database_entry_t *e = list_head(n->endpoints);
               while(epid < n->nEndpoints) {
                  const uint8_t node_id_endpoint_info[] = {0x10,0x01,0x5e, 0x98,
                                                           0x68, 0x23, 0xF1, 0x00,
                                                           0x86, 0x85, 0x8e, 0x59, 0x72, 0x5a, 0x73, 0x80, 0x71, 0x84, 0x7a};
                  check_equal(e->endpoint_id, epid, "Endpoint ID");
                  check_equal(e->endpoint_info_len, (sizeof(node_id_endpoint_info)/sizeof(node_id_endpoint_info[0])), "Endpoint info length");
                  check_mem(node_id_endpoint_info, e->endpoint_info, e->endpoint_info_len, "Endpoint info mismatch", "Endpoint info match");
                  switch(epid) {
                  case 0:
                  {
                     const char endpoint_name[] = "Sensor";
                     const char endpoint_location[] = "Hiding in the corner";
                     check_equal(e->installer_iconID, ICON_TYPE_SPECIFIC_SENSOR_NOTIFICATION_HOME_SECURITY, "installer icon ID");
                     check_equal(e->user_iconID, ICON_TYPE_SPECIFIC_SENSOR_NOTIFICATION_HOME_SECURITY, "user icon ID");
                     check_equal(e->endpoint_loc_len, strlen(endpoint_location), "Endpoint location length");
                     check_true(!strncmp(endpoint_location, e->endpoint_location, e->endpoint_loc_len), "Endpoint location");
                     check_equal(e->endpoint_name_len, strlen(endpoint_name), "Endpoint name len");
                     check_true(!strncmp(endpoint_name, e->endpoint_name, e->endpoint_name_len), "Endpoint name");
                     check_equal(e->endpoint_aggr_len, 0, "Endpoint aggr length");
                  }  break;
                  default:
                     check_true(false, "Unexpected endpoint exists");
                     break;
                  }
                  e = list_item_next(e);
                  epid++;
               }
            }
         }  break;
         case 3:
         {
            const char node_name[] = "Switch";
            check_equal(n->nodeid, 3, "Node 3");
            check_equal(n->wakeUp_interval, 0, "Wake Up interval");
            check_equal(n->lastAwake, 0, "last awake");
            check_equal(n->lastUpdate, 0, "last update");
            check_equal(n->security_flags, 0, "security flags");
            check_equal(n->state, STATUS_DONE , "state");
            check_equal(n->manufacturerID, 0, "Manufacturer ID");
            check_equal(n->productType, 3, "Product type");
            check_equal(n->productID, 2, "Product ID");
            check_equal(n->nodeType, BASIC_TYPE_ROUTING_SLAVE, "Node type"); // Not set in JSON file
            check_equal(n->refCnt, 0, "Reference count");
            check_equal(n->mode, MODE_ALWAYSLISTENING, "mode");
            check_equal(n->nEndpoints, 1, "Number of endpoints");
            check_equal(n->nAggEndpoints, 0, "Number of aggregated endpoints");
            check_equal(n->nodeNameLen, strlen(node_name), "Node name length");
            check_true(!strncmp(node_name, n->nodename, n->nodeNameLen), "Node name");
            check_equal(n->node_properties_flags, 4, "Node properties flags");
            check_equal(n->node_version_cap_and_zwave_sw, 0x03, "Version capability report");
            check_equal(n->node_cc_versions[0].command_class, COMMAND_CLASS_VERSION, "CC versions - COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[0].version, 2, "CC versions - version of COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[1].command_class, COMMAND_CLASS_ZWAVEPLUS_INFO, "CC versions - COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[1].version, 2, "CC versions - version of COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[2].command_class, COMMAND_CLASS_MANUFACTURER_SPECIFIC, "CC versions - COMMAND_CLASS_MANUFACTURER_SPECIFIC");
            check_equal(n->node_cc_versions[2].version, 2, "CC versions - version of COMMAND_CLASS_MANUFACTURER_SPECIFIC,");
            check_equal(n->node_cc_versions[3].command_class, COMMAND_CLASS_WAKE_UP, "CC versions - COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[3].version, 0, "CC versions - version of COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[4].command_class, COMMAND_CLASS_MULTI_CHANNEL_V4, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[4].version, 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[5].command_class, COMMAND_CLASS_ASSOCIATION, "CC versions - COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[5].version, 2, "CC versions - version of COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[6].command_class, COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[6].version, 3, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[7].command_class, 0xffff, "CC versions - ending mark");
            check_equal(n->node_cc_versions[7].version, 0xff, "CC versions - ending mark in version");
            check_equal(n->dskLen, 0, "No DSK");
            if (n->nEndpoints > 0) {
               check_not_null(n->endpoints, "Endpoints");
               check_equal(list_length(n->endpoints), n->nEndpoints, "Length of endpoints");
               epid = 0;
               rd_ep_database_entry_t *e = list_head(n->endpoints);
               while(epid < n->nEndpoints) {
                  check_equal(e->endpoint_id, epid, "Endpoint ID");
                  const uint8_t node_id_endpoint_info[] = {0x10,0x01,0x5e, 0x25, 0x85, 0x8e, 0x59, 0x86, 0x72, 0x5a, 0x73, 0x7a, 0x68, 0x23};
                  check_equal(e->endpoint_id, epid, "Endpoint ID");
                  check_equal(e->endpoint_info_len, (sizeof(node_id_endpoint_info)/sizeof(node_id_endpoint_info[0])), "Endpoint info length");
                  check_mem(node_id_endpoint_info, e->endpoint_info, e->endpoint_info_len, "Endpoint info mismatch", "Endpoint info match");
                  switch(epid) {
                  case 0:
                  {
                     const char endpoint_name[] = "Binary Switch OnOff";
                     const char endpoint_location[] = "Hiding in the corner";
                     check_equal(e->installer_iconID, ICON_TYPE_GENERIC_ON_OFF_POWER_SWITCH, "installer icon ID");
                     check_equal(e->user_iconID, ICON_TYPE_GENERIC_ON_OFF_POWER_SWITCH, "user icon ID");
                     check_equal(e->endpoint_loc_len, strlen(endpoint_location), "Endpoint location length");
                     check_true(!strncmp(endpoint_location, e->endpoint_location, e->endpoint_loc_len), "Endpoint location");
                     check_equal(e->endpoint_name_len, strlen(endpoint_name), "Endpoint name len");
                     check_true(!strncmp(endpoint_name, e->endpoint_name, e->endpoint_name_len), "Endpoint name");
                     check_equal(e->endpoint_aggr_len, 0, "Endpoint aggr length");
                  }  break;
                  default:
                     check_true(false, "Unexpected endpoint exists");
                     break;
                  }
                  e = list_item_next(e);
                  epid++;
               }
            }
         }  break;
         case 4:
         {
            const char node_name[] = "Multi channel service";
            check_equal(n->nodeid, 4, "Node 4");
            check_equal(n->wakeUp_interval, 0, "Wake Up interval");
            check_equal(n->lastAwake, 0, "last awake");
            check_equal(n->lastUpdate, 0, "last update");
            check_equal(n->security_flags, 1, "security flags");
            check_equal(n->state, STATUS_DONE , "state");
            check_equal(n->manufacturerID, 0, "Manufacturer ID");
            check_equal(n->productType, 3, "Product type");
            check_equal(n->productID, 11, "Product ID");
            check_equal(n->nodeType, BASIC_TYPE_ROUTING_SLAVE, "Node type"); // Not set in JSON file
            check_equal(n->refCnt, 0, "Reference count");
            check_equal(n->mode, MODE_ALWAYSLISTENING, "mode");
            check_equal(n->nEndpoints, 4, "Number of endpoints");
            check_equal(n->nAggEndpoints, 0, "Number of aggregated endpoints");
            check_equal(n->nodeNameLen, strlen(node_name), "Node name length");
            check_true(!strncmp(node_name, n->nodename, n->nodeNameLen), "Node name");
            check_equal(n->node_properties_flags, 4, "Node properties flags");
            check_equal(n->node_cc_versions[0].command_class, COMMAND_CLASS_VERSION, "CC versions - COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[0].version, 2, "CC versions - version of COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[1].command_class, COMMAND_CLASS_ZWAVEPLUS_INFO, "CC versions - COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[1].version, 2, "CC versions - version of COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[2].command_class, COMMAND_CLASS_MANUFACTURER_SPECIFIC, "CC versions - COMMAND_CLASS_MANUFACTURER_SPECIFIC");
            check_equal(n->node_cc_versions[2].version, 2, "CC versions - version of COMMAND_CLASS_MANUFACTURER_SPECIFIC,");
            check_equal(n->node_cc_versions[3].command_class, COMMAND_CLASS_WAKE_UP, "CC versions - COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[3].version, 0, "CC versions - version of COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[4].command_class, COMMAND_CLASS_MULTI_CHANNEL_V4, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[4].version, 4, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[5].command_class, COMMAND_CLASS_ASSOCIATION, "CC versions - COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[5].version, 2, "CC versions - version of COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[6].command_class, COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[6].version, 3, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[7].command_class, 0xffff, "CC versions - ending mark");
            check_equal(n->node_cc_versions[7].version, 0xff, "CC versions - ending mark in version");
            check_equal(n->dskLen, 0, "No DSK");
            if (n->nEndpoints > 0) {
               check_not_null(n->endpoints, "Endpoints");
               check_equal(list_length(n->endpoints), n->nEndpoints, "Length of endpoints");
               epid = 0;
               rd_ep_database_entry_t *e = list_head(n->endpoints);

               while(epid < n->nEndpoints) {
                  check_equal(e->endpoint_id, epid, "Endpoint ID");
                  check_equal(e->endpoint_id, epid, "Endpoint ID");
                  switch(epid) {
                  case 0:
                  {
                     const uint8_t node_id_endpoint_info[] = {0x10,0x01,0x5e, 0x98,
                                                              0x68, 0x23,
                                                              0xf1, 0x00,
                                                              0x25, 0x26, 0x85, 0x8e, 0x59, 0x71, 0x86, 0x72, 0x5a, 0x73, 0x60, 0x7a};
                     const char endpoint_name[] = "EP1";
                     const char endpoint_location[] = "Hiding in the corner";
                     check_equal(e->endpoint_info_len, (sizeof(node_id_endpoint_info)/sizeof(node_id_endpoint_info[0])), "Endpoint info length");
                     check_mem(node_id_endpoint_info, e->endpoint_info, e->endpoint_info_len, "Endpoint info mismatch", "Endpoint info match");
                     check_equal(e->installer_iconID, ICON_TYPE_GENERIC_POWER_STRIP, "installer icon ID");
                     check_equal(e->user_iconID, ICON_TYPE_GENERIC_POWER_STRIP, "user icon ID");
                     check_equal(e->endpoint_loc_len, strlen(endpoint_location), "Endpoint location length");
                     check_true(!strncmp(endpoint_location, e->endpoint_location, e->endpoint_loc_len), "Endpoint location");
                     check_equal(e->endpoint_name_len, strlen(endpoint_name), "Endpoint name len");
                     check_true(!strncmp(endpoint_name, e->endpoint_name, e->endpoint_name_len), "Endpoint name");
                     check_equal(e->endpoint_aggr_len, 0, "Endpoint aggr length");
                  }  break;
                  case 1:
                  {
                     const uint8_t node_id_endpoint_info[] = {0x10,0x01,0x5e, 0x98,
                                                              0xf1, 0x00, 0x25, 0x85, 0x59, 0x8e, 0x71};
                     const char endpoint_name[] = "EP2";
                     const char endpoint_location[] = "Hiding in the corner";
                     check_equal(e->endpoint_info_len, (sizeof(node_id_endpoint_info)/sizeof(node_id_endpoint_info[0])), "Endpoint info length");
                     check_mem(node_id_endpoint_info, e->endpoint_info, e->endpoint_info_len, "Endpoint info mismatch", "Endpoint info match");
                     check_equal(e->installer_iconID, ICON_TYPE_UNASSIGNED, "installer icon ID");
                     check_equal(e->user_iconID, 8, "user icon ID");
                     check_equal(e->endpoint_loc_len, strlen(endpoint_location), "Endpoint location length");
                     check_true(!strncmp(endpoint_location, e->endpoint_location, e->endpoint_loc_len), "Endpoint location");
                     check_equal(e->endpoint_name_len, strlen(endpoint_name), "Endpoint name len");
                     check_true(!strncmp(endpoint_name, e->endpoint_name, e->endpoint_name_len), "Endpoint name");
                     check_equal(e->endpoint_aggr_len, 0, "Endpoint aggr length");
                  }  break;
                  case 2:
                  {
                     const uint8_t node_id_endpoint_info[] = {0x10,0x01,0x5e, 0x98,
                                                              0xf1, 0x00, 0x25, 0x85, 0x59, 0x8e, 0x71};
                     const char endpoint_name[] = "EP3";
                     const char endpoint_location[] = "Hiding in the corner";
                     check_equal(e->endpoint_info_len, (sizeof(node_id_endpoint_info)/sizeof(node_id_endpoint_info[0])), "Endpoint info length");
                     check_mem(node_id_endpoint_info, e->endpoint_info, e->endpoint_info_len, "Endpoint info mismatch", "Endpoint info match");
                     check_equal(e->installer_iconID, ICON_TYPE_UNASSIGNED, "installer icon ID");
                     check_equal(e->user_iconID, 8, "user icon ID");
                     check_equal(e->endpoint_loc_len, strlen(endpoint_location), "Endpoint location length");
                     check_true(!strncmp(endpoint_location, e->endpoint_location, e->endpoint_loc_len), "Endpoint location");
                     check_equal(e->endpoint_name_len, strlen(endpoint_name), "Endpoint name len");
                     check_true(!strncmp(endpoint_name, e->endpoint_name, e->endpoint_name_len), "Endpoint name");
                     check_equal(e->endpoint_aggr_len, 0, "Endpoint aggr length");
                  }  break;
                  case 3:
                  {
                     const uint8_t node_id_endpoint_info[] = {0x10,0x01,0x5e, 0x98,
                                                              0xf1, 0x00, 0x26, 0x85, 0x59, 0x8e, 0x71};
                     const char endpoint_name[] = "EP4";
                     const char endpoint_location[] = "Hiding in the corner";
                     check_equal(e->endpoint_info_len, (sizeof(node_id_endpoint_info)/sizeof(node_id_endpoint_info[0])), "Endpoint info length");
                     check_mem(node_id_endpoint_info, e->endpoint_info, e->endpoint_info_len, "Endpoint info mismatch", "Endpoint info match");
                     check_equal(e->installer_iconID, ICON_TYPE_UNASSIGNED, "installer icon ID");
                     check_equal(e->user_iconID, 8, "user icon ID");
                     check_equal(e->endpoint_loc_len, strlen(endpoint_location), "Endpoint location length");
                     check_true(!strncmp(endpoint_location, e->endpoint_location, e->endpoint_loc_len), "Endpoint location");
                     check_equal(e->endpoint_name_len, strlen(endpoint_name), "Endpoint name len");
                     check_true(!strncmp(endpoint_name, e->endpoint_name, e->endpoint_name_len), "Endpoint name");
                     check_equal(e->endpoint_aggr_len, 0, "Endpoint aggr length");
                  }  break;
                  default:
                     check_true(false, "Unexpected endpoint exists");
                     break;
                  }
                  e = list_item_next(e);
                  epid++;
               }
            }

         }  break;
         case 5:
         {
            const char node_name[] = "PIR Sensor Device";
            check_equal(n->nodeid, 5, "Node 5");
            check_equal(n->wakeUp_interval, 4000, "Wake Up interval");
            check_equal(n->lastAwake, 0, "last awake");
            check_equal(n->lastUpdate, 0, "last update");
            check_equal(n->security_flags, 1, "security flags");
            check_equal(n->state, STATUS_FAILING , "state"); // checking if the FAILING node state is properly persisted in EEPROM file
            check_equal(n->manufacturerID, 0, "Manufacturer ID");
            check_equal(n->productType, 3, "Product type");
            check_equal(n->productID, 3, "Product ID");
            check_equal(n->nodeType, BASIC_TYPE_SLAVE, "Node type"); // Not set in JSON file
            check_equal(n->refCnt, 0, "Reference count");
            check_equal(n->mode, MODE_NONLISTENING, "mode");
            check_equal(n->nEndpoints, 1, "Number of endpoints");
            check_equal(n->nAggEndpoints, 0, "Number of aggregated endpoints");
            check_equal(n->nodeNameLen, strlen(node_name), "Node name length");
            check_true(!strncmp(node_name, n->nodename, n->nodeNameLen), "Node name");
            check_equal(n->node_properties_flags, 4, "Node properties flags");
            check_equal(n->node_version_cap_and_zwave_sw, 0x03, "Version capability report");
            check_equal(n->node_cc_versions[0].command_class, COMMAND_CLASS_VERSION, "CC versions - COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[0].version, 2, "CC versions - version of COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[1].command_class, COMMAND_CLASS_ZWAVEPLUS_INFO, "CC versions - COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[1].version, 2, "CC versions - version of COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[2].command_class, COMMAND_CLASS_MANUFACTURER_SPECIFIC, "CC versions - COMMAND_CLASS_MANUFACTURER_SPECIFIC");
            check_equal(n->node_cc_versions[2].version, 2, "CC versions - version of COMMAND_CLASS_MANUFACTURER_SPECIFIC,");
            check_equal(n->node_cc_versions[3].command_class, COMMAND_CLASS_WAKE_UP, "CC versions - COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[3].version, 2, "CC versions - version of COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[4].command_class, COMMAND_CLASS_MULTI_CHANNEL_V4, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[4].version, 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[5].command_class, COMMAND_CLASS_ASSOCIATION, "CC versions - COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[5].version, 2, "CC versions - version of COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[6].command_class, COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[6].version, 3, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[7].command_class, 0xffff, "CC versions - ending mark");
            check_equal(n->node_cc_versions[7].version, 0xff, "CC versions - ending mark in version");
            check_equal(n->dskLen, 0, "No DSK");
            if (n->nEndpoints > 0) {
               check_not_null(n->endpoints, "Endpoints");
               check_equal(list_length(n->endpoints), n->nEndpoints, "Length of endpoints");
               epid = 0;
               rd_ep_database_entry_t *e = list_head(n->endpoints);
               while(epid < n->nEndpoints) {
                  const uint8_t node_id_endpoint_info[] = {0x10,0x01,0x5e, 0x98,
                                                           0x68, 0x23, 0xF1, 0x00,
                                                           0x86, 0x85, 0x8e, 0x59, 0x72, 0x5a, 0x73, 0x80, 0x71, 0x84, 0x7a};
                  check_equal(e->endpoint_id, epid, "Endpoint ID");
                  check_equal(e->endpoint_info_len, (sizeof(node_id_endpoint_info)/sizeof(node_id_endpoint_info[0])), "Endpoint info length");
                  check_mem(node_id_endpoint_info, e->endpoint_info, e->endpoint_info_len, "Endpoint info mismatch", "Endpoint info match");
                  switch(epid) {
                  case 0:
                  {
                     const char endpoint_name[] = "PIRSensor";
                     const char endpoint_location[] = "Hiding in the corner";
                     check_equal(e->installer_iconID, ICON_TYPE_SPECIFIC_SENSOR_NOTIFICATION_HOME_SECURITY, "installer icon ID");
                     check_equal(e->user_iconID, ICON_TYPE_SPECIFIC_SENSOR_NOTIFICATION_HOME_SECURITY, "user icon ID");
                     check_equal(e->endpoint_loc_len, strlen(endpoint_location), "Endpoint location length");
                     check_true(!strncmp(endpoint_location, e->endpoint_location, e->endpoint_loc_len), "Endpoint location");
                     check_equal(e->endpoint_name_len, strlen(endpoint_name), "Endpoint name len");
                     check_true(!strncmp(endpoint_name, e->endpoint_name, e->endpoint_name_len), "Endpoint name");
                     check_equal(e->endpoint_aggr_len, 0, "Endpoint aggr length");
                  }  break;
                  default:
                     check_true(false, "Unexpected endpoint exists");
                     break;
                  }
                  e = list_item_next(e);
                  epid++;
               }
            }
         }  break;
         default:
            /* Should not reach here */
            check_true(false, "Unexpected node exists");
         }
         rd_data_store_mem_free(n);
      } else {
         check_true(n == NULL, "Empty node");
      }
      nodeid++;
   }
}

void test_example_NoProbe_json() {
   char json_file_path[500];
   zw_controller_t *ctrl;
   MyNodeID = 0x1;
   homeID = UIP_NTOHL(0xE1DB3EDC);
   snprintf(json_file_path, sizeof(json_file_path), "%s/%s", EXAMPLE_JSON_ROOT_PATH, "zgw_example_NoProbe.json");
   test_init_controller_json(&ctrl);
   test_setup(json_file_path, ctrl);
   verify_eeprom_file_NoProbe_json();
   free_ctrl(&ctrl);
}


void test_example_NoProbe_json_offline() {
   char json_file_path[500];
   zw_controller_t *ctrl = 0;
   MyNodeID = 0;
   homeID =0;
   snprintf(json_file_path, sizeof(json_file_path), "%s/%s", EXAMPLE_JSON_ROOT_PATH, "zgw_example_NoProbe.json");
   test_setup(json_file_path, ctrl);
   verify_eeprom_file_NoProbe_json();
}

/******************************************************************************
 * MinimalNoProbe.json
 *****************************************************************************/
/* Verify against network_test_2_nodes.json */
void verify_eeprom_file_MinimalNoProbe_json()
{
   nodeid_t nodeid = 0;
   uint8_t epid = 0;
   // NB: Home id is stored in EEPROM in network byte order (big endian)
   check_equal(UIP_NTOHL(rd_zgw_homeid_get()), 0xEB71529E, "home ID");
   check_equal(rd_zgw_nodeid_get(), 1, "node ID");
   size_t temp_assoc_virtual_nodeid_count = 0;
   nodeid_t temp_assoc_virtual_nodeids[PREALLOCATED_VIRTUAL_NODE_COUNT] = {0};
   nodeid_t expected_virtual_nodeids[PREALLOCATED_VIRTUAL_NODE_COUNT] = {0};
   temp_assoc_virtual_nodeid_count = rd_datastore_unpersist_virtual_nodes(
       temp_assoc_virtual_nodeids, PREALLOCATED_VIRTUAL_NODE_COUNT);
   check_equal(temp_assoc_virtual_nodeid_count, 0,
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
            check_equal(n->nodeid, 1, "Node 1");
            check_equal(n->wakeUp_interval, DEFAULT_WAKE_UP_INTERVAL, "Wake Up interval");
            check_equal(n->lastAwake, 0, "last awake");
            check_equal(n->security_flags, 113, "security flags");
            check_equal(n->state, STATUS_DONE, "state");
            check_equal(n->manufacturerID, 0x1, "Manufacturer ID");
            check_equal(n->productType, 0x1, "Product type");
            check_equal(n->productID, 0x2, "Product ID");
            check_equal(n->nodeType, BASIC_TYPE_STATIC_CONTROLLER, "Node type");
            check_equal(n->refCnt, 0, "Reference count");
            check_equal(n->nEndpoints, 1, "Number of endpoints");
            check_equal(n->nAggEndpoints, 0, "Number of aggregated endpoints");
            check_equal(n->nodeNameLen, strlen(""), "Node name length");

            check_equal(n->node_cc_versions[0].command_class, COMMAND_CLASS_VERSION, "CC versions - COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[0].version, 0, "CC versions - version of COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[1].command_class, COMMAND_CLASS_ZWAVEPLUS_INFO, "CC versions - COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[1].version, 0, "CC versions - version of COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[2].command_class, COMMAND_CLASS_MANUFACTURER_SPECIFIC, "CC versions - COMMAND_CLASS_MANUFACTURER_SPECIFIC");
            check_equal(n->node_cc_versions[2].version, 0, "CC versions - version of COMMAND_CLASS_MANUFACTURER_SPECIFIC,");
            check_equal(n->node_cc_versions[3].command_class, COMMAND_CLASS_WAKE_UP, "CC versions - COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[3].version, 0, "CC versions - version of COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[4].command_class, COMMAND_CLASS_MULTI_CHANNEL_V4, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[4].version, 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[5].command_class, COMMAND_CLASS_ASSOCIATION, "CC versions - COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[5].version, 0, "CC versions - version of COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[6].command_class, COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[6].version, 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[7].command_class, 0xffff, "CC versions - ending mark");
            check_equal(n->node_cc_versions[7].version, 0xff, "CC versions - ending mark in version");

            check_equal(n->dskLen, 0, "DSK length");
            if (n->nEndpoints > 0) {
               const uint8_t node_id_endpoint_info[] = {0x10,0x01,0x5e, 0x86, 0x72, 0x7a, 0x73, 0x22, 0x85, 0x59, 0x70, 0x56, 0x5a, 0x6c, 0x55, 0x7a, 0x74, 0x98, 0x9f,
                                                        0x68, 0x23,
                                                        0xf1, 0x00,
                     0x86, 0x72, 0x73, 0x59, 0x85, 0x7a};
               check_not_null(n->endpoints, "Endpoints");
               check_equal(list_length(n->endpoints), n->nEndpoints, "Length of endpoints");
               epid = 0;
               rd_ep_database_entry_t *e = list_head(n->endpoints);
               while(epid < n->nEndpoints) {
                  check_equal(e->endpoint_id, epid, "Endpoint ID");
                  check_equal(e->installer_iconID, ICON_TYPE_GENERIC_GATEWAY, "installer icon ID");
                  check_equal(e->user_iconID, ICON_TYPE_GENERIC_GATEWAY, "user icon ID");
                  check_equal(e->endpoint_loc_len, 0, "Endpoint location length");
                  check_equal(e->endpoint_name_len, 0, "Endpoint name len");
                  check_equal(e->endpoint_info_len, (sizeof(node_id_endpoint_info)/sizeof(node_id_endpoint_info[0])), "Endpoint info length");
                  check_mem(node_id_endpoint_info, e->endpoint_info, e->endpoint_info_len, "Endpoint info mismatch", "Endpoint info match");
                  check_equal(e->endpoint_aggr_len, 0, "Endpoint aggr length");
                  e = list_item_next(e);
                  epid++;
               }
            }
            break;
         case 2:
         {
            check_equal(n->nodeid, 2, "Node 2");
            check_equal(n->wakeUp_interval, DEFAULT_WAKE_UP_INTERVAL, "Wake Up interval");
            check_equal(n->lastAwake, 0, "last awake");
            check_equal(n->lastUpdate, 0, "last update");
            check_equal(n->security_flags, 1, "security flags");
            check_equal(n->state, STATUS_DONE , "state");
            check_equal(n->manufacturerID, 0, "Manufacturer ID");
            check_equal(n->productType, 3, "Product type");
            check_equal(n->productID, 3, "Product ID");
            check_equal(n->nodeType, BASIC_TYPE_SLAVE, "Node type"); // Not set in JSON file
            check_equal(n->refCnt, 0, "Reference count");
            check_equal(n->nEndpoints, 1, "Number of endpoints");
            check_equal(n->nAggEndpoints, 0, "Number of aggregated endpoints");
            check_equal(n->nodeNameLen, 0, "Node name length");
            check_equal(n->node_properties_flags, 4, "Node properties flags");
            check_equal(n->node_version_cap_and_zwave_sw, 0x00, "Version capability report");
            check_equal(n->node_cc_versions[0].command_class, COMMAND_CLASS_VERSION, "CC versions - COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[0].version, 0, "CC versions - version of COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[1].command_class, COMMAND_CLASS_ZWAVEPLUS_INFO, "CC versions - COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[1].version, 0, "CC versions - version of COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[2].command_class, COMMAND_CLASS_MANUFACTURER_SPECIFIC, "CC versions - COMMAND_CLASS_MANUFACTURER_SPECIFIC");
            check_equal(n->node_cc_versions[2].version, 0, "CC versions - version of COMMAND_CLASS_MANUFACTURER_SPECIFIC,");
            check_equal(n->node_cc_versions[3].command_class, COMMAND_CLASS_WAKE_UP, "CC versions - COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[3].version, 0, "CC versions - version of COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[4].command_class, COMMAND_CLASS_MULTI_CHANNEL_V4, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[4].version, 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[5].command_class, COMMAND_CLASS_ASSOCIATION, "CC versions - COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[5].version, 0, "CC versions - version of COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[6].command_class, COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[6].version, 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[7].command_class, 0xffff, "CC versions - ending mark");
            check_equal(n->node_cc_versions[7].version, 0xff, "CC versions - ending mark in version");
            check_equal(n->dskLen, 0, "No DSK");
            if (n->nEndpoints > 0) {
               check_not_null(n->endpoints, "Endpoints");
               check_equal(list_length(n->endpoints), n->nEndpoints, "Length of endpoints");
               epid = 0;
               rd_ep_database_entry_t *e = list_head(n->endpoints);
               while(epid < n->nEndpoints) {
                  const uint8_t node_id_endpoint_info[] = {0x10,0x01,0x5e, 0x98,
                                                           0x68, 0x23,
                                                           0xF1, 0x00, 0x86, 0x85, 0x8e, 0x59, 0x72, 0x5a, 0x73, 0x80, 0x71, 0x84, 0x7a};
                  check_equal(e->endpoint_id, epid, "Endpoint ID");
                  check_equal(e->endpoint_info_len, (sizeof(node_id_endpoint_info)/sizeof(node_id_endpoint_info[0])), "Endpoint info length");
                  check_mem(node_id_endpoint_info, e->endpoint_info, e->endpoint_info_len, "Endpoint info mismatch", "Endpoint info match");
                  switch(epid) {
                  case 0:
                  {
                     check_equal(e->installer_iconID, ICON_TYPE_UNASSIGNED, "installer icon ID");
                     check_equal(e->user_iconID, ICON_TYPE_UNASSIGNED, "user icon ID");
                     check_equal(e->endpoint_loc_len, 0, "Endpoint location length");
                     check_equal(e->endpoint_name_len, 0, "Endpoint name len");
                     check_equal(e->endpoint_aggr_len, 0, "Endpoint aggr length");
                  }  break;
                  default:
                     check_true(false, "Unexpected endpoint exists");
                     break;
                  }
                  e = list_item_next(e);
                  epid++;
               }
            }
         }  break;
         case 3:
         {
            check_equal(n->nodeid, 3, "Node 3");
            check_equal(n->wakeUp_interval, DEFAULT_WAKE_UP_INTERVAL, "Wake Up interval");
            check_equal(n->lastAwake, 0, "last awake");
            check_equal(n->lastUpdate, 0, "last update");
            check_equal(n->security_flags, 0, "security flags");
            check_equal(n->state, STATUS_DONE , "state");
            check_equal(n->manufacturerID, 0, "Manufacturer ID");
            check_equal(n->productType, 3, "Product type");
            check_equal(n->productID, 2, "Product ID");
            check_equal(n->nodeType, BASIC_TYPE_ROUTING_SLAVE, "Node type"); // Not set in JSON file
            check_equal(n->refCnt, 0, "Reference count");
            check_equal(n->nEndpoints, 1, "Number of endpoints");
            check_equal(n->nAggEndpoints, 0, "Number of aggregated endpoints");
            check_equal(n->nodeNameLen, 0, "Node name length");
            check_equal(n->node_properties_flags, 4, "Node properties flags");
            check_equal(n->node_version_cap_and_zwave_sw, 0x00, "Version capability report");
            check_equal(n->node_cc_versions[0].command_class, COMMAND_CLASS_VERSION, "CC versions - COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[0].version, 0, "CC versions - version of COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[1].command_class, COMMAND_CLASS_ZWAVEPLUS_INFO, "CC versions - COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[1].version, 0, "CC versions - version of COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[2].command_class, COMMAND_CLASS_MANUFACTURER_SPECIFIC, "CC versions - COMMAND_CLASS_MANUFACTURER_SPECIFIC");
            check_equal(n->node_cc_versions[2].version, 0, "CC versions - version of COMMAND_CLASS_MANUFACTURER_SPECIFIC,");
            check_equal(n->node_cc_versions[3].command_class, COMMAND_CLASS_WAKE_UP, "CC versions - COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[3].version, 0, "CC versions - version of COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[4].command_class, COMMAND_CLASS_MULTI_CHANNEL_V4, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[4].version, 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[5].command_class, COMMAND_CLASS_ASSOCIATION, "CC versions - COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[5].version, 0, "CC versions - version of COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[6].command_class, COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[6].version, 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[7].command_class, 0xffff, "CC versions - ending mark");
            check_equal(n->node_cc_versions[7].version, 0xff, "CC versions - ending mark in version");
            check_equal(n->dskLen, 0, "No DSK");
            if (n->nEndpoints > 0) {
               check_not_null(n->endpoints, "Endpoints");
               check_equal(list_length(n->endpoints), n->nEndpoints, "Length of endpoints");
               epid = 0;
               rd_ep_database_entry_t *e = list_head(n->endpoints);
               while(epid < n->nEndpoints) {
                  check_equal(e->endpoint_id, epid, "Endpoint ID");
                  const uint8_t node_id_endpoint_info[] = {0x10,0x01,0x5e, 0x25, 0x85, 0x8e, 0x59, 0x86, 0x72, 0x5a, 0x73, 0x7a,
                                                           0x68, 0x23};
                  check_equal(e->endpoint_id, epid, "Endpoint ID");
                  check_equal(e->endpoint_info_len, (sizeof(node_id_endpoint_info)/sizeof(node_id_endpoint_info[0])), "Endpoint info length");
                  check_mem(node_id_endpoint_info, e->endpoint_info, e->endpoint_info_len, "Endpoint info mismatch", "Endpoint info match");
                  switch(epid) {
                  case 0:
                  {
                     check_equal(e->installer_iconID, ICON_TYPE_UNASSIGNED, "installer icon ID");
                     check_equal(e->user_iconID, ICON_TYPE_UNASSIGNED, "user icon ID");
                     check_equal(e->endpoint_loc_len, 0, "Endpoint location length");
                     check_equal(e->endpoint_name_len, 0, "Endpoint name len");
                     check_equal(e->endpoint_aggr_len, 0, "Endpoint aggr length");
                  }  break;
                  default:
                     check_true(false, "Unexpected endpoint exists");
                     break;
                  }
                  e = list_item_next(e);
                  epid++;
               }
            }
         }  break;
         case 4:
         {
            check_equal(n->nodeid, 4, "Node 4");
            check_equal(n->wakeUp_interval, DEFAULT_WAKE_UP_INTERVAL, "Wake Up interval");
            check_equal(n->lastAwake, 0, "last awake");
            check_equal(n->lastUpdate, 0, "last update");
            check_equal(n->security_flags, 1, "security flags");
            check_equal(n->state, STATUS_DONE , "state");
            check_equal(n->manufacturerID, 0, "Manufacturer ID");
            check_equal(n->productType, 3, "Product type");
            check_equal(n->productID, 11, "Product ID");
            check_equal(n->nodeType, BASIC_TYPE_ROUTING_SLAVE, "Node type"); // Not set in JSON file
            check_equal(n->refCnt, 0, "Reference count");
            check_equal(n->nEndpoints, 4, "Number of endpoints");
            check_equal(n->nAggEndpoints, 0, "Number of aggregated endpoints");
            check_equal(n->nodeNameLen, 0, "Node name length");
            check_equal(n->node_properties_flags, 4, "Node properties flags");
            check_equal(n->node_cc_versions[0].command_class, COMMAND_CLASS_VERSION, "CC versions - COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[0].version, 0, "CC versions - version of COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[1].command_class, COMMAND_CLASS_ZWAVEPLUS_INFO, "CC versions - COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[1].version, 0, "CC versions - version of COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[2].command_class, COMMAND_CLASS_MANUFACTURER_SPECIFIC, "CC versions - COMMAND_CLASS_MANUFACTURER_SPECIFIC");
            check_equal(n->node_cc_versions[2].version, 0, "CC versions - version of COMMAND_CLASS_MANUFACTURER_SPECIFIC,");
            check_equal(n->node_cc_versions[3].command_class, COMMAND_CLASS_WAKE_UP, "CC versions - COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[3].version, 0, "CC versions - version of COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[4].command_class, COMMAND_CLASS_MULTI_CHANNEL_V4, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[4].version, 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[5].command_class, COMMAND_CLASS_ASSOCIATION, "CC versions - COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[5].version, 0, "CC versions - version of COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[6].command_class, COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[6].version, 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[7].command_class, 0xffff, "CC versions - ending mark");
            check_equal(n->node_cc_versions[7].version, 0xff, "CC versions - ending mark in version");
            check_equal(n->dskLen, 0, "No DSK");
            if (n->nEndpoints > 0) {
               check_not_null(n->endpoints, "Endpoints");
               check_equal(list_length(n->endpoints), n->nEndpoints, "Length of endpoints");
               epid = 0;
               rd_ep_database_entry_t *e = list_head(n->endpoints);

               while(epid < n->nEndpoints) {
                  check_equal(e->endpoint_id, epid, "Endpoint ID");
                  check_equal(e->endpoint_id, epid, "Endpoint ID");
                  switch(epid) {
                  case 0:
                  {
                     const uint8_t node_id_endpoint_info[] = {0x10,0x01,0x5e, 0x98,
                                                              0x68, 0x23,
                                                              0xf1, 0x00, 0x25, 0x26, 0x85, 0x8e, 0x59, 0x71, 0x86, 0x72, 0x5a, 0x73, 0x60, 0x7a};
                     check_equal(e->endpoint_info_len, (sizeof(node_id_endpoint_info)/sizeof(node_id_endpoint_info[0])), "Endpoint info length");
                     check_mem(node_id_endpoint_info, e->endpoint_info, e->endpoint_info_len, "Endpoint info mismatch", "Endpoint info match");
                     check_equal(e->installer_iconID, ICON_TYPE_UNASSIGNED, "installer icon ID");
                     check_equal(e->user_iconID, ICON_TYPE_UNASSIGNED, "user icon ID");
                     check_equal(e->endpoint_loc_len, 0, "Endpoint location length");
                     check_equal(e->endpoint_name_len, 0, "Endpoint name len");
                     check_equal(e->endpoint_aggr_len, 0, "Endpoint aggr length");
                  }  break;
                  case 1:
                  {
                     const uint8_t node_id_endpoint_info[] = {0x10,0x01,0x5e, 0x98,
                                                              0xf1, 0x00, 0x25, 0x85, 0x89, 0x8e, 0x71};
                     const char endpoint_name[] = "EP2";
                     const char endpoint_location[] = "Hiding in the corner";
                     check_equal(e->endpoint_info_len, (sizeof(node_id_endpoint_info)/sizeof(node_id_endpoint_info[0])), "Endpoint info length");
                     check_mem(node_id_endpoint_info, e->endpoint_info, e->endpoint_info_len, "Endpoint info mismatch", "Endpoint info match");
                     check_equal(e->installer_iconID, ICON_TYPE_UNASSIGNED, "installer icon ID");
                     check_equal(e->user_iconID, ICON_TYPE_UNASSIGNED, "user icon ID");
                     check_equal(e->endpoint_loc_len, 0, "Endpoint location length");
                     check_equal(e->endpoint_name_len, 0, "Endpoint name len");
                     check_equal(e->endpoint_aggr_len, 0, "Endpoint aggr length");
                  }  break;
                  case 2:
                  {
                     const uint8_t node_id_endpoint_info[] = {0x10,0x01,0x5e, 0x98,
                                                              0xf1, 0x00, 0x25, 0x85, 0x59, 0x8e, 0x71};
                     const char endpoint_name[] = "EP3";
                     const char endpoint_location[] = "Hiding in the corner";
                     check_equal(e->endpoint_info_len, (sizeof(node_id_endpoint_info)/sizeof(node_id_endpoint_info[0])), "Endpoint info length");
                     check_mem(node_id_endpoint_info, e->endpoint_info, e->endpoint_info_len, "Endpoint info mismatch", "Endpoint info match");
                     check_equal(e->installer_iconID, ICON_TYPE_UNASSIGNED, "installer icon ID");
                     check_equal(e->user_iconID, ICON_TYPE_UNASSIGNED, "user icon ID");
                     check_equal(e->endpoint_loc_len, 0, "Endpoint location length");
                     check_equal(e->endpoint_name_len, 0, "Endpoint name len");
                     check_equal(e->endpoint_aggr_len, 0, "Endpoint aggr length");
                  }  break;
                  case 3:
                  {
                     const uint8_t node_id_endpoint_info[] = {0x10,0x01,0x5e, 0x98,
                                                              0xf1, 0x00, 0x26, 0x85, 0x59, 0x8e, 0x71};
                     const char endpoint_name[] = "EP4";
                     const char endpoint_location[] = "Hiding in the corner";
                     check_equal(e->endpoint_info_len, (sizeof(node_id_endpoint_info)/sizeof(node_id_endpoint_info[0])), "Endpoint info length");
                     check_mem(node_id_endpoint_info, e->endpoint_info, e->endpoint_info_len, "Endpoint info mismatch", "Endpoint info match");
                     check_equal(e->installer_iconID, ICON_TYPE_UNASSIGNED, "installer icon ID");
                     check_equal(e->user_iconID, ICON_TYPE_UNASSIGNED, "user icon ID");
                     check_equal(e->endpoint_loc_len, 0, "Endpoint location length");
                     check_equal(e->endpoint_name_len, 0, "Endpoint name len");
                     check_equal(e->endpoint_aggr_len, 0, "Endpoint aggr length");
                  }  break;
                  default:
                     check_true(false, "Unexpected endpoint exists");
                     break;
                  }
                  e = list_item_next(e);
                  epid++;
               }
            }

         }  break;
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

void test_example_MinimalNoProbe_json() {
   char json_file_path[500];
   MyNodeID = 0x1;
   homeID = UIP_NTOHL(0xEB71529E);
   zw_controller_t *ctrl;
   snprintf(json_file_path, sizeof(json_file_path), "%s/%s", EXAMPLE_JSON_ROOT_PATH, "zgw_example_MinimalNoProbe.json");
   test_init_controller_json(&ctrl);
   test_setup(json_file_path, ctrl);
   verify_eeprom_file_MinimalNoProbe_json();
   free_ctrl(&ctrl);
}


/******************************************************************************
 * FullProbe.json
 *****************************************************************************/
/* Verify against network_test_2_nodes.json */
void verify_eeprom_file_FullProbe_json()
{
   nodeid_t nodeid = 0;
   uint8_t epid = 0;
   // NB: Home id is stored in EEPROM in network byte order (big endian)
   check_equal(UIP_NTOHL(rd_zgw_homeid_get()), 0xF9D0C8EF, "home ID");
   check_equal(rd_zgw_nodeid_get(), 1, "node ID");
   size_t temp_assoc_virtual_nodeid_count = 0;
   nodeid_t temp_assoc_virtual_nodeids[PREALLOCATED_VIRTUAL_NODE_COUNT] = {0};
   nodeid_t expected_virtual_nodeids[PREALLOCATED_VIRTUAL_NODE_COUNT] = {5};
   temp_assoc_virtual_nodeid_count = rd_datastore_unpersist_virtual_nodes(
       temp_assoc_virtual_nodeids, PREALLOCATED_VIRTUAL_NODE_COUNT);
   check_equal(temp_assoc_virtual_nodeid_count, 1,
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
            check_equal(n->nodeid, 1, "Node 1");
            check_equal(n->wakeUp_interval, DEFAULT_WAKE_UP_INTERVAL, "Wake Up interval");
            check_equal(n->lastAwake, 0, "last awake");
            check_equal(n->security_flags, 113, "security flags");
            check_equal(n->state, STATUS_CREATED, "state");
            check_equal(n->manufacturerID, 0x0, "Manufacturer ID");
            check_equal(n->productType, 0x0, "Product type");
            check_equal(n->productID, 0x0, "Product ID");
            check_equal(n->nodeType, BASIC_TYPE_STATIC_CONTROLLER, "Node type");
            check_equal(n->refCnt, 0, "Reference count");
            check_equal(n->nEndpoints, 1, "Number of endpoints");
            check_equal(n->nAggEndpoints, 0, "Number of aggregated endpoints");
            check_equal(n->nodeNameLen, 0, "Node name length");

            check_equal(n->node_cc_versions[0].command_class, COMMAND_CLASS_VERSION, "CC versions - COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[0].version, 0, "CC versions - version of COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[1].command_class, COMMAND_CLASS_ZWAVEPLUS_INFO, "CC versions - COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[1].version, 0, "CC versions - version of COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[2].command_class, COMMAND_CLASS_MANUFACTURER_SPECIFIC, "CC versions - COMMAND_CLASS_MANUFACTURER_SPECIFIC");
            check_equal(n->node_cc_versions[2].version, 0, "CC versions - version of COMMAND_CLASS_MANUFACTURER_SPECIFIC,");
            check_equal(n->node_cc_versions[3].command_class, COMMAND_CLASS_WAKE_UP, "CC versions - COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[3].version, 0, "CC versions - version of COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[4].command_class, COMMAND_CLASS_MULTI_CHANNEL_V4, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[4].version, 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[5].command_class, COMMAND_CLASS_ASSOCIATION, "CC versions - COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[5].version, 0, "CC versions - version of COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[6].command_class, COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[6].version, 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[7].command_class, 0xffff, "CC versions - ending mark");
            check_equal(n->node_cc_versions[7].version, 0xff, "CC versions - ending mark in version");

            check_equal(n->dskLen, 0, "DSK length");
            if (n->nEndpoints > 0) {
               check_not_null(n->endpoints, "Endpoints");
               check_equal(list_length(n->endpoints), n->nEndpoints, "Length of endpoints");
               epid = 0;
               rd_ep_database_entry_t *e = list_head(n->endpoints);
               while(epid < n->nEndpoints) {
                  check_equal(e->endpoint_id, epid, "Endpoint ID");
                  check_equal(e->installer_iconID, ICON_TYPE_GENERIC_GATEWAY, "installer icon ID");
                  check_equal(e->user_iconID, ICON_TYPE_GENERIC_GATEWAY, "user icon ID");
                  check_equal(e->endpoint_loc_len, 0, "Endpoint location length");
                  check_equal(e->endpoint_name_len, 0, "Endpoint name len");
                  check_equal(e->endpoint_info_len, 0, "Endpoint info length");
                  check_equal(e->endpoint_aggr_len, 0, "Endpoint aggr length");
                  e = list_item_next(e);
                  epid++;
               }
            }
            break;
         case 2:
         {
            check_equal(n->nodeid, 2, "Node 2");
            check_equal(n->wakeUp_interval, DEFAULT_WAKE_UP_INTERVAL, "Wake Up interval");
            check_equal(n->lastAwake, 0, "last awake");
            check_equal(n->lastUpdate, 0, "last update");
            check_equal(n->security_flags, 1, "security flags");
            check_equal(n->state, STATUS_CREATED , "state");
            check_equal(n->manufacturerID, 0, "Manufacturer ID");
            check_equal(n->productType, 0, "Product type");
            check_equal(n->productID, 0, "Product ID");
            check_equal(n->nodeType, BASIC_TYPE_SLAVE, "Node type"); // Not set in JSON file
            check_equal(n->refCnt, 0, "Reference count");
            check_equal(n->nEndpoints, 1, "Number of endpoints");
            check_equal(n->nAggEndpoints, 0, "Number of aggregated endpoints");
            check_equal(n->nodeNameLen, 0, "Node name length");
            check_equal(n->node_properties_flags, 4, "Node properties flags");
            check_equal(n->node_version_cap_and_zwave_sw, 0x00, "Version capability report");
            check_equal(n->node_cc_versions[0].command_class, COMMAND_CLASS_VERSION, "CC versions - COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[0].version, 0, "CC versions - version of COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[1].command_class, COMMAND_CLASS_ZWAVEPLUS_INFO, "CC versions - COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[1].version, 0, "CC versions - version of COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[2].command_class, COMMAND_CLASS_MANUFACTURER_SPECIFIC, "CC versions - COMMAND_CLASS_MANUFACTURER_SPECIFIC");
            check_equal(n->node_cc_versions[2].version, 0, "CC versions - version of COMMAND_CLASS_MANUFACTURER_SPECIFIC,");
            check_equal(n->node_cc_versions[3].command_class, COMMAND_CLASS_WAKE_UP, "CC versions - COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[3].version, 0, "CC versions - version of COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[4].command_class, COMMAND_CLASS_MULTI_CHANNEL_V4, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[4].version, 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[5].command_class, COMMAND_CLASS_ASSOCIATION, "CC versions - COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[5].version, 0, "CC versions - version of COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[6].command_class, COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[6].version, 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[7].command_class, 0xffff, "CC versions - ending mark");
            check_equal(n->node_cc_versions[7].version, 0xff, "CC versions - ending mark in version");
            check_equal(n->dskLen, 0, "No DSK");
            if (n->nEndpoints > 0) {
               check_not_null(n->endpoints, "Endpoints");
               check_equal(list_length(n->endpoints), n->nEndpoints, "Length of endpoints");
               epid = 0;
               rd_ep_database_entry_t *e = list_head(n->endpoints);
               while(epid < n->nEndpoints) {
                  check_equal(e->endpoint_id, epid, "Endpoint ID");
                  check_equal(e->endpoint_info_len, 0, "Endpoint info length");
                  switch(epid) {
                  case 0:
                  {
                     check_equal(e->installer_iconID, ICON_TYPE_UNASSIGNED, "installer icon ID");
                     check_equal(e->user_iconID, ICON_TYPE_UNASSIGNED, "user icon ID");
                     check_equal(e->endpoint_loc_len, 0, "Endpoint location length");
                     check_equal(e->endpoint_name_len, 0, "Endpoint name len");
                     check_equal(e->endpoint_aggr_len, 0, "Endpoint aggr length");
                  }  break;
                  default:
                     check_true(false, "Unexpected endpoint exists");
                     break;
                  }
                  e = list_item_next(e);
                  epid++;
               }
            }
         }  break;
         case 3:
         {
            check_equal(n->nodeid, 3, "Node 3");
            check_equal(n->wakeUp_interval, DEFAULT_WAKE_UP_INTERVAL, "Wake Up interval");
            check_equal(n->lastAwake, 0, "last awake");
            check_equal(n->lastUpdate, 0, "last update");
            check_equal(n->security_flags, 0, "security flags");
            check_equal(n->state, STATUS_CREATED , "state");
            check_equal(n->manufacturerID, 0, "Manufacturer ID");
            check_equal(n->productType, 0, "Product type");
            check_equal(n->productID, 0, "Product ID");
            check_equal(n->nodeType, BASIC_TYPE_ROUTING_SLAVE, "Node type"); // Not set in JSON file
            check_equal(n->refCnt, 0, "Reference count");
            check_equal(n->nEndpoints, 1, "Number of endpoints");
            check_equal(n->nAggEndpoints, 0, "Number of aggregated endpoints");
            check_equal(n->nodeNameLen, 0, "Node name length");
            check_equal(n->node_properties_flags, 4, "Node properties flags");
            check_equal(n->node_version_cap_and_zwave_sw, 0x00, "Version capability report");
            check_equal(n->node_cc_versions[0].command_class, COMMAND_CLASS_VERSION, "CC versions - COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[0].version, 0, "CC versions - version of COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[1].command_class, COMMAND_CLASS_ZWAVEPLUS_INFO, "CC versions - COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[1].version, 0, "CC versions - version of COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[2].command_class, COMMAND_CLASS_MANUFACTURER_SPECIFIC, "CC versions - COMMAND_CLASS_MANUFACTURER_SPECIFIC");
            check_equal(n->node_cc_versions[2].version, 0, "CC versions - version of COMMAND_CLASS_MANUFACTURER_SPECIFIC,");
            check_equal(n->node_cc_versions[3].command_class, COMMAND_CLASS_WAKE_UP, "CC versions - COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[3].version, 0, "CC versions - version of COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[4].command_class, COMMAND_CLASS_MULTI_CHANNEL_V4, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[4].version, 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[5].command_class, COMMAND_CLASS_ASSOCIATION, "CC versions - COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[5].version, 0, "CC versions - version of COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[6].command_class, COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[6].version, 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[7].command_class, 0xffff, "CC versions - ending mark");
            check_equal(n->node_cc_versions[7].version, 0xff, "CC versions - ending mark in version");
            check_equal(n->dskLen, 0, "No DSK");
            if (n->nEndpoints > 0) {
               check_not_null(n->endpoints, "Endpoints");
               check_equal(list_length(n->endpoints), n->nEndpoints, "Length of endpoints");
               epid = 0;
               rd_ep_database_entry_t *e = list_head(n->endpoints);
               while(epid < n->nEndpoints) {
                  check_equal(e->endpoint_id, epid, "Endpoint ID");
                  check_equal(e->endpoint_info_len, 0, "Endpoint info length");
                  switch(epid) {
                  case 0:
                  {
                     check_equal(e->installer_iconID, ICON_TYPE_UNASSIGNED, "installer icon ID");
                     check_equal(e->user_iconID, ICON_TYPE_UNASSIGNED, "user icon ID");
                     check_equal(e->endpoint_loc_len, 0, "Endpoint location length");
                     check_equal(e->endpoint_name_len, 0, "Endpoint name len");
                     check_equal(e->endpoint_aggr_len, 0, "Endpoint aggr length");
                  }  break;
                  default:
                     check_true(false, "Unexpected endpoint exists");
                     break;
                  }
                  e = list_item_next(e);
                  epid++;
               }
            }
         }  break;
         case 4:
         {
            check_equal(n->nodeid, 4, "Node 4");
            check_equal(n->wakeUp_interval, DEFAULT_WAKE_UP_INTERVAL, "Wake Up interval");
            check_equal(n->lastAwake, 0, "last awake");
            check_equal(n->lastUpdate, 0, "last update");
            check_equal(n->security_flags, 1, "security flags");
            check_equal(n->state, STATUS_CREATED , "state");
            check_equal(n->manufacturerID, 0, "Manufacturer ID");
            check_equal(n->productType, 0, "Product type");
            check_equal(n->productID, 0, "Product ID");
            check_equal(n->nodeType, BASIC_TYPE_ROUTING_SLAVE, "Node type"); // Not set in JSON file
            check_equal(n->refCnt, 0, "Reference count");
            check_equal(n->nEndpoints, 4, "Number of endpoints");
            check_equal(n->nAggEndpoints, 0, "Number of aggregated endpoints");
            check_equal(n->nodeNameLen, 0, "Node name length");
            check_equal(n->node_properties_flags, 4, "Node properties flags");
            check_equal(n->node_cc_versions[0].command_class, COMMAND_CLASS_VERSION, "CC versions - COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[0].version, 0, "CC versions - version of COMMAND_CLASS_VERSION");
            check_equal(n->node_cc_versions[1].command_class, COMMAND_CLASS_ZWAVEPLUS_INFO, "CC versions - COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[1].version, 0, "CC versions - version of COMMAND_CLASS_ZWAVEPLUS_INFO");
            check_equal(n->node_cc_versions[2].command_class, COMMAND_CLASS_MANUFACTURER_SPECIFIC, "CC versions - COMMAND_CLASS_MANUFACTURER_SPECIFIC");
            check_equal(n->node_cc_versions[2].version, 0, "CC versions - version of COMMAND_CLASS_MANUFACTURER_SPECIFIC,");
            check_equal(n->node_cc_versions[3].command_class, COMMAND_CLASS_WAKE_UP, "CC versions - COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[3].version, 0, "CC versions - version of COMMAND_CLASS_WAKE_UP");
            check_equal(n->node_cc_versions[4].command_class, COMMAND_CLASS_MULTI_CHANNEL_V4, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[4].version, 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_V4");
            check_equal(n->node_cc_versions[5].command_class, COMMAND_CLASS_ASSOCIATION, "CC versions - COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[5].version, 0, "CC versions - version of COMMAND_CLASS_ASSOCIATION");
            check_equal(n->node_cc_versions[6].command_class, COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, "CC versions - COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[6].version, 0, "CC versions - version of COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3");
            check_equal(n->node_cc_versions[7].command_class, 0xffff, "CC versions - ending mark");
            check_equal(n->node_cc_versions[7].version, 0xff, "CC versions - ending mark in version");
            check_equal(n->dskLen, 0, "No DSK");
            if (n->nEndpoints > 0) {
               check_not_null(n->endpoints, "Endpoints");
               check_equal(list_length(n->endpoints), n->nEndpoints, "Length of endpoints");
               epid = 0;
               rd_ep_database_entry_t *e = list_head(n->endpoints);

               while(epid < n->nEndpoints) {
                  check_equal(e->endpoint_id, epid, "Endpoint ID");
                  check_equal(e->endpoint_info_len, 0, "Endpoint info length");
                  e = list_item_next(e);
                  epid++;
               }
            }

         }  break;
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

void test_example_FullProbe_json() {
   char json_file_path[500];
   MyNodeID = 0x1;
   homeID = UIP_NTOHL(0xF9D0C8EF);
   zw_controller_t *ctrl;
   snprintf(json_file_path, sizeof(json_file_path), "%s/%s", EXAMPLE_JSON_ROOT_PATH, "zgw_example_FullProbe.json");
   test_init_controller_json(&ctrl);
   test_setup(json_file_path, ctrl);
   verify_eeprom_file_FullProbe_json();
   free_ctrl(&ctrl);
}


int main(void) {
   verbosity = test_case_start_stop;

   /* Start the logging system at the start of main. */
   zgw_log_setup("restore_example_json.log");

   /* Log that we have entered this function. */
   zgw_log_enter();

   /* Log at level 1 */
   zgw_log(1, "ZGWlog test arg %d\n", 7);

   test_example_NoProbe_json();
   test_example_MinimalNoProbe_json();
   test_example_FullProbe_json();

   test_example_NoProbe_json_offline();
   close_run();

   /* Close down the logging system at the end of main(). */
   data_store_exit();
   zgw_log_teardown();

   return numErrs;
}
