/* © 2014  Silicon Laboratories Inc. */

#ifndef CLASICZIPNODE_H_
#define CLASICZIPNODE_H_
#include "Serialapi.h"
#include "contiki-net.h"
#include "Bridge.h"
#include "ZW_SendDataAppl.h"
#include "ZW_udp_server.h"

/**
 * \defgroup ip_emulation IP Emulation Layer
 *
 * This layer implements IP emulation for all Z-Wave nodes.
 *
 * \section ip_support_for_zwave_nodes IP Support for Z-Wave Nodes
 *
 * - The Z/IP Gateway must perform an inspection of each IP Packet received to
 *   check if the receiving (destination) node in the packet is a classic Z-Wave
 *   node. Z/IP Gateway does not support IP-enabled nodes in this release. If
 *   the receiving (destination) node in the IP packet is a Z-Wave node, the
 *   Z/IP Gateway must intercept all IP packets and if possible, emulate the
 *   requested service by using equivalent features of Z-Wave.
 *
 * - The Z/IP Gateway MUST emulate IP Ping (ICMP Echo). If a Ping request is
 *   received by the Z/IP Gateway for a Z-Wave node, the Z/IP Gateway must use
 *   the Z-Wave NOP command to emulate the ping, and respond using ICMP reply to
 *   the requesting address.
 *
 * - The Z/IP Gateway MUST forward the Z-Wave payload of any Z/IP Packet
 *   received for a classic Z-Wave node to the node. It must also handle Z/IP
 *   ACK, and perform same ACK check on Z-Wave if requested.
 *
 * \section asproxy Association Proxy
 *
 * The IP Association Proxy extends your network beyond the Z-Wave PAN, by
 * allowing Z-Wave devices to communicate with any IPv6 enabled device that
 * supports the Z/IP Framework on either another Z-Wave network or a separate IP
 * application. IP Associations are created between two Z/IP resources
 * identified by a resource name – which may be resolved to an IP address and an
 * endpoint ID. The Full Z/IP Gateway emulates the IP properties of Z/IP Nodes
 * for classic nodes residing in the network:
 *
 * - Association from one simple node to another: Send a normal Association Set
 *   to the association source in a normal frame.
 *
 * - Association from a multichannel endpoint to another multichannel endpoint:
 *   Send a multichannel Association Set to the association source encapsulated
 *   in a multichannel frame.
 *
 * - Association from a multichannel endpoint to a simple node: Send an
 *   Association Set to the association source encapsulated in a multichannel
 *   frame.
 *
 * - Association from a simple node to a multichannel endpoint: Send an
 *   Association Set to the association source encapsulated in a normal frame.
 *   The association targets a virtual node in the Z/IP Gateway. Create a
 *   companion association from the virtual node to the multichannel endpoint.
 *
 * \section temp_ip_assoc Temporary IP Associations
 *
 * Temporary IP associations are automatically created when a Z/IP client
 * connects to the gateway. It associates the IP address and port of the Z/IP
 * client to one of the gateway's virtual nodes in the Z-Wave network. There's a
 * limit on the number of concurrent temporary associations.
 *
 * See the description for temp_association_t and #MAX_TEMP_ASSOCIATIONS.
 *
 * @{
 */

extern uint8_t backup_buf[UIP_BUFSIZE];
extern uint16_t backup_len;
extern uint16_t zip_payload_len;
extern zwave_connection_t zw;

#define BACKUP_UIP_IP_BUF ((struct uip_ip_hdr *)backup_buf)
#define BACKUP_UIP_UDP_BUF ((struct uip_udp_hdr *)&backup_buf[UIP_IPH_LEN])
#define BACKUP_ZIP_PKT_BUF ((ZW_COMMAND_ZIP_PACKET *)&backup_buf[UIP_IPUDPH_LEN])

/**
 * Input for IP packages destined for Classic Z-Wave nodes.
 *
 * Assumes the package in uip_buf has node as it destination.
 * This routine will do asynchronous processing, i.e., it will return before any IP reply
 * is ready.
 *
 * If ClassicZIPNode_input() is already busy, it invokes completedFunc
 * immediately with status \ref TRANSMIT_COMPLETE_REQUEUE.
 *
 * @param node node to send this package to
 * @param completedFunc callback to be called when frame has been sent.
 * @param bFromMailbox Boolean should be TRUE if this frame is sent from mailbox.
 * @param bRequeued Boolean should be TRUE if this frame has been re-queued to node-queue already.
 * @return true if the package has been processed.
 */
int ClassicZIPNode_input(nodeid_t node, void (*completedFunc)(BYTE, BYTE *, uint16_t),
                         int bFromMailbox, int bRequeued);

/**
 * Call the callback function registered with ClassicZIPNode_input().
 *
 * \note Since this function is also used as a callback from ZW_SendDataZIP(),
 * its signature must be of type ZW_SendDataAppl_Callback_t even though it adds
 * two unused parameters.
 *
 * @param bStatus Status code to pass to callback function
 * @param usr     Not used
 * @param t       Not used
 */
void ClassicZIPNode_CallSendCompleted_cb(BYTE bStatus, void* usr, TX_STATUS_TYPE *t);

/**
 * Receive de-crypted UDP frames from the DTLS service.
 *
 * When ClassicZIPNode_dec_input() prepares to send a frame, it checks
 * the following three conditions to determine if the gateway is ready
 * to send:
 * - is Network Management busy (except probe_by_SIS)
 * - is Resource Directory busy
 * - is Mailbox busy and fromMailbox NOT set on the frame.
 *
 * The Mailbox busy condition obviously needs this extra rule: if the
 * frame that ClassicZIPNode is trying to send is the one from
 * Mailbox, it should be sent.
 *
 * If these tests fail, ClassicZIPNode_dec_input() re-queues the frame
 * with TRANSMIT_COMPLETE_REQUEUE_QUEUED.
 */
int ClassicZIPNode_dec_input(void *data, int len);

/**
 * Input a UDP package to a classic node.
 * @param c The UDP connection
 * @param data pointer to the UDP data.
 * @param len the length of the UDP payload
 * @param secure was the command received secure
 */
int ClassicZIPUDP_input(struct uip_udp_conn *c, u8_t *data, u16_t len, BOOL secure);

/**
 * Wrapper for ZW_SendDataAppl() that saves result to private handle in ClassicZipNode module.
 *
 * For description of parameters and return value see ZW_SendDataAppl().
 */
uint8_t ClassicZIPNode_SendDataAppl(ts_param_t *p,
                                    const void *pData,
                                    uint16_t dataLength,
                                    ZW_SendDataAppl_Callback_t callback,
                                    void *user);

/**
 * Abort the currently active session (if any) started with ClassicZIPNode_SendDataAppl().
 *
 */
void ClassicZIPNode_AbortSending(void);

/**
 * Init data structures.
 */
void ClassicZIPNode_init();

int ClassicZIPNode_UnsolicitedHandler(ts_param_t *p,
                                      ZW_APPLICATION_TX_BUFFER *pCmd, BYTE cmdLength, uip_ipaddr_t *pDestAddr);
/*
 * Send a IP encapsulated to an IP host, from sourceNode to port@destIP.
 */
void ClassicZIPNode_SendUnsolicited(zwave_connection_t *__c,
                                    ZW_APPLICATION_TX_BUFFER *pCmd,
                                    BYTE cmdLength, uip_ip6addr_t *destIP, u16_t port, BOOL bAck);

void send_zip_ack(uint8_t bStatus);
void report_send_completed(uint8_t bStatus);

uint8_t is_local_address(uip_ipaddr_t *ip);

/**
 * Set the txOptions used in transmissions.
 */
void ClassicZIPNode_setTXOptions(uint8_t opt);

void ClassicZIPNode_addTXOptions(uint8_t opt);

uint8_t ClassicZIPNode_getTXOptions(void);

uint8_t *ClassicZIPNode_getTXBuf(void);

void ClassicZIPNode_sendNACK(BOOL __sendAck);

void CreateLogicalUDP(ts_param_t *p, unsigned char *pCmd, uint8_t cmdLength);

void SendUDPStatus(int flags0, int flags1, zwave_connection_t *c);

void ZW_SendData_UDP(zwave_connection_t *c,
                     const BYTE *dataptr,
                     u16_t datalen,
                     void (*cbFunc)(BYTE, void *user),
                     BOOL ackreq);
/**
 * @}
 */
#endif /* CLASICZIPNODE_H_ */
