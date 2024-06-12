/* © 2014 Silicon Laboratories Inc.
 */

/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/
#include "Serialapi.h"
#include "zw_network_info.h"
#include "zip_router_ipv6_utils.h" /* nodeOfIP, ipOfNode */
#include "ZW_tcp_client.h" /* gisTnlPacket, tun_lladdr */
#include "ZW_ZIPApplication.h" /* net_scheme */

#include <ZW_typedefs.h>
#include <ZW_udp_server.h>
#include <ZW_classcmd.h>
#include <ZW_zip_classcmd.h>
#include <security_layer.h>
#include "NodeCache.h"

#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"
#include "net/uip-udp-packet.h"
#include "net/uip-debug.h"
#include "sys/ctimer.h"

#include "ipv46_addr.h"
#include "DTLS_server.h"
#include "ZIP_Router_logging.h"
/****************************************************************************/
/*                     EXPORTED TYPES and DEFINITIONS                       */
/****************************************************************************/

/****************************************************************************/
/*                      PRIVATE TYPES and DEFINITIONS                       */
/****************************************************************************/
#define l3_udp_hdr_len (UIP_IPUDPH_LEN + uip_ext_len)

/****************************************************************************/
/*                              EXPORTED DATA                               */
/****************************************************************************/

/****************************************************************************/
/*                              PRIVATE DATA                                */
/****************************************************************************/
/****************************************************************************/
/*                              PRIVATE FUNCTIONS                           */
/****************************************************************************/

/****************************************************************************/
/*                              EXPORTED FUNCTIONS                          */
/****************************************************************************/

#define DEBUG DEBUG_PRINT
#include "net/uip-udp-packet.h"
#include "net/uip-debug.h"
#include "net/uip.h"

#include "Serialapi.h"

#include "ZW_classcmd_ex.h"
#include <ZW_classcmd.h>
#include "ResourceDirectory.h"

#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP_UDP_BUF  ((struct uip_udp_hdr *)&uip_buf[uip_l2_l3_hdr_len])

#define MAX_UDP_PAYLOAD_LEN 512
//#define PRINTF printf
static struct uip_udp_conn *server_conn;
//static struct uip_udp_conn *server_send_conn;

static struct ctimer zw_udp_timer;
struct uip_packetqueue_handle async_queue;

/* This structure saves metadata for the UDP packet received and saved along 
 * with udp payload (Z-wave Packet without ZIP header) by 
 * uip_packetqueue_alloc() in te async_queue
 */
struct async_state
{
  uint8_t ack_req;
  zwave_connection_t conn;
//ZW_APPLICATION_META_TX_BUFFER rxBuf;
};

/**
 * Queue of unsolicited packets from PAN nodes which cannot be delivered until the
 * source has obtained a DHCP IPv4 address.
 */
static struct uip_packetqueue_handle ipv4_packet_q = {NULL};

/** Format of the packets in ipv4_packet_q */
struct ipv4_packet_buffer {
  struct uip_udp_conn c;
  u16_t len;
  uint8_t data[1];
};


/**
 * UDP transmit session
 */
struct udp_tx_session {
  list_t next;
  /*
  u8_t* data;
  u8_t* data_len;
  u8_t* count;
  */
  ZW_SendDataAppl_Callback_t cb;
  void* user;
  u8_t seq;
  u8_t src_ep;
  u8_t dst_ep;
  uip_ip6addr_t dst_ip;
  uip_ip6addr_t src_ip;
  u16_t dst_port; //Destination port in network byte order
  struct ctimer timeout;
};

MEMB(udp_tx_sessions_memb,struct udp_tx_session,4);
LIST(udp_tx_sessions_list);


int zwave_connection_compare(zwave_connection_t* a, zwave_connection_t* b) {
  return
      uip_ipaddr_cmp(&a->conn.ripaddr,&b->conn.ripaddr) &&
      uip_ipaddr_cmp(&a->conn.sipaddr,&b->conn.sipaddr) &&
      (a->conn.lport == b->conn.lport) &&
      (a->conn.rport == b->conn.rport) &&
      (a->rendpoint == b->rendpoint) &&
      (a->lendpoint == b->lendpoint)
      ;
}

/*
 * Sequence number used in transmissions
 */
static BYTE seqNo = 0;

PROCESS(udp_server_process, "UDP server process");

/**
 * Create a udp con structure from the package in uip_buf
 */
struct uip_udp_conn* get_udp_conn() {
  static struct uip_udp_conn c;

  memset(&c,0,sizeof(c));
  c.lport = UIP_UDP_BUF->destport;
  c.rport = UIP_UDP_BUF->srcport;
  uip_ipaddr_copy(&c.ripaddr, &UIP_IP_BUF->srcipaddr);
  uip_ipaddr_copy(&c.sipaddr, &UIP_IP_BUF->destipaddr);
//  ERR_PRINTF("get_udp_conn: port: %d\n", UIP_HTONS(UIP_UDP_BUF->destport));
  return &c;
}

security_scheme_t efi_to_shceme(uint8_t ext1, uint8_t ext2) {
  switch (ext1) {
  case   EFI_SEC_LEVEL_NONE:
    if(ext2 & EFI_FLAG_CRC16) {
      return USE_CRC16;
    } else {
      return NO_SCHEME;
    }
  case   EFI_SEC_S0:
    return SECURITY_SCHEME_0;
  case   EFI_SEC_S2_UNAUTHENTICATED:
    return SECURITY_SCHEME_2_UNAUTHENTICATED;
  case   EFI_SEC_S2_AUTHENTICATED:
    return SECURITY_SCHEME_2_AUTHENTICATED;
  case   EFI_SEC_S2_ACCESS:
    return SECURITY_SCHEME_2_ACCESS;
  }

  WRN_PRINTF("Invalid encapsulation format info\n");
  return NO_SCHEME;
}

/*---------------------------------------------------------------------------*/
/* Max size of ZIP Header incl longest hdr extension we might add*/
/* Extensions currently accounted for: EFI and MULTICAST */
#define MAX_ZIP_HEADER_SIZE (ZIP_HEADER_SIZE + 5)

void
ZW_SendData_UDP(zwave_connection_t* c,
    const BYTE *dataptr, u16_t datalen, void(*cbFunc)(BYTE, void*), void *user, BOOL ackreq) //make CC_REENTRANT_ARG to place the local variables in the stack
{
  static struct uip_udp_conn client_conn;
  security_scheme_t scheme;;
  ZW_COMMAND_ZIP_PACKET *pZipPacket;
  char udp_buffer[MAX_UDP_PAYLOAD_LEN + MAX_ZIP_HEADER_SIZE];
  unsigned int i = 0;

  if (datalen + MAX_ZIP_HEADER_SIZE > sizeof(udp_buffer))
  {
    ERR_PRINTF("ZW_SendData_UDP: Package is too large.\n");
    return;
  }

  pZipPacket = (ZW_COMMAND_ZIP_PACKET*) udp_buffer;
  pZipPacket->cmdClass = COMMAND_CLASS_ZIP;
  pZipPacket->cmd = COMMAND_ZIP_PACKET;
  pZipPacket->flags0 = ackreq ? ZIP_PACKET_FLAGS0_ACK_REQ : 0;
  if (datalen == 0) {
      /*For UDP mailbox ping */
      DBG_PRINTF("datalen is zero. No command included\n");
      pZipPacket->flags1 = ZIP_PACKET_FLAGS1_HDR_EXT_INCL;
  } else {
      pZipPacket->flags1 = ZIP_PACKET_FLAGS1_ZW_CMD_INCL | ZIP_PACKET_FLAGS1_HDR_EXT_INCL;
  }

  pZipPacket->sEndpoint = c->lendpoint;
  pZipPacket->dEndpoint = c->rendpoint;
  pZipPacket->seqNo = seqNo++;

  if(c->scheme != NO_SCHEME) {
    pZipPacket->flags1 |=ZIP_PACKET_FLAGS1_SECURE_ORIGIN;
  }

  i = 0;
  pZipPacket->payload[i++] = 0;  /* Filled when header is complete */
  pZipPacket->payload[i++] = 0x80 | ENCAPSULATION_FORMAT_INFO;
  pZipPacket->payload[i++] = 2;
  pZipPacket->payload[i++] = EFI_SEC_LEVEL_NONE;
  pZipPacket->payload[i++] = 0;

  if(c->scheme == SECURITY_SCHEME_UDP) {
    scheme = net_scheme;
  } else {
    scheme = c->scheme;
  }

  switch (scheme)
  {
  case USE_CRC16:
    pZipPacket->payload[4] = EFI_FLAG_CRC16;
    break;
  case SECURITY_SCHEME_0:
    pZipPacket->payload[3] = EFI_SEC_S0;
    break;
  case SECURITY_SCHEME_2_UNAUTHENTICATED:
    pZipPacket->payload[3] = EFI_SEC_S2_UNAUTHENTICATED;
    break;
  case SECURITY_SCHEME_2_AUTHENTICATED:
    pZipPacket->payload[3] = EFI_SEC_S2_AUTHENTICATED;
    break;
  case SECURITY_SCHEME_2_ACCESS:
    pZipPacket->payload[3] = EFI_SEC_S2_ACCESS;
    break;
  case NET_SCHEME:
  case SECURITY_SCHEME_UDP:
  case AUTO_SCHEME:
  case NO_SCHEME:
    break;
  }

  if (
      ((c->rx_flags & RECEIVE_STATUS_TYPE_MASK) ==  RECEIVE_STATUS_TYPE_MULTI)
      || ((c->rx_flags & RECEIVE_STATUS_TYPE_MASK) ==  RECEIVE_STATUS_TYPE_BROAD)
  )
  {
    pZipPacket->payload[i++] = ZWAVE_MULTICAST_ADDRESSING;
    pZipPacket->payload[i++] = 0;  /* This is a zero-length TLV */
  }

  pZipPacket->payload[0] = i;  /* Set header extension length (total of all TLVs) */

  memcpy(udp_buffer + ZIP_HEADER_SIZE+i, dataptr, datalen);


  client_conn = c->conn;

  udp_send_wrap(&client_conn, pZipPacket, ZIP_HEADER_SIZE + datalen + i, cbFunc, user);
}
void
udp_server_check_ipv4_queue()
{
  struct ipv4_packet_buffer*pkg_buf;
  uip_ipv4addr_t addr;

  while (uip_packetqueue_buf(&ipv4_packet_q))
  {
    pkg_buf = ( struct ipv4_packet_buffer*)uip_packetqueue_buf(&ipv4_packet_q);
    nodeid_t node = nodeOfIP(&pkg_buf->c.sipaddr);

    if (ipv46nat_ipv4addr_of_node(&addr, node))
    {
      DBG_PRINTF("Packet sent from Ipv4 queue\n");
      udp_send_wrap(&pkg_buf->c, pkg_buf->data, pkg_buf->len, 0, 0);
      uip_packetqueue_pop(&ipv4_packet_q);
    } else {
      break;
    }
  }
}

void udp_send_wrap(struct uip_udp_conn* c, const void* buf, u16_t len, void (*cbFunc)(BYTE, void*), void *user)
{
  uint8_t tmp[1500];
  uip_ip6addr_t *nexthop;
  uip_ds6_route_t *r;
  uip_ds6_nbr_t* nbr;

  /*Check if the source node has an IPv4 address before sending*/
  if (uip_is_4to6_addr(&c->ripaddr)) {
    uip_ipv4addr_t addr;

    struct ipv4_packet_buffer* pkt_buf = (struct ipv4_packet_buffer*)tmp;

    nodeid_t node = nodeOfIP(&c->sipaddr);

    if(ipv46nat_ipv4addr_of_node(&addr,node)==0) {
      DBG_PRINTF("Node %i does not have an IPv4 address yet. Queuing packet\n",node);
      pkt_buf->c = *c;
      pkt_buf->len = len;
      memcpy(pkt_buf->data, buf,len);
      uip_packetqueue_alloc(&ipv4_packet_q,tmp,len + sizeof(struct ipv4_packet_buffer)-1,CLOCK_SECOND*60);
      return;
    }
  }

  c->ttl =64;

  /* Check if the destination address is to be reached through the portal
   * TODO it seems stange to have a complete nexthop determination here... */
  nbr = NULL;
  if (uip_ds6_is_addr_onlink(&c->ripaddr))
  {
    nbr = uip_ds6_nbr_lookup(&c->ripaddr);
  }
  else
  {
    r = uip_ds6_route_lookup(&c->ripaddr);
    if (r)
    {
      nexthop = &r->nexthop;
    }
    else
    {
      nexthop = uip_ds6_defrt_choose();
    }
    if (nexthop)
    {
      nbr = uip_ds6_nbr_lookup(nexthop);
    }
  }

  if (nbr)
  {
    /* Is portal the next hop? */
    if (memcmp(nbr->lladdr.addr, tun_lladdr.addr, 6) == 0) /* Is L2 address in portal */
    {
      /* Nexthop L2 is the portal */
      c->lport = UIP_HTONS(ZWAVE_PORT);
      if (cbFunc) {
        cbFunc(0, user);
      }
      return uip_udp_packet_send(c, buf, len);
    }
  }

  /* Do not set the udp timer for DTLS traffic here, its done in DTLS_server.c */
  if( c->lport != UIP_HTONS(DTLS_PORT) ) {
    if (cbFunc) {
      cbFunc(0, user);
    }
  }
  /* At this point all traffic to the portal should have been dispatched in the
   * if (nbr) above. So we proceed to dispatch data for PAN destination addresses.
   * */
  if (nodeOfIP(&c->ripaddr))
  {
    uip_udp_packet_send(c, buf, len);
    return;
  }

#ifdef DISABLE_DTLS
  uip_udp_packet_send(c, buf,len);
#else
  /* Since this is neither portal nor PAN traffic, is must be LAN
   * traffic. Most LAN traffic is dtls encrypted */
  if( c->lport == UIP_HTONS(DTLS_PORT) ) {
    dtls_send(c, buf,len,1, cbFunc, user);
  } else {
    uip_udp_packet_send(c, buf,len);
  }
#endif
}


void __ZW_SendDataZIP(
    uip_ip6addr_t* src,uip_ip6addr_t* dst, WORD port,
    const void *dataptr,
    u16_t datalen,
    ZW_SendDataAppl_Callback_t cbFunc) {
  zwave_connection_t c;

  memset(&c,0,sizeof(c));
  uip_ipaddr_copy(&c.ripaddr,dst);
  uip_ipaddr_copy(&c.lipaddr,src);
  c.rport = port;
#ifdef DISABLE_DTLS
  c.lport = UIP_HTONS(ZWAVE_PORT);
#else
  c.lport = UIP_HTONS(DTLS_PORT);
#endif

  ZW_SendDataZIP(&c,dataptr,datalen,cbFunc);
}


void __ZW_SendDataZIP_ack(
    uip_ip6addr_t* src,uip_ip6addr_t* dst, WORD port,
    const void *dataptr,
    u16_t datalen,
    ZW_SendDataAppl_Callback_t cbFunc)
{
    zwave_connection_t c;

    memset(&c,0,sizeof(c));
    uip_ipaddr_copy(&c.ripaddr,dst);
    uip_ipaddr_copy(&c.lipaddr,src);
    c.rport = port;
#ifdef DISABLE_DTLS
    c.lport = UIP_HTONS(ZWAVE_PORT);
#else
    c.lport = UIP_HTONS(DTLS_PORT);
#endif

    ZW_SendDataZIP_ack(&c,dataptr,datalen,cbFunc);
}


void
ZW_SendDataZIP(zwave_connection_t *c,  const  void *dataptr, u16_t datalen, void
    (*cbFunc)(BYTE, void* user, TX_STATUS_TYPE *))
{
  ts_param_t p;
  nodeid_t rnode;

  rnode = nodeOfIP(&c->ripaddr);

  if (rnode)
  {
    nodeid_t lnode = nodeOfIP(&c->lipaddr);

    ts_set_std(&p, rnode);
    p.scheme = c->scheme;
    p.snode = lnode;
    p.tx_flags = c->tx_flags;
    p.rx_flags = c->rx_flags;

    p.dendpoint = c->rendpoint;
    p.sendpoint = c->lendpoint;

    if(!ZW_SendDataAppl(&p, dataptr, datalen, cbFunc, 0) && cbFunc != NULL) {
      cbFunc(TRANSMIT_COMPLETE_FAIL,0, NULL);
    }
  }
  else
  {
    /* TODO: Stop typecasting the function pointer to take fewer args than the function
     * This works in practice, but is undefined behavior according to the C standard.
     * We rely on the calling convention of caller (not callee) balancing the stack.
     * This holds for x86 and ARM architectures, and most other archs in existence.
     * For arm, this works because we are using four or fewer arguments, so they are all
     * stored in registers [1].
     * See also https://stackoverflow.com/questions/188839/function-pointer-cast-to-different-signature
     *
     * [1] ARM IHI 0042F §5.5
     * */
    ZW_SendData_UDP(c, dataptr, datalen, (void (*)(BYTE, void*))cbFunc, 0 ,FALSE);
  }
}


static void ack_session_timeout(void* data) {
  struct udp_tx_session *s = (struct udp_tx_session*) data;

  if(s->cb) {
    s->cb(TRANSMIT_COMPLETE_FAIL,s->user, NULL);
  }
  list_remove(udp_tx_sessions_list,s);
  DBG_PRINTF("Freed UDP session slot(timeout): %d\n", memb_slot_number(&udp_tx_sessions_memb, s));
  memb_free(&udp_tx_sessions_memb,s);
}

/* This will find a member of udp_tx_sessions_list which has uip_udp_c = c and will start timer for that udp_tx_sessions_list */
static void cb_udp_send_done(BYTE b, void *user)
{
  struct udp_tx_session *s = user;
  if (s) {
    //Set the same udp session timer to half a second now as we got the callback
    ctimer_set(&s->timeout,CLOCK_CONF_SECOND/2,ack_session_timeout,s);
  }
}

void
ZW_SendDataZIP_ack(zwave_connection_t *c,const void *dataptr, u8_t datalen, void
    (*cbFunc)(u8_t, void*, TX_STATUS_TYPE *))
{
  if (ZW_IsZWAddr(&c->ripaddr))
  {
    ts_param_t p;
    ts_set_std(&p, c->ripaddr.u8[0]);
    p.scheme = c->scheme;
    if(!ZW_SendDataAppl(&p, dataptr, datalen & 0xFF, cbFunc, 0) && cbFunc != NULL) {
      cbFunc(TRANSMIT_COMPLETE_FAIL,0, NULL);
    }
  }
  else
  {
    struct udp_tx_session *s;
    s = memb_alloc(&udp_tx_sessions_memb);
    if(s) {
      DBG_PRINTF("Allocated UDP session slot: %d\n", memb_slot_number(&udp_tx_sessions_memb, s));
      s->seq = seqNo;
      uip_ipaddr_copy(&s->dst_ip,&c->ripaddr);
      uip_ipaddr_copy(&s->src_ip,&c->lipaddr);
      s->dst_port = c->rport;
      s->cb = cbFunc;
      s->src_ep =c->lendpoint;
      s->dst_ep =c->rendpoint;
      s->user = 0; /*FIXME */
      list_add(udp_tx_sessions_list,s);
      // Start the udp session timer here to cover the missing callback cb_udp_send_done()
      ctimer_set(&s->timeout,CLOCK_CONF_SECOND,ack_session_timeout,s);
      ZW_SendData_UDP(c, dataptr, datalen, cb_udp_send_done, (void *)s, TRUE);
    } else {
      WRN_PRINTF("No more free tx_sessions");
      if(cbFunc) {
        cbFunc(TRANSMIT_COMPLETE_FAIL,0, NULL);
      }
    }
  }
}

/*---------------------------------------------------------------------------*/
//static void
//solicited_reply_handler(void)
//{
//    if(uip_newdata()) {
//      ((char *)uip_appdata)[uip_datalen()] = 0;
//      //PRINTF("Client received solicited reply: '%s' from ", (char *)uip_appdata);
//      PRINT6ADDR(&UIP_IP_BUF->srcipaddr);
//      ZW_DEBUG_SEND_BYTE(':');
//      ZW_DEBUG_SEND_NUMW(uip_ntohs(UIP_UDP_BUF->srcport));
//      ZW_DEBUG_SEND_NL();
//      //PRINTF("to ");
//      PRINT6ADDR(&UIP_IP_BUF->destipaddr);
//      //PRINTF(":");
//      //PRINTF("%u", uip_ntohs(UIP_UDP_BUF->destport));
//      //PRINTF("\n");
//    }
//    else
//    {
//      PRINTF("Client error: received empty reply\n");
//    }
//}
/*---------------------------------------------------------------------------*/

static void
ZipND_CommandHandler(struct uip_udp_conn* c,const u8_t* data, u16_t len)
{
  ZW_ZIP_NODE_ADVERTISEMENT_V4_FRAME* zna =
      (ZW_ZIP_NODE_ADVERTISEMENT_V4_FRAME*) data;
  ZW_ZIP_INV_NODE_SOLICITATION_V4_FRAME* zina =
      (ZW_ZIP_INV_NODE_SOLICITATION_V4_FRAME*) data;
  nodeid_t nodeid = 0;

  if (len >= sizeof(ZW_ZIP_INV_NODE_SOLICITATION_V4_FRAME)) {
      nodeid = zina->extendedNodeidMSB <<8;
      nodeid |= zina->extendedNodeidLSB; 
  } else {
      nodeid = zina->nodeID;
  }
#ifdef IP_4_6_NAT
  uip_ipv4addr_t a;
#endif
  uint32_t homeID_hb;

  switch (zina->cmd)
  {
    case ZIP_INV_NODE_SOLICITATION: /*Look up ipaddr from node ID*/

#ifdef IP_4_6_NAT
      if(uip_is_4to6_addr(&c->ripaddr))
      {
        DBG_PRINTF("ND for 4to6 mapped address\n");
        if(ipv46nat_ipv4addr_of_node(&a,nodeid))
        {
          memset(&zna->ipv6Address1,0,10);
          zna->ipv6Address11 = 0xFF;
          zna->ipv6Address12 = 0xFF;
          zna->ipv6Address13 = a.u8[0];
          zna->ipv6Address14 = a.u8[1];
          zna->ipv6Address15 = a.u8[2];
          zna->ipv6Address16 = a.u8[3];
        }
        else
        {
          memset(&zna->ipv6Address1,0,16);
          zna->ipv6Address11 = 0xFF;
          zna->ipv6Address12 = 0xFF;
          ERR_PRINTF("IPv4 address of node %i not found\n", nodeid );
        }
      }
      else
#endif
    {
      DBG_PRINTF("ND for ipv6 address\n");
      ipOfNode((uip_ip6addr_t*) &(zna->ipv6Address1), nodeid);
    }
    if (is_lr_node(nodeid)) {
      zna->extendedNodeidMSB = nodeid >> 8;
      zna->extendedNodeidLSB = nodeid & 0xff;
      zna->nodeID = 0xff;
    } else {
      zna->extendedNodeidMSB = 0;
      zna->extendedNodeidLSB = 0;
      zna->nodeID = nodeid;
    }
     

      break;
    case ZIP_NODE_SOLICITATION: /*Lookup node from IP */
    {
#ifdef IP_4_6_NAT
      if(uip_is_4to6_addr((uip_ip6addr_t*)&(zna->ipv6Address1)))
      {

        nodeid = ipv46nat_get_nat_addr( (uip_ipv4addr_t*) &(zna->ipv6Address13 ) );
        if (nodeid > 0x100) {
            zna->extendedNodeidMSB = nodeid >> 8;
            zna->extendedNodeidLSB = nodeid & 0xff; 
            zna->nodeID = 0xff;
        } else {
            zna->extendedNodeidMSB = 0;
            zna->extendedNodeidLSB = 0; 
            zna->nodeID = nodeid;
        }
      }
      else
#endif
      {
        nodeid = nodeOfIP((uip_ip6addr_t*) &(zna->ipv6Address1));
        /*Resolve node id from ipaddress*/
        if (nodeid > 0x100) {
            zna->extendedNodeidMSB = nodeid >> 8;
            zna->extendedNodeidLSB = nodeid & 0xff; 
            zna->nodeID = 0xff;
        } else {
            zna->extendedNodeidMSB = 0;
            zna->extendedNodeidLSB = 0; 
            zna->nodeID = nodeid;
        }

      }

    }
      break;
  }

  /* Prepare the message */
  zna->cmd = ZIP_NODE_ADVERTISEMENT;
  /*homeID is stored network byte order*/
  homeID_hb = UIP_HTONL(homeID);

//printf("homeID = 0x%lx  homeID_hb = 0x%lx\r\n", (unsigned long)homeID, (unsigned long)homeID_hb);
  /*This Operation assumes homeID_hb to be in host byte order*/
  zna->homeId1 = (homeID_hb >> 24) & 0xFF;
  zna->homeId2 = (homeID_hb >> 16) & 0xFF;
  zna->homeId3 = (homeID_hb >> 8) & 0xFF;
  zna->homeId4 = (homeID_hb >> 0) & 0xFF;

  zna->properties1 &= ZIP_NODE_ADVERTISEMENT_PROPERTIES1_LOCAL_BIT_MASK; // mask the reserved bits
  zna->properties1 &= ~3; //Mask out the lower 3 bits
  if (nodeid)
  {

    zna->properties1 |= (nodeid== MyNodeID) ||
        rd_node_exists(nodeid) ?
            ZIP_NODE_ADVERTISEMENT_VALIDITY_INFORMATION_OK :
            ZIP_NODE_ADVERTISEMENT_VALIDITY_INFORMATION_OBSOLETE;
  }
  else
  {
    zna->properties1 |= ZIP_NODE_ADVERTISEMENT_VALIDITY_INFORMATION_NOT_FOUND;
  }


  udp_send_wrap(c,zna, sizeof(ZW_ZIP_NODE_ADVERTISEMENT_V4_FRAME), 0, 0);
}

static void
do_app_handler() CC_REENTRANT_ARG
{
  struct async_state *s;
  u16_t udp_payload_len;

  while (uip_packetqueue_buflen(&async_queue))
  {
    s = (struct async_state*) uip_packetqueue_buf(&async_queue);
    udp_payload_len = uip_packetqueue_buflen(&async_queue)
        - sizeof(struct async_state);

    /* Send ACK before handling the packet if needed */
    if (s->ack_req)
    {
      send_udp_ack(&s->conn, RES_ACK);
    }
    ApplicationIpCommandHandler(&s->conn,
        (BYTE*) s + sizeof(struct async_state), udp_payload_len);

    uip_packetqueue_pop(&async_queue);
  }
}

void
send_udp_ack(zwave_udp_session_t* s, zwave_udp_response_t res)
{
  /* Ack package, ready to send, just update the seqNr*/
  ZW_COMMAND_ZIP_PACKET ack =
    { COMMAND_CLASS_ZIP, COMMAND_ZIP_PACKET, ZIP_PACKET_FLAGS0_ACK_RES, //Parm1
        0,                          //Parm2
        0,                          //Seq
        0,                          //Endpoint
        0, };

//  ERR_PRINTF("Setting seq no to seq[%d]: %x\n", nodeOfIP(&s->conn.sipaddr), s->seq[nodeOfIP(&s->conn.sipaddr)]);
  ack.seqNo = s->seq;
  ack.dEndpoint = s->rendpoint;
  ack.sEndpoint = s->lendpoint;

  switch (res)
  {
    case RES_ACK:
      ack.flags0 = ZIP_PACKET_FLAGS0_ACK_RES;
      ack.flags1 = 0;
      break;
    case RES_NAK:
      ack.flags0 = ZIP_PACKET_FLAGS0_NACK_RES;
      ack.flags1 = 0;
      break;
    case RES_WAITNG:
      ack.flags0 = ZIP_PACKET_FLAGS0_NACK_RES | ZIP_PACKET_FLAGS0_NACK_WAIT;
      ack.flags1 = 0;
      break;
    case RES_OPT_ERR:
      ack.flags0 = ZIP_PACKET_FLAGS0_NACK_RES | ZIP_PACKET_FLAGS0_NACK_OERR;
      ack.flags1 = 0;
      break;
  }

  udp_send_wrap(&s->conn, &ack, ZIP_HEADER_SIZE, 0, 0);
}

void
UDPCommandHandler(struct uip_udp_conn* c,const u8_t* data, u16_t len,u8_t received_secure) CC_REENTRANT_ARG
{
  const u8_t keep_alive_ack[] = {COMMAND_CLASS_ZIP,COMMAND_ZIP_KEEP_ALIVE,ZIP_KEEP_ALIVE_ACK_RESPONSE};

  u16_t udp_payload_len;
  BOOL isMulticast;
  struct uip_packetqueue_packet *p;
  struct async_state* s;
  ZW_COMMAND_ZIP_PACKET* pZipPacket = (ZW_COMMAND_ZIP_PACKET*) data;
  BYTE* payload;
  zwave_udp_session_t ses;
  security_scheme_t scheme = AUTO_SCHEME;

  int tmp_flags1;

  if (pZipPacket->cmdClass == COMMAND_CLASS_ZIP_ND)
  {
    ZipND_CommandHandler(c,data,len);
    return;
  }

  if (pZipPacket->cmdClass == COMMAND_CLASS_ZIP)
  {
    /*Spoof multicast queries to look like they were sent to the link local address */
    if (uip_is_addr_linklocal_allrouters_mcast(&c->sipaddr) )
    {
      uip_ds6_addr_t *ll_ds;
      isMulticast = 1;
      ll_ds = uip_ds6_get_global(ADDR_PREFERRED);
      if (!ll_ds)
        ll_ds = uip_ds6_get_global(ADDR_TENTATIVE);

      if (ll_ds)
      {
        uip_ipaddr_copy(&c->sipaddr, &ll_ds->ipaddr);
      }
      else
      {
        ERR_PRINTF("IPv6 DAD is not done yet! Package dropped\n");
        return;
      }
    }
    else
    {
      isMulticast = 0;
    }

    switch (pZipPacket->cmd)
    {
      case COMMAND_ZIP_KEEP_ALIVE:

        if(data[2] & ZIP_KEEP_ALIVE_ACK_REQUEST) {
          DBG_PRINTF("Sending keep alive ACK from Gateway to port:%d of IP:", UIP_HTONS(c->rport));
          uip_debug_ipaddr_print(&c->ripaddr);
          udp_send_wrap(c, &keep_alive_ack, sizeof(keep_alive_ack), 0, 0);
          return;
        }
        break;
      case COMMAND_ZIP_PACKET:
      {
//        ERR_PRINTF("Setting seq[%d]: %x\n", nodeOfIP(&c->sipaddr), pZipPacket->seqNo);
        ses.seq = pZipPacket->seqNo;
        ses.conn = *c;
        ses.lendpoint = pZipPacket->dEndpoint;
        ses.rendpoint = pZipPacket->sEndpoint;

        /*Chekc if this is an ACK response, check with our tx sessions*/
        if(pZipPacket->flags0 & ZIP_PACKET_FLAGS0_ACK_RES) {
          struct udp_tx_session *s;
          for(s = list_head(udp_tx_sessions_list); s; s= list_item_next(s)) {
            if( uip_ipaddr_cmp(&s->dst_ip, &c->ripaddr ) &&
                uip_ipaddr_cmp(&s->src_ip, &c->sipaddr) &&
                s->dst_port == c->rport &&
                s->seq == pZipPacket->seqNo &&
                s->src_ep == pZipPacket->dEndpoint &&
                s->dst_ep == pZipPacket->sEndpoint
            ) {
              ctimer_stop(&s->timeout);
              if(s->cb) {
                s->cb(TRANSMIT_COMPLETE_OK,0, NULL);
              }
              list_remove(udp_tx_sessions_list,s);
              DBG_PRINTF("Freed UDP session slot: %d\n", memb_slot_number(&udp_tx_sessions_memb, s));
              memb_free(&udp_tx_sessions_memb,s);
              break;
            }
          }
        }

        udp_payload_len = len - ZIP_HEADER_SIZE;
        if (udp_payload_len + sizeof(struct async_state) > UIP_BUFSIZE)
        {
          ERR_PRINTF("UDP frame is too large.");

          /*We will ACK this any way. Think of this as a corrupt frame.*/
          if (pZipPacket->flags0 & ZIP_PACKET_FLAGS0_ACK_REQ)
          {
            send_udp_ack(&ses, RES_ACK);
          }
          return;
        }

        /* TODO: Implement ACK response and waits */
        if (pZipPacket->flags0
            & (ZIP_PACKET_FLAGS0_ACK_RES | ZIP_PACKET_FLAGS0_NACK_WAIT))
          return;

        /*Save the flags1*/
        tmp_flags1 = pZipPacket->flags1;
        payload = &pZipPacket->payload[0];

        /*Parse header extentions*/
        if (tmp_flags1 & ZIP_PACKET_FLAGS1_HDR_EXT_INCL )
        {
          tlv_t* opt = (tlv_t*) &pZipPacket->payload[1];

          if( *payload == 0 || *payload > udp_payload_len) {
            goto drop;
          }

          udp_payload_len -= *payload;
          payload += *payload;

          while ((BYTE*) opt < payload)
          {
            switch (opt->type)
            {
              case (ENCAPSULATION_FORMAT_INFO | 0x80):

                if(opt->length < 2) {
                  goto opt_error;
                }
                scheme = efi_to_shceme(opt->value[0],opt->value[1]);
                break;
              default:
                if (opt->type & 0x80) //Check for critical options
                {
                  /*Send option error*/
                  goto opt_error;
                }
                break;
            }
            opt = (tlv_t*) ((BYTE*) opt + (opt->length + 2));
          }

          if ((BYTE*) opt != payload)
          {
            goto drop;
          }
        }

        if(udp_payload_len==0) {
          goto drop;
        }

        /*Keep a backup of command since uip_udp_packet_sendto, will change uip_appdata*/

        /* payload here is ZWave command without ZIP header */
        p = uip_packetqueue_alloc(&async_queue,
            (u8_t*) payload - sizeof(struct async_state),
            sizeof(struct async_state) + udp_payload_len, 1000);
        if (p == NULL)
        {
          printf("UDP Server:No Memory. Packet Lost. Queue Len = %d \r\n",
              uip_packetqueue_len(&async_queue));
          return;
        }
        s = (struct async_state*) p->queue_buf;
        
        s->conn.rendpoint = pZipPacket->sEndpoint;
        s->conn.lendpoint = pZipPacket->dEndpoint;
        s->conn.conn = *c;
        s->conn.rx_flags=0;
        s->conn.seq = pZipPacket->seqNo;

        /* Save ack req for this Z-Wave packet so that dp_app_handler() can use
         * it */
        s->ack_req = pZipPacket->flags0 & ZIP_PACKET_FLAGS0_ACK_REQ;
        
        if(scheme == AUTO_SCHEME) {
#ifndef DEBUG_ALLOW_NONSECURE
        s->conn.scheme =
            (tmp_flags1 & ZIP_PACKET_FLAGS1_SECURE_ORIGIN) && received_secure ?
                SECURITY_SCHEME_UDP : NO_SCHEME;
#else
        s->conn.scheme =
            (tmp_flags1 & ZIP_PACKET_FLAGS1_SECURE_ORIGIN)  ?
                SECURITY_SCHEME_UDP : NO_SCHEME;

#endif
        } else {
          s->conn.scheme = scheme;
        }

        if (tmp_flags1 & ZIP_PACKET_FLAGS1_ZW_CMD_INCL)
        {
          if (isMulticast)
          {
            /*Multicast frames are handled with a delay*/
            s->ack_req = 0;
            ctimer_set(&zw_udp_timer, random_rand() & 0x1af, do_app_handler, 0);
            return;
          }
          else
          {
            do_app_handler();
          }
        }
        break;
      }
      default:
        PRINTF("Invalid Z-Wave package\n");
        break;
    }
  }
  return;

drop:
  ERR_PRINTF("Invalid package dropped\n");
  return;
opt_error:
  if (pZipPacket->flags0 & ZIP_PACKET_FLAGS0_ACK_REQ)
  {
    send_udp_ack(&ses,RES_OPT_ERR);
  }
  return;
}
/*---------------------------------------------------------------------------*/
static void
tcpip_handler(void)
{
  if (uip_newdata())
  {
    PRINTF("Incomming UDP\n");

    /* TODO: When, if ever, should we set rport back to something else? */

    // Call ApplicationIPCommandHandler() here

    UDPCommandHandler(get_udp_conn(), uip_appdata, uip_datalen()  , gisTnlPacket);

    /* Restore server connection to allow data from any node */
    server_conn->rport = 0;
    memset(&server_conn->ripaddr, 0, sizeof(uip_ipaddr_t));
    memset(&server_conn->sipaddr, 0, sizeof(uip_ipaddr_t));

    //uip_udp_packet_send(server_send_conn, buf, strlen(buf));
  }
}
#if 0
/*---------------------------------------------------------------------------*/
static void
print_local_addresses(void)
{
  int i;
  uint8_t state;

  PRINTF("Server IPv6 addresses: ");
  for(i = 0; i < UIP_DS6_ADDR_NB; i++)
  {
    state = uip_ds6_if.addr_list[i].state;
    if(uip_ds6_if.addr_list[i].isused &&
        (state == ADDR_TENTATIVE || state == ADDR_PREFERRED))
    {
      PRINT6ADDR(&uip_ds6_if.addr_list[i].ipaddr);
      PRINTF("\n");
    }
  }
}
#endif /* if 0 */
/*---------------------------------------------------------------------------*/

const uip_ipaddr_t uip_all_routers_addr =
  {
    { 0xFF, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x02 } };

PROCESS_THREAD(udp_server_process, ev, data)
{
  PROCESS_BEGIN()
    ;
    PRINTF("UDP server started\n");
    ZW_LOG(U, 2);

    struct udp_tx_session *s;

    for(s = list_head(udp_tx_sessions_list); s; s= list_item_next(s)) {
        ctimer_stop(&s->timeout);
    }

    memb_init(&udp_tx_sessions_memb);
    list_init(udp_tx_sessions_list);

    //print_local_addresses();

    uip_packetqueue_new(&async_queue);
    /*
    This is intentional. The queue must not be flushed on restart
    */
    //uip_packetqueue_new(&ipv4_packet_q);

    server_conn = udp_new(NULL, UIP_HTONS(0), &server_conn);
    if (NULL == server_conn)
    {
      //printf("could not initialize connection 1\n");
      ZW_LOG(U, 3);
    }
    udp_bind(server_conn, UIP_HTONS(ZWAVE_PORT));

    if (!uip_ds6_maddr_add((uip_ipaddr_t*) &uip_all_routers_addr))
    {
      ERR_PRINTF("Could not Register Multicast address\n");
    }

    while (1)
    {
      PROCESS_YIELD()
      ;
      if (ev == tcpip_event)
      {
        if (data == &server_conn)
        {
          tcpip_handler();
        }
//      else if(data == &client_conn) {
//        solicited_reply_handler();
//      }
      }
    }

  PROCESS_END();
}
