/* © 2019 Silicon Laboratories Inc. */

#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include "zw_network_info.h"
#include "zip_router_ipv6_utils.h"
#include "ZIP_Router.h"
#include "router_events.h"

#include "provisioning_list.h" /* init */
#include "contiki.h"
#include "contiki-net.h"
#include "node_queue.h"
#include "mDNSService.h"

#include "ipv4_interface.h"
#include "Serialapi.h"
#include "dev/serial-line.h"

#include "multicast_group_manager.h"
#include "ZW_SendRequest.h"
#include "ZW_udp_server.h"
#include "ZW_zip_classcmd.h"
#include "ZW_SendDataAppl.h"
#ifdef SECURITY_SUPPORT
#include "security_layer.h"
#endif
#include "transport_service2.h"
#include "ZW_ZIPApplication.h"
#include "NodeCache.h"

#include "CC_NetworkManagement.h"
#include "CC_PowerLevel.h"
#include "Mailbox.h"

#include "ipv46_nat.h"
#include "dhcpc2.h"

#include "serial_api_process.h"
#define DEBUG DEBUG_FULL
#include "net/uip.h"
#include "net/uip-ds6.h"
#include "net/uip-icmp6.h"
#include "net/uip-debug.h"
#include "net/uiplib.h"
#include "etimer.h"

#include "CC_Gateway.h"
#include "lib/rand.h"

#include "ClassicZIPNode.h"
#include "NodeCache.h"
#include "ResourceDirectory.h"
#include "security_layer.h"
#include "s2_keystore.h"
#include "S2_wrap.h"

#include "ZW_tcp_client.h"
#include "ZW_udp_server.h"
#include "CC_FirmwareUpdate.h"
#include "DTLS_server.h"
#include "ZIP_Router_logging.h"
#include "zgw_crc.h"
//#include "ZW_queue.h"
#include "Bridge.h"
#include "RD_DataStore.h" /* For data_store_init() */
#include "ZW_tcp_client.h"
#include "S2.h"
#include "random.h"

//#include "dev/eeprom.h"

#include "ZW_ZIPApplication.h"
#include "uip-packetqueue.h"

#include "command_handler.h"
#include "zgw_backup.h"
#include "ZW_transport_api.h"
#include "RF_Region_Set_Validator.h"

#define TX_QUEUE_LIFETIME 10000

#ifdef DEBUG_WEBSERVER
#include "webserver-nogui.h"
#endif

extern BYTE serial_ok;

extern const char* linux_conf_provisioning_cfg_file;
extern const char* linux_conf_provisioning_list_storage_file;
extern const char* linux_conf_fin_script;
extern void keystore_public_key_debug_print(void);


/*---------------------------------------------------------------------------*/
PROCESS(zip_process, "ZIP process");
PROCESS_NAME(udp_server_process);
PROCESS_NAME(coap_server_process);
PROCESS_NAME(mDNS_server_process);
PROCESS_NAME(dtls_server_process);
PROCESS_NAME(zip_tcp_client_process);
PROCESS_NAME(resolv_process);

#ifndef __ASIX_C51__
AUTOSTART_PROCESSES(&zip_process);
#endif



#define BUF ((struct uip_eth_hdr *)&uip_buf[0])
#define IPBUF ((struct uip_tcpip_hdr *)&uip_buf[UIP_LLH_LEN])

/**
 * State flags of the \ref ZIP_Router components.
 *
 * TODO: Maintain correctly.
 */
typedef enum zgw_state {
   /** ZGW process just launching */
   ZGW_BOOTING =                            0x0001,
   /** Bridge is busy. */
   ZGW_BRIDGE =                             0x0002,
   /** Network Management */
   ZGW_NM =                                 0x0004,
   /** Backup is requested or running. */
   ZGW_BU =                                 0x0400,
   /** ZIP Router is resetting. */
   ZGW_ROUTER_RESET =                       0x0800,
   /** Zipgateway is shutting down. */
   ZGW_SHUTTING_DOWN =                      0x8000,
} zgw_state_t;

/** Test if \p comp is currently active.
 * \param comp A component flag of type zgw_state_t.
 */
#define ZGW_COMPONENT_ACTIVE(comp) (zgw_state & comp)

/**
 * \ingroup zgw_backup
 * Interval of the backup idle-check timer (\ref zgw_bu_timer).
 */
#define ZGW_BU_CHECK_INTERVAL (3 * CLOCK_SECOND)

/* ****************** */
/*global configuration*/
/* ****************** */

/** The ZGW configuration settings. */
struct router_config cfg;

nodeid_t MyNodeID; // ID of this node
DWORD homeID;  // Home ID in network byte order (big endian)

controller_role_t controller_role;

static u8_t ipv4_init_once = 0;

/**
 * State of the \ref ZIP_Router component.
 *
 * This is really just a uint that is big enough to hold the highest value in the enum zgw_state.
 */
zgw_state_t zgw_state = ZGW_BOOTING;

/*
 * Flag to indicate if the zipgateway process is still initializing after process start.
 *
 * Init is considered to be complete when NM is done.
 */
static BOOL zgw_initing = TRUE;

/**
 * \ingroup zgw_backup
 * Periodic timer to check if the zipgateway is idle and can start the
 * requested backup.
 *
 * We use set, not restart, on this timer, since it does not have to be
 * stable.
 */
struct etimer zgw_bu_timer;

/* ********************** */
/* LAN interface hook */
extern void
system_net_hook(int init);

/* IPv4 helper */
extern void
tcpip_ipv4_force_start_periodic_tcp_timer(void);

#ifdef TEST_MULTICAST_TX
uint8_t sec2_send_multicast_nofollowup(ts_param_t *p, const uint8_t * data, uint8_t data_len,ZW_SendDataAppl_Callback_t callback);
#endif

/* **** declarations ****** */
/* TODO: do we need other components to know this function? */
bool zgw_component_idle(zgw_state_t comp);

/** Do a backup of the zipgateway if all components are now idle.
 *
 * Backup should only happen when there is no active traffic or
 * administration in progress.  This function checks that all relevant
 * components are idle.  If they are, it calls the \ref zgw_backup
 * component to do a synchronous backup.
 *
 * \param data Pointer to user data or NULL.
 */
void zip_router_check_backup(void *data);

/* Dummy linklayer address based on HOME ID used to track where packages come from. */
uip_lladdr_t pan_lladdr;
const uip_lladdr_t tun_lladdr =
  {
    { 0xFF, 0xFF, 0xEE, 0xEE, 0xEE, 0xEE } };

static u8_t
(*landev_send)(uip_lladdr_t *a);


/* ********************** */
/*    static functions    */
/* ********************** */

const char* zgw_status_text(zgw_state_t st) {
  static char str[25];

  switch (st) {
     /** ZGW process just launching */
  case ZGW_BOOTING: return "BOOTING";
   /** Bridge is busy. */
  case ZGW_BRIDGE: return "BRIDGE initialization";
   /** Network Management */
  case ZGW_NM: return "Network Management";
     /** Backup is requested. */
  case ZGW_BU: return "Backup request";
   /** ZIP Router is resetting. */
  case ZGW_ROUTER_RESET: return "ZGW_ROUTER resetting";
   /** Zipgateway is shutting down. */
  case ZGW_SHUTTING_DOWN: return "Shutting down";
  }
}

static void zgw_component_done(zgw_state_t comp, void *data) {
   zgw_state &= ~comp;
   zip_router_check_backup(data);
}

static void zgw_component_start(zgw_state_t comp) {
   zgw_state |= comp;
}

/* Something completed, check if we should do backup. */
void zip_router_check_backup(void *data) {
   
   if (ZGW_COMPONENT_ACTIVE(ZGW_BU)) {
      if (zgw_idle()) {
         DBG_PRINTF("Backup can start now\n");
         zgw_backup_now();
         zgw_component_done(ZGW_BU, 0);
         /* Just let the timer run out, zip_process does nothing when BU
          * is not requested */
      } else {
         /* TODO-BU: remove print */
         DBG_PRINTF("Waiting for zipgateway to complete processing before backing up.\n");
         etimer_set(&zgw_bu_timer, ZGW_BU_CHECK_INTERVAL);
      }
   }
}

/* ********************** */
/*        functions       */
/* ********************** */

bool zgw_component_idle(zgw_state_t comp) {
   return !(zgw_state & comp);
}

/* We can ignore running timers in mailbox (ping timer) and RD
 * (dead_nodes_worker), since the actual backing up is synchronous. */
/* TODO: remove the debugging */
bool zgw_idle() {
   if ((!zgw_initing)
       && process_is_running(&serial_api_process) /* serial works */
       && (zgw_component_idle(ZGW_BOOTING)) /* zipgateway is up and running */
       && (process_nevents() <= 1) /* contiki - serial line process always posts "continue" to itself. */
       && bridge_idle()
       && (zgw_component_idle(ZGW_ROUTER_RESET)) /* Eg, we are not updating after a set-as-SIS */
       && NetworkManagement_idle()
       && !rd_probe_in_progress() /* When RD goes idle, it always
                                   * report back t someone else, so it
                                   * does not give idle
                                   * notifications. */
       && PowerLevel_idle()
       && mdns_idle()
       && mb_idle() /* When MB goes idle, it always starts the node
                     * queues, so it does not give idle
                     * notifications. */
       && ZW_SendDataAppl_idle()
       && node_queue_idle() /* node queue state idle, both queues empty */
      ) {
      return true;
   } else {
      DBG_PRINTF("Components: %x, contiki: %d\n",
                 zgw_state, process_nevents());
      DBG_PRINTF("bridge%s idle \n", (bridge_idle())?" ":" not");
      DBG_PRINTF("Mailbox%s idle \n", (mb_idle())?" ":" not");
      DBG_PRINTF("mDNS%s idle \n", (mdns_idle())?" ":" not");
      DBG_PRINTF("NM%s idle \n", (NetworkManagement_getState() == NM_IDLE)?" ":" not");
      DBG_PRINTF("RD probe%s idle \n", (!rd_probe_in_progress())?" ":" not");
      DBG_PRINTF("PowerLevel%s idle \n", (PowerLevel_idle())?" ":" not");
      DBG_PRINTF("SendData%s idle \n", (ZW_SendDataAppl_idle())?" ":" not");
      DBG_PRINTF("node q%s idle \n", (node_queue_idle())?" ":" not");
      return false;
   }
}

/* ******* IP/LAN ******** */
void
set_landev_outputfunc(u8_t
(*f)(uip_lladdr_t *a))
{
  landev_send = f;
}

/*static void debug_print_time() {
    time_t     now;
    struct tm  ts;
    char       buf[80];
    // Get the current time
    time(&now);
    // Format and print the time, "hh:mm:ss "
    ts = *localtime(&now);
    strftime(buf, sizeof(buf), "%H:%M:%S", &ts);
    printf("%s", buf);
}*/

/**
 * Create a ULA prefix according to rfc4193
 */
static void create_ula_prefix(uip_ip6addr_t* a, u16_t subnet_id)
{
  a->u16[0] = UIP_HTONS((0xFD << 8) | (random_rand() & 0xFF));
  a->u16[1] = random_rand();
  a->u16[2] = random_rand();
  a->u16[3] = UIP_HTONS(subnet_id);
  a->u16[4] = 0;
  a->u16[5] = 0;
  a->u16[6] = 0;
  a->u16[7] = 0;
}


struct ctimer sleep_fw_upgrade_timer;
void fw_upgrade_timeout(void *user)
{
    rd_node_database_entry_t *n = user;
    if (n && (n->mode == MODE_FIRMWARE_UPGRADE)) {
      n->mode = MODE_MAILBOX;
      DBG_PRINTF("Node: %d is timed out with firmware upgrade. Mailbox enabled for this node\n", n->nodeid);
    }
}

/* ******* Z-Wave ******** */

/*========================   ApplicationCommandHandler   ====================
 **    Handling of a received application commands and requests
 **
 ** This is an handler for all incomming non-IP frames. This is not only RAW z-wave frames
 ** but also decrypted and multicannel stripped frames.
 **
 **--------------------------------------------------------------------------*/
void
ApplicationCommandHandlerZIP(ts_param_t *p, ZW_APPLICATION_TX_BUFFER *pCmd,
    WORD cmdLength) CC_REENTRANT_ARG
{
  if (cmdLength == 0)
    return;

  LOG_PRINTF("ApplicationCommandHandler %d->%d [%s] \n",
      (int )p->snode,(int)p->dnode, print_frame((const char *)pCmd, cmdLength));


  ZW_FIRMWARE_UPDATE_MD_REQUEST_REPORT_V3_FRAME
    *fwUpdateMdReqReportV3 = (ZW_FIRMWARE_UPDATE_MD_REQUEST_REPORT_V3_FRAME *)pCmd;

  if (pCmd->ZW_Common.cmdClass == COMMAND_CLASS_FIRMWARE_UPDATE_MD) {
    rd_node_database_entry_t *n = rd_get_node_dbe(p->snode);
    if (n)
    {
      if((pCmd->ZW_Common.cmd == FIRMWARE_UPDATE_MD_REQUEST_REPORT) &&
         (fwUpdateMdReqReportV3->status == 0xFF)) { // if status is okay
        if (n->mode == MODE_MAILBOX) { 
          n->mode = MODE_FIRMWARE_UPGRADE;
          DBG_PRINTF("Node: %d has started firmware upgrade. Mailbox disabled for this node\n", p->snode);
        }
      } else if(pCmd->ZW_Common.cmd == FIRMWARE_UPDATE_MD_STATUS_REPORT) {
        if (n->mode == MODE_FIRMWARE_UPGRADE) { 
          n->mode = MODE_MAILBOX;
          DBG_PRINTF("Node: %d is done with firmware upgrade. Mailbox enabled for this node\n", p->snode);
        }
      } else if (pCmd->ZW_Common.cmd == FIRMWARE_UPDATE_MD_GET) {
        // If incase firmware upgrade stalls, and the node becomes MODE_MAILBOX in fw_upgrade_timeout() 
        // And if we see more frames of MODE_FIRMWARE_UPGRADE from node, we need to set the node back to
        // MODE_FIRMWARE_UPGRADE state.
        if (n->mode == MODE_MAILBOX) { 
          n->mode = MODE_FIRMWARE_UPGRADE;
          DBG_PRINTF("Node: %d is still in progress. Mailbox disabled for this node\n", p->snode);
        }
 
        // Set a timeout of 10 seconds to set the mode back to MODE_MAILBOX on receival of each FIRMWARE_UPDATE_MD_GET
        // So incase something wrong goes and firmware upgrade stalls, the node will go back to being
        // mailbox node
        ctimer_set(&sleep_fw_upgrade_timer, CLOCK_SECOND*10, fw_upgrade_timeout ,n);
      }
    }
  }
  switch (pCmd->ZW_Common.cmdClass)
  {
    case COMMAND_CLASS_CONTROLLER_REPLICATION:
      ZW_ReplicationReceiveComplete();
    break;
    case COMMAND_CLASS_CRC_16_ENCAP:
      if (pCmd->ZW_Common.cmd != CRC_16_ENCAP)
      {
        return;
      }
      if (p->scheme != NO_SCHEME)
      {
        WRN_PRINTF("Security encapsulated CRC16 frame received. Ignoring.\n");
        return;
      }

      if (zgw_crc16(CRC_INIT_VALUE, &((BYTE*) pCmd)[0], cmdLength) == 0)
      {
        p->scheme = USE_CRC16;
        ApplicationCommandHandlerZIP(p,
            (ZW_APPLICATION_TX_BUFFER*) ((BYTE*) pCmd + 2), cmdLength - 4);
      }
      else
      {
        ERR_PRINTF("CRC16 Checksum failed\n");
      }
      return;
    case COMMAND_CLASS_SECURITY:
      if(isNodeBad(p->snode)) {
          WRN_PRINTF("Dropping security0 package from KNWON BAD NODE\n");
          return;
      }
 
      if (pCmd->ZW_Common.cmd != SECURITY_COMMANDS_SUPPORTED_REPORT)
      {
        security_CommandHandler(p,pCmd, cmdLength); /* IN Number of command bytes including the command */
        return;
      }
      break;
    case COMMAND_CLASS_SECURITY_2:
      if(isNodeBad(p->snode)) {
          WRN_PRINTF("Dropping security2 package from KNWON BAD NODE\n");
          return;
      }
      if (pCmd->ZW_Common.cmd != SECURITY_2_COMMANDS_SUPPORTED_REPORT)
      {
        security2_CommandHandler(p,pCmd,cmdLength);
        return;
      }
      break;
    case COMMAND_CLASS_TRANSPORT_SERVICE:
      if (p->scheme != NO_SCHEME)
      {
        WRN_PRINTF("Security encapsulated transport service frame received. Ignoring.\n");
        return;
      }
      TransportService_ApplicationCommandHandler(p, (BYTE *)pCmd,
          cmdLength);
      return;
  case COMMAND_CLASS_MULTI_CHANNEL_V2:
      if (pCmd->ZW_MultiChannelCmdEncapV2Frame.cmd == MULTI_CHANNEL_CMD_ENCAP_V2
          && cmdLength > 4)
    {
      /*Strip off multi cannel encap */
      p->sendpoint = pCmd->ZW_MultiChannelCmdEncapV2Frame.properties1;
      p->dendpoint = pCmd->ZW_MultiChannelCmdEncapV2Frame.properties2;
      /* Ignore when frame sent to GW endpoint other than 0 */
      if ((p->dnode == MyNodeID) && (p->dendpoint > 0))
      {
        WRN_PRINTF("Multi channel frame received %d->%d towards ep %d. Ignoring.\n", p->snode, p->dnode, p->dendpoint);
        return;
      }

      if (p->dendpoint & MULTI_CHANNEL_CMD_ENCAP_PROPERTIES2_BIT_ADDRESS_BIT_MASK_V2) { //if its bit address do not respond
          WRN_PRINTF("multi channel command with bit address bit sent. Igoring the command \n");
          return;
      }
        ApplicationCommandHandlerZIP(p,
            (ZW_APPLICATION_TX_BUFFER*) &pCmd->ZW_MultiChannelCmdEncapV2Frame.encapFrame,
            cmdLength - 4);
      return;
    }
  } // end case
  // If we got here, the frame has been transport layer unwraped...
  ZW_SendDataAppl_FrameRX_Notify(p, (const uint8_t*)pCmd, cmdLength);

  /*
   * Check if this was a reply to a request we have made
   */
  if (SendRequest_ApplicationCommandHandler(p, pCmd, cmdLength))
  {
	  return;
  }

  /* In general, frames to other nodeIDs than ours are for virtual nodes, and should be directed to bridge_virtual_node_commandhandler.
   * But there is an exception: Frames received via S2 multicast have their destination nodes altered to the multicast group ID by libs2.
   * They also have the RECEIVE_STATUS_TYPE_BROAD set. Such frames never go to the bridge_..._commandhandler().
   * Note: This check does not allow S2 multicast to virtual nodes. So far, this is not a feature the ZIPGW supports.
   * All S2 multicast will go to the unsolicited destination, regardless of IP associations. */
  /* TODO: Support S2 multicast to ZIP clients via IP Association (filed under ZGW-935). */
  if( !(p->rx_flags & RECEIVE_STATUS_TYPE_BROAD) &&
      (p->dnode != MyNodeID) && bridge_virtual_node_commandhandler(p, (BYTE*) pCmd, cmdLength)
    ) {
      if (cmdLength >= sizeof(ZW_WAKE_UP_INTERVAL_REPORT_FRAME)) {
        if ((pCmd->ZW_Common.cmdClass == COMMAND_CLASS_WAKE_UP) 
            && (pCmd->ZW_Common.cmd == WAKE_UP_INTERVAL_REPORT)) {
          uint8_t *c = (uint8_t *)pCmd;
          rd_node_database_entry_t* nd;
          nd = rd_node_get_raw(p->snode);
          if (nd) {
            uint32_t interval = pCmd->ZW_WakeUpIntervalReportFrame.seconds1 << 16
                                | pCmd->ZW_WakeUpIntervalReportFrame.seconds2 << 8
                                | pCmd->ZW_WakeUpIntervalReportFrame.seconds3 << 0;
            if (nd->wakeUp_interval != interval) { 
              nd->wakeUp_interval = interval;
              DBG_PRINTF("Wakeup report frame intercepted. "
                         "Wakeup Interval for nodeid:%d set to %d seconds\n",
                         p->dnode,
                         interval);
              rd_data_store_nvm_free(nd);
              rd_data_store_nvm_write(nd);
            }
          }
        }
      }
      //Frames targeted virtual nodes should not go to the unsolicited destination.
    DBG_PRINTF("Frame sent to virtual node is Handled\n");
    return;
  }

  zwave_connection_t c;
  memset(&c, 0, sizeof(c));

  /*
    Note: the lipaddr address is set back to ripaddr in ClassicZIPNode_SendUnsolicited()
    for sending unsol packets. Because ZIP Gateway is forwarding the packet */

  ipOfNode(&c.ripaddr,p->snode);
  ipOfNode(&c.lipaddr,p->dnode);
  /* Check if command should be handled by the Z/IP router itself */
  c.scheme = p->scheme;
  c.rendpoint = p->sendpoint;
  c.lendpoint = p->dendpoint;
  c.rx_flags = p->rx_flags;

  if (c.lendpoint == 0) {
      // Block access to Network Management CCs from RF
      if (pCmd->ZW_Common.cmdClass == COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC ||
          pCmd->ZW_Common.cmdClass == COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION ||
          pCmd->ZW_Common.cmdClass == COMMAND_CLASS_NETWORK_MANAGEMENT_INSTALLATION_MAINTENANCE ||
          pCmd->ZW_Common.cmdClass == COMMAND_CLASS_NETWORK_MANAGEMENT_PRIMARY ||
          pCmd->ZW_Common.cmdClass == COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY ) {
        WRN_PRINTF("Blocking RF access to Network Management CC\n");
        return;
      }

      ApplicationIpCommandHandler(&c, (BYTE*) pCmd,  cmdLength);
  } else {
      WRN_PRINTF("multi channel encap command to non-zero endpoint of gateway? Dropping.\n");
  }
}

/**
 * Input for RAW Z-Wave frames
 */
void
ApplicationCommandHandlerSerial(BYTE rxStatus, nodeid_t destNode, nodeid_t sourceNode,
    ZW_APPLICATION_TX_BUFFER *pCmd, BYTE cmdLength)CC_REENTRANT_ARG
{

  ts_param_t p;
  p.dendpoint = 0;
  p.sendpoint = 0;

  p.rx_flags = rxStatus;
  p.tx_flags = ((rxStatus & RECEIVE_STATUS_LOW_POWER) ?
      TRANSMIT_OPTION_LOW_POWER : 0) | TRANSMIT_OPTION_ACK
      | TRANSMIT_OPTION_EXPLORE | TRANSMIT_OPTION_AUTO_ROUTE;

  if(nodemask_nodeid_is_invalid(sourceNode)) {
      WRN_PRINTF("source node id: %d is out of range. Dropping!\n", sourceNode);
      return;
  }

  if ((destNode != 0xff) && (p.rx_flags & RECEIVE_STATUS_TYPE_EXPLORE )) {
    DBG_PRINTF("Destinaton node is NOT 0xff and RECEIVE_STATUS_TYPE_EXPLORE is "
               "set. Marking the frame as neither MCAST nor BCAST. \n");
    p.rx_flags &= ~(RECEIVE_STATUS_TYPE_MULTI | RECEIVE_STATUS_TYPE_BROAD);
  }

  p.scheme = NO_SCHEME;
  p.dnode = destNode ? destNode : MyNodeID;
  p.snode = sourceNode;

  ApplicationCommandHandlerZIP(&p,pCmd, cmdLength);
  rd_node_is_alive(p.snode);
}


/* ******* Contiki ******** */

/*TODO consider a double buffer scheme */
#define UIP_ICMP_BUF ((struct uip_icmp_hdr *)&uip_buf[UIP_LLIPH_LEN + uip_ext_len])
#define UIP_IP_BUF ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])

/*
 * This is the output function of uip.
 * \parm addr is the linklayer(L2) destination address.
 * We use this address to check which physical network the package should
 * go out on.
 * If adder is 0, then package should be multicasted to all interfaces.
 */
static u8_t
zwave_send(uip_lladdr_t *addr)
{
  nodeid_t nodeid = 0;
  /* Is Unicast? */
  if (addr)
  {
    /*    DBG_PRINTF("  %02x:%02x:%02x:%02x:%02x:%02x length %i",
     pan_lladdr.addr[0],
     pan_lladdr.addr[1],
     pan_lladdr.addr[2],
     pan_lladdr.addr[3],
     pan_lladdr.addr[4],
     pan_lladdr.addr[5],uip_len);*/

    if(memcmp(addr->addr, tun_lladdr.addr, 6) == 0)  /* Is L2 address in portal */
    {
#ifndef ANDROID
      send_ipv6_to_tcp_ssl_client();
#endif
    }
    else if (memcmp(addr->addr, pan_lladdr.addr, 4) == 0) /* Is L2 address on PAN? */
    {
      nodeid = nodeOfIP(&UIP_IP_BUF->destipaddr);
      node_input_queued(nodeid, FALSE);
      uip_len =0;
    }
    else
    {
      if (landev_send)
      {
         //DBG_PRINTF("Send to lan\n");
         landev_send(addr);
      }
    }
  }
  else
  {
    /*Do not send RPL messages to LAN. It is not very pretty
     * to do package IP package inspection in a L2 function,
     * but I think this is the only way right now. */
    if (!(UIP_IP_BUF ->proto == UIP_PROTO_ICMP6
        && UIP_ICMP_BUF ->type == ICMP6_RPL))
    {
      if (landev_send)
      /* Multicast, send to all interfaces */
      landev_send(addr);
    }
  }
  return 0;
}

/* ******* Z-Wave nvm stuff ******** */

/* Generate Ethernet MAC address*/
static void
permute_l2_addr(uip_lladdr_t* a)
{
  uint32_t seed;

  seed = (MyNodeID << 24) | (MyNodeID << 16) | (MyNodeID << 8)
      | (MyNodeID << 0);
  seed ^= homeID;
  seed ^= (a->addr[3] << 16) | (a->addr[4] << 8) | (a->addr[5] << 0);
  srand(seed);

  a->addr[0] = 0x00; //IEEE OUI Assignment for Zensys
  a->addr[1] = 0x1E; //
  a->addr[2] = 0x32; //
  a->addr[3] = 0x10 | (random_rand() & 0x0F);
  a->addr[4] = random_rand() & 0xFF;
  a->addr[5] = random_rand() & 0xFF;
}


/**
 * Write a new pan  NWM to Z-Wave module
 */
static void
new_pan_ula() REENTRANT
{
  uip_ip6addr_t pan_prefix;
  LOG_PRINTF("Entering new network\n");
  create_ula_prefix(&pan_prefix,2);
  zw_appl_nvm_write(offsetof(zip_nvm_config_t,ula_pan_prefix), &pan_prefix,
      sizeof(pan_prefix));
}

/**
 * Check if a uip link layer address is all zeroes.
 * This function assumes that UIP_LL_LENGTH is 6.
 * \return TRUE if all zeros. FALSE otherwise.
 */
unsigned is_linklayer_addr_zero(uip_lladdr_t const *a)
{
  if(    ((a->addr[0]) == 0)
      && ((a->addr[1]) == 0)
      && ((a->addr[2]) == 0)
      && ((a->addr[3]) == 0)
      && ((a->addr[4]) == 0)
      && ((a->addr[5]) == 0)
      )
  {
    return TRUE;
  }
  return FALSE;
}


/**
 * Read appropriate settings from Z-Wave nvm. This is stuff like generated ULA addresses
 */
static int 
ZIP_eeprom_init() CC_REENTRANT_ARG
{
  uip_ip6addr_t ula;
  u32_t magic=0;
  u16_t version = NVM_CONFIG_VERSION;
  Gw_Config_St_t   GwConfig_St;

  if(!nvm_config_get(magic,&magic)) {
      ERR_PRINTF("Error reading the NVM MAGIC ID because of Serial API is not"
                 "responding. Stopping Z/IP Gateway.\n");
      return 0;
  } 

  DBG_PRINTF("Magic check %x  == %x\n",magic, ZIPMAGIC );
  // TODO: should clear_eeprom be cleared after initial boot of the gateway? */
  if ((magic != ZIPMAGIC) || cfg.clear_eeprom)
  {
	  //In case of ZIPR, we will get mac address from I2C EEPROM/Asix registers
#ifndef __ASIX_C51__
    WRN_PRINTF("Writing new MAC addr\n");
    memset(&uip_lladdr,0,sizeof(uip_lladdr));
    permute_l2_addr(&uip_lladdr);
#endif
    magic = 0;
    nvm_config_set(magic,&magic);
    nvm_config_set(config_version,&version);

    nvm_config_set(mac_addr,&uip_lladdr);

    /*Create a ULA LAN address*/
    create_ula_prefix(&ula,1);
    nvm_config_set(ula_lan_addr,&ula);

    ApplicationDefaultSet();

    /* Create a new PAN ULA address */
    new_pan_ula();
    sec2_create_new_static_ecdh_key();
    sec2_create_new_network_keys();
    magic = ZIPMAGIC;
    nvm_config_set(magic,&magic);
  }
  /*Migrate nvm for old version*/
  nvm_config_get(config_version,&version);
  DBG_PRINTF("NVM version is %i\n",version);
  if(version <= 1) {
    uint8_t assigned_keys = KEY_CLASS_S0;
    nvm_config_set(assigned_keys,&assigned_keys);
    version = NVM_CONFIG_VERSION;
    sec2_create_new_static_ecdh_key();
    sec2_create_new_network_keys();
    nvm_config_set(config_version,&version);
    /* This is to adapt new network scheme */
    magic = ZIPMAGIC;
    nvm_config_set(magic,&magic);
  }

#if 0
  //check for the default GW configuration;
  eeprom_read(EXT_GW_SETTINGS_START_ADDR, (u8_t *)&GwConfig_St, sizeof(GwConfig_St));
   //Flash Erased or new flash?
  if(GwConfig_St.magic != GW_CONFIG_MAGIC)
  {
    /*Initialize the GW config TODO this should be a part of nvm_config */
    GwConfig_St.magic =  GW_CONFIG_MAGIC;
    GwConfig_St.mode = ZIP_GW_MODE_STDALONE;
    GwConfig_St.showlock = 0x0;
    eeprom_write(EXT_GW_SETTINGS_START_ADDR,(u8_t*) &GwConfig_St, sizeof(GwConfig_St));
  }
#endif

  /* TODO Store the L2 address in EEPROM and read it from the EEPROM,
   * Use IPv6 DAD to ensure there are no devices on the network which has the same L2 address
   * */
  nvm_config_get(mac_addr,&uip_lladdr);

  /* This is for the migration scenario: If we come up with a valid EEPROM MAGIC,
   *  but an all-zeros MAC address, we generate a new mac address. */
  if (is_linklayer_addr_zero(&uip_lladdr))
  {
    memset(&uip_lladdr,0,sizeof(uip_lladdr));
    permute_l2_addr(&uip_lladdr);
    nvm_config_set(mac_addr,&uip_lladdr);
  }
  LOG_PRINTF("L2 HW addr ");
  PRINTLLADDR(&uip_lladdr);
  LOG_PRINTF("\n");

  /*If lan and pan prefixes are not given in config file, use ULA addresses*/
  //uip_debug_ipaddr_print(&uip_all_zeroes_addr);

  if (uip_is_addr_unspecified(&cfg.cfg_lan_addr))
  {
    nvm_config_get(ula_lan_addr,&cfg.lan_addr);

    if (uip_is_addr_unspecified(&cfg.lan_addr))
    {
      /*Create a ULA LAN address*/
      create_ula_prefix(&cfg.lan_addr,1);
      nvm_config_set(ula_lan_addr,&cfg.lan_addr);
    }

    /*Create autoconf address */
    uip_ds6_set_addr_iid(&cfg.lan_addr, &uip_lladdr);
    cfg.lan_prefix_length = 64;
    LOG_PRINTF("Using ULA address for LAN \n");
  }
  else
  {
    cfg.lan_prefix_length = cfg.cfg_lan_prefix_length;
    uip_ipaddr_copy(&cfg.lan_addr,&cfg.cfg_lan_addr);
  }

  if (uip_is_addr_unspecified(&cfg.cfg_pan_prefix))
  {
    nvm_config_get(ula_pan_prefix,&cfg.pan_prefix);
    if (uip_is_addr_unspecified(&cfg.pan_prefix))
    {
      /*Create a ULA LAN address*/
      create_ula_prefix(&cfg.pan_prefix,2);
      nvm_config_set(ula_pan_prefix,&cfg.pan_prefix);
    }
    LOG_PRINTF("Using ULA address for HAN\n");
  }
  else
  {
    uip_ipaddr_copy(&cfg.pan_prefix,&cfg.cfg_pan_prefix);
  }
  return 1;
}

/* ******* IP/LAN ******** */

/**
 * Initialize the IPv6 stack
 * This will at some point trigger a TCP_EVENT,TCP_DONE
 * Which means that the DAD has completed and the IPv6 stack is ready to
 * send an receive messages
 */
static void
ipv6_init()
{
  tcpip_set_outputfunc(zwave_send);

  /* Use homeid to set pan link local address, this address is only used in this file
   * to keep track of whare packages are comming from.  */
  pan_lladdr.addr[0] = (homeID & 0xFF000000) >> 24;
  pan_lladdr.addr[1] = (homeID & 0xFF0000) >> 16;
  pan_lladdr.addr[2] = (homeID & 0xFF00) >> 8;
  pan_lladdr.addr[3] = (homeID & 0xFF);
  pan_lladdr.addr[4] = 0x0;
  pan_lladdr.addr[5] = MyNodeID;

  DBG_PRINTF(
      "ZIP_Router_Reset: pan_lladdr: %02x:%02x:%02x:%02x:%02x:%02x  Home ID = 0x%x \r\n",
      (unsigned int ) pan_lladdr.addr[0], (unsigned int )pan_lladdr.addr[1],
      (unsigned int )pan_lladdr.addr[2], (unsigned int )pan_lladdr.addr[3],
      (unsigned int ) pan_lladdr.addr[4], (unsigned int ) pan_lladdr.addr[5],
      UIP_HTONL(homeID));

  process_exit(&tcpip_process);
  process_start(&tcpip_process,0);

  refresh_ipv6_addresses();
  keystore_public_key_debug_print();
  /* Init RPL as dag root using our PAN IP as DODAGID */
#ifdef UIP_CONF_IPV6_RPL
  {
    rpl_dag_t * dag;

    rpl_init();
    dag = rpl_set_root(&ipaddr);
    if(dag != NULL)
    {
      rpl_set_prefix(dag, &(cfg.pan_prefix), 64);
      DBG_PRINTF("created a new RPL dag\n");
    }
  }
#endif

  /*Init UDP listners */
  process_exit(&udp_server_process);
  process_start(&udp_server_process,0);

#ifndef DISABLE_DTLS
  process_exit(&dtls_server_process);
  process_start(&dtls_server_process,0);
#endif
#ifdef SUPPORTS_MDNS
  /* Make sure that mDNS is started, but do not stop mDNS if it is
   * already running as that is slow, complicated, and not needed.
   * process_start() is nop when process is running. */
  process_start(&mDNS_server_process,0);
#endif

#ifdef SUPPORTS_COAP
  process_exit(&coap_server_process);
  process_start(&coap_server_process,0);
#endif

#ifdef DEBUG_WEBSERVER
  process_exit(&webserver_nogui_process);
  process_start(&webserver_nogui_process,0);
#endif

}

/*---------------------------------------------------------------------------*/
/*--- ZIP Router --------*/
static BOOL
ZIP_Router_Reset() CC_REENTRANT_ARG
{
  BYTE rnd[8];
  nm_state_t nms = NetworkManagement_getState();

  LOG_PRINTF("Resetting ZIP Gateway\n");

  if (nms == NM_WAIT_FOR_MDNS || nms == NM_SET_DEFAULT) {
     /* There is already a network management operation that will
      * trigger a reset, so just hang in there. */
     LOG_PRINTF("Await pending reset in Network Management.\n");
     return TRUE;
  }

  /*Fire up the serial API process */
  process_exit(&serial_api_process);
  process_start(&serial_api_process,cfg.serial_port);

  if (!serial_ok)
  {
    return FALSE;
  }

  MemoryGetID((BYTE*) &homeID, &MyNodeID);
  if(!dev_urandom(sizeof(rnd), rnd)) {
      ERR_PRINTF("Failed to seed random number generator. Security is compromised\n");
  }
  /*Seed the contiki random number generator*/
  random_init(rnd[0] <<24 | rnd[1] <<16 | rnd[2] <<8 | rnd[3]);

  DBG_PRINTF("...... Firmware Update Meta Data command class version: %i ......\n",
             ZW_comamnd_handler_version_get(TRUE, COMMAND_CLASS_FIRMWARE_UPDATE_MD ) );

#ifdef TEST_MULTICAST_TX
  /* Flush the multicast groups. */
  mcast_group_init();
#endif
  if(!ZIP_eeprom_init()) {
      return FALSE;
  } 
  ApplicationInitNIF();

  ipv6_init();

  /* The IPv4 tunnel is only initalized once. This means that it is
   * not restarted when we are entering or leaving networks. */
#ifdef IP_4_6_NAT
  if(!cfg.ipv4disable && !ipv4_init_once)
  {
    ipv4_interface_init();
    DBG_PRINTF("Starting zip tcp client\n");
#ifndef ANDROID
    process_exit(&zip_tcp_client_process);
    process_start(&zip_tcp_client_process, cfg.portal_url);
#endif
    ipv4_init_once = 1;
    process_exit(&resolv_process);
    process_start(&resolv_process, NULL);

#ifdef __ASIX_C51__
  if(process_is_running(&zip_tcp_client_process))
  {
	  process_exit(&ntpclient_process);
	  process_start(&ntpclient_process, NULL);
  }
#endif

  }
  else if(ipv4_init_once)
  {
    uip_ds6_prefix_add(&(cfg.tun_prefix), cfg.tun_prefix_length, 1, 0, 0, 0);
  }
#endif

//  TransportService_SetCommandHandler(ApplicationCommandHandlerZIP);
  node_queue_init();

//  ClassicZIPNode_init();
  ZW_TransportService_Init(ApplicationCommandHandlerZIP);
  ZW_SendRequest_init();

  /* init: at this point ZW_Send_* functions can be used */
  if (!data_store_init())
  {
    return FALSE;
  }
  mb_init();

  process_exit(&dhcp_client_process);
  process_start(&dhcp_client_process, 0);

  provisioning_list_init(linux_conf_provisioning_list_storage_file,
                         linux_conf_provisioning_cfg_file);

  /*Network management is initialized async*/
  network_management_init_done = 0;

  uint8_t rfregion;
  uint8_t channel_idx, max_idx;
  if(ZW_GECKO_CHIP_TYPE(chip_desc.my_chip_type)) {
    rfregion = ZW_RFRegionGet();
  } else {
    rfregion = cfg.rfregion;// ZW_RFRegionGet() is not available in 500
  }

  rfregion = RF_REGION_CHECK(rfregion);
  if(rfregion != 0xFE) { 
    if ((rfregion == JP) || (rfregion == KR)) {
      max_idx = 3;
    } else {
      max_idx = 2;
    }
    for (channel_idx = 0; channel_idx < max_idx; channel_idx++) {
      if(!ZW_SetListenBeforeTalkThreshold(channel_idx, cfg.zw_lbt)) {
        ERR_PRINTF("ERROR: Failed Setting ListenBeforeTalk Threshold to %d on "
             "channel: %d\n", cfg.zw_lbt, channel_idx);
        goto err;
      }
      DBG_PRINTF("Setting ListenBeforeTalk Threshold to %d channel: %d\n",
                 cfg.zw_lbt, channel_idx);
    }
  }
err:
  return TRUE;
}



/***************************************************************************/
/* The backup interface is used when a destination is not found in ND cache */

static void
backup_init(void)
{
}

static void
backup_output(void)
{
  //ASSERT(0);
  /*FIXME: What is the right thing to do? */
  /* Send at Ethernet multicast */
  ERR_PRINTF("Using backup interface!\n");
  landev_send(0);
}

const struct uip_fallback_interface backup_interface =
{ backup_init, backup_output };

/*TODO this is debug stuff */
extern uip_ds6_nbr_t uip_ds6_nbr_cache[];
extern uip_ds6_defrt_t uip_ds6_defrt_list[];
extern uip_ds6_route_t uip_ds6_routing_table[];
extern void start_periodic_tcp_timer(void);
extern struct etimer periodic;

void rd_set_failing(rd_node_database_entry_t* n, uint8_t failing);
/* End of debug stuff */

//#ifndef ROUTER_REVISION
//#define ROUTER_REVISION "Unknown"
//#endif

/*---------------------------------------------------------------------------*/
/**
 * \fn zip_process
 *
 * \ingroup ZIP_Router
 *
 * Main Z/IP router process
 *
 * This process starts all other required processes of the gateway.

 ZIP Router Init Procedure
 --------------------------
 Outline of the init procedure used on startup and setdefault of zip router family.

 Full inits are triggered by either receiving a Network Management #DEFAULT_SET command
 or by starting the ZIP Router. In the latter case, ZIP_Router_Reset() is called directly.

 Full inits will also be triggered from \ref NW_CMD_handler by GW learn mode.

When triggered from NMS, an init must be preceeded by a teardown,
invalidation, or rediscovery of some components.


Initialization Steps
--------------------

These steps also apply when re-initializating after a teardown.

- When #zip_process receives #ZIP_EVENT_RESET
 - #ZIP_Router_Reset();
  - stop/start serial_api_process
  - read in #homeID/#MyNodeID from serial
  - init random
  - init eeprom
  - init the zipgateway's cache of the serial device's capabilities and the zipgateway NIF
  - init ipv6
   - stop/start tcpip_process  ---> will emit \ref tcpip_event when done
   - stop/start udp_server_process, dtls_server_process
   - start \ref ZIP_MDNS (if it is not already running)
  - init ipv4 (if it is not already initialized)
  - init node queue
  - \ref ZW_TransportService_Init (ApplicationCommandHandlerZIP)
  - initialize \ref Send_Request (at this point ZW_Send_* functions can be used)
  - init data store file - if #homeID or #MyNodeID have changed, update file and invalidate bridge.
  - init \ref mailbox
  - stop/start \ref ZIP_DHCP ---> will emit #ZIP_EVENT_ALL_IPV4_ASSIGNED
  - init provisioning list
  - set NM init "not done"

- When #zip_process receives tcpip_event
 - #ClassicZIPNode_init();
 - #bridge_init() ---> will emit #ZIP_EVENT_BRIDGE_INITIALIZED

- When #zip_process receives #ZIP_EVENT_BRIDGE_INITIALIZED
 - #ApplicationInitProtocols()
  - ZW_SendDataAppl_init();
  - unlock RD: #rd_init (FALSE); ---> will emit #ZIP_EVENT_ALL_NODES_PROBED
   - import from eeprom file, reset #nat_table, update rd/nat_table if needed, send DHCP DISCOVER for gw, resume rd probe.
  - #rd_probe_new_nodes(); -- This looks redundant
 - NetworkManagement_Init();
  - cancel mns timer, set NM init "done", clear waiting_for_middleware_probe flag
 - #send_nodelist() if this was requested before reset and NMS is IDLE
 - #NetworkManagement_NetworkUpdateStatusUpdate() ---> may emit #ZIP_EVENT_NETWORK_MANAGEMENT_DONE
  - if all flags are in:
   - send the prepared NMS reply buffer ---> will emit #ZIP_EVENT_NETWORK_MANAGEMENT_DONE
   - reset NMS to #NM_IDLE

- When #zip_process receives #ZIP_EVENT_ALL_NODES_PROBED
 - NetworkManagement_all_nodes_probed()
 - NetworkManagement_NetworkUpdateStatusUpdate() ---> may emit #ZIP_EVENT_NETWORK_MANAGEMENT_DONE
  - if all flags are in:
   - send the prepared NMS reply buffer ---> will emit #ZIP_EVENT_NETWORK_MANAGEMENT_DONE
   - set NMS state to #NM_IDLE
 - if DHCP is done and NMS is IDLE
  - #send_nodelist() if this was requested before reset
 - post #ZIP_EVENT_QUEUE_UPDATED

- When #zip_process receives #ZIP_EVENT_ALL_IPV4_ASSIGNED
 - if probing is done  and NMS is IDLE
  - #send_nodelist() if this was requested before reset
 - NetworkManagement_NetworkUpdateStatusUpdate(); ---> may emit #ZIP_EVENT_NETWORK_MANAGEMENT_DONE
  - if all flags are in:
   - send the prepared NMS reply buffer ---> will emit #ZIP_EVENT_NETWORK_MANAGEMENT_DONE
   - set NMS state to #NM_IDLE

- When #zip_process receives #ZIP_EVENT_NETWORK_MANAGEMENT_DONE
 - #send_nodelist() if this was requested before reset and NMS is IDLE or if #zip_process just starts
 - process the node queues

- When #zip_process receives #ZIP_EVENT_QUEUE_UPDATED
 - process the node queues


Tear down in Learn Mode Scenarios
---------------------------------

\<NetworkManagement receives #LEARN_MODE_SET CMD>
- #ZW_SetLearnMode(.., \<LearnModeStatus callback>)
- #LearnModeStatus (#LEARN_MODE_STARTED)
 - lock RD, invalidate #MyNodeID, start sec2 learn mode with #SecureInclusionDone() callback
- LearnModeStatus(#LEARN_MODE_DONE)
 - if exclusion:
  - exit DHCP and send DHCP release on nodeid 1, if this was not GW
  - Set #MyNodeID=1 and update nat_table entry of GW to nodeid 1
  - prepare nms reply
  - call rd_exit() to tear down mDNS and RD ---> will emit PROCESS_EVENT_EXITED(mDNS_server_process)
  - set NMS in #NM_SET_DEFAULT
  - start DHCP
 - if inclusion:
  - set NMS in state #NM_WAIT_FOR_SECURE_LEARN
  - use the new nodeid of the gateway to delete any existing nat_table entry for that nodeid and update the nat_table entry of the GW - the ipv4 address of the gateway is retained
  - exit DHCP process
  - delete the RD entry of the new gateway nodeid
  - update #MyNodeID/#homeID over serial api, refresh ipv6 addresses and sec2 with this
  - #ipv46nat_del_all_nodes() - delete nodes in the old network from nat_table (and send DHCP release)
  - if interview_completed requested
    - start DHCP and add including node to nat_table
  - #new_pan_ula()
  - wait for security completion on PAN side (#SecureInclusionDone() callback)
 - if ctrl replication/ctrl shift
  - restore #MyNodeID/#homeID
  - prepare nms reply
  - set NMS in state #NM_WAIT_FOR_MDNS
  - call rd_exit() to tear down mDNS and RD ---> will emit PROCESS_EVENT_EXITED(mDNS_server_process)

For inclusion of the gateway, this happens asynchronously:

- #SecureInclusionDone(#NM_EV_SECURITY_DONE) in #NM_WAIT_FOR_SECURE_LEARN
 - prepare nms reply
 - set NMS in #NM_WAIT_FOR_MDNS
 - call rd_exit() to tear down mDNS and RD ---> will emit PROCESS_EVENT_EXITED(mDNS_server_process)

Tear down then continues:

- When #zip_process receives PROCESS_EVENT_EXITED(mDNS_server_process)
 - rd_destroy()
 - call #NetworkManagement_mdns_exited()

At this point, Network Management can be in #NM_SET_DEFAULT state (in
case of exclusion) or #NM_WAIT_FOR_MDNS state (any other outcome).

 - NetworkManagement_mdns_exited() in #NM_WAIT_FOR_MDNS
  - if incl or excl, do bridge_reset()
  - if interview_completed requested
   - send nms reply
   - setup nms interview_completed reply
   - setup timer and wait for interview by SIS
   - when interview is completed,  --->  emit #ZIP_EVENT_RESET
    - set NMS state to #NM_WAIT_FOR_OUR_PROBE
    - (when our (post-reset) interview is completed (receive #NM_EV_ALL_PROBED)
     - unlock RD
     - start nms timer)
  - else  --->  emit #ZIP_EVENT_RESET
   - unlock RD
   - start reply timer

In the #NM_SET_DEFAULT scenarios, tear down continues as follows

 - #NetworkManagement_mdns_exited() in #NM_SET_DEFAULT ---> will emit #ZIP_EVENT_RESET
  - ApplicationDefaultSet()
   - #rd_exit() -- again, but here it is almost certainly a NOP
   - #ipv46nat_del_all_nodes() - delete nodes in the old network from nat_table (and send DHCP release)
   - #rd_data_store_invalidate(), read #homeID/#MyNodeID
   - ZW_SetSUCNodeID(#MyNodeID,...)
   - #security_set_default();
   - #sec2_create_new_network_keys();
  - #bridge_reset() -- invalidate bridge
  - #new_pan_ula()
  - process_post(&zip_process, #ZIP_EVENT_RESET, 0);
  - change NMS state to #NM_WAITING_FOR_PROBE
  - start nms timer
  - re-start probing:  #rd_probe_lock (FALSE) - this should pause, waiting for bridge

From here on, the gateway continues with the steps from
initialization.

Tear down in Set Default scenario
---------------------------------

\<NetworkManagement receives #DEFAULT_SET CMD>
- \ref ZW_SetDefault(\<SetDefaultStatus-callback>)
- set NMS state to #NM_SET_DEFAULT
- SetDefaultStatus()
 - call rd_exit() to tear down mDNS and RD ---> will emit PROCESS_EVENT_EXITED(mDNS_server_process)

From here on, teardown continues as in the learn mode exclusion
scenario from where #zip_process receives
PROCESS_EVENT_EXITED(mDNS_server_process).


Tear down in Set-as-SIS Scenario
--------------------------------

\<#ApplicationControllerUpdate() receives #UPDATE_STATE_SUC_ID>
- set flag to send new nodelist to unsolicited after reset
- if the new id is #MyNodeID, post #ZIP_EVENT_RESET to #zip_process

From here on, the gateway continues with the steps from
initialization.


*/


/**
 * \ref zip_process is defined as a Contiki thread.
 */
PROCESS_THREAD(zip_process, ev, data)
{
  int i;
  PROCESS_BEGIN()
    ;
    /*Setup uip */

#ifdef __ASIX_C51__
  LOG_PRINTF("Starting ZIP Gateway version %bu build %bu\n", zipr_fw_version_st.major_version
         ,zipr_fw_version_st.minor_version);
#else
  LOG_PRINTF("Starting " PACKAGE_STRING " build " PACKAGE_SVN_REVISION "\n");
#endif

#ifdef NO_ZW_NVM
  zw_appl_nvm_init();
#endif

#ifdef __ASIX_C51__
    GwConfigCheck();
#endif

    while(1)
    {
      //DBG_PRINTF("Event ***************** %x ********************\n",ev);
#ifndef __ASIX_C51__
      if(ev == serial_line_event_message)
      {
        /* Keyboard input */
        char kbd_char = ((char*) data)[0];

#ifdef TEST_MULTICAST_TX
        static ts_param_t ts_param;
        ts_set_std(&ts_param, 0);
        ts_param.snode = MyNodeID;
        ts_param.scheme = SECURITY_SCHEME_2_UNAUTHENTICATED;

        nodemask_add_node(6, ts_param.node_list);
        nodemask_add_node(7, ts_param.node_list);

        // static uint8_t mc_data_on[]   = {COMMAND_CLASS_DOOR_LOCK, DOOR_LOCK_OPERATION_SET, DOOR_LOCK_OPERATION_SET_DOOR_SECURED};
        // static uint8_t mc_data_off[]  = {COMMAND_CLASS_DOOR_LOCK, DOOR_LOCK_OPERATION_SET, DOOR_LOCK_OPERATION_SET_DOOR_UNSECURED};
        static uint8_t mc_data_on[]   = {COMMAND_CLASS_SWITCH_BINARY, SWITCH_BINARY_SET, 0xff};
        static uint8_t mc_data_off[]  = {COMMAND_CLASS_SWITCH_BINARY, SWITCH_BINARY_SET, 0x00};

        LOG_PRINTF("\n--[ %c ]-----------------------------------------------------\n", kbd_char);
#endif // #ifdef TEST_MULTICAST_TX

        switch (kbd_char)
        {
          case 'l':
              ERR_PRINTF("Sending RA\n");
              uip_nd6_ra_output(NULL);
          break;
#ifdef TEST_MULTICAST_TX
          // m,M is _with_ single cast follow-ups
          case 'm':
          {
            sec2_send_multicast(&ts_param, mc_data_on, sizeof(mc_data_on), TRUE, 0, 0);
          }
          break;
          case 'M':
          {
            sec2_send_multicast(&ts_param, mc_data_off, sizeof(mc_data_off), TRUE, 0, 0);
          }
          break;
          // n,N is _without_ single cast follow-ups
          case 'n':
          {
            sec2_send_multicast(&ts_param, mc_data_on, sizeof(mc_data_on), FALSE, 0, 0);
          }
          break;
          case 'N':
          {
            sec2_send_multicast(&ts_param, mc_data_off, sizeof(mc_data_off), FALSE, 0, 0);
          }
          break;
#endif // #ifdef TEST_MULTICAST_TX

          case 'x':
            LOG_PRINTF("Z/IP router is exiting\n");

            rd_exit();
            process_post(PROCESS_BROADCAST, PROCESS_EVENT_EXIT, 0);
            break;
          case 'd':
            rd_full_network_discovery();
            break;
          case 'f':
          {
            rd_node_database_entry_t* n = rd_get_node_dbe(atoi( &((char*) data)[2] ) );
            if (!n)
            {
              WRN_PRINTF("Usage: f <node id>\n");
              break;
            }
            rd_set_failing(n, TRUE);

            mb_failing_notify(n->nodeid);
            rd_free_node_dbe(n);
          }
          break;
#ifdef DEBUG
          case 'i':
             if (zgw_idle()) {
                DBG_PRINTF("Idle\n");
             } else {
                DBG_PRINTF("Not idle\n");
                //process_post(&zip_process, ZIP_EVENT_BACKUP_REQUEST, NULL);
             }
            break;
#endif
          case 'r':
          process_post(&zip_process,ZIP_EVENT_RESET,0);
            break;
          case 'b':
            print_bridge_status();
            break;
          case 'w':
            provisioning_list_print();
            break;
          case 'p':
          {
            uint8_t test_prekit_homeid[] = {0xDE, 0xAD, 0xBE, 0xEF};
            NetworkManagement_smart_start_inclusion(0xC0, test_prekit_homeid, false);
          }

          case 's':
          {
            NetworkManagement_smart_start_init_if_pending();
          }
          break;
          case 'e':
          {
            uint8_t rfregion = ZW_RFRegionGet();
            LOG_PRINTF("Current RF region: 0x%02x\n", rfregion);
          }
          break;

          case 't':
          {
            uint8_t test_rfregion = 0x02;
            LOG_PRINTF("Setting RF region to 0x%02x\n", test_rfregion);
            ZW_RFRegionSet(test_rfregion);
            LOG_PRINTF("Current RF region: 0x%02x\n", ZW_RFRegionGet());
          }
          break;

          case 'g':
          {
            TX_POWER_LEVEL txpowerlevel = ZW_TXPowerLevelGet();
            LOG_PRINTF("Current normal TX powerlevel: %d, current measured 0dBm powerlevel: %d\n", txpowerlevel.normal, txpowerlevel.measured0dBm);
          }
          break;

          default:

            LOG_PRINTF("IP addresses:\n");
            for(i = 0; i < UIP_DS6_ADDR_NB; i++)
            {
              if(uip_ds6_if.addr_list[i].isused)
              {
                PRINT6ADDR(&uip_ds6_if.addr_list[i].ipaddr);
              }
            }

            LOG_PRINTF("IPv4 address %d.%d.%d.%d:\n",uip_hostaddr.u8[0],uip_hostaddr.u8[1],uip_hostaddr.u8[2],uip_hostaddr.u8[3]);


            LOG_PRINTF("Neighbor cache:\n");
            for(i = 0; i < UIP_DS6_NBR_NB; i++)
            {
              if(uip_ds6_nbr_cache[i].isused)
              {
                PRINT6ADDR(&uip_ds6_nbr_cache[i].ipaddr);
                LOG_PRINTF("  ----  %02x:%02x:%02x:%02x:%02x:%02x",
                    uip_ds6_nbr_cache[i].lladdr.addr[0],
                    uip_ds6_nbr_cache[i].lladdr.addr[1],
                    uip_ds6_nbr_cache[i].lladdr.addr[1],
                    uip_ds6_nbr_cache[i].lladdr.addr[2],
                    uip_ds6_nbr_cache[i].lladdr.addr[3],
                    uip_ds6_nbr_cache[i].lladdr.addr[5]);
                LOG_PRINTF("\n");
              }
            }
            LOG_PRINTF("Routes:\n");
            for(i = 0; i < UIP_DS6_ROUTE_NB; i++)
            {
              if(uip_ds6_routing_table[i].isused)
              {
                PRINT6ADDR(&uip_ds6_routing_table[i].ipaddr);
                LOG_PRINTF(" via ");
                PRINT6ADDR(&uip_ds6_routing_table[i].nexthop);
                LOG_PRINTF("\n");
              }
            }

            LOG_PRINTF("Default Routes:\n");
            for(i = 0; i < UIP_DS6_DEFRT_NB; i++)
            {
              if(uip_ds6_defrt_list[i].isused)
              {
                PRINT6ADDR(&uip_ds6_defrt_list[i].ipaddr);
                LOG_PRINTF("\n");
              }
            }
          break;
        }
#endif // #ifndef __ASIX_C51__
      }
      else if (ev == PROCESS_EVENT_INIT)
      {
        if (!ZIP_Router_Reset())
        {
          ERR_PRINTF("Fatal error\n");
          process_post(&zip_process, PROCESS_EVENT_EXIT, 0);
        }
      }
      else if (ev == ZIP_EVENT_NODE_IPV4_ASSIGNED)
      {
        nodeid_t node = ((uintptr_t)data) & 0xFFFF;
        LOG_PRINTF("New IPv4 assigned for node %d\n", node);
        if (node == MyNodeID)
        {
          /*Now the that the Z/IP gateway has an IPv4 address we may route IPv4 to IPv6 mapped addresses to the gateway itself.*/

          /* With this new route it might be possible to for the NM module to deliver the reply package after a Set default
           * or learn mode. */
          NetworkManagement_Init();
        }
        else
        {
          /* Notify network management module that we have a new IPv4 address. */
          NetworkManagement_IPv4_assigned(node);
        }
      }
      else if (ev == ZIP_EVENT_RESET)
      {
        if (data == (void*) 0)
        {
          LOG_PRINTF("Resetting....\n");
          zgw_component_start(ZGW_ROUTER_RESET);
          if (!ZIP_Router_Reset())
          {
            ERR_PRINTF("Fatal error\n");
            process_post(&zip_process, PROCESS_EVENT_EXIT, 0);
          }
        }
      }
      else if (ev == ZIP_EVENT_TUNNEL_READY)
      {
        printf("TUNNEL READY!\n");

        /*Refresh used lan and pan addresses*/
        ZIP_eeprom_init();

        /*Reinit the IPv6 addresses*/
        refresh_ipv6_addresses();
        keystore_public_key_debug_print();
        system_net_hook(1);

        /*Reinsert the IPv6 mapped route.*/
#if 0
        uip_ip6addr_t addr_map;
        addr_map.u16[0] = 0;
        addr_map.u16[1] = 0;
        addr_map.u16[2] = 0;
        addr_map.u16[3] = 0;
        addr_map.u16[4] = 0;
        addr_map.u16[5] = 0xFFFF;
        addr_map.u16[6] = 0;
        addr_map.u16[7] = 0;
        uip_ds6_route_add(&addr_map, 64,&cfg.gw_addr,1);
#endif
        /*The Portal tunnel is ready. Now we might be able to send the network management reply. */
        NetworkManagement_Init();
      }
      else if (ev == ZIP_EVENT_NEW_NETWORK)
      {
        /*We have entered a new Z-Wave network */
        new_pan_ula();
      }
      else if (ev == tcpip_event)
      {
        if (data == (void*) TCP_READY)
        {
          /* Now we are able to send IPv6 packages */
          LOG_PRINTF("Comming up\n");
          system_net_hook(1);

          ClassicZIPNode_init();
          zgw_component_start(ZGW_BRIDGE);
          bridge_init();
        }
      }
      else if (ev == ZIP_EVENT_QUEUE_UPDATED)
      {
        process_node_queues();
      }
      else if (ev == PROCESS_EVENT_EXITED)
      {
        LOG_PRINTF("A process exited %p\n",data);
#ifndef DISABLE_DTLS
        if (data == &dtls_server_process)
        {
          LOG_PRINTF("dtls process exited \n");
          dtls_close_all();
        } else
#endif
        if (data == &mDNS_server_process)
        {
          LOG_PRINTF("mDNS process exited \n");
          sec2_persist_span_table();
          rd_destroy();
          NetworkManagement_mdns_exited();
          data_store_exit();
        }
      } else if(ev == PROCESS_EVENT_EXIT) {
        LOG_PRINTF("Bye bye\n");
        zgw_component_start(ZGW_SHUTTING_DOWN);
#ifdef NO_ZW_NVM
        zw_appl_nvm_close();
#endif
      }
      else if (ev == ZIP_EVENT_ALL_NODES_PROBED)
      {
        LOG_PRINTF("All nodes have been probed\n");
        NetworkManagement_all_nodes_probed();
        NetworkManagement_NetworkUpdateStatusUpdate(NETWORK_UPDATE_FLAG_PROBE);

        /* We need to reenable this in case we disabled it during virtual node creation */
        NetworkManagement_smart_start_init_if_pending();

        /* Send node list if needed and all nodes at this point has an ipaddress, if they do not
         * #ZIP_EVENT_NODE_DHCP_TIMEOUT will be called at a later stage */
        if( ipv46nat_all_nodes_has_ip() ) {
          send_nodelist();
        }
        /*Kickstart the node queue again*/
        process_post(&zip_process,ZIP_EVENT_QUEUE_UPDATED,0);

        /* Check if we are waiting for backup */
        zip_router_check_backup(data);
      } else if (ev == ZIP_EVENT_NODE_PROBED)
      {
         /* Tell NMS that the node probe has completed. */
         NetworkManagement_node_probed(data);
      } else if (ev == ZIP_EVENT_ALL_IPV4_ASSIGNED
                 || ev == ZIP_EVENT_NODE_DHCP_TIMEOUT)
      {
        /* FIXME: We trigger the script here. But what if few nodes do not get DHCP lease. THen
           we dont get this event. Find a correct place where GW gets the IP address or?
         */
        if (ev == ZIP_EVENT_ALL_IPV4_ASSIGNED)
        {
          char ip[IPV6_STR_LEN] = {0};
          char *execve_args[2] = {0};
          pid_t pid = 0;

          snprintf(ip, IPV6_STR_LEN, "IP=%d.%d.%d.%d", uip_hostaddr.u8[0], uip_hostaddr.u8[1], uip_hostaddr.u8[2], uip_hostaddr.u8[3]);
          char *const env[] = {"PATH=/bin:/sbin", ip, 0};
          execve_args[0] = (char*)linux_conf_fin_script;
          execve_args[1] = 0;

          if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
            ERR_PRINTF("Failed to ignore signals from fin script process. Call to signal(SIGCHLD, SIG_IGN) failed. errno=%d\n", errno);
          }

          pid = fork();

          switch (pid)
          {
            case -1: /* Fork failed */
              ERR_PRINTF("Cannot spawn process for fin script. Call to fork() fialed. errno=%d, strerror=%s\n", errno, strerror(errno));
              ASSERT(FALSE);
              break;

            case 0: /* Child process */
              /* Restore the signal for child process. */
              signal(SIGCHLD, SIG_DFL);
              /* Call the external script. It'll never return on success. */
              execve(linux_conf_fin_script, execve_args, env);
              /* script failed. */
              ERR_PRINTF("Error executing: %s. errno=%d, strerror=%s\n",linux_conf_fin_script, errno, strerror(errno));
              break;

            default: /* Parent process */
              /* Continue moving forward */
              break;
          }
        }
        udp_server_check_ipv4_queue();
        /*Send node list if we have no unprobed nodes */
        if(!rd_probe_in_progress()) {
          send_nodelist();
        }

        LOG_PRINTF("DHCP pass completed\n");

        NetworkManagement_NetworkUpdateStatusUpdate(NETWORK_UPDATE_FLAG_DHCPv4);
      }
      else if (ev == ZIP_EVENT_BRIDGE_INITIALIZED)
      {
        LOG_PRINTF("Bridge init done\n");
        zgw_component_done(ZGW_BRIDGE, data);
        ApplicationInitProtocols();

        /* Unpersist S2 SPAN table after ApplicationInitProtocols,
         * as that initialize Resource Directory, which holds the persisted S2 SPAN table*/
        sec2_unpersist_span_table();

        /* With this IPv6 ready it might be possible to for the NM
         * module to deliver the reply package after a Set default or
         * learn mode. */
        NetworkManagement_Init();
        send_nodelist();

        tcpip_ipv4_force_start_periodic_tcp_timer();

        /* Tell NM to poll for its requirements.  This will set
         * NETWORK_UPDATE_FLAG_VIRTUAL and maybe other flags. */
        NetworkManagement_NetworkUpdateStatusUpdate(0);
      } else if(ev == ZIP_EVENT_NETWORK_MANAGEMENT_DONE) {
        /* Always send node list report when GW just starts */
        if (zgw_initing == TRUE) {
          set_should_send_nodelist();
          zgw_component_done(ZGW_BOOTING, data);
          zgw_initing = FALSE;
        }
        send_nodelist();
        process_node_queues();
        if (ZGW_COMPONENT_ACTIVE(ZGW_ROUTER_RESET)) {
           //DBG_PRINTF("ZGW restart completed\n");
           zgw_component_done(ZGW_ROUTER_RESET, data);
        }
        if (NetworkManagement_idle()) {
           /* If NMS is not waiting for a middleware probe, it is done
            * now. */
           zgw_component_done(ZGW_NM, data);
        }
      } else if(ev == ZIP_EVENT_NM_VIRT_NODE_REMOVE_DONE) {
        DBG_PRINTF(" ZIP_EVENT_NM_VIRT_NODE_REMOVE_DONE triggered\n");
        NetworkManagement_VirtualNodes_removed();
      } else if (ev == ZIP_EVENT_BACKUP_REQUEST) {
        if (ZGW_COMPONENT_ACTIVE(ZGW_BU)) {
          WRN_PRINTF("Backup already requested once.\n");
        }
        zgw_component_start(ZGW_BU);
        zip_router_check_backup(data);
      } else if (ev == ZIP_EVENT_COMPONENT_DONE) {
         if (ZGW_COMPONENT_ACTIVE(ZGW_BU)) {
            if (data == (void*)extend_middleware_probe_timeout) {
               DBG_PRINTF("NetworkManagement done after middleware probe timeout.\n");
            } else {
               DBG_PRINTF("Component done with data: %p\n", data);
            }
         }
         zip_router_check_backup(data);
      } else if (ev == PROCESS_EVENT_TIMER) {
         if (data == (void*)&zgw_bu_timer) {
            zip_router_check_backup(data);
         }
      }

      PROCESS_WAIT_EVENT()
      ;
    }

  PROCESS_END()
  }

  /*---------------------------------------------------------------------------*/
