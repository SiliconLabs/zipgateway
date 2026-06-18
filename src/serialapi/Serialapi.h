/* © 2014 Silicon Laboratories Inc. */

#ifndef SERIAL_API_H_
#define SERIAL_API_H_

#include "TYPES.H"

#define ZW_CONTROLLER
#define ZW_CONTROLLER_BRIDGE

#include "ZW_SerialAPI.h"
#include "ZW_basis_api.h"
#include "ZW_controller_api.h"
#include "ZW_transport_api.h"
#include "ZW_timer_api.h"
#include "zgw_nodemask.h"
#include "ZW_nvr_api.h"
/**
 * Size of RX and TX buffers used with the serial API.
 *
 * NB: Value is taken from SERBUF_MAX in conhandle.h in Z-Wave controller.
 *     ZGW has a modified version of conhandle.h where SERBUF_MAX is defined
 *     differently.
 */
#define BUF_SIZE 180
#define RF_US_LR 0x09
/**
 * Structure holding all values which are known from the Z-Wave application programmers
 * guide. They all serve the essentially same function, except for the ApplicationInitHW,
 * which is always called with  bWakeupReason==0
 */
struct SerialAPI_Callbacks {
  void (*ApplicationCommandHandler)(BYTE  rxStatus,uint16_t destNode, uint16_t sourceNode, ZW_APPLICATION_TX_BUFFER *pCmd, BYTE cmdLength);
  void (*ApplicationNodeInformation)(BYTE *deviceOptionsMask,APPL_NODE_TYPE *nodeType,BYTE **nodeParm,BYTE *parmLength ); /*TODO Remove this callback*/
  void (*ApplicationControllerUpdate)(BYTE bStatus,uint16_t bNodeID,BYTE* pCmd,BYTE bLen, BYTE*);
  BYTE (*ApplicationInitHW)(BYTE bWakeupReason); /*TODO Remove this callback*/
  BYTE (*ApplicationInitSW)(void ); /*TODO Remove this callback*/
  void (*ApplicationPoll)(void); /*TODO Remove this callback*/
  void (*ApplicationTestPoll)(void); /*TODO Remove this callback*/
  void (*ApplicationCommandHandler_Bridge)(BYTE  rxStatus,uint16_t destNode, uint16_t sourceNode, ZW_APPLICATION_TX_BUFFER *pCmd, BYTE cmdLength);
  void (*SerialAPIStarted)(BYTE *pData, BYTE pLen);
  void (*OnNcpS2CountSync)(uint16_t node_id, uint8_t s2_count, uint8_t last_seq);
  void (*OnDeviceLost)(const uint8_t *payload, uint16_t payload_len);
#if 0
  void (*ApplicationSlaveCommandHandler)(BYTE rxStatus, BYTE destNode, BYTE sourceNode, ZW_APPLICATION_TX_BUFFER *pCmd, BYTE cmdLength);
#endif
};

#define CHIP_DESCRIPTOR_UNINITIALIZED 0x42
// The wakeup reason of Z-Wave API Started Command (0x0A) is 0x07 as defined in the Z-Wave Serial API specification.
#define SOFT_RESET_WAKEUP_REASON 0x07

/**
 * This chip descriptor contains the version of chip (e.g. 500 or 700).
 * It is populated when the serial API process starts up.
 * Until populated, it contains the value CHIP_DESCRIPTOR_UNINITIALIZED.
 */
struct chip_descriptor {
  uint8_t my_chip_type;
  uint8_t my_chip_version;
};
extern struct chip_descriptor chip_desc;

typedef enum node_id_type {
  NODEID_8BITS = 1,
  NODEID_16BITS = 2
} node_id_type_t;

/*static struct SerialAPI_Callbacks serial_api_callbacks = {
    ApplicationCommandHandler,
    ApplicationNodeInformation,
    ApplicationControllerUpdate,
    ApplicationInitHW,
    ApplicationInitSW,
    ApplicationPoll,
    ApplicationTestPoll,
};*/

/*
 * Initialize the Serial API, with all the callbacks needed for operation.
 */
BOOL SerialAPI_Init(const char* serial_port, const struct SerialAPI_Callbacks* callbacks );
void SerialAPI_Destroy();
/*
 * This function must be called from an application main loop since it drives the
 * serial API engine. SerialAPI_Poll checks the serial port for new data and
 * handles timers and all other async callbacks, i.e., all Serial API callbacks
 * are called from within this function.
 *
 * TODO: It would be nice to have SerialAPI_Poll return the minimum number of system
 * ticks to parse before a timer times out. In this way we don't need to call
 * SerialAPI_Poll unless we got a receive interrupt from the uart or if a timer
 * has timed out. This means that the system would be able to go into low power
 * mode between SerialAPI_Poll calls.
 */
uint8_t SerialAPI_Poll();

/** Used to indicate that transmissions was not completed due to a
 * SendData that returned false. This is used in some of the higher level sendata
 * calls where lower layer senddata is called async. */
#define TRANSMIT_COMPLETE_ERROR 0xFF

/**
 * Tell the upper queue to re-queue this frame
 * because ClassicZIPNode is busy sending another frame.
 */
#define TRANSMIT_COMPLETE_REQUEUE 0xFE
/**
 * Tell the upper queue to requeue this frame
 * because ClassicZIPNode cannot send to PAN side because
 * NetworkManagament or Mailbox are busy, or because the destination
 * node is being probed.
 * The frame should be re-queued to long-queue or Mailbox. */
#define TRANSMIT_COMPLETE_REQUEUE_QUEUED 0xFD


void SerialAPI_GetLRNodeList(uint16_t *len, BYTE *lr_nodelist);
uint8_t GetLongRangeChannel(void);
/**
 *  Set the long range channel.
 *  \param channel The channel index in integer range 1..2.
 * Note: This will silently do nothing if the module does not support LR.
 */
void SetLongRangeChannel(uint8_t channel);

BYTE SerialAPI_GetInitData( BYTE *ver, BYTE *capabilities, BYTE *len, BYTE *nodesList,BYTE* chip_type,BYTE* chip_version );

BOOL SerialAPI_GetRandom(BYTE count, BYTE* randomBytes);

/** Look up the cached chip type and chip version.
 * Only valid after \ref SerialAPI_GetInitData() has been called.
 *
 * \param type Return argument for the chip type.
 * \param version Return argument for the chip version.
 */
void SerialAPI_GetChipTypeAndVersion(uint8_t *type, uint8_t *version);

/* Long timers */
BYTE ZW_LTimerStart(void (*func)(),unsigned long timerTicks, BYTE repeats);
BYTE ZW_LTimerRestart(BYTE handle);
BYTE ZW_LTimerCancel(BYTE handle);


/* Access hardware AES encrypter. ext_input,ext_output,ext_key are all 16 byte arrays
 * Returns true if the routine went well. */
BOOL SerialAPI_AES128_Encrypt(const BYTE *ext_input, BYTE *ext_output, const BYTE *ext_key);

/*FIXME Don't know why this is not defined elsewhere */
BOOL ZW_EnableSUC( BYTE state, BYTE capabilities );

void SerialAPI_ApplicationSlaveNodeInformation(uint8_t dstNode, BYTE listening,
                                               APPL_NODE_TYPE nodeType,
                                               BYTE *nodeParm, BYTE parmLength);

void SerialAPI_ApplicationNodeInformation( BYTE listening, APPL_NODE_TYPE nodeType, BYTE *nodeParm, BYTE parmLength );

void Get_SerialAPI_AppVersion(uint8_t *major, uint8_t *minor);

/**
 * Enable APM mode
 */
void ZW_AutoProgrammingEnable(void);


void ZW_GetRoutingInfo_old(uint16_t bNodeID, BYTE *buf, BYTE bRemoveBad, BYTE bRemoveNonReps);

void
ZW_AddNodeToNetworkSmartStart(BYTE bMode, BYTE *dsk,
                    VOID_CALLBACKFUNC(completedFunc)(auto LEARN_INFO*));

void
ZW_GetBackgroundRSSI(BYTE *rssi_values, BYTE *values_length);


/**
  * \ingroup ZWCMD
  * Transmit data buffer to a list of Z-Wave Nodes (multicast frame) in bridge mode.
  *
  * Invokes the ZW_SendDataMulti_Bridge() function on the Z-Wave chip via serial API.
  *
  * This function should be used also when the gateway is the source node because the
  * function ZW_SendDataMulti() is not available on a chip with bridge controller
  * library (which is what the gateway uses).
  *
  * NB: This function has a different name than the actual Z-Wave API function.
  *     This is intentional since pre-processor defines in ZW_transport_api.h
  *     re-maps ZW_SendDataMulti and ZW_SendDataMulti_Bridge.
  *
  * \param[in] srcNodeID      Source nodeID - if 0xFF then controller is set as source
  * \param[in] dstNodeMask    Node mask where the bits corresponding to the destination node IDs are set.
  * \param[in] data           Data buffer pointer
  * \param[in] dataLength     Data buffer length
  * \param[in] txOptions      Transmit option flags
  * \param[in] completedFunc  Transmit completed call back function
  * \return FALSE if transmitter queue overflow
  */
BYTE SerialAPI_ZW_SendDataMulti_Bridge(uint16_t srcNodeID,
                                       nodemask_t dstNodeMask,
                                       BYTE *data,
                                       BYTE dataLength,
                                       BYTE txOptions,
                                       VOID_CALLBACKFUNC(completedFunc)(BYTE txStatus));

/**
 * \ingroup BASIS
 * *
 * Enable Watchdog and start kicking it in the Z-Wave chip.
 * serialapi{
 * HOST -> ZW: REQ | 0xD2
 * }
 *
 */
void SerialAPI_WatchdogStart();

/**
 * Close the NVM (restarts the radio) after read or write operation.
 * Returns:
 *  - 0x00: Success
 *  - 0x01: Generic error
 *  - 0x02: Error: Read/Write operation mismatch
 *  - 0x03: Error: Read operation was disturbed by another write
 *  - 0xFF: End of File error
*/
uint8_t SerialAPI_nvm_close();

/**
 * Open the NVM (stops the radio) before read or write operation.
 * Returns the expected size in bytes of the NVM area.
*/
uint32_t SerialAPI_nvm_open();

/**
 * Read a chunk of the 500-series NVM or 700-series NVM3.
 *
 * \returns Status Code for the backup operation.
 *
 *  \retval 0 Status OK
 *  \retval 1 Unspecified Error
 *  \retval 2 - Status End-of-file
 */
uint8_t SerialAPI_nvm_backup(uint16_t offset, uint8_t *buf, uint8_t length, uint8_t *length_read);

/**
 * Write a chunk of the 500-series NVM or 700-series NVM3.
 *
 * \returns Status Code for the write operation.
 *
 *  \retval 0 Status OK
 *  \retval 1 Unspecified Error
 *  \retval 2 - Status End-of-file
 */
uint8_t SerialAPI_nvm_restore(uint16_t offset, uint8_t *buf, uint8_t length, uint8_t *length_written);

/**
 * Check if a command is supported by the current Serial API connection.
 *
 * \note A serial API connection must have been opened with SerialAPI_Init()
 * before calling this function.
 *
 * \param func_id The FUNC_ID of the command to check

 * \retval 1 The command is supported
 * \retval 0 The command is NOT supported
 */
uint8_t SerialAPI_SupportsCommand_func(uint8_t func_id);

/*
 * @brief Report whether the Z-WaveLR is supported or not.
 *
 * @return true
 * @return false
 */
bool SerialAPI_SupportsLR();

/*
 * @brief Enable Z-Wave Long Range capability (basetype and LR virtual nodes).
 *
 * @return true success
 * @return false fail
 */
bool SerialAPI_EnableLR();

/*
 * @brief Disable Z-Wave Long Range capability (basetype and LR virtual nodes).
 *
 * @return true success
 * @return false fail
 */
bool SerialAPI_DisableLR();


/*
 * @brief This is an improvement of ZW_SoftReset() function with check the reset reason of the NCP controller.
 * Note: Recent Z-Wave versions (at least 6.80 and 7.00) will return FUNC_ID_SERIAL_API_STARTED once restarted.
 * Mechanism to check if the NCP controller has been reset successfully or not: 
 * Check if the received frame is the unsolicited frame "Z-Wave API Started Command - FUNC_ID_SERIAL_API_STARTED (0x0A)".
 * If the NCP controller has been reset successfully, it will return the above unsolicited frame with
 * byte-5 data as SOFT_RESET_WAKEUP_REASON (0x07).
 * @return true success
 * @return false fail
 */
bool ZW_SoftResetWithCheck(void);

/*
 * Host hibernation extension — return convention (int):
 *   SERIALAPI_HOST_HIBERNATION_ERR_*   — host-side failure (must stay negative)
 *   SERIALAPI_HOST_HIBERNATION_OK (0)  — success where no separate NCP status is returned
 *   Otherwise             — NCP wire codes below (non-negative; see macros in this block)
 *
 * If new host-side ERR_* values are added, keep them in the range -1 .. -255 so they stay
 * distinct from Z/IP gateway GW_KEEPALIVE_ERR_* in CC_GatewayKeepAlive.h (-256 .. -258).
 *
 * Allocation today: -1 .. -3 below. Next new code should be -4, then -5, etc. (still ≥ -255).
 */
 #define SERIALAPI_HOST_HIBERNATION_OK                                    0
 #define SERIALAPI_HOST_HIBERNATION_ERR_INVALID_ARG                       (-1)
 #define SERIALAPI_HOST_HIBERNATION_ERR_TRANSPORT                         (-2)
 #define SERIALAPI_HOST_HIBERNATION_ERR_BAD_RESPONSE                      (-3)
 
 /* Important Devices List — NCP command status (returned by SerialAPI_SendImportantDeviceList on failure) */
 #define SERIALAPI_HOST_HIBERNATION_LIST_STATUS_SUCCESS                   0x00u
 #define SERIALAPI_HOST_HIBERNATION_LIST_STATUS_UNKNOWN_NODEID            0x01u
 #define SERIALAPI_HOST_HIBERNATION_LIST_STATUS_DUPLICATE_NODEID          0x02u
 #define SERIALAPI_HOST_HIBERNATION_LIST_STATUS_NOT_ENOUGH_NODES_FRAGMENT 0x03u
 #define SERIALAPI_HOST_HIBERNATION_LIST_STATUS_WRONG_FRAME_INDEX         0x04u
 #define SERIALAPI_HOST_HIBERNATION_LIST_STATUS_WRONG_TOTAL_FRAMES        0x05u
 
 /* Important Devices Clear — NCP command status byte */
 #define SERIALAPI_HOST_HIBERNATION_CLEAR_STATUS_NOT_ACCEPTED_OR_ERROR    0x00u
 #define SERIALAPI_HOST_HIBERNATION_CLEAR_STATUS_SUCCESS_MIN              0x01u /* 0x01..0xFF: accepted on wire */
 
/*
 * Host hibernation (Serial API FUNC_ID_SERIAL_API_HOST_HIBERNATION, 0xF0 +
 *     sub-command).
 * @return Summary: SERIALAPI_HOST_HIBERNATION_ERR_* on host failure; otherwise
 *     SERIALAPI_HOST_HIBERNATION_OK and/or NCP codes — see SERIALAPI_HOST_HIBERNATION_*
 *     macros at top of header.
 */

/*
 * @brief Push the important-device list to the NCP, splitting into multiple
 *     frames when needed.
 * @param entries
 *     Node list payload (gateway-defined; typically node id + keep-alive window
 *     per entry).
 * @param count Number of entries.
 * @param max_frame_length
 *     Maximum host-hibernation payload length per frame (usually from module
 *     capabilities). Must be 8..BUF_SIZE or the call fails with
 *     SERIALAPI_HOST_HIBERNATION_ERR_INVALID_ARG.
 * @return
 *   - SERIALAPI_HOST_HIBERNATION_OK: all frames accepted (same value as
 *     SERIALAPI_HOST_HIBERNATION_LIST_STATUS_SUCCESS).
 *   - SERIALAPI_HOST_HIBERNATION_LIST_STATUS_* (0x01..0x05): first NCP command
 *     status if a frame is rejected.
 *   - SERIALAPI_HOST_HIBERNATION_ERR_*: transport, bad response, or invalid
 *     argument on the host.
 */
int SerialAPI_SendImportantDeviceList(const void *entries, uint16_t count,
    uint8_t max_frame_length);

/*
 * @brief Clear the important-device list stored on the NCP.
 * @return
 *   - SERIALAPI_HOST_HIBERNATION_CLEAR_STATUS_NOT_ACCEPTED_OR_ERROR: NCP did not
 *     accept the clear.
 *   - SERIALAPI_HOST_HIBERNATION_CLEAR_STATUS_SUCCESS_MIN..0xFF: clear accepted.
 *   - SERIALAPI_HOST_HIBERNATION_ERR_*: host could not complete the exchange.
 */
int SerialAPI_SendImportantDevicesClear(void);

/*
 * @brief Tell the NCP whether the host is awake or about to sleep.
 * @param host_state
 *     Host state on wire (0x00 = awake, 0x01 = going to sleep). After sleep,
 *     clear the important-device list before reporting awake again.
 * @param severity_level
 *     Minimum severity of frames needed to wake the host. Ignored by NCP when
 *     host_state is not 0x01 (going to sleep).
 * @param ncp_severity_level
 *     If non-NULL, filled with the NCP's configured severity threshold from
 *     the response frame.
 * @return SERIALAPI_HOST_HIBERNATION_OK on success, or
 *     SERIALAPI_HOST_HIBERNATION_ERR_* on failure.
 */
int SerialAPI_NotifyHostState(uint8_t host_state, uint8_t severity_level,
    uint8_t *ncp_severity_level);

/*
 * @brief Ask the NCP to send a wakeup report (transport-level acknowledged
 *     request only).
 * @return SERIALAPI_HOST_HIBERNATION_OK, or SERIALAPI_HOST_HIBERNATION_ERR_*.
 */
int SerialAPI_RequestWakeupReport(void);

/*
 * @brief Read important-device list limits: max devices and max list frame length
 *     supported by the module.
 * @param max_devices
 *     Filled with the NCP maximum important-device count (non-NULL).
 * @param max_frame_length
 *     Filled with the NCP maximum list payload length for list frames (non-NULL).
 * @return SERIALAPI_HOST_HIBERNATION_OK with outputs set, or
 *     SERIALAPI_HOST_HIBERNATION_ERR_*.
 */
int SerialAPI_GetModuleCapabilities(uint8_t *max_devices, uint8_t *max_frame_length);

/*
 * @brief Request S2 message count information; use node id 0 to query all nodes.
 *     Delivers data via OnNcpS2CountSync and may use multiple responses.
 * @param node_id Target node, or 0 for all.
 * @return SERIALAPI_HOST_HIBERNATION_OK when the exchange completes, or
 *     SERIALAPI_HOST_HIBERNATION_ERR_* (counts arrive only through the callback).
 */
int SerialAPI_RequestS2MessageCountList(uint16_t node_id);

#endif /* SERIAL_API_H_ */
