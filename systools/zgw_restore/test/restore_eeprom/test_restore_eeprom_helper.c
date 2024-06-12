/* © 2019 Silicon Laboratories Inc. */

#include "zgw_data.h"
#include "ZW_classcmd.h"
#include "TYPES.H"
#include "ZW_controller_api.h"
#include "NodeCache.h"
#include "RD_internal.h"
#include "RD_types.h"
#include "zw_network_info.h"
#include "lib/list.h"
#include "uip.h"
#include "test_restore_eeprom_helper.h"

#include "ZW_transport_api.h"

#include <stdlib.h>

extern cc_version_pair_t controlled_cc_v[];

int generate_network(zgw_node_data_t **zgw_node, uint16_t num_nodes)
{
  if (num_nodes > ZW_MAX_NODES) {
    printf("%d nodes exceeds maximum # of nodes %d\n", num_nodes, ZW_MAX_NODES);
    return -1;
  }
  printf("Generating network with %u node\n", num_nodes);
  homeID = test_homeID;
  MyNodeID = test_MyNodeID;
  zw_controller_add(0, NULL);
  nodeid_t nodeid = 0;
  uint8_t epid = 0;
  zgw_node_ep_data_t *zgw_node_ep = NULL;
  for(nodeid = 0; nodeid < num_nodes; nodeid++) {
    zgw_node[nodeid] = malloc(sizeof(zgw_node_data_t));
    memset(zgw_node[nodeid], 0, sizeof(zgw_node_data_t));
    if (*zgw_node == NULL) {
      printf("malloc failed when generatin network\n");
      return -1;
    }
    zgw_node[nodeid]->node_gwdata.zw_node_data.node_id = nodeid;
    zgw_node[nodeid]->node_gwdata.security_flags = (NODE_FLAG_SECURITY2_ACCESS
                                        | NODE_FLAG_SECURITY2_AUTHENTICATED
                                        | NODE_FLAG_SECURITY2_UNAUTHENTICATED);

    zgw_node[nodeid]->node_gwdata.probe_state.state = STATUS_DONE;
    zgw_node[nodeid]->node_gwdata.probe_state.probe_flags = RD_NODE_FLAG_PROBE_HAS_COMPLETED;
    zgw_node[nodeid]->node_gwdata.probe_state.node_properties_flags = 0x0000;
    zgw_node[nodeid]->node_gwdata.probe_state.node_version_cap_and_zwave_sw = 0x01;
    zgw_node[nodeid]->node_gwdata.probe_state.node_is_zws_probed = 0x01;
    zgw_node[nodeid]->node_gwdata.probe_state.node_cc_versions_len = controlled_cc_v_size();
    zgw_node[nodeid]->node_gwdata.probe_state.node_cc_versions = malloc(zgw_node[nodeid]->node_gwdata.probe_state.node_cc_versions_len);
    memcpy(zgw_node[nodeid]->node_gwdata.probe_state.node_cc_versions, controlled_cc_v, zgw_node[nodeid]->node_gwdata.probe_state.node_cc_versions_len);
    if (zgw_node[nodeid]->node_gwdata.probe_state.node_cc_versions == NULL) {
      printf("malloc failed when generating node cc versions\n");
      return -1;
    }
    zgw_node[nodeid]->node_gwdata.nEndpoints = 4;
    zgw_node[nodeid]->node_gwdata.mode = MODE_NONLISTENING;
    LIST_STRUCT_INIT(&(zgw_node[nodeid]->node_gwdata), endpoints);
    for (epid = 0; epid < zgw_node[nodeid]->node_gwdata.nEndpoints; epid++) {
      zgw_node_ep = malloc(sizeof(zgw_node_ep_data_t));
      if (zgw_node_ep == NULL) {
        printf("malloc failed when generating endpoints\n");
        return -1;
      }
      zgw_node_ep->endpoint_id = epid;
      zgw_node_ep->installer_iconID = ICON_TYPE_GENERIC_GATEWAY;
      zgw_node_ep->user_iconID = ICON_TYPE_GENERIC_GATEWAY;
      zgw_node_ep->state = EP_STATE_PROBE_DONE;
      zgw_node_ep->ep_mDNS_data.endpoint_location = "office";
      zgw_node_ep->ep_mDNS_data.endpoint_loc_len = strlen(zgw_node_ep->ep_mDNS_data.endpoint_location);

      zgw_node_ep->ep_mDNS_data.endpoint_name = "sensor";
      zgw_node_ep->ep_mDNS_data.endpoint_name_len = strlen(zgw_node_ep->ep_mDNS_data.endpoint_name);

      zgw_node_ep->endpoint_agg = NULL;
      zgw_node_ep->endpoint_aggr_len = 0;

      zgw_node_ep->endpoint_info = NULL;
      zgw_node_ep->endpoint_info_len = 0;
      list_add(zgw_node[nodeid]->node_gwdata.endpoints, zgw_node_ep);
    }

    zgw_node[nodeid]->node_gwdata.nAggEndpoints = 0;

    zgw_node[nodeid]->node_gwdata.node_prod_id.manufacturerID = 0x0000;
    zgw_node[nodeid]->node_gwdata.node_prod_id.productType = 0x0001;
    zgw_node[nodeid]->node_gwdata.node_prod_id.productID = 0x0001;

    zgw_node[nodeid]->node_gwdata.liveness.wakeUp_interval = 4200;
    zgw_node[nodeid]->node_gwdata.liveness.lastAwake = 120;
    zgw_node[nodeid]->node_gwdata.liveness.lastUpdate = 120;

    zgw_node[nodeid]->node_gwdata.zw_node_data.capability = NODEINFO_LISTENING_SUPPORT;
    zgw_node[nodeid]->node_gwdata.zw_node_data.node_type.basic = BASIC_TYPE_SLAVE;

//    uip_ip6addr_t ipv6 = { .u8 = {0xff, 0x00, 0xbb, 0xcc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, nodeid + 1}};
//    zgw_node[nodeid]->ip_data.ipv6_address = ipv6;
    zgw_node[nodeid]->ip_data.mDNS_node_name = "sensor";
    zgw_node[nodeid]->ip_data.mDNS_node_name_len = strlen(zgw_node[nodeid]->ip_data.mDNS_node_name);

    zgw_node[nodeid]->uid_type = node_uid_dsk;
    zgw_node[nodeid]->node_uid.dsk_uid.dsk_len = 16;
    uint8_t dsk[16] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    zgw_node[nodeid]->node_uid.dsk_uid.dsk = malloc(zgw_node[nodeid]->node_uid.dsk_uid.dsk_len);
    if (zgw_node[nodeid]->node_uid.dsk_uid.dsk == NULL) {
      printf("malloc failed when generating DSK");
      return -1;
    }
    memcpy(zgw_node[nodeid]->node_uid.dsk_uid.dsk, dsk, zgw_node[nodeid]->node_uid.dsk_uid.dsk_len);

  }
  return 0;

}

int teardown_network(zgw_node_data_t *zgw_node[ZW_MAX_NODES])
{
  nodeid_t nodeid = 0;
  for (nodeid = 0; nodeid < ZW_MAX_NODES; nodeid++) {
    if (zgw_node[nodeid] != NULL) {
      /* Free node cc versions */
      if (zgw_node[nodeid]->node_gwdata.probe_state.node_cc_versions != NULL) {
        free(zgw_node[nodeid]->node_gwdata.probe_state.node_cc_versions);
      }
      /* Free endpoint through linked-list */
      zgw_node_ep_data_t *zgw_node_ep = list_head(zgw_node[nodeid]->node_gwdata.endpoints);
      while(zgw_node_ep != NULL) {
        zgw_node_ep_data_t *to_be_free = zgw_node_ep;
        zgw_node_ep = list_item_next(zgw_node_ep);
        free(to_be_free);
      }
      /* Free DSK */
      free(zgw_node[nodeid]->node_uid.dsk_uid.dsk);
      /* Free the node pointer in the end */
      free(zgw_node[nodeid]);
    }
  }
  return 0;
}
