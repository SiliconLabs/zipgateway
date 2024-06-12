/* © 2019 Silicon Laboratories Inc.  */

#include "ResourceDirectory.h"
#include "ZW_classcmd_ex.h"
#include "RD_internal.h"
#include "RD_DataStore.h"
#include "TYPES.H"
#include "ZW_transport_api.h"
#include "ZIP_Router_logging.h"
#include "provisioning_list.h"
#include "zgw_str.h"
#include "zwdb.h"

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "zw_network_info.h" /* MyNodeID */

/* TODO-ath: Consider to move this to a GW component, ref: ZGW-1243 */
const cc_version_pair_t controlled_cc_v[] = {{COMMAND_CLASS_VERSION, 0x0},
                                             {COMMAND_CLASS_ZWAVEPLUS_INFO, 0x0},
                                             {COMMAND_CLASS_MANUFACTURER_SPECIFIC, 0x0},
                                             {COMMAND_CLASS_WAKE_UP, 0x0},
                                             {COMMAND_CLASS_MULTI_CHANNEL_V4, 0x0},
                                             {COMMAND_CLASS_ASSOCIATION, 0x0},
                                             {COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, 0x0},
                                             {0xffff, 0xff}};
/* This eats memory ! */
/**
 * The node database.
 */
static rd_node_database_entry_t* ndb[ZW_MAX_NODES];

uint8_t controlled_cc_v_size()
{
  return sizeof(controlled_cc_v);
}

int rd_mem_cc_versions_set_default(uint8_t node_cc_versions_len,
                                   cc_version_pair_t *node_cc_versions)
{
  if (node_cc_versions_len < sizeof(controlled_cc_v)) {
     return 0;
  }
  if (node_cc_versions) {
    memcpy(node_cc_versions, controlled_cc_v, sizeof(controlled_cc_v));
  }
  return sizeof(controlled_cc_v);
}

void rd_node_cc_versions_set_default(rd_node_database_entry_t *n)
{
  if(!n)
    return;
  if(n->node_cc_versions) {
    memcpy(n->node_cc_versions, controlled_cc_v, n->node_cc_versions_len);
  } else {
    WRN_PRINTF("Node CC version set default failed.\n");
  }
}

uint8_t rd_node_cc_version_get(rd_node_database_entry_t *n, uint16_t command_class)
{
  int i, cnt;
  uint8_t version;

  if(!n) {
    return 0;
  }

  if(!n->node_cc_versions) {
    return 0;
  }

  if(n->mode & MODE_FLAGS_DELETED) {
    return 0;
  }

  version = 0;
  cnt = n->node_cc_versions_len / sizeof(cc_version_pair_t);

  for(i = 0; i < cnt ; i++) {
    if(n->node_cc_versions[i].command_class == command_class) {
      version = n->node_cc_versions[i].version;
      break;
    }
  }
  return version;
}

void rd_node_cc_version_set(rd_node_database_entry_t *n, uint16_t command_class, uint8_t version)
{
  int i, cnt;

  if(!n) {
    return;
  }

  if(!n->node_cc_versions) {
    return;
  }

  if(n->mode & MODE_FLAGS_DELETED) {
    return;
  }
  cnt = n->node_cc_versions_len / sizeof(cc_version_pair_t);

  for(i = 0; i < cnt; i++) {
    if(n->node_cc_versions[i].command_class == command_class) {
      n->node_cc_versions[i].version = version;
      break;
    }
  }
}

rd_node_database_entry_t* rd_node_entry_alloc(nodeid_t nodeid)
{
   rd_node_database_entry_t* nd = rd_data_mem_alloc(sizeof(rd_node_database_entry_t));
   ndb[nodeid - 1] = nd;
   if (nd != NULL) {
      memset(nd, 0, sizeof(rd_node_database_entry_t));
      nd->nodeid = nodeid;

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
      rd_node_cc_versions_set_default(nd);
      nd->node_version_cap_and_zwave_sw = 0x00;
      nd->probe_flags = RD_NODE_PROBE_NEVER_STARTED;
      nd->node_is_zws_probed = 0x00;
      nd->node_properties_flags = 0x0000;

      LIST_STRUCT_INIT(nd, endpoints);
   }

   return nd;
}

rd_node_database_entry_t* rd_node_entry_import(nodeid_t nodeid)
{
   ndb[nodeid-1] = rd_data_store_read(nodeid);
   return ndb[nodeid-1];
}

void rd_node_entry_free(nodeid_t nodeid)
{
   rd_node_database_entry_t* nd = ndb[nodeid - 1];
   rd_data_store_nvm_free(nd);
   rd_data_store_mem_free(nd);
   ndb[nodeid - 1] = NULL;
}

rd_node_database_entry_t* rd_node_get_raw(nodeid_t nodeid)
{
   return ndb[nodeid - 1];
}


void
rd_destroy()
{
  nodeid_t i;
  rd_node_database_entry_t *n;
  for (i = 0; i < ZW_MAX_NODES; i++)
  {
    n = ndb[i];
    if (n)
    {
      rd_data_store_mem_free(n);
      ndb[i] = 0;
    }
  }
}

u8_t rd_node_exists(nodeid_t node)
{
  if (node > 0 && node <= ZW_MAX_NODES)
  {
    return (ndb[node - 1] != 0);
  }
  return FALSE;
}

rd_ep_database_entry_t* rd_ep_first(nodeid_t node)
{
  nodeid_t i;

  /* special rule for node 0 to be valid for searching all nodes */
  if ((node > ZW_MAX_NODES)
      || ((node > ZW_CLASSIC_MAX_NODES) && (node < ZW_LR_MIN_NODE_ID))) {
    return 0;
  }

  if (node == 0)
  {
    for (i = 0; i < ZW_MAX_NODES; i++)
    {
      if (ndb[i])
      {
        return list_head(ndb[i]->endpoints);
      }
    }
  }
  else if (ndb[node - 1])
  {
    return list_head(ndb[node - 1]->endpoints);
  }
  return 0;
}

rd_ep_database_entry_t* rd_ep_next(nodeid_t node, rd_ep_database_entry_t* ep)
{
  nodeid_t i;
  rd_ep_database_entry_t* next = list_item_next(ep);

  if (next == 0 && node == 0)
  {
    for (i = ep->node->nodeid; i < ZW_MAX_NODES; i++)
    {
      if (ndb[i])
      {
        return list_head(ndb[i]->endpoints);
      }
    }
  }
  return next;
}

rd_node_mode_t rd_node_mode_value_get(nodeid_t n) {
   rd_node_database_entry_t *node = rd_node_get_raw(n);
   if (node) {
      return RD_NODE_MODE_VALUE_GET(node);
   } else {
      return MODE_NODE_UNDEF;
   }
}

u8_t
rd_get_node_name(rd_node_database_entry_t* n, char* buf, u8_t size)
{
  if (n->nodeNameLen)
  {
    if (size > n->nodeNameLen)
    {
      size = n->nodeNameLen;
    }
    memcpy(buf, n->nodename, size);
    return size;
  }
  else
  {
    return snprintf(buf, size, "zw%08X%04X", UIP_HTONL(homeID), n->nodeid);
  }
}

rd_node_database_entry_t* rd_lookup_by_node_name(const char* name)
{
  nodeid_t i;
  uint8_t j;
  char buf[64];
  for (i = 0; i < ZW_MAX_NODES; i++)
  {
    if (ndb[i])
    {
      j = rd_get_node_name(ndb[i], buf, sizeof(buf));
      if (strncasecmp(buf,name, j) == 0)
      {
        return ndb[i];
      }
    }
  }
  return 0;
}

u8_t rd_get_ep_name(rd_ep_database_entry_t* ep, char* buf, u8_t size) {
  /* If there is a real name, use that. */
  if (ep->endpoint_name && ep->endpoint_name_len) {
    if (size > ep->endpoint_name_len)
    {
      size = ep->endpoint_name_len;
    }
    memcpy(buf, ep->endpoint_name, size);
    return size;
  } else {
    /* If there is no name, but there is info, generate a name from the info. */
    if (ep->endpoint_info && (ep->endpoint_info_len > 2)) {
      const char* type_str = get_gen_type_string(ep->endpoint_info[0]);
      return snprintf(buf, size, "%s [%04x%04x%02x]",
                      type_str, UIP_HTONL(homeID), ep->node->nodeid,
                      ep->endpoint_id);
    } else {
      /* If there is no name and no info, generate a generic name from
       * the homeid, node id, and ep id. */
      return snprintf(buf, size, "(unknown) [%04x%04x%02x]", UIP_HTONL(homeID),
                      ep->node->nodeid, ep->endpoint_id);
    }
  }
}

u8_t rd_get_ep_location(rd_ep_database_entry_t* ep, char* buf, u8_t size) {
  if (size > ep->endpoint_loc_len) {
    size = ep->endpoint_loc_len;
  }

  memcpy(buf, ep->endpoint_location, size);
  return size;
}

void rd_node_add_dsk(nodeid_t node, uint8_t dsklen, const uint8_t *dsk)
{
   rd_node_database_entry_t *nd;

   if ((dsklen == 0) || (dsk == NULL)) {
      return;
   }

   nd = rd_node_get_raw(node);
   if (nd) {
      rd_node_database_entry_t* old_nd = rd_lookup_by_dsk(dsklen, dsk);
      if (old_nd) {
         /* Unlikely, but possible: the same device gets added again. */
         DBG_PRINTF("New node id %d replaces existing node id %d for device with this dsk.\n",
                    node, old_nd->nodeid);
         rd_data_mem_free(old_nd->dsk);
         old_nd->dsk = NULL;
         old_nd->dskLen = 0;
         /* TODO: Should the node id also be set failing here? */
      }
      if (nd->dskLen != 0) {
         /* TODO: this is not supposed to happen - replace DSK is not supported */
         assert(nd->dskLen == 0);
         if (nd->dsk != NULL) {
            WRN_PRINTF("Replacing old dsk\n");
            /* Silently replace, for now */
            rd_data_mem_free(nd->dsk);
            nd->dskLen = 0;
         }
      }

      nd->dsk = rd_data_mem_alloc(dsklen);
      if (nd->dsk) {
         memcpy(nd->dsk, dsk, dsklen);
         nd->dskLen = dsklen;
         DBG_PRINTF("Setting dsk 0x%02x%02x%02x%02x... on node %u.\n",
                    dsk[0],dsk[1],dsk[2],dsk[3],node);

         /* Insert other fields from pvl. */
         struct pvs_tlv *pvs_tlv = NULL;
         uint8_t len = 0;
         rd_ep_database_entry_t *ep0 = list_head(nd->endpoints);

         pvs_tlv = provisioning_list_tlv_dsk_get(dsklen, dsk, PVS_TLV_TYPE_LOCATION);
         if (pvs_tlv && pvs_tlv->length) {
            if (is_valid_mdns_location((char*)pvs_tlv->value, pvs_tlv->length)) {
               ep0->endpoint_location = rd_data_mem_alloc(pvs_tlv->length);
               if (ep0->endpoint_location) {
                  DBG_PRINTF("Adding location on node %u.\n", node);
                  ep0->endpoint_loc_len = pvs_tlv->length;
                  memcpy(ep0->endpoint_location, pvs_tlv->value, ep0->endpoint_loc_len);
               }
            }
            /* else, too bad? */
            if (ep0->endpoint_loc_len == 0) {
               DBG_PRINTF("Provisioned location could not be used.\n");
            }
         }
         pvs_tlv = provisioning_list_tlv_dsk_get(dsklen, dsk, PVS_TLV_TYPE_NAME);
         if (pvs_tlv && pvs_tlv->length) {
            len = pvs_tlv->length;
            if (pvs_tlv->value[pvs_tlv->length-1] == '\0') {
               /* Names must not contain 'termination characters'
                * according the ZIP Naming CC, so we think 0 should be
                * stripped. */
               len--;
            }
            if (is_valid_mdns_name((char*)pvs_tlv->value, len)) {
               ep0->endpoint_name = rd_data_mem_alloc(len);
               if (ep0->endpoint_name) {
                  DBG_PRINTF("Adding name on node %u.\n", node);
                  ep0->endpoint_name_len = len;
                  memcpy(ep0->endpoint_name, pvs_tlv->value,
                         ep0->endpoint_name_len);
               }
            }
            /* else, too bad? */
            if (ep0->endpoint_name_len == 0) {
               DBG_PRINTF("Provisioned name could not be used.\n");
            }
         }
      } else {
         /* TODO: should we return an error here. */
         nd->dskLen = 0;
      }
   }
   /* TODO: should we return an error if no nd?. */
}

rd_node_database_entry_t* rd_lookup_by_dsk(uint8_t dsklen, const uint8_t* dsk)
{
   nodeid_t ii;

   if (dsklen == 0) {
      return NULL;
   }

   for (ii = 0; ii < ZW_MAX_NODES; ii++)
   {
      if (ndb[ii] && (ndb[ii]->dskLen >= dsklen))
      {
         if (memcmp(ndb[ii]->dsk, dsk, dsklen) == 0)
         {
            return ndb[ii];
         }
      }
   }
   return NULL;
}

rd_node_database_entry_t* rd_get_node_dbe(nodeid_t nodeid)
{
  //ASSERT(nodeid>0);
  if (nodemask_nodeid_is_invalid(nodeid)) {
    ERR_PRINTF("Invalid node id\n");
    return 0;
  }

  if (ndb[nodeid - 1])
    ndb[nodeid - 1]->refCnt++;
  return ndb[nodeid - 1];
}
