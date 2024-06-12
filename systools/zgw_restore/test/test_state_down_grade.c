

#include "zw_data.h"
#include "zgwr_json_parser.h"
#include "json_helpers.h"
#include "zgw_data.h"
#include "zgw_restore_cfg.h"
#include "zgwr_eeprom.h"
#include "unity.h"

#include "ZW_classcmd.h"

#include "ZIP_Router.h"
#include "ResourceDirectory.h"
#include "RD_DataStore.h"
#include "zw_network_info.h"
zw_controller_t *ctrl;
json_object* zgw_backup_obj = 0;

static const zw_node_type_t basic_type_slave = {BASIC_TYPE_SLAVE, 0, 0};
static const zw_node_type_t basic_type_routing_slave = {BASIC_TYPE_ROUTING_SLAVE, 0, 0};
static const zw_node_type_t basic_type_static_controller = {BASIC_TYPE_STATIC_CONTROLLER, 0, 0};

zw_node_data_t gw_node_data =  { .node_id = 1, .capability=1, .security=1, .neighbors=NULL, .node_type={BASIC_TYPE_STATIC_CONTROLLER, 0, 0} };
zw_node_data_t node_id_2_data= { .node_id = 2, .capability=1, .security=3, .neighbors=NULL, .node_type={BASIC_TYPE_ROUTING_SLAVE, 0, 0} };
zw_node_data_t node_id_4_data= { .node_id = 4, .capability=1, .security=3, .neighbors=NULL, .node_type={BASIC_TYPE_ROUTING_SLAVE, 0, 0} };
zw_node_data_t node_id_6_data= { .node_id = 6, .capability=1, .security=3, .neighbors=NULL, .node_type={BASIC_TYPE_ROUTING_SLAVE, 0, 0} };
zw_node_data_t node_id_8_data= { .node_id = 8, .capability=1, .security=3, .neighbors=NULL, .node_type={BASIC_TYPE_ROUTING_SLAVE, 0, 0} };

void setUp() {
  remove(RESTORE_EEPROM_FILENAME);
}
void tearDown() {
  data_store_exit();
}

/* This function steals the filename */
void do_setup()
{
   cfg_init();
   /* Mock what the serial reader does. */
   MyNodeID = 0x1;
   homeID = uip_ntohl(0xfab5b0b0);

   zgw_data_init(NULL,NULL, NULL, NULL);
   ctrl = zw_controller_init(1);
   TEST_ASSERT_NOT_NULL(ctrl);

   TEST_ASSERT( zw_controller_add_node(ctrl, 1, &gw_node_data) );
   TEST_ASSERT( zw_controller_add_node(ctrl, 2, &node_id_2_data) );
   TEST_ASSERT( zw_controller_add_node(ctrl, 4, &node_id_4_data) );
   TEST_ASSERT( zw_controller_add_node(ctrl, 6, &node_id_6_data) );
   TEST_ASSERT( zw_controller_add_node(ctrl, 8, &node_id_8_data) );
  
   restore_cfg.json_filename = TEST_SRC_DIR "/test.json";
   restore_cfg.data_path="";
   // 1. Read file into json
   zgw_backup_obj = zgw_restore_json_read();

   TEST_ASSERT_NOT_NULL(zgw_backup_obj );
}


static void setup_tempered_json_file(const char* key_path, const char* key ) {

  int res = 0; /* aka success */
  json_object* child;

  do_setup();

  child = json_object_get_from_path(zgw_backup_obj,key_path );
  TEST_ASSERT_NOT_NULL(child);

  //Remove the key from the json tree, first make sure it is there. 
  TEST_ASSERT(json_object_object_get_ex( child,key,NULL) );
  json_object_object_del(child,key);

  // 2. Parse json into internal data structure
  res = zgw_restore_parse_backup(zgw_backup_obj, ctrl);
  TEST_ASSERT_EQUAL(0,res);

  /* Call the eeprom_writer with the data loaded from the JSON file */
  zgw_restore_eeprom_file();
}
/******************************************** Actual test cases ********************************/

void test_missing_nif_ep0() {
  //Try to remove the root NIF
  setup_tempered_json_file("zgw.nodeList[1].zgwZWNodeData.endpoints[0].endpointInfoFrames","nonSecureNIF");
  
  rd_node_database_entry_t *n = rd_data_store_read(2);
  //Node not present and node in state created is "almost" the same thing.
  TEST_ASSERT_NULL( n );
}

void test_missing_nif_ep1() {
  //Try to remove the ep1 nif
  setup_tempered_json_file("zgw.nodeList[1].zgwZWNodeData.endpoints[1].endpointInfoFrames","nonSecureNIF");

  rd_node_database_entry_t *n = rd_data_store_read(2);
  //Node not present and node in state created is "almost" the same thing.
  TEST_ASSERT_NULL( n );
}

void test_missing_all_eps() {
  //Try to remove the ep1 nif
  setup_tempered_json_file("zgw.nodeList[1].zgwZWNodeData","endpoints");

  rd_node_database_entry_t *n = rd_data_store_read(2);
  //Node not present and node in state created is "almost" the same thing.
  TEST_ASSERT_NULL( n );
}

void test_missing_granted_keys() {
  
  //Try to remove the root NIF
  setup_tempered_json_file("zgw.nodeList[1].zgwZWNodeData","grantedKeys");
  
  rd_node_database_entry_t *n = rd_data_store_read(2);
  TEST_ASSERT_NOT_NULL(n);
  if (n) {
    TEST_ASSERT_EQUAL(2,n->nodeid);
    TEST_ASSERT_EQUAL(RD_NODE_PROBE_NEVER_STARTED,n->probe_flags & RD_NODE_PROBE_NEVER_STARTED);
    TEST_ASSERT_EQUAL(STATUS_CREATED, n->state );
  }
}
