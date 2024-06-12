#include <lib/list.h>
#include <TYPES.H>
#include "net/uip-debug.h"
#include "RD_types.h"
//typedef unsigned char           uint8_t;
//typedef unsigned short int      uint16_t;
//typedef unsigned long int       uint32_t;
typedef uint8_t   u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;

typedef enum {
  MODE_PROBING,
  MODE_NONLISTENING,
  MODE_ALWAYSLISTENING,
  MODE_FREQUENTLYLISTENING,
  MODE_MAILBOX,
} rd_node_mode_t;

typedef enum {
  //STATUS_ADDING,
  STATUS_CREATED,
  //STATUS_PROBE_PROTOCOL_INFO,
  STATUS_PROBE_NODE_INFO,
  STATUS_PROBE_PRODUCT_ID,
  STATUS_ENUMERATE_ENDPOINTS,
  STATUS_SET_WAKE_UP_INTERVAL,
  STATUS_ASSIGN_RETURN_ROUTE,
  STATUS_PROBE_WAKE_UP_INTERVAL,
  STATUS_PROBE_ENDPOINTS,
  STATUS_MDNS_PROBE,
  STATUS_MDNS_EP_PROBE,
  STATUS_DONE,
  STATUS_PROBE_FAIL,
  STATUS_FAILING,
} rd_node_state_t;

typedef enum {
  EP_STATE_PROBE_AGGREGATED_ENDPOINTS,
	EP_STATE_PROBE_INFO,
  EP_STATE_PROBE_SEC2_C2_INFO,
  EP_STATE_PROBE_SEC2_C1_INFO,
  EP_STATE_PROBE_SEC2_C0_INFO,
  EP_STATE_PROBE_SEC0_INFO,
	EP_STATE_PROBE_ZWAVE_PLUS,
	EP_STATE_MDNS_PROBE,
	EP_STATE_MDNS_PROBE_IN_PROGRESS,
	EP_STATE_PROBE_DONE,
	EP_STATE_PROBE_FAIL
} rd_ep_state_t;

typedef struct rd_node_database_entry {

  uint32_t wakeUp_interval;
  uint32_t lastAwake;
  uint32_t lastUpdate;

  uip_ip6addr_t ipv6_address;

  nodeid_t nodeid;
  uint8_t security_flags;
  /*uint32_t homeID;*/

  rd_node_mode_t mode;
  rd_node_state_t state;

  uint16_t manufacturerID;
  uint16_t productType;
  uint16_t productID;

  uint8_t nodeType; // Is this a controller, routing slave ... etc

  uint8_t refCnt;
  uint8_t nEndpoints; //Total number of endpointss
  uint8_t nAggEndpoints; //Number of aggregated endpoints

  LIST_STRUCT(endpoints);

  uint8_t nodeNameLen;
  char* nodename;
} rd_node_database_entry_t;


typedef struct rd_ep_data_store_entry {
  uint8_t endpoint_info_len;
  uint8_t endpoint_name_len;
  uint8_t endpoint_loc_len;
  uint8_t endpoint_aggr_len;
  uint8_t endpoint_id;
  rd_ep_state_t state;
  uint16_t iconID;
} rd_ep_data_store_entry_t;


typedef struct rd_ep_database_entry {
  struct rd_ep_database_entry* list;
  rd_node_database_entry_t* node;
  char *endpoint_location;
  char *endpoint_name;
  uint8_t *endpoint_info;
  uint8_t *endpoint_agg;

  uint8_t endpoint_info_len;
  uint8_t endpoint_name_len;
  uint8_t endpoint_loc_len;
  uint8_t endpoint_aggr_len; //Length of aggregations
  uint8_t endpoint_id;
  rd_ep_state_t state;
  uint16_t installer_iconID;
  uint16_t user_iconID;
} rd_ep_database_entry_t;



typedef union ip4addr_t {
  u8_t  u8[4];          /* Initializer, must come first!!! */
  u16_t u16[2];
} ipv4addr_t;

typedef ipv4addr_t uip_ipv4addr_t;

typedef struct rd_group_entry {
  char name[64];
  list_t* endpoints;
} rd_group_entry_t;
#define MODE_FLAGS_DELETED 0x0100
//#define assert(e) ((void)0)
#define RD_ALL_NODES 0
#define SUPPORTED  0x5
#define CONTROLLED 0xA
#define random_rand() 0
#define UIP_CONF_IPV6 1

#include <stdio.h>

u8_t rd_get_ep_name(rd_ep_database_entry_t* ep,char* buf,u8_t size);
rd_ep_database_entry_t* rd_ep_first(nodeid_t node);
rd_ep_database_entry_t* rd_ep_next(nodeid_t node, rd_ep_database_entry_t* ep);
rd_node_database_entry_t* rd_lookup_by_node_name(const char* name);
u8_t rd_get_node_name(rd_node_database_entry_t* n, char* buf, u8_t size);
void ipOfNode(uip_ip6addr_t* dst, nodeid_t nodeID);
rd_group_entry_t* rd_lookup_group_by_name(const char* name);
rd_ep_database_entry_t* rd_ep_iterator_group_begin(rd_group_entry_t* ge);
int rd_ep_class_support(rd_ep_database_entry_t* ep, uint16_t cls);
rd_ep_database_entry_t* rd_group_ep_iterator_next(rd_group_entry_t* ge, rd_ep_database_entry_t* ep);
u8_t ipv46nat_ipv4addr_of_node(uip_ipv4addr_t* ip, nodeid_t node);
int test_uip_datalen();

uint8_t sec2_gw_node_flags2keystore_flags(uint8_t gw_flags);
uint8_t GetCacheEntryFlag(nodeid_t nodeid);
void uip_debug_ipaddr_print(const uip_ipaddr_t *addr);
void mdns_hexdump(uint8_t* buf, int size);
