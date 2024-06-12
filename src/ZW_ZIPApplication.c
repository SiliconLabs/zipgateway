/* © 2014 Silicon Laboratories Inc.
 */

#include "ZW_ZIPApplication.h"
#include "ZW_udp_server.h"
#include "CC_NetworkManagement.h"
#include "CC_FirmwareUpdate.h"
#include "CC_Portal.h"
#include "CC_Gateway.h"
#include "CC_Wakeup.h"
#include "command_handler.h"
#include "Mailbox.h"
#include "NodeCache.h"
#include "zw_network_info.h"
#include "zip_router_ipv6_utils.h" /* nodeOfIP */
#include "router_events.h"
#include "ZW_controller_api.h"
#include <stdlib.h>
#include <arpa/inet.h>
#include "ZW_classcmd.h"
#include "Serialapi.h"
#ifdef SECURITY_SUPPORT

#include "security_layer.h"
#endif
#include "ResourceDirectory.h"
#include "ClassicZIPNode.h"

#define DEBUG DEBUG_FULL
#include "net/uip-debug.h"

#include "ipv46_nat.h"
#include "DataStore.h"
#include "RD_DataStore.h"

#include "security_layer.h"
#include "S2_wrap.h"
#include "sys/process.h"
#include "ZIP_Router_logging.h"
security_scheme_t net_scheme;

BYTE IPNIF[0xff];
BYTE IPNIFLen=0;
BYTE IPSecureClasses[64];
BYTE IPnSecureClasses=0;
extern uint8_t suc_changed;


BYTE MyNIF[0xff];
BYTE MyNIFLen=0;
/* Secure Command classes for LAN */
BYTE SecureClasses[64];
BYTE nSecureClasses=0;
/* Extra Secure Command classes for unsolicited destination, added by portal, advertised only to PAN */BYTE nSecureClassesPAN =
    0;
BYTE SecureClassesPAN[16];
static struct ctimer new_node_probe_timer;

#define NIF ((NODEINFO*) MyNIF)
#define CLASSES ((BYTE*)&MyNIF[sizeof(NODEINFO)])

#define ADD_COMMAND_CLASS(c) { \
    CLASSES[MyNIFLen-sizeof(NODEINFO)] = c; \
    MyNIFLen++; ASSERT(MyNIFLen < sizeof(MyNIF)); }

ZW_APPLICATION_TX_BUFFER txBuf;

/** Set if the gateway should send a nodelist after an asynchronous
 * operation such as a node probe or reset. */
static BOOL should_send_nodelist = 0;

/**
 * Command classes which we always support non-secure
 */
const BYTE MyClasses[] = {
    COMMAND_CLASS_ZWAVEPLUS_INFO,
    COMMAND_CLASS_TRANSPORT_SERVICE,
    COMMAND_CLASS_CRC_16_ENCAP,
    COMMAND_CLASS_APPLICATION_STATUS,
    COMMAND_CLASS_SECURITY_2};

const BYTE IpClasses[] = {
    COMMAND_CLASS_ZIP,
    COMMAND_CLASS_ZIP_ND,
};

/* Forward declarations */
void build_ip_nif();



/****************************************************************************/
/*                              PRIVATE DATA                                */
/****************************************************************************/

#define STR_CASE(x) \
  case x:           \
    return #x;

const char* network_scheme_name(security_scheme_t scheme)
{
  static char message[25];
  switch (scheme) {
  STR_CASE(NO_SCHEME)
  STR_CASE(SECURITY_SCHEME_0)
  STR_CASE(SECURITY_SCHEME_2_UNAUTHENTICATED)
  STR_CASE(SECURITY_SCHEME_2_AUTHENTICATED)
  STR_CASE(SECURITY_SCHEME_2_ACCESS)
  STR_CASE(SECURITY_SCHEME_UDP)
  STR_CASE(USE_CRC16)

  default:
    snprintf(message, sizeof(message), "%d", scheme);
    return message;
  }
}

/**
 * Setup the GW NIF 
 */
BYTE ApplicationInitNIF(void) 
{ /* IN   Nothing   */
  static uint16_t cc_disable_list[10];
  static int cc_disable_list_length=0;

  BYTE ver, capabilities, len, chip_type, chip_version;
  BYTE nodelist[32];
  BYTE cap;
  nodeid_t n;
  BYTE *c;
  DWORD tmpHome; //Not using homeID scince it might have side effects
  int i;
  ZW_GetNodeProtocolInfo(MyNodeID, NIF);
  NIF->nodeType.specific = SPECIFIC_TYPE_GATEWAY;
  NIF->nodeType.generic= GENERIC_TYPE_STATIC_CONTROLLER;
  memcpy(CLASSES,MyClasses,sizeof(MyClasses));
  MyNIFLen = sizeof(MyClasses) + sizeof(NODEINFO);

  SerialAPI_GetInitData(&ver, &capabilities, &len, nodelist, &chip_type,
      &chip_version);
  // NOTE: No need of getting LR node list here as we dont really need it
  // Even classic nodelist above is not used anywhere in this function


  LOG_PRINTF("%u00 series chip version %u serial api version %u\n",chip_type,chip_version,ver);

  if(ver <8) {
    WRN_PRINTF("SMART START is disabled because the controller library used is too old. Please upgrade controller to enable SMART START.");
    cfg.enable_smart_start=0;
  } else {
    cfg.enable_smart_start=1;
  }

  cap = ZW_GetControllerCapabilities();

  if( (cap & (CONTROLLER_NODEID_SERVER_PRESENT | CONTROLLER_IS_SECONDARY) )==0  ) {
     MemoryGetID((BYTE*) &tmpHome,&n);
     LOG_PRINTF("Assigning myself(NodeID %u ) SIS role \n",n);
     ZW_SetSUCNodeID(n,TRUE,FALSE,ZW_SUC_FUNC_NODEID_SERVER,0);
     cap = ZW_GetControllerCapabilities();
     suc_changed = 1;
  }

  if((cap & CONTROLLER_NODEID_SERVER_PRESENT) == 0  || (cap & CONTROLLER_IS_SUC) == 0) {
    cfg.enable_smart_start=0;
    WRN_PRINTF("SMART START is disabled because the controller is not SIS");
  }

  if(capabilities & GET_INIT_DATA_FLAG_SECONDARY_CTRL) {
    LOG_PRINTF("I'am a Secondary controller\n");
    controller_role = SECONDARY;
  }
  if(capabilities & GET_INIT_DATA_FLAG_IS_SUC) {
    LOG_PRINTF("I'am SUC\n");
    controller_role = SUC;
  }
  if(capabilities & GET_INIT_DATA_FLAG_SLAVE_API) {
    LOG_PRINTF("I'am slave\n");
    controller_role = SLAVE;
  }


  MemoryGetID((BYTE*) &homeID, &MyNodeID);
  security_init( );

  uint8_t flags = sec2_get_my_node_flags();
  net_scheme = NO_SCHEME;

  if(flags & NODE_FLAG_SECURITY0) {  
    /*Security 0 should only go to the NIF if we have S0 key*/ 
    ADD_COMMAND_CLASS(COMMAND_CLASS_SECURITY); 
    net_scheme = SECURITY_SCHEME_0;
  }
  if(flags & NODE_FLAG_SECURITY2_UNAUTHENTICATED) net_scheme = SECURITY_SCHEME_2_UNAUTHENTICATED;
  if(flags & NODE_FLAG_SECURITY2_AUTHENTICATED) net_scheme = SECURITY_SCHEME_2_AUTHENTICATED;
  if(flags & NODE_FLAG_SECURITY2_ACCESS) net_scheme = SECURITY_SCHEME_2_ACCESS;

  LOG_PRINTF("Network scheme is: %s\n", network_scheme_name(net_scheme));
  ZW_command_handler_init();
  cc_disable_list_length=0;
  if (cap & CONTROLLER_NODEID_SERVER_PRESENT)
  {
    LOG_PRINTF("I'm a primary or inclusion controller.\n");
  }
  else if ((cap & CONTROLLER_IS_SECONDARY) == 0)
  {
    DBG_PRINTF("I'am a Primary controller\n");
  } else {
    /*Remove the inclusion controller cc from the list*/
    cc_disable_list[cc_disable_list_length++] = COMMAND_CLASS_INCLUSION_CONTROLLER;
    cc_disable_list[cc_disable_list_length++] = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
  }

  build_ip_nif();
  if(!cfg.enable_smart_start) {
    cc_disable_list[cc_disable_list_length++] = COMMAND_CLASS_PROVISIONING_LIST;
  }
  //Just make sure :-)
  assert( sizeof(cc_disable_list) / sizeof(uint16_t)  > cc_disable_list_length);

  ZW_command_handler_disable_list(cc_disable_list,cc_disable_list_length);

  /*Build the NIF which we present on the UDP side.*/
  memcpy(IPNIF,NIF,MyNIFLen);
  IPNIFLen = MyNIFLen;

  memcpy(IPNIF+IPNIFLen,IpClasses,sizeof(IpClasses));
  IPNIFLen+= sizeof(IpClasses);

  IPNIFLen+= ZW_command_handler_get_nif(NO_SCHEME, &IPNIF[IPNIFLen],sizeof(IPNIF)-IPNIFLen);
  IPnSecureClasses = ZW_command_handler_get_nif(SECURITY_SCHEME_UDP, &IPSecureClasses[0],sizeof(IPSecureClasses));

  MyNIFLen+= ZW_command_handler_get_nif(NO_SCHEME, &MyNIF[MyNIFLen],sizeof(MyNIF)-MyNIFLen);
  nSecureClasses = ZW_command_handler_get_nif(net_scheme, &SecureClasses[0],sizeof(SecureClasses));

  /*
   * The extra classes from the config file is added securely
   */
  appNodeInfo_CC_Add();
  return 0;
}

/**
 * Rebuild the IP nif and secure classes list from scratch. Good starting point for reinitializing
 * or changing the IP NIF dynamically.
 */
void build_ip_nif()
{
  /*Build the NIF which we present on the UDP side.*/
  memcpy(IPNIF,NIF,MyNIFLen);
  IPNIFLen = MyNIFLen;

  memcpy(IPNIF+IPNIFLen,IpClasses,sizeof(IpClasses));
  IPNIFLen+= sizeof(IpClasses);

  IPNIFLen+= ZW_command_handler_get_nif(NO_SCHEME, &IPNIF[IPNIFLen],sizeof(IPNIF)-IPNIFLen);
  IPnSecureClasses = ZW_command_handler_get_nif(SECURITY_SCHEME_UDP, &IPSecureClasses[0],sizeof(IPSecureClasses));
}

/*Initialization of the protocols */
void
ApplicationInitProtocols(void) /* IN   Nothing   */
{
  ZW_SendDataAppl_init();
#ifdef SUPPORTS_MDNS
  rd_init(FALSE);
#endif

  SetCacheEntryFlagMasked(MyNodeID,sec2_get_my_node_flags(),0xFF);

  rd_probe_new_nodes();
}

static void
update_timeout(void* c)
{
  rd_probe_new_nodes();
}


void
ApplicationControllerUpdate(BYTE bStatus, /**<  Status event */
nodeid_t bNodeID, /**<  Node id of the node that send node info */
BYTE* pCmd, /**<  Pointer to Application Node information */
BYTE bLen, /**<  Node info length                        */
BYTE *prospectHomeID /**< NULL or the prospect homeid if smart start inclusion */
)CC_REENTRANT_ARG
{

  LOG_PRINTF("ApplicationControllerUpdate: status=0x%x node=%" PRIu16 " NIF len=%u\n",
      (unsigned )bStatus, bNodeID, bLen);

  if ((bStatus == UPDATE_STATE_NODE_INFO_FOREIGN_HOMEID_RECEIVED) ||
      (bStatus == UPDATE_STATE_NODE_INFO_SMARTSTART_HOMEID_RECEIVED_LR))
  {
    /* This bStatus has different syntax from others, so handle this first */
    /* pCmd is first part of the public key-derived homeID */

    assert(prospectHomeID != NULL);
    NetworkManagement_smart_start_inclusion(ADD_NODE_OPTION_NETWORK_WIDE | ADD_NODE_OPTION_NORMAL_POWER,
                                            prospectHomeID,
                                            bStatus == UPDATE_STATE_NODE_INFO_SMARTSTART_HOMEID_RECEIVED_LR);

    return;
  }
  else if (bStatus == UPDATE_STATE_INCLUDED_NODE_INFO_RECEIVED)
  {
    uint8_t INIF_rxStatus;
    uint8_t INIF_NWI_homeid[4];

    INIF_rxStatus = pCmd[0];
    memcpy(INIF_NWI_homeid, &pCmd[1], 4);
    NetworkManagement_INIF_Received(bNodeID, INIF_rxStatus, INIF_NWI_homeid);
    return;
  }

  if( nodemask_nodeid_is_invalid(bNodeID) && (bStatus != UPDATE_STATE_NODE_INFO_REQ_FAILED)) {
      ERR_PRINTF("Controller update from invalid nodeID %d",bNodeID);
      return;
  }

  switch (bStatus)
  {

  case UPDATE_STATE_NEW_ID_ASSIGNED:
    should_send_nodelist = TRUE;

    /**
     * We start a timer here to wait for the inclusion controller to probe the node, If
     * we get an COMMAND_CLASS_INCLUSION_CONTROLLER INITATE command, then we stop this timer.
     */
    if(NetworkManagement_getState() == NM_IDLE) {
      ctimer_set(&new_node_probe_timer,5000,update_timeout,0);
    }
    break;
  case UPDATE_STATE_NODE_INFO_RECEIVED:
      rd_node_is_alive(bNodeID);
      if (bNodeID && bLen)
      {
#ifdef SUPPORTS_MDNS
      rd_nif_request_notify(TRUE,bNodeID,pCmd,bLen);
#endif
      NetworkManagement_nif_notify(bNodeID,pCmd,bLen);
      }
    break;
  case UPDATE_STATE_NODE_INFO_REQ_DONE:
      //NodeInfoRequestDone(UPDATE_STATE_NODE_INFO_REQ_DONE);
	  break;
  case UPDATE_STATE_NODE_INFO_REQ_FAILED:
      //NodeInfoRequestDone(UPDATE_STATE_NODE_INFO_REQ_FAILED);
#ifdef SUPPORTS_MDNS
	  rd_nif_request_notify(FALSE,bNodeID,pCmd,bLen);
#endif
	  break;
  case UPDATE_STATE_ROUTING_PENDING:
    break;
  case UPDATE_STATE_DELETE_DONE:
    should_send_nodelist = TRUE;
    send_nodelist();
#ifdef SUPPORTS_MDNS
    rd_remove_node(bNodeID);
#endif
    ip_assoc_remove_by_nodeid(bNodeID);
    NetworkManagement_smart_start_init_if_pending();
    break;
  case UPDATE_STATE_SUC_ID:
    should_send_nodelist = TRUE;
    if ((NetworkManagement_getState() != NM_WAIT_FOR_SECURE_LEARN) && 
        (NetworkManagement_getState() != NM_LEARN_MODE_STARTED)) {
        if(bNodeID !=0 && bNodeID == MyNodeID) {
          /*Create an async application reset */
          process_post(&zip_process,ZIP_EVENT_RESET,0);
        }
    } else {
        /* Its not safe to reset in these states as these states will eventually reset the gateway*/
        DBG_PRINTF("NM state is NM_WAIT_FOR_SECURE_LEARN or NM_LEARN_MODE_STARTED. Not resetting\n");
    }
    DBG_PRINTF("SUC node Id updated, new ID is %i...\n",bNodeID);
    break;
  }
}


static void ApplicationBusy(zwave_connection_t*c)
{
  txBuf.ZW_ApplicationBusyFrame.cmdClass=COMMAND_CLASS_APPLICATION_STATUS;
  txBuf.ZW_ApplicationBusyFrame.cmd=APPLICATION_BUSY;
  txBuf.ZW_ApplicationBusyFrame.status=0;
  ZW_SendDataZIP(c,(BYTE*)&txBuf,sizeof(txBuf.ZW_ApplicationBusyFrame), 0);
}

void set_should_send_nodelist()
{
  should_send_nodelist = 1;
  return;
}



/**
 * Main application command handler for commands coming both via Z-Wave and IP
 */
bool
ApplicationIpCommandHandler(zwave_connection_t *c, void *pData, u16_t bDatalen) REENTRANT
{
  uint8_t rnode;
	ZW_APPLICATION_TX_BUFFER* pCmd = (ZW_APPLICATION_TX_BUFFER*)pData;
  c->tx_flags =((c->rx_flags & RECEIVE_STATUS_LOW_POWER) ? TRANSMIT_OPTION_LOW_POWER : 0)
          | TRANSMIT_OPTION_ACK | TRANSMIT_OPTION_EXPLORE
					| TRANSMIT_OPTION_AUTO_ROUTE;

  if (bDatalen < 2)
  {
	  //ERR_PRINTF("%s: Package is too small.",__FUNCTION__);
	  ERR_PRINTF("ApplicationIpCommandHandler: Package is too small.\r\n");
	  return true;
	}

  // The wake command class is a controlling command, it will receive 
  // special attention.
  if(pCmd->ZW_NodeInfoCachedGetFrame.cmdClass == COMMAND_CLASS_WAKE_UP) {
    if(WakeUpHandler(c,pData, bDatalen) == COMMAND_HANDLED) {
      return true;
    }
  }

  if (pCmd->ZW_SupervisionGetFrame.cmdClass == COMMAND_CLASS_SUPERVISION &&
               pCmd->ZW_SupervisionGetFrame.cmd == SUPERVISION_GET) {
    /* Always allow, whether or not it's multicast */
  } else  if (
      ((c->rx_flags & RECEIVE_STATUS_TYPE_MASK)  != RECEIVE_STATUS_TYPE_SINGLE) ||
      uip_is_addr_mcast(&c->lipaddr) ) {
    /* Drop all multicast frames except for node info cached get from IP */
      goto send_to_unsolicited;
  }
  NetworkManagement_frame_notify();

  // FALSE since we are not unwrapping supervision and CC_Supervision will set
  // this flag.
  switch(ZW_command_handler_run(c,pData,bDatalen, FALSE)) {
  case COMMAND_HANDLED:
    return true; //We are done
  case COMMAND_CLASS_DISABLED:
    WRN_PRINTF("Disabled command  0x%02x:0x%02x from ", pCmd->ZW_Common.cmdClass, pCmd->ZW_Common.cmd);
    return true; //Don't handle for disabled CCs. Supervision will do the job.
  case COMMAND_NOT_SUPPORTED:
    WRN_PRINTF("Unknown command  0x%02x:0x%02x from ", pCmd->ZW_Common.cmdClass, pCmd->ZW_Common.cmd);
    return true;
  case COMMAND_BUSY:
    ApplicationBusy(c);
    return true;
  case COMMAND_POSTPONED:
    return false; //We are done
  case COMMAND_PARSE_ERROR:
    return true; //Just drop
  case CLASS_NOT_SUPPORTED:
    break; //move on
  }

  DBG_PRINTF("Unhandled command  0x%02x:0x%02x from ", pCmd->ZW_Common.cmdClass, pCmd->ZW_Common.cmd);
  PRINT6ADDR(&c->ripaddr);
  DBG_PRINTF("\n");

send_to_unsolicited:

  if(!rd_check_security_for_unsolicited_dest(nodeOfIP(&c->ripaddr),c->scheme,pData,bDatalen)) {
    WRN_PRINTF("Frame not forwarded to unsolicited because it was not sent on right scheme or not supported.");
    return true;
  }

  //We are forwarding the frame to the unsolicited destination.
  if (!uip_is_addr_unspecified(&cfg.unsolicited_dest))
  {
    /*If not for classic me then send to unsolicited destination, and only ask for ACK for^M
     * single cast frames */
    ClassicZIPNode_SendUnsolicited(c, pCmd, bDatalen, &cfg.unsolicited_dest, UIP_HTONS(cfg.unsolicited_port),
        (c->rx_flags & RECEIVE_STATUS_TYPE_MASK) == RECEIVE_STATUS_TYPE_SINGLE);
  }

  if (!uip_is_addr_unspecified(&cfg.unsolicited_dest2))
  {
    /* TODO: Is it intentional that we dont set ack request for unsoc dest 2. We set it for unsoc1. */
    /* It could be intentional to avoid some sort of App Busy response. */
    ClassicZIPNode_SendUnsolicited(c, pCmd, bDatalen, &cfg.unsolicited_dest2, UIP_HTONS(cfg.unsolicited_port2), FALSE);
  }
  return true;
}


 static int
 compare_bytes (const void *a, const void *b)
 {
   const uint8_t *da = (const uint8_t *) a;
   const uint8_t *db = (const uint8_t *) b;
   return (*da > *db) - (*da < *db);
 }

 /**
  * Remove duplicates from byte array. This function will
  * sort the list.
  *
  * \param data     input array
  * \param data_len length of array
  * \return         new length of array
  */
 int uniq_byte_array(uint8_t* data, int data_len) {
   qsort( data, data_len, sizeof(uint8_t),compare_bytes);

   uint8_t* s,*d;

   s=d =data;
   do
   {
     *d = *s;
     s++;
     if(*s == *d) s++;
     d++;
   } while(s < (data+data_len));
   return d-data;
 }


void
ApplicationDefaultSet()
{
  rd_exit();

  /* Release the ipv4 addresses of the nodes. */
  ipv46nat_del_all_nodes();

  rd_data_store_invalidate();

  /*Enable SUC/SIS */
  MemoryGetID((BYTE*) &homeID, &MyNodeID);
  /* ZW_EnableSUC(TRUE,ZW_SUC_FUNC_NODEID_SERVER); call is deprecated */
  ZW_SetSUCNodeID(MyNodeID,TRUE,FALSE,ZW_SUC_FUNC_NODEID_SERVER,0);
  suc_changed = 0;

  security_set_default();
  sec2_create_new_network_keys();
}

/*
 * Add secure command classes for the unsolicited destination to GW.
 * Subsequent calls to this function will overwrite the existing secure CCs in the list.
 *  These CCs are only advertized on the PAN. */
void
AddSecureUnsocDestCCsToGW(BYTE *ccList, BYTE ccCount) CC_REENTRANT_ARG
/* Reentrant to conserve XDATA on ASIX C51 */
{
  BYTE idx,i;
  for (idx = 0; idx < ccCount; idx++)
  {
    SecureClasses[nSecureClasses++] = ccList[idx];
    IPSecureClasses[IPnSecureClasses++] = ccList[idx];
  }

   // FIXME this will not work well with extended command classes
  nSecureClasses = uniq_byte_array(SecureClasses,nSecureClasses);
  IPnSecureClasses = uniq_byte_array(IPSecureClasses,IPnSecureClasses);


/*  for(i=0 ; i < IPnSecureClasses; i++) {
    printf("AddSecureUnsocDestCCsToGW Sec IP class %x\n",IPSecureClasses[i]);
  }*/

}
void reset_the_nif()
{
  ZW_GetNodeProtocolInfo(MyNodeID, NIF);
  NIF->nodeType.specific = SPECIFIC_TYPE_GATEWAY;
  NIF->nodeType.generic= GENERIC_TYPE_STATIC_CONTROLLER;
  memcpy(CLASSES,MyClasses,sizeof(MyClasses));
  MyNIFLen = sizeof(MyClasses) + sizeof(NODEINFO);

  if (is_sec0_key_granted()>0) {
    ADD_COMMAND_CLASS(COMMAND_CLASS_SECURITY);
  }

  memcpy(IPNIF,NIF,MyNIFLen);
  IPNIFLen = MyNIFLen;

  memcpy(IPNIF+IPNIFLen,IpClasses,sizeof(IpClasses));
  IPNIFLen+= sizeof(IpClasses);

  IPNIFLen+= ZW_command_handler_get_nif(NO_SCHEME, &IPNIF[IPNIFLen],sizeof(IPNIF)-IPNIFLen);
  IPnSecureClasses = ZW_command_handler_get_nif(SECURITY_SCHEME_UDP, &IPSecureClasses[0],sizeof(IPSecureClasses));

  /* Append command classes whose handlers say NO_SCHEME */
  MyNIFLen+= ZW_command_handler_get_nif(NO_SCHEME, &MyNIF[MyNIFLen],sizeof(MyNIF)-MyNIFLen);

  nSecureClasses = ZW_command_handler_get_nif(net_scheme, &SecureClasses[0],sizeof(SecureClasses));

}

/* This function returns TRUE only if the CC is on the very short list of command classes
 * that MUST remain nonsecure even when the network security scheme is higher.*/
static int is_CC_pinned_to_nonsecure(uint8_t cc)
{
  uint8_t nonsecure_CCs[] = {COMMAND_CLASS_TIME};

  /* TODO: This will not work with extended command classes */
  for (int idx = 0; idx < sizeof (nonsecure_CCs); idx++) {
    if (cc == nonsecure_CCs[idx]) {
      return TRUE;
    }
  }
  return FALSE;
}

/*
 *   Add CCs to PAN Node Info Frame. Useful for advertising capabilities
 *   of the unsolicited destination.
 *   Subsequent calls to this function will overwrite previously added CCs.
 */
void AddUnsocDestCCsToGW(BYTE *ccList, BYTE ccCount) CC_REENTRANT_ARG
/* Reentrant to conserve XDATA on ASIX C51 */
{
  int idx,i;

  for (idx = 0; idx < ccCount; idx++)  {
    /* If net_scheme is anything but non-secure, we invoke the support-on-highest
     * recommendation and move all the non-secure extra classes up to secure.
     * Except for Multi and Time CCs, they MUST stay in nonsecure.
     * If net_scheme is nonsecure, we keep all extra classes nonsecure.
     */
    if ((net_scheme == NO_SCHEME) || is_CC_pinned_to_nonsecure(ccList[idx])) {
      ADD_COMMAND_CLASS( ccList[idx] );
      IPNIF[IPNIFLen++] =  ccList[idx];
    } else {
      assert(nSecureClasses < sizeof(SecureClasses));
      SecureClasses[nSecureClasses++] = ccList[idx];
      assert(IPnSecureClasses < sizeof(IPSecureClasses));
      IPSecureClasses[IPnSecureClasses++] = ccList[idx];
    }
  }

  // FIXME this will not work well with extended command classes
  MyNIFLen = uniq_byte_array( &MyNIF[sizeof(NODEINFO)], MyNIFLen - sizeof(NODEINFO) ) + sizeof(NODEINFO);
  IPNIFLen = uniq_byte_array( &IPNIF[sizeof(NODEINFO)], IPNIFLen - sizeof(NODEINFO) ) + sizeof(NODEINFO);

  /*Make Z-Wave+ info appear first */
  for(idx = sizeof(NODEINFO); idx < MyNIFLen; idx++) {

    if(MyNIF[idx] == COMMAND_CLASS_ZWAVEPLUS_INFO) {
      MyNIF[idx] = MyNIF[sizeof(NODEINFO)];
      MyNIF[sizeof(NODEINFO)] = COMMAND_CLASS_ZWAVEPLUS_INFO;
    }
  }
}

void CommandClassesUpdated() {
  BYTE listening;
  APPL_NODE_TYPE nodeType;
  BYTE *nodeParm;
  BYTE parmLength;

  LOG_PRINTF("Command classes updated\n");

	/* this is a listening node and it supports optional CommandClasses */
	listening = APPLICATION_NODEINFO_LISTENING
			| APPLICATION_NODEINFO_OPTIONAL_FUNCTIONALITY;
	nodeType.generic = NIF->nodeType.generic; /* Generic device type */
	nodeType.specific = NIF->nodeType.specific; /* Specific class */

	nodeParm = CLASSES; /* Send list of known command classes. */
	parmLength = MyNIFLen - sizeof(NODEINFO); /* Set length*/

  SerialAPI_ApplicationNodeInformation(listening,nodeType,nodeParm,parmLength);
  security_set_supported_classes( SecureClasses , nSecureClasses );
  rd_register_new_node(MyNodeID,0);
}


void send_nodelist() {
  if (should_send_nodelist
      && (!uip_is_addr_unspecified(&cfg.unsolicited_dest)
          || !uip_is_addr_unspecified(&cfg.unsolicited_dest2))) {
    if(NetworkManagement_SendNodeList_To_Unsolicited()) {
      should_send_nodelist =FALSE;
    }
  }
}

void SerialAPIStarted(uint8_t *pData, uint8_t length)
{
  LOG_PRINTF("SerialAPI restarted\n");
  if (ZW_RFRegionGet() == RF_US_LR) {
    if (SerialAPI_EnableLR() == false) {
      LOG_PRINTF("Fail to enable Z-Wave Long Range capability\n");
    }
  } else {
    if (SerialAPI_DisableLR() == false) {
      LOG_PRINTF("Fail to disable Z-Wave Long Range capability\n");
    }
  }
  NetworkManagement_smart_start_init_if_pending();
}

void SetPreInclusionNIF(security_scheme_t target_scheme) {
  const APPL_NODE_TYPE nodeType = {GENERIC_TYPE_STATIC_CONTROLLER,SPECIFIC_TYPE_GATEWAY};
  security_scheme_t scheme_bak;
  uint8_t tmpNIF[64];
  uint8_t tmpNIFlen;

  LOG_PRINTF("Setting pre-inclusion NIF\n");
  memcpy(tmpNIF,MyClasses,sizeof(MyClasses));
  tmpNIFlen= sizeof(MyClasses);
  /*We also need this */
  tmpNIF[tmpNIFlen++] = COMMAND_CLASS_SECURITY;

  scheme_bak = net_scheme;
  net_scheme = target_scheme;

  /* TODO: Why do we operate on a temp NIF here?
   * Both SecureClasses and IPSecureClasses and IPNIF are simply overwritten and restored later.
   * We could do the same for NIF. */
  tmpNIFlen+=ZW_command_handler_get_nif( NO_SCHEME, tmpNIF+tmpNIFlen,sizeof(tmpNIF)-tmpNIFlen);

  /* Rebuild SecureClasses using target scheme*/
  nSecureClasses = ZW_command_handler_get_nif(target_scheme, &SecureClasses[0],sizeof(SecureClasses));

  /* We rebuild and add extra classes to the IP NIFs alongside the normal nif
   * to keep security class in sync on IP side and PAN side.
   * Note that we are building it for target_scheme via a global var.
   * */
  build_ip_nif();

  net_scheme=scheme_bak;

  /* If target scheme is anything but non-secure, we invoke the support-on-highest
   * recommendation and move all the non-secure extra classes up to secure.
   * Except for Multi and Time CCs, they MUST stay in nonsecure
   * If target scheme is nonsecure, we keep all extra classes nonsecure.
   * We dont need a backup of SecureClasses because we will only call this function
   * once with target_scheme != NO_SCHEME, then we will do an AppliationInitSW()
   * which will reset SecureClasses */
  for(int i=0; i < cfg.extra_classes_len; i++) {
    if ((target_scheme == NO_SCHEME) || is_CC_pinned_to_nonsecure(cfg.extra_classes[i])) {
      tmpNIF[tmpNIFlen++] = cfg.extra_classes[i];
      assert(IPNIFLen < sizeof(IPNIF));
      IPNIF[IPNIFLen++] =  cfg.extra_classes[i];
    } else {
      SecureClasses[nSecureClasses++] = cfg.extra_classes[i];
      assert(nSecureClasses < sizeof(SecureClasses));
      //nSecureClasses = uniq_byte_array(SecureClasses, nSecureClasses);
      assert(IPnSecureClasses < sizeof(IPSecureClasses));
      IPSecureClasses[IPnSecureClasses++] = cfg.extra_classes[i];
    }
  }
  /* Secure extra classes are just added to IP NIF as secure */
  AddSecureUnsocDestCCsToGW(cfg.sec_extra_classes, cfg.sec_extra_classes_len);

  security_set_supported_classes( SecureClasses , nSecureClasses );

  /* We always set the APPLICATION_NODEINFO_OPTIONAL_FUNCTIONALITY flag. And we put it in ERN that this flag has to be
   * unset if need */
  SerialAPI_ApplicationNodeInformation(TRUE | APPLICATION_NODEINFO_OPTIONAL_FUNCTIONALITY, nodeType, tmpNIF, tmpNIFlen);
}

void StopNewNodeProbeTimer() {
  ctimer_stop(&new_node_probe_timer);
}
