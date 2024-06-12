/* © 2019 Silicon Laboratories Inc.  */

#ifndef RD_BASIC_H
#define RD_BASIC_H

#include "lib/list.h" /* Contiki lists */
#include "net/uip.h" /* Contiki uip_ip6addr_t */
#include "RD_types.h"

/** Default wake-up interval
 *
 * \ingroup ZIP_Resource
 */
#define DEFAULT_WAKE_UP_INTERVAL (70 * 60)

/** Get the mDNS mode of a node.
 *
 * \ingroup node_db
 *
 * The rd_node_database_entry_t::mode field is used to store both the
 * actual mode and some status flags.  This returns the mode without
 * flags.
 *
 * \param n A node database entry pointer.
 * \return The node's mode of type \ref rd_node_mode_t. */
#define RD_NODE_MODE_VALUE_GET(n) (n->mode & 0xFF)

/**
 * Possible values for the DNS mode field.
 * \ingroup ZIP_Resource
 */
typedef enum {
   /** Initial value.  Should not be put on the wire, as HAN interview
    * should update with the correct value before mDNS probing. */
  MODE_PROBING,
   /** Node supports neither listening nor wake-up. */
  MODE_NONLISTENING,
   /** Node has capability \a NODEINFO_LISTENING_SUPPORT. */
  MODE_ALWAYSLISTENING,
   /** Node supports wake-up beam. */
  MODE_FREQUENTLYLISTENING,
   /** Node is a wakeup node. */
  MODE_MAILBOX,
   /** Node is a wakeup node. but its having a firmware upgrade to should
    * not be put to sleep or frames to it should not go to mailbox. */
  MODE_FIRMWARE_UPGRADE,
  /** Node does not exist */
  MODE_NODE_UNDEF = 0xFF,
} rd_node_mode_t;

/**
 * Probe/interview status for a node.
 * \ingroup ZIP_Resource
 */
typedef enum {
   /** Initial state   */
  STATUS_CREATED,
   /** Waiting for NIF   */
  STATUS_PROBE_NODE_INFO,
   /** If node supports Manufacturer Specific, ask for it.  On reply,
    * go to #STATUS_ENUMERATE_ENDPOINTS */
  STATUS_PROBE_PRODUCT_ID,
   /** If node supports multichannel, ask how many endpoints.  On
    * reply, go to #STATUS_FIND_ENDPOINTS */
  STATUS_ENUMERATE_ENDPOINTS,
   /** If node supports multichannel, ask what are the endpoint ids.  On
    * reply, go to #STATUS_CHECK_WU_CC_VERSION */
  STATUS_FIND_ENDPOINTS,
   /** If node supports WakeUp and gateway supports mailbox, ask for
    * WakeUp version, on reply, if version >= 2, go to
    * #STATUS_GET_WU_CAP, if version == 1, got to
    * #STATUS_SET_WAKE_UP_INTERVAL.  If not supported, go to
    * #STATUS_PROBE_ENDPOINTS.  */
  STATUS_CHECK_WU_CC_VERSION,
   /** Ask for WakeUp interval capabilities.  On reply, cache values
    * and go to #STATUS_SET_WAKE_UP_INTERVAL.  */
  STATUS_GET_WU_CAP,
   /** Set WakeUp interval (v2).  When sent, go to
    * #STATUS_ASSIGN_RETURN_ROUTE. */
  STATUS_SET_WAKE_UP_INTERVAL,
   /** Assign Return Route.  In callback, go to
    * #STATUS_PROBE_WAKE_UP_INTERVAL.  */
  STATUS_ASSIGN_RETURN_ROUTE,
   /** Request WakeUp interval report.  On successful reply, go to
    * STATUS_PROBE_ENDPOINTS.  */
  STATUS_PROBE_WAKE_UP_INTERVAL,
   /** For each endpoint, call \ref rd_ep_probe_update() until enpoint
    * status is either #EP_STATE_PROBE_FAIL or #EP_STATE_PROBE_DONE.
    * Then go to #STATUS_MDNS_PROBE.  */
  STATUS_PROBE_ENDPOINTS,
   /** Call \ref mdns_node_name_probe() with callback
    * #rd_node_name_probe_done().  In callback, go to
    * #STATUS_MDNS_EP_PROBE.  */
  STATUS_MDNS_PROBE,
   /** For each endpoint, if endpoint state is #EP_STATE_MDNS_PROBE,
    * call \ref rd_ep_probe_update().  */
  STATUS_MDNS_EP_PROBE,
   /** Final state   */
  STATUS_DONE,
   /** Final state   */
  STATUS_PROBE_FAIL,
   /** Final state.  With respect to probing, sub-state of STATUS_DONE.  */
  STATUS_FAILING,
} rd_node_state_t;

/**
 * \defgroup node_db Resource Directory Node Database
 * \ingroup ZIP_Resource
 *
 * The Node Database is the component of the \ref ZIP_Resource
 * handling the storage and retrieval of data associated with nodes
 * and their endpoints.
 *
 */

/* Flags for node properties */

/* These two flags are obsoleted but needed for conversion */
#define OBSOLETED_NODE_FLAG_JUST_ADDED    0x04
#define OBSOLETED_NODE_FLAG_ADDED_BY_ME   0x80

/** This node is a portable slave */
#define RD_NODE_FLAG_PORTABLE 0x01
/** Node probe has not completed since node was added. */
#define RD_NODE_FLAG_JUST_ADDED  0x02
/** Whether the node was (security-)added by this GW.
 *
 * This flag must not be set if the node was added by another
 * controller.  Only relevant if RD_NODE_FLAG_JUST_ADDED is also set.
 *
 * If this flag is set, GW already knows
 * the security keys of the node and security should not be
 * down-graded by the probe process. */
#define RD_NODE_FLAG_ADDED_BY_ME 0x04


/* Flags for node interview properties. */
/** This node has not been probed since the gateway process was
    launched (TODO: persist version probe and change 0 to mean
    never probed by this gateway or introduce a real flag.) */
#define RD_NODE_PROBE_NEVER_STARTED 0x00
/** The gateway has started to probe this node and has received at
    least one reply.  I.e., the gateway has at least a NIF for the
    node. */
#define RD_NODE_FLAG_PROBE_STARTED 0x01
/*
 * The gateway has probed the node and failed some of the probing (e.g. version)
 * TODO: Make probing to adopt this flag. Now only version probing adopts it.
 */
#define RD_NODE_FLAG_PROBE_FAILED 0x02
/** The probe of this node has completed at least once. */
#define RD_NODE_FLAG_PROBE_HAS_COMPLETED 0x03

/** Node information.
 *
 * \ingroup node_db
 *
 * Cached information about a node in the PAN network.
 */
typedef struct rd_node_database_entry {

   /** Node's wake up interval, as determined during RD probe or
    * mailbox probe. */
  uint32_t wakeUp_interval;
    /** Timestamp of the last time the gateway heard from the node.
     * Faked during restart. */
  uint32_t lastAwake;
   /** Timestamp of the last time the gateway successfully probed node. */
  uint32_t lastUpdate;

   /** This field is no longer used. */
   uint8_t not_used[16];

   /** Node id on the PAN network. */
  nodeid_t nodeid;
   /** Which security has been negotiated (or probed) for this node. */
  uint8_t security_flags;
  /*uint32_t homeID;*/
   /** Combined field containing the node status flags and the DNS
    * mode field */
  rd_node_mode_t mode;
   /** Probe/interview state for the node. */
  rd_node_state_t state;

  uint16_t manufacturerID;
  uint16_t productType;
  uint16_t productID;

  uint8_t nodeType;  /**< Is this a controller, routing slave ..., etc. */
   /** Reference counter tracking outstanding pointers to this entry. */
  uint8_t refCnt;
  uint8_t nEndpoints;  /**< Total number of endpoints, including root
                          device and aggregated eps. */
  uint8_t nAggEndpoints;  /**< Number of aggregated endpoints. */

  LIST_STRUCT(endpoints);

   /** Length of #nodename. */
  uint8_t nodeNameLen;
  uint8_t dskLen; /**< Currently 16 or 0. */
  /** The version capabilities report and, if the node supports it,
      the ZWave software report. Length is not needed for this field,
      since the first byte indicates the length. */
  uint8_t node_version_cap_and_zwave_sw;
  /** Probe status flags covering all probes of this node, not just
   * the current state.  (Current state is stored in \ref state).  */
  /* TODO: would an enum be better here? */
  uint16_t probe_flags;
  /** Node properties and capabilities that can conveniently be stored as flags.  E.g. NODE_PORTABLE. */
  /* TODO: store properties such as RD_NODE_FLAG_ADDED_BY_ME and
     RD_NODE_FLAG_JUST_ADDED here instead of overloading the security
     flags. */
  uint16_t node_properties_flags;

  uint8_t node_cc_versions_len;

  /* Could be removed when we store zws report */
  uint8_t node_is_zws_probed;
   /** mDNS name of the node or NULL. */
  char* nodename;
   /** Key to connect with the corresponding \ref provision in the \ref pvslist. */
  uint8_t *dsk;

  /*
   * Versions of the controlled command class that have multiple version,
   * as determined at last probing. The value 0 means the version has not
   * been determined, either because it is not needed, or because no reply
   * has been received yet. The version stored is the version reported by
   * the node, even if the gateway only controls a lower version.
   */
  /* CC versions cache for this node */
  cc_version_pair_t *node_cc_versions;

  /* This is not persisted in eeprom */
  ProbeCCVersionState_t *pcvs;

} rd_node_database_entry_t;

typedef struct rd_node_database_entry_legacy {

   /** Node's wake up interval, as determined during RD probe or
    * mailbox probe. */
  uint32_t wakeUp_interval;
    /** Timestamp of the last time the gateway heard from the node.
     * Faked during restart. */
  uint32_t lastAwake;
   /** Timestamp of the last time the gateway successfully probed node. */
  uint32_t lastUpdate;

   /** This field is no longer used. */
   uint8_t not_used[16];

   /** Node id on the PAN network. */
  uint8_t nodeid;
   /** Which security has been negotiated (or probed) for this node. */
  uint8_t security_flags;
  /*uint32_t homeID;*/
   /** Combined field containing the node status flags and the DNS
    * mode field */
  rd_node_mode_t mode;
   /** Probe/interview state for the node. */
  rd_node_state_t state;

  uint16_t manufacturerID;
  uint16_t productType;
  uint16_t productID;

  uint8_t nodeType;  /**< Is this a controller, routing slave ..., etc. */
   /** Reference counter tracking outstanding pointers to this entry. */
  uint8_t refCnt;
  uint8_t nEndpoints;  /**< Total number of endpoints, including root
                          device and aggregated eps. */
  uint8_t nAggEndpoints;  /**< Number of aggregated endpoints. */

  LIST_STRUCT(endpoints);

   /** Length of #nodename. */
  uint8_t nodeNameLen;
  uint8_t dskLen; /**< Currently 16 or 0. */
  /** The version capabilities report and, if the node supports it,
      the ZWave software report. Length is not needed for this field,
      since the first byte indicates the length. */
  uint8_t node_version_cap_and_zwave_sw;
  /** Probe status flags covering all probes of this node, not just
   * the current state.  (Current state is stored in \ref state).  */
  /* TODO: would an enum be better here? */
  uint16_t probe_flags;
  /** Node properties and capabilities that can conveniently be stored as flags.  E.g. NODE_PORTABLE. */
  /* TODO: store properties such as RD_NODE_FLAG_ADDED_BY_ME and
     RD_NODE_FLAG_JUST_ADDED here instead of overloading the security
     flags. */
  uint16_t node_properties_flags;

  uint8_t node_cc_versions_len;

  /* Could be removed when we store zws report */
  uint8_t node_is_zws_probed;
   /** mDNS name of the node or NULL. */
  char* nodename;
   /** Key to connect with the corresponding \ref provision in the \ref pvslist. */
  uint8_t *dsk;

  /*
   * Versions of the controlled command class that have multiple version,
   * as determined at last probing. The value 0 means the version has not
   * been determined, either because it is not needed, or because no reply
   * has been received yet. The version stored is the version reported by
   * the node, even if the gateway only controls a lower version.
   */
  /* CC versions cache for this node */
  cc_version_pair_t *node_cc_versions;

  /* This is not persisted in eeprom */
  ProbeCCVersionState_t *pcvs;

} rd_node_database_entry_legacy_t;

typedef struct rd_node_database_entry_v20 {

   /** Node's wake up interval, as determined during RD probe or
    * mailbox probe. */
  uint32_t wakeUp_interval;
    /** Timestamp of the last time the gateway heard from the node.
     * Faked during restart. */
  uint32_t lastAwake;
   /** Timestamp of the last time the gateway successfully probed node. */
  uint32_t lastUpdate;

   /** IPv6 address of node. */
  uip_ip6addr_t ipv6_address;

   /** Node id on the PAN network. */
  uint8_t nodeid;
   /** Which security has been negotiated (or probed) for this node. */
  uint8_t security_flags;
  /*uint32_t homeID;*/
   /** Combined field containing the node status flags and the DNS
    * mode field */
  rd_node_mode_t mode;
   /** Probe/interview state for the node. */
  rd_node_state_t state;

  uint16_t manufacturerID;
  uint16_t productType;
  uint16_t productID;

  uint8_t nodeType;  /**< Is this a controller, routing slave ..., etc. */
   /** Reference counter tracking outstanding pointers to this entry. */
  uint8_t refCnt;
  uint8_t nEndpoints;  /**< Total number of endpoints. */
  uint8_t nAggEndpoints;  /**< Number of aggregated endpoints. */

  LIST_STRUCT(endpoints);

   /** Length of #nodename. */
  uint8_t nodeNameLen;
  uint8_t dskLen; /**< Currently 16 or 0. */
  char* nodename;
   /** Key to connect with the corresponding \ref provision in the \ref pvslist. */
  uint8_t *dsk;

} rd_node_database_entry_v20_t;

typedef rd_node_database_entry_v20_t rd_node_database_entry_v22_t;

/**
 * Probe/interview status for an endpoint.
 *
 * \ingroup ZIP_Resource
 *
 * Endpoint 0 represents the node itself.
 */
typedef enum {
  EP_STATE_PROBE_AGGREGATED_ENDPOINTS,
  EP_STATE_PROBE_INFO, /**< Initial state */
  EP_STATE_PROBE_SEC2_C2_INFO,
  EP_STATE_PROBE_SEC2_C1_INFO, /**< Skipped when probing proper endpoints (>0). */
  EP_STATE_PROBE_SEC2_C0_INFO, /**< Skipped when probing proper endpoints (>0). */
  EP_STATE_PROBE_SEC0_INFO,
  EP_STATE_PROBE_ZWAVE_PLUS,
  EP_STATE_MDNS_PROBE,
  EP_STATE_MDNS_PROBE_IN_PROGRESS,
  EP_STATE_PROBE_DONE,
  EP_STATE_PROBE_FAIL,
  /* Added as of Version V3. Probe the version for command classes that are
   * controlled by GW. This version probing is sent toward root device and
   * each endpoint.This is added in the bottom of the list to avoid messing
   * around the state order.
   */
  EP_STATE_PROBE_VERSION,
} rd_ep_state_t;


/**
 * Endpoint information stored in persistent storage.
 * \ingroup rd_data_store
 */
typedef struct rd_ep_data_store_entry {
  uint8_t endpoint_info_len;
  uint8_t endpoint_name_len;
  uint8_t endpoint_loc_len;
  uint8_t endpoint_aggr_len;
  uint8_t endpoint_id;
  rd_ep_state_t state;
  uint16_t iconID;
} rd_ep_data_store_entry_t;

/**
 * Endpoint information
 *
 * \ingroup node_db
 *
 * Cached information about an endpoint of a node in the PAN network.
 *
 * Endpoint 0 represents the node itself.
 */
typedef struct rd_ep_database_entry {
   /** Contiki list management. */
  struct rd_ep_database_entry* list;
   /** Pointer to the node this endpoint belongs to. */
  rd_node_database_entry_t* node;
   /** mDNS field. */
  char *endpoint_location;
   /** mDNS field. */
  char *endpoint_name;

   /** Command classes supported by endpoint, as determined at last
       probing.  Used in \ref rd_ep_class_support() to determine if a
       node/ep supports a given CC. */
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
} rd_ep_database_entry_t;

/** Allocate a node entry in the \ref node_db.
 *
 * \ingroup node_db
 *
 * This function allocates memory for the node entry in the \ref
 * node_db and initializes it, but it does not persist the node to
 * \ref rd_data_store.
 *
 * \param nodeid The id of the node.
 * \return Pointer to the new node entry.
 */
rd_node_database_entry_t* rd_node_entry_alloc(nodeid_t nodeid);

/** Import stored data for node \p nodeid from \ref rd_data_store into \ref node_db.
 *
 * \ingroup node_db
 *
 * \return Pointer to the newly allocated node entry, or NULL if it
 * was not found in \ref rd_data_store.
 */
rd_node_database_entry_t* rd_node_entry_import(nodeid_t nodeid);

/**
 * Free the dynamic data of node \p nodeid in both \ref node_db and \ref rd_data_store.
 */
void rd_node_entry_free(nodeid_t nodeid);

rd_node_database_entry_t* rd_node_get_raw(nodeid_t nodeid);

/** Set the DSK of a node.
 *
 * \ingroup node_db
 *
 * The node must support S2 or Smart Start to have a DSK.  The DSK
 * should only be added here if it has been authenticated in S2.
 *
 * If memory allocation fails, a warning is printed and 0 length is stored.
 *
 * If there is already another nodeid with the same DSK, it must be
 * the same device. (Maybe it was, eg, factory reset.)  So the dsk is
 * removed from the old device to avoid confusion.
 *
 * @param node The node id.  Should not be MyNodeId or a virtual node.
 * @param dsklen Length of the dsk.
 * @param dsk Pointer to the memory we should copy the dsk from.
 */
void rd_node_add_dsk(nodeid_t node, uint8_t dsklen, const uint8_t *dsk);

/**
 * Finally free memory allocated by RD
 *
 * \ingroup node_db
 *
 */
void rd_destroy(void);

/**
 * function to get the node name, use the auto generated name if no name is set
 * \param n Given node entry
 * \param buf buffer to write into
 * \param size maximum number of bytes to copy
 */
u8_t rd_get_node_name(rd_node_database_entry_t* n,char* buf,u8_t size);

/** Get the mDNS mode of a node from the nodeid.
 *
 * \ingroup ZIP_Resource
 *
 * The rd_node_database_entry_t::mode field is used to store both the
 * actual mode and some status flags.  This returns the mode without
 * flags.
 *
 * If the node does not exist, return \ref MODE_NODE_UNDEF.
 *
 * \param n A node id.
 * \return The node's mode of type \ref rd_node_mode_t. */
rd_node_mode_t rd_node_mode_value_get(nodeid_t n);

/**
 * \ingroup ZIP_Resource
 *
 * Find node by node name
 */
rd_node_database_entry_t* rd_lookup_by_node_name(const char* name);

/** Find the name of the endpoint.
 *
 * This function always generates a name for an endpoint using the following rules:
 * - If the name has been set, use that.
 * - If there is no name, but there is info, generate a name from the info.
 * - If there is no name and no info, generate a generic name from
 *   the homeid, node id, and ep id.
 * \param ep Pointer to the endpoint.
 * \param buf Pointer to a buffer with size at least \p size.
 * \param size Size of \p buf.
 */
u8_t rd_get_ep_name(rd_ep_database_entry_t* ep, char* buf, u8_t size);
u8_t rd_get_ep_location(rd_ep_database_entry_t* ep,char* buf,u8_t size);
/**
 * Find a node entry from the node's DSK.
 *
 * \ingroup node_db
 */
rd_node_database_entry_t* rd_lookup_by_dsk(uint8_t dsklen, const uint8_t* dsk);

/**
 * Allocate a pointer to the node's database entry in the data store
 * from the node id.
 *
 * \ingroup node_db
 *
 * \note #rd_free_node_dbe(), MUST
 * be called when the node entry is no longer needed.
 *
 * \param nodeid The node id.
 * \return A reference counted pointer to the node entry in \ref node_db
 */
rd_node_database_entry_t* rd_get_node_dbe(nodeid_t nodeid);

/** Node CC version helper function - Get
 *
 * \param n The requested node for version
 * \param command_class The requested command class for version
 */
uint8_t rd_node_cc_version_get(rd_node_database_entry_t *n, uint16_t command_class);

/** Node CC version helper function - Set
 *
 * \ingroup node_db
 *
 * \param n The node to set the version
 * \param command_class The set command class
 * \param version The version to set
 */
void rd_node_cc_version_set(rd_node_database_entry_t *n, uint16_t command_class, uint8_t version);

/** Node CC version helper function - Set default
 *
 * Write default values to a memory area, not specifically to an
 * rd_node_database_entry.
 *
 * \ingroup node_db
 *
 * \param node_cc_versions_len The size of the memory area. 
 * \param *node_cc_versions The memory area to write the default version info to.
 * \return The actual size of the default values.
 */
int rd_mem_cc_versions_set_default(uint8_t node_cc_versions_len,
                                   cc_version_pair_t *node_cc_versions);

/** Node CC version helper function - Set Default
 *
 * \ingroup node_db
 *
 * \param n The node to reset the version probing information
 */
void rd_node_cc_versions_set_default(rd_node_database_entry_t *n);

/**
 * @brief Return the size of controlled command class that gateway is caching.
 *
 * \ingroup node_db
 *
 * \return size size of controlled_cc_v array
 */
uint8_t controlled_cc_v_size();

#endif
