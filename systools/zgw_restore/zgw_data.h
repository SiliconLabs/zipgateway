/* © 2019 Silicon Laboratories Inc. */
#ifndef _ZGW_DATA_H
#define _ZGW_DATA_H

#include <stdint.h>

#include "lib/list.h" /* Contiki lists */
#include "net/uip.h" /* Contiki uip_ip6addr_t */

#include "zw_data.h"
#include "zgw_restore.h"

#include "RD_internal.h"
#include "provisioning_list.h"
#include "Bridge_temp_assoc.h"
#include "RD_types.h"

/**
 * \ingroup zgw-restore
 *
 * \defgroup restore-db Z/IP Gateway Restore Tool Internal Data
 *
 * @{
 *
 * The internal data structure \ref zip_gateway_backup_data used
 * for restoring a Z/IP Gateway from a back-up or from an older
 * controller.
 *
 * The structure is used when reading a Z/IP Gateway backup file,
 * extracting information from a bridge controller, and re-creating
 * Z/IP Gateway persistence files. It is used to organize,
 * synchronize, and validate information that may come from different
 * sources and go to different targets.
 *
 * For more details on how the fields in the ZGW Data File format are
 * mapped to fields in a running Z/IP Gateway, see \ref page-zgw-data.
 *
 * The functions used for reading from these internal structures always give
 * pointers to the structure, i.e., all data must be copied by the reader.
 *
 * The functions used for adding data to the internal structures may take or copy
 * their pointer arguments, depending on the specific fields. Be sure to
 * check the definitions in each function when using these.
 */

/* =========================== ZGW Node Info Data Types ========================== */

/** \defgroup restore-db-zgw Restore Tool Internal Data for Z/IP Gateway
 * Data structures to store the runtime state of the Z/IP Gateway.
@{
 */


/** Structure for a node UID that is Z-Wave network based. */
typedef struct zw_net_uid {
   /** homeID here should be the same as the one that is read on
    * the serial API. */
   uint32_t homeID;
   nodeid_t node_id;
} zgw_zw_net_uid_t;

/** Structure for a node UID that is DSK based. */
typedef struct dsk_id {
   uint8_t dsk_len; /**<  Length of the DSK.  Currently 16 or 0. Initialize to 0. */
   /** Device-Specific Key for a device. Pointer to a byte array or NULL. */
   uint8_t *dsk;
} zgw_dsk_uid_t;

/** Type selector for the node UID. */
typedef enum zgw_node_uid_type {
   /** struct dsk_id - must be used if DSK exists. */
   node_uid_dsk,
   /** Use struct zw_net_uid - for a device without a DSK in a Z-Wave
    * network. */
   node_uid_zw_net_id
} zgw_node_uid_type_t;

/** Data type for the node UID. */
typedef union {
   zgw_zw_net_uid_t net_uid;
   zgw_dsk_uid_t dsk_uid;
} zgw_node_uid_t;

/**
 * Provisioning data for a node.
 *
 * Z/IP Gateway type
 *
 * \note Unsupported.
 */
typedef struct zgw_node_pvs {
   uint8_t num_tlvs;
   struct provision pvs;
   // pvs_status_t status; /** Current state of this item. */
   // provisioning_bootmode_t bootmode; /**< Provisioning bootmode of this item (S2 or smartstart) */
   // struct pvs_tlv *tlv_list; /**< TLVs for this item. */
} zgw_node_pvs_t;

/**
 * DHCP lease data for a node.
 *
 * Z/IP Gateway type
 *
 * \note Unsupported.
 */
typedef uint8_t dhcp_lease_t;

/**
 * Z/IP Gateway LAN-side data for a Z-Wave PAN-side node
 * (Including the gateway's Z-Wave node interface.)
 */
typedef struct zgw_node_IP_data {
   /** Length of #mDNS_node_name, which is derived from the JSON string length. Initialize to 0. */
   uint8_t mDNS_node_name_len;
   /** Corresponds to the nodename field in rd_node_database_entry.
    * Has fixed format zw%08X%02X, possibly followed by a counter.
    * Default value NULL. */
   char *mDNS_node_name;
   dhcp_lease_t lease; /**< TODO-FUTURE */
} zgw_node_IP_data_t;



/** ZGWs mDNS-specific data for a Z-Wave end-point.
 *
 * Z/IP Gateway type
 *
 * Optional data for migration.
 *
 */
typedef struct zgw_ep_mDNS_data {
   /** mDNS field, which must be valid according to the SDS13782 before storing in this field. */
   char *endpoint_location;
   /** mDNS field, which must be valid according to the SDS13782 before storing in this field. */
   char *endpoint_name;
  /** Length of #endpoint_location, which is derived from the JSON string length. Initialize to 0. */
   uint8_t endpoint_loc_len;
   /** Length of #endpoint_name, which is derived from the JSON string length. Initialize to 0. */
   uint8_t endpoint_name_len;
} zgw_ep_mDNS_data_t;

/**
 * ZGW-specific data on an endpoint of a Z-Wave node.
 *
 * If the node's probe state is DONE, the endpoint ID, info, icons,
 * and aggregations must be correct.
 */
typedef struct zgw_node_ep_data {
   struct zgw_node_ep_data_t* ep_list; /**< Contiki list management. */
   /* rd_node_database_entry_t* parent_node; Pointer to the ep's
      parent node.  Should be inserted by eeprom.dat writer. */
   zgw_ep_mDNS_data_t ep_mDNS_data; /**< mDNS name and location for endpoint. */
   /** Length of #endpoint_info, which is computed from the composite of the
       relevant JSON fields. Initialize to 0. */
   uint8_t endpoint_info_len;
   /** Length of #endpoint_agg, which is derived from the JSON hex-string
    * length. Initialize to 0. */
   uint8_t endpoint_aggr_len;
   uint8_t endpoint_id; /**< End Point identifier.  Derived from the JSON list index. */
   zgw_restore_field_status_t endpoint_id_field_status;
   /** End Point probing state, which is set to EP_STATE_PROBE_INFO by the
       JSON reader if the parent node has state RE-INTERVIEW.  Set to
       EP_STATE_PROBE_DONE if the parent node has state DONE.
       Initialized to EP_STATE_PROBE_INFO. */
   rd_ep_state_t state;
   uint16_t installer_iconID; /**< Z-Wave plus icon ID. */
   uint16_t user_iconID; /**< Z-Wave plus icon ID. */

   /** The NIF of the end point, as determined at last probing.  For ep0,
    * the NIF and secure NIF of the node. For other endpoints, the
    * data in the multi channel capability report for the
    * endpoint.
    *
    * Contains the generic and specific device classes and the command
    * classes supported by the root device/end points. It must be
    * represented exactly as it appears in the protocol frame.
    *
    * If the nonSecureNIF field is present on the root device of a
    * node (endpoint index 0) in JSON, the parser will add ZIP and
    * ZIP_NAMING to the end of the non-secure CC, just like ZGW does
    * in \ref rd_nif_request_notify().
    *
    * Used in \ref rd_ep_class_support() to determine if a
    * node/ep supports a given CC.  Represented as a pointer to a byte
    * array.  MAX size 0xFF. */
   uint8_t *endpoint_info;
   /** Aggregated endpoints.  Pointer to a byte array.  Max size 0xFF. */
   uint8_t *endpoint_agg;
} zgw_node_ep_data_t;


/** ZGW liveness estimate for a PAN side node.
 *
 * Depending on the type of node, ZGW may poll it or expect to hear
 * from it at configured intervals.  If the node repeatedly fails to
 * report as expected, ZGW will set its state to FAILING and report
 * this on mDNS.
 */
typedef enum zgw_node_liveness_state {
   ZGW_NODE_OK = STATUS_DONE, /**< Node has been heard from within the
                               * configured time period. */
   ZGW_NODE_FAILING = STATUS_FAILING /**< ZGW has not been able to
                                      * contact node in the configured
                                      * way. */
} zgw_node_liveness_state_t;

/** JSON data to configure the ZGW startup behavior after restore.
 * How should the ZGW interview machine handle a node.
 *
 * If the node is non-listening, ZGW may delay the interview.
 */
typedef enum zgwr_user_node_interview {
   node_interview = STATUS_CREATED, /**< Request a new interview. (STATUS_CREATED)*/
   node_do_not_interview = STATUS_DONE, /**< Request no interview. (STATUS_DONE)*/
   node_interview_unknown = 127 /**< If all the data is available, do not interview. */
} zgw_user_node_interview_t;

/** ZGW liveness information on a Z-Wave node.
 *
 * Reporting interval and timestamps for last communications.
 */
typedef struct zgw_node_liveness_data {
   zgw_node_liveness_state_t liveness_state; /**< OK or FAILING. Currently
                                              *   overloaded into state.*/
   uint32_t wakeUp_interval; /**< Node's wake up interval, found by probing. */
   uint32_t lastAwake; /**< Timestamp of the last frame received from
                        * the node.  Faked during restart. */
   uint32_t lastUpdate; /**< Timestamp for the last successful probe by ZGW. */
} zgw_node_liveness_data_t;

/**
 * Mask to isolate the bits of node_properties that are persisted.
 */
#define PERSISTED_NODE_PROPERTIES_FLAGS (RD_NODE_FLAG_PORTABLE | RD_NODE_FLAG_ADDED_BY_ME)

/**
 * ZGW-specific data on the state of the interview of a node.
 *
 * Some of this is ZGW-internal and can be deduced from other data
 * during import.
 */
typedef struct zgw_node_probe_state {
   /** Internal data. Probe/interview state for the node.  Default
    * value DONE. */
   rd_node_state_t state;
   /** JSON field guiding the probe/interview state request for the
    * node.
    *
    * If the state is set to DONE, it may be "down-graded" to
    * RE-INTERVIEW if the grantedKeys field is not given for the node
    * or if the nonSecureNIF is not given for all the endpoints of the
    * node (except aggregated endpoints).
    *
    * Default value node_interview for M1. */
   zgw_user_node_interview_t interview_state;
   /** Cached result of Version V3 probe.
    *
    * Contains the properties field of the version capability report frame.
    * Only valid if CC Version V3 is supported by node.
    * Required if state is DONE and CC Version V3 is supported.
    *
    * VERSION_CAPABILITIES_REPORT_PROPERTIES1_VERSION_BIT_MASK_V3 0x01 must be set.
    * VERSION_CAPABILITIES_REPORT_PROPERTIES1_COMMAND_CLASS_BIT_MASK_V3 0x02 must be set.
    * VERSION_CAPABILITIES_REPORT_PROPERTIES1_Z_WAVE_SOFTWARE_BIT_MASK_V3 0x04 can be 0 or 1.
    */
   uint8_t node_version_cap_and_zwave_sw;
   /** Has the controller received a VERSION_ZWAVE_SOFTWARE_REPORT from
    * the node.
    *
    * Only valid if CC Version V3 is supported by node and versionCap
    * is 7.
    *
    * Default value true if state is DONE and Version V3 is supported
    * and node_version_cap_and_zwave_sw is 0x7, false otherwise.
    * Ignored if state is CREATED.
    */
   uint8_t node_is_zws_probed;

   /** Internal data. Probe status flags covering all probes of this
    * node, not just the current \ref state.  E.g., NEVER_STARTED,
    * STARTED, PROBE_FAILED, HAS_COMPLETED.
    *
    * Default value RD_NODE_FLAG_PROBE_HAS_COMPLETED.  The JSON reader
    * will set RD_NODE_FLAG_PROBE_HAS_COMPLETED if node state is DONE,
    * and RD_NODE_PROBE_NEVER_STARTED if the node state is CREATED.
    */
   uint16_t probe_flags;
   /** Node properties and capabilities stored as flags.
    *
    * - RD_NODE_FLAG_JUST_ADDED - transient flag
    * - RD_NODE_FLAG_ADDED_BY_ME 0x04 - granted keys are trusted,
    *   never downgrade security when interviewing.
    * - RD_NODE_FLAG_PORTABLE 0x01
    *
    * No meaningful default.  Initialize to 0.
    *
    * RD_NODE_FLAG_ADDED_BY_ME will be set for security reasons, if
    * the grantedKeys field is present in the Z/IP Gateway JSON file.
    *
    * RD_NODE_FLAG_PORTABLE is set if ZWplusRoleType is set to
    * portableSlave in the ZGW Data File.  This field MUST be set if
    * it is applicable to the node.
    */
   uint16_t node_properties_flags;

   /** Length of \ref node_cc_versions.  Gateway constant.  See \ref
       rd_mem_cc_versions_set_default(). */
   uint8_t node_cc_versions_len;
   /** CC versions cache for this node.
    *
    * Should be filled correctly if interview state is DONE in the
    * JSON file, but not strictly required.
    *
    * Default value 0 for all CCs controlled by the gateway.
    *
    * Will be set to 0 by the JSON reader if state is STATUS_CREATED.
    */
   cc_version_pair_t *node_cc_versions;
} zgw_node_probe_state_t;

/** ZGW CC Manufacturer-specific data on a node.
 */
typedef struct zgw_node_product_id {
  uint16_t manufacturerID;
  uint16_t productType;
  uint16_t productID;
} zgw_node_product_id_t;

/**
 * Z/IP Gateway type
 *
 * Data for a Z-Wave node, maintained by a Z-Wave
 * controller application (RD in the gateway).
 */
typedef struct zgw_node_zw_data {
   /* Liveness of the node */
   zgw_node_liveness_data_t liveness; /**< How to get in touch with this
                                         node and node reachability. */
   /** Z-Wave specific data for node (protocol data).
    *
    * May be empty. If node is only provisioned, nodeid is 0.
    */
   zw_node_data_t zw_node_data;

   /** Node interview data and meta-data */
   zgw_node_probe_state_t probe_state;

   /** Manufacturer-specific CC data. Required fields if state is DONE. */
   zgw_node_product_id_t node_prod_id;

   /** Bit mask of security keys granted to this node.  Stored in
    * node_flag format.
    *
    * Correlated with the ADDED_BY_ME flag on node_properties.  If
    * ADDED_BY_ME is set, the gateway will assume that the keys are
    * trusted and never downgrade security during probing.
    *
    * Set from the grantedKeys field in JSON.
    * ADDED_BY_ME is set if the grantedKeys field is present in JSON.
    *
    * Initialize to 0.  */
   uint8_t security_flags;

   /** Total number of endpoints, incl root device. Computed from the
    * length of the JSON endpoints array.  Initialize to 0. */
   uint8_t nEndpoints;
   /** Number of aggregated endpoints. Computed from the JSON
    * hex-string. Initialize to 0. */
   uint8_t nAggEndpoints;

   /** ALWAYSLISTENING, NONLISTENING, FREQUENTLYLISTENING, MAILBOX
   */
   rd_node_mode_t mode;

   LIST_STRUCT(endpoints); /**< Contiki list of endpoint data. */
} zgw_node_zw_data_t;

/** Information the gateway holds about a device on a PAN network.
 *
 */
typedef struct zgw_node_data {
   zgw_node_uid_type_t uid_type;
   zgw_node_uid_t node_uid; /**< (Locally) unique ID, e.g., HomeID/nodeID
                             * pair or DSK of the device. */
   zgw_node_zw_data_t node_gwdata; /**< Z-Wave-related data maintained
                                    * by a Z-Wave controller.  May be
                                    * empty, e.g., if node is only
                                    * provisioned. */
   zgw_node_IP_data_t ip_data; /**< node IP info */
   zgw_node_pvs_t pvs_data; /**< Provisioning data. May be empty, e.g.,
                             * for S0 nodes. The DSK is not included
                             * here. */
} zgw_node_data_t;


/* =========================== ZGW Data Types ========================== */

/**
 * The gateway's LAN side IP configuration parameters.
 *
 * Mostly related to zipgateway.cfg.
 *
 * Mostly not used for migration (except MAC address and
 * zgw_ula_lan_addr).
 */
typedef struct zip_lan_ip_data {
   /** ZipLanGw6 in the cfg file.  Field gw_addr in \ref router_config.
    * IPv6 default gateway/next hop on the LAN side.  Initialize to
    * zeros.  Required in cfg file for IPv6 connectivity. */
   uip_ip6addr_t zgw_cfg_lan_gw_ip6addr;

   /** Config file ZipLanIp6. cfg_lan_addr in struct router_config.
     * Initialize with zeros. */
   uip_ip6addr_t zgw_cfg_lan_addr;

   /** ZGW lan side address. ula_lan_addr in struct router_config.
    * Generated ULA (stored in NVM area). Initialized with zeros.
    * Should be included for backup. */
   uip_ip6addr_t zgw_ula_lan_addr;

   /** ZGW L2 address (MAC address). Set to 0 to regenerate on gateway
    * startup. */
   uip_lladdr_t zgw_uip_lladdr;

   uip_ip6addr_t unsol_dest1;
   uint16_t unsol_port1;
   uip_ip6addr_t unsol_dest2;
   uint16_t unsol_port2;

   /** The gateway's DTLS key from cfg file.  Not needed if
    * zipgateway.cfg is provided. */
   uint8_t zip_psk[64];
   size_t zip_psk_len; /**< The length of the gateways DTLS key from
                        * cfg file. Derived from the hex-string length. */
   /** ZipCaCert in zipgateway.cfg.  Default /usr/local/etc/Portal.ca_x509.pem. */
   char *zip_ca_cert_location;
   /** ZipCert in  zipgateway.cfg.  Default /usr/local/etc/ZIPR.x509_1024.pem */
   char *zip_cert_location;
   char *zip_priv_key_location; /**< Default /usr/local/etc/ZIPR.key_1024.pem */
} zip_lan_ip_data_t;

/**
 * Z-Wave network side security keys for the gateway software.
 */
typedef struct zw_security {
   /* Security 0 key */
   uint8_t security_netkey[16];
   /*S2 Network keys */
   uint8_t assigned_keys; /**< The gateway assigned keys.  Uses
                             keystore format.  A new key will be
                             generated by the gateway if the it is not
                             assigned. */
   uint8_t security2_key[3][16]; /**< S2 net keys. */
   uint8_t security2_lr_key[2][16]; /**< S2 LR net keys. */
   uint8_t ecdh_priv_key[32]; /**< The gateways private key. */
} zw_security_t;

/**
 * Z-Wave network side network data for the gateway software.
 */
typedef struct zip_pan_data {
   /** IPv6 ULA Z-Wave side prefix.  Stored in NVM area.  Used if prefix is
    * not set in cfg file. */
   uip_ip6addr_t ipv6_pan_prefix;
   /** PAN side security keys.  Stored in NVM area. */
   zw_security_t zw_security_keys;
} zip_pan_data_t;

typedef struct zgw_temporary_association_data {
   uint8_t virtual_nodes_count;
   nodeid_t virtual_nodes[MAX_CLASSIC_TEMP_ASSOCIATIONS];
} zgw_temporary_association_data_t;

/** ZGW data.
 *
 *  Z/IP Gateway backup data that is specific to the gateway software.
 */
typedef struct zgw_data {
   zgw_node_data_t *zw_nodes[8000]; /**< ZGW info about nodes on the
                                    * Z-Wave network and in the
                                    * provisioning list.  Included
                                    * nodes are indexed by node_id. */

   uint8_t zip_ipv6_prefix_len; /**< Config file
                                   ZipLanIp6PrefixLength, must be 64. */;
   zip_lan_ip_data_t zip_lan_data;
   zip_pan_data_t zip_pan_data;
   zgw_temporary_association_data_t zgw_temporary_association_data;
} zgw_data_t;

/**
 * @}
 */



/* =========================== Full System Data Types ========================== */

/** Timestamp of the backup file.
 */
typedef uint32_t zgw_bu_timestamp_t;

/** Backup meta-data/manifest.
 *
 */
typedef struct zip_gateway_backup_manifest {
   uint8_t backup_version_major;
   uint8_t backup_version_minor;
   zgw_bu_timestamp_t backup_timestamp;
} zip_gateway_backup_manifest_t;

/**
 * Main Z/IP Gateway Back-up data.
 */
typedef struct zip_gateway_backup_data {
   zip_gateway_backup_manifest_t manifest;
   char* serial_path;
   zw_controller_t controller;
   zgw_data_t zgw;
} zip_gateway_backup_data_t;


/* =========================== Functions ========================== */

/* Helpers */

/** Logging helper to print a uid.
 * Re-uses the same memory area.
 */
char* zgw_node_uid_to_str(zgw_node_data_t *node);

/**
 * Logging helper to print an ipv6 address.
 */
void zgwr_ipaddr6_print(const uip_ipaddr_t *addr);

/** Validate that the int32 is a valid nodeid and store it.
 * \return The node id.  0 if the_number is out of range.
 */
uint16_t node_id_set_from_intval(int32_t the_number,
                                uint16_t *slot);

/** Security keys format conversion */
uint8_t node_flags2keystore_flags(uint8_t gw_flags);

/** Security keys format conversion */
uint8_t keystore_flags2node_flags(uint8_t key_store_flags);

/** Setting supported version for a command class if it is supported by the
 * gateway.
 * \ingroup restore-db-zgw
 * If the command class is not supported, nothing happens.
 */
uint8_t cc_version_set(zgw_node_probe_state_t *probe_state, uint16_t cc,
                       uint8_t node_version);

/** Convert string to Contiki IP address.
 * (Wrapper for the Contiki converter.) */
bool ipv6addr_set_from_string(const char* addr, uip_ip6addr_t *slot);


/* Put stuff in internal representation */

/**
 * Flush \ref zip_gateway_backup_data
 */
void zgw_data_reset(void);

/**
 * Access a writable version of the data for the JSON reader to fill in.
 */
zip_gateway_backup_data_t * zip_gateway_backup_data_unsafe_get(void);


/** Add data for a node to the backup data.
 *
 * \ingroup restore-db-zgw
 * node_data must be a valid pointer.  The functions keeps the pointer. */
bool zgw_node_data_pointer_add(nodeid_t node_id, zgw_node_data_t *node_data);

/**
 * Allocate and initialize \ref zgw_node_data_t.
 * \ingroup restore-db-zgw
 */
zgw_node_data_t *zgw_node_data_init(void);

/**
 * Allocate and initialize \ref zgw_node_ep_data_t.
 * \ingroup restore-db-zgw
 */
/*@null@*/zgw_node_ep_data_t *zgw_node_endpoint_init(void);

/**
 * Free \ref zgw_node_ep_data_t and sub-structures.
 * \ingroup restore-db-zgw
 */
void zgw_node_endpoint_free(zgw_node_ep_data_t *ep);

/**
 * Initialize \ref zip_gateway_backup_data.
 *
 * If zgw is NULL, the zgw of the backup will be cleared.  Pointers may be leaked.
 *
 * \param manifest Pointer to backup-package meta-data.
 * \param controller Pointer to controller data that will be copied to the backup.
 * \param zgw Pointer to gateway data that will be copied to the backup.
 * \param provisioned_nodes Pointer to provisioning data that will be kept.  Not used.
 */
int zgw_data_init(zip_gateway_backup_manifest_t *manifest,
                  zw_controller_t *controller,
                  zgw_data_t *zgw,
                  zgw_node_pvs_t *provisioned_nodes);


/*
 * Get data from the internal representation.
 */

/** Get Z-Wave data for a node from the global controller object.
 *
 * \ingroup restore-db-zgw
 */
const zw_node_data_t * zw_node_data_get(nodeid_t nodeid);

/**
 *  Get the gateway-specific data and data about the network from the backup object.
 * \ingroup restore-db-zgw
 */
const zgw_data_t *zgw_data_get(void);

/**
 *  Get the gateway's data about a node in the network from the backup object.
 * \ingroup restore-db-zgw
 */
const zgw_node_data_t *zgw_node_data_get(nodeid_t nodeid);

/**
 * Not supported.
 */
const zgw_node_pvs_t * zgw_node_pvs_get(zgw_dsk_uid_t node_uid);

/** Get endpoint data.
 *
 * \ingroup restore-db-zgw
 */
const zgw_node_ep_data_t * zgw_node_ep_data_get(nodeid_t nodeid, uint8_t epid);

/** Get Z-Wave controller data from the global backup object.
 *
 */
const zw_controller_t * zgw_controller_zw_get(void);

/**
 *
 * \ingroup restore-db-zgw
 */
const zip_lan_ip_data_t * zip_lan_ip_data_get(void);

/** Get the gateway's temporary association data
 *
 * \ingroup restore-db-zgw
 */
const zgw_temporary_association_data_t * zgw_temporary_association_data_get(void);

/**
 *
 */
const zip_gateway_backup_data_t * zip_gateway_backup_data_get(void);

/**
 * Check that the data read from import sources is complete, valid,
 * and consistent before writing to the zip gateway files.
 *
 * \return Number of errors the sanitizer finds in the data.
 */
int zip_gateway_backup_data_sanitize(void);

/**
 * @}
 */

#endif
