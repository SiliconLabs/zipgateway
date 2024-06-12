/* © 2019 Silicon Laboratories Inc. */
#include <rand.h>
#include <stdlib.h>
#include <stdio.h>

#include "RD_DataStore.h"
#include "DataStore.h"
#include "DataStore.h"
#include "unity.h"
#include "memb.h"
#include "s2_protocol.h"
//Stubs
uint8_t MyNodeID;
uint32_t homeID;
const char* linux_conf_database_file;

void bridge_reset()
{
}

void *random_alloc(size_t s)
{
  uint8_t *a = malloc(s);
  for (int i = 0; i < s; i++)
  {
    a[i] = rand() & 0xff;
  }
  return a;
}

rd_ep_database_entry_t *create_random_endpoint(int id)
{
  rd_ep_database_entry_t *e = random_alloc(sizeof(rd_ep_database_entry_t));
  e->list = 0;
  e->endpoint_aggr_len = rand() & 0xff;
  e->endpoint_agg = random_alloc(e->endpoint_aggr_len);
  e->endpoint_name_len = rand() & 0xff;
  e->endpoint_name = random_alloc(e->endpoint_name_len);
  e->endpoint_loc_len = rand() & 0xff;
  e->endpoint_location = random_alloc(e->endpoint_loc_len);
  e->endpoint_info_len = rand() & 0xff;
  e->endpoint_info = random_alloc(e->endpoint_info_len);
  e->installer_iconID = rand() & 0xffff;
  e->user_iconID = rand() & 0xffff;

  e->endpoint_id = id;
  return e;
}

void compare_endpoints(const rd_ep_database_entry_t *a, const rd_ep_database_entry_t *b)
{
  TEST_ASSERT_EQUAL(a->endpoint_id, b->endpoint_id);

  TEST_ASSERT_EQUAL(a->endpoint_aggr_len, b->endpoint_aggr_len);
  if (a->endpoint_aggr_len)
  {
    TEST_ASSERT_EQUAL_INT8_ARRAY(a->endpoint_agg, b->endpoint_agg, a->endpoint_aggr_len);
  }

  TEST_ASSERT_EQUAL(a->endpoint_info_len, b->endpoint_info_len);
  if (a->endpoint_info_len)
  {
    TEST_ASSERT_EQUAL_INT8_ARRAY(a->endpoint_info, b->endpoint_info, a->endpoint_info_len);
  }

  TEST_ASSERT_EQUAL(a->endpoint_loc_len, b->endpoint_loc_len);
  if (a->endpoint_loc_len)
  {
    TEST_ASSERT_EQUAL_INT8_ARRAY(a->endpoint_location, b->endpoint_location, a->endpoint_loc_len);
  }

  TEST_ASSERT_EQUAL(a->endpoint_name_len, b->endpoint_name_len);
  if (a->endpoint_name_len)
  {
    TEST_ASSERT_EQUAL_INT8_ARRAY(a->endpoint_name, b->endpoint_name, a->endpoint_name_len);
  }
}

rd_node_database_entry_t *create_random_node(int id)
{
  rd_node_database_entry_t *n;
  n = random_alloc(sizeof(rd_node_database_entry_t));
  n->nodeid = id;
  n->nodeNameLen = rand() & 0xff;
  n->nodename = random_alloc(n->nodeNameLen);
  n->dskLen = rand() & 0xff;
  n->dsk = random_alloc(n->dskLen);
  n->node_cc_versions_len = rand() & 0xff;
  n->node_cc_versions = random_alloc(n->node_cc_versions_len);
  n->pcvs = 0;
  LIST_STRUCT_INIT(n, endpoints);

  n->nEndpoints = rand() & 0xf;

  for (int i = 0; i < n->nEndpoints; i++)
  {
    rd_ep_database_entry_t *e = create_random_endpoint(i);
    e->node = n;
    list_add(n->endpoints, e);
  }
  return n;
}

void compare_nodes(const rd_node_database_entry_t *a, const rd_node_database_entry_t *b)
{
  TEST_ASSERT_EQUAL(a->wakeUp_interval, b->wakeUp_interval);
  TEST_ASSERT_EQUAL(a->lastAwake, b->lastAwake);
  TEST_ASSERT_EQUAL(a->lastUpdate, b->lastUpdate);
  TEST_ASSERT_EQUAL(a->nodeid, b->nodeid);
  TEST_ASSERT_EQUAL(a->security_flags, b->security_flags);
  TEST_ASSERT_EQUAL(a->mode, b->mode);
  TEST_ASSERT_EQUAL(a->state, b->state);
  TEST_ASSERT_EQUAL(a->manufacturerID, b->manufacturerID);
  TEST_ASSERT_EQUAL(a->productType, b->productType);
  TEST_ASSERT_EQUAL(a->productID, b->productID);
  TEST_ASSERT_EQUAL(a->nodeType, b->nodeType);
  //  TEST_ASSERT_EQUAL(a->refCnt, b->refCnt);
  TEST_ASSERT_EQUAL(a->nEndpoints, b->nEndpoints);
  TEST_ASSERT_EQUAL(a->nAggEndpoints, b->nAggEndpoints);

  TEST_ASSERT_EQUAL(a->nodeNameLen, b->nodeNameLen);
  TEST_ASSERT_EQUAL(a->dskLen, b->dskLen);
  TEST_ASSERT_EQUAL(a->node_version_cap_and_zwave_sw, b->node_version_cap_and_zwave_sw);
  TEST_ASSERT_EQUAL(a->probe_flags, b->probe_flags);

  TEST_ASSERT_EQUAL(a->node_properties_flags, b->node_properties_flags);
  TEST_ASSERT_EQUAL(a->node_cc_versions_len, b->node_cc_versions_len);
  TEST_ASSERT_EQUAL(a->node_is_zws_probed, b->node_is_zws_probed);

  if (a->nodeNameLen)
  {
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a->nodename, b->nodename, a->nodeNameLen);
  }
  if (a->dskLen)
  {
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a->dsk, b->dsk, a->dskLen);
  }
  if (a->node_cc_versions_len)
  {
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a->node_cc_versions, b->node_cc_versions, a->node_cc_versions_len);
  }

  rd_ep_database_entry_t *ae;
  rd_ep_database_entry_t *be;

  ae = list_head(a->endpoints);
  be = list_head(b->endpoints);
  while (ae && be)
  {
    compare_endpoints(ae, be);
    ae = list_item_next(ae);
    be = list_item_next(be);
  }
}

/********************* Test cases *********************/
void test_rd_node_entry()
{
  rd_node_database_entry_t *ndb_a[232];
  rd_node_database_entry_t *ndb_b[232];

  linux_conf_database_file = "test_rd_node_entry.db";
  system("rm -f test_rd_node_entry.db");
  data_store_init();

  //Generate random entries and write them to data store
  for (int i = 0; i < 232; i++)
  {
    rd_node_database_entry_t *n = create_random_node(i + 1);
    rd_data_store_nvm_write(n);
    ndb_a[i] = n;
  }

  //Read entries back
  for (int i = 0; i < 232; i++)
  {
    ndb_b[i] = rd_data_store_read(i + 1);
    TEST_ASSERT_NOT_NULL(ndb_b[i]);
  }

  //Compare entries
  for (int i = 0; i < 232; i++)
  {
    compare_nodes(ndb_a[i], ndb_b[i]);
  }

  for (int i = 0; i < 232; i++)
  {
    //Free the generated entries
    //free_node(ndb_a[i]);
    rd_data_store_mem_free(ndb_a[i]);
    //Free the read entries
    rd_data_store_mem_free(ndb_b[i]);
  }
  data_store_exit();
}

void test_rd_data_store_overwrite() {

  rd_node_database_entry_t* ndb_a;
  rd_node_database_entry_t* ndb_b;

  linux_conf_database_file = "test_rd_data_store_nvm_free.db";
  system("rm -f test_rd_data_store_nvm_free.db");
  data_store_init();

  ndb_a = create_random_node(42);
  ndb_b = create_random_node(42);

  rd_data_store_nvm_write(ndb_a);
  rd_data_store_nvm_write(ndb_b);

  rd_data_store_mem_free(ndb_a);
  ndb_a = rd_data_store_read(42);

  compare_nodes(ndb_a, ndb_b);

  rd_data_store_mem_free(ndb_a);
  rd_data_store_mem_free(ndb_b);

  data_store_exit();
}


void test_virtual_nodes()
{
#define VIRTUAL_NODE_COUNT 12

  linux_conf_database_file = "test_virtual_nodes.db";
  system("rm -f test_virtual_nodes.db");
  data_store_init();

  nodeid_t *test_nodes = random_alloc(VIRTUAL_NODE_COUNT*sizeof(nodeid_t));
  nodeid_t *cmp_nodes = random_alloc(VIRTUAL_NODE_COUNT * 2*sizeof(nodeid_t));

  for(int i=0; i < VIRTUAL_NODE_COUNT; i++) {
    test_nodes[i] = 2*i+1;
  }

  rd_datastore_persist_virtual_nodes(test_nodes, VIRTUAL_NODE_COUNT);

  //Read virtual node list, into a buffer that is larger than the number of
  //expected nodes this is to check that the count returns the actual number of
  //nodes
  size_t count = rd_datastore_unpersist_virtual_nodes(cmp_nodes, VIRTUAL_NODE_COUNT * 2);
  TEST_ASSERT_EQUAL(VIRTUAL_NODE_COUNT, count);

  //Nodes will be reordered
  for(int i=0 ; i < count; i++) {
    int j=0;
    for ( ; j < VIRTUAL_NODE_COUNT; j++)
    {
      if(test_nodes[j] == cmp_nodes[i]) {
        break;
      }
    }
    TEST_ASSERT_NOT_EQUAL(VIRTUAL_NODE_COUNT,j) ;
  }
  free(test_nodes);
  free(cmp_nodes);
  data_store_exit();
}


void test_ip_assoc_store()
{
  linux_conf_database_file = "test_ip_assoc_store.db";
  system("rm -f test_ip_assoc_store.db");
  data_store_init();

  LIST(ip_association_table_a);
  LIST(ip_association_table_b);
  MEMB(ip_association_pool, ip_association_t, MAX_IP_ASSOCIATIONS);


  for(int i=0; i <MAX_IP_ASSOCIATIONS; i++) {
    ip_association_t* ipa = random_alloc(sizeof(ip_association_t));
    list_add(ip_association_table_a,ipa);
  }
  rd_data_store_persist_associations(ip_association_table_a);
  rd_datastore_unpersist_association(ip_association_table_b, &ip_association_pool);

  ip_association_t* a = list_head(ip_association_table_a);
  ip_association_t* b = list_head(ip_association_table_b);
  while( (a!=NULL) && (b!=NULL)) {
    TEST_ASSERT_EQUAL(a->virtual_id,b->virtual_id);
    TEST_ASSERT_EQUAL(a->type,b->type);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a->resource_ip.u8,b->resource_ip.u8,16);
    TEST_ASSERT_EQUAL(a->resource_endpoint,b->resource_endpoint);
    TEST_ASSERT_EQUAL(a->resource_port,b->resource_port);
    TEST_ASSERT_EQUAL(a->virtual_endpoint,b->virtual_endpoint);
    TEST_ASSERT_EQUAL(a->grouping,b->grouping);
    TEST_ASSERT_EQUAL(a->han_nodeid,b->han_nodeid);
    TEST_ASSERT_EQUAL(a->han_endpoint,b->han_endpoint);
    TEST_ASSERT_EQUAL(a->was_dtls,b->was_dtls);
    TEST_ASSERT_EQUAL(a->mark_removal,b->mark_removal);
    a=list_item_next(a);
    b=list_item_next(b);
  }
  //Make sure that both lists has the same length
  TEST_ASSERT_NULL(b);
  TEST_ASSERT_NULL(a);

  while(list_head(ip_association_table_a)) {
    free(list_pop(ip_association_table_a));
  }
}


void test_cc_gateway() {
  Gw_Config_St_t a;
  Gw_Config_St_t b;
  a.actualPeers = 12;
  a.mode = 0x42;
  a.peerProfile=2;
  a.showlock=1;

  //Show read back and actual peers not set
  rd_datastore_persist_gw_config(&a);
  rd_datastore_unpersist_gw_config(&b);
  TEST_ASSERT_EQUAL(0,b.actualPeers);
  TEST_ASSERT_EQUAL(0x42,b.mode);
  TEST_ASSERT_EQUAL(2,b.peerProfile);
  TEST_ASSERT_EQUAL(1,b.showlock);

  //Show update
  a.mode = 0x24;
  rd_datastore_persist_gw_config(&a);
  rd_datastore_unpersist_gw_config(&b);
  TEST_ASSERT_EQUAL(0x24,b.mode);

  //Demonstrate peers
  Gw_PeerProfile_St_t pp1 = {
    .peer_ipv6_addr.u16 = {0x1234,0x1234,0x1234,0x1234},
    .peerName="Hest",
    .peerNameLength = 4,
    .port1 = 0x12,
    .port2 = 0x34,
  };

  Gw_PeerProfile_St_t pp2 = {
    .peer_ipv6_addr.u16 = {0x5678,0x5678,0x5678,0x5678},
    .peerName="Zebra",
    .peerNameLength = 5,
    .port1 = 0x56,
    .port2 = 0x78,
  };
  Gw_PeerProfile_St_t pp1_v;
  Gw_PeerProfile_St_t pp2_v;
  rd_datastore_persist_peer_profile(1,&pp1);
  rd_datastore_persist_peer_profile(2,&pp2);
  rd_datastore_unpersist_peer_profile(1,&pp1_v);
  rd_datastore_unpersist_peer_profile(2,&pp2_v);
  TEST_ASSERT_EQUAL(pp1.peerNameLength,pp1_v.peerNameLength);
  TEST_ASSERT_EQUAL(pp1.port1,pp1_v.port1);
  TEST_ASSERT_EQUAL(pp1.port2,pp1_v.port2);
  TEST_ASSERT_EQUAL_MEMORY(pp1.peer_ipv6_addr.u8,pp1_v.peer_ipv6_addr.u8,16);
  TEST_ASSERT_EQUAL_STRING(pp1.peerName,pp1_v.peerName);

  //Test that we now have two profiles
  rd_datastore_unpersist_gw_config(&b);
  TEST_ASSERT_EQUAL(0x2,b.actualPeers);

  //Test profile overwrite
  pp1.port1=0x11;
  pp1.port2=0x22;
  rd_datastore_persist_peer_profile(1,&pp1);
  rd_datastore_unpersist_peer_profile(1,&pp1_v);
  TEST_ASSERT_EQUAL(0x11,pp1_v.port1);
  TEST_ASSERT_EQUAL(0x22,pp1_v.port2);
}

void test_s2_span()
{
  linux_conf_database_file = ":memory:";
  data_store_exit();
  data_store_init();
  struct SPAN span_table[SPAN_TABLE_SIZE] = {};

  // Create test data
  const struct SPAN span_test[] = {
    { // SPAN entry to store
      {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}}, //r_nonce
      42, // lnode
      13, // rnode
      1,  // rx_seq
      10, // tx_seq
      3,  // class_id
      SPAN_NEGOTIATED // state
    },
    { // SPAN entry to skip because state is NOT SPAN_NEGOTIATED
      {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}},
      42, 13, 1, 10, 3, SPAN_NOT_USED
    },
    { // SPAN entry to skip because state is NOT SPAN_NEGOTIATED
      {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}},
      42, 13, 1, 10, 3, SPAN_NO_SEQ
    },
    { // SPAN entry to skip because state is NOT SPAN_NEGOTIATED
      {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}},
      42, 13, 1, 10, 3, SPAN_SOS
    },
    { // SPAN entry to skip because state is NOT SPAN_NEGOTIATED
      {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}},
      42, 13, 1, 10, 3, SPAN_SOS_LOCAL_NONCE
    },
    { // SPAN entry to skip because state is NOT SPAN_NEGOTIATED
      {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}},
      42, 13, 1, 10, 3, SPAN_SOS_REMOTE_NONCE
    },
    { // SPAN entry to skip because state is NOT SPAN_NEGOTIATED
      {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}},
      42, 13, 1, 10, 3, SPAN_INSTANTIATE
    },
    { // SPAN entry to store
      {{99, 98, 97, 96, 95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, 84}},
      80, 70, 60, 50, 40, SPAN_NEGOTIATED
    },
    { // SPAN entry to store with rng instead of r_nonce in SPAN.d
      {12, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}, {17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31}},
      10, 20, 30, 40, 50, SPAN_NEGOTIATED
    }};
  memcpy(span_table, span_test, sizeof(span_test));

  // Fill rest of the table with Random data (and SPAN_NEGOTIATED state)
  for (size_t i = 9; i < SPAN_TABLE_SIZE; i++) {
    span_table[i].lnode = rand();
    span_table[i].rnode = rand();
    span_table[i].rx_seq = rand();
    span_table[i].tx_seq = rand();
    span_table[i].class_id = rand();
    span_table[i].state = SPAN_NEGOTIATED;
  }
  // persist
  rd_datastore_persist_s2_span_table(span_table, SPAN_TABLE_SIZE);

  // unpersist
  struct SPAN span_readback[SPAN_TABLE_SIZE] = {};
  rd_datastore_unpersist_s2_span_table(span_readback, SPAN_TABLE_SIZE);

  // verify the data
  unsigned int idx_org = 0; // index in span_test table
  unsigned int idx_rb = 0;  // index in span_readback table
  TEST_ASSERT_EQUAL_UINT8_ARRAY(span_test[idx_org].d.r_nonce, span_readback[idx_rb].d.r_nonce, sizeof(((struct SPAN *)0)->d.r_nonce));
  TEST_ASSERT_EQUAL(span_test[idx_org].lnode, span_readback[idx_rb].lnode);
  TEST_ASSERT_EQUAL(span_test[idx_org].rnode, span_readback[idx_rb].rnode);
  TEST_ASSERT_EQUAL(span_test[idx_org].rx_seq, span_readback[idx_rb].rx_seq);
  TEST_ASSERT_EQUAL(span_test[idx_org].tx_seq, span_readback[idx_rb].tx_seq);
  TEST_ASSERT_EQUAL(span_test[idx_org].class_id, span_readback[idx_rb].class_id);
  TEST_ASSERT_EQUAL(span_test[idx_org].state, span_readback[idx_rb].state);

  // Skip entry 1-6 in the span_table, as they should not have been stored.
  idx_org = 7;

  for (idx_rb = 1; idx_rb < SPAN_TABLE_SIZE; idx_rb++) {
    if (idx_org >= SPAN_TABLE_SIZE) {
      // Test index out of idx_org are set to 0
      const struct SPAN zero_span = {0};
      TEST_ASSERT_EQUAL_UINT8_ARRAY(zero_span.d.r_nonce, span_readback[idx_rb].d.r_nonce, sizeof(((struct SPAN *)0)->d.r_nonce));
      TEST_ASSERT_EQUAL(0, span_readback[idx_rb].lnode);
      TEST_ASSERT_EQUAL(0, span_readback[idx_rb].rnode);
      TEST_ASSERT_EQUAL(0, span_readback[idx_rb].rx_seq);
      TEST_ASSERT_EQUAL(0, span_readback[idx_rb].tx_seq);
      TEST_ASSERT_EQUAL(0, span_readback[idx_rb].class_id);
      TEST_ASSERT_EQUAL(0, span_readback[idx_rb].state);
    }
    else {
      TEST_ASSERT_EQUAL_UINT8_ARRAY(span_table[idx_org].d.r_nonce, span_readback[idx_rb].d.r_nonce, sizeof(((struct SPAN *)0)->d.r_nonce));
      TEST_ASSERT_EQUAL_UINT8_ARRAY(&span_table[idx_org].d.rng, &span_readback[idx_rb].d.rng, sizeof(((struct SPAN *)0)->d.rng));
      TEST_ASSERT_EQUAL(span_table[idx_org].lnode, span_readback[idx_rb].lnode);
      TEST_ASSERT_EQUAL(span_table[idx_org].rnode, span_readback[idx_rb].rnode);
      TEST_ASSERT_EQUAL(span_table[idx_org].rx_seq, span_readback[idx_rb].rx_seq);
      TEST_ASSERT_EQUAL(span_table[idx_org].tx_seq, span_readback[idx_rb].tx_seq);
      TEST_ASSERT_EQUAL(span_table[idx_org].class_id, span_readback[idx_rb].class_id);
      TEST_ASSERT_EQUAL(span_table[idx_org].state, span_readback[idx_rb].state);
      idx_org++;
    }
  }
}
