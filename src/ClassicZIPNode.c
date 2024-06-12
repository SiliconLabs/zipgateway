/* © 2014 Silicon Laboratories Inc. */

#include"contiki.h"
#include "crc16.h"
#include"contiki-net.h"
#include"ClassicZIPNode.h"
#include"Serialapi.h"
#include"ZW_udp_server.h"
#include "ZW_SendDataAppl.h"
#include "security_layer.h"
#include <ZW_zip_classcmd.h>
#include <ZW_classcmd_ex.h>
#include "zw_network_info.h"
#include "ResourceDirectory.h"
#include "DTLS_server.h"
#include "sys/ctimer.h"
#include "NodeCache.h"
#include "stdint.h"
#include "CC_InstallationAndMaintenance.h"
#include "node_queue.h"
#include "CC_NetworkManagement.h"
#include "Mailbox.h"
#include "CommandAnalyzer.h"
#include "zip_router_ipv6_utils.h" /* nodeOfIP, ipOfNode */
#include "ZIP_Router_logging.h"
#include "zip_router_config.h"
#include "zgw_nodemask.h"
#include "multicast_tlv.h"

#include "Bridge_ip_assoc.h"
#include "Bridge_temp_assoc.h"

#define MANGLE_MAGIC 0x55AA

#define MAX_CLASSIC_SESSIONS ZW_MAX_NODES /* Only used for non-bridge libraries */
#define FAIL_TIMEOUT_TEMP_ASSOC 2000 /* Time to wait for ZW_SendData() callback on unsolicited,
                                      * incoming frames before we give up. */
/* 10 sec delay, from SDS11402 */
#define CLASSIC_SESSION_TIMEOUT 65000UL

#define UIP_IP_BUF                          ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP_ICMP_BUF                      ((struct uip_icmp_hdr *)&uip_buf[uip_l2_l3_hdr_len])
#define UIP_UDP_BUF                        ((struct uip_udp_hdr *)&uip_buf[uip_l2_l3_hdr_len])
#define ZIP_PKT_BUF                      ((ZW_COMMAND_ZIP_PACKET*)&uip_buf[UIP_LLH_LEN + UIP_IPUDPH_LEN])
#define ZIP_PKT_BUF_SIZE                 (UIP_BUFSIZE -(UIP_LLH_LEN + UIP_IPUDPH_LEN))
#define IP_ASSOC_PKT_BUF                 ((ZW_COMMAND_IP_ASSOCIATION_SET*)&uip_buf[UIP_LLH_LEN + UIP_IPUDPH_LEN + ZIP_HEADER_SIZE])
#define BACKUP_UIP_IP_BUF                ((struct uip_ip_hdr *)backup_buf)
#define BACKUP_IP_ASSOC_PKT_BUF          ((ZW_COMMAND_IP_ASSOCIATION_SET*)&backup_buf[UIP_IPUDPH_LEN + ZIP_HEADER_SIZE])
#define BACKUP_PKT_BUF                   ((ZW_COMMAND_ZIP_PACKET*)&backup_buf[UIP_IPUDPH_LEN])

/* 10 sec delay, from SDS11402 */
const BYTE ZW_NOP[] =
  { 0 };

static const ZW_APPLICATION_BUSY_FRAME application_busy =
  {
      COMMAND_CLASS_APPLICATION_STATUS,
      APPLICATION_BUSY,
      APPLICATION_BUSY_TRY_AGAIN_IN_WAIT_TIME_SECONDS,
      0xff, };

uint8_t backup_buf[UIP_BUFSIZE];
uint16_t backup_len;
uint8_t is_device_reset_locally = 0;

static uint8_t classic_txBuf[UIP_BUFSIZE - UIP_IPUDPH_LEN];/*NOTE: the local txBuf is ok here, because the frame is sent via the SerialAPI*/

static uint8_t txOpts;

static BOOL bSendNAck;
static BOOL __fromMailbox; //TODO clean this up
static BOOL __requeued; //TODO clean this up

static struct ctimer nak_wait_timer;

/* Place holders for parameters needed to send ACK, in the ZW_SendData callback */

zwave_connection_t zw;

static BYTE cur_flags0;
static BYTE cur_flags1;

static uint8_t cur_SendDataAppl_handle;

/* TODO: Do we need this? Why not hardcode to call zip_data_sent() directly? */
static VOID_CALLBACKFUNC(cbCompletedFunc)
(BYTE,BYTE* data,uint16_t len);

static BOOL
proxy_command_handler(zwave_connection_t* c, const u8_t* payload, u8_t len, BOOL was_dtls, BOOL ack_req, uint8_t bSupervisionUnwrapped);

static int
LogicalRewriteAndSend();

extern command_handler_codes_t
ZIPNamingHandler(zwave_connection_t *c, uint8_t* pData, uint16_t bDatalen);


uint16_t zip_payload_len;


/**
 * Wrapper for ZW_SendDataAppl() that saves result to private handle in ClassicZipNode module.
 * 
 * For description of parameters and return value see ZW_SendDataAppl().
 */
uint8_t ClassicZIPNode_SendDataAppl(ts_param_t* p,
                                    const void *pData,
                                    uint16_t dataLength,
                                    ZW_SendDataAppl_Callback_t callback,
                                    void* user)
{
  cur_SendDataAppl_handle = ZW_SendDataAppl(p, pData, dataLength, callback, user);
  return cur_SendDataAppl_handle;
}

void
ClassicZIPNode_CallSendCompleted_cb(BYTE bStatus, void* usr, TX_STATUS_TYPE *t)
{

  uint16_t l;
  VOID_CALLBACKFUNC(tmp_cbCompletedFunc)
  (BYTE,BYTE* data,uint16_t len);

  l = backup_len;
  backup_len = 0;
  cur_SendDataAppl_handle = 0;
  tmp_cbCompletedFunc = cbCompletedFunc;
  cbCompletedFunc = NULL;
  if (tmp_cbCompletedFunc)
    tmp_cbCompletedFunc(bStatus, backup_buf, l);
}

void
report_send_completed(uint8_t bStatus)
{
  ClassicZIPNode_CallSendCompleted_cb(bStatus, NULL, NULL);
  ctimer_stop(&nak_wait_timer);
}


static void
UDP_SendBuffer(int udp_payload_len)
{
  uip_len = UIP_IPUDPH_LEN + udp_payload_len;

  UIP_UDP_BUF ->udplen = UIP_HTONS(UIP_UDPH_LEN + udp_payload_len);
  UIP_UDP_BUF ->udpchksum = 0;

  /* For IPv6, the IP length field does not include the IPv6 IP header
   length. */UIP_IP_BUF ->len[0] = ((uip_len - UIP_IPH_LEN) >> 8);
  UIP_IP_BUF ->len[1] = ((uip_len - UIP_IPH_LEN) & 0xff);
  UIP_IP_BUF ->ttl = 0xff;
  UIP_IP_BUF ->proto = UIP_PROTO_UDP;
  UIP_IP_BUF ->vtc = 0x60;
  UIP_IP_BUF ->tcflow = 0x00;
  UIP_IP_BUF ->flow = 0x00;

  UIP_UDP_BUF ->udpchksum = ~(uip_udpchksum());
  if (UIP_UDP_BUF ->udpchksum == 0)
  {
    UIP_UDP_BUF ->udpchksum = 0xffff;
  }

  memset(uip_buf, 0, UIP_LLH_LEN);
  /*Send */
  tcpip_ipv6_output();
}

static void
NOP_Callback(BYTE bStatus, void* user, TX_STATUS_TYPE *t)
{
  nodeid_t node;

  memcpy(&uip_buf[UIP_LLH_LEN], backup_buf, backup_len);
  node = nodeOfIP(&UIP_IP_BUF ->destipaddr);
  uip_len = backup_len;

  if (bStatus == TRANSMIT_COMPLETE_OK)
  {
    uip_icmp6_echo_request_input();
    tcpip_ipv6_output();
  }
  else if (rd_get_node_mode(node) != MODE_MAILBOX)
  {
    uip_icmp6_error_output(ICMP6_DST_UNREACH, ICMP6_DST_UNREACH_ADDR, 0);
    tcpip_ipv6_output();
  }
  ClassicZIPNode_CallSendCompleted_cb(bStatus, NULL, NULL);
}

#define DEBUG DEBUG_FULL
#include "uip-debug.h"
#include "crc16.h"

/**
 * Send Ack or nack to from srcNode to dst_ip(NOTE this write directly to uip_buf)
 * If ack is true send a ack otherwise send a nak , if wait is true also mark wait.
 * This is an extended version of SendUDPStatus which also sends RSSI info.
 *
 * \param is_mcast Flag to indicate if this ack should contain multicast statuc TLV
 */
void SendUDPStatus_rssi(int flags0, int flags1, zwave_connection_t *c, TX_STATUS_TYPE *txStat, BOOL is_mcast)
{

  u32_t delta;
  rd_node_database_entry_t *n;
  u16_t len;

  ZIP_PKT_BUF ->cmdClass = COMMAND_CLASS_ZIP;
  ZIP_PKT_BUF ->cmd = COMMAND_ZIP_PACKET;

  ZIP_PKT_BUF ->flags0 = flags0;
  ZIP_PKT_BUF ->flags1 = flags1;
  ZIP_PKT_BUF ->seqNo = c->seq;
  ZIP_PKT_BUF ->sEndpoint = c->lendpoint;
  ZIP_PKT_BUF ->dEndpoint = c->rendpoint;

  len = ZIP_HEADER_SIZE;

  /*
   * Add "Expected Delay" option to extended header?
   */
  if (flags0 & ZIP_PACKET_FLAGS0_NACK_WAIT)
  {
    delta = 0;
    n = rd_get_node_dbe(nodeOfIP(&c->conn.sipaddr));
    if (n)
    {
      if (n->mode == MODE_MAILBOX)
      {
        delta = n->wakeUp_interval - (clock_seconds() - n->lastAwake);
      }
      else if (n->mode == MODE_FREQUENTLYLISTENING)
      {
        delta = 1;
      }
      rd_free_node_dbe(n);
    }
    else
    {
      ASSERT(0);
    }

    /*Convert to 24bit Big endian */
    u8_t data[3];
    data[0] =  (delta >> 16) & 0xff;
    data[1] =  (delta >>  8) & 0xff;
    data[2] =  (delta >>  0) & 0xff;
    delta =UIP_HTONL(delta) << 8;
    len +=add_ext_header(ZIP_PKT_BUF, ZIP_PKT_BUF_SIZE,
        ZIP_PACKET_EXT_EXPECTED_DELAY,data ,sizeof(data), 1);
  }


  /*
   * Add IMA, if this is a ACK or NACK response
   */
  if(cur_send_ima && (flags0 & (ZIP_PACKET_FLAGS0_ACK_RES | ZIP_PACKET_FLAGS0_NACK_RES) ))
  {
    const struct node_ima* ima =  ima_get_entry(nodeOfIP(&c->conn.sipaddr));
    /* IME Type                         IME Length
     *---------------------------------------------
     * IMA_OPTION_RC                    3
     * IMA_OPTION_TT                    4
     * IMA_OPTION_LWR                   7
     * IMA_OPTION_RSSI                  7 
     * IMA_OPTION_ACK_CHANNE            3
     * IMA_OPTION_TRANSMIT_CHANNEL      3
     * IMA_OPTION_TX_POWER              4
     * IMA_OPTION_MEASURE_NOISE_FLOOR   4
     * IMA_OPTION_OUTGOING_RSSI         7
     */
    u8_t ima_buffer[14+7+3+3+4+4+7];
    u8_t *p = ima_buffer;
    u8_t ima_len = 7; // Keep track of the expected header length.
    *p++ = IMA_OPTION_RC;
    *p++ = 1;
    *p++ = ima_was_route_changed;

    *p++ = IMA_OPTION_TT;
    *p++ = 2;
    *p++ = (ima_last_tt >> 8) & 0xff;
    *p++ = (ima_last_tt >> 0) & 0xff;

    // If IMA lookup failed, drop the LWR IME. It's invalid anyways.
    if (NULL != ima) {
      *p++ = IMA_OPTION_LWR;
      *p++ = 5;
      memcpy(p,&ima->lwr, 5);
      p += 5;
      ima_len += 7;
    }
    if (NULL != txStat) {
      *p++ = IMA_OPTION_INCOMING_RSSI;
      *p++ = 5;
      memcpy(p,&txStat->rssi_values, 5);
      p+= 5;
      *p++ = IMA_OPTION_ACK_CHANNEL;
      *p++ = 1;
      *p++ = txStat->bACKChannelNo;
      *p++ = IMA_OPTION_TRANSMIT_CHANNEL;
      *p++ = 1;
      *p++ = txStat->bLastTxChannelNo;
      *p++ = IMA_OPTION_TX_POWER;
      *p++ = 2;
      *p++ = txStat->bUsedTxpower;
      *p++ = txStat->bAckDestinationUsedTxPower;
      *p++ = IMA_OPTION_MEASURE_NOISE_FLOOR;
      *p++ = 2;
      *p++ = txStat->bMeasuredNoiseFloor;
      *p++ = txStat->bDestinationckMeasuredNoiseFloor;
      *p++ = IMA_OPTION_OUTGOING_RSSI;
      *p++ = 5;
      *p++ = txStat->bDestinationAckMeasuredRSSI;
      *p++ = RSSI_NOT_AVAILABLE; // LR has no hops for now
      *p++ = RSSI_NOT_AVAILABLE;
      *p++ = RSSI_NOT_AVAILABLE;
      *p++ = RSSI_NOT_AVAILABLE;
      ima_len += 28;
    }
    assert((p - ima_buffer) == ima_len);

    len += add_ext_header(ZIP_PKT_BUF, ZIP_PKT_BUF_SIZE, INSTALLATION_MAINTENANCE_REPORT,ima_buffer, p - ima_buffer, 1);
  }

#ifdef TEST_MULTICAST_TX
  /*
   * Add multicast status tlv if appropriate
   */
  if (is_mcast) {

    uint8_t  ack_value_buf[ZW_MAX_NODES * MAX_MCAST_ACK_SIZE_PER_NODE];
    uint16_t ack_value_len = gen_mcast_ack_value(c,
                                                 global_mcast_status,
                                                 global_mcast_status_len,
                                                 ack_value_buf,
                                                 sizeof(ack_value_buf));

    if (ack_value_len > 0)
    {
      len += add_ext_header(ZIP_PKT_BUF,
                            ZIP_PKT_BUF_SIZE,
                            MULTICAST_ACK | ZIP_EXT_HDR_OPTION_CRITICAL_FLAG,
                            ack_value_buf, ack_value_len, 2);
    }
  }
  #endif

  udp_send_wrap(&c->conn,ZIP_PKT_BUF,len, 0, 0);
}

/*
 * Send Ack or nack to from srcNode to dst_ip(NOTE this write directly to uip_buf)
 * If ack is true send a ack otherwise send a nak , if wait is true also mark wait
 */
void SendUDPStatus(int flags0, int flags1, zwave_connection_t *c)
{
  SendUDPStatus_rssi(flags0, flags1, c, NULL, FALSE);
}

static void
nak_wait_timeout(void* user)
{
  SendUDPStatus(ZIP_PACKET_FLAGS0_NACK_WAIT | ZIP_PACKET_FLAGS0_NACK_RES ,cur_flags1,&zw);
}

/**
 * Callback from send_using_temp_assoc
 * 
 * Send a ZIP Ack/Nack
 *
 * @param bStatus Transmission status
 * @param user HAN node id of destination node
 * @param t pointer to TX status
 * @param is_mcast TRUE if ClassicZIPNode_SendDataAppl() was sending a multicast
 *                 frame, FALSE otherwise
 */
static void
send_using_temp_assoc_callback_ex(BYTE bStatus, void *user, TX_STATUS_TYPE *t, BOOL is_mcast)
{
  /* NB: The "user" pointer is NOT pointing to a memory location with the node
   * id. The actual pointer value IS the node id (saves us from creating a
   * static/global variable to hold the node id).
   */
  // TODO: we're only passing a single node in "user". Revisit this when completing multicast support!
  nodeid_t dest_nodeid = (nodeid_t) (intptr_t) user;

  ctimer_stop(&nak_wait_timer);

  DBG_PRINTF("send_using_temp_assoc_callback_ex for node %d status %u\n", dest_nodeid, bStatus);

  if((bStatus == TRANSMIT_COMPLETE_NO_ACK) && (get_queue_state() == QS_SENDING_FIRST)) {
    /* Do not send an ACK/NACK because the frame will be re-queued in the long queue*/
  } else {
    /* Only send ACK if it has been requested */
    if ( (cur_flags0 & ZIP_PACKET_FLAGS0_ACK_REQ) )
    {
      int flags0 = (bStatus == TRANSMIT_COMPLETE_OK) ? ZIP_PACKET_FLAGS0_ACK_RES
                                                     : ZIP_PACKET_FLAGS0_NACK_RES;
      SendUDPStatus_rssi(flags0, cur_flags1, &zw, t, is_mcast);
    }

    if (bStatus == TRANSMIT_COMPLETE_OK) {
       rd_node_is_alive(dest_nodeid);
    } else {
       rd_node_is_unreachable(dest_nodeid);
    }
  }

  report_send_completed(bStatus);
}

#ifdef TEST_MULTICAST_TX
/* Callback from send_using_temp_assoc on multicast frame */
static void
send_using_temp_assoc_callback_mcast(BYTE bStatus, void* user, TX_STATUS_TYPE *t)
{
  send_using_temp_assoc_callback_ex(bStatus, user, t, TRUE);
}
#endif

/* Callback from send_using_temp_assoc on single-cast frame */
static void
send_using_temp_assoc_callback(BYTE bStatus, void* user, TX_STATUS_TYPE *t)
{
  send_using_temp_assoc_callback_ex(bStatus, user, t, FALSE);
}

/* Callback from senddata after forwarding a frame through a proxy IP
 * Association.
 */
static void
forward_to_ip_assoc_proxy_callback(BYTE bStatus, void* user, TX_STATUS_TYPE *t)
{
  ip_association_t *a = (ip_association_t*)user;
  DBG_PRINTF("forward_to_ip_assoc_proxy_callback status %u, node %u\n", bStatus, a->han_nodeid);

  if (bStatus == TRANSMIT_COMPLETE_OK)
  {
    rd_node_is_alive(a->han_nodeid);
  }

  report_send_completed(bStatus);
}


uint8_t
is_local_address(uip_ipaddr_t *ip)
{
  return nodeOfIP(ip) != 0;
}

/**
 * Send a ZIP Packet with ACK/NACK status on association processing.
 *
 * \note This function uses TRANSMIT_COMPLETE_OK (==0) to indicate success.
 *
 * \param bStatus Status. TRANSMIT_COMPLETE_OK if successful, TRANSMIT_COMPLETE_FAIL if failed.
 */
void
send_zip_ack(uint8_t bStatus)
{
  if(bStatus == TRANSMIT_COMPLETE_OK) {
    SendUDPStatus(ZIP_PACKET_FLAGS0_ACK_RES,cur_flags1,&zw);
  } else if(bSendNAck) {
    SendUDPStatus(ZIP_PACKET_FLAGS0_NACK_RES,cur_flags1,&zw);
  }
}

/**
 * Send frame in classic_txBuf using temporary association virtual node
 * 
 * @param a temporary association to use
 */
static void
send_using_temp_assoc(temp_association_t *a)
{
  /* Extract the Z-Wave destination nodeid and endpoint */
  nodeid_t han_nodeid = nodeOfIP(&(BACKUP_UIP_IP_BUF->destipaddr));
  uint8_t  han_endpoint = BACKUP_ZIP_PKT_BUF->dEndpoint;

  ts_param_t p = {};
  uint8_t h = 0;

  ts_set_std(&p, han_nodeid);
  p.tx_flags = ClassicZIPNode_getTXOptions();
  p.is_mcast_with_folloup = cur_flags0 & ZIP_PACKET_FLAGS0_ACK_REQ ? TRUE : FALSE;
  p.dendpoint = han_endpoint;
  p.sendpoint = zw.rendpoint;
  if (is_device_reset_locally) {
    p.snode = MyNodeID;
  } else {
    p.snode = a->virtual_id;
  }
  p.scheme = zw.scheme;

#ifdef TEST_MULTICAST_TX
  if (p.tx_flags & TRANSMIT_OPTION_MULTICAST) {
    memcpy(p.node_list, mcast_node_list, sizeof(p.node_list));

    h = ClassicZIPNode_SendDataAppl(&p,
                                    classic_txBuf,
                                    zip_payload_len,
                                    send_using_temp_assoc_callback_mcast,
                                    (void *) (intptr_t) han_nodeid);
  } else {
#endif
    h = ClassicZIPNode_SendDataAppl(&p,
                                    classic_txBuf,
                                    zip_payload_len,
                                    send_using_temp_assoc_callback,
                                    (void *) (intptr_t) han_nodeid);
#ifdef TEST_MULTICAST_TX
  }
#endif

  if (h)
  {
    ctimer_set(&nak_wait_timer, 150, nak_wait_timeout, NULL);
  }
  else
  {
    ERR_PRINTF("ZW_SendDataAppl() failed\n");
    send_using_temp_assoc_callback(TRANSMIT_COMPLETE_FAIL,
                                   (void *) (intptr_t) han_nodeid,
                                   NULL);
  }
}


/**
 * Intercept version command class get for the ZIP naming class and the ip association class.
 */
static uint8_t
handle_version(const uint8_t* payload,u8_t len, BOOL ack_req)
{
  int the_version;
  ZW_VERSION_COMMAND_CLASS_REPORT_FRAME f;

  if(len < sizeof(ZW_VERSION_COMMAND_CLASS_GET_FRAME)) {
    return FALSE;
  }

  if(payload[1] == VERSION_COMMAND_CLASS_GET) {
    the_version = 0;

    if(payload[2] == COMMAND_CLASS_ZIP_NAMING) the_version = ZIP_NAMING_VERSION;
    if(payload[2] == COMMAND_CLASS_IP_ASSOCIATION) the_version = IP_ASSOCIATION_VERSION;
#ifdef TEST_MULTICAST_TX
    if(payload[2] == COMMAND_CLASS_ZIP) the_version = ZIP_VERSION_V5;
#else
    if(payload[2] == COMMAND_CLASS_ZIP) the_version = ZIP_VERSION_V5;
#endif

    if(the_version)
    {
      /*Send ACK first*/
      if(ack_req) {
        SendUDPStatus(  ZIP_PACKET_FLAGS0_ACK_RES  , cur_flags1,&zw);
      }

      f.cmdClass = COMMAND_CLASS_VERSION;
      f.cmd = VERSION_COMMAND_CLASS_REPORT;
      f.requestedCommandClass = payload[2];
      f.commandClassVersion = the_version;

      ZW_SendDataZIP(&zw,&f,sizeof(f), ClassicZIPNode_CallSendCompleted_cb);
      return TRUE;
    }
  }
  return FALSE;
}




static uint8_t
proxy_supervision_command_handler(zwave_connection_t* c, const uint8_t* payload, u8_t len, BOOL was_dtls, BOOL ack_req)
{
  ZW_SUPERVISION_GET_FRAME *f =(ZW_SUPERVISION_GET_FRAME *)payload;

  if(f->cmd == SUPERVISION_GET) {
    // TRUE since we are unwrapping the supervision
    if(proxy_command_handler(c,payload+4,f->encapsulatedCommandLength,was_dtls,ack_req, TRUE)) {
      ZW_SUPERVISION_REPORT_FRAME rep;
      rep.cmdClass = COMMAND_CLASS_SUPERVISION;
      rep.cmd = SUPERVISION_REPORT;
      rep.sessionid = f->sessionid & 0x3f;
      rep.status = SUPERVISION_REPORT_SUCCESS;
      rep.duration = 0;

      ZW_SendDataZIP(c,&rep,sizeof(rep), ClassicZIPNode_CallSendCompleted_cb);
      return TRUE;
    }
  }

  return FALSE;

}

/**
 * Intercept frame before sending to Z-Wave network
 * \return TRUE if the frame is handled already together with callback, FALSE if the frame should just be sent to Z-Wave network
 */
static BOOL
proxy_command_handler(zwave_connection_t* c, const u8_t* payload, u8_t len, BOOL was_dtls, BOOL ack_req, uint8_t bSupervisionUnwrapped)
{
  zwave_connection_t tc = *c;
  tc.scheme = SECURITY_SCHEME_UDP;
  switch (payload[0])
  {
  case COMMAND_CLASS_IP_ASSOCIATION:
    if (ack_req)
    {
      ctimer_set(&nak_wait_timer, 150, nak_wait_timeout, NULL);
    }
    return handle_ip_association(&tc, payload, zip_payload_len, was_dtls);
  case COMMAND_CLASS_ZIP_NAMING:
    if (ack_req)
    {
      SendUDPStatus( ZIP_PACKET_FLAGS0_ACK_RES, cur_flags1, &tc);
    }
    uint8_t rc = ZW_command_handler_run(&tc, (uint8_t*) payload, len, bSupervisionUnwrapped);
    if (rc == COMMAND_HANDLED)
    {
      ClassicZIPNode_CallSendCompleted_cb(TRANSMIT_COMPLETE_OK, NULL, NULL);
      return TRUE;
    }
    return FALSE;
  case COMMAND_CLASS_VERSION:
    return handle_version(payload, zip_payload_len, ack_req);
  case COMMAND_CLASS_SUPERVISION:
    return proxy_supervision_command_handler(&tc, payload, len, was_dtls, ack_req);
  default:
    return FALSE;
  }
}

/**
 * Parse an incoming UDP packet adressed for a classic ZWave node. The packet must be already
 * decrypted and located in the backup_buf buffer. uip_buf is unsafe because it will be
 * overwritten if we use async requests as a part of the parsing.
 *
 * Input argument data must point to ZIP hdr and length must count only zip hdr + zip payload
 * Furthermore, callers must ensure that data points to somewhere in the backup_buf buffer.
 */
/* Overview:
 * if bridge not ready: return FALSE
 * if ZIP packet:
 *    setup some globals from the packet
 *    if packet is ACK: UDPCommandHandler()
 *    syntax check packet, if wrong, go to drop or opt_error
 *    remember device_reset_locally
 *    if (some other module in gateway is busy): (shouldn't this test be earlier?)
 *       requeue with TRANSMIT_COMPLETE_REQUEUE_DECRYPTED, return TRUE
 *    if we can find an association from the destination:
 *       send with ZW_SendDataAppl(), return TRUE
 *    if (proxy_command_handler()): return TRUE
 *    if (ip_assoc_create() != OK):
 *       reset ip association state and call completedFunc(ERROR), return TRUE
 * else:
 *    reset ip assoc state and call completedFunc(ERROR), return TRUE
 * drop:
 *    send ZIP nack if needed
 *    reset ip assoc state and call completedFunc(ERROR), return TRUE
 * opt_error:
 *    send ZIP error
 *    reset ip assoc state and call completedFunc(ERROR), return TRUE
 */

int ClassicZIPUDP_input(struct uip_udp_conn* c, u8_t* pktdata, u16_t len, BOOL secure)
{
  ZW_COMMAND_ZIP_PACKET* zip_ptk = (ZW_COMMAND_ZIP_PACKET*)pktdata;
  BYTE* payload;

  DBG_PRINTF("ClassicZIPUDP_input len: %d secure: %d [%s]\n", len, secure, print_frame(pktdata, len));

  if ((bridge_state != initialized)  && ((NetworkManagement_getState() != NM_WAIT_FOR_PROBE_BY_SIS)))
  {
    DBG_PRINTF("Bridge not initialized\n");
    resume_bridge_init();
    backup_len=0;
    return FALSE;
  }

  if (zip_ptk->cmdClass == COMMAND_CLASS_ZIP
      && zip_ptk->cmd == COMMAND_ZIP_PACKET)
  {
    /* Sanity check
     */
    zip_payload_len = len - ZIP_HEADER_SIZE;
    payload = &zip_ptk->payload[0];

    cur_flags0 = zip_ptk->flags0;
    cur_flags1 = secure ? ZIP_PACKET_FLAGS1_SECURE_ORIGIN : 0;
    zw.conn = *c;
//  ERR_PRINTF("Setting seq[%d]:  to %x\n", nodeOfIP(&c->sipaddr), zip_ptk->seqNo);
    zw.seq = zip_ptk->seqNo;
    zw.lendpoint = zip_ptk->dEndpoint;
    zw.rendpoint = zip_ptk->sEndpoint;
    zw.scheme = (zip_ptk->flags1 & ZIP_PACKET_FLAGS1_SECURE_ORIGIN) ? AUTO_SCHEME  : NO_SCHEME;

#ifndef DEBUG_ALLOW_NONSECURE
    if((zip_ptk->flags1 & ZIP_PACKET_FLAGS1_SECURE_ORIGIN) && !secure ) {
      ERR_PRINTF("Frame marked with secure origin but was not received secure\n");
      goto drop_frame;
    }
#endif

    /*
     * This is an ACK for a previously sent command
     */
    if ((zip_ptk->flags0 & ZIP_PACKET_FLAGS0_ACK_RES))
    {
      DBG_PRINTF("ACK frame\n");
      UDPCommandHandler(c,pktdata, len, secure);
    }

    /* Parse header extensions */
    if (zip_ptk->flags1 & ZIP_PACKET_FLAGS1_HDR_EXT_INCL) {
      uint16_t ext_hdr_size = 0;

      if( *payload - 1 == 0 || *payload - 1 > zip_payload_len) {
        ERR_PRINTF("BAD extended header\n");
        goto drop_frame;
      }

      /*
       * ext_hdr_size reports the actual length in case of EXT_HDR_LENGTH and
       * excluding one byte field
       */
      return_codes_zip_ext_hdr_t rc = parse_CC_ZIP_EXT_HDR(&zip_ptk->payload[1], *payload-1, &zw,  &ext_hdr_size);
      if (rc == DROP_FRAME) {
        goto drop_frame;
      }
      else if (rc == OPT_ERROR) {
        goto opt_error;
      }
      zip_payload_len -= (ext_hdr_size + 1);
      payload += (ext_hdr_size + 1);
    }

    /*
     * If no ZW Command included just drop
     */
    if( ((zip_ptk->flags1 & ZIP_PACKET_FLAGS1_ZW_CMD_INCL)==0) || zip_payload_len==0 ) {
      DBG_PRINTF("No Zwave command included, Dropping\n");
      goto drop_frame;
    }
    /* If frame is too large, drop */
    if (zip_payload_len > sizeof(classic_txBuf)) {
      ERR_PRINTF("Frame too large\n");
      goto drop_frame;
    }
    /* Remember for later if it is a reset command. */

    /* Do not set source node as virtual node if its COMMAND_CLASS_DEVICE_RESET_LOCALLY */
    if (payload[0] == COMMAND_CLASS_DEVICE_RESET_LOCALLY) {
        is_device_reset_locally = 1;
    } else {
        is_device_reset_locally = 0;
    }

    /*
     * TO#03860 Check that we are done probing the node. Re queue the frame
     * to a slow queue (Mailbox or long queue) if not.
     * Requeue if Network Management is busy or
     *         if Mailbox is busy and
     *              = this is not the frame that Mailbox is busy with or
     *              - the frame has not been requeued before.
     */
    if ((!(__fromMailbox || __requeued) && mb_is_busy()) ||
        ((NetworkManagement_getState() != NM_IDLE) && (NetworkManagement_getState() != NM_WAIT_FOR_PROBE_BY_SIS)) ||
        (rd_get_node_state(nodeOfIP(&zw.lipaddr)) < STATUS_MDNS_PROBE))
    {
      report_send_completed(TRANSMIT_COMPLETE_REQUEUE_QUEUED);
      DBG_PRINTF("Requeueing frame __fromMailbox: %d, __requeued: %d "
                 "mb_is_busy(): %d nm state: %d, rd_state: %d\n",
                 __fromMailbox, __requeued, mb_is_busy(),
                 NetworkManagement_getState(),
                 rd_get_node_state(nodeOfIP(&zw.lipaddr)));
      if ((pktdata[0] == 0x16) && (pktdata[1] == 0xfe) && (pktdata[2] == 0xff)) {
          /*TODO: ZGW-633 */
          WRN_PRINTF("Requeued packet is DTLS handhshake from port: %d, ip:", UIP_HTONS(c->rport));
          uip_debug_ipaddr_print(&c->ripaddr);
      }
      return TRUE;
    }

    ip_association_t *ia = NULL;
    
    /* When proxying a case2 IP Association, keep the virtual node id when forwarding.
     * Without this exception, the outgoing sender field would be changed to one of
     * the temporary association virtual nodeids.
     *
     * Warning: zw.conn struct notion of local and remote have been swapped. The virtual
     * destination node is now stored as remote IP address.
     * The swapping happens in get_udp_conn() as a preparation for the common case
     * of _replying_. Forwarding, as we are doing here, is a special case.  */
    uip_debug_ipaddr_print(&zw.ripaddr);
    if (nodeOfIP(&zw.ripaddr)
        && (ia = ip_assoc_lookup_by_virtual_node(nodeOfIP(&zw.ripaddr))))
    {
      ASSERT(ia->type == PROXY_IP_ASSOC);
      ts_param_t p;
      ts_set_std(&p, nodeOfIP(&zw.lipaddr));
      p.tx_flags = ClassicZIPNode_getTXOptions();
      p.dendpoint = ia->resource_endpoint;
      ASSERT(uip_ipaddr_prefixcmp(&zw.lipaddr, &ia->resource_ip, 128));
      ASSERT(p.dnode);
      if(is_device_reset_locally) {
          p.snode = MyNodeID;
      } else {
          p.snode = ia->virtual_id;
      }
      p.scheme = zw.scheme;

      if (!ClassicZIPNode_SendDataAppl(&p, (BYTE*) &zip_ptk->payload, zip_payload_len,
                                       forward_to_ip_assoc_proxy_callback, ia))
      {
        /* Give up if transmission fails. TODO: Should we report/retry this error? */
        WRN_PRINTF("ClassicZIPNode_SendDataAppl failed on case2 IP Assoc proxying\n");
      }
      return TRUE;
    }


    // FALSE since we are not doing supervision unwrapping
    if(proxy_command_handler(&zw,payload,zip_payload_len,secure, zip_ptk->flags0 & ZIP_PACKET_FLAGS0_ACK_REQ, FALSE)) {
      return TRUE;
    }

    if (zip_payload_len > sizeof(classic_txBuf))
    {
      goto drop_frame;
    }
    memcpy(classic_txBuf, payload, zip_payload_len);

    /* If we get this far it's time to create a temporary association */

    temp_association_t *ta = temp_assoc_create(secure);
    if (ta)
    {
      send_using_temp_assoc(ta);

      /* Check if it's firmware update md get or report */
      if ((payload[0] == COMMAND_CLASS_FIRMWARE_UPDATE_MD) &&
          ((payload[1] == FIRMWARE_UPDATE_MD_GET_V3 ) || (payload[1] == FIRMWARE_UPDATE_MD_REPORT_V3)))
      {
        temp_assoc_register_fw_lock(ta);
      }
    }
    else
    {
      /* Malloc or create virtual failed, abort */
      ASSERT(0);
      ERR_PRINTF("Temporary association creation failed");
      send_zip_ack(TRANSMIT_COMPLETE_ERROR);
      report_send_completed(TRANSMIT_COMPLETE_ERROR);
    }

    return TRUE;

  } /* if COMMAND_CLASS_ZIP */
  else
  {
    DBG_PRINTF("Dropping packet received on Z-Wave port: Not ZIP encapped\n");
    if ((pktdata[0] == 0x16) && (pktdata[1] == 0xfe) && (pktdata[2] == 0xff)) {
        /*TODO: ZGW-633 */
        WRN_PRINTF("Dropped packet is DTLS handhshake from port: %d, %d ip:", UIP_HTONS(c->rport), secure);
        uip_debug_ipaddr_print(&c->ripaddr);
    }
    /* unknown packet, drop and continue */
    report_send_completed(TRANSMIT_COMPLETE_ERROR);
  }

  return TRUE;

drop_frame:
/*TODO why send a NACK at all? */
  if (ZIP_PKT_BUF->flags0 & ZIP_PACKET_FLAGS0_ACK_REQ) {
    SendUDPStatus(ZIP_PACKET_FLAGS0_NACK_RES,cur_flags1,&zw);
  }
  report_send_completed(TRANSMIT_COMPLETE_ERROR);
  return TRUE;

opt_error:
  ERR_PRINTF("Option error\n");
  if (ZIP_PKT_BUF->flags0 & ZIP_PACKET_FLAGS0_ACK_REQ) {
    SendUDPStatus(ZIP_PACKET_FLAGS0_NACK_RES | ZIP_PACKET_FLAGS0_NACK_OERR , cur_flags1,&zw);
  }
  report_send_completed(TRANSMIT_COMPLETE_ERROR);
  return TRUE;
}


int ClassicZIPNode_dec_input(void * pktdata, int len)
{
  uint8_t *cc = (uint8_t *) pktdata;
 
  DBG_PRINTF("Decrypted pkt: len: %d [%s]\n", len, print_frame(pktdata, len));
  /* Copy decrypted payload into uip_buf, leaving the ip header intact */
  uip_appdata = &uip_buf[UIP_LLH_LEN + UIP_IPUDPH_LEN];
  memcpy(uip_appdata,pktdata,len);
  uip_len = UIP_IPUDPH_LEN + len;

  nodeid_t node = nodeOfIP(&UIP_IP_BUF->destipaddr);

  //ERR_PRINTF("node: %d\n", node);
  return node_input_queued(node, TRUE);
}


/* This function is used in both directions.
Normally from LAN to PAN
And used from PAN to LAN after LogicalRewriteAndSend()
*/
int
ClassicZIPNode_input(nodeid_t node, VOID_CALLBACKFUNC(completedFunc) (BYTE,BYTE*,uint16_t),
                     int bFromMailbox, int bRequeued)
{
  /* Overview:
   *   if unknown destination: drop
   *   if not idle: send back to original queue
   *   if from PAN:
   *      call LogicalRewriteAndSend() to find association and forward,
   *      call completedFunc with OK, whether this works or not.
   *   switch on IP protocol:
   *      if UPD:
   *         if (destined to zwave/DTLS port):
   *            return ClassicZIPUDP_input(blabla, completedFunc)
   *         else
   *            throw Port Unreachable
   *            call completedFunc(OK)
   *      if ICMP:
   *         if ping:
   *            send NOP on PAN, if this fails, drop
   *         else:
   *            ignore it and call completedFunc(OK)
   *   drop:
   *      call completedFunc(ERROR)
   */

  if (nodemask_nodeid_is_invalid(nodeOfIP(&(UIP_IP_BUF->destipaddr))))
  {
    ERR_PRINTF("Dropping as the node id: %d is out of range\n", nodeOfIP(&(UIP_IP_BUF->destipaddr)));
    completedFunc(TRANSMIT_COMPLETE_ERROR, uip_buf+UIP_LLH_LEN,uip_len);
    return TRUE;
  }

  if(backup_len > 0) {
     /* ClassicZIPNode is busy, send frame back to its original queue. */
     if (bFromMailbox) {
        DBG_PRINTF("Requeue from Mailbox\n");
     } else if (bRequeued) {
        DBG_PRINTF("Requeue from long-queue\n");
     } else {
        DBG_PRINTF("Requeue from node queues\n");
     }
     completedFunc(TRANSMIT_COMPLETE_REQUEUE, uip_buf+UIP_LLH_LEN,uip_len);
     return TRUE;
  }

  cbCompletedFunc = completedFunc;
  __fromMailbox = bFromMailbox;
  __requeued = bRequeued;

  DBG_PRINTF("ClassicZIPNode_input: uip_len:%d \n", uip_len);
  /*Create a backup of package, in order to make async requests. */
  backup_len = uip_len;
  memcpy(backup_buf,uip_buf+UIP_LLH_LEN,backup_len);
  ASSERT(nodeOfIP(&(BACKUP_UIP_IP_BUF->destipaddr)));

  /* Destination address of frame has MANGLE_MAGIC, ie, this was a Z-wave frame. */
  if(LogicalRewriteAndSend())
  {
    ClassicZIPNode_CallSendCompleted_cb(TRANSMIT_COMPLETE_OK, NULL, NULL);
    return TRUE;
  }

  switch(UIP_IP_BUF->proto)
  {
    case UIP_PROTO_UDP:
    {

      if(UIP_UDP_BUF ->destport == UIP_HTONS(ZWAVE_PORT))
      {
        uip_appdata = &uip_buf[UIP_LLH_LEN + UIP_IPUDPH_LEN];
        uip_len -= UIP_IPUDPH_LEN;

        struct uip_udp_conn* c = get_udp_conn();
        return ClassicZIPUDP_input(c, &backup_buf[UIP_IPUDPH_LEN],
            backup_len - UIP_IPUDPH_LEN, uip_buf[UIP_LLH_LEN+3] ==FLOW_FROM_TUNNEL);
      }
#ifndef DISABLE_DTLS
      else if( UIP_UDP_BUF ->destport == UIP_HTONS(DTLS_PORT))
      {
        struct uip_udp_conn* c = get_udp_conn();
        return ClassicZIPUDP_input(c, &backup_buf[UIP_IPUDPH_LEN],
            backup_len - UIP_IPUDPH_LEN, TRUE);

        /*This should call classic UDP input at some point */
        //process_post_synch(&dtls_server_process,DTLS_SERVER_INPUT_EVENT,0);

        //if(!session_done_for_uipbuf() ) {
        //  DBG_PRINTF("DTLS Classic node session package\n");
        //  ClassicZIPNode_CallSendCompleted_cb(TRANSMIT_COMPLETE_OK, NULL, NULL); //If session is not yet established we are done processing the package.
        //} else if (dtls_ssl_read_failed){ //If the SSL_read call failed then there will be no callback.
        //  ClassicZIPNode_CallSendCompleted_cb(TRANSMIT_COMPLETE_OK, NULL, NULL);
        //} else {
        //  DBG_PRINTF("DTLS for Classic node\n");
        //}
        //return TRUE;
      }
#endif
      else /* Unknown port */
      {
        /*Throw a port unreachable */
        uip_icmp6_error_output(ICMP6_DST_UNREACH, ICMP6_DST_UNREACH_NOPORT, 0);
        tcpip_ipv6_output();
        ClassicZIPNode_CallSendCompleted_cb(TRANSMIT_COMPLETE_OK, NULL, NULL);
        return TRUE;
      }
    }
    break;
    case UIP_PROTO_ICMP6:
    switch(UIP_ICMP_BUF->type)
    {
      case ICMP6_ECHO_REQUEST:
      /*Create a backup of package, in order to make async requests. */
      DBG_PRINTF("Echo request for classic node %i\n",node);
      /*We send NOP as a non secure package*/
      ts_param_t p;
      ts_set_std(&p,node);
      p.scheme = NO_SCHEME;
      p.tx_flags = ClassicZIPNode_getTXOptions();
      if (ClassicZIPNode_SendDataAppl(&p,(u8_t*)ZW_NOP,sizeof(ZW_NOP),NOP_Callback,0))
      {
        return TRUE;
      } // else drop
      break; //ICMP6_ECHO_REQUEST
    default:
      DBG_PRINTF("ICMP type 0x%02x not supported by classic node\n",UIP_ICMP_BUF->type);
      ClassicZIPNode_CallSendCompleted_cb(TRANSMIT_COMPLETE_OK, NULL, NULL);
      return TRUE;
    }
    break; //UIP_PROTO_ICMP6
  default:
    DBG_PRINTF("Unknown protocol %i\n",UIP_IP_BUF->proto);
    ClassicZIPNode_CallSendCompleted_cb(TRANSMIT_COMPLETE_OK, NULL, NULL);
    return TRUE;
  }
drop:
  /* We have processed this packet but it is not delivered. */
  ClassicZIPNode_CallSendCompleted_cb(TRANSMIT_COMPLETE_ERROR, NULL, NULL);
  return TRUE;
}

static nodeid_t ack_rnode = 0;
static security_scheme_t busy_scheme;

static void send_unsolicited_callback(BYTE status, void* d, TX_STATUS_TYPE *t) {
  ts_param_t p;
  //d=d;

  if(status != TRANSMIT_COMPLETE_OK) {
    ts_set_std(&p, ack_rnode);
    p.scheme = busy_scheme;
    p.tx_flags = ClassicZIPNode_getTXOptions();

    if(!ZW_SendDataAppl(&p, (BYTE*) &application_busy, sizeof(application_busy), 0,
        0)) {
      ERR_PRINTF("Unable to send application busy\n");
    }
  }
}

void
ClassicZIPNode_SendUnsolicited(
    zwave_connection_t* __c, ZW_APPLICATION_TX_BUFFER *pCmd,
    BYTE cmdLength, uip_ip6addr_t* destIP, u16_t port, BOOL bAck)
{
  zwave_connection_t c;
  nodeid_t rnode;
  memcpy(&c, __c, sizeof(zwave_connection_t));

  /* rpiaddr is here ip address of source from where this unsol
     package is originated it could be one following two
     - PAN address of node
     - LAN address of ZIP Client or unsolicited destination itself
   If the unsol package originated from PAN side 
        Swap addresses if we are forwaridng packet from PAN side.

   Else if we are replying to unsol destination
        packets we use the LAN or PAN address of gateway where the unsol destination sent the packet*/

  if( uip_ipaddr_prefixcmp(&cfg.pan_prefix, &c.ripaddr, 64) ){
    c.lipaddr = c.ripaddr;
  }
  c.ripaddr = *destIP;

  /* Swap endpoints */
  uint8_t tmp;
  tmp = c.lendpoint;
  c.lendpoint = c.rendpoint;
  c.rendpoint = tmp;

  c.rport = port;
#ifdef DISABLE_DTLS
  c.lport = UIP_HTONS(ZWAVE_PORT);
#else
  c.lport = (c.rport == UIP_HTONS(ZWAVE_PORT)) ? UIP_HTONS(ZWAVE_PORT): UIP_HTONS(DTLS_PORT);
#endif

  /* NOte we are just forwardig the packet to unsol destination. Do not flip the end points */

  rnode = nodeOfIP( &c.lipaddr );

  DBG_PRINTF("Sending Unsolicited to IP app...\n");


  if(bAck &&( CommandAnalyzerIsGet(pCmd->ZW_Common.cmdClass,pCmd->ZW_Common.cmd) || CommandAnalyzerIsSet(pCmd->ZW_Common.cmdClass,pCmd->ZW_Common.cmd) ) ) {
    ack_rnode = rnode;
    busy_scheme = c.scheme;

    ZW_SendDataZIP_ack(&c,pCmd,cmdLength,send_unsolicited_callback);
  } else {
    ZW_SendDataZIP(&c,pCmd,cmdLength,0);
  }
}

void
ClassicZIPNode_init()
{
#if NON_BRIDGE
  int n;

  for (n = 0; n < MAX_CLASSIC_SESSIONS; n++)
  {
    classic_sessions[n].dstNode = 0;
    timer_set(&(classic_sessions[n].timeout),CLASSIC_SESSION_TIMEOUT);
  }
#endif
  backup_len = 0;

  ClassicZIPNode_setTXOptions(TRANSMIT_OPTION_ACK |
                              TRANSMIT_OPTION_AUTO_ROUTE |
                              TRANSMIT_OPTION_EXPLORE);
}

/**
 * Form a UDP package from the the Z-Wave package. Destination address will
 * be a logical HAN address which contains the source and destination node
 * IDs and endpoints.
 *
 * The resulting destination address is on the form
 * han_pefix::ffaa:0000:[dendpoint][sendpoint]:[dnode][snode]
 */
void
CreateLogicalUDP(ts_param_t* p, unsigned char *pCmd, uint8_t cmdLength)
{
  BYTE l;

  zwave_connection_t c;
   
  memset(&c,0,sizeof(c));

  c.tx_flags = p->tx_flags;
  c.rx_flags = p->rx_flags;
  c.scheme = p->scheme;
  c.rendpoint = p->dendpoint;
  c.lendpoint = p->sendpoint;

  /* Lookup destination node in assoc table*/
  if (p->dendpoint & 0x80)
  {
    WRN_PRINTF(
        "bit-addressed multichannel encaps should never be sent to virtual nodes\n");
    return;
  }
  temp_association_t *a = temp_assoc_lookup_by_virtual_nodeid(p->dnode);
  if (a)
  {
    int len = 0;

    if ((pCmd[0] == COMMAND_CLASS_FIRMWARE_UPDATE_MD) && //If its firmware update md get or report
        ((pCmd[1] == FIRMWARE_UPDATE_MD_GET_V3) || (pCmd[1] == FIRMWARE_UPDATE_MD_REPORT_V3)))
    {
      DBG_PRINTF("LogicalRewriteAndSend: Marking temp association %p as locked by firmware update.\n", a);

      /* We could end up reusing temporary association already created for OTA
         * upgrade and this causes OTA to stall so we need to mark it with
         * firmware flag so that its not reused */
      if (temp_assoc_fw_lock.locked_a)
      {
        DBG_PRINTF("Previous temp association locked by firmware update was %p %d->ANY\n",
                   temp_assoc_fw_lock.locked_a,
                   temp_assoc_fw_lock.locked_a->virtual_id);
      }

      temp_assoc_fw_lock.locked_a = a;
      DBG_PRINTF("New temp association locked by firmware update is %p %d->ANY\n",
                 a,
                 a->virtual_id);

      ctimer_set(&temp_assoc_fw_lock.reset_fw_timer,
                 60000,
                 temp_assoc_fw_lock_release_on_timeout,
                 a);
    }

    c.rport = a->resource_port;
    c.lport = a->was_dtls ? UIP_HTONS(DTLS_PORT) : UIP_HTONS(ZWAVE_PORT);

    /* Association to LAN - use DTLS and association source as source ip */
    ipOfNode(&c.lipaddr, p->snode);
    uip_ipaddr_copy(&c.ripaddr, &a->resource_ip);

    DBG_PRINTF("Packet [%s] from nodeid: %d to port: %d IP addr: ", print_frame((const char *)pCmd, cmdLength), p->snode, UIP_HTONS(c.rport));
    uip_debug_ipaddr_print(&c.ripaddr);

    ZW_SendData_UDP(&c, pCmd, cmdLength, NULL, FALSE);
  }
  else
  {
    WRN_PRINTF("No temp association found for package\n");
  }
}

/**
 * Rewrite source and destination addresses of IP package based on
 * logical HAN destination address and temporary association tables.
 */
static int
LogicalRewriteAndSend()
{
  if (UIP_IP_BUF ->destipaddr.u16[4] == MANGLE_MAGIC)
  {
    nodeid_t han_nodeid = UIP_IP_BUF ->destipaddr.u16[7];
    nodeid_t virtual_nodeid = UIP_IP_BUF ->destipaddr.u16[6];
    u8_t endpoint = UIP_IP_BUF ->destipaddr.u8[10]; // Not currently used for anything here
    DBG_PRINTF("DeMangled HAN node %d Virtual Node %d\n", han_nodeid, virtual_nodeid);

    temp_association_t *a = temp_assoc_lookup_by_virtual_nodeid(virtual_nodeid);
    if (a)
    {
      struct uip_udp_conn c;

      int len = 0;
      if (ZIP_PKT_BUF->flags1 & 0x80) { //extended header
          len = len + ZIP_PKT_BUF->payload[0];
      }

      if    ((ZIP_PKT_BUF->payload[len] == COMMAND_CLASS_FIRMWARE_UPDATE_MD) && //If its firmware update md get or report
           ((ZIP_PKT_BUF->payload[len+1] == FIRMWARE_UPDATE_MD_GET_V3 ) || (ZIP_PKT_BUF->payload[len+1] == FIRMWARE_UPDATE_MD_REPORT_V3)))
      {
        DBG_PRINTF("LogicalRewriteAndSend: Marking temp association %p as locked by firmware update.\n", a);

        /* We could end up reusing temporary association already created for OTA
         * upgrade and this causes OTA to stall so we need to mark it with
         * firmware flag so that its not reused */
        if (temp_assoc_fw_lock.locked_a) {
           DBG_PRINTF("Previous temp association locked by firmware update was %p %d->ANY\n",
                      temp_assoc_fw_lock.locked_a,
                      temp_assoc_fw_lock.locked_a->virtual_id);
        }

        temp_assoc_fw_lock.locked_a = a;
        DBG_PRINTF("New temp association locked by firmware update is %p %d->ANY\n",
                   a,
                   a->virtual_id);

        ctimer_set(&temp_assoc_fw_lock.reset_fw_timer,
                   60000,
                   temp_assoc_fw_lock_release_on_timeout,
                   a);
      }
      ZIP_PKT_BUF ->dEndpoint = a->resource_endpoint;
      ZIP_PKT_BUF ->sEndpoint =  UIP_IP_BUF ->destipaddr.u8[11];
      c.rport = a->resource_port ;
      c.lport = a->was_dtls ? UIP_HTONS(DTLS_PORT) : UIP_HTONS(ZWAVE_PORT);

      /* Association to LAN - use DTLS and association source as source ip */
      ipOfNode(&c.sipaddr, han_nodeid);
      uip_ipaddr_copy(&c.ripaddr, &a->resource_ip);
      DBG_PRINTF("Packet from Z-wave side (nodeid: %d) to port: %d IP addr: ", han_nodeid, UIP_HTONS(c.rport));
      uip_debug_ipaddr_print(&c.ripaddr);
      udp_send_wrap(&c, ZIP_PKT_BUF, uip_len - UIP_IPUDPH_LEN, 0, 0);
    }
    else
    {
      WRN_PRINTF("No temp association found for package\n");
    }
    return TRUE;
  }
  return FALSE;
}

void
ClassicZIPNode_setTXOptions(uint8_t opt)
{
  txOpts = opt;
}

void
ClassicZIPNode_addTXOptions(uint8_t opt)
{
  txOpts |= opt;
}

uint8_t
ClassicZIPNode_getTXOptions(void)
{
  return txOpts;
}

uint8_t *
ClassicZIPNode_getTXBuf(void)
{
  return classic_txBuf;
}


void
ClassicZIPNode_sendNACK(BOOL __sendNAck)
{
  bSendNAck = __sendNAck;
}

void
ClassicZIPNode_AbortSending()
{
  if (cur_SendDataAppl_handle)
  {
    ZW_SendDataApplAbort(cur_SendDataAppl_handle);
  }
}

