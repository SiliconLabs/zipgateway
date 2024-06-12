/* © 2014 Silicon Laboratories Inc.
 */

#ifndef ZW_SENDDATAAPPL_H_
#define ZW_SENDDATAAPPL_H_
#include <stdbool.h>
#include <ZW_typedefs.h>
#include <ZW_transport_api.h>
#include "zgw_nodemask.h"
#include "RD_types.h"

/** \ingroup transport
 * \defgroup Send_Data_Appl Send Data Appl
 *
 * SendDataAppl is the top level module, which sends encapsulated frames on
 * the Z-Wave network. All outgoing data from the GW to the HAN goes through
 * this module.
 *
 * The module consists of the application level queue, in which
 * frames are stored before transport layer encapsulation, e.g., un-encrypted
 * frames, and the low-level queue, which holds frames
 * that will be sent over the Z-Wave radio without further
 * processing. Transport modules such as S0,S2 and Transport Service must only
 * use the low-level queue. They should never call the serial API directly.
 *
 * @{
 */

/**
 * Enumeration of security schemes, this must map to the bit numbers in NODE_FLAGS
 */
typedef enum SECURITY_SCHEME {
  /** Do not encrypt message */
  NO_SCHEME = 0xFF,

  /** Let the SendDataAppl layer decide how to send the message */
  AUTO_SCHEME = 0xFE,

  NET_SCHEME = 0xFC, //Highest network scheme
  USE_CRC16 = 0xFD, //Force use of CRC16, not really a security scheme..

  /** Send the message with security Scheme 0 */
  SECURITY_SCHEME_0 = 7,

  /** Send the message with security Scheme 2 */
  SECURITY_SCHEME_2_UNAUTHENTICATED = 1,
  SECURITY_SCHEME_2_AUTHENTICATED = 2,
  SECURITY_SCHEME_2_ACCESS = 3,
  SECURITY_SCHEME_UDP = 4

} security_scheme_t ;


//typedef uint8_t node_list_t[ZW_MAX_NODES/8];


/**
 * Structure describing how a package was received / should be transmitted
 */
typedef struct tx_param {
  /**
   * Source node
   */
  nodeid_t snode;
  /**
   * Destination node
   */
  nodeid_t dnode;

  /**
   * Source endpoint
   */
  uint8_t sendpoint;

  /**
   * Destination endpoint
   */
  uint8_t dendpoint;

  /**
   * Transmit flags
   * see txOptions in \ref ZW_SendData_Bridge
   */
  uint8_t tx_flags;

  /**
   * Receive flags
   * see rxStatus in \ref SerialAPI_Callbacks::ApplicationCommandHandler
   */
  uint8_t rx_flags;

  /**
   * Security scheme used for this package
   */
  security_scheme_t scheme; // Security scheme

  /**
   * Nodemask used when sending multicast frames.
   */
  nodemask_t node_list;
  /**
   * True if this is a multicast with followup enabled.
   */
  BOOL is_mcast_with_folloup;

  /**
   * Force verify delivery
   */
  BOOL force_verify_delivery;

  /**
   */
  BOOL is_multicommand;

  /**
   *   Maximum time ticks this transmission is allowed to spend in queue waiting to be processed by SerialAPI before it is dropped.
   *   There are CLOCK_SECOND ticks in a second. 0 means never drop.
   */
  uint16_t discard_timeout;    /* not using clock_time_t to avoid dependency on contiki-conf.h */
} ts_param_t;


/**
 * Set standard transmit parameters.
 * @param p
 * @param dnode
 */
void ts_set_std(ts_param_t* p, nodeid_t dnode);

/**
 * Copy transport parameter structure.
 * @param dst
 * @param src
 */
void ts_param_copy(ts_param_t* dst, const ts_param_t* src);

/**
 * Convert transport parameters to a reply.
 * @param dst
 * @param src
 */
void ts_param_make_reply(ts_param_t* dst, const ts_param_t* src);

/**
 * \Return true if source and destinations are identical.
 */
uint8_t ts_param_cmp(ts_param_t* a1, const ts_param_t* a2);

/**
 * \param txStatus, see \ref ZW_SendData
 * \param user User-defined pointer
 */
typedef void( *ZW_SendDataAppl_Callback_t)(BYTE txStatus,void* user, TX_STATUS_TYPE *txStatEx);

/**
 * Old style send data callback
 * @param txStatus  see \ref ZW_SendData
 */
typedef void( *ZW_SendData_Callback_t)(BYTE txStatus);



/**
 * Send a Z-Wave frame with the right encapsulation, such as Multichannel, Security0, Security2, CRC16, Transport
 * Service or no encapsulation at all automatically. This is the main sending function for application data used in the GW.
 * The decision of encapsulation is done based in the parameter given
 * in \a p, \a dataLength and the know capabilities of the destination node, i.e., this module relies on
 * data collected by the resource directory.
 *
 * If the frame sending FSM detects that a GET message has been queued, it will introduce a short pause
 * before sending the next frame, giving room the the report sent in the opposite direction. If no
 * response is received from the node, the pause will be 25 0ms + TX time. The pausing does not
 * apply to low-level encapsulation messages (NONCE_GET, etc. ), because they are put in the low-level
 * sending queue.
 *
 * \param p Parameters used for the transmission
 * \param pData Data to send
 * \param dataLength Length of frame
 * \param callback function to be called in completion
 * \param user user defined pointer which will provided a the second argument of the callback
 * see \ref ZW_SendDataAppl_Callback_t
 *
 */
uint8_t ZW_SendDataAppl(ts_param_t* p,
  const void *pData,
  uint16_t  dataLength,
  ZW_SendDataAppl_Callback_t callback,
  void* user) __attribute__((warn_unused_result));

/** Check that \ref Send_Data_Appl is idle.
 *
 * SendDataAppl is idle if there is nothing in the queues and no current sessions.
 *
 * \return True if SendDataAppl has nothing to send, false otherwise.
 */
bool ZW_SendDataAppl_idle(void);

/**
 * Initialize senddataAppl module.
 */
void ZW_SendDataAppl_init();

/**
 * Abort the transmission.
 */
void
ZW_SendDataApplAbort(uint8_t handle ) ;


/**
 * Return true if a has a larger or equal scheme to b.
 */
int scheme_compare(security_scheme_t a, security_scheme_t b);

/* Return the highest security scheme.*/
security_scheme_t highest_scheme(uint8_t scheme_mask);


/**
 *
 * Notify the Lower sendData layers that a frame is received.
 * This is used to stop the backoff timer, if a frame is received from a particular node.
 */
void ZW_SendDataAppl_FrameRX_Notify(const ts_param_t *p, const uint8_t* frame, uint16_t length);


/** @} */

#endif /* ZW_SENDDATAAPPL_H_ */
