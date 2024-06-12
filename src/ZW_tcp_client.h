/* © 2014 Silicon Laboratories Inc.
 */

#ifndef ZW_TCP_CLIENT_H_
#define ZW_TCP_CLIENT_H_
/** \ingroup processes
 * \defgroup Portal_handler Z/IP portal connection processes
 * The Z/IP Gateway provides a means of Remote Access through a secure
 * Transport Layer Security (TLS) v1.1 based Transmission Control Protocol (TCP)/IPv4
 * connection over port 44123 to a portal, with a Domain Name System (DNS) resolvable
 * Uniform Resource Locator (URL), outside the home network, synchronized by an internal
 * Network Time Protocol (NTP) client.
 *
 * It is the Z/IP Gateway that initiates this connection to the portal; attempting
 * connection every 5 seconds on failure.  On connection, the Z/IP Gateway sends a
 * keep-alive every 5 seconds.  On some platforms it may take a considerable amount
 * of time to establish the secure tunnel, as it uses a 2-way handshake with RSA-1024
 * certificates with Secure Hash Algorithm (SHA) -1 digest.  If the connection breaks
 * down, the Z/IP Gateway supports session resumption within 24 hours in less than
 * 10 seconds.  After connection has been set up, Z/IP packets over this connection are
 * encrypted with Advanced Encryption Standard (AES) 128.
 * @{
 */
PROCESS_NAME(zip_tcp_client_process);
PROCESS_NAME(resolv_process);

void send_ipv6_to_tcp_ssl_client(void);

#define ZIPR_NOT_READY				0x0
#define HANDSHAKE_DONE				0x1
#define ZIPR_READY					0x2

#define IPV6_ADDR_LEN	  16

extern const uip_lladdr_t tun_lladdr;
extern uint8_t gisTnlPacket;

#define TUNNEL_CONFIG_PACKET_LEN   33  //Unsolicited Ipv6 addr ddr:Tunnel IPv6 addr (16 Bytes) + Tunnel Prefix len(1 Byte)

void TcpTunnel_ReStart(void);
uint8_t parse_portal_config(uint8_t *buf, uint8_t len);
extern uint8_t gisZIPRReady;

/** @} */
#endif /* ZW_TCP_CLIENT_H_ */
