/* © 2019 Silicon Laboratories Inc.  */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>


/*------------------------------------------------------*/
/*      Taken from RD_types and RD_internal             */
/*------------------------------------------------------*/

#define COMMAND_CLASS_VERSION                                                            0x86
#define COMMAND_CLASS_ZWAVEPLUS_INFO                                                     0x5E /*SDS11907-3*/
#define COMMAND_CLASS_MANUFACTURER_SPECIFIC                                              0x72
#define COMMAND_CLASS_WAKE_UP                                                            0x84
#define COMMAND_CLASS_MULTI_CHANNEL_V4                                                   0x60
#define COMMAND_CLASS_ASSOCIATION                                                        0x85
#define COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3                                       0x8E

/** Struct to form the list of controlled CC, including CC and its latest version */
typedef struct cc_version_pair {
   /** The command class identifier. */
   uint16_t command_class;
   /** The version supported by the node. */
   uint8_t version;
} cc_version_pair_t;

/*------------------------------------------------------*/
/*      Taken from Resource Directory                   */
/*------------------------------------------------------*/

typedef union uip_ip6addr_t {
  uint8_t  u8[16];         /* Initializer, must come first!!! */
  uint16_t u16[8];
} uip_ip6addr_t ;

#define ZW_MAX_NODES          232
#define RD_SMALLOC_SIZE    0x5E00

#define PREALLOCATED_VIRTUAL_NODE_COUNT 4
#define MAX_IP_ASSOCIATIONS (200 + PREALLOCATED_VIRTUAL_NODE_COUNT)
#define ENDIAN(hex) (((hex & 0x00ff) << 8) | ((hex & 0xff00) >> 8))

/** Obsoleted eeprom.dat magic number.  This magic indicates that the
 * hdr struct is unversioned.  The gateway will assume v2.61 format
 * and try to convert the data. */
#define RD_MAGIC_V0 0x491F00E5

/** The eeprom.dat magic number.  This magic indicates that the hdr struct
 * includes a versioning field. */
#define RD_MAGIC_V1 0x94F100E5

typedef uint8_t nodeid_t;
enum ASSOC_TYPE {temporary_assoc, permanent_assoc, local_assoc, proxy_assoc};

typedef enum {
  /* Initial state for probing version. */
  PCV_IDLE,
  /* GW is sending VERSION_COMMAND_CLASS_GET and wait for cc_version_callback to store the version. */
  PCV_SEND_VERSION_CC_GET,
  /* Check if this is the last command class we have to ask. */
  PCV_LAST_REPORT,
  /* Basic CC version probing is done. Check if this is a Version V3 node. */
  PCV_CHECK_IF_V3,
  /* It's a V3 node. GW is sending VERSION_CAPABILITIES_GET and wait for
   * version_capabilities_callback. */
  PCV_SEND_VERSION_CAP_GET,
  /* The node supports ZWS. GW is sending VERSION_ZWAVE_SOFTWARE_GET and wait
   * for version_zwave_software_callback. */
  PCV_SEND_VERSION_ZWS_GET,
  /* Full version probing is done. Finalize/Clean up the probing. */
  PCV_VERSION_PROBE_DONE,
} pcv_state_t;

typedef void (*_pcvs_callback)(void *user, uint8_t status_code);

typedef struct ProbeCCVersionState {
  /* The index for controlled_cc, mainly for looping over the CC */
  uint8_t probe_cc_idx;
  /* Global probing state */
  pcv_state_t state;
  /* The callback will be lost after sending asynchronous ZW_SendRequest. Keep the callback here for going back */
  _pcvs_callback callback;
} ProbeCCVersionState_t;

struct IP_association {
  void *next;
  nodeid_t virtual_id;
  enum ASSOC_TYPE type; /* unsolicited or association */
  uip_ip6addr_t resource_ip; /*Association Destination IP */
  uint8_t resource_endpoint;  /* From the IP_Association command. Association destination endpoint */
  uint16_t resource_port;
  uint8_t virtual_endpoint;   /* From the ZIP_Command command */
  uint8_t grouping;
  uint8_t han_nodeid; /* Association Source node ID*/
  uint8_t han_endpoint; /* Association Source endpoint*/
  uint8_t was_dtls;
  uint8_t mark_removal;
}; // __attribute__((packed));   /* Packed because we copy byte-for-byte from mem to eeprom */

#define ASSOCIATION_TABLE_EEPROM_SIZE (sizeof(uint16_t) + MAX_IP_ASSOCIATIONS * sizeof(struct IP_association))

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
    EP_STATE_PROBE_INFO,
    EP_STATE_PROBE_SEC_INFO,
    EP_STATE_PROBE_ZWAVE_PLUS,
    EP_STATE_MDNS_PROBE,
    EP_STATE_MDNS_PROBE_IN_PROGRESS,
    EP_STATE_PROBE_DONE,
    EP_STATE_PROBE_FAIL
} rd_ep_state_t;

typedef void ** list_t;

#define LIST_CONCAT2(s1, s2) s1##s2
#define LIST_CONCAT(s1, s2) LIST_CONCAT2(s1, s2)
#define LIST_STRUCT(name) \
         void *LIST_CONCAT(name,_list); \
         list_t name

/**********************************/
/*          Ancient format        */
/**********************************/
typedef struct rd_ep_data_store_entry_ancient {
  uint8_t endpoint_info_len;
  uint8_t endpoint_name_len;
  uint8_t endpoint_loc_len;
  uint8_t endpoint_id;
  rd_ep_state_t state;
  uint16_t iconID;
} rd_ep_data_store_entry_ancient_t;

typedef struct rd_eeprom_static_hdr_ancient {
  uint32_t magic;
  uint32_t homeID;
  uint8_t  nodeID;
  uint32_t flags;
  uint16_t node_ptrs[ZW_MAX_NODES];
  uint8_t  smalloc_space[RD_SMALLOC_SIZE];
  uint8_t temp_assoc_virtual_nodeid_count;
  nodeid_t temp_assoc_virtual_nodeids[PREALLOCATED_VIRTUAL_NODE_COUNT];
  uint16_t association_table_length;
  uint8_t association_table[ASSOCIATION_TABLE_EEPROM_SIZE];
} rd_eeprom_static_hdr_ancient_t;

typedef struct rd_node_database_entry_ancient {

  uint32_t wakeUp_interval;
  uint32_t lastAwake;
  uint32_t lastUpdate;

  uip_ip6addr_t ipv6_address;

  uint8_t nodeid;
  uint8_t security_flags;
  /*uint32_t homeID;*/

  rd_node_mode_t mode;
  rd_node_state_t state;

  uint16_t manufacturerID;
  uint16_t productType;
  uint16_t productID;

  uint8_t nodeType; // Is this a controller, routing slave ... etc

  uint8_t refCnt;
  uint8_t nEndpoints;

  LIST_STRUCT(endpoints);

  uint8_t nodeNameLen;
  char* nodename;
} rd_node_database_entry_ancient_t;

/**********************************/
/*           v0 format          */
/**********************************/
typedef struct rd_ep_data_store_entry_v0 {
  uint8_t endpoint_info_len;
  uint8_t endpoint_name_len;
  uint8_t endpoint_loc_len;
  uint8_t endpoint_aggr_len;
  uint8_t endpoint_id;
  rd_ep_state_t state;
  uint16_t iconID;
} rd_ep_data_store_entry_v0_t;
typedef rd_eeprom_static_hdr_ancient_t rd_eeprom_static_hdr_v0_t;

typedef struct rd_node_database_entry_v0 {

  uint32_t wakeUp_interval;
  uint32_t lastAwake;
  uint32_t lastUpdate;

  uip_ip6addr_t ipv6_address;

  uint8_t nodeid;
  uint8_t security_flags;
  /*uint32_t homeID;*/

  rd_node_mode_t mode;
  rd_node_state_t state;

  uint16_t manufacturerID;
  uint16_t productType;
  uint16_t productID;

  uint8_t nodeType; // Is this a controller, routing slave ... etc

  uint8_t refCnt;
  uint8_t nEndpoints;
  uint8_t nAggEndpoints;

  LIST_STRUCT(endpoints);

  uint8_t nodeNameLen;
  char* nodename;
} rd_node_database_entry_v0_t;

/**********************************/
/*           v2.0 format          */
/**********************************/
typedef rd_ep_data_store_entry_v0_t rd_ep_data_store_entry_v20_t;

typedef struct rd_eeprom_static_hdr_v20 {
  uint32_t magic;
   /** Home ID of the stored gateway. */
  uint32_t homeID;
   /** Node ID of the stored gateway. */
  uint8_t  nodeID;
  uint8_t version_major;
  uint8_t version_minor;
  uint32_t flags;
  uint16_t node_ptrs[ZW_MAX_NODES];
  /** The area used for dynamic allocations with the \ref small_mem. */
  uint8_t  smalloc_space[RD_SMALLOC_SIZE];
  uint8_t temp_assoc_virtual_nodeid_count;
  nodeid_t temp_assoc_virtual_nodeids[PREALLOCATED_VIRTUAL_NODE_COUNT];
  uint16_t association_table_length;
  uint8_t association_table[ASSOCIATION_TABLE_EEPROM_SIZE];
} rd_eeprom_static_hdr_v20_t;

typedef struct rd_node_database_entry_v20 {

  uint32_t wakeUp_interval;
  uint32_t lastAwake;
  uint32_t lastUpdate;

  uip_ip6addr_t ipv6_address;

  uint8_t nodeid;
  uint8_t security_flags;
  /*uint32_t homeID;*/

  rd_node_mode_t mode;
  rd_node_state_t state;

  uint16_t manufacturerID;
  uint16_t productType;
  uint16_t productID;

  uint8_t nodeType; // Is this a controller, routing slave ... etc

  uint8_t refCnt;
  uint8_t nEndpoints;
  uint8_t nAggEndpoints;

  LIST_STRUCT(endpoints);

  uint8_t nodeNameLen;
  uint8_t dskLen;
  char* nodename;
  uint8_t *dsk;

} rd_node_database_entry_v20_t;

/**********************************/
/*           v2.1 format          */
/**********************************/
typedef rd_ep_data_store_entry_v0_t rd_ep_data_store_entry_v21_t;

typedef rd_eeprom_static_hdr_v20_t rd_eeprom_static_hdr_v21_t;

typedef struct rd_node_database_entry_v21 {

  uint32_t wakeUp_interval;
  uint32_t lastAwake;
  uint32_t lastUpdate;

  uip_ip6addr_t ipv6_address;

  uint8_t nodeid;
  uint8_t security_flags;
  /*uint32_t homeID;*/

  rd_node_mode_t mode;
  rd_node_state_t state;

  uint16_t manufacturerID;
  uint16_t productType;
  uint16_t productID;

  uint8_t nodeType; // Is this a controller, routing slave ... etc

  uint8_t refCnt;
  uint8_t nEndpoints;
  uint8_t nAggEndpoints;

  LIST_STRUCT(endpoints);

  uint8_t nodeNameLen;
  uint8_t dskLen;
  uint8_t node_version_cap_and_zwave_sw;
  uint16_t probe_flags;
  uint16_t node_properties_flags;
  uint8_t node_cc_versions_len;
  uint8_t node_is_zws_probed;
  char* nodename;
  uint8_t *dsk;
  cc_version_pair_t *node_cc_versions;
  ProbeCCVersionState_t *pcvs;

} rd_node_database_entry_v21_t;

/**********************************/
/*           v2.2 format          */
/**********************************/
typedef rd_ep_data_store_entry_v0_t rd_ep_data_store_entry_v22_t;

typedef rd_eeprom_static_hdr_v20_t rd_eeprom_static_hdr_v22_t;

typedef rd_node_database_entry_v20_t rd_node_database_entry_v22_t;

/**********************************/
/*           v2.3 format          */
/**********************************/
typedef rd_ep_data_store_entry_v0_t rd_ep_data_store_entry_v23_t;

typedef rd_eeprom_static_hdr_v20_t rd_eeprom_static_hdr_v23_t;

typedef rd_node_database_entry_v21_t rd_node_database_entry_v23_t;
