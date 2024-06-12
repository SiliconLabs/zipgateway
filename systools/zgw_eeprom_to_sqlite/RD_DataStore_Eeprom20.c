#include <stddef.h>
#include <string.h>
#include <assert.h>

#include "RD_DataStore.h"
#include "RD_DataStore_Eeprom20.h"
#include "ZIP_Router_logging.h"
#include "test_helpers.h"

/* Mainly copy from 2.81.02 RD_Datastore and adapt the data type to v20 */
static rd_ep_database_entry_v20_t*
rd_data_store_read_ep(uint16_t ptr,
                      rd_node_database_entry_v20_t* n) {
  rd_ep_data_store_entry_v20_t e;
  rd_ep_database_entry_v20_t* ep;

  rd_eeprom_read(ptr, sizeof(rd_ep_data_store_entry_v20_t), &e);

  ep = rd_data_mem_alloc(sizeof(rd_ep_database_entry_v20_t));
  if(!ep) {
    return 0;
  }
  memcpy(&ep->endpoint_info_len, &e, sizeof(rd_ep_data_store_entry_v20_t));

  /*Setup pointers*/
  ep->endpoint_info = rd_data_mem_alloc(e.endpoint_info_len);
  ep->endpoint_name = rd_data_mem_alloc(e.endpoint_name_len);
  ep->endpoint_location = rd_data_mem_alloc(e.endpoint_loc_len);
  ep->endpoint_agg =  rd_data_mem_alloc(e.endpoint_aggr_len);

  if(
      (e.endpoint_info_len && ep->endpoint_info==0) ||
      (e.endpoint_name_len && ep->endpoint_name==0) ||
      (e.endpoint_loc_len && ep->endpoint_location==0) ||
      (e.endpoint_aggr_len && ep->endpoint_agg==0)
    ) {
    rd_store_mem_free_ep((rd_ep_database_entry_t*)ep);
    return 0;
  }

  ptr += sizeof(rd_ep_data_store_entry_v20_t);
  rd_eeprom_read(ptr,ep->endpoint_info_len, ep->endpoint_info);
  ptr += ep->endpoint_info_len;
  rd_eeprom_read(ptr,ep->endpoint_name_len, ep->endpoint_name);
  ptr += ep->endpoint_name_len;
  rd_eeprom_read(ptr,ep->endpoint_loc_len, ep->endpoint_location);

  ptr += ep->endpoint_loc_len;
  rd_eeprom_read(ptr,ep->endpoint_aggr_len, ep->endpoint_agg);

  ep->node = n;
  return ep;
}

rd_node_database_entry_v20_t* rd_data_store_read_v20(uint8_t nodeID)
{
  rd_node_database_entry_v20_t *r;
  rd_ep_database_entry_v20_t *ep;
  uint16_t n_ptr, e_ptr;
  uint8_t i;

  /*Size of static content of */
  const uint16_t static_size = offsetof(rd_node_database_entry_v20_t, nodename);

  n_ptr = 0;
  rd_eeprom_read(offsetof(rd_eeprom_static_hdr_v20_t, node_ptrs[nodeID]),
                 sizeof(uint16_t),
                 &n_ptr);

  if (n_ptr==0) {
    //DBG_PRINTF("Node %i is not in eeprom\n", nodeID);
    return 0;
  }

  r = rd_data_mem_alloc(sizeof(rd_node_database_entry_v20_t));
  if (r == 0) {
    ERR_PRINTF("Out of memory\n");
    return 0;
  }
  memset(r, 0, sizeof(rd_node_database_entry_v20_t));

  /* Read the first part of the node entry */
  rd_eeprom_read(n_ptr, static_size, r );

  LIST_STRUCT_INIT(r, endpoints);

  /* Read the node name */
  r->nodename = rd_data_mem_alloc(r->nodeNameLen);
  if (r->nodename != NULL) {
    rd_eeprom_read(n_ptr + static_size, r->nodeNameLen ,r->nodename );
  }

  r->dsk =  rd_data_mem_alloc(r->dskLen);
  if (r->dsk != NULL) {
      rd_eeprom_read(n_ptr + static_size + r->nodeNameLen, r->dskLen, r->dsk);
  }

  n_ptr += static_size + r->nodeNameLen + r->dskLen;
  for(i=0; i < r->nEndpoints; i++) {
    rd_eeprom_read(n_ptr, sizeof(uint16_t), &e_ptr);

    ep = rd_data_store_read_ep(e_ptr, r);
    //DBG_PRINTF("EP alloc %p\n",ep);
    assert(ep);
    list_add(r->endpoints, ep);
    n_ptr+=sizeof(uint16_t);
  }

  return r;
}

void rd_store_mem_free_ep_v20(rd_ep_database_entry_v20_t* ep) {
  if(ep->endpoint_info) rd_data_mem_free(ep->endpoint_info);
  if(ep->endpoint_name) rd_data_mem_free(ep->endpoint_name);
  if(ep->endpoint_location) rd_data_mem_free(ep->endpoint_location);
  rd_data_mem_free(ep);
}

void rd_data_store_mem_free_v20(rd_node_database_entry_v20_t *n) {
  rd_ep_database_entry_v20_t *ep;

  while( (ep = list_pop(n->endpoints)) ) {
    //DBG_PRINTF("EP free %p\n",ep);
    rd_store_mem_free_ep_v20(ep);
  }

  if(n->nodename) rd_data_mem_free(n->nodename);

  rd_data_mem_free(n);
}




int rd_data_store_entry_compare_v20_v23(const rd_node_database_entry_v20_t *v20, const rd_node_database_entry_v23_t *v23)
{
  /// Verify if everything is sane
  /// The last node should be NULL because only maximum 231 nodes in 2.81
  if ((v20 == 0) || (v23 == 0))
  {
    /// Dumb check, but just for readability
    check_equal(v20, 0, "empty node");
    check_equal(v23, 0, "empty node");
  }
  else
  {
    /// verify all the fields
    check_equal(v20->wakeUp_interval,
                v23->wakeUp_interval,
                "wake up interval");
    check_equal(v20->lastAwake,
                v23->lastAwake,
                "last awake");
    check_equal(v20->lastUpdate,
                v23->lastUpdate,
                "last update");
    //The IP address field has been deprecated in format 2.3
    // check_mem(&v20->ipv6_address.u8[0],
    //           &v23->ipv6_address.u8[0],
    //           sizeof(v20->ipv6_address),
    //           "IPv6 address mismatch for element %02d. Expected %02x, was %02x\n",
    //           "IPv6 address");
    check_equal(v20->nodeid,
                v23->nodeid,
                "node ID");
    check_equal(v20->mode,
                v23->mode,
                "mode");
    check_equal(v20->state,
                v23->state,
                "state");
    check_equal(v20->manufacturerID,
                v23->manufacturerID,
                "manufacturer ID");
    check_equal(v20->productType,
                v23->productType,
                "product type");
    check_equal(v20->productID,
                v23->productID,
                "product ID");
    check_equal(v20->nodeType,
                v23->nodeType,
                "node type");
    // Refcnt is purposely not checked as it is not used.
    // check_equal(v20->refCnt,
    //             v23->refCnt,
    //             "reference count");
     check_equal(v20->nEndpoints,
                v23->nEndpoints,
                "number of endpoints");
    check_equal(v20->nAggEndpoints,
                v23->nAggEndpoints,
                "number of aggregated endpoints");
    check_equal(v20->nodeNameLen,
                v23->nodeNameLen,
                "node name length");
    check_equal(v20->dskLen,
                v23->dskLen,
                "dsk length");
    check_mem((uint8_t *)v20->nodename,
              (uint8_t *)v23->nodename,
              v20->nodeNameLen,
              "node name mismatch for element %02d. Expected %02x, was %02x",
              "node name");
    check_mem(v20->dsk,
              v23->dsk,
              v20->dskLen,
              "dsk mismatch for element %02d. Expected %02x, was %02x",
              "dsk");

    /// Endpoints
    if (v20->nEndpoints > 0)
    {
      check_not_null(v20->endpoints, "v20 endpoint pointer not NULL");
      check_not_null(v23->endpoints, "v23 endpoint pointer not NULL");

      /// nEndpoint check already is done above
      check_equal(list_length(v20->endpoints),
                  v20->nEndpoints,
                  "v20 endpoint list length");
      check_equal(list_length(v23->endpoints),
                  v23->nEndpoints,
                  "v23 endpoint list length");
      int epid = 0;
      rd_ep_database_entry_v20_t *e_v20 = list_head(v20->endpoints);
      rd_ep_database_entry_v23_t *e_v23 = list_head(v23->endpoints);
      while (epid < v20->nEndpoints)
      {
        check_equal(e_v23->endpoint_id,
                    epid,
                    "endpoint ID check with fixed number");
        check_equal(e_v20->endpoint_id,
                    e_v23->endpoint_id,
                    "endpoint ID equality between v20 and v23");
        check_equal(e_v20->installer_iconID,
                    e_v23->installer_iconID,
                    "installer icon ID");
        check_equal(e_v20->user_iconID,
                    e_v23->user_iconID,
                    "user icon ID");
        check_equal(e_v20->state,
                    e_v23->state,
                    "endpoint state");
        check_equal(e_v20->endpoint_info_len,
                    e_v23->endpoint_info_len,
                    "endpoint info length");
        check_equal(e_v20->endpoint_name_len,
                    e_v23->endpoint_name_len,
                    "endpoint name length");
        check_equal(e_v20->endpoint_loc_len,
                    e_v23->endpoint_loc_len,
                    "endpoint location length");
        check_equal(e_v20->endpoint_aggr_len,
                    e_v23->endpoint_aggr_len,
                    "endpoint aggregated length");
        check_mem((uint8_t *)e_v20->endpoint_location,
                  (uint8_t *)e_v23->endpoint_location,
                  e_v20->endpoint_loc_len,
                  "endpoint location mismatch for element %02d. Expected %02x, was %02x",
                  "endpoint location");
        check_mem((uint8_t *)e_v20->endpoint_name,
                  (uint8_t *)e_v23->endpoint_name,
                  e_v20->endpoint_name_len,
                  "endpoint name mismatch for element %02d. Expected %02x, was %02x",
                  "endpoint name");
        check_mem(e_v20->endpoint_info,
                  e_v23->endpoint_info,
                  e_v20->endpoint_info_len,
                  "endpoint info mismatch for element %02d. Expected %02x, was %02x",
                  "endpoint info");
        check_mem(e_v20->endpoint_agg,
                  e_v23->endpoint_agg,
                  e_v20->endpoint_aggr_len,
                  "endpoint aggregated mismatch for element %02d. Expected %02x, was %02x",
                  "endpoint aggregated");
        /// sanity check if endpoint refers back to the right node
        check_equal(e_v20->node->nodeid,
                    v20->nodeid,
                    "node pointer inside endpoint");
        e_v20 = list_item_next(e_v20);
        e_v23 = list_item_next(e_v23);
        epid++;
      }
    }

    /// Flags moved during conversion
    if (v20->security_flags & OBSOLETED_NODE_FLAG_JUST_ADDED)
    {
      check_true(v23->node_properties_flags & RD_NODE_FLAG_JUST_ADDED,
                 "node properties flag - just added");
      check_true(!(v23->security_flags & OBSOLETED_NODE_FLAG_JUST_ADDED),
                 "check v23 security flags does not have flag - just added");
    }
    if (v20->security_flags & OBSOLETED_NODE_FLAG_ADDED_BY_ME)
    {
      check_true(v23->node_properties_flags &
                     RD_NODE_FLAG_ADDED_BY_ME,
                 "node properties flag - added by me");
      check_true(!(v23->security_flags & OBSOLETED_NODE_FLAG_ADDED_BY_ME),
                 "check v23 security flags does not have flag - added by me");
    }

    /// Verify V23 only stuff, basically compare it with default value
    check_equal(v23->node_version_cap_and_zwave_sw,
                0,
                "version capabilities and ZW software version");
    check_equal(v23->node_cc_versions_len,
                controlled_cc_v_size(),
                "node CC version length");
    check_mem((uint8_t *)v23->node_cc_versions,
              (uint8_t *)controlled_cc_v,
              controlled_cc_v_size(),
              "node CC version mismatch",
              "node CC version");
    check_equal(v23->probe_flags,
                RD_NODE_PROBE_NEVER_STARTED,
                "probe flags");
    check_equal(v23->node_is_zws_probed,
                0,
                "flag for whether Z-Wave software probed");
  }
  return numErrs;
}
