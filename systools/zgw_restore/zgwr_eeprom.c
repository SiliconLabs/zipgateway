/* © 2019 Silicon Laboratories Inc. */
#include <TYPES.H>
#include "zgw_restore_cfg.h"
#include "zgw_data.h"
#include "zgwr_eeprom.h"
#include "dev/eeprom.h"
#include "lib/list.h"
#include "net/uip.h"
#include "DataStore.h" /* For rd_eeprom_static_hdr */
#include "RD_DataStore.h"
#include "RD_internal.h"
#include "ZW_controller_api.h"
#include "zgw_str.h"
#include <stdlib.h>

/* Used by data_store_init() */
extern uint8_t MyNodeID;
extern uint32_t homeID;

/* Used by eeprom_init() implemented in contiki/platform/linux/File_eeprom.c */
const char* linux_conf_database_file;
/* Path to restore the eeprom file */
char* eeprom_fullpath;

static int restore_eeprom_init(void) {
  /* Assume the installation path has "/" at the end to form the fullpath of eeprom file
   * 1 being the length of null-terminated string */
  const char *path = data_dir_path_get();
  int eeprom_fullpath_len = 0;
  if (path != NULL) {
    eeprom_fullpath_len = strlen(path) + strlen(RESTORE_EEPROM_FILENAME) + 1;
  }
  eeprom_fullpath = malloc(eeprom_fullpath_len);
  if (eeprom_fullpath != NULL) {
    snprintf(eeprom_fullpath, eeprom_fullpath_len, "%s%s", path, RESTORE_EEPROM_FILENAME);
  } else {
    printf("malloc failed.\n");
    return -1;
  }
  linux_conf_database_file = eeprom_fullpath;

  printf("Restoring gateway in network home ID: 0x%x, gateway ID: %d.\n",
         homeID, MyNodeID);
  /* Fill in the magic, version, homeID, and MyNodeID - RD_DataStore API */
  data_store_init();

  return 0;
}

void rd_node_dbe_init(rd_node_database_entry_t *nd) {
   memset(nd, 0, sizeof(rd_node_database_entry_t));

   /*When node name is 0, then we use the default names*/
   nd->nodeNameLen = 0;
   nd->nodename = NULL;
   nd->dskLen = 0;
   nd->dsk = NULL;
   nd->pcvs = NULL;
   nd->state = STATUS_CREATED;
   nd->mode = MODE_PROBING;
   nd->security_flags = 0;
   nd->wakeUp_interval = DEFAULT_WAKE_UP_INTERVAL ; //Default wakeup interval is 70 minutes
   nd->node_cc_versions_len = controlled_cc_v_size();
   nd->node_cc_versions = rd_data_mem_alloc(nd->node_cc_versions_len);
   //   rd_node_cc_versions_set_default(nd);
   nd->node_version_cap_and_zwave_sw = 0x00;
   nd->probe_flags = RD_NODE_PROBE_NEVER_STARTED;
   nd->node_is_zws_probed = 0x00;
   nd->node_properties_flags = 0x0000;
   return;
}

/* Main function to execute the eeprom restoration. The idea is to construct
 * a rd_node_database_entry_t object and write the eeprom via RD_DataStore API.
 */
static int restore_eeprom(void) {
  uint8_t epid = 0, reprobe_cnt = 0, doneprobe_cnt = 0, gw_cnt = 0;
  int ii;
  const zgw_node_data_t *zgw_node_data = NULL;
  const zgw_node_ep_data_t *zgw_node_ep_data = NULL;
  rd_node_database_entry_t *rd_node;
  rd_ep_database_entry_t *rd_ep;

  /* Create a fake Resource Directory from the global backup data. */
  /* It is necessary to look up every possible legal nodeid as the
   * nodeids may not be consecutive in a network. */
  for (ii = 0;  ii <= ZW_MAX_NODES ; ii++)  {
    zgw_node_data = zgw_node_data_get(ii);
    if (NULL == zgw_node_data) {
       continue;
    }
    rd_node = rd_data_mem_alloc(sizeof(rd_node_database_entry_t));
    if (rd_node == NULL) {
      printf("zgw_restore_eeprom: rd_node malloc failed\n");
      return -1;
    }
    rd_node_dbe_init(rd_node);
    rd_node->wakeUp_interval = zgw_node_data->node_gwdata.liveness.wakeUp_interval;
    rd_node->lastAwake = zgw_node_data->node_gwdata.liveness.lastAwake;
    rd_node->lastUpdate = zgw_node_data->node_gwdata.liveness.lastUpdate;
    rd_node->nodeid = zgw_node_data->node_gwdata.zw_node_data.node_id;
    rd_node->security_flags = zgw_node_data->node_gwdata.security_flags;

    /* Derive mode based on node capability, which is the same logic as we have
     * in update_protocol_info() and probing state machine */
    /* FIXME Change to have the same logic to fill MODE_MAILBOX as we have in
     * GW, i.e. node supports CC_WAKE_UP & MAILBOX is enabled. Now we just
     * assume every non-listening node is mailbox node. */

    /* Defulte value: 0, Allowed value: 1, 2, 3, 4
     * 0 indicates no mode field found in JSON
     */
    if (zgw_node_data->node_gwdata.mode != 0) {
      rd_node->mode = zgw_node_data->node_gwdata.mode;
    } else {
      if (zgw_node_data->node_gwdata.zw_node_data.capability & NODEINFO_LISTENING_SUPPORT) {
        rd_node->mode = MODE_ALWAYSLISTENING;
      } else if (zgw_node_data->node_gwdata.zw_node_data.security
          &(NODEINFO_ZWAVE_SENSOR_MODE_WAKEUP_1000
            | NODEINFO_ZWAVE_SENSOR_MODE_WAKEUP_250)) {
        rd_node->mode = MODE_FREQUENTLYLISTENING;
      } else {
        rd_node->mode = MODE_MAILBOX;
      }
    }
    /* The mode field also carries the DELETED flag, but that is not currently persisted. */
    if (zgw_node_data->node_gwdata.liveness.liveness_state == ZGW_NODE_FAILING) {
       rd_node->state = STATUS_FAILING;
    } else {
       rd_node->state = (rd_node_state_t) zgw_node_data->node_gwdata.probe_state.interview_state;
    }

    rd_node->manufacturerID = zgw_node_data->node_gwdata.node_prod_id.manufacturerID;
    rd_node->productType = zgw_node_data->node_gwdata.node_prod_id.productType;
    rd_node->productID = zgw_node_data->node_gwdata.node_prod_id.productID;

    /* Node type stored in the RD node is basic type */
    rd_node->nodeType = zgw_node_data->node_gwdata.zw_node_data.node_type.basic;
    rd_node->refCnt = 0;
    rd_node->nEndpoints = zgw_node_data->node_gwdata.nEndpoints;
    rd_node->nAggEndpoints = zgw_node_data->node_gwdata.nAggEndpoints;


    LIST_STRUCT_INIT(rd_node, endpoints);
    for (epid = 0; epid < rd_node->nEndpoints; epid++) {
      zgw_node_ep_data = zgw_node_ep_data_get(ii, epid);
      if (zgw_node_ep_data == NULL) {
        printf("zgw_restore_eeprom: node %u has %u endpoints, but endpoint %u data is missing\n",
               ii, rd_node->nEndpoints, epid);
        return -1;
      }
      rd_ep = rd_data_mem_alloc(sizeof(rd_ep_database_entry_t));
      if (!rd_ep) {
        printf("zgw_restore_eeprom: rd_ep malloc failed\n");
        return -1;
      }
      memset(rd_ep, 0, sizeof(rd_ep_database_entry_t));
      rd_ep->endpoint_id = zgw_node_ep_data->endpoint_id;
      rd_ep->installer_iconID = zgw_node_ep_data->installer_iconID;
      rd_ep->user_iconID = zgw_node_ep_data->user_iconID;
      rd_ep->state = zgw_node_ep_data->state;
      /* endpoint location */
      if ((zgw_node_ep_data->ep_mDNS_data.endpoint_location != NULL)
          && (zgw_node_ep_data->ep_mDNS_data.endpoint_loc_len !=0)) {
        if (is_valid_mdns_location(zgw_node_ep_data->ep_mDNS_data.endpoint_location
              , zgw_node_ep_data->ep_mDNS_data.endpoint_loc_len) == true) {
          rd_ep->endpoint_loc_len = zgw_node_ep_data->ep_mDNS_data.endpoint_loc_len;
          rd_ep->endpoint_location = rd_data_mem_alloc(rd_ep->endpoint_loc_len);
          memcpy(rd_ep->endpoint_location, zgw_node_ep_data->ep_mDNS_data.endpoint_location
              , zgw_node_ep_data->ep_mDNS_data.endpoint_loc_len);
        } else {
          printf("mDNS location for node %u endpoint %u not imported - invalid format\n", ii, epid);
        }
      }
      /* endpoint name */
      if ((zgw_node_ep_data->ep_mDNS_data.endpoint_name != NULL)
          && (zgw_node_ep_data->ep_mDNS_data.endpoint_name_len != 0)) {
        if (is_valid_mdns_name(zgw_node_ep_data->ep_mDNS_data.endpoint_name
              , zgw_node_ep_data->ep_mDNS_data.endpoint_name_len) == true) {
          rd_ep->endpoint_name_len = zgw_node_ep_data->ep_mDNS_data.endpoint_name_len;
          rd_ep->endpoint_name = rd_data_mem_alloc(rd_ep->endpoint_name_len);
          memcpy(rd_ep->endpoint_name, zgw_node_ep_data->ep_mDNS_data.endpoint_name
              , zgw_node_ep_data->ep_mDNS_data.endpoint_name_len);
        } else {
          printf("mDNS name for node %u endpoint %u not imported - invalid format\n", ii, epid);
        }
      }
      /* endpoint info */
      rd_ep->endpoint_info_len = zgw_node_ep_data->endpoint_info_len;
      if (zgw_node_ep_data->endpoint_info != NULL) {
        rd_ep->endpoint_info = rd_data_mem_alloc(rd_ep->endpoint_info_len);
        memcpy(rd_ep->endpoint_info, zgw_node_ep_data->endpoint_info
               , zgw_node_ep_data->endpoint_info_len);
      }
      /* endpoint aggregation */
      rd_ep->endpoint_aggr_len = zgw_node_ep_data->endpoint_aggr_len;
      if (zgw_node_ep_data->endpoint_agg != NULL) {
        rd_ep->endpoint_agg = rd_data_mem_alloc(rd_ep->endpoint_aggr_len);
        memcpy(rd_ep->endpoint_agg, zgw_node_ep_data->endpoint_agg
               , zgw_node_ep_data->endpoint_aggr_len);
      }
      list_add(rd_node->endpoints, rd_ep);
    }
    /* Node name */
    if ((zgw_node_data->ip_data.mDNS_node_name != NULL)
        && (zgw_node_data->ip_data.mDNS_node_name_len !=0)) {
      if (is_valid_mdns_name(zgw_node_data->ip_data.mDNS_node_name
            , zgw_node_data->ip_data.mDNS_node_name_len) == true) {
        rd_node->nodeNameLen = zgw_node_data->ip_data.mDNS_node_name_len;
        rd_node->nodename = rd_data_mem_alloc(rd_node->nodeNameLen);
        memcpy(rd_node->nodename, zgw_node_data->ip_data.mDNS_node_name
            , zgw_node_data->ip_data.mDNS_node_name_len);
      } else {
        printf("mDNS name for node %u not imported - invalid format\n", ii);
      }
    }
    /* DSK */
    if (zgw_node_data->uid_type == node_uid_dsk) {
      rd_node->dskLen = zgw_node_data->node_uid.dsk_uid.dsk_len;
      rd_node->dsk = rd_data_mem_alloc(rd_node->dskLen);
      memcpy(rd_node->dsk, zgw_node_data->node_uid.dsk_uid.dsk, rd_node->dskLen);
    } else {
      rd_node->dskLen = 0;
      rd_node->dsk = NULL;
    }
    /* Version V3 info */
    rd_node->node_version_cap_and_zwave_sw = zgw_node_data->node_gwdata.probe_state.node_version_cap_and_zwave_sw;
    rd_node->probe_flags = zgw_node_data->node_gwdata.probe_state.probe_flags;
    rd_node->node_properties_flags = zgw_node_data->node_gwdata.probe_state.node_properties_flags;
    rd_node->node_cc_versions_len = zgw_node_data->node_gwdata.probe_state.node_cc_versions_len;

    if (zgw_node_data->node_gwdata.probe_state.node_cc_versions != NULL) {
      rd_node->node_cc_versions = rd_data_mem_alloc(rd_node->node_cc_versions_len);
      memcpy(rd_node->node_cc_versions, zgw_node_data->node_gwdata.probe_state.node_cc_versions
             , zgw_node_data->node_gwdata.probe_state.node_cc_versions_len);
    }
    rd_node->node_is_zws_probed = zgw_node_data->node_gwdata.probe_state.node_is_zws_probed;
    rd_data_store_nvm_write(rd_node);
    if (rd_node->nodeid == MyNodeID) {
      printf("Gateway node ID %u has been imported.\n", rd_node->nodeid);
      gw_cnt++;
    } else if (rd_node->state == STATUS_CREATED) {
      printf("Node ID %u is partiall imported and re-interview is needed.\n", rd_node->nodeid);
      reprobe_cnt++;
    } else {
      printf("Node ID %u is fully imported.\n", rd_node->nodeid);
      doneprobe_cnt++;
    }
    rd_data_store_mem_free(rd_node);
  }
  printf("Summary: %u node(s) and %u gateway(s) has been imported, among which, %u node(s) will be re-probed when Z/IP Gateway starts up.\n", reprobe_cnt + doneprobe_cnt + gw_cnt, gw_cnt, reprobe_cnt);

  return 0;
}

/* Clean up after the eeprom restoration */
static int restore_eeprom_destory(void) {
  if (eeprom_fullpath != NULL)
    free(eeprom_fullpath);
  return 0;
}

int zgw_restore_eeprom_file(void) {
  int res = 0;
  uint16_t len = 0;

  res = restore_eeprom_init();
  if (res) {
    printf("Cannot init eeprom.dat file\n");
    return res;
  }

  res = restore_eeprom();
  if (res) {
    printf("Cannot restore eeprom.dat file\n");
    return res;
  }

  const zgw_temporary_association_data_t *temp_assoc_data
                                  = zgw_temporary_association_data_get();
  rd_datastore_persist_virtual_nodes(temp_assoc_data->virtual_nodes, temp_assoc_data->virtual_nodes_count);

  res = restore_eeprom_destory();
  if (res) {
    printf("Cannot teardown eeprom.dat buffer\n");
  }
  return 0;
}
