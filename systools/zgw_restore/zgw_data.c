/* © 2019 Silicon Laboratories Inc. */

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>

#include "uiplib.h"
#include "lib/list.h"

#include "zgw_data.h"

#include "NodeCache.h"
#include "libs2/include/s2_keystore.h"
#include "ZW_transport_api.h"
#include "ZW_classcmd.h"
#include "zgw_nodemask.h"
#include "RD_internal.h" /* For  DEFAULT_WAKE_UP_INTERVAL (70 * 60) */

uint32_t homeID = 0;
uint8_t MyNodeID = 0;

zip_gateway_backup_data_t backup_rep;

/*
 * logging helpers
 */
char slush_buf[256];

char* zgw_node_uid_to_str(zgw_node_data_t *node) {
   snprintf(slush_buf, 128, "0"); /* Not a valid node id */
   if (node->uid_type == node_uid_dsk) {
      uint8_t ii = 0;
      while (ii < node->node_uid.dsk_uid.dsk_len) {
         sprintf(&(slush_buf[ii*2]), "%x", node->node_uid.dsk_uid.dsk[ii]);
         ii++;
      }
   } else if (node->uid_type == node_uid_zw_net_id) {
      /* homeID in the UID is stored in network order.  Print it like
       * the gateway does. */
      sprintf(slush_buf, "0x%04x:%u",
              UIP_HTONL(node->node_uid.net_uid.homeID),
              node->node_uid.net_uid.node_id);
   } else {
      sprintf(slush_buf, "Unsupported node id type");
   }
   return slush_buf;
}

void zgwr_ipaddr6_print(const uip_ipaddr_t *addr) {
  uint16_t a, i;
  int f;
  for(i = 0, f = 0; i < sizeof(uip_ipaddr_t); i += 2) {
    a = (addr->u8[i] << 8) + addr->u8[i + 1];
    if(a == 0 && f >= 0) {
      if(f++ == 0) {
        printf(":");
        printf(":");
      }
    } else {
      if(f > 0) {
        f = -1;
      } else if(i > 0) {
        printf(":");
      }
      printf("%02x",a);
    }
  }
  printf("\n");
}

/*
 * ZGW semantic conversions helpers.
 *
 * Conversions between different formats that require knowledge of ZGW representations.
 *
 * May be split into a new .c file if there are a lot of them.
 */

uint16_t node_id_set_from_intval(int32_t the_number,
                                uint16_t *slot) {
   if (nodemask_nodeid_is_valid(the_number)) {
      printf("  Setting node id %d\n", the_number);
      *slot = (uint16_t) the_number;
   } else {
      printf("Illegal Z-Wave node id: %d\n", the_number);
      return 0;
   }
   return *slot;
}

uint8_t node_flags2keystore_flags(uint8_t gw_flags) {
  uint8_t f = 0;

  if(gw_flags & NODE_FLAG_SECURITY0) {
    f |= KEY_CLASS_S0;
  }
  if(gw_flags & NODE_FLAG_SECURITY2_ACCESS) {
    f |= KEY_CLASS_S2_ACCESS;
  }
  if(gw_flags & NODE_FLAG_SECURITY2_AUTHENTICATED) {
    f |= KEY_CLASS_S2_AUTHENTICATED;
  }
  if(gw_flags & NODE_FLAG_SECURITY2_UNAUTHENTICATED) {
    f |= KEY_CLASS_S2_UNAUTHENTICATED;
  }
  return f;
}

uint8_t keystore_flags2node_flags(uint8_t key_store_flags) {
  uint8_t flags = 0;
  if (key_store_flags & KEY_CLASS_S0) {
    flags |= NODE_FLAG_SECURITY0;
  }
  if (key_store_flags & KEY_CLASS_S2_ACCESS) {
    flags |= NODE_FLAG_SECURITY2_ACCESS;
  }
  if (key_store_flags & KEY_CLASS_S2_AUTHENTICATED) {
    flags |= NODE_FLAG_SECURITY2_AUTHENTICATED;
  }
  if (key_store_flags & KEY_CLASS_S2_UNAUTHENTICATED) {
    flags |= NODE_FLAG_SECURITY2_UNAUTHENTICATED;
  }
  return flags;
}

/* Return 0 if CC is not there, aka not controlled by GW, node_version
 * if it is set or unchanged.
 * If it is already set, print a warning, but ignore the new setting. 
 */
uint8_t cc_version_set(zgw_node_probe_state_t *probe_state,
                       uint16_t cc, uint8_t node_version) {
   int cnt = probe_state->node_cc_versions_len / sizeof(cc_version_pair_t);

   for (int ii = 0; ii < cnt ; ii++) {
      if (probe_state->node_cc_versions[ii].command_class == cc) {
         if (probe_state->node_cc_versions[ii].version == 0) {
            /* not set yet, so just set it */
            probe_state->node_cc_versions[ii].version = node_version;
         } else {
            printf("\nDouble setting of CC 0x%x version %u and %u.\n",
                   cc, probe_state->node_cc_versions[ii].version,
                   node_version);
            return node_version;
         }
         return node_version;
      }
   }
   return 0;
}

/* Convert string to contiki ip address. */
bool ipv6addr_set_from_string(const char* addr, uip_ip6addr_t *slot) {
   int res = uiplib_ipaddrconv(addr, slot);
   if (res == 0) {
      printf("Invalid ip address: %s\n", addr);
      return false;
   }
   printf("  Setting ip address ");
   zgwr_ipaddr6_print(slot);
   return true;
}



/*
 * Add stuff to internal representation of backup data.
 */

/*  Initialize backup data.
 */
void zgw_data_reset(void) {
   bzero(&backup_rep, sizeof(backup_rep));
}

zip_gateway_backup_data_t * zip_gateway_backup_data_unsafe_get(void) {
   return &backup_rep;
}


/* Controller functions. */

/* Add controller data to a backup structure. */
zw_controller_t * zw_controller_add(uint8_t SUCid,
                                    uint8_t *cc_list) {
   backup_rep.controller.SUCid = SUCid;
   /* clear the node list */
   bzero(&(backup_rep.controller.included_nodes),
         sizeof(zw_node_data_t*) * ZW_MAX_NODES);
   /* We assume the CC list is handled by the gateway when it starts
      up, so this is not really needed here. */
   backup_rep.controller.cc_list = cc_list;

   return &(backup_rep.controller);
}


bool zw_node_add(zw_controller_t *zw_controller, nodeid_t node_id,
                 uint8_t capability, uint8_t security,
                 const zw_node_type_t *node_type ) {
   zw_node_data_t *node_data = zw_controller->included_nodes[node_id-1];

   if (node_data == NULL) {
      node_data = malloc(sizeof(zw_node_data_t));
      if (!node_data) {
         return false;
      }
      bzero(node_data, sizeof(zw_node_data_t));
   } else {
      printf("Overwriting node ZW data for node %d\n", node_id);
      bzero(node_data, sizeof(zw_node_data_t));
   }

   node_data->node_id = node_id;
   node_data->neighbors = NULL; /* ZGW doesn't care */
   node_data->capability = capability;
   node_data->security = security;
   node_data->node_type = *node_type;
   zw_controller->included_nodes[node_id-1] = node_data;
   return true;
}


/* Gateway functions. */

/* node_data must be a valid pointer.  The functions keeps the pointer. */
bool zgw_node_data_pointer_add(nodeid_t node_id, zgw_node_data_t *node_data) {
   if (node_id <= ZW_MAX_NODES) {
      if (zgw_node_data_get(node_id) == NULL) {
         backup_rep.zgw.zw_nodes[node_id] = node_data;
         return true;
      } else {
         printf("Node %u repeated in file.\n", node_id);
         free(node_data);
         return false;
      }
   } else {
      printf("Provisions not supported\n");
      free(node_data);
      return false;
   }
}

void zgw_node_ep_data_set_default(zgw_node_ep_data_t *ep) {
   ep->ep_mDNS_data.endpoint_location = NULL; /* mDNS field.  Generated by ZGW if NULL. */
   ep->ep_mDNS_data.endpoint_name= NULL; /* mDNS field. Generated by ZGW if NULL. */
   ep->ep_mDNS_data.endpoint_loc_len = 0;  /* Length of #endpoint_location. */
   ep->ep_mDNS_data.endpoint_name_len = 0; /* Length of #endpoint_name. */

   ep->endpoint_info_len = 0;/* Length of #endpoint_info. */
   ep->endpoint_aggr_len = 0; /* Length of aggregations */
   ep->endpoint_id = 0; /* Endpoint identifier. */
   ep->state = EP_STATE_PROBE_INFO; /* Endpoint probing state. */
   /* No meaningful default, initialize to "missing".  */
   ep->installer_iconID = ICON_TYPE_UNASSIGNED; /* Z-Wave plus icon ID. */
   ep->user_iconID = ICON_TYPE_UNASSIGNED; /* Z-Wave plus icon ID. */

   /** Command classes supported by endpoint, as determined at last
       probing.  Used in \ref rd_ep_class_support() to determine if a
       node/ep supports a given CC.  No meaningful default. */
   ep->endpoint_info = NULL;
   /** Aggregation info. Only non-null in aggregated endpoints. */
   ep->endpoint_agg = NULL;

   return;
}

zgw_node_ep_data_t* zgw_node_endpoint_init(void) {
   zgw_node_ep_data_t *new_ep = malloc(sizeof(zgw_node_ep_data_t));
   if (new_ep == NULL) {
      return NULL;
   }
   bzero(new_ep, sizeof(zgw_node_ep_data_t));
   /* Fill in default values */
   zgw_node_ep_data_set_default(new_ep);
   return new_ep;
}

void zgw_node_endpoint_free(zgw_node_ep_data_t *ep) {
   if (ep->ep_mDNS_data.endpoint_name) {
      free(ep->ep_mDNS_data.endpoint_name);
   }
   if (ep->ep_mDNS_data.endpoint_location) {
      free(ep->ep_mDNS_data.endpoint_location);
   }
   free(ep);
}

void zgw_node_zw_data_set_default(zgw_node_zw_data_t *gw_data) {
   gw_data->liveness.liveness_state = ZGW_NODE_OK; /* aka STATUS_DONE */
   gw_data->liveness.wakeUp_interval = DEFAULT_WAKE_UP_INTERVAL;
   /* Will be reset by ZGW when the process starts up. */
   gw_data->liveness.lastAwake = 0;
   /* Timestamps from the json file can only be used if we can
    * correlate the clock of the original host with the clock of the
    * destination host. */
   //   gw_data->liveness.lastUpdate = clock_seconds();
   gw_data->liveness.lastUpdate = 0;

   /* No meaningful defaults */
   gw_data->zw_node_data.node_id = 0;

   gw_data->probe_state.state = STATUS_CREATED;
   /* Use node_interview_unknown in next release */
   gw_data->probe_state.interview_state = node_interview;

   /* RD_NODE_PROBE_NEVER_STARTED (aka 0x00), 
      RD_NODE_FLAG_PROBE_STARTED,
      RD_NODE_FLAG_PROBE_FAILED,
      RD_NODE_FLAG_PROBE_HAS_COMPLETED*/
   /* Note that only some of these are flags. */
   /* Should be RD_NODE_FLAG_PROBE_STARTED when we have cc_versions,
      RD_NODE_FLAG_PROBE_HAS_COMPLETED if all the Version V3 probing
      is completed. */
   gw_data->probe_state.probe_flags = RD_NODE_PROBE_NEVER_STARTED;

   /* Node properties and capabilities. */
   gw_data->probe_state.node_properties_flags = 0;

   gw_data->probe_state.node_is_zws_probed = 0;
   gw_data->probe_state.node_version_cap_and_zwave_sw = 0;

   /* CC versions cache for this node */
   gw_data->probe_state.node_cc_versions_len = controlled_cc_v_size();
   gw_data->probe_state.node_cc_versions = malloc(controlled_cc_v_size());
   if (gw_data->probe_state.node_cc_versions == NULL) {
      gw_data->probe_state.node_cc_versions_len = 0;
      printf("Failed to allocate memory for command class versions\n");
   } else {
      gw_data->probe_state.node_cc_versions_len =
         rd_mem_cc_versions_set_default(gw_data->probe_state.node_cc_versions_len,
                                        gw_data->probe_state.node_cc_versions);
      printf("  Allocated %u bytes for command class versions.\n",
             controlled_cc_v_size());
   }
   if (gw_data->probe_state.node_cc_versions_len == 0) {
      printf("Node CC version set default failed.\n");
   } else {
      int cnt = gw_data->probe_state.node_cc_versions_len / sizeof(cc_version_pair_t);
      printf("  Initialized version settings for %d CCs controlled by the gateway.\n",
             cnt);
      for (int ii = 0; ii < cnt - 1 ; ii++) {
         printf("  Version of CC 0x%x (%d) set to %d.\n",
                gw_data->probe_state.node_cc_versions[ii].command_class,
                gw_data->probe_state.node_cc_versions[ii].command_class,
                gw_data->probe_state.node_cc_versions[ii].version);
      }
   }

   /* No meaningful default */
   //gw_data->node_prod_id;

   gw_data->security_flags = 0;
   gw_data->nEndpoints = 0;
   gw_data->nAggEndpoints = 0;
}

zgw_node_data_t *zgw_node_data_init(void) {
   zgw_node_data_t *new_node = malloc(sizeof(zgw_node_data_t));
   if (new_node == NULL) {
      return NULL;
   }
   bzero(new_node, sizeof(zgw_node_data_t));

   zgw_node_zw_data_t *node_gwdata = &(new_node->node_gwdata);
   LIST_STRUCT_INIT(node_gwdata, endpoints);

   /* Fill in default values */
   zgw_node_zw_data_set_default(node_gwdata);
   return new_node;
}

void zgw_data_set_default(zgw_data_t *zgw_data) {
   bzero(zgw_data, sizeof(zgw_data_t));

   /* ZipLanIp6PrefixLength, 64 */;
   /* This is not actually configurable. */
   zgw_data->zip_ipv6_prefix_len = 64;
}

/** Note that provisioned_nodes are currently not supported */
int zgw_data_init(zip_gateway_backup_manifest_t *manifest,
                  zw_controller_t *controller,
                  zgw_data_t *zgw,
                  zgw_node_pvs_t *provisioned_nodes) {

   if (manifest == NULL) {
      bzero(&(backup_rep.manifest), sizeof(zip_gateway_backup_manifest_t));
   } else {
      backup_rep.manifest = *manifest;
   }
   /* If there was a controller, and we don't get a new one, flush the contents */
   if (controller == NULL) {
      bzero(&(backup_rep.controller), sizeof(zw_controller_t));
   } else {
      backup_rep.controller = *controller;
   }
   if (zgw == NULL) {
      bzero(&(backup_rep.zgw), sizeof(zgw_data_t));
   } else {
      backup_rep.zgw = *zgw;
   }

   return 0;
}

/*
 * Get stuff from internal representation.
 */

const zgw_data_t *zgw_data_get(void) {
   return &(backup_rep.zgw);
}

const zgw_node_data_t *zgw_node_data_get(nodeid_t nodeid) {
   return (backup_rep.zgw.zw_nodes[nodeid]);
}

const zgw_node_pvs_t * zgw_node_pvs_get(zgw_dsk_uid_t node_uid) {
   return NULL;
}

const zgw_node_ep_data_t * zgw_node_ep_data_get(nodeid_t nodeid, uint8_t epid) {
  zgw_node_ep_data_t *ep = list_head(backup_rep.zgw.zw_nodes[nodeid]->node_gwdata.endpoints);
  int i = 0;
  while (ep != NULL) {
    if (i == epid)
      break;
    ep = list_item_next(ep);
    i++;
  }
  /* Meaning this is the requested endpoints so return ep */
  if (i == epid)
    return (ep);
  /* Otherwise, return NULL */
  return NULL;
}

const zw_controller_t * zgw_controller_zw_get(void) {
   return &(backup_rep.controller);
}

const zip_lan_ip_data_t * zip_lan_ip_data_get(void) {
   return &(backup_rep.zgw.zip_lan_data);
}

const zgw_temporary_association_data_t * zgw_temporary_association_data_get(void) {
  return &(backup_rep.zgw.zgw_temporary_association_data);
}

const zip_gateway_backup_data_t * zip_gateway_backup_data_get(void) {
   return &backup_rep;
}

/* The sanitizer: */
/*   - VALIDATION TODO: consider whether we should overwrite or compare and give */
/*     error on mismatch in zw_node_add() if node already exists. */
/*   - TODO VALIDATE that the node is not secure if it does not have secure NIF on */
/*     any endpoint. */
/*   - TODO: post-validate if a node with no wake-up interval is a wake */
/*     up node? (sanitizer) */
/*   - Check for missing required fields (can partly be done by schema, */
/*     but some dependencies may be tricky). */
/*   - check that all fields of productId are present */
/*   - TODO: Do we want to downgrade to re-interview if CCVersions are missing? */
int zip_gateway_backup_data_sanitize(void) {
  /* Checking if virtual node ID conflicts with node ID */
  const zgw_temporary_association_data_t *temp_assoc_data
                           = zgw_temporary_association_data_get();
  for (int i = 0; i < temp_assoc_data->virtual_nodes_count; i++) {
    if (zgw_node_data_get(temp_assoc_data->virtual_nodes[i])) {
      printf("Error: node ID %u exists in both virtual node list and node list\n",
             temp_assoc_data->virtual_nodes[i]);
      return 1;
    }
  }
  return 0;
}
