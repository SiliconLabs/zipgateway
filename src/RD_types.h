/* © 2018 Silicon Laboratories Inc.  */

#ifndef RD_TYPES_H
#define RD_TYPES_H

/* TODO Consider to move all the following types here
 * - rd_ep_state_t
 * - rd_node_state_t
 * - rd_node_mode_t
 * - rd_node_database_entry_t
 * - rd_ep_data_store_entry_t
 */

typedef uint16_t nodeid_t;

/*************************************************************/
/*                          Version Probing                  */
/*************************************************************/

/** Probe CC Version State
 * \ingroup cc_version_probe
 */
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

typedef enum {
  PCV_EV_INITIAL,
  PCV_EV_START,
  PCV_EV_CC_PROBED,
  PCV_EV_CC_NOT_SUPPORT,
  PCV_EV_VERSION_CC_REPORT_RECV,
  PCV_EV_VERSION_CC_CALLBACK_FAIL,
  PCV_EV_NOT_LAST,
  PCV_EV_VERSION_CC_DONE,
  PCV_EV_IS_V3,
  PCV_EV_NOT_V3,
  PCV_EV_VERSION_CAP_REPORT_RECV,
  PCV_EV_VERSION_CAP_CALLBACK_FAIL,
  PCV_EV_CAP_PROBED,
  PCV_EV_VERSION_ZWS_REPORT_RECV,
  PCV_EV_VERSION_ZWS_CALLBACK_FAIL,
  PCV_EV_ZWS_PROBED,
  PCV_EV_ZWS_NOT_SUPPORT,
} pcv_event_t;

/** Struct to form the list of controlled CC, including CC and its latest version */
typedef struct cc_version_pair {
   /** The command class identifier. */
   uint16_t command_class;
   /** The version supported by the node. */
   uint8_t version;
} cc_version_pair_t;

/* The callback type for probe_cc_version done
 *
 * \param user The user defined data
 * \param callback The status code, 0 indicating success
 */
typedef void (*_pcvs_callback)(void *user, uint8_t status_code);

typedef struct ProbeCCVersionState {
  /* The index for controlled_cc, mainly for looping over the CC */
  uint8_t probe_cc_idx;
  /* Global probing state */
  pcv_state_t state;
  /* The callback will be lost after sending asynchronous ZW_SendRequest. Keep the callback here for going back */
  _pcvs_callback callback;
} ProbeCCVersionState_t;


#endif
