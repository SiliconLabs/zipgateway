/* © 2014 Silicon Laboratories Inc.
 */
#ifndef ZW_UDP_SERVER_H_
#define ZW_UDP_SERVER_H_

/** \ingroup processes
 * \defgroup ZIP_Udp Z/IP UDP process
 * Handles the all Z/IP inbound and outbout UDP communication
 * Manages the Z/IP sessions for the Zipgateway
 * @{
 */
/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/
#include "net/uip.h"
#include "ZW_SendDataAppl.h"
#define ZWAVE_PORT 4123
#define DTLS_PORT 41230

typedef struct {
  union {
    /**
     * Convenience attribute.
     */
    struct uip_udp_conn conn;
    struct {
      /**
       * Local ip address
       */
      uip_ipaddr_t lipaddr;
      /**
       * Remote IP address
       */
      uip_ipaddr_t ripaddr;
      /**
       *  The local port number in network byte order.
       */
      u16_t lport;
      /**
       * The remote port number in network byte order.
       */
      u16_t rport;
    };
  };
  /**
   * remote endpoint
   */
  uint8_t rendpoint;

  /**
   * local endpoint
   *
   */
  uint8_t lendpoint;
  /**
   * Sequence number when sending UDP frames
   */
  uint8_t seq;
  /**
   * Security scheme when sending Z-Wave frames
   */
  security_scheme_t scheme;
  /**
   * rx flags when receive as a Z-Wave frame
   */
  u8_t rx_flags;
  /**
   * tx flags when sending as a Z-Wave frame
   */
  u8_t tx_flags;
} zwave_udp_session_t;

/**
 * Structure describing a Z-Wave or Z/IP connection, with source and destination
 * ips/nodids/endpoint. Together with specific parameter such as receiver flags.
 */
typedef zwave_udp_session_t zwave_connection_t;

/**
 * Returns true if the two connections are identical, i has the same source and destination addresses.
 */
int zwave_connection_compare(zwave_connection_t* a,zwave_connection_t *b);


typedef enum {
  RES_ACK,RES_NAK,RES_WAITNG,RES_OPT_ERR,
} zwave_udp_response_t;

/**
 * Send a udp ACK NAK or waiting
 */
void send_udp_ack(zwave_udp_session_t* s, zwave_udp_response_t res  );

/**
 * Wrapper function to Send package at an ordinary Z-wave package or as a
 * Z/IP UDP package depending on destination address.
 *
 * If the first 15 bytes of the destination address is all zeroes then
 * the package is sent as a ordinary Z-wave package. In this case
 * \see ZW_SendData_UDP
 * \see ZW_SendData
 *
 * \param c connection describing the source and destination for this package
 * \param dataptr pointer to the data being sent
 * \param datalen length of the data being sent
 * \param cbFunc Callback to be called when transmission is complete
 */
extern void
ZW_SendDataZIP(zwave_connection_t *c,  const  void *dataptr, u16_t datalen,
    ZW_SendDataAppl_Callback_t cbFunc);


/**
 * Convenience function which calls \ref ZW_SendDataZIP
 *
 * \param src Source ip of package
 * \param  dst Destination ip of package
 * \param  port Destination port to send to
 * \param dataptr pointer to the data being sent
 * \param datalen length of the data being sent
 * \param cbFunc Callback to be called when transmission is complete
 * \deprecated
 */
extern void __ZW_SendDataZIP(
    uip_ip6addr_t* src,uip_ip6addr_t* dst, WORD port,
    const void *dataptr,
    u16_t datalen,
    ZW_SendDataAppl_Callback_t cbFunc);


/**
 * Convenience function which calls \ref ZW_SendDataZIP_ack
 *
 * \param src Source ip of package
 * \param  dst Destination ip of package
 * \param  port Destination port to send to in network byte order
 * \param dataptr pointer to the data being sent
 * \param datalen length of the data being sent
 * \param cbFunc Callback to be called when transmission is complete
 * \deprecated
 */
extern void __ZW_SendDataZIP_ack(
    uip_ip6addr_t* src,uip_ip6addr_t* dst, WORD port,
    const void *dataptr,
    u16_t datalen,
    ZW_SendDataAppl_Callback_t cbFunc);

/**
 * Send a Z-Wave udp frame, but with the ACK flag set. No retransmission will be attempted
 * See \ref ZW_SendDataZIP
 */
extern void
ZW_SendDataZIP_ack(zwave_connection_t *c,const void *dataptr, u8_t datalen, void
    (*cbFunc)(u8_t, void*, TX_STATUS_TYPE *));

extern void (*uip_completedFunc)(u8_t,void*);


/**
 * Create a udp con structure from the package in uip_buf
 */
struct uip_udp_conn* get_udp_conn();

/**
 * \return TRUE if the specified address is a Z-Wave address ie a 0\::node address
 */
#define ZW_IsZWAddr(a)     \
  (                        \
   (((a)->u8[1]) == 0) &&                        \
   (((a)->u16[1]) == 0) &&                          \
   (((a)->u16[2]) == 0) &&                          \
   (((a)->u16[3]) == 0) &&                          \
   (((a)->u16[4]) == 0) &&                          \
   (((a)->u16[5]) == 0) &&                          \
   (((a)->u16[6]) == 0) &&                          \
   (((a)->u8[14]) == 0) &&                          \
   (((a)->u8[15]) == 0))

/**
 * Input function for RAW Z-Wave udp frames. This function parses the COMMAND_CLASS_ZIP header
 * and manages UDP sessions.
 * \param c UDP connection the frame ware recieved on.
 * \param data pointer to the udp data
 * \param len length of the udp data
 * \param received_secure TRUE if this was a UDP frame which has been decrypted with DTLS.
 */
void UDPCommandHandler(struct uip_udp_conn* c,const u8_t* data, u16_t len,u8_t received_secure);


/**
 * Send package using the given UDP connection. The package may be DTLS encrypted.
 *
 * \param c UDP connection the frame ware recieved on.
 * \param data pointer to the udp data
 * \param len length of the udp data
 * @param cbFunc to callback function
 * @param user: user pointer
 */

void udp_send_wrap(struct uip_udp_conn* c, const void* data, u16_t len, void (*cbFunc)(BYTE, void*), void *user);


typedef struct
{
  u8_t type;
  u8_t length;
  u8_t value[1];
} tlv_t;


/**
 * Convert zwave udp packet EFI extensions to a security scheme
 * \param ext1 fist ext byte
 * \param ext2 second ext byte
 */
security_scheme_t efi_to_shceme(uint8_t ext1, uint8_t ext2);



/**
 * Check if we have anything outstanding in the IPv4 queue
 */
void
udp_server_check_ipv4_queue();


/**
 * This process handles all Z/ZIP UDP communication.
 */
PROCESS_NAME(udp_server_process);

/** @} */
#endif /* ZW_UDP_SERVER_H_ */

