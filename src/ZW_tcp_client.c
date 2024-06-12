/* © 2014 Silicon Laboratories Inc.
 */

/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/

#ifdef AXTLS
#include "os_port.h"
#include "ssl.h"
#else
#include <openssl/ssl.h>
#define ssl_ctx_free SSL_CTX_free
#define ssl_free SSL_free
#define ssl_read SSL_read
#define ssl_write SSL_write
#define SSL_SESSION_ID_SIZE 32
#endif

#include "contiki-net-ipv4.h"
#include "net/tcpip_ipv4.h"
#include <string.h>

#include "ZW_tcp_client.h"
#include "uip-packetqueue.h"
#include "sys/etimer.h"

#include "router_events.h"
#include "zip_router_config.h"
#include <TYPES.H>
#include "ZW_classcmd.h"
#include "ZW_classcmd_ex.h"
#include "ZIP_Router_logging.h"

extern process_event_t tcpip_event;

#define QUEUE_PACKET_TIMEOUT 0x124F80UL //20 mins
#ifdef __ASIX_C51__
#include "axled.h"
#include "ntp_client.h"
#define data 	__data	  
U8_T gconfig_ValidateCertPartition(void) REENTRANT;
u8_t RecopyCertToIntFlash(void) REENTRANT;
void Portal_Led_Intvl_Set(uint32_t intvl);
PROCESS_NAME(ntpclient_process);
#endif

//extern unsigned int totalmem;
struct uip_packetqueue_handle ipv6TxQueue;
extern struct uip_packetqueue_handle sslread_queue;
struct uip_packetqueue_handle sslwrite_queue;
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

uint8_t retx_buf[1500]; /* Buffer used to store last packet sent, in case we need to retransmit */
uint16_t retx_len;      /* Length of packet in retx_buf */

/****************************************************************************/
/*                              PRIVATE FUNCTIONS                           */
/****************************************************************************/

/****************************************************************************/
/*                              EXPORTED FUNCTIONS                          */
/****************************************************************************/

extern uip_buf_t uip_aligned_buf;
#define uip_buf (uip_aligned_buf.u8)

#define BUF 				((struct uip_eth_hdr *)&uip_ipv4_buf[0])
#define UIP_IP_BUF   		((struct uip_ip_hdr *)&uip_ipv4_buf[UIP_LLH_LEN])
#define UIP_UDP_BUF  		((struct uip_udp_hdr *)&uip_ipv4_buf[uip_l2_l3_hdr_len])

PROCESS(zip_tcp_client_process, "ZIP TCP client process");

static uip_ipaddr_t * portal_ipaddr = NULL;
struct uip_conn *tcp_tunnelconn = NULL;
extern u16_t uip_len;

#define KEEP_ALIVE_TIMEOUT	    300L
#define CLIENT_TIMEOUT			8L
#define CLIENT_TIMEOUT_NO_CONN	15L
#define SSL_TIMEOUT				5L
#define HNAME_RESOL_TIMEOUT 	5L

#ifdef __ASIX_C51__
#define SSLWATCHDOG_TIMEOUT   600L
#else
#define SSLWATCHDOG_TIMEOUT   60L
#endif

struct etimer et_tcpclient, et_keepalive, et_sslclean;
struct etimer et_hname;
struct etimer et_sslwatchdog;
static uint8_t IsClientTimer = 0, IsAliveTimer = 0, IsSslTimer = 0;
static uint8_t IsWatchdogTimer = 0;
static char * portalhost = 0;

extern uint8_t NTP_READY;
#define KEEP_ALIVE "KEEPALIVE"

extern uip_lladdr_t uip_lladdr;

static void
Portal_CommandHandler(uint8_t *buf, int len) REENTRANT;

static void
stop_watchdog_timer(void);


struct pushed_configuration
{

  uip_ip6addr_t lan_address; //may be 0
  u8_t lan_prefix_length;

  uip_ip6addr_t tun_prefix;
  u8_t tun_prefix_length;

  uip_ip6addr_t gw_address; //default gw address, may be 0.

  uip_ip6addr_t pan_prefix; //pan prefix may be 0... prefix length is always /64
}CC_ALIGN_PACK;

struct manufacturer_specific
{
  u16_t manufacturer_id;
  u16_t product_type;
  u16_t product_id;
}CC_ALIGN_PACK;

uint8_t isSSLConnClose = 0;
uint8_t isHshakeFailed = 0;
uint8_t gisZIPRReady = ZIPR_NOT_READY;
uint8_t gIsSslHshakeInProgress = 0;
uint8_t gisPortalClose = 0;
uint8_t gisTnlPacket = FALSE;
void
SerialFlushQueue(void);

static struct pt pt_portal_hostname_resolver =
  { 0 };

void
uip_debug_ipaddr_print(const uip_ip6addr_t *addr);

#define uip_is_addr_unspecified(a)               \
  ((((a)->u16[0]) == 0) &&                       \
   (((a)->u16[1]) == 0) &&                       \
   (((a)->u16[2]) == 0) &&                       \
   (((a)->u16[3]) == 0) &&                       \
   (((a)->u16[4]) == 0) &&                       \
   (((a)->u16[5]) == 0) &&                       \
   (((a)->u16[6]) == 0) &&                       \
   (((a)->u16[7]) == 0))

void
tcpip_input(void);

//ssl stuff
SSL_CTX *gssl_client_ctx = NULL;
SSL *gssl_client = NULL;

#ifndef AXTLS
static BIO* rbio;
static BIO* wbio;
#else
SSL_CTX *
ssl_client_init(void);
SSL *
do_ssl_client_handshake(SSL_CTX *ssl_ctx, int client_fd);
extern uint8_t gsession_id[];
#endif

int gRemainDatLen = 0;
int gOrigDatLen = 0;

//#define TUNNEL_TAP

#ifdef TUNNEL_TAP
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/if_tun.h>

static int dbg_fd;
static void dbg_tun_init()
{
  struct ifreq ifr;

  dbg_fd = open("/dev/net/tun", O_RDWR);
  if(dbg_fd == -1)
  {
    //perror("tapdev: tapdev_init: open");
    return;
  }

  memset(&ifr, 0, sizeof(ifr));
  ifr.ifr_flags = IFF_TAP|IFF_NO_PI;
  if (ioctl(dbg_fd, TUNSETIFF, (void *) &ifr) < 0)
  {
    //perror("Debug tap init error\n");
    exit(1);
  }
}
#endif
/* 
 Portal_CommandHandler process the input packet from portal/tunnel and send it to uip6 stack
 */

static void
Portal_CommandHandler(uint8_t *buf, int len)REENTRANT
{
  ZW_APPLICATION_TX_BUFFER* pCmd = (ZW_APPLICATION_TX_BUFFER*) buf;

  if (buf == NULL)
  {
    return;
  }

  //printf("Portal_CommandHandler. cmdclass = %d  cmd = %d\r\n", pCmd->ZW_Common.cmdClass, pCmd->ZW_Common.cmd);

  //Tunneled Ipv6 ZIP packet always has first byte 0x6
  if (pCmd->ZW_Common.cmdClass == COMMAND_CLASS_ZIP_PORTAL)
  {
    switch (pCmd->ZW_Common.cmd)
    {
      case GATEWAY_CONFIGURATION_SET:
      {
        ZW_GATEWAY_CONFIGURATION_STATUS GwConfigStatus;
        ZW_GATEWAY_CONFIGURATION_SET *pGwConfigSet =
            (ZW_GATEWAY_CONFIGURATION_SET *) buf;

        GwConfigStatus.cmdClass = COMMAND_CLASS_ZIP_PORTAL;
        GwConfigStatus.cmd = GATEWAY_CONFIGURATION_STATUS;

        if (parse_portal_config(&pGwConfigSet->payload[0],
            (len - sizeof(ZW_GATEWAY_CONFIGURATION_SET) + 1)))
        {
          GwConfigStatus.status = ZIPR_READY_OK;
          //Send the ZIPR Ready Ack to Portal
#ifdef __ASIX_C51__
          Portal_Led_Intvl_Set(NO_BLINK);
#endif		 
          if (uip_packetqueue_alloc(&ipv6TxQueue, (uint8_t *) &GwConfigStatus,
              sizeof(GwConfigStatus), QUEUE_PACKET_TIMEOUT) == 0)
          {
            ERR_PRINTF("ipv6TxQueue Full-1. This must not happen.\r\n");
          }
          else
          {
            stop_watchdog_timer();
            gisZIPRReady = ZIPR_READY;
            process_post(&zip_process, ZIP_EVENT_TUNNEL_READY, 0);
            LOG_PRINTF("Send the ZIPR Ready message to portal.\r\n");
          }
        }
        else
        {
          GwConfigStatus.status = INVALID_CONFIG;
          if (uip_packetqueue_alloc(&ipv6TxQueue, (uint8_t *) &GwConfigStatus,
              sizeof(GwConfigStatus), QUEUE_PACKET_TIMEOUT) == 0)
          {
            ERR_PRINTF("ipv6TxQueue Full-2. This must not happen.\r\n");
          }
        }

      }
        break;

      case GATEWAY_CONFIGURATION_GET:
      {
        uint8_t buf[128] =
          { 0 };  //buffer should be more than size of portal config + 2
        ZW_GATEWAY_CONFIGURATION_REPORT *pGwConfigReport =
            (ZW_GATEWAY_CONFIGURATION_REPORT *) buf;
        portal_ip_configuration_st_t *portal_config =
            (portal_ip_configuration_st_t *) (&pGwConfigReport->payload[0]);

        pGwConfigReport->cmdClass = COMMAND_CLASS_ZIP_PORTAL;
        pGwConfigReport->cmd = GATEWAY_CONFIGURATION_REPORT;

        memcpy(&portal_config->lan_address, &cfg.lan_addr, IPV6_ADDR_LEN);
        portal_config->lan_prefix_length = cfg.lan_prefix_length;

        memcpy(&portal_config->tun_prefix, &cfg.tun_prefix, IPV6_ADDR_LEN);
        portal_config->tun_prefix_length = cfg.tun_prefix_length;

        memcpy(&portal_config->gw_address, &cfg.gw_addr, IPV6_ADDR_LEN);

        memcpy(&portal_config->pan_prefix, &cfg.pan_prefix, IPV6_ADDR_LEN);

        if (uip_packetqueue_alloc(&ipv6TxQueue, (uint8_t *) buf,
            (sizeof(ZW_GATEWAY_CONFIGURATION_REPORT) - 1
                + sizeof(portal_ip_configuration_st_t)), QUEUE_PACKET_TIMEOUT)
            == 0)
        {
            ERR_PRINTF("ipv6TxQueue Full-3. This must not happen.\r\n");
        }

      }
        break;

      case GATEWAY_UNREGISTER:
      {
        WRN_PRINTF("Unregistered Gateway. Exiting the Portal process.\r\n");
        uip_close();
        gisPortalClose = TRUE;
      }
        break;

      default:
        WRN_PRINTF("Un-Identified Portal command received.\r\n");
        break;
    }
  }
  else if (gisZIPRReady == ZIPR_READY)
  {
    // printf("ZIPR ready and calling applications.\r\n");
    if (len > (UIP_BUFSIZE - UIP_LLH_LEN))
    {
      ERR_PRINTF("Bigger ZIP Packet received. Drop it !!!!\r\n");
      gRemainDatLen = 0;
      return;
    }
    memcpy(&uip_buf[UIP_LLH_LEN], buf, len);

    //change the packet type to IPV6
    ((struct uip_eth_hdr *) &uip_buf[0])->type = UIP_HTONS(UIP_ETHTYPE_IPV6);

    uip_len = UIP_LLH_LEN + len;

    uip_buf[UIP_LLH_LEN+3] = FLOW_FROM_TUNNEL;//Assign the flow label to 1 to mark this package as coming from the tunnel

#ifdef TUNNEL_TAP
    if(write(dbg_fd, &uip_buf[0], uip_len));
#endif
    //pass the ZIP packet(ETH+Ipv6+UDP)to ipv6 stack;
    //printf("tcpinp st 0x%lx uip_len = %u \r\n", (unsigned long)clock_time(), uip_len);

    gisTnlPacket = TRUE;

    tcpip_input();

    gisTnlPacket = FALSE;
    // printf("tcpinp end 0x%lx \r\n", (unsigned long)clock_time());
  }

  return;
}

/*
 parse_portal_config validates and parse the received portal configuration.
 It also stores the received configuration in external NVM
 */
uint8_t
parse_portal_config(uint8_t *buf, uint8_t len) REENTRANT
{
  uint8_t result = 0;
  uint16_t config_len = sizeof(struct pushed_configuration);

  struct pushed_configuration *portal_config =
      (struct pushed_configuration *) buf;

  if (len >= config_len)
  {
    DBG_PRINTF("Config Received from portal:--- Start\r\n");

    if ((uip_is_addr_mcast(&portal_config->lan_address))
        || (uip_is_addr_mcast(&portal_config->pan_prefix))
        || (uip_is_addr_mcast(&portal_config->tun_prefix))
        || (uip_is_addr_mcast(&portal_config->gw_address)))
    {
        DBG_PRINTF("Invalid config(multicast) received. ---End\r\n");
      return result;

    }
    else if ((uip_is_addr_loopback(&portal_config->lan_address))
        || (uip_is_addr_loopback(&portal_config->pan_prefix))
        || (uip_is_addr_loopback(&portal_config->tun_prefix))
        || (uip_is_addr_loopback(&portal_config->gw_address))
        || (uip_is_addr_unspecified(&portal_config->tun_prefix))
        || (portal_config->tun_prefix_length == 0))
    {

        DBG_PRINTF("Invalid config(loopback/unspecified) received. ---End\r\n");
      return result;

    }

    if (!uip_is_addr_unspecified(&portal_config->lan_address))
    {
      memcpy(&cfg.cfg_lan_addr, &portal_config->lan_address, IPV6_ADDR_LEN);
      LOG_PRINTF("LAN Addr: ");
      uip_debug_ipaddr_print(&cfg.cfg_lan_addr);
    }
    else
    {
      memset(&cfg.cfg_lan_addr, 0, IPV6_ADDR_LEN);
      LOG_PRINTF("LAN Addr: ::\r\n");
    }

    cfg.cfg_lan_prefix_length = portal_config->lan_prefix_length;

    LOG_PRINTF("LAN Prefix Length = %d\r\n", cfg.cfg_lan_prefix_length);

    memcpy(&cfg.tun_prefix, &portal_config->tun_prefix, IPV6_ADDR_LEN);
    cfg.tun_prefix_length = portal_config->tun_prefix_length;

    LOG_PRINTF("Tunnel Prefix Length = %d\r\n", cfg.tun_prefix_length);

    if (!uip_is_addr_unspecified(&portal_config->gw_address))
    {
      memcpy(&cfg.gw_addr, &portal_config->gw_address, IPV6_ADDR_LEN);
      DBG_PRINTF("GW Addr: ");
      uip_debug_ipaddr_print(&cfg.gw_addr);
    }
    else
    {
      memset(&cfg.gw_addr, 0, IPV6_ADDR_LEN);
      LOG_PRINTF("GW Addr: ::\r\n");
    }

    if (!uip_is_addr_unspecified(&portal_config->pan_prefix))
    {
      memcpy(&cfg.cfg_pan_prefix, &portal_config->pan_prefix, IPV6_ADDR_LEN);
      LOG_PRINTF("PAN Prefix Addr: ");
      uip_debug_ipaddr_print(&cfg.cfg_pan_prefix);
    }
    else
    {
      memset(&cfg.cfg_pan_prefix, 0, IPV6_ADDR_LEN);
      LOG_PRINTF("PAN Addr: ::\r\n");
    }

    LOG_PRINTF("Tunnel Prefix: ");
    uip_debug_ipaddr_print(&cfg.tun_prefix);

    LOG_PRINTF("tun_prefix_length = %d\r\n", cfg.tun_prefix_length);

    result = 1;

    LOG_PRINTF("Portal config ---End\r\n");
  }
  else
  {
    ERR_PRINTF("Invalid configuration received from portal. len = %d\r\n", len);
  }

  return result;
}

static void
restart_SSL_clean_timer(void)
{
  unsigned long timeout = 0;

  if (IsSslTimer == 1)
  {
    etimer_stop(&et_sslclean);
  }
  timeout = (SSL_TIMEOUT * CLOCK_SECOND);
  etimer_set(&et_sslclean, timeout);
  IsSslTimer = 1;
  DBG_PRINTF("restart_SSL_timer done.\r\n");
  return;
}

static void
restart_client_timer(unsigned int tout)
{
  unsigned long timeout = 0;

  if (IsClientTimer == 1)
  {
    etimer_stop(&et_tcpclient);
  }
  timeout = (tout * CLOCK_SECOND);
  //printf("restart_client_timer done.\r\n");
  etimer_set(&et_tcpclient, timeout);
  IsClientTimer = 1;

  return;
}

static void
restart_alive_timer(void)
{
  unsigned long timeout = 0;
  if (IsAliveTimer == 1)
  {
    etimer_stop(&et_keepalive);
  }

  timeout = (KEEP_ALIVE_TIMEOUT * CLOCK_SECOND);
  etimer_set(&et_keepalive, timeout);
  IsAliveTimer = 1;
  //printf("Keep-Alive timer started.\r\n");
  return;
}

static void
restart_watchdog_timer(void)
{
  unsigned long timeout = 0;

  if (IsWatchdogTimer == 1)
  {
    etimer_stop(&et_sslwatchdog);
  }
  timeout = (SSLWATCHDOG_TIMEOUT * CLOCK_SECOND);
  etimer_set(&et_sslwatchdog, timeout);
  IsWatchdogTimer = 1;
  ERR_PRINTF("SSL Watchdog timer (re)started.\r\n");
  return;
}

static void
stop_client_timer(void)
{
  if (IsClientTimer == 1)
  {
    etimer_stop(&et_tcpclient);
    IsClientTimer = 0;
  }

  return;
}

static void
stop_alive_timer(void)
{
  if (IsAliveTimer == 1)
  {
    etimer_stop(&et_keepalive);
    IsAliveTimer = 0;
  }
  //printf("Keep Alive timer STOPPED.\r\n");

  return;
}

static void
stop_watchdog_timer(void)
{
  if (IsWatchdogTimer == 1)
  {
    etimer_stop(&et_sslwatchdog);
    IsWatchdogTimer = 0;
  }
  ERR_PRINTF("SSL Watchdog timer STOPPED.\r\n");

  return;
}

static void
stop_SSL_clean_timer(void)
{
  if (IsSslTimer == 1)
  {
    etimer_stop(&et_sslclean);
    IsSslTimer = 0;
  }
  //printf("SSL clean timer STOPPED.\r\n");
  return;
}

/* 
 tunnel_SSL_clean Clean the SSL Tunnel Connection
 IsContextClean		TRUE 	:	Cleans SSL session information
 FALSE 	:	SSL session information/context won't be cleared
 waitforclean   		TRUE	:	Wait for the uip stack to close the connection
 FALSE:  :	Restart client connect timer to retry periodically
 */
static void
tunnel_SSL_clean(uint8_t IsContextClean, uint8_t waitforclean)
{

  gisZIPRReady = ZIPR_NOT_READY;

  gisTnlPacket = FALSE;
  gRemainDatLen = 0;

  stop_SSL_clean_timer();

  if (IsContextClean == TRUE)
  {
    DBG_PRINTF("Cleaned SSL context.\r\n");
    ssl_ctx_free(gssl_client_ctx);

    gssl_client_ctx = NULL;

    gssl_client = NULL;

    /*		memset(&gsession_id, 0, SSL_SESSION_ID_SIZE);*/
  }
  else
  {
    ssl_free(gssl_client);
    gssl_client = NULL;
  }

  gIsSslHshakeInProgress = 0;

  //stop the keep alive timer
  stop_alive_timer();

  //printf("No of messages in ipv6TxQueue = %u flushed\r\n", uip_packetqueue_len(&ipv6TxQueue));

  //Free the data in the queue
  while (uip_packetqueue_len(&ipv6TxQueue))
  {
    uip_packetqueue_pop(&ipv6TxQueue);
  }

#ifdef AXTLS
  while (uip_packetqueue_len(&sslread_queue))
  {
    uip_packetqueue_pop(&sslread_queue);
  }
#endif

  while (uip_packetqueue_len(&sslwrite_queue))
  {
    uip_packetqueue_pop(&sslwrite_queue);
  }

  if (waitforclean == TRUE)
  {
    isSSLConnClose = 1;
  }
  else
  {
    //start the retry connect timer.
    restart_client_timer(CLIENT_TIMEOUT);
    isSSLConnClose = 0;
  }

  //printf("uip_ipv4_flags = 0x%x\r\n", (unsigned)uip_ipv4_flags);

  return;
}

#ifndef AXTLS
static int
ssl_output()
{
  int len;
  len = BIO_read(wbio, &uip_ipv4_buf[UIP_LLH_LEN + UIP_IPTCPH_LEN],
      UIP_BUFSIZE - UIP_LLH_LEN - UIP_IPTCPH_LEN);
  if (len > 0)
  {
    retx_len = len;
    memcpy(retx_buf, &uip_ipv4_buf[UIP_LLH_LEN + UIP_IPTCPH_LEN], len);
    uip_ipv4_send(&uip_ipv4_buf[UIP_LLH_LEN + UIP_IPTCPH_LEN], len);
    return len;
  }
  else
  {
    return 0;
  }
}

/* Retransmit the same packet again.
 * Should be done when requested by TCP stack. */
static void ssl_retransmit()
{
  uip_ipv4_send(retx_buf, retx_len);
}
#endif
/*---------------------------------------------------------------------------*/
/* 
 * zip_tcp_client_appcall handles the tcp events/data received by zip_tcp_client_process
 */
static void
zip_tcp_client_appcall(void *data)REENTRANT
{
  int len = 0;
  int ssl_err;
  uint8_t read_buf[512];

  uint16_t ipv6_packet_len = 0;

  //Handling of old connections that are not active
  if (uip_ipv4_conn != tcp_tunnelconn)
  {
    ERR_PRINTF(
        "Conenction with lport = %u tcpflags = 0x%02x is NOT active. Abort it.\r\n",
        uip_ipv4_conn->lport, uip_ipv4_conn->tcpstateflags);

    if (!(uip_closed() || uip_aborted() || uip_timedout()))
    {
      //Any other condition abort the connection.
      uip_abort();
    }
    //MUST return from here.
    return;
  }
  if (uip_connected())
  {
#ifdef __ASIX_C51__
    Portal_Led_Intvl_Set(PORTAL_CONNECT_PROGRESS_BLINK_INVL_TICKS);
#endif
    stop_client_timer();

    if (gssl_client_ctx == NULL)
    {
      //create ssl client context
#ifdef AXTLS
      gssl_client_ctx = ssl_client_init();
#else
      SSL_library_init();
      SSL_load_error_strings();
      gssl_client_ctx = SSL_CTX_new(TLS_client_method());
#endif
      if (gssl_client_ctx == NULL)
      {
        ERR_PRINTF("ssl_client_init failed.\r\n");
        return;
        //TODO: Do we need to exit the process ??
      }
      else
      {
#ifndef AXTLS
        if (!SSL_CTX_use_certificate_file(gssl_client_ctx, cfg.cert,
            SSL_FILETYPE_PEM))
        {
            ERR_PRINTF("Error loading certificate, please check the file.\n");
        }

        if (!SSL_CTX_use_PrivateKey_file(gssl_client_ctx, cfg.priv_key,
            SSL_FILETYPE_PEM))
        {
            ERR_PRINTF("Error loading key, please check the file.\n");
        }
#endif
        DBG_PRINTF("Client init DONE..................\n");
      }
    }
    ERR_PRINTF("starting SSL negotiation....\r\n");
    restart_watchdog_timer();
    //Uip stack doesn't have any socket descriptor..always pass 0x1
#ifdef AXTLS
    gssl_client = do_ssl_client_handshake(gssl_client_ctx, 0x1);
#else
    gssl_client = SSL_new(gssl_client_ctx);
    rbio = BIO_new(BIO_s_mem());
    //BIO_set_nbio(rbio, 1);
    wbio = BIO_new(BIO_s_mem());
    //BIO_set_nbio(wbio, 1);
    SSL_set_bio(gssl_client, rbio, wbio);

    SSL_set_connect_state(gssl_client);
    SSL_do_handshake(gssl_client);
    ssl_output();
#endif

    isHshakeFailed = 0;

    isSSLConnClose = 0;

    gRemainDatLen = 0;

    //start the keep alive timer
    restart_alive_timer();


    //Connection has been established
    DBG_PRINTF("ZIP TCP connection has been established.\r\n");
#ifdef AXTLS
    return;
#endif
  }

  //TODO: Handle abort and timeout scenarios
  if (uip_closed() || uip_aborted() || uip_timedout())
  {
#ifdef __ASIX_C51__
    Portal_Led_Intvl_Set(NO_PORTAL_CONNECT_BLINK_INVL_TICKS);
#endif
    ERR_PRINTF("zip tcp clinet connection closed/aborted/timedout. \r\n");
    if (isHshakeFailed == 1)
    {
      isHshakeFailed = 0;
      tunnel_SSL_clean(TRUE, FALSE);
    }
    else
    {
      tunnel_SSL_clean(FALSE, FALSE);
    }

    //After receiving abort return
    return;

  }

  if (uip_acked())
  {
    uip_packetqueue_pop(&sslwrite_queue);
    //printf("zip tcp clinet: data done:q len = %d\r\n", tcp_packetqueue_len(&sslwrite_queue));
    if(uip_packetqueue_buflen(&ipv6TxQueue)) {
      process_post(&tcpip_ipv4_process,0, tcp_tunnelconn);
    }
  }

  if (uip_newdata())
  {
    //Ethernet + IPv4 + TCP + Payload(IPv6 ZIP Packet)

    // printf("IPv4 received len = %u\r\n", uip_ipv4_datalen());

    if ((gssl_client != NULL) && (uip_ipv4_datalen() > 0))
    {
#ifndef AXTLS
      len = BIO_write(rbio, uip_ipv4_appdata, uip_ipv4_datalen());
      if (len > 0)
      {
           //
      }
      else
      {
        ERR_PRINTF("BIO Write failed\n");
      }

      if ((len = SSL_read(gssl_client, &read_buf, sizeof(read_buf))) < 0)
      {
        ssl_err = SSL_get_error(gssl_client,len);
        if (SSL_is_init_finished(gssl_client) && (ssl_err != SSL_ERROR_WANT_READ))
        {

            ERR_PRINTF("ssl_read failed = %d ssl error code %i\r\n", len,ssl_err);

          gisZIPRReady = ZIPR_NOT_READY;
          ssl_free(gssl_client);
          gssl_client = NULL;
          if (uip_packetqueue_len(&sslwrite_queue) > 0)
          {
             ERR_PRINTF("No of messages in sslwrite_queue = %d\r\n",
                uip_packetqueue_len(&sslwrite_queue));
            restart_SSL_clean_timer();
          }
          else
          {
            uip_close();
            tunnel_SSL_clean(FALSE, FALSE);
            return;
          }
        }
      }
      else
      {
        Portal_CommandHandler(read_buf, len);
      }

#else
      gOrigDatLen = uip_ipv4_datalen();
      gRemainDatLen = 0;
      do
      {
        if(ssl_handshake_status(gssl_client) != SSL_OK)
        {
          SerialFlushQueue();
          gIsSslHshakeInProgress = 1;
          if ((res = ssl_read(gssl_client, NULL)) < 0)
          {
            gisZIPRReady = ZIPR_NOT_READY;
            ssl_free(gssl_client);
            gssl_client = NULL;
            isHshakeFailed = 1;
            printf("SSL handshake FAILED.\r\n");
            //wait for 5 seconds to empty the queue and restart the handshake.
            if (uip_packetqueue_len(&sslwrite_queue) > 0)
            {
              //printf("No of messages in sslwrite_queue = %u\r\n", uip_packetqueue_len(&sslwrite_queue));
              restart_SSL_clean_timer();
            }
            else
            {
              uip_close();
              //If handshake fails....clean the context also
              tunnel_SSL_clean(TRUE, FALSE);
              return;
            }
            break;
          }
          if(ssl_handshake_status(gssl_client) == SSL_OK)
          {
            printf("SSL handshake successful.\r\n");

            //SSL handshake is done
            gIsSslHshakeInProgress = 0;

#if ENABLE_SESSION_RESUMPTION
            memcpy(gsession_id, ssl_get_session_id(gssl_client), SSL_SESSION_ID_SIZE);
            //printf("SSL handshake done. totalmem = %u\r\n", totalmem);
            printf("Session-ID: ");
            for (i = 0; i < SSL_SESSION_ID_SIZE; i++)
            {
              printf("%02bX", gsession_id[i]);
            }
            printf("\r\n");

            gisZIPRReady = HANDSHAKE_DONE;

          }
        }
        else
#else

        if (SSL_is_init_finished(gssl_client))
#endif
        {
#ifdef AXTLS
          if ((len = ssl_read(gssl_client, &read_buf)) < 0)
#else
          if ((len = SSL_read(gssl_client, &read_buf, sizeof(read_buf))) < 0)
#endif
          {
            printf("ssl_read failed = %d\r\n", len);

            gisZIPRReady = ZIPR_NOT_READY;
            ssl_free(gssl_client);
            gssl_client = NULL;
            if (uip_packetqueue_len(&sslwrite_queue) > 0)
            {
              printf("No of messages in sslwrite_queue = %d\r\n",
                  uip_packetqueue_len(&sslwrite_queue));
              restart_SSL_clean_timer();
            }
            else
            {
              uip_close();
              tunnel_SSL_clean(FALSE, FALSE);
              return;
            }
            break;
          }

          if (len > 0)
          {
            Portal_CommandHandler(read_buf, len);
          }
        }
      }
      while (gRemainDatLen != 0);
#endif

#ifdef AXTLS

      while (uip_packetqueue_len(&sslread_queue))
      {
        uip_packetqueue_pop(&sslread_queue);
        printf("SSL read queue cleaning done.\r\n");
      }
#endif
      //restart the keep alive timer
      restart_alive_timer();
    }
  }

  if (uip_rexmit())
  {
    ssl_retransmit();
    restart_alive_timer();
  }
  else if (uip_poll())
  {
  //Application has sent poll request/pending retransmission as it has something to send
    if ((gssl_client != NULL)
        &&
#ifdef AXTLS

        (!gssl_client->bm_read_index) && //axTLS uses single buffer per connection. Don't call write if read is in progress
#endif
        (uip_packetqueue_len(&sslwrite_queue) == 0) && ((ipv6_packet_len =
            uip_packetqueue_buflen(&ipv6TxQueue)) > 0)
        && uip_packetqueue_free_len())
    {
      if (ssl_write(gssl_client, (uint8_t*) uip_packetqueue_buf(&ipv6TxQueue),
          ipv6_packet_len) <= 0)
      {
        //it seems we don't have enough memory to send out packet.
        printf("ssl_write: Failed\r\n");
        gisZIPRReady = ZIPR_NOT_READY;
        ssl_free(gssl_client);
        gssl_client = NULL;
        restart_SSL_clean_timer();
      }
      else
      {
        uip_packetqueue_pop(&ipv6TxQueue);
      }
    }

#ifdef AXTLS
    if ((ipv6_packet_len = uip_packetqueue_buflen(&sslwrite_queue)) > 0)
    {
      //printf("ipv6_packet_len = %u\r\n", ipv6_packet_len);

      if (ipv6_packet_len > (UIP_BUFSIZE - (UIP_LLH_LEN + UIP_IPTCPH_LEN)))
      {
        ipv6_packet_len = (UIP_BUFSIZE - (UIP_LLH_LEN + UIP_IPTCPH_LEN));
        printf("Bigger packet over tunnel truncated.\r\n");
      }
      memcpy(&uip_ipv4_buf[UIP_LLH_LEN + UIP_IPTCPH_LEN],
          uip_packetqueue_buf(&sslwrite_queue), ipv6_packet_len);

      uip_ipv4_send(&uip_ipv4_buf[UIP_LLH_LEN + UIP_IPTCPH_LEN],
          ipv6_packet_len);

#if 0
      printf("Poll/Re-transmit data:\r\n");

      if ( ipv6_packet_len >= 48)
      {

        for(i=0;i<(ipv6_packet_len - 48); i++)
        {
          printf("0x%bx ", (unsigned char)uip_ipv4_buf[14+40+48+i]);
        }
        printf("\r\n");
      }
      else
      {
        //Keep alive
        for(i=0;i<(ipv6_packet_len); i++)
        {
          printf("0x%bx ", (unsigned char)uip_ipv4_buf[14+40+i]);
        }
        printf("\r\n");
      }
      //printf("uip poll: IPv6 data sent out over IPv4. ipv6_packet_len = %u\r\n", ipv6_packet_len);
#endif

      //restart the keep alive timer
      restart_alive_timer();
    }
#else
    if (ssl_output())
    {
      restart_alive_timer();
    }
#endif

  }

  //close the ssl connection: ssl error happened; application waited for 5 seconds to send any messages pending in queue.
  if (isSSLConnClose == 1)
  {
    isSSLConnClose = 0;

    uip_close();

    //Start the client connect timer
    restart_client_timer(CLIENT_TIMEOUT);
  }

  return;
}

#ifdef AXTLS
int
send_ssl_to_tcp_client(void *pData, int len)
{

  int ret_len = len;
#ifdef __ASIX_C51__
  unsigned long timeout = 0x124F80; //20 mins
#else
  unsigned long timeout = 0xffff;

#endif

  if (ret_len > tcp_tunnelconn->mss)
  {
    //printf("send_ssl_to_tcp_client: received BIGGER packet.!!!\r\n");
    ret_len = tcp_tunnelconn->mss;
  }

  if (uip_packetqueue_alloc(&sslwrite_queue, (uint8_t *) pData, ret_len,
          timeout) == 0)
  {
    printf("SSL:sslwrite_queue is full = %d!!!\r\n",
        uip_packetqueue_len(&sslwrite_queue));
    //printf("uip_packetqueue_free_len Len = %d\r\n", uip_packetqueue_free_len());
    return 0;
  }

  return ret_len;
}
#endif

void
send_ipv6_to_tcp_ssl_client(void)
{
#ifdef __ASIX_C51__
  unsigned long timeout = 0x60000; //6 mins
#else
  unsigned long timeout = 0xFFFF;
#endif
#ifdef TUNNEL_TAP
  ((struct uip_eth_hdr *)&uip_buf[0])->type = UIP_HTONS(UIP_ETHTYPE_IPV6);
  if(write(dbg_fd, &uip_buf[0], uip_len+UIP_LLH_LEN));
#endif

  if ((gisZIPRReady == ZIPR_READY) && (uip_packetqueue_free_len() > 1))
  {
    if (uip_packetqueue_alloc(&ipv6TxQueue, (uint8_t *) &uip_buf[UIP_LLH_LEN],
        uip_len, timeout) == 0)
    {
      ERR_PRINTF("No memory ..ipv6TxQueue alloc failed.\r\n");
      return;
    }
    tcpip_ipv4_poll_tcp(tcp_tunnelconn);
  }
  else
  {
    ERR_PRINTF("UIP Queue Full or ZIPR Not Ready !!!\r\n");
#if 0
    printf("ipv6TxQueue Len = %d\r\n", uip_packetqueue_len(&ipv6TxQueue));
    printf("uip_packetqueue_free_len Len = %d\r\n", uip_packetqueue_free_len());
#endif
  }
  return;
}

static struct uip_conn *
tcp_client_connect(void)
{
  struct uip_conn *tcpconn = NULL;

  if (portal_ipaddr)
  {
    tcpconn = tcp_ipv4_connect(portal_ipaddr, UIP_HTONS(cfg.portal_portno),
        NULL);
  }
  if (tcpconn == NULL)
  {
    uip_abort();
    ERR_PRINTF("TCP connection to portal server failed.\r\n");
  }
  DBG_PRINTF("tcp_client_connect: executed.\r\n");
  return tcpconn;
}

static void
send_keepalive(void) REENTRANT
{
  char ref_buf[32] =
    { 0 };
  uint8_t len = 0;
#ifdef __ASIX_C51__
  unsigned long timeout = 0x60000; //6 mins
#else
  unsigned long timeout = 0xFFFF;
#endif

  //printf("start sending keep-alive\r\n");
  strcpy(ref_buf, KEEP_ALIVE);

  len = strlen(ref_buf) + 1;
  if (gisZIPRReady == ZIPR_READY)
  {
    if (uip_packetqueue_alloc(&ipv6TxQueue, (uint8_t *) ref_buf, len, timeout)
        == 0)
    {
      ERR_PRINTF("No memory send keep alive failed.\r\n");
      return;
    }
    tcpip_ipv4_poll_tcp(tcp_tunnelconn);
  }
  else
  {
    WRN_PRINTF("send_keepalive sent before client set-up. Dropped !!!\r\n");
  }

  return;
}

static int
uiplib_ip4addrconv(const char *addrstr, uip_ip4addr_t *ipaddr) REENTRANT
{
  unsigned char tmp;
  char c;
  unsigned char i, j;

  tmp = 0;

  for (i = 0; i < 4; ++i)
  {
    j = 0;
    do
    {
      c = *addrstr;
      ++j;
      if (j > 4)
      {
        return 0;
      }
      if (c == '.' || c == 0)
      {
        ipaddr->u8[i] = tmp;
        tmp = 0;
      }
      else if (c >= '0' && c <= '9')
      {
        tmp = (tmp * 10) + (c - '0');
      }
      else
      {
        return 0;
      }
      ++addrstr;
    }
    while (c != '.' && c != 0);
  }
  return 1;
}

/*-----------------------------------------------------------------------------------*/
/*
 TcpTunnel_ReStart restarts the tcp client and ntp processes.
 */
void
TcpTunnel_ReStart(void)
{
  DBG_PRINTF("TCP Tunnel restart called.\r\n");
  process_exit(&zip_tcp_client_process);
  process_start(&zip_tcp_client_process, cfg.portal_url);
#ifdef __ASIX_C51__
  if(process_is_running(&zip_tcp_client_process))
  {
    process_exit(&ntpclient_process);
    process_start(&ntpclient_process, NULL);
  }
#endif
}

static PT_THREAD(portal_hostname_resolver(void))
{
  static struct pt *pt_this = &pt_portal_hostname_resolver;
  static uip_ip4addr_t addr;

  /*
   * A proto thread starts with PT_BEGIN() and ends with
   * PT_END().
   */

  PT_BEGIN(pt_this)
    ;

    if (uiplib_ip4addrconv(portalhost, &addr))
    {
      portal_ipaddr = &addr;
      PT_EXIT(pt_this);
    }

    LOG_PRINTF("Looking for portal host= %s \r\n", portalhost);

    while (1)
    {
      portal_ipaddr = resolv_lookup(portalhost);
      if (portal_ipaddr == NULL)
      {
        resolv_query(portalhost);
      }
      else
      {
        LOG_PRINTF("Resolver: Server IP = %d.%d.%d.%d\r\n",
            portal_ipaddr->u8[0], portal_ipaddr->u8[1], portal_ipaddr->u8[2],
            portal_ipaddr->u8[3]);
        PT_EXIT(pt_this);
      }
      etimer_set(&et_hname, (HNAME_RESOL_TIMEOUT * CLOCK_SECOND));
      PT_WAIT_UNTIL(pt_this, etimer_expired(&et_hname));
    }

    /*
     * A proto thread starts with PT_BEGIN() and ends with
     * PT_END().
     */
  PT_END(pt_this);
}

PROCESS_THREAD(zip_tcp_client_process, ev, data)
{
PROCESS_BEGIN()
  ;
  LOG_PRINTF("ZIP TCP Client Started.\r\n");
#ifdef __ASIX_C51__
  Portal_Led_Intvl_Set(NO_PORTAL_CONNECT_BLINK_INVL_TICKS);
#endif

  if (strlen(data) == 0)
  {
    LOG_PRINTF("No portal host defined. Running without portal.\n");
    PROCESS_EXIT()
    ;
  }
  else
  {
    portalhost = (char*) data;
  }
#ifdef __ASIX_C51__
//verify the certificate partition
  if(gconfig_ValidateCertPartition() != TRUE)
  {
    printf("TCP Tunnel: certificate verification  failed\r\n");

    //It may be because of device turned off when cert copy is in progress.
    //Re-copy the cert data from external flash to internal flash;
    RecopyCertToIntFlash();

    if (gconfig_ValidateCertPartition() != TRUE)
    {
      printf("FATAL: Certificates corrupted. Can't be recovered.\r\n");
    }
    else
    {
      printf("Certifcate checksum passed.\r\n");
    }
  }
  else
  {
    printf("Cert checksum verification passed.\r\n");
  }
#endif

#ifdef TUNNEL_TAP
  dbg_tun_init();
#endif
  uip_packetqueue_new(&ipv6TxQueue);

#ifdef AXTLS
  uip_packetqueue_new(&sslread_queue);
  uip_packetqueue_new(&sslwrite_queue);
#endif

#ifdef __ASIX_C51__
  printf("Waiting for the NTP Event.\r\n");
  PROCESS_WAIT_EVENT_UNTIL(ev == NTP_READY);
#endif

  /*Wait until you get portal server ip address */
  PROCESS_PT_SPAWN(&(pt_portal_hostname_resolver), portal_hostname_resolver());

  LOG_PRINTF("Portal: Server IP = %d.%d.%d.%d\r\n", portal_ipaddr->u8[0],
      portal_ipaddr->u8[1], portal_ipaddr->u8[2], portal_ipaddr->u8[3]);

  memset(&et_tcpclient, 0, sizeof(et_tcpclient));
  memset(&et_keepalive, 0, sizeof(et_keepalive));
  memset(&et_sslclean, 0, sizeof(et_sslclean));
  memset(&et_sslwatchdog, 0, sizeof(et_sslwatchdog));

  tcp_tunnelconn = tcp_client_connect();

  if (tcp_tunnelconn == NULL)
  {
    ERR_PRINTF("Exit TCP Client Process.\r\n");
    process_exit(&zip_tcp_client_process);
  }

//Send the ZIPR Message to Portal

  while (1)
  {
    PROCESS_WAIT_EVENT()
    ;

    if (ev == PROCESS_EVENT_EXIT)
    {
      LOG_PRINTF("ZIP TCP Client Exit received.\r\n");
      tunnel_SSL_clean(TRUE, FALSE);
#ifdef __ASIX_C51__
      Portal_Led_Intvl_Set(NO_PORTAL_CONNECT_BLINK_INVL_TICKS);
#endif
      stop_client_timer();
    }
    else if (ev == tcpip_ipv4_event)
    {
      zip_tcp_client_appcall(data);
    }
    else if (ev == PROCESS_EVENT_TIMER)
    {
      //printf("Event timer event.\r\n");
      if (((data == &et_tcpclient) && etimer_expired(&et_tcpclient))
          || ((data == &et_hname) && etimer_expired(&et_hname))

          )
      {
        /*Wait until you get portal server ip address */
        PROCESS_PT_SPAWN(&(pt_portal_hostname_resolver),
            portal_hostname_resolver());

        //printf("Reconnect timer.\r\n");
        tcp_tunnelconn = tcp_client_connect();
        if (tcp_tunnelconn == NULL)
        {
          //No TCP connections are available....they may be timewait state
          //retry for every 15 seconds
          WRN_PRINTF("No TCP connections available... retrying....");
          restart_client_timer(CLIENT_TIMEOUT_NO_CONN);
        }
      }

      if ((data == &et_keepalive) && etimer_expired(&et_keepalive))
      {
        send_keepalive();
        //printf("Keep Alive timer.\r\n");
        etimer_restart(&et_keepalive);
      }

      if ((data == &et_sslwatchdog) && etimer_expired(&et_sslwatchdog))
      {
        ERR_PRINTF("SSL Watchdog timer. calling tunnel_SSL_clean.\r\n");
        tunnel_SSL_clean(TRUE, FALSE);
      }

      if ((data == &et_sslclean) && etimer_expired(&et_sslclean))
      {
        //printf("SSL clean timer.\r\n");
        if (isHshakeFailed == 1)
        {
          isHshakeFailed = 0;
          tunnel_SSL_clean(TRUE, TRUE);
        }
        else
        {
          tunnel_SSL_clean(FALSE, TRUE);
        }
      }
    }
  }

PROCESS_END();
}

