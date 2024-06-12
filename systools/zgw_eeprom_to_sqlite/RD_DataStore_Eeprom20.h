#ifndef RD_DATASTORE_EEPROM20_H
#define RD_DATASTORE_EEPROM20_H

#include <stdint.h>
#include <stdio.h>

#include "RD_types.h"
#include "RD_internal.h"
#include "RD_DataStore_Eeprom.h"
#include "DataStore.h"
#include "net/uip.h"
#include "sys/clock.h"

#ifdef __cplusplus
extern "C"
{
#endif

/// Default value for verification
extern const cc_version_pair_t controlled_cc_v[];

/**********************************/
/*       EEPROM static header     */
/**********************************/
typedef rd_eeprom_static_hdr_t rd_eeprom_static_hdr_v20_t;
typedef rd_eeprom_static_hdr_t rd_eeprom_static_hdr_v23_t;

/**********************************/
/*           Node struct          */
/**********************************/

/* V2.0 node struct can be found in RD_internal.h */

typedef struct rd_node_database_entry_v23 {
  uint32_t wakeUp_interval;
  uint32_t lastAwake;
  uint32_t lastUpdate;

  uip_ip6addr_t ipv6_address;

  uint8_t nodeid;
  uint8_t security_flags;

  rd_node_mode_t mode;
  rd_node_state_t state;

  uint16_t manufacturerID;
  uint16_t productType;
  uint16_t productID;

  uint8_t nodeType;

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
} rd_node_database_entry_v23_t;

/**********************************/
/*         Endpoint struct        */
/**********************************/
typedef rd_ep_data_store_entry_t rd_ep_data_store_entry_v20_t;
typedef rd_ep_data_store_entry_t rd_ep_data_store_entry_v23_t;

typedef struct rd_ep_database_entry_v20 {
   /** Contiki list management. */
  struct rd_ep_database_entry_v20* list;
   /** Pointer to the node this endpoint belongs to. */
  rd_node_database_entry_v20_t* node;
   /** mDNS field. */
  char *endpoint_location;
   /** mDNS field. */
  char *endpoint_name;
   /** Command classes supported by endpoint determined at last probing. */
  uint8_t *endpoint_info;
   /** Link to aggregation info */
  uint8_t *endpoint_agg;

   /** Length of #endpoint_info. */
  uint8_t endpoint_info_len;
   /** Length of #endpoint_name. */
  uint8_t endpoint_name_len;
   /** Length of #endpoint_location. */
  uint8_t endpoint_loc_len;
   /** Length of aggregations */
  uint8_t endpoint_aggr_len;
   /** Endpoint identifier. */
  uint8_t endpoint_id;
   /** Endpoint probing state. */
  rd_ep_state_t state;
   /** Z-Wave plus icon ID. */
  uint16_t installer_iconID;
   /** Z-Wave plus icon ID. */
  uint16_t user_iconID;
} rd_ep_database_entry_v20_t;

typedef rd_ep_database_entry_t rd_ep_database_entry_v23_t;

rd_node_database_entry_v20_t *rd_data_store_read_v20(uint8_t nodeID);
void rd_data_store_mem_free_v20(rd_node_database_entry_v20_t *n);
int rd_data_store_entry_compare_v20_v23(const rd_node_database_entry_v20_t *v20, const rd_node_database_entry_v23_t *v23);

#ifdef __cplusplus
}
#endif

#endif
