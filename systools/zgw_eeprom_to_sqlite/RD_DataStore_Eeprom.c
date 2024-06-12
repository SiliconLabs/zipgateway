#include <stddef.h>
#include <string.h> /* for memcpy, memset, etc */
#include "smalloc.h"
#include "TYPES.H"
#include "ZIP_Router_logging.h"
#include "dev/eeprom.h"
#include "DataStore.h"
#include "RD_internal.h"
#include "RD_DataStore_Eeprom.h"
#include "ResourceDirectory.h"
#include "DataStore.h"
#include "memb.h"
#include "zgw_smalloc_utils.h"

uint16_t rd_eeprom_read(uint16_t offset, uint8_t len, void *data)
{
  //DBG_PRINTF("Reading at %x\n", 0x40 + offset);
  eeprom_read(0x40 + offset, data, len);
  return len;
}

uint16_t rd_eeprom_write(uint16_t offset,uint8_t len,void* data) {
  //DBG_PRINTF("Writing at %x\n", 0x100 + offset);
  if(len) {
    eeprom_write(0x40 + offset,data,len);
  }
  return len;
}

static const small_memory_device_t nvm_dev  = {
  .offset = offsetof(rd_eeprom_static_hdr_t, smalloc_space),
  .size = sizeof(((rd_eeprom_static_hdr_t*)0)->smalloc_space),
  .psize = 8,
  .read = rd_eeprom_read,
  .write = rd_eeprom_write,
};

static void rd_data_store_cc_version_read(uint16_t ptr_offset, rd_node_database_entry_t *n)
{
  cc_version_pair_t cv_pair;
  int cnt = n->node_cc_versions_len / sizeof(cc_version_pair_t);
  int i;
  for (i = 0; i < cnt; i++)
  {
    rd_eeprom_read(ptr_offset, sizeof(cc_version_pair_t), &cv_pair);
    rd_node_cc_version_set(n, cv_pair.command_class, cv_pair.version);
    ptr_offset += sizeof(cc_version_pair_t);
  }
}

static rd_ep_database_entry_t *rd_data_store_read_ep(uint16_t ptr, rd_node_database_entry_t *n)
{
  rd_ep_data_store_entry_t e;
  rd_ep_database_entry_t *ep;

  rd_eeprom_read(ptr, sizeof(rd_ep_data_store_entry_t), &e);

  ep = rd_data_mem_alloc(sizeof(rd_ep_database_entry_t));
  if (!ep)
  {
    return 0;
  }
  memcpy(&ep->endpoint_info_len, &e, sizeof(rd_ep_data_store_entry_t));

  /*Setup pointers*/
  ep->endpoint_info = rd_data_mem_alloc(e.endpoint_info_len);
  ep->endpoint_name = rd_data_mem_alloc(e.endpoint_name_len);
  ep->endpoint_location = rd_data_mem_alloc(e.endpoint_loc_len);
  ep->endpoint_agg = rd_data_mem_alloc(e.endpoint_aggr_len);

  if (
      (e.endpoint_info_len && ep->endpoint_info == 0) ||
      (e.endpoint_name_len && ep->endpoint_name == 0) ||
      (e.endpoint_loc_len && ep->endpoint_location == 0) ||
      (e.endpoint_aggr_len && ep->endpoint_agg == 0))
  {
    rd_store_mem_free_ep(ep);
    return 0;
  }

  ptr += sizeof(rd_ep_data_store_entry_t);
  rd_eeprom_read(ptr, ep->endpoint_info_len, ep->endpoint_info);
  ptr += ep->endpoint_info_len;
  rd_eeprom_read(ptr, ep->endpoint_name_len, ep->endpoint_name);
  ptr += ep->endpoint_name_len;
  rd_eeprom_read(ptr, ep->endpoint_loc_len, ep->endpoint_location);

  ptr += ep->endpoint_loc_len;
  rd_eeprom_read(ptr, ep->endpoint_aggr_len, ep->endpoint_agg);

  ep->node = n;
  return ep;
}

/*
 * EEPROM converters from and to various versions
 * v0   - v2.0
 * v2.0 - v2.3
 * v2.1 - v2.3
 * v2.2 - v2.3
 * Note that there is no converter from v2.0 or v2.1 to v2.2 since eeprom.dat
 * v2.2 only exists in 2.81.03
 */
/*
 * Convert the eeprom version from eeprom v0 to v2.0, i.e. from ZIPGW 2.6x to
 * ZIPGW 2.81.03. Check Z/IP Gateway user guide - Overview of eeprom versions
 * section for more version mapping details/
 */
bool data_store_convert_none_to_2_0(uint8_t old_MyNodeID) {
   rd_node_database_entry_t r;
   uint8_t maj = 2;
   uint8_t min = 0;
   uint32_t magic = RD_MAGIC_V1; 
   uint8_t i;
   uint16_t n_ptr = 0;
   uint8_t zero = 0;
   uint8_t state_version_offset = 0;
   rd_node_state_t node_state;

    WRN_PRINTF("Found old eeprom, attempting conversion.\n");

    /* Read in gw info from eeprom to determine version. */
    rd_eeprom_read(offsetof(rd_eeprom_static_hdr_t, node_ptrs[old_MyNodeID]),
                   sizeof(uint16_t), &n_ptr);
    if (n_ptr < offsetof(rd_eeprom_static_hdr_t, smalloc_space)) {
       ERR_PRINTF("Invalid gateway node ptr 0x%x for gateway node id %d\n",
                  n_ptr, old_MyNodeID);
       return false;
    }
    rd_eeprom_read(n_ptr + offsetof(rd_node_database_entry_v20_t, state),
                   sizeof(node_state), &node_state);
    if (node_state < 0xA || node_state > 0xB) {
       /* Gateway probe state must always be DONE. */
       ERR_PRINTF("Invalid eeprom file\n");

       return false;
    }

    /* No need to insert bytes in eeprom file for version_major, version_minor as by
       structure alignment on 32 bit systems, these fields take offset where there was garbage
       so we just neeed to set those bytes at those offsets. Same for dskLen */
    rd_eeprom_write(offsetof(rd_eeprom_static_hdr_t, version_major),1,&maj); // Major Version 2
    rd_eeprom_write(offsetof(rd_eeprom_static_hdr_t, version_minor),1,&min); // Minor Version 0
    rd_eeprom_write(offsetof(rd_eeprom_static_hdr_t, magic),sizeof(magic), &magic);

    state_version_offset = STATUS_DONE - node_state;
    for (i = 1; i < ZW_MAX_EEPROM_NODES; i++) {
        n_ptr = 0;
        rd_eeprom_read(offsetof(rd_eeprom_static_hdr_t,node_ptrs[i]),sizeof(uint16_t),&n_ptr);
        if (n_ptr < offsetof(rd_eeprom_static_hdr_t, smalloc_space)) {
           if (n_ptr != 0) {
              WRN_PRINTF("Skipping invalid node ptr 0x%x (0x%zu) for position %d\n",
                         n_ptr, offsetof(rd_eeprom_static_hdr_t, smalloc_space), i);
           }
           continue;
        }
        WRN_PRINTF("Converting node: %d\n", i);

        /* Update node state */
        /* Default to a sane state in case read fails. */
        node_state = STATUS_DONE-state_version_offset;
        rd_eeprom_read(n_ptr + offsetof(rd_node_database_entry_v20_t, state),
                       sizeof(node_state), &node_state);
        node_state += state_version_offset;
        rd_eeprom_write(n_ptr + offsetof(rd_node_database_entry_v20_t, state),
                        sizeof(node_state), &node_state);
        /* Write 0 (of size 1 byte) at dskLen */
        rd_eeprom_write(n_ptr + offsetof(rd_node_database_entry_v20_t, dskLen),
                        sizeof(r.dskLen), &zero);
        /* Not writing DSK as we set dsk len to zero */
    }
    return true;
}

static void convert_rd_legacy_to_sqlite(rd_node_database_entry_legacy_t *n_legacy, rd_node_database_entry_t *n)
{
  n->wakeUp_interval = n_legacy->wakeUp_interval;
  n->lastAwake = n_legacy->lastAwake;
  n->lastUpdate = n_legacy->lastUpdate;
  n->nodeid = n_legacy->nodeid;
  n->security_flags = n_legacy->security_flags;
  n->mode = n_legacy->mode;
  n->state = n_legacy->state;
  n->manufacturerID = n_legacy->manufacturerID;
  n->productType = n_legacy->productType;
  n->productID = n_legacy->productID;
  n->nodeType = n_legacy->nodeType;
  n->refCnt = n_legacy->refCnt;
  n->nEndpoints = n_legacy->nEndpoints;
  n->nAggEndpoints = n_legacy->nAggEndpoints;
  n->nodeNameLen = n_legacy->nodeNameLen;
  n->dskLen = n_legacy->dskLen;
}

rd_node_database_entry_t *rd_data_store_read_2_0(uint8_t nodeID)
{
  rd_node_database_entry_legacy_t *n_legacy=NULL;
  rd_node_database_entry_t *n=NULL;
  rd_ep_database_entry_t *e;
  uint16_t n_ptr = 0, e_ptr = 0;
  uint8_t i = 0, j = 0;
  uint8_t maj = 2;
  uint8_t min = 3;
  const uint16_t static_size_v20 = offsetof(rd_node_database_entry_v20_t, nodename);
  //Node 232 not supported in old eeproms
  if(nodeID >= ZW_MAX_EEPROM_NODES) {
    return NULL;
  }

  n_ptr = 0;
  rd_eeprom_read(offsetof(rd_eeprom_static_hdr_t, node_ptrs[nodeID]), sizeof(uint16_t), &n_ptr);
  if (n_ptr != 0)
  {
    n_legacy = rd_data_mem_alloc(sizeof(rd_node_database_entry_legacy_t));
    memset(n_legacy, 0, sizeof(rd_node_database_entry_legacy_t));
    rd_eeprom_read(n_ptr, static_size_v20, n_legacy);

    n = rd_data_mem_alloc(sizeof(rd_node_database_entry_t));
    memset(n, 0, sizeof(rd_node_database_entry_t));
    convert_rd_legacy_to_sqlite(n_legacy, n);
    rd_data_mem_free(n_legacy);

    /* JUST_ADDED and ADDED_BY_ME are flagged in node_properties_flags instead
       * of security_flags  */
    if (n->security_flags & OBSOLETED_NODE_FLAG_JUST_ADDED)
    {
      n->security_flags &= ~OBSOLETED_NODE_FLAG_JUST_ADDED;
      n->node_properties_flags |= RD_NODE_FLAG_JUST_ADDED;
    }
    if (n->security_flags & OBSOLETED_NODE_FLAG_ADDED_BY_ME)
    {
      n->security_flags &= ~OBSOLETED_NODE_FLAG_ADDED_BY_ME;
      n->node_properties_flags |= RD_NODE_FLAG_ADDED_BY_ME;
    }
    DBG_PRINTF("Index %d Node ID %d\n", nodeID, n->nodeid);
    DBG_PRINTF("wake up %d, last awake %d, last update %d, nodeid %d, security flags %d, mode %d, state %d, manufacturer id %d, product type %d, product id %d, node type %d, nEndpoint %d, nAggEndpoint %d\n", n->wakeUp_interval, n->lastAwake, n->lastUpdate, n->nodeid, n->security_flags, n->mode, n->state, n->manufacturerID, n->productType, n->productID, n->nodeType, n->nEndpoints, n->nAggEndpoints);
    LIST_STRUCT_INIT(n, endpoints);
    n->nodename = rd_data_mem_alloc(n->nodeNameLen);
    rd_eeprom_read(n_ptr + static_size_v20, n->nodeNameLen, n->nodename);

    n->dsk = rd_data_mem_alloc(n->dskLen);
    rd_eeprom_read(n_ptr + static_size_v20 + n->nodeNameLen, n->dskLen, n->dsk);

    n->node_cc_versions_len = controlled_cc_v_size();
    n->node_cc_versions = rd_data_mem_alloc(n->node_cc_versions_len);
    rd_node_cc_versions_set_default(n);
    /* v20 eeprom contains no node_cc_version */
    n_ptr += static_size_v20 + n->nodeNameLen + n->dskLen;

    for (j = 0; j < n->nEndpoints; j++)
    {
      n_ptr += rd_eeprom_read(n_ptr, sizeof(uint16_t), &e_ptr);

      e = rd_data_store_read_ep(e_ptr, n);
      DBG_PRINTF("EP alloc %p\n", e);
      assert(e);
      list_add(n->endpoints, e);
    }
  }
  return n;
}

rd_node_database_entry_t *rd_data_store_read_2_1(uint8_t nodeID)
{
  return rd_data_store_read_2_3(nodeID + 1);
}

/*
 * EEPROM conversion from v2.2 to v2.3, namely ZIPGW 2.81.03 to 7.11.01
 */
rd_node_database_entry_t *rd_data_store_read_2_2(uint8_t nodeID)
{
  rd_node_database_entry_legacy_t *n_legacy=NULL;
  rd_node_database_entry_t *n=NULL;
  rd_ep_database_entry_t *e;
  uint16_t n_ptr = 0, e_ptr = 0;
  uint8_t i = 0, j = 0;
  uint8_t maj = 2;
  uint8_t min = 3;
  const uint16_t static_size_v22 = offsetof(rd_node_database_entry_v22_t, nodename);

  if(nodeID > ZW_MAX_EEPROM_NODES) {
    return NULL;
  }

  /* Convert the eeprom node by node, starting from index 0 since v2.2 eeprom
   * node index has been shifted. */
  n_ptr = 0;
  rd_eeprom_read(offsetof(rd_eeprom_static_hdr_t, node_ptrs[nodeID-1]), sizeof(uint16_t), &n_ptr);
  if (n_ptr != 0)
  {
    n_legacy = rd_data_mem_alloc(sizeof(rd_node_database_entry_legacy_t));
    memset(n_legacy, 0, sizeof(rd_node_database_entry_legacy_t));
    rd_eeprom_read(n_ptr, static_size_v22, n_legacy);

    n = rd_data_mem_alloc(sizeof(rd_node_database_entry_t));
    memset(n, 0, sizeof(rd_node_database_entry_t));
    convert_rd_legacy_to_sqlite(n_legacy, n);
    rd_data_mem_free(n_legacy);

    /* JUST_ADDED and ADDED_BY_ME are flagged in node_properties_flags instead
       * of security_flags  */
    if (n->security_flags & OBSOLETED_NODE_FLAG_JUST_ADDED)
    {
      n->security_flags &= ~OBSOLETED_NODE_FLAG_JUST_ADDED;
      n->node_properties_flags |= RD_NODE_FLAG_JUST_ADDED;
    }
    if (n->security_flags & OBSOLETED_NODE_FLAG_ADDED_BY_ME)
    {
      n->security_flags &= ~OBSOLETED_NODE_FLAG_ADDED_BY_ME;
      n->node_properties_flags |= RD_NODE_FLAG_ADDED_BY_ME;
    }
    DBG_PRINTF("wake up %d, last awake %d, last update %d, nodeid %d, security flags %d, mode %d, state %d, manufacturer id %d, product type %d, product id %d, node type %d, nEndpoint %d, nAggEndpoint %d\n", n->wakeUp_interval, n->lastAwake, n->lastUpdate, n->nodeid, n->security_flags, n->mode, n->state, n->manufacturerID, n->productType, n->productID, n->nodeType, n->nEndpoints, n->nAggEndpoints);
    LIST_STRUCT_INIT(n, endpoints);
    n->nodename = rd_data_mem_alloc(n->nodeNameLen);
    rd_eeprom_read(n_ptr + static_size_v22, n->nodeNameLen, n->nodename);

    n->dsk = rd_data_mem_alloc(n->dskLen);
    rd_eeprom_read(n_ptr + static_size_v22 + n->nodeNameLen, n->dskLen, n->dsk);

    n->node_cc_versions_len = controlled_cc_v_size();
    n->node_cc_versions = rd_data_mem_alloc(n->node_cc_versions_len);
    rd_node_cc_versions_set_default(n);
    /* v22 eeprom contains no node_cc_version */
    n_ptr += static_size_v22 + n->nodeNameLen + n->dskLen;

    for (j = 0; j < n->nEndpoints; j++)
    {
      n_ptr += rd_eeprom_read(n_ptr, sizeof(uint16_t), &e_ptr);

      e = rd_data_store_read_ep(e_ptr, n);
      DBG_PRINTF("EP alloc %p\n", e);
      assert(e);
      list_add(n->endpoints, e);
    }
  }
  return n;
}

rd_node_database_entry_t *rd_data_store_read_2_3(uint8_t nodeID)
{
  rd_node_database_entry_legacy_t *r_legacy;
  rd_node_database_entry_t *r;
  rd_ep_database_entry_t *ep;
  uint16_t n_ptr[2], e_ptr;
  uint8_t i;

  /*Size of static content of */
  const uint16_t static_size_legacy = offsetof(rd_node_database_entry_legacy_t, nodename);

  if(nodeID > ZW_MAX_EEPROM_NODES) {
    return NULL;
  }

  n_ptr[0] = 0;
  rd_eeprom_read(offsetof(rd_eeprom_static_hdr_t, node_ptrs[nodeID - 1]), sizeof(uint16_t), &n_ptr[0]);

  if (n_ptr[0] == 0)
  {
    //DBG_PRINTF("Node %i is not in eeprom\n",nodeID);
    return 0;
  }

  r = rd_data_mem_alloc(sizeof(rd_node_database_entry_t));
  if (r == 0)
  {
    ERR_PRINTF("Out of memory\n");
    return 0;
  }
  memset(r, 0, sizeof(rd_node_database_entry_t));

  r_legacy = rd_data_mem_alloc(sizeof(rd_node_database_entry_legacy_t));
  if (r_legacy == 0)
  {
    ERR_PRINTF("Out of memory\n");
    return 0;
  }
  memset(r_legacy, 0, sizeof(rd_node_database_entry_legacy_t));

  /*Read the first part of the node entry*/
  rd_eeprom_read(n_ptr[0], static_size_legacy, r_legacy);
  convert_rd_legacy_to_sqlite(r_legacy, r);
  rd_data_mem_free(r_legacy);
  DBG_PRINTF("wake up %d, last awake %d, last update %d, nodeid %d, security flags %d, mode %d, state %d, manufacturer id %d, product type %d, product id %d, node type %d, nEndpoint %d, nAggEndpoint %d\n", r->wakeUp_interval, r->lastAwake, r->lastUpdate, r->nodeid, r->security_flags, r->mode, r->state, r->manufacturerID, r->productType, r->productID, r->nodeType, r->nEndpoints, r->nAggEndpoints);
  /* Init node_cc_versions and its length */
  r->node_cc_versions_len = controlled_cc_v_size();
  r->node_cc_versions = rd_data_mem_alloc(r->node_cc_versions_len);
  rd_node_cc_versions_set_default(r);
  rd_data_store_cc_version_read(n_ptr[0] + static_size_legacy, r);

  /* Due to the limitation of one chunk smalloc space size 0x80, we have to use
   * another pointer to keep the rest of node entry */
  rd_eeprom_read(n_ptr[0] + static_size_legacy + r->node_cc_versions_len, sizeof(uint16_t), &n_ptr[1]);

  /*Init the endpoint list */
  LIST_STRUCT_INIT(r, endpoints);

  if (r->nodeNameLen == 0)
  {
    r->nodename = NULL;
  }
  else
  {
    /*Read the node name*/
    r->nodename = rd_data_mem_alloc(r->nodeNameLen);
    if (r->nodename != NULL)
    {
      rd_eeprom_read(n_ptr[1], r->nodeNameLen, r->nodename);
    }
  }

  if (r->dskLen == 0)
  {
    r->dsk = NULL;
  }
  else
  {
    r->dsk = rd_data_mem_alloc(r->dskLen);
    if (r->dsk != NULL)
    {
      rd_eeprom_read(n_ptr[1] + r->nodeNameLen, r->dskLen, r->dsk);
    }
  }

  /* PCVS: ProbeCCVersionState is not persisted in eeprom */
  r->pcvs = NULL;

  n_ptr[1] += r->nodeNameLen + r->dskLen;
  for (i = 0; i < r->nEndpoints; i++)
  {
    rd_eeprom_read(n_ptr[1], sizeof(uint16_t), &e_ptr);

    ep = rd_data_store_read_ep(e_ptr, r);
    DBG_PRINTF("EP alloc %p id %i\n", ep, ep->endpoint_id);
    assert(ep);
    list_add(r->endpoints, ep);
    n_ptr[1] += sizeof(uint16_t);
  }
  return r;
}

uint32_t rd_eeprom_magic_get()
{
  uint32_t magic;
  rd_eeprom_read(offsetof(rd_eeprom_static_hdr_t, magic), sizeof(uint32_t), &magic);
  return magic;
}

uint32_t rd_eeprom_homeid_get()
{
  uint32_t homeID;
  rd_eeprom_read(offsetof(rd_eeprom_static_hdr_t, homeID), sizeof(uint32_t), &homeID);
  return homeID;
}

uint8_t rd_eeprom_nodeid_get()
{
  uint8_t MyNodeID;
  rd_eeprom_read(offsetof(rd_eeprom_static_hdr_t, nodeID), sizeof(uint8_t), &MyNodeID);
  return MyNodeID;
}

uint32_t rd_eeprom_version_get_legacy()
{
  uint8_t major = 0, minor = 0;
  uint32_t eeprom_version = 0;
  rd_eeprom_read(offsetof(rd_eeprom_static_hdr_t, version_major), sizeof(uint8_t), &major);
  rd_eeprom_read(offsetof(rd_eeprom_static_hdr_t, version_minor), sizeof(uint8_t), &minor);
  eeprom_version = major * 100 + minor;
  return eeprom_version;
}

bool rd_data_store_validate(int eeprom_version)
{
  LOG_PRINTF("Validating input eeprom - magic version %x\n", rd_eeprom_magic_get());
  //TODO do some more
  if (rd_eeprom_magic_get() != RD_MAGIC_V1
   && rd_eeprom_magic_get() != RD_MAGIC_V0) {
    WRN_PRINTF("Invalid magic version\n");
    return false;
  }

  rd_eeprom_static_hdr_t static_hdr;
  rd_eeprom_read(0, offsetof(rd_eeprom_static_hdr_t, flags), &static_hdr);
  for (uint8_t index=0; index<ZW_MAX_EEPROM_NODES; index++) {
    rd_eeprom_read(offsetof(rd_eeprom_static_hdr_t, node_ptrs[index]),
                   sizeof(uint16_t),
                   &static_hdr.node_ptrs[index]);
  }
  LOG_PRINTF("Validating smalloc space consistency\n");
  if (is_smalloc_consistent(&nvm_dev, static_hdr.node_ptrs, ZW_MAX_EEPROM_NODES, eeprom_version) == false) {
    WRN_PRINTF("Smalloc consistency check failed\n");
    return false;
  }
  return true;
}

void eeprom_datastore_unpersist_association(list_t ip_association_table, struct memb *ip_association_pool)
{
  uint16_t len;
  int addr;
  int i;

  ASSERT(*ip_association_table == NULL); /* Make sure we dont leak memory */
  rd_eeprom_read(offsetof(rd_eeprom_static_hdr_t, association_table_length), sizeof(uint16_t), &len);

  // Read IP associations
  for (i = 0; i < len; i++)
  {
    ip_association_t *a = (ip_association_t *)memb_alloc(ip_association_pool);
    if (!a)
    {
      ERR_PRINTF("Out of memory during unpersist of association table\n");
      return;
    }
    addr = offsetof(rd_eeprom_static_hdr_t, association_table) + i * sizeof(ip_association_t);
    LOG_PRINTF("Unpersisting IP association %u at address 0x%x, length %u\n", i, addr + 0x40,
               (u32_t)sizeof(ip_association_t));
    rd_eeprom_read(addr, sizeof(ip_association_t), a);
    // New association was not found in the table. Add it.
    list_add(ip_association_table, a);
  }
}

size_t eeprom_datastore_unpersist_virtual_nodes(nodeid_t *nodelist, size_t max_node_count)
{
  uint8_t nodeid_count = 0;
  // Buffer for storing 1 byte node ID list
  uint8_t buffer[MAX_CLASSIC_TEMP_ASSOCIATIONS] = {0};

  rd_eeprom_read(
      offsetof(rd_eeprom_static_hdr_t, temp_assoc_virtual_nodeid_count), sizeof(uint8_t), &nodeid_count);
  if (nodeid_count > MAX_CLASSIC_TEMP_ASSOCIATIONS)
  {
    nodeid_count = MAX_CLASSIC_TEMP_ASSOCIATIONS;
  }
  rd_eeprom_read(offsetof(rd_eeprom_static_hdr_t, temp_assoc_virtual_nodeids),
                 nodeid_count * sizeof(uint8_t), buffer);
  for(int i=0; i<nodeid_count; i++) {
    nodelist[i] = buffer[i];
  }
  return nodeid_count;
}

bool node_sanity_check(rd_node_database_entry_t *n, int nodeid)
{
  if (n->nodeid != nodeid) {
    WRN_PRINTF("Node ID: %u should be %d\n", n->nodeid, nodeid);
    return false;
  }

  if (n->state < STATUS_CREATED || n->state > STATUS_FAILING) {
    WRN_PRINTF("node %u - invalid state %d\n", n->nodeid, n->state);
    return false;
  }

  if ((((n->mode & 0xff) < MODE_PROBING) || (n->mode & 0xff) > MODE_MAILBOX) &&
         ((n->mode & 0xff) != MODE_NODE_UNDEF)) {
    WRN_PRINTF("node %u - mode:0x%02x (unknown) \n", n->nodeid, n->mode & 0xff);
    return false;
  }
  if (((n->mode & 0xff00) != MODE_FLAGS_DELETED) &&
      ((n->mode & 0xff00)!= MODE_FLAGS_FAILED) &&
      ((n->mode & 0xff00)!= MODE_FLAGS_LOWBAT) &&
      ((n->mode & 0xff00) != 0)) {
    WRN_PRINTF("node %u - mode:0x%04x (unknown) \n", n->nodeid, n->mode);
    return false;
  }

  if ((n->dskLen != 0) && (n->dskLen != 16)) {
    WRN_PRINTF("node %u - invalid dsklen:%d\n", n->nodeid, n->dskLen);
    return false;
  }

  if (n->nEndpoints != list_length(n->endpoints)) {
    WRN_PRINTF("node %u - number of endpoints: %u length of endpoints list: %u\n"
               , n->nodeid, n->nEndpoints, list_length(n->endpoints));
    return false;
  }
  if (n->nEndpoints < 1) {
    WRN_PRINTF("node %u - invalid number of endpoints: %u\n", n->nodeid, n->nEndpoints);
    return false;
  }
  if (list_length(n->endpoints) < 1) {
    WRN_PRINTF("node %u - invalid length of endpoints: %u\n", n->nodeid, list_length(n->endpoints));
    return false;
  }
  /* FIXME: Only in Ring's case */
  /*
  if (n->nodeid == 2 || n->nodeid == 3 || n->nodeid == 4 || n->nodeid == 5) {
    WRN_PRINTF("node %u - virtual nodes assumed to be node 2, 3, 4, 5\n", n->nodeid);
    return false;
  }*/
  return true;
}
