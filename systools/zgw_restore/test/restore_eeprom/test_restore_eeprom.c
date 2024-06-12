/* © 2019 Silicon Laboratories Inc. */

#include <stdlib.h>

#include "zgwr_eeprom.h"
#include "zgw_data.h"
#include "zgw_restore_cfg.h"
#include "lib/zgw_log.h"
#include "RD_DataStore.h"
#include "ZW_classcmd_ex.h"
#include "zw_network_info.h"

#include "test_helpers.h"
#include "test_restore_help.h"
#include "test_restore_eeprom_helper.h"

extern zip_gateway_backup_data_t backup_rep;
extern ZGW_restore_config_t cfg;

zgw_log_id_define(test_restore_eeprom);
zgw_log_id_default_set(test_restore_eeprom);

void test_restore_eeprom_init(uint8_t nums) {
  restore_cfg.data_path = "./";
  generate_network(backup_rep.zgw.zw_nodes, nums);
}

void test_restore_eeprom_teardown() {
  teardown_network(backup_rep.zgw.zw_nodes);
}

void test_restore_eeprom_verify(uint8_t nums) {
  int nodeid = 0, epid = 0, found_nodeids = 0;
  // NB: Home id is stored in EEPROM in network byte order (big endian)
  uint32_t zgw_homeid = rd_zgw_homeid_get();
  nodeid_t zgw_nodeid = rd_zgw_nodeid_get();
  check_true(zgw_homeid == test_homeID, "ZGW home ID was imported correctly");
  check_true(zgw_nodeid == test_MyNodeID, "ZGW node ID was imported correctly");
  while (nodeid < ZW_MAX_NODES && found_nodeids < nums) {
    /* Node info from eeprom.dat */
    rd_node_database_entry_t *n = rd_data_store_read(nodeid);

    /* Node info from internal data structure */
    const zgw_node_data_t *zgw_node_data = zgw_node_data_get(nodeid);

    // If the nodeid exists in the eeprom or internal data struct, increment the found_nodeids counter
    if (NULL != n || NULL != zgw_node_data) {
      found_nodeids++;
      if (NULL == n) // if one of them only is not NULL, we are in trouble
      {
        check_true(0, "rd_data_store_read(nodeid) returned NULL");
        break;
      }
      if (NULL == zgw_node_data)
      {
        check_true(0, "zgw_node_data_get(nodeid) returned NULL");
        break;
      }
    }

    check_true(zgw_node_data->node_gwdata.liveness.wakeUp_interval == n->wakeUp_interval, "Wake Up Interval");
    check_true(zgw_node_data->node_gwdata.liveness.lastAwake == n->lastAwake, "Last awake");
    check_true(zgw_node_data->node_gwdata.liveness.lastUpdate == n->lastUpdate, "Last update");
//    check_mem((uint8_t*)(&(zgw_node_data->ip_data.ipv6_address)), (uint8_t*)(&(n->ipv6_address)), sizeof(&zgw_node_data->ip_data.ipv6_address)
//        , "IPv6 address eeprom restore failed test", "IPv6 address eeprom restore passed");
    check_true(nodeid == n->nodeid, "Node ID");
    check_true(zgw_node_data->node_gwdata.security_flags == n->security_flags, "Security flags");
    check_true((rd_node_state_t)zgw_node_data->node_gwdata.probe_state.interview_state == n->state, "State");
    check_true(zgw_node_data->node_gwdata.node_prod_id.manufacturerID == n->manufacturerID, "Manufacturer ID");
    check_true(zgw_node_data->node_gwdata.node_prod_id.productType == n->productType, "Product type");
    check_true(zgw_node_data->node_gwdata.node_prod_id.productID == n->productID, "Product ID");
    check_true(zgw_node_data->node_gwdata.zw_node_data.node_type.basic == n->nodeType, "Node type");
    check_true(0 == n->refCnt, "Reference count");
    check_true(zgw_node_data->node_gwdata.nEndpoints == n->nEndpoints, "Number of endpoints");
    check_true(zgw_node_data->node_gwdata.nAggEndpoints == n->nAggEndpoints, "Number of aggregated endpoints");

    if (n->nEndpoints >0) {
      check_not_null(n->endpoints, "Endpoints");
      check_true(list_length(n->endpoints) == n->nEndpoints, "Length of endpoints");
      epid = 0;
      rd_ep_database_entry_t *e = list_head(n->endpoints);
      while(epid < n->nEndpoints) {
        const zgw_node_ep_data_t *zgw_node_ep_data = zgw_node_ep_data_get(nodeid, epid);
        check_not_null(zgw_node_ep_data, "Endpoint data");
        check_true(e->endpoint_id == epid, "Endpoint ID");
        check_true(e->installer_iconID == ICON_TYPE_GENERIC_GATEWAY, "installer icon ID");
        check_true(e->user_iconID == ICON_TYPE_GENERIC_GATEWAY, "user icon ID");
        check_true(e->state == EP_STATE_PROBE_DONE, "Endpoint state");
        if (zgw_node_ep_data) {
          check_true(zgw_node_ep_data->ep_mDNS_data.endpoint_loc_len == e->endpoint_loc_len, "Endpoint location length");
          if (zgw_node_ep_data->ep_mDNS_data.endpoint_location != NULL) {
            check_mem((uint8_t*)(zgw_node_ep_data->ep_mDNS_data.endpoint_location), (uint8_t*)(e->endpoint_location), e->endpoint_loc_len
                , "Endpoint location eeprom retore failed test", "Endpoint location eeprom restore passed");
          }
          check_true(zgw_node_ep_data->ep_mDNS_data.endpoint_name_len == e->endpoint_name_len, "Endpoint name eeprom restore length");
          if (zgw_node_ep_data->ep_mDNS_data.endpoint_name != NULL) {
            check_mem((uint8_t*)(zgw_node_ep_data->ep_mDNS_data.endpoint_name), (uint8_t*)(e->endpoint_name), e->endpoint_name_len
                , "Endpoint name eeprom retore failed test", "Endpoint name eeprom restore passed");
          }
          check_true(zgw_node_ep_data->endpoint_info_len == e->endpoint_info_len, "Endpoint info length");
          if (zgw_node_ep_data->endpoint_info != NULL) {
            check_mem((uint8_t*)(zgw_node_ep_data->endpoint_info), (uint8_t*)(e->endpoint_info), e->endpoint_info_len
                , "Endpoint info eeprom retore failed test", "Endpoint info eeprom restore passed");
          }
          check_true(zgw_node_ep_data->endpoint_aggr_len == e->endpoint_aggr_len, "Endpoint aggregated length");
          if (zgw_node_ep_data->endpoint_agg != NULL) {
            check_mem((uint8_t*)(zgw_node_ep_data->endpoint_agg), (uint8_t*)(e->endpoint_agg), e->endpoint_aggr_len
                , "Endpoint aggr eeprom retore failed test", "Endpoint aggr eeprom restore passed");
          }
        }
        e = list_item_next(e);
        epid++;
      }
    }

    check_true(zgw_node_data->ip_data.mDNS_node_name_len == n->nodeNameLen, "Node name length");
    if (zgw_node_data->ip_data.mDNS_node_name != NULL) {
      check_mem((uint8_t*)(zgw_node_data->ip_data.mDNS_node_name), (uint8_t*)(n->nodename), n->nodeNameLen
          , "Node name eeprom restore failed test", "Node name eeprom retore passed");
    }
    check_true(zgw_node_data->node_gwdata.probe_state.node_version_cap_and_zwave_sw == n->node_version_cap_and_zwave_sw, "Node version capability and Z-Wave software");
    check_true(zgw_node_data->node_gwdata.probe_state.probe_flags == n->probe_flags, "Probe flags");
    check_true(zgw_node_data->node_gwdata.probe_state.node_properties_flags == n->node_properties_flags, "Node properties flags");
    check_true(zgw_node_data->node_gwdata.probe_state.node_cc_versions_len == n->node_cc_versions_len, "Node CC version length");
    check_mem((uint8_t*)(zgw_node_data->node_gwdata.probe_state.node_cc_versions), (uint8_t*)(n->node_cc_versions), n->node_cc_versions_len
        , "Node CC version eeprom restore failed test", "Node CC version eeprom restore passed");
    check_true(zgw_node_data->node_gwdata.probe_state.node_is_zws_probed == n->node_is_zws_probed, "Node is ZWS probed flag");
    check_true(zgw_node_data->node_gwdata.mode == MODE_NONLISTENING, "Mode");
    check_true(zgw_node_data->node_uid.dsk_uid.dsk_len == n->dskLen, "DSK length");
    check_mem((uint8_t*)zgw_node_data->node_uid.dsk_uid.dsk, (uint8_t*)n->dsk, n->dskLen
        , "DSK eeprom restore failed test", "DSK eeprom restore passed");

    nodeid++;
    rd_data_store_mem_free(n);
  }
  check_true(found_nodeids == nums, "Found NodeIDs in eeprom.dat/data structure");

}

int main(void) {
    verbosity = test_case_start_stop;

    /* Start the logging system at the start of main. */
    zgw_log_setup("restore_eeprom.log");

    /* Log that we have entered this function. */
    zgw_log_enter();

    /* Log at level 1 */
    zgw_log(1, "ZGWlog test arg %d\n", 7);

    uint8_t nums = 94;

    remove(RESTORE_EEPROM_FILENAME);
    test_restore_eeprom_init(nums);
    zgw_restore_eeprom_file();
    test_restore_eeprom_verify(nums);
    test_restore_eeprom_teardown();

    close_run();

    /* Close down the logging system at the end of main(). */
    zgw_log_teardown();

   return numErrs;
}
