/* CC_GatewayKeepAlive.h - Z/IP Gateway Keep-Alive / Important Node / App State module */

#ifndef CC_GATEWAY_KEEP_ALIVE_H_
#define CC_GATEWAY_KEEP_ALIVE_H_

#include "TYPES.H"
#include "ZW_classcmd_ex.h"
#include <stdint.h>

/**
 * Gateway-layer return codes from gw_keepalive_handle_*.
 * GW_KEEPALIVE_OK (0) means success — same meaning as for Z-Wave REPORT status (0x00 = OK).
 * This is not the same as NCP wire byte 0x00 for “clear not accepted”; that case uses GW_KEEPALIVE_ERR_CLEAR_NOT_ACCEPTED.
 * Passthrough Serial API / NCP list status may also appear (SERIALAPI_HOST_HIBERNATION_ERR_* or positive list codes).
 * GW_KEEPALIVE_ERR_* use -256..-258 so they do not collide if Serial API adds more negative ERR_* values (see Serialapi.h).
 */
#define GW_KEEPALIVE_OK                         0
#define GW_KEEPALIVE_ERR_BASE                   (-256) /* first gateway-local error; block -256..-258 reserved */
#define GW_KEEPALIVE_ERR_INVALID_PAYLOAD        (GW_KEEPALIVE_ERR_BASE) /* GATEWAY_IMPORTANT_NODE_LIST_SET: length/count/payload malformed */
#define GW_KEEPALIVE_ERR_CLEAR_NOT_ACCEPTED     ((GW_KEEPALIVE_ERR_BASE) - 1) /* NCP clear status 0x00; cannot return GW_KEEPALIVE_OK to caller */
#define GW_KEEPALIVE_ERR_INVALID_APP_STATE      ((GW_KEEPALIVE_ERR_BASE) - 2) /* GATEWAY_APP_STATE_SET: state is not AWAKE/ASLEEP */
#define GW_KEEPALIVE_ERR_SEVERITY_MISMATCH      ((GW_KEEPALIVE_ERR_BASE) - 3) /* NCP severity threshold differs from requested value */

/**
 * Initialize keep-alive module (call once after SerialAPI_GetInitData(), e.g. from ApplicationInitNIF).
 * Queries NCP via SerialAPI_GetModuleCapabilities() so SerialAPI_SendImportantDeviceList uses the correct max frame length.
 */
void gw_keepalive_init(void);

/**
 * Handle GATEWAY_IMPORTANT_NODE_LIST_SET from the app.
 * Parses the payload, stores the list, and returns GW_KEEPALIVE_OK on success.
 * On Serial API failure returns that function's status (SERIALAPI_HOST_HIBERNATION_ERR_* or NCP list status).
 * Returns GW_KEEPALIVE_ERR_CLEAR_NOT_ACCEPTED if the NCP rejects clear (wire 0x00).
 * Returns GW_KEEPALIVE_ERR_INVALID_PAYLOAD if the payload is invalid (length, count, or entries).
 */
int gw_keepalive_handle_important_node_set(const uint8_t *payload, uint16_t len);

/**
 * Handle GATEWAY_APP_STATE_SET from the app.
 * @param state       GW_APP_STATE_AWAKE or GW_APP_STATE_ASLEEP.
 * @param severity_level  Minimum frame severity to wake the host (only meaningful
 *                        when state == GW_APP_STATE_ASLEEP; ignored on wake).
 * Returns GW_KEEPALIVE_OK on success; on Serial API failure returns that function's status (SERIALAPI_HOST_HIBERNATION_ERR_*).
 * Returns GW_KEEPALIVE_ERR_INVALID_APP_STATE if \a state is invalid.
 */
int gw_keepalive_handle_app_state_set(uint8_t state, uint8_t severity_level);

/**
 * Get current app state (GW_APP_STATE_AWAKE / GW_APP_STATE_ASLEEP).
 */
uint8_t gw_keepalive_get_app_state(void);

/**
 * Get the current important node list.
 * @param[out] count  Number of entries.
 * @return pointer to internal array (valid until next set); NULL if empty.
 */
const gw_important_node_entry_t *gw_keepalive_get_node_list(uint16_t *count);

/**
 * Handle one row of the NCP S2 message count list (host hibernation, typically after wake).
 * Passes NCP @a s2_count through to SPAN reconciliation (PRNG advance when last_seq moved); persists when a matching span exists.
 * Does not send GATEWAY_WAKE_NOTIFY_REPORT.
 * @param node_id   Node ID for this row (from S2 message count list response).
 * @param s2_count  NCP S2 message count for this node (used as sync input, not a host-stored baseline).
 * @param last_seq  Last sequence byte from the NCP for this node.
 * @return None.
 */
void gw_keepalive_on_ncp_s2_count_sync(uint16_t node_id, uint8_t s2_count, uint8_t last_seq);

/**
 * Handle the Device Lost unsolicited report from the NCP.
 * Sends GATEWAY_WAKE_NOTIFY_REPORT with reason DEVICE_LOST; forwards optional payload as opt_data.
 * @param payload      Raw table from NCP: N x (node_id BE uint16, last_seen_minutes BE uint16).
 * @param payload_len  Byte length of payload (multiple of 4 when non-zero).
 * @return None.
 */
void gw_keepalive_on_device_lost(const uint8_t *payload, uint16_t payload_len);

/**
 * Send GATEWAY_WAKE_NOTIFY_REPORT to the unsolicited destination (optional payload for HCAPI metadata).
 * @param wake_reason  GW_WAKE_REASON_xxx (ZW_classcmd_ex.h). For GW_WAKE_REASON_CRITICAL_MESSAGE,
 *                     opt_data is 3 bytes: source node_id u16 BE, then severity (lower 4 bits).
 * @param opt_data     Optional (may be NULL); truncated to GW_WAKE_NOTIFY_REPORT_OPT_DATA_MAX
 * @param opt_data_len Byte length of opt_data
 * @return None.
 */
void gw_keepalive_send_wake_notify(uint8_t wake_reason,
                                   const uint8_t *opt_data,
                                   uint16_t opt_data_len);

#endif /* CC_GATEWAY_KEEP_ALIVE_H_ */
