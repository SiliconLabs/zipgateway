/* © 2014 Silicon Laboratories Inc. */

#include "ZW_classcmd_ex.h"
#include "Mailbox.h"
#include "contiki-net.h"
#include "list.h"
#include "crc16.h"
#include "ZW_zip_classcmd.h"
#include "ZW_udp_server.h"
#include "ZW_classcmd.h"
#include "sys/ctimer.h"
#include "ClassicZIPNode.h"
#include "zw_network_info.h"
#include "ResourceDirectory.h"
#include "ZW_SendDataAppl.h"
#include "DTLS_server.h"
#include <stdlib.h>
#include "CC_NetworkManagement.h"
#include "ZW_SendRequest.h"
#include "command_handler.h"
#include "parse_config.h"
#include "zip_router_ipv6_utils.h" /* nodeOfIP */
#include "ZIP_Router_logging.h"
#include "zip_router_config.h"
#include "router_events.h"
#include "zgw_nodemask.h"

#define MAX_MAIL_BOX_PAYLOAD UIP_BUFSIZE
#define PING_TIMEOUT_SEC 600
#define WAITING_TIMEOUT 60
#define NO_MORE_TIMEOUT 3

#include "uip-debug.h"

u8_t rd_node_in_probe(nodeid_t node);
/**
 * linked list of posted packages
 */
typedef struct mailbox
{
  void* next;
  uint8_t data[MAX_MAIL_BOX_PAYLOAD];
  uint16_t data_len;
  uint8_t handle;
  uip_ip6addr_t proxy;
  uint8_t waiting_enabled;
  struct ctimer waiting_timer;
  unsigned long queued_time;
} mailbox_t;
LIST(mb_list);
MEMB(mb_memb, struct mailbox, MAX_MAILBOX_ENTRIES);

typedef struct
{
  enum
  {
    MB_STATE_IDLE,
    MB_STATE_PROBE_WAKEUP_INTERVAL,
    MB_STATE_SENDING,
    MB_STATE_SENDING_THROUGH_PROXY,
    MB_STATE_SENDING_FROM_PROXY,
    MB_STATE_SENDING_LAST_FORM_PROXY,
    MB_STATE_SEND_NO_MORE_INFO,
    MB_STATE_SEND_NO_MORE_INFO_DELAYED,
    MB_STATE_SENDING_PING,
    MB_STATE_PING_ZIPCLIENTS,
  } state;
  nodeid_t node;

  zwave_udp_session_t udp_session;
  uint8_t handle; //Handle of current message
  uint8_t send_no_more; /* 1: more_info bit__NOT__ set; 0: more_info bit set. */
  uint8_t txStatus;
  mailbox_t* last_entry;
  uint8_t broadcast_wun; /* Wakeup notification received as broadcast - dont send no more info */
                         /* Different from mb_state.send_no_more which postpones no more info
                          * because the Z/IP client has requested it. This is triggered by the wakeup
                          * node itself. */
} mb_state_t;

#define STR_CASE(x) \
  case x:           \
    return #x;

const char *mb_state_name(int state)
{
  static char message[25];
  switch (state) {
    STR_CASE(MB_STATE_IDLE)
    STR_CASE(MB_STATE_PROBE_WAKEUP_INTERVAL)
    STR_CASE(MB_STATE_SENDING)
    STR_CASE(MB_STATE_SENDING_THROUGH_PROXY)
    STR_CASE(MB_STATE_SENDING_FROM_PROXY)
    STR_CASE(MB_STATE_SENDING_LAST_FORM_PROXY)
    STR_CASE(MB_STATE_SEND_NO_MORE_INFO)
    STR_CASE(MB_STATE_SEND_NO_MORE_INFO_DELAYED)
    STR_CASE(MB_STATE_SENDING_PING)
    STR_CASE(MB_STATE_PING_ZIPCLIENTS)

   default:
      snprintf(message, sizeof(message), "%d", state);
      return message;
  }
}

typedef enum
{
  MB_EVENT_SEND_DONE,
  MB_EVENT_WAKEUP,
  MB_EVENT_POP,
  MB_EVENT_POPLAST,
  MB_EVENT_TIMEOUT,
  MB_EVNET_PING_REQ,
  MB_EVNET_TIME_TO_PING, //Emitted when it is time to ping the Z/IP Clients
  MB_EVENT_TRANSITION,
  MB_EVENT_FRAME_SENT_TO_NODE,
} mb_event_t;

const char *mb_event_name(mb_event_t ev)
{
  static char message[25];
  switch (ev) {
    STR_CASE(MB_EVENT_SEND_DONE)
    STR_CASE(MB_EVENT_WAKEUP)
    STR_CASE(MB_EVENT_POP)
    STR_CASE(MB_EVENT_POPLAST)
    STR_CASE(MB_EVENT_TIMEOUT)
    STR_CASE(MB_EVNET_PING_REQ)
    STR_CASE(MB_EVNET_TIME_TO_PING)
    STR_CASE(MB_EVENT_TRANSITION)
    STR_CASE(MB_EVENT_FRAME_SENT_TO_NODE)
    default:
      snprintf(message, sizeof(message), "%d", ev);
      return message;
  }
}
static mb_state_t mb_state;

static struct ctimer ping_timer;
static struct ctimer no_more_timer;


/**
 * CRC16 List of blacklisted packages, this is to avoid having the same package posted twice.
 */
static uint16_t black_list_crc16[MAX_MAILBOX_ENTRIES];

#define UIP_IP_BUF                          ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP_ICMP_BUF                      ((struct uip_icmp_hdr *)&uip_buf[uip_l2_l3_hdr_len])
#define UIP_UDP_BUF                        ((struct uip_udp_hdr *)&uip_buf[uip_l2_l3_hdr_len])
#define ZIP_PKT_BUF                      ((ZW_COMMAND_ZIP_PACKET*)&uip_buf[UIP_LLH_LEN + UIP_IPUDPH_LEN])
#define IP_ASSOC_PKT_BUF                 ((ZW_COMMAND_IP_ASSOCIATION_SET*)&uip_buf[UIP_LLH_LEN + UIP_IPUDPH_LEN + ZIP_HEADER_SIZE])

//static uint32_t next_delivery_time;

static nodeid_t
mb_get_dnode(uint8_t* ip_pkt);

static void
mb_send_done_event(BYTE status, BYTE* data, uint16_t len);

static void
mb_state_transition(mb_event_t event);

static void
mb_send_done(uint8_t status, void* user, TX_STATUS_TYPE *t);


/**
 * Check to see if the more information flag is set.
 */
uint8_t
is_more_info_set()
{
  return (ZIP_PKT_BUF->flags1 & ZIP_PACKET_FLAGS1_MORE_INFORMATION) > 0;
}

/**
 * Returns TRUE if the pkt is a ZIP command
 */
uint8_t
is_zip_pkt(uint8_t* pkt)
{
  struct uip_ip_hdr* iph = (struct uip_ip_hdr *) (pkt);
  struct uip_udp_hdr *udph = (struct uip_udp_hdr *) (pkt + uip_l3_hdr_len);
  ZW_COMMAND_ZIP_PACKET* ziph = (ZW_COMMAND_ZIP_PACKET*) (pkt + UIP_IPUDPH_LEN);

  if (iph->proto != UIP_PROTO_UDP)
    return FALSE;
  if (  (udph->destport != UIP_HTONS(DTLS_PORT)) && (udph->destport != UIP_HTONS(ZWAVE_PORT) ) )
    return FALSE;
  if (ziph->cmdClass != COMMAND_CLASS_ZIP)
    return FALSE;
  if (ziph->cmd != COMMAND_ZIP_PACKET)
    return FALSE;
  return TRUE;
}



/**
 * Create a zw conneciton from the package in the udp buffer
 */
zwave_connection_t* get_zw_conn() {
  static zwave_connection_t zw;

  zw.conn = *get_udp_conn();
  zw.lendpoint = ZIP_PKT_BUF->sEndpoint;
  zw.rendpoint = ZIP_PKT_BUF->dEndpoint;
  zw.seq = ZIP_PKT_BUF->seqNo;
  return &zw;
}

static void
mb_free_entry(mailbox_t* m)
{
  ctimer_stop(&m->waiting_timer);
  list_remove(mb_list, m);
  memb_free(&mb_memb, m);
}

/**
 * Send a MAILBOX_QUEUE command to the right proxy. The source ip will always be the mail ip of the GW
 * Destination port will be ZWAVE_PORT. The mailbox message will be piggybacked on the entry.
 *
 * @param command The command byte to send.
 * @param entry The mailbox entry to append to the message
 */
static void
send_mb_proxy_queue_command(u8_t command, mailbox_t* entry)
{
  mailbox_t* m;
  uip_ip6addr_t ip;

  m = memb_alloc(&mb_memb);
  ASSERT(m);

  m->data[0] = COMMAND_CLASS_MAILBOX;
  m->data[1] = MAILBOX_QUEUE;
  m->data[2] = command;
  m->data[3] = m->handle;

  memcpy(m->data + 4, entry->data, entry->data_len);
  ipOfNode(&ip, MyNodeID);
  __ZW_SendDataZIP(&ip, &entry->proxy, UIP_HTONS(DTLS_PORT), m->data,
      entry->data_len + 4, 0);
  memb_free(&mb_memb, m);
}

static void
mb_failing_notify_byip(uip_ip6addr_t* ip)
{
  mailbox_t* mb;
  mailbox_t* m;

  mb = list_head(mb_list);
  while (mb)
    {
      struct uip_ip_hdr* iph = (struct uip_ip_hdr*) mb->data;
      DBG_PRINTF("Destination of this mailbox message ");
      uip_debug_ipaddr_print(&iph->destipaddr);
      DBG_PRINTF("\n Failing dest");
      uip_debug_ipaddr_print(ip);
      DBG_PRINTF("\n");

      if (uip_ipaddr_cmp(&iph->destipaddr, ip))
      {

        m = mb;
        mb = list_item_next(mb);
        /* Free the mailbox entry */

        /* - All the mailbox message lying around for 10 minutes will be discarded here
           - Caller of this function is called every 1 minute so the number 11 below
          */
        if ((m->queued_time + (11 * 60)) < clock_seconds())
        {
            ERR_PRINTF("Dropping mailbox msg as it has reached its 10 minute life\n");
            if (is_zip_pkt(m->data) && uip_is_addr_unspecified(&m->proxy))
            {
              memcpy(UIP_IP_BUF, m->data, UIP_IPUDPH_LEN + ZIP_HEADER_SIZE);
              SendUDPStatus(ZIP_PACKET_FLAGS0_NACK_RES, 0, get_zw_conn());
            }
            mb_free_entry(m);
        }
      }
      else
      {
        mb = list_item_next(mb);
      }
    }
}

static void
send_waiting(u8_t* ipbuf)
{
  int f1;
  DBG_PRINTF("Sending waiting\n");
  memcpy(UIP_IP_BUF, ipbuf, UIP_IPUDPH_LEN + ZIP_HEADER_SIZE);
  f1 = ZIP_PKT_BUF->flags1 & ZIP_PACKET_FLAGS1_SECURE_ORIGIN;
  SendUDPStatus( ZIP_PACKET_FLAGS0_NACK_RES | ZIP_PACKET_FLAGS0_NACK_WAIT, f1,
      get_zw_conn());
}

/*
 * Every 10 minutes try to ping the nodes
 */
static void
send_ping_timeout(void *data)
{
  mb_state_transition(MB_EVNET_TIME_TO_PING);

  /*Start timer here to be sure its running, the timer will be reset when the last
   * element has been ping'ed. But there is a chance that the FSM is not in IDLE, we
   * we are sending the event, in that case the FSM will not set the timer.
   * */
  ctimer_set(&ping_timer, PING_TIMEOUT_SEC * CLOCK_CONF_SECOND,
      send_ping_timeout, 0);
}

static void
send_waiting_timeout(void* data)
{
  mailbox_t* mb = data;

  if (mb->waiting_enabled)
  {
     if (uip_is_addr_unspecified(&mb->proxy))
     {
         send_waiting(mb->data);
     }
     else
     {
         send_mb_proxy_queue_command(MAILBOX_WAITING, mb);
     }
  }
  ctimer_set(&mb->waiting_timer, WAITING_TIMEOUT * CLOCK_SECOND,
     send_waiting_timeout, mb);
}

/*
 * Send queue full message to sender of message in uip_buf
 */
static void
mb_send_queue_full()
{
  int f1;
  ERR_PRINTF("mb_send_queue_full().");
  f1 = ZIP_PKT_BUF->flags1 & ZIP_PACKET_FLAGS1_SECURE_ORIGIN;
  SendUDPStatus( ZIP_PACKET_FLAGS0_NACK_RES | ZIP_PACKET_FLAGS0_NACK_QF, f1,
      get_zw_conn());
}

/**
 * Send encapsulated status message to mailbox proxy service
 */
void
mb_send_proxy_status(mailbox_queue_mode_t status)
{
  ZW_MAILBOX_QUEUE_FRAME f;

  /*If the port is not null this is a proxy session*/
  if (mb_state.udp_session.conn.rport)
    {
      ASSERT(mb_state.last_entry);

      f.cmdClass = COMMAND_CLASS_MAILBOX;
      f.cmd = MAILBOX_QUEUE;
      f.param1 = status;
      f.handle = mb_state.handle;
      ZW_SendDataZIP(&mb_state.udp_session, (BYTE*) &f, 3, NULL);

      /*__ZW_SendDataZIP(&cfg.lan_addr, &mb_state.udp_session.conn.ripaddr,
       mb_state.udp_session.conn.rport, (BYTE*) &f, 3, NULL);*/
    }
}

static nodeid_t
mb_get_dnode(uint8_t* ip_pkt)
{
  struct uip_ip_hdr* iph = (struct uip_ip_hdr*) ip_pkt;
  return nodeOfIP(&iph->destipaddr);
}

static void
load_uipbuf_with(mailbox_t* m)
{
  ASSERT(m->data_len);
  memcpy(uip_buf + UIP_LLH_LEN, m->data, m->data_len);
  uip_len = m->data_len;
}

/**
 * Callback when a UDP ping reply has been received
 */
static void
mb_udp_ping_reply(u8_t status, void* data, TX_STATUS_TYPE *t)
{
  mb_state.txStatus = status;
  mb_state_transition(MB_EVENT_SEND_DONE);
}


static command_handler_codes_t
mb_command_handler(zwave_connection_t *c, uint8_t* pData, uint16_t bDatalen)
{
  ZW_APPLICATION_TX_BUFFER* pCmd = (ZW_APPLICATION_TX_BUFFER*) pData;
  WORD tmp_port;

  switch (pCmd->ZW_Common.cmd)
  {
  case MAILBOX_WAKEUP_NOTIFICATION:
    DBG_PRINTF("Got MAILBOX_WAKEUP_NOTIFICATION\n");
    mb_state.node = 0;
    mb_state.handle = pData[2];
    mb_state.udp_session.conn = c->conn;
    mb_state.broadcast_wun = 0; /* Should be zero already, clearing to be sure */
    mb_state_transition(MB_EVENT_WAKEUP);
    break;
  case MAILBOX_CONFIGURATION_GET:
    {
      ZW_MAILBOX_CONFIGURATION_REPORT_FRAME f;
      f.cmdClass = COMMAND_CLASS_MAILBOX;
      f.cmd = MAILBOX_CONFIGURATION_REPORT;
      f.properties1 = MAILBOX_SERVICE_SUPPORTED | MAILBOX_PROXY_SUPPORTED | (cfg.mb_conf_mode & 0x7);

      f.mailboxCapacity1 = (MAX_MAILBOX_ENTRIES >> 8) & 0xFF;
      f.mailboxCapacity2 = (MAX_MAILBOX_ENTRIES >> 0) & 0xFF;

      uip_ipaddr_copy((uip_ip6addr_t* )&f.forwardingDestinationIpv6Address1, &cfg.mb_destination);
//        ERR_PRINTF("cfg.mb_port is %x\n", cfg.mb_port);
      tmp_port = uip_htons(cfg.mb_port);
//        ERR_PRINTF("tmp_port is %x\n", tmp_port);
      memcpy(&f.udpPortNumber, &tmp_port, sizeof(WORD));
//        ERR_PRINTF("f.udpPortNumber is %x\n", f.udpPortNumber);

      ZW_SendDataZIP(c, (BYTE*) &f, sizeof(ZW_MAILBOX_CONFIGURATION_REPORT_FRAME), NULL);
    }
    break;
  case MAILBOX_CONFIGURATION_SET:
    {
      ZW_MAILBOX_CONFIGURATION_SET_FRAME *f = (ZW_MAILBOX_CONFIGURATION_SET_FRAME *) pData;
      uip_ipaddr_copy(&cfg.mb_destination, (uip_ip6addr_t* )&f->forwardingDestinationIpv6Address1);
      cfg.mb_conf_mode = f->properties1 & 7;
      if (cfg.mb_conf_mode == 1) {
        WRN_PRINTF("Mailbox is enabled now\n");
      } else if (cfg.mb_conf_mode == 0) {
        WRN_PRINTF("Mailbox is disabled now\n");
      }

      char output[128] = { 0 };
      snprintf(output, 2, "%01d", cfg.mb_conf_mode);
      config_update("ZipMBMode", output);
      memcpy(&tmp_port, &f->udpPortNumber, sizeof(WORD));
//          DBG_PRINTF("------------------tmp_port: %x\n", tmp_port);
      cfg.mb_port = uip_ntohs(tmp_port);
      sprintf(output, "%d", cfg.mb_port);
      config_update("ZipMBPort", output);
      uip_ipaddr_sprint(output, &cfg.mb_destination);
      config_update("ZipMBDestinationIp6", output);
    }
    break;
  case MAILBOX_NODE_FAILING:
    {
      ZW_MAILBOX_NODE_FAILING_FRAME* f = (ZW_MAILBOX_NODE_FAILING_FRAME*) pData;
      mb_failing_notify_byip((uip_ip6addr_t*) &f->node_ip);
    }
    break;
  case MAILBOX_QUEUE:
    {
      /* I think this should be more like an mailbox encap message */
      ZW_MAILBOX_QUEUE_FRAME *f = (ZW_MAILBOX_QUEUE_FRAME *) pData;
      mailbox_queue_mode_t mode = f->param1 & 0x7;

      uip_len = bDatalen - 3;
      memcpy(uip_buf + UIP_LLH_LEN, f->mailbox_entry, uip_len);

      switch (mode)
      {
      case MAILBOX_ACK:
        /*TODO Check source*/
        mb_state.txStatus = TRANSMIT_COMPLETE_OK;
        mb_state_transition(MB_EVENT_SEND_DONE);
        break;
      case MAILBOX_NAK:
        /*TODO Check source*/
        mb_state.txStatus = TRANSMIT_COMPLETE_FAIL;
        mb_state_transition(MB_EVENT_SEND_DONE);
        break;
      case MAILBOX_QUEUE_FULL:
        mb_send_queue_full();
        break;
      case MAILBOX_WAITING:
        send_waiting(uip_buf + UIP_LLH_LEN);
        break;
      case MAILBOX_PING:
        mb_state.udp_session = *c;
        mb_state.handle = f->handle;
        mb_state_transition(MB_EVNET_PING_REQ);
        break;
      case MAILBOX_PUSH:
        mb_post_uipbuf(&c->ripaddr, f->handle);
        break;
      case MAILBOX_POP:
        if (mb_state.state != MB_STATE_IDLE)
        {
          WRN_PRINTF("Mailbox pop frame dropped because mailbox is not idle...");
          break;
        }

        mb_state.node = mb_get_dnode(f->mailbox_entry);
        mb_state.handle = f->handle;
        mb_state.udp_session = *c;
        mb_state.send_no_more = TRUE;

        /*          mb_state.udp_session.send = p->sendpoint;
         mb_state.udp_session.dend = p->dendpoint;*/

        if (f->param1 & MAILBOX_POP_LAST_BIT)
        {
          mb_state_transition(MB_EVENT_POPLAST);
        }
        else
        {
          mb_state_transition(MB_EVENT_POP);
        }

        break;
      }
    }
    break;
  default:
    return COMMAND_NOT_SUPPORTED;
  }

  return COMMAND_HANDLED;
}




/**
 * Post the package in uip_buf. Returns true if the packages has been posted.
 * Returns false if
 * 1) The destination is not a MAILBOX_NODE
 * 3) We are out of mem
 */
uint8_t
mb_post_uipbuf(uip_ip6addr_t* proxy, uint8_t handle)
{
  mailbox_t* m;
  nodeid_t dnode = nodeOfIP(&UIP_IP_BUF->destipaddr);
  uip_ip6addr_t ip;
  u8_t waiting;
  /*If ICMPv6 only queue Echo request*/
  if (UIP_IP_BUF->proto == UIP_PROTO_ICMP6
      && UIP_ICMP_BUF->type != ICMP6_ECHO_REQUEST)
    {
      return FALSE;
    }

  if (uip_len < UIP_IPUDPH_LEN)
    {
      ERR_PRINTF("Frame too short!");
      return FALSE;
    }

  if (cfg.mb_conf_mode == DISABLE_MAILBOX)
    {
      DBG_PRINTF("Mailbox has been disabled, not posting.\n");
      return TRUE;
    }

  waiting = is_zip_pkt(uip_buf + UIP_LLH_LEN);

  if (cfg.mb_conf_mode == ENABLE_MAILBOX_PROXY_FORWARDING)
    {
      /*TODO use send_mb_proxy_queue command*/
      m = memb_alloc(&mb_memb);
      ASSERT(m);
      ASSERT(uip_len);

      m->data_len = uip_len;
      m->data[0] = COMMAND_CLASS_MAILBOX;
      m->data[1] = MAILBOX_QUEUE;
      m->data[2] = MAILBOX_PUSH;
      m->data[3] = nodeOfIP(&UIP_IP_BUF->destipaddr);
      m->waiting_enabled = waiting;

      /*Convert destination address to IPv4 address if needed */
      /*if (is_4to6_addr((ip6addr_t*) &UIP_IP_BUF ->srcipaddr))
       {
       uip_ipv4addr_t ip4;

       ipv46nat_ipv4addr_of_node(&ip4, nodeOfIP(&UIP_IP_BUF ->destipaddr));
       ip4to6_addr(&UIP_IP_BUF ->destipaddr, (uip_ipaddr_t*) &ip4);
       }*/

      memcpy(m->data + 4, uip_buf + UIP_LLH_LEN, m->data_len);

      DBG_PRINTF("Forwarding to proxy\n");
      ipOfNode(&ip, MyNodeID);
      __ZW_SendDataZIP_ack(&ip, &cfg.mb_destination, cfg.mb_port, m->data,
          uip_len + 4, 0);

      memb_free(&mb_memb, m);
      return TRUE;
    }

  m = memb_alloc(&mb_memb);

  if (!m)
    {
      if (!proxy)
        {
          mb_send_queue_full();
        }
      return FALSE;
    }
  DBG_PRINTF("Frame put in mailbox\n");

  ASSERT(uip_len);
  m->data_len = uip_len;
  m->waiting_enabled = waiting;
  memcpy(m->data, uip_buf + UIP_LLH_LEN, m->data_len);
  DBG_PRINTF("-------------queued\n");
  m->queued_time = clock_seconds();

  /* Send NACK waiting in 200ms,FIXME this also triggers a NACK waiting to other clients,
   * But right now I see no harm in that.
   * */
  ctimer_set(&m->waiting_timer, 200, send_waiting_timeout, m);

  if (proxy)
    {
      uip_ipaddr_copy(&m->proxy, proxy);
      m->handle = handle;

      /*If we have only one memb left use it to send queue full*/
      if (memb_free_count(&mb_memb) == 1)
        {
          send_mb_proxy_queue_command(MAILBOX_QUEUE_FULL, m);
          WRN_PRINTF("Our queue is full, informing proxy\n");
          mb_free_entry(m);
          return TRUE;
        }
    }
  else
    {
      memset(&m->proxy, 0, sizeof(m->proxy));
    }

  list_add(mb_list, m);

  if (rd_get_node_state(dnode) == STATUS_FAILING)
    {
      mb_failing_notify(dnode);
    }

  DBG_PRINTF("We now have %d frames in the mailbox src port %i dst port %i %i\n",
      list_length(mb_list), UIP_HTONS(UIP_UDP_BUF->srcport),
      UIP_HTONS(UIP_UDP_BUF->destport), UIP_UDP_BUF->destport);
  return TRUE;
}

/**
 * Called when we should purge all messages in the mailbox queue
 * destined for a given node.
 * Called directly when a node without fixed wakeup interval has not reported in.
 * Called indirectly when a node with fixed wakeup interval misses its deadline twice.
 */
void
mb_purge_messages(nodeid_t node)
{
  uip_ip6addr_t ip;
  if (cfg.mb_conf_mode == ENABLE_MAILBOX_PROXY_FORWARDING)
  {
    ZW_MAILBOX_NODE_FAILING_FRAME f;
    f.cmdClass = COMMAND_CLASS_MAILBOX;
    f.cmd = MAILBOX_NODE_FAILING;
    ipOfNode((uip_ip6addr_t*) &f.node_ip, node);
    __ZW_SendDataZIP(&cfg.lan_addr, &cfg.mb_destination, cfg.mb_port, &f,
        sizeof(f), 0);
  }
  else
  {
    ipOfNode(&ip, node);
    mb_failing_notify_byip(&ip);
  }
}

/**
 * Called when a node has found to be failing.
 */
void mb_failing_notify(nodeid_t node)
{
  mb_purge_messages(node);
}



static void
mb_send_no_more_information(nodeid_t node)
{
  static const ZW_WAKE_UP_NO_MORE_INFORMATION_FRAME f =
    { COMMAND_CLASS_WAKE_UP, WAKE_UP_NO_MORE_INFORMATION };

  if (mb_state.broadcast_wun == 0) {
    DBG_PRINTF("No more info for node %d putting to sleep\n", node);
    ts_param_t p;
    ts_set_std(&p, node);
    if(!ZW_SendDataAppl(&p, (BYTE*) &f, sizeof(f), mb_send_done, 0)) {
      mb_send_done(TRANSMIT_COMPLETE_FAIL,0, NULL);
    }
  }
  else {
    /* Dont send No More Info for broadcast WUNs */
    /* Clear the flag so we only do this once */
    mb_state.broadcast_wun = 0;
    DBG_PRINTF("Not sending Wake Up No More Info due to broadcast WUN\n");
    /* Fake the send done event so we return to idle without sending anything */
    mb_send_done(TRANSMIT_COMPLETE_OK,0, NULL);
  }

}



void
mb_init()
{
  memb_init(&mb_memb);
  list_init(mb_list);
  memset(black_list_crc16, 0, sizeof(black_list_crc16));

  /* Start the ping timer */
  ctimer_set(&ping_timer, 60 * CLOCK_SECOND, send_ping_timeout, 0);
}

static void
mb_state_transition(mb_event_t new_state);


static uint8_t wakeup_notification[] =
  { COMMAND_CLASS_MAILBOX, MAILBOX_WAKEUP_NOTIFICATION, 0 };

void
mb_wakeup_event(nodeid_t node, u8_t is_broadcast)
{
  uip_ip6addr_t ip;

  if(NetworkManagement_getState() != NM_IDLE) {
    ERR_PRINTF("Wake up notification ignored, because networkmanagemnet FSM is not IDLE\n");
    return;
  }
  switch (cfg.mb_conf_mode)
    {
  case DISABLE_MAILBOX:
    DBG_PRINTF("Mailbox is currently disabled\n");
    break;
  case ENABLE_MAILBOX_SERVICE:
    if (!rd_probe_in_progress() && mb_state.state == MB_STATE_IDLE)
      {
        mb_state.node = node;
        mb_state.send_no_more = 1;
        /* MUST NOT send no more info as reply to broadcast Wake Up Notifications */
        mb_state.broadcast_wun = is_broadcast;
        if (mb_state.broadcast_wun) {
          DBG_PRINTF("Broadcast WUN received\n");
        }
        mb_state_transition(MB_EVENT_WAKEUP);
      }
    break;
  case ENABLE_MAILBOX_PROXY_FORWARDING:
    DBG_PRINTF("Forwarding wakeup notification\n");
    wakeup_notification[2] = node;
    ipOfNode(&ip, MyNodeID);
    __ZW_SendDataZIP(&ip, &cfg.mb_destination, cfg.mb_port,
        (void*) wakeup_notification, sizeof(wakeup_notification), 0);
    break;

    }
}

static void
mb_send_done_event(BYTE status, BYTE* data, uint16_t len)
{
  DBG_PRINTF("Done sending mailbox item status is %i\n",status);
  mb_state.txStatus = status;
  mb_state_transition(MB_EVENT_SEND_DONE);
}


static int 
mb_probe_wakeup_callback(BYTE txStatus, BYTE rxStatus,
    ZW_APPLICATION_TX_BUFFER *pCmd, WORD cmdLength, void* user)
{

  if ((txStatus == TRANSMIT_COMPLETE_OK) && (mb_state.state ==MB_STATE_PROBE_WAKEUP_INTERVAL))
  {
    rd_node_database_entry_t* e = rd_get_node_dbe(mb_state.node);

    e->wakeUp_interval = pCmd->ZW_WakeUpIntervalReportFrame.seconds1 << 16
        | pCmd->ZW_WakeUpIntervalReportFrame.seconds2 << 8
        | pCmd->ZW_WakeUpIntervalReportFrame.seconds3 << 0;

    rd_free_node_dbe(e);
  }
  mb_state_transition(MB_EVENT_SEND_DONE);
  return 0;
}

static void
mb_probe_node_callback(rd_ep_database_entry_t* ep, void* user)
{
  mb_state.state = MB_STATE_SENDING;
  mb_state_transition(MB_EVENT_TRANSITION);
}

static void
mb_send_done(uint8_t status, void* user, TX_STATUS_TYPE *t)
{
  mb_state.txStatus = status;
  mb_state_transition(MB_EVENT_SEND_DONE);
}

static void no_more_timeout(void* user) {
  mb_state_transition(MB_EVENT_TIMEOUT);
}


void mb_put_node_to_sleep_later(nodeid_t node) {

  if(mb_state.state == MB_STATE_IDLE) {
    ctimer_set(&no_more_timer, NO_MORE_TIMEOUT * CLOCK_SECOND, no_more_timeout, 0);
    mb_state.state = MB_STATE_SEND_NO_MORE_INFO_DELAYED;
    mb_state.node = node;
  }
}

/* Should be called when we are performing a state transition,
 * this does not check if the transition is legal.*/
static void
mb_state_transition(mb_event_t event)
{
  rd_node_database_entry_t *n;

  DBG_PRINTF("mb_state_transition event: %s state: %s\n", mb_event_name(event),
             mb_state_name(mb_state.state));
  switch (mb_state.state)
    {
  /* Entry actions ...*/
  case MB_STATE_IDLE:
    if (event == MB_EVNET_TIME_TO_PING)
      {
        mb_state.last_entry = list_head(mb_list);
        mb_state.state = MB_STATE_PING_ZIPCLIENTS;
        mb_state_transition(MB_EVENT_TRANSITION);
      }
    else if (event == MB_EVNET_PING_REQ) /*TODO could we use MB_STATE_PING_ZIPCLIENTS ? */
      {
        DBG_PRINTF("PINGING Z/IP Client on behalf of proxy\n");
        /*NOTE! Here we assume that udp_buf has the payload of the mailbox message*/
        __ZW_SendDataZIP_ack(&cfg.lan_addr, &UIP_IP_BUF->srcipaddr,
        UIP_UDP_BUF->srcport, 0, 0, mb_udp_ping_reply);

        mb_state.state = MB_STATE_SENDING_PING;
      }
    else if (event == MB_EVENT_WAKEUP)
      {
        DBG_PRINTF("MB_STATE_WAKEUP\n");

        /*This is a proxy node info is stored in the mb_state.udp_session structure */
        if (mb_state.node == 0)
          {
            mb_state.last_entry = list_head(mb_list);
            mb_state.state = MB_STATE_SENDING_THROUGH_PROXY;
            mb_state_transition(MB_EVENT_TRANSITION);
            return;
          }

        n = rd_get_node_dbe(mb_state.node);
        if (n)
          {
            /*  If a failing node has reported in it might have become failing becase it has changed its wakeup interval.
             *  If we received a wakeup notification at least 12% before time, the wakeup interval might have been changed */
            if (rd_get_node_state(mb_state.node) == STATUS_FAILING
                ||
                (abs( (int)n->wakeUp_interval - (int)(clock_seconds() - n->lastAwake))
                    > (n->wakeUp_interval >> 3)))
              {
                  /*ERR_PRINTF("Node %i has been failing but is now awake!\n",
                    mb_state.node);*/
                DBG_PRINTF("Node was last awake at %u the clock is %lu the interval should be %u \n",n->lastAwake,clock_seconds(),n->wakeUp_interval);
                mb_state.state = MB_STATE_PROBE_WAKEUP_INTERVAL;
                mb_state_transition(MB_EVENT_TRANSITION); //Fake the send done event
              }
            else // Node is not failing
              {
                mb_state.state = MB_STATE_SENDING;
                mb_state_transition(MB_EVENT_TRANSITION); //Fake the send done event
              }
            rd_free_node_dbe(n);
          }
        else
        {
          mb_state.state = MB_STATE_SEND_NO_MORE_INFO;
          mb_send_no_more_information(mb_state.node);
        }
      }
    else if (event == MB_EVENT_POP)
      {
        WRN_PRINTF("MAILBOX POP\n");
        mb_state.state = MB_STATE_SENDING_FROM_PROXY;
        /*Here we assume that uip_buf has been loaded with the frame to pop*/
        if(!ClassicZIPNode_input(mb_state.node, mb_send_done_event, TRUE, FALSE)) {
          mb_send_done_event(TRANSMIT_COMPLETE_FAIL,0,0);
        }
      }
    else if (event == MB_EVENT_POPLAST)
      {
        WRN_PRINTF("MAILBOX POPLAST\n");
        /*Here we assume that uip_buf has been loaded with the frame to pop*/
        mb_state.state = MB_STATE_SENDING_LAST_FORM_PROXY;
        if(!ClassicZIPNode_input(mb_state.node, mb_send_done_event, TRUE, FALSE)) {
          mb_send_done_event(TRANSMIT_COMPLETE_FAIL,0,0);
        }
      }
    else if (event == MB_EVENT_TRANSITION)
      {
        process_post(&zip_process, ZIP_EVENT_QUEUE_UPDATED, 0);
      }
    break;
  case MB_STATE_PROBE_WAKEUP_INTERVAL:
    if (event == MB_EVENT_TRANSITION)
      {
        static const ZW_WAKE_UP_INTERVAL_GET_FRAME wakeup_get =
         { COMMAND_CLASS_WAKE_UP, WAKE_UP_INTERVAL_GET };
        ts_param_t p;
        ts_set_std(&p, mb_state.node);

        DBG_PRINTF("Wakeup notification received ahead of time\n");
        /*Force a node update */


        if (!ZW_SendRequest(&p, (BYTE*) &wakeup_get, sizeof(wakeup_get),
                  WAKE_UP_INTERVAL_REPORT, 60, 0, mb_probe_wakeup_callback))
        {
          mb_state_transition(MB_EVENT_SEND_DONE);
        }
      }
    else if (event == MB_EVENT_SEND_DONE)
      {
        mb_state.state = MB_STATE_SENDING;
        mb_state_transition(MB_EVENT_TRANSITION);
      }
    break;
  case MB_STATE_PING_ZIPCLIENTS:
    if (event == MB_EVENT_TRANSITION)
      {
        /*Check to see if this is the last entry*/
        if (mb_state.last_entry == 0)
          {
            ctimer_set(&ping_timer, PING_TIMEOUT_SEC * CLOCK_CONF_SECOND,
                send_ping_timeout, 0);
            mb_state.state = MB_STATE_IDLE;
            mb_state_transition(MB_EVENT_TRANSITION);
            return;
          }

        /*Skip pinging if we know the client is offline.*/
        if (!mb_state.last_entry->waiting_enabled)
          {
            mb_state.last_entry = list_item_next(mb_state.last_entry);
            mb_state_transition(MB_EVENT_TRANSITION);
            return;
          }

        /*If this is a proxy item, then send the ping through the proxy*/

        if (uip_is_addr_unspecified(&mb_state.last_entry->proxy))
          {
            zwave_connection_t c;
            struct uip_ip_hdr* iph =
                (struct uip_ip_hdr*) mb_state.last_entry->data;
            struct uip_udp_hdr *udph =
                (struct uip_udp_hdr *) (mb_state.last_entry->data
                    + uip_l3_hdr_len);

            uip_ipaddr_copy(&c.lipaddr, &iph->destipaddr);
            uip_ipaddr_copy(&c.ripaddr, &iph->srcipaddr);
            c.rport = udph->srcport; //Port in network byte order
            c.lport = udph->destport;
            c.scheme = AUTO_SCHEME;

            ZW_COMMAND_ZIP_PACKET* ziph = (ZW_COMMAND_ZIP_PACKET*) (mb_state.last_entry->data + UIP_IPUDPH_LEN);
            c.lendpoint = ziph->dEndpoint;
            c.rendpoint = ziph->sEndpoint;

            DBG_PRINTF("Sending udp ping to ZIP client\n");
            ZW_SendDataZIP_ack(&c, 0, 0, mb_udp_ping_reply);
          }
        else
          {
            DBG_PRINTF("Sending ping through proxy %p\n", mb_state.last_entry);
            send_mb_proxy_queue_command(MAILBOX_PING, mb_state.last_entry);
          }
      }
    else if (event == MB_EVENT_SEND_DONE)
      {
        if (mb_state.txStatus != TRANSMIT_COMPLETE_OK)
          {
            DBG_PRINTF(
                "Client seems to be offline, we will not send more waiting messages for him\n");
          }
        mb_state.last_entry->waiting_enabled =
            mb_state.txStatus == TRANSMIT_COMPLETE_OK ? 1 : 0; //Disable the transmission of waiting
        mb_state.last_entry = list_item_next(mb_state.last_entry);
        mb_state_transition(MB_EVENT_TRANSITION);
      }
    break;
  case MB_STATE_SENDING_THROUGH_PROXY:
    /*
     * Send a frame thorough a proxy service
     */
    if (event == MB_EVENT_TIMEOUT)
      {
        ERR_PRINTF("Timeout while waiting for ACK/NAK from proxy!\n");
        mb_state.state = MB_STATE_IDLE;
        mb_state_transition(MB_EVENT_TRANSITION);
      }
    else if (event == MB_EVENT_TRANSITION || event == MB_EVENT_SEND_DONE)
      {
        mailbox_t* mb;
        int count = 0;
        BOOL last;

        if (is_more_info_set())
          {
            last = FALSE;
          }
        else
          {
            last = TRUE;
          }


        /* Free the previous item if the transmission succeeded. */
        if (event
            == MB_EVENT_SEND_DONE&& mb_state.txStatus == TRANSMIT_COMPLETE_OK)
          {
            mb_free_entry(mb_state.last_entry);
          }

        count = 0;
        /* match and check if it is the last match*/
        for (mb = list_head(mb_list); mb; mb = list_item_next(mb))
          {
            if (uip_ipaddr_cmp(&mb->proxy, &mb_state.udp_session.conn.ripaddr)
                && mb_state.handle == mb->handle)
              {
                if (count == 0)
                  {
                    mb_state.last_entry = mb;
                  }
                else
                  {
                    last = FALSE;
                  }
                count++;
              }
          }

        /*If at least one match was found*/
        if (count)
          {
            send_mb_proxy_queue_command(last << 3 | MAILBOX_POP,
                mb_state.last_entry);
          }
        else
          {
            /* Nothing more to send .*/
            mb_state.last_entry = 0;
            mb_state.state = MB_STATE_IDLE;
            mb_state_transition(MB_EVENT_TRANSITION);
          }
      }
    break;
  case MB_STATE_SENDING:
    /* If frame transmission has failed then send a NAK */
    if(event == MB_EVENT_SEND_DONE ) {
      mailbox_t* mb;

      mb = mb_state.last_entry;
      mb_state.last_entry = list_item_next(mb_state.last_entry);

      if(mb_state.txStatus == TRANSMIT_COMPLETE_OK) {
        mb_free_entry(mb);
      } else {
         /* What to do in case of TRANSMIT_COMPLETE_ERROR? */
         if (mb_state.txStatus == TRANSMIT_COMPLETE_ERROR) {
            WRN_PRINTF("Error in frame in Mailbox\n");
         }
      }
    } else if(event == MB_EVENT_TRANSITION) {
      mb_state.last_entry = list_head(mb_list);
    }

    /*Find the next entry belonging to this node */
    while(mb_state.last_entry) {
      if (mb_get_dnode(mb_state.last_entry->data) == mb_state.node) {
        break;
      }
      mb_state.last_entry = list_item_next(mb_state.last_entry);
    }

    if(mb_state.last_entry) {
      DBG_PRINTF("We now have %d frames in the mailbox - sending\n",
          list_length(mb_list));
      load_uipbuf_with(mb_state.last_entry);

      if (is_more_info_set())
        {
          mb_state.send_no_more = 0;
        }
      if(ClassicZIPNode_input(mb_state.node, mb_send_done_event, TRUE, FALSE)==0) {
        mb_state.state = MB_STATE_SEND_NO_MORE_INFO;
        mb_state_transition(MB_EVENT_TRANSITION);
      }
      uip_len = 0;
    } else {
      if (mb_state.send_no_more)
      {
        mb_state.state = MB_STATE_SEND_NO_MORE_INFO;
        mb_state_transition(MB_EVENT_TRANSITION);
      }
      else
      {
        mb_state.state = MB_STATE_SEND_NO_MORE_INFO_DELAYED;
        ctimer_set(&no_more_timer, NO_MORE_TIMEOUT * CLOCK_SECOND, no_more_timeout, 0);
        process_post(&zip_process, ZIP_EVENT_QUEUE_UPDATED, 0);
      }
    }
    break;
  case MB_STATE_SEND_NO_MORE_INFO_DELAYED:
    if(event == MB_EVENT_TIMEOUT) {
      if (rd_node_in_probe(mb_state.node) || sleeping_node_is_in_firmware_upgrade(mb_state.node)) {
        DBG_PRINTF("Node: %d is still in probe or firmware upgrade. Delaying Wake up No more info\n", mb_state.node);
        ctimer_set(&no_more_timer, NO_MORE_TIMEOUT * CLOCK_SECOND, no_more_timeout, 0);
      } else {
        mb_state.state = MB_STATE_SEND_NO_MORE_INFO;
        mb_send_no_more_information(mb_state.node);
      }
    } else if(event == MB_EVENT_FRAME_SENT_TO_NODE) {
      ctimer_set(&no_more_timer, NO_MORE_TIMEOUT * CLOCK_SECOND, no_more_timeout, 0);
    }
  break;
  case MB_STATE_SEND_NO_MORE_INFO:
    if (event == MB_EVENT_TRANSITION)
      {
        /*Send ACK to proxy */
        mb_send_proxy_status(
            mb_state.txStatus == TRANSMIT_COMPLETE_OK ?
                MAILBOX_ACK : MAILBOX_NAK);
        mb_send_no_more_information(mb_state.node);
      }
    else if (event == MB_EVENT_SEND_DONE)
      {
        mb_state.state = MB_STATE_IDLE;
        mb_state_transition(MB_EVENT_TRANSITION);
      }
    break;
  case MB_STATE_SENDING_FROM_PROXY:
    if (event == MB_EVENT_SEND_DONE)
      {
        /*Send ACK to proxy */
        mb_send_proxy_status(
            mb_state.txStatus == TRANSMIT_COMPLETE_OK ?
                MAILBOX_ACK : MAILBOX_NAK);
        mb_state.state = MB_STATE_IDLE;
        mb_state_transition(MB_EVENT_TRANSITION);
      }
    break;
  case MB_STATE_SENDING_LAST_FORM_PROXY:
    if (event == MB_EVENT_SEND_DONE)
      {
        mb_state.state = MB_STATE_SEND_NO_MORE_INFO;
        mb_state_transition(MB_EVENT_TRANSITION);
      }
    break;
  case MB_STATE_SENDING_PING:
    {
      mb_send_proxy_status(
          mb_state.txStatus == TRANSMIT_COMPLETE_OK ?
              MAILBOX_ACK : MAILBOX_NAK);
      mb_state.state = MB_STATE_IDLE;
      mb_state_transition(MB_EVENT_TRANSITION);
    }
    }
}

uint8_t
mb_enabled()
{
  return cfg.mb_conf_mode != DISABLE_MAILBOX;
}

uint8_t
mb_is_busy(void)
{
  DBG_PRINTF("Mailbox state: %s\n", mb_state_name(mb_state.state));
  return !((mb_state.state == MB_STATE_IDLE) ||
           (mb_state.state == MB_STATE_SENDING_PING) ||
           (mb_state.state == MB_STATE_PING_ZIPCLIENTS) ||
           (mb_state.state == MB_STATE_SEND_NO_MORE_INFO_DELAYED));

}

bool mb_idle(void)
{
  return (mb_state.state == MB_STATE_IDLE);
}

void mb_abort_sending() {
  if(mb_is_busy()) {
    mb_state.state = MB_STATE_IDLE;
    ClassicZIPNode_AbortSending();
  }
}

void mb_node_transmission_ok_event(nodeid_t node) {
  if(mb_state.node == node) {
    mb_state_transition(MB_EVENT_FRAME_SENT_TO_NODE);
  }
}


nodeid_t mb_get_active_node() {
  if(mb_idle()) {
    return 0;
  } else {
    return mb_state.node;
  }
}



REGISTER_HANDLER(
    mb_command_handler,
    0,
    COMMAND_CLASS_MAILBOX, MAILBOX_VERSION_V2, SECURITY_SCHEME_UDP);

