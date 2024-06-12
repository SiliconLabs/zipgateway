/* © 2019 Silicon Laboratories Inc. */

#define REPLACE_FAILED
#include "CC_NetworkManagement.h"
#include "ZW_ZIPApplication.h"
#include "ZW_udp_server.h"
#include "Serialapi.h"
#include "ZW_classcmd_ex.h"
#include "Bridge.h"
#include "dhcpc2.h"
#include "ClassicZIPNode.h"

#include "NodeCache.h"
#include "zw_network_info.h"
#include "ResourceDirectory.h"
#include "ZW_SendDataAppl.h"
#include "ZW_controller_api_ex.h"
#include "sys/rtimer.h"
#include "ipv46_nat.h"
#include "S2_wrap.h"
#include "s2_keystore.h"
#include "gw_s2_keystore.h"
#include "CC_InclusionController.h"
#include <provisioning_list.h>
#include "CC_provisioning_list.h"
#include "node_queue.h"
#include "Mailbox.h"
#include "provisioning_list.h"
#include "ZIP_Router_logging.h"
#include "zip_router_ipv6_utils.h"
#include "zip_router_config.h"
#include "router_events.h"
#include "CC_Indicator.h" /* For DefaultSet() */
#include "CC_NetworkManagement_queue.h"

#define ADD_REMOVE_TIMEOUT 6000
#define LEARN_TIMEOUT 6000
#define NETWORK_MANAGEMENT_TIMEOUT 2*6000
#define SMART_START_SELF_DESTRUCT_TIMEOUT (3*CLOCK_SECOND)
/* 240 seconds is enough for the joining node to time out S2 bootstrapping from any state */
#define SMART_START_SELF_DESTRUCT_RETRY_TIMEOUT (240*CLOCK_SECOND)

extern PROTOCOL_VERSION zw_protocol_version2;
/* Defer restarting Smart Start Add mode after including one smart start node until this time
 * has elapsed. */
#define SMART_START_MIDDLEWARE_PROBE_TIMEOUT (9 * CLOCK_SECOND)

#ifdef SECURITY_SUPPORT
#include "security_layer.h"
#endif

#include "s2_inclusion.h"

extern uint8_t ecdh_dynamic_key[32];
extern void crypto_scalarmult_curve25519_base(uint8_t *q, const uint8_t *n);
extern void keystore_public_key_debug_print(void);

static uint16_t BuildFailedNodeListFrame(uint8_t* fnl_buf,uint8_t seq);
static uint16_t BuildNodeListFrame(uint8_t* buf,uint8_t seq);

/* See CC.0034.01.08.11.001 */
#define NM_FAILED_NODE_REMOVE_FAIL 0x02
#define NM_FAILED_NODE_REMOVE_DONE 0x01
#define NM_FAILED_NODE_NOT_FOUND 0x00

struct node_add_smart_start_event_data {
  uint8_t *smart_start_homeID;
  uint8_t inclusion_options;
};

/****************************  Forward declarations *****************************************/
static void
RemoveNodeStatusUpdate(LEARN_INFO* inf);
static void
AddNodeStatusUpdate(LEARN_INFO* inf);
static void
LearnModeStatus(LEARN_INFO* inf);
static void
ReplaceFailedNodeStatus(BYTE status);
static void
RemoveSelfDestructStatus(BYTE status);
static void
RequestNodeNeighborUpdateStatus(BYTE status);

static void
nm_send_reply(void* buf, u16_t len);
static void
LearnTimerExpired(void);


static void
SecureInclusionDone(int status);
static void
timeout(void* none);

/** Disable networkUpdateStatusFlags to indicate that gw is rolling over.
 *
 *  Unlocks the probe engine.
 * 
 *  Should be called after #NM_EV_MDNS_EXIT, after posting reset, to
 *  set up the response that should be sent when gw is ready again.
*/
static void SendReplyWhenNetworkIsUpdated();

void
mem_replace(unsigned char *buf, char old, char new, size_t len);
size_t
mem_insert(u8_t* dst, const u8_t *src, u8_t find, u8_t add, size_t len, size_t max);

static void
nm_fsm_post_event(nm_event_t ev, void* event_data);
static void ResetState(BYTE dummy, void* user, TX_STATUS_TYPE *t);
static void wait_for_middleware_probe(BYTE dummy, void* user, TX_STATUS_TYPE *t);
static void nop_send_done(uint8_t status, void* user, TX_STATUS_TYPE *t);

/** Send a fail reply to \c (which may be different from nms.conn).
 * @param c Connection of the peer gw replies to.
 * @param pCmd Pointer to the incoming command.
 * @param bDatalen Command length.
 */
static void NetworkManagementReturnFail(zwave_connection_t* c, const ZW_APPLICATION_TX_BUFFER* pCmd, BYTE bDatalen);

/** Build a reply to a NodeInfoCachedGet.
 *
 * Populate the relevant data from the RD entry \p node to a report frame \p f.
 *
 * The function does not call \ref rd_free_node_dbe().  If the \p node
 * argument is fetched with \ref rd_get_node_dbe(), it is the
 * responsibility of the caller to free it.
 *
 * \param node Pointer to the node database entry.
 * \param f Pointer to a frame buffer.
 * \return Length of the report.
 */
static int BuildNodeInfoCachedReport(rd_node_database_entry_t *node,
                                     ZW_NODE_INFO_CACHED_REPORT_1BYTE_FRAME* f);

/********************************************************************************************/
/**
 * Sub-state flag for the Network Management State machine.
 *
 * NMS is processing an S2 addition (Smart-Start or simple S2).
 * \ingroup NW_CMD_handler
 */
#define NMS_FLAG_S2_ADD 1
/**
 * Sub-state flag for the Network Management State machine.
 *
 * NMS is processing a proxy inclusion or proxy replace.
 * \ingroup NW_CMD_handler
 */
#define NMS_FLAG_PROXY_INCLUSION 2
/**
 * Sub-state flag for the Network Management State machine.
 *
 * The ZW_LEARN_MODE_RETURN_INTERVIEW_STATUS was set on the
 * #LEARN_MODE_SET command, i.e., #LEARN_MODE_INTERVIEW_COMPLETED is
 * requested.
 * \ingroup NW_CMD_handler
 * \see Learn Mode Set Command (Network Management Basic Node Command
 * Class, version 2 and up)
 */
#define NMS_FLAG_LEARNMODE_NEW 4
/**
 * Sub-state flag for the Network Management State machine.
 *
 * The ZW_SET_LEARN_MODE_NWI was set on the #LEARN_MODE_SET command.
 *
 * \see Learn Mode Set Command (Network Management Basic Node Command
 * Class).
 * \ingroup NW_CMD_handler
 */
#define NMS_FLAG_LEARNMODE_NWI 8
/**
 * Sub-state flag for the Network Management State machine.
 *
 * The ZW_SET_LEARN_MODE_NWE was set on the #LEARN_MODE_SET command.
 *
 * \see Learn Mode Set Command (Network Management Basic Node Command
 * Class).
 * \ingroup NW_CMD_handler
 */
#define NMS_FLAG_LEARNMODE_NWE 0x10
/**
 * Sub-state flag for the Network Management State machine.
 *
 * After LEARN_MODE_DONE, NMS has determined that it is neither being
 * included nor excluded, so it must be processing a controller
 * replication (or controller shift).
 *
 * \ingroup NW_CMD_handler
 */
#define NMS_FLAG_CONTROLLER_REPLICATION 0x20
/**
 * Sub-state flag for the Network Management State machine.
 *
 * NMS is processing a Smart-Start addition.
 *
 * \ingroup NW_CMD_handler
 */
#define NMS_FLAG_SMART_START_INCLUSION 0x40
/**
 * Sub-state flag for the Network Management State machine.
 *
 * Inclusion and S2 inclusion have succeeded, so NMS should include
 * the DSK when sending NODE_ADD_STATUS.
 *
 * \ingroup NW_CMD_handler
 */
#define NMS_FLAG_REPORT_DSK 0x80
/**
 * Sub-state flag for the Network Management State machine.
 *
 * \ingroup NW_CMD_handler
 */
#define NMS_FLAG_CSA_INCLUSION 0x100

/**
 * Control structure for the Network Management State machine (NMS).
 * \ingroup NW_CMD_handler
 */
struct NetworkManagementState
{
  BYTE class;
  BYTE cmd;
  BYTE seq;

  zwave_connection_t conn;

  BYTE addRemoveNodeTimerHandle;
  BYTE networkManagementTimer;
  BYTE txOptions;
  uint16_t waiting_for_ipv4_addr;
  /** The node nms is currently working on:
   * - The node being added in add node.
   * - The node including the zipgateway in learn mode.
   * - The node being probed to reply to a NodeInfoCachedGet.
   * - etc. */
  nodeid_t tmp_node;
  /*This buffer is global, because the node info is only present in the */
  nm_state_t state;
   /** Sub-state flags on the current state. */
  int flags;
  uint16_t buf_len;
  uint8_t count;
  uint8_t dsk_valid; /**< Is the dsk in just_included_dsk valid for this inclusion.
                        Since 0 is valid, this needs a separate flag. */
  uint8_t granted_keys; /**< The keys we have granted */
  union { // This union is just to increase the size of buf
    ZW_APPLICATION_TX_BUFFER buf;
    uint8_t raw_buf[256];
  };
  struct ctimer timer;
  /* Used for testing when middleware has finished probing a newly included smart start node */
  BYTE just_included_dsk[16];
  uint32_t inclusion_flags;
};

/* This is not part of nms because it must survive ResetState() */
static nodeid_t nm_newly_included_ss_nodeid;

/** Collect the events that are prerequisites for NMS: DHCP done,
 * bridge ready, and probe done.  When all are in, trigger
 * nm_send_reply() (which will set up ResetState() as callback) and
 * cancel the networkManagementTimer.
 */
static BYTE networkUpdateStatusFlags;

int network_management_init_done = 0;
int delay_neighbor_update = 0;
static void RequestNodeNeighborUpdat_callback_after_s2_inclusion(BYTE status);
/* This is a lock which prevents reactivation of Smart Start inclusion
 * until middleware_probe_timeout() has triggered */
int waiting_for_middleware_probe = FALSE;

struct NetworkManagementState nms =
{ 0 };

/* Ctimer used to abort s2 incl asynchronously to avoid a buffer overwrite in libs2. */
static struct ctimer cancel_timer;

/* Smart start middleware probe timer */
static struct ctimer ss_timer;

/**
 * Integer log2
 */
static unsigned int
ilog2(int x)
{
  int i = 16;
  do
  {
    i--;
    if ((1 << i) & x)
      return i;
  }
  while (i);
  return 0;
}

static clock_time_t
ageToTime(BYTE age)
{
  return (1 << (age & 0xf)) * 60;
}



// Ring Patch
int should_skip_flirs_nodes_be_used()
{
 /* ADD_NODE_OPTION_SFLND is only available in 7.19.0 or above.
  * Use it when the expected inclusion time will be more than 60 seconds.
  */
 DBG_PRINTF("Protocol version %d.%d\n", zw_protocol_version2.protocolVersionMajor, zw_protocol_version2.protocolVersionMinor);
 return (zw_protocol_version2.protocolVersionMajor > 7 ||
         (zw_protocol_version2.protocolVersionMajor == 7 &&
          zw_protocol_version2.protocolVersionMinor >= 19)) &&
         rd_calculate_s2_inclusion() > 60000;
}



static uint8_t
is_cc_in_nif(uint8_t* nif, uint8_t nif_len, uint8_t cc)
{
  int i;
  for (i = 0; i < nif_len; i++)
  {
    if (nif[i] == cc)
    {
      return 1;
    }
  }
  return 0;
}

static void inclusion_controller_complete(int status) {
  nm_fsm_post_event(NM_EV_PROXY_COMPLETE,&status);
}

static int set_conn_to_unsol1(zwave_connection_t*  c)
{
  if(uip_is_addr_unspecified(&cfg.unsolicited_dest)) {
    return 0; 
  } else {
    uip_ipaddr_copy(&c->lipaddr, &cfg.lan_addr);
    uip_ipaddr_copy(&c->ripaddr, &cfg.unsolicited_dest);
    c->rport = UIP_HTONS(cfg.unsolicited_port);

#ifdef DISABLE_DTLS
    c->lport = UIP_HTONS(ZWAVE_PORT);
#else
    c->lport = (c->rport == UIP_HTONS(ZWAVE_PORT)) ? UIP_HTONS(ZWAVE_PORT): UIP_HTONS(DTLS_PORT);
#endif
    return 1;
 }
}
static int set_conn_to_unsol2(zwave_connection_t*  c)
{
  if(uip_is_addr_unspecified(&cfg.unsolicited_dest2)) {
    return 0; 
  } else {
    uip_ipaddr_copy(&c->lipaddr, &cfg.lan_addr);
    uip_ipaddr_copy(&c->ripaddr, &cfg.unsolicited_dest2);
    c->rport = UIP_HTONS(cfg.unsolicited_port2);

#ifdef DISABLE_DTLS
    c->lport = UIP_HTONS(ZWAVE_PORT);
#else
    c->lport = (c->rport == UIP_HTONS(ZWAVE_PORT)) ? UIP_HTONS(ZWAVE_PORT): UIP_HTONS(DTLS_PORT);
#endif
    return 1;
 }
}

static uint8_t set_unsolicited_as_nm_dest()
{
  nms.seq = random_rand() & 0xFF;
  if(set_conn_to_unsol1( &nms.conn )) {
    DBG_PRINTF("Setting unsolicited 1 as NM dest\n");
    return TRUE;
  } else
  {
    DBG_PRINTF("No unsolicited 1 configured for NM dest\n");
    return FALSE;
  }
}

/** Reset the fields in the global NM state to be ready for a node add. */
static void nm_prepare_for_node_add_status(bool lr)
{
  memset(&nms.buf, 0, sizeof(nms.buf));
  nms.cmd = NODE_ADD;
  nms.buf.ZW_NodeAddStatus1byteFrame.cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
  ZW_EXTENDED_NODE_ADD_STATUS_1BYTE_FRAME* f = (ZW_EXTENDED_NODE_ADD_STATUS_1BYTE_FRAME*) &nms.buf;
  if (lr) {
    f->cmd = EXTENDED_NODE_ADD_STATUS;
    nms.buf_len = f->nodeInfoLength + 6;
  } else {
    nms.buf.ZW_NodeAddStatus1byteFrame.cmd = NODE_ADD_STATUS;
    nms.buf_len = nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength + 6;
  }
  nms.buf.ZW_NodeAddStatus1byteFrame.seqNo = nms.seq;
  nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength = 1;
}

/**
 * This function is used to abort S2 inclusion asynchronously.
 * We cannot call s2_dsk_accept() immediately when we realize the DSKs dont match
 * because that causes a buffer overwrite in libs2. Instead we call asynchronously on a
 * ctimer.
 */
static void
cbfunc_cancel_inclusion(void* none)
{
  sec2_dsk_accept(0, 0, 2);
}

static void
nm_fsm_post_event(nm_event_t ev, void* event_data)
{
  uint32_t zero = 0;

  DBG_PRINTF( "nm_fsm_post_event event: %s state: %s\n",nm_event_name(ev),nm_state_name(nms.state));


  if((nms.state  ==NM_LEARN_MODE_STARTED) || (nms.state == NM_WAIT_FOR_SECURE_LEARN) ) {
    if(ev == NM_EV_S0_STARTED) {
      /* Make temporary NIF, used for inclusion */
      LOG_PRINTF("S0 inclusion has started. Setting net_scheme to S0\n");
      net_scheme = SECURITY_SCHEME_0;
      SetPreInclusionNIF(SECURITY_SCHEME_0);
      /*Make sure to clear the s2 keys.*/
      keystore_network_key_clear(KEY_CLASS_S2_UNAUTHENTICATED);
      keystore_network_key_clear(KEY_CLASS_S2_AUTHENTICATED);
      keystore_network_key_clear(KEY_CLASS_S2_ACCESS);
      /*Stop the S2 FSM */
      sec2_abort_join();
    }
  }

  switch (nms.state)
  {
  case NM_IDLE:
    if (ev == NM_EV_LEARN_SET) {
      ZW_LEARN_MODE_SET_FRAME* f = (ZW_LEARN_MODE_SET_FRAME*) event_data;

      if(f->mode == ZW_SET_LEARN_MODE_CLASSIC) {
        nms.flags = 0;
      } else if(f->mode == ZW_SET_LEARN_MODE_NWI) {
        nms.flags = NMS_FLAG_LEARNMODE_NWI;
        /*Note it is supposed to be MODE CLASSIC*/
      } else if(f->mode == ZW_SET_LEARN_MODE_NWE ) {
        nms.flags = NMS_FLAG_LEARNMODE_NWE;
      } else if(f->mode == ZW_SET_LEARN_MODE_DISABLE ) {
        ZW_LEARN_MODE_SET_STATUS_FRAME* f = (ZW_LEARN_MODE_SET_STATUS_FRAME*) &nms.buf;
        f->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC;
        f->cmd = LEARN_MODE_SET_STATUS;
        f->seqNo = nms.seq;
        f->status = LEARN_MODE_FAILED;
        f->newNodeId = 0;
        f->reserved = 0;
        nm_send_reply(f, sizeof(ZW_LEARN_MODE_SET_STATUS_FRAME));
        return;
      } else {
        WRN_PRINTF("Unknown learnmode\n");
        return;
      }

      /* This clears the DSK field in Learn Mode Set Status to make it more obvious that
       * it is not valid during exclusion or replication. Even though we know that all-zeros
       * is also a valid (but unlikely) DSK. */
      memset(&nms.buf, 0, sizeof(nms.buf));

      /* Before going to learn mode, Stop add node mode in case we are in Smart Start add mode */
      ZW_AddNodeToNetwork(ADD_NODE_STOP, NULL);

      /* Make temporary NIF, used for inclusion */
      SetPreInclusionNIF(NO_SCHEME);

      nms.state = NM_LEARN_MODE;

      if ((f->reserved & ZW_LEARN_MODE_RETURN_INTERVIEW_STATUS) && (nms.flags != NMS_FLAG_LEARNMODE_NWE))
      {
        nms.flags |= NMS_FLAG_LEARNMODE_NEW;
      }

      nms.count = 0;
      ZW_SetLearnMode(ZW_SET_LEARN_MODE_CLASSIC, LearnModeStatus);

      if (f->mode == ZW_SET_LEARN_MODE_CLASSIC)
      {
          /* Keep the timeout to 20 seconds if the learn mode is CLASSIC */
          ctimer_set(&nms.timer, CLOCK_SECOND*20, timeout, 0);
      }
      else
      {
          /* in case of NWI mode Keeping the timeout to 2 seconds come from recommendation in the
             document SDS11846. We keep the timeout same for both NWI and NWE modes*/
        /* This timeout has been extended from 2 seconds, because we changed too early and
         * broke direct range exclusion. */
          ctimer_set(&nms.timer, CLOCK_SECOND*6, timeout, 0);
      }

    }else if (ev == NM_EV_NODE_ADD || ev == NM_EV_NODE_ADD_S2)
    {
      ZW_AddNodeToNetwork(*((uint8_t*) event_data), AddNodeStatusUpdate);
      nms.state = NM_WAITING_FOR_ADD;

      nm_prepare_for_node_add_status(false);
      nms.dsk_valid = FALSE;

      if(ev == NM_EV_NODE_ADD_S2) {
        nms.flags = NMS_FLAG_S2_ADD;
      }
      ctimer_set(&nms.timer, ADD_REMOVE_TIMEOUT * 10, timeout, 0);
    } else if (ev == NM_EV_NODE_ADD_SMART_START)
    {
      uint8_t buf_smartstart_status[30];
      uint8_t i;
      struct node_add_smart_start_event_data *smart_start_ev_data = (struct node_add_smart_start_event_data*)event_data;
      set_unsolicited_as_nm_dest();
      struct provision *pvl_entry = provisioning_list_dev_get_homeid(smart_start_ev_data->smart_start_homeID);
      if (!pvl_entry)
      {
        WRN_PRINTF("Could not find provisioning list entry from homeid");
        return;
      }

      ZW_AddNodeToNetworkSmartStart(ADD_NODE_HOME_ID |
                                      smart_start_ev_data->inclusion_options,
                                    &pvl_entry->dsk[8],
                                    AddNodeStatusUpdate);
      nms.state = NM_WAITING_FOR_ADD;

      nm_prepare_for_node_add_status(pvl_entry->bootmode == 0x02);

      nms.flags = NMS_FLAG_S2_ADD | NMS_FLAG_SMART_START_INCLUSION;
      ctimer_set(&nms.timer, ADD_REMOVE_TIMEOUT * 10, timeout, 0);

      i = 0;
      buf_smartstart_status[i++] = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
      buf_smartstart_status[i++] = COMMAND_SMART_START_JOIN_STARTED_REPORT;
      buf_smartstart_status[i++] = nms.seq;
      buf_smartstart_status[i++] = pvl_entry->dsk_len & PROVISIONING_LIST_DSK_LENGTH_MASK;
      memcpy(&buf_smartstart_status[i], pvl_entry->dsk, pvl_entry->dsk_len);
      /* We store the dsk, but we only consider it valid after S2
       * inclusion has succeeded. */
      nms.dsk_valid = FALSE;
      memcpy(&nms.just_included_dsk, pvl_entry->dsk, sizeof(nms.just_included_dsk) );

      i += pvl_entry->dsk_len;
      send_to_both_unsoc_dest(buf_smartstart_status, i, NULL);

    } else if (ev == NM_EV_NODE_ADD_SMART_START_REJECT)
    {
      ResetState(0,0,0);
    } else if (ev == NM_EV_REPLACE_FAILED_START || ev == NM_EV_REPLACE_FAILED_START_S2 || ev == NM_EV_REPLACE_FAILED_STOP)
    {
       ZW_FAILED_NODE_REPLACE_FRAME* f = (ZW_FAILED_NODE_REPLACE_FRAME*) event_data;
       ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX* reply = (ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX*) &nms.buf;
       nms.cmd = FAILED_NODE_REPLACE;
       nms.tmp_node = f->nodeId;
       nms.state = NM_REPLACE_FAILED_REQ;

       reply->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
       reply->cmd = FAILED_NODE_REPLACE_STATUS;
       reply->seqNo = nms.seq;
       reply->nodeId = nms.tmp_node;
       reply->status = ZW_FAILED_NODE_REPLACE_FAILED;
       reply->kexFailType=0x00;
       reply->grantedKeys =0x00;
       nms.buf_len = sizeof(ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX);

       if(ev == NM_EV_REPLACE_FAILED_START_S2) {
         nms.flags = NMS_FLAG_S2_ADD;
       }
       if(ev == NM_EV_REPLACE_FAILED_STOP) {
         goto send_reply;
       }
        
       ZW_AddNodeToNetwork(ADD_NODE_STOP, NULL);

       DBG_PRINTF("Replace failed, node %i \n",f->nodeId);
       if (ZW_ReplaceFailedNode(f->nodeId, f->txOptions != TRANSMIT_OPTION_LOW_POWER,
           ReplaceFailedNodeStatus) == ZW_FAILED_NODE_REMOVE_STARTED)
       {
         ctimer_set(&nms.timer, ADD_REMOVE_TIMEOUT * 10, timeout, 0);
       } else {
         WRN_PRINTF("replace failed not started\n");
         goto send_reply;
       }
    } else if (ev == NM_EV_REQUEST_NODE_LIST) {
      /*I'm the SUC/SIS or i don't know the SUC/SIS*/
      uint8_t buf[sizeof(ZW_FAILED_NODE_LIST_REPORT_1BYTE_V4_FRAME) + sizeof(nodemask_t)];  
      uint16_t len = BuildNodeListFrame((uint8_t *)buf, nms.seq);
      nm_send_reply(&buf, len);
    } else if (ev == NM_EV_REQUEST_FAILED_NODE_LIST) {
      uint8_t buf[sizeof(ZW_FAILED_NODE_LIST_REPORT_1BYTE_V4_FRAME) + sizeof(nodemask_t)];  
      uint16_t len = BuildFailedNodeListFrame((uint8_t *)buf, nms.seq);
      nm_send_reply(&buf, len);
    } else if (ev == NM_NODE_ADD_STOP) {
      DBG_PRINTF("Event  NM_NODE_ADD_STOP in NM_IDLE state\n");
      memset(&nms.buf, 0, sizeof(nms.buf.ZW_NodeAddStatus1byteFrame));
      nms.buf.ZW_NodeAddStatus1byteFrame.cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
      nms.buf.ZW_NodeAddStatus1byteFrame.cmd = NODE_ADD_STATUS;
      nms.buf.ZW_NodeAddStatus1byteFrame.seqNo = nms.seq;
      nms.buf.ZW_NodeAddStatus1byteFrame.status = ADD_NODE_STATUS_FAILED;
      nms.buf_len = sizeof(nms.buf.ZW_NodeAddStatus1byteFrame) - 1;
      goto send_reply;
    } else if( ev == NM_EV_START_PROXY_INCLUSION || ev == NM_EV_START_PROXY_REPLACE) {
      nms.tmp_node = *((uint16_t*)event_data);
      nms.cmd = (ev == NM_EV_START_PROXY_INCLUSION) ? NODE_ADD : FAILED_NODE_REPLACE;

      ZW_RequestNodeInfo(nms.tmp_node,0);
      nms.state = NM_PROXY_INCLUSION_WAIT_NIF;
      ctimer_set(&nms.timer,5000,timeout,0);

      /* Send inclusion request to unsolicited destination */
      set_unsolicited_as_nm_dest();
    }
    break;
  case NM_REPLACE_FAILED_REQ:
    if(ev ==NM_EV_TIMEOUT || ev == NM_EV_REPLACE_FAILED_STOP || ev == NM_EV_REPLACE_FAILED_FAIL) {
      ZW_AddNodeToNetwork(ADD_NODE_STOP,0);
      goto send_reply;
    } if(ev == NM_EV_REPLACE_FAILED_DONE) {
      int common_flags = 0;
      nms.state = NM_WAIT_FOR_SECURE_ADD;

      /*Cache security flags*/
      if(nms.flags & NMS_FLAG_PROXY_INCLUSION){
        LEARN_INFO *inf = (LEARN_INFO *) event_data;
        if(NULL != inf)
        {
          uint8_t* nif = inf->pCmd+3;
          if(is_cc_in_nif(nif,inf->bLen-3,COMMAND_CLASS_SECURITY)) common_flags |=NODE_FLAG_SECURITY0;
          if(is_cc_in_nif(nif,inf->bLen-3,COMMAND_CLASS_SECURITY_2)) common_flags |=(NODE_FLAG_SECURITY2_ACCESS |NODE_FLAG_SECURITY2_UNAUTHENTICATED | NODE_FLAG_SECURITY2_AUTHENTICATED);
          // TODO: Add LR keys here
        }
      } else {
        common_flags = GetCacheEntryFlag(nms.tmp_node);
      }

      ApplicationControllerUpdate(UPDATE_STATE_DELETE_DONE, nms.tmp_node, 0, 0, NULL);

      rd_probe_lock(TRUE);

      nodeid_t suc_node = ZW_GetSUCNodeID();

      if(suc_node != MyNodeID &&  SupportsCmdClass(suc_node, COMMAND_CLASS_INCLUSION_CONTROLLER)) {
         rd_register_new_node(nms.tmp_node, RD_NODE_FLAG_JUST_ADDED);
         ctimer_set(&nms.timer,CLOCK_SECOND*2,timeout,0);
         nms.state = NM_PREPARE_SUC_INCLISION;
         return;
      }

      rd_register_new_node(nms.tmp_node, RD_NODE_FLAG_JUST_ADDED | RD_NODE_FLAG_ADDED_BY_ME);

#if 0
      /* This is to keep the probe from doing a secure commands supported get */
      SetCacheEntryFlagMasked(nms.tmp_node,
      NODE_FLAG_INFO_ONLY | NODE_FLAG_SECURITY0 | NODE_FLAG_KNOWN_BAD,
      NODE_FLAG_INFO_ONLY | NODE_FLAG_SECURITY0 | NODE_FLAG_KNOWN_BAD);
      rd_probe_lock(FALSE);
#endif

      if( (nms.flags & NMS_FLAG_S2_ADD) &&
          (common_flags & (NODE_FLAG_SECURITY2_ACCESS |NODE_FLAG_SECURITY2_UNAUTHENTICATED | NODE_FLAG_SECURITY2_AUTHENTICATED)))
      {
        // TODO: Add LR keys here
        sec2_start_add_node(nms.tmp_node, SecureInclusionDone);
        return;
      }


      if(common_flags & NODE_FLAG_SECURITY0) {
        if(nms.flags & NMS_FLAG_PROXY_INCLUSION ) {
          inclusion_controller_you_do_it(SecureInclusionDone);
          return;
        } else if (!(nms.flags & NMS_FLAG_SMART_START_INCLUSION)) { /* SmartStart inclusions must never be downgraded to S0 */
          security_add_begin(nms.tmp_node, nms.txOptions,
              isNodeController(nms.tmp_node), SecureInclusionDone);
          return;
        }
      }

      /*This is a non secure node or the node has already been included securely*/
      ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX* reply = (ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX*) &nms.buf;
      reply->status = ZW_FAILED_NODE_REPLACE_DONE;

      nm_fsm_post_event(NM_EV_SECURITY_DONE, &zero);
    }
    break;
  case NM_WAITING_FOR_ADD:
    if (ev == NM_NODE_ADD_STOP || ev == NM_EV_TIMEOUT)
    {
      nms.buf.ZW_NodeAddStatus1byteFrame.status = ADD_NODE_STATUS_FAILED;
      ZW_AddNodeToNetwork(ADD_NODE_STOP, AddNodeStatusUpdate);
      nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength = 1;
      nms.buf_len = nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength + 6;
      goto send_reply;
    }
    else if (ev == NM_EV_ADD_NODE_FOUND)
    {
      nms.state = NM_NODE_FOUND;
      ctimer_set(&nms.timer,CLOCK_SECOND*60,timeout,0);
    }
    break;
  case NM_NODE_FOUND:
    if (ev == NM_EV_ADD_CONTROLLER || ev == NM_EV_ADD_END_NODE)
    {
      LEARN_INFO *inf = (LEARN_INFO *) event_data;
      clock_time_t tout;
      if (ev == NM_EV_ADD_CONTROLLER) {
        tout = rd_calculate_inclusion_timeout(TRUE);
      } else if (ev == NM_EV_ADD_END_NODE) {
        tout = rd_calculate_inclusion_timeout(FALSE);
      }

      if (inf->bLen && (inf->bSource!=0)  && (!is_virtual_node(inf->bSource)))
      {
        ctimer_set(&nms.timer,tout,timeout,0);
        nms.tmp_node = inf->bSource;

        ZW_EXTENDED_NODE_ADD_STATUS_1BYTE_FRAME* f = (ZW_EXTENDED_NODE_ADD_STATUS_1BYTE_FRAME*) &nms.buf;
        if ((nms.flags & NMS_FLAG_SMART_START_INCLUSION) &&
            (is_lr_node(nms.tmp_node))) {
          f->newNodeIdMSB = inf->bSource >> 8;
          f->newNodeIdLSB = inf->bSource & 0xFF;
        } else {
          nms.buf.ZW_NodeAddStatus1byteFrame.newNodeId = inf->bSource;
        }
        nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength = inf->bLen + 3;
        memcpy(&(nms.buf.ZW_NodeAddStatus1byteFrame.basicDeviceClass), inf->pCmd, inf->bLen);
        nms.state = NM_WAIT_FOR_PROTOCOL;
      } else {
        if (is_virtual_node(inf->bSource)) {
          ERR_PRINTF("Node id included was a virtual node id: %d\n", inf->bSource);
        }
        nm_fsm_post_event(NM_EV_ADD_FAILED, 0);
      }
      if (nms.flags & NMS_FLAG_SMART_START_INCLUSION)
      {
        nm_newly_included_ss_nodeid = nms.tmp_node;
      }
    } else if(ev == NM_EV_ADD_FAILED || ev == NM_EV_TIMEOUT ){
      nms.buf.ZW_NodeAddStatus1byteFrame.status = ADD_NODE_STATUS_FAILED;

      /* Add node failed - Application should indicate this to user */
      ZW_AddNodeToNetwork(ADD_NODE_STOP_FAILED, NULL);

      rd_probe_lock(FALSE); //Unlock the probe machine
      nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength = 1;
      nms.buf_len = nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength + 6;
      goto send_reply;
    }
    break;
  case NM_WAIT_FOR_PROTOCOL:
    if (ev == NM_EV_ADD_PROTOCOL_DONE) {
      ZW_AddNodeToNetwork(ADD_NODE_STOP, AddNodeStatusUpdate);
    } else if (ev == NM_EV_ADD_NODE_STATUS_SFLND_DONE){
      DBG_PRINTF("The module has skipped doing Neighbor Discovery of FLIRS nodes\n");
      delay_neighbor_update = 1;
    } else if ((ev == NM_EV_ADD_NODE_STATUS_DONE)) {
      NODEINFO ni;

      /* It is recommended to stop the process again here */
      ZW_AddNodeToNetwork(ADD_NODE_STOP, NULL);
      /* Reset the timer started in NM_NODE_FOUND state after calculating 
       * timeout with rd_calculate_inclusion_timeout() 
       * Reset to value more than S2 inclusion TAI1 to allow user to change
       * granted keys */
      ctimer_set(&nms.timer,250 * CLOCK_SECOND,timeout,0);

      /* Get the Capabilities and Security fields. */
      ZW_GetNodeProtocolInfo(nms.tmp_node, &ni);
      nms.buf.ZW_NodeAddStatus1byteFrame.properties1 = ni.capability;
      nms.buf.ZW_NodeAddStatus1byteFrame.properties2 = ni.security;

      nms.state = NM_WAIT_FOR_SECURE_ADD;

      if((rd_node_exists(nms.tmp_node)) && !(nms.flags & NMS_FLAG_PROXY_INCLUSION ))
      {
        uint32_t flags = GetCacheEntryFlag(nms.tmp_node);
        WRN_PRINTF("This node has already been included\n");
        /*This node has already been included*/
        /* TODO: Handle DSK in the RD if it does not match
         * just_included. Do NOT use just_included, since it has not
         * been authenticated. Either invalidate RD dsk or keep it? */
        nms.dsk_valid = FALSE;
        nm_fsm_post_event(NM_EV_SECURITY_DONE, &flags);

        /* In NM_WAIT_FOR_SECURE_ADD we go straight to
         * NM_WAIT_FOR_PROBE_AFTER_ADD, so here we set up the trigger to
         * continue from that state. */
        rd_node_database_entry_t *nde = rd_node_get_raw(nms.tmp_node);
        /* Do we want to check things like probe state or deleted flag here? */
        process_post(&zip_process, ZIP_EVENT_NODE_PROBED, (void*) nde);
        return;
      } else {
        nodeid_t suc_node = ZW_GetSUCNodeID();

        rd_probe_lock(TRUE);

        if(suc_node != MyNodeID &&  SupportsCmdClass(suc_node, COMMAND_CLASS_INCLUSION_CONTROLLER)) {
          rd_register_new_node(nms.tmp_node, RD_NODE_FLAG_JUST_ADDED);
          ApplicationControllerUpdate(UPDATE_STATE_NEW_ID_ASSIGNED, nms.tmp_node, 0, 0, NULL);
          ctimer_set(&nms.timer,CLOCK_SECOND*2,timeout,0);
          nms.state = NM_PREPARE_SUC_INCLISION;
          return;
        }

        rd_register_new_node(nms.tmp_node, RD_NODE_FLAG_JUST_ADDED | RD_NODE_FLAG_ADDED_BY_ME);
        ApplicationControllerUpdate(UPDATE_STATE_NEW_ID_ASSIGNED, nms.tmp_node, 0, 0, NULL);
        /*Security 2 inclusion if this nodes supports a S2 key, and we are asked to try an s2 inclusion*/
        if ((nms.flags & NMS_FLAG_S2_ADD) &&
            (GetCacheEntryFlag(MyNodeID) &
            (NODE_FLAG_SECURITY2_ACCESS |NODE_FLAG_SECURITY2_UNAUTHENTICATED | NODE_FLAG_SECURITY2_AUTHENTICATED))
            )
        {
            // TODO:
            /* For Smart Start inclusions we start S2 bootstrapping regardless of node NIF.
             * This prevents downgrade attacks that modify the NIF in the air. */
            if (is_cc_in_nif(&nms.buf.ZW_NodeAddStatus1byteFrame.commandClass1,
              nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength - 3, COMMAND_CLASS_SECURITY_2)
              || nms.flags & NMS_FLAG_SMART_START_INCLUSION)
          {
            sec2_start_add_node(nms.tmp_node, SecureInclusionDone);
            return;
          }
        }

        if (GetCacheEntryFlag(MyNodeID) & (NODE_FLAG_SECURITY0))
        {
          /*Security 0 inclusion*/
          if (is_cc_in_nif(&nms.buf.ZW_NodeAddStatus1byteFrame.commandClass1,
              nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength - 3, COMMAND_CLASS_SECURITY))
          {
            if(nms.flags & NMS_FLAG_PROXY_INCLUSION ) {
              inclusion_controller_you_do_it(SecureInclusionDone);
            } else if (!(nms.flags & NMS_FLAG_SMART_START_INCLUSION)) { /* SmartStart inclusions must never be downgraded to S0 */
              security_add_begin(nms.tmp_node, nms.txOptions,
                  isNodeController(nms.tmp_node), SecureInclusionDone);
            }
            return;
          }
        }
        /* This is a non secure node or a non secure GW */
        nm_fsm_post_event(NM_EV_SECURITY_DONE, &zero);
      }
    }
    else if (ev == NM_EV_TIMEOUT || ev == NM_EV_ADD_FAILED || ev == NM_EV_ADD_NOT_PRIMARY)
    {
      nms.buf.ZW_NodeAddStatus1byteFrame.status = ADD_NODE_STATUS_FAILED;

      /* Add node failed - Application should indicate this to user */
      ZW_AddNodeToNetwork(ADD_NODE_STOP_FAILED, NULL);

      rd_probe_lock(FALSE); //Unlock the probe machine
      nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength = 1;
      nms.buf_len = nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength + 6;
      goto send_reply;
    }
    break;
  case NM_PREPARE_SUC_INCLISION:
    if(ev == NM_EV_TIMEOUT) {
      nms.state = NM_WIAT_FOR_SUC_INCLUSION;
      request_inclusion_controller_handover(
          nms.tmp_node,
          (nms.cmd == FAILED_NODE_REPLACE),
          &inclusion_controller_complete );
    }
    break;
  case NM_WAIT_FOR_NEIGHBOR_UPDATE_AFTER_SECURE_ADD:
    if ((ev == NM_EV_NEIGHBOR_UPDATE_AFTER_SECURE_ADD_DONE) || (ev == NM_EV_TIMEOUT)) {
       DBG_PRINTF("Delayed node Neighbor Discovery done or timed out after S2 inclusion.\n");
       nms.state = NM_WAIT_FOR_PROBE_AFTER_ADD;
       rd_probe_lock(FALSE);
    }
    break;
  case NM_WAIT_FOR_SECURE_ADD:
    if (ev == NM_EV_SECURITY_DONE || ev == NM_NODE_ADD_STOP)
    {
      uint32_t inclusion_flags;

      ctimer_stop(&nms.timer);
      if(ev == NM_NODE_ADD_STOP) {
        sec2_key_grant(0,0,0);
        sec2_dsk_accept(0,0,2);
        inclusion_flags = NODE_FLAG_KNOWN_BAD;
      } else {
        inclusion_flags = (*(uint32_t*) event_data);
        nms.inclusion_flags = inclusion_flags;
      }

      /*If status has not yet already been set use the result of the secure add*/
      if(nms.cmd == NODE_ADD) {
      nms.buf.ZW_NodeAddStatus1byteFrame.status =
          inclusion_flags & NODE_FLAG_KNOWN_BAD ? ADD_NODE_STATUS_SECURITY_FAILED : ADD_NODE_STATUS_DONE;
      } else {
        ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX* reply = (ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX*) &nms.buf;

        reply->status =
            inclusion_flags & NODE_FLAG_KNOWN_BAD ? ADD_NODE_STATUS_SECURITY_FAILED : ZW_FAILED_NODE_REPLACE_DONE;
        reply->grantedKeys = sec2_gw_node_flags2keystore_flags(inclusion_flags & 0xFF);
        reply->kexFailType = (inclusion_flags >> 16) & 0xFF; //TODO
      }
      SetCacheEntryFlagMasked(nms.tmp_node, inclusion_flags & 0xFF, NODE_FLAGS_SECURITY);

      /* Create a new ECDH pair for next inclusion.*/
      sec2_create_new_dynamic_ecdh_key();

      /* If this is a failed smart start inclusion, dont do probing. The node will self destruct
       * soon anyway. */
      if ((nms.flags & NMS_FLAG_SMART_START_INCLUSION)
           && (inclusion_flags & NODE_FLAG_KNOWN_BAD))
      {
        nms.state = NM_WAIT_FOR_SELF_DESTRUCT;
        ctimer_set(&nms.timer, SMART_START_SELF_DESTRUCT_TIMEOUT, timeout, 0);
        break;
      }

      if (nms.dsk_valid == TRUE) {
        /* If the DSK is S2 confirmed, store it in the RD.  Smart
         * start devices must be S2 by now, S2 devices need an extra
         * check on inclusion_flags.  S0 and non-secure devices should
         * not have a DSK.  */
        /* if nms.flags have  NMS_FLAG_S2_ADD, that covers both smartstart and original s2 */
        if ((nms.flags & NMS_FLAG_SMART_START_INCLUSION)
            || ((inclusion_flags & NODE_FLAGS_SECURITY2)
                && (!(inclusion_flags & NODE_FLAG_KNOWN_BAD)))) {
           /* Link the PVL entry to the RD entry: Add the DSK to the
            * RD and copy name and location tlvs if they exist */
           rd_node_add_dsk(nms.tmp_node, 16, nms.just_included_dsk);
        }
      }
      /* If this secure inclusion was successful and S2, remember we should
       * report the DSK when sending NODE_ADD_STATUS later */
      if ((inclusion_flags & NODE_FLAGS_SECURITY2)
            && !(inclusion_flags & NODE_FLAG_KNOWN_BAD))
      {
        nms.flags |= NMS_FLAG_REPORT_DSK;
      }

      if (nms.flags & NMS_FLAG_SMART_START_INCLUSION)
      {
        /* We are also setting this flag in state NM_WAIT_DHCP where we also start the
         * timer responsible for clearing the flag eventually.
         * We have to set the flag this early because the probing process will throw
         * ZIP_EVENT_ALL_NODES_PROBED calling _init_if_pending(), which we need to block.
         */
        waiting_for_middleware_probe = TRUE;
      }

      if (delay_neighbor_update) {
        DBG_PRINTF("Starting the delayed Neighbor Disovery for node:%d now. FLIRS nodes will also be discovered\n", nms.tmp_node);
        nms.state = NM_WAIT_FOR_NEIGHBOR_UPDATE_AFTER_SECURE_ADD;
        clock_time_t tout = rd_calculate_inclusion_timeout(TRUE);
        ctimer_set(&nms.timer, tout, timeout, 0);
        ZW_RequestNodeNeighborUpdate(nms.tmp_node, RequestNodeNeighborUpdat_callback_after_s2_inclusion);
        delay_neighbor_update = 0;
      } else { 
        /* Now we either have smart start with S2 success or plain S2 or node already exists. */
        nms.state = NM_WAIT_FOR_PROBE_AFTER_ADD;
          /* Re-interview this node as it now has secure classes and its nif might have changed.*/
          /* When the interview is completed, ZIP_Router will trigger NMS
           * again by calling NetworkManagement_node_probed(). */
          rd_probe_lock(FALSE);
      }
    } else if (ev == NM_EV_ADD_SECURITY_REQ_KEYS) {
       /* We dont leave the NM_WAIT_FOR_SECURE_ADD here because
        * we are still just proxying for the Security FSM */
      s2_node_inclusion_request_t *req = (s2_node_inclusion_request_t*) (event_data);

      if (nms.flags & NMS_FLAG_SMART_START_INCLUSION)
      {
        struct provision* pe = provisioning_list_dev_get(16, nms.just_included_dsk);

        uint8_t keys = req->security_keys;
        DBG_PRINTF("Security keys requested by the node: %x",
                   req->security_keys);
        if (pe) {
        /* Check if adv_join tlv exists in pvl.  If so, extract
         * allowed keys from it and grant only intersection of pvl granted keys
         * and requested keys*/
          struct pvs_tlv *tlv = provisioning_list_tlv_get(pe, PVS_TLV_TYPE_ADV_JOIN);
          if(tlv && (tlv->length==1)) {
              keys = (*tlv->value & req->security_keys);
              DBG_PRINTF("Security keys Granted in provisioning list: %x\n",
                         *tlv->value);
          }
        }
        nms.granted_keys = keys;

        DBG_PRINTF("Secuirty keys Granted to the node: %x\n", keys);
        sec2_key_grant(NODE_ADD_KEYS_SET_EX_ACCEPT_BIT,keys, 0);
      }
      else
      {
        ZW_NODE_ADD_KEYS_REPORT_FRAME_EX f;

        f.cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
        f.cmd = NODE_ADD_KEYS_REPORT;
        f.seqNo = nms.seq;
        f.requested_keys = req->security_keys;
        f.request_csa = req->csa;

        ZW_SendDataZIP(&nms.conn,&f,sizeof(f),0);
      }
    } else if (ev == NM_EV_ADD_SECURITY_KEY_CHALLENGE) {
      /* Reset the timer to cover TAI2 to allow USER to verify/Input the DSK */  
      ctimer_set(&nms.timer, 310 * CLOCK_SECOND,timeout,0);
      ZW_NODE_ADD_DSK_REPORT_FRAME_EX f;
      s2_node_inclusion_challenge_t *challenge_evt = (s2_node_inclusion_challenge_t*) (event_data);
      /* Consult provisioning list and fill in missing Input DSK digits*/
      /* We dont filter by bootstrap mode, since SmartStart devices should also get assist
       * if they are being included normally. */
      struct provision* w = provisioning_list_dev_match_challenge(challenge_evt->length, challenge_evt->public_key);
      if (w)
      {
        challenge_evt->public_key[0] = w->dsk[0];
        challenge_evt->public_key[1] = w->dsk[1];
        memcpy(nms.just_included_dsk, challenge_evt->public_key, sizeof(nms.just_included_dsk));
        nms.dsk_valid = TRUE;
        sec2_dsk_accept(1, challenge_evt->public_key,2);
      } else if (nms.granted_keys == KEY_CLASS_S2_UNAUTHENTICATED) {
        // Unauthenticated MAY use a dynamic DSK, allow this.
        memcpy(nms.just_included_dsk, challenge_evt->public_key, sizeof(nms.just_included_dsk));
        nms.dsk_valid = TRUE;
        sec2_dsk_accept(1, challenge_evt->public_key,2);
      }
      else
      {
        if (nms.flags & NMS_FLAG_SMART_START_INCLUSION)
        {
          WRN_PRINTF("Smart Start: Input DSK not found in provisioning list\n");
          nms.dsk_valid = FALSE;
          /* Abort S2 inclusion since we do not have a matching PVL entry after all
           * We do this by accepting with an incomplete */
          ctimer_set(&cancel_timer, 1, cbfunc_cancel_inclusion, 0);
        }
        else
        {
          f.cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
          f.cmd = NODE_ADD_DSK_REPORT;
          f.seqNo = nms.seq;

          /* Input DSK length is 2 for ACCESS and AUTHENTICATED keys when we are doing non-csa inclusion, zero otherwise */
          if ((challenge_evt->granted_keys &
              (KEY_CLASS_S2_ACCESS | KEY_CLASS_S2_AUTHENTICATED)) && ((nms.flags & NMS_FLAG_CSA_INCLUSION) == 0) )
          {
            f.reserved_dsk_len = 2;
          } else {
            f.reserved_dsk_len = 0;
          }
          memcpy(f.dsk, challenge_evt->public_key,16);
          memcpy(nms.just_included_dsk, challenge_evt->public_key, sizeof(nms.just_included_dsk));

          ZW_SendDataZIP(&nms.conn,&f,sizeof(f),0);
        }
      }
    } else if(ev == NM_EV_ADD_SECURITY_KEYS_SET) {
      ZW_NODE_ADD_KEYS_SET_FRAME_EX* f = (ZW_NODE_ADD_KEYS_SET_FRAME_EX*) event_data;

      if(f->reserved_accept & NODE_ADD_KEYS_SET_EX_CSA_BIT) nms.flags |=NMS_FLAG_CSA_INCLUSION;

      sec2_key_grant(f->reserved_accept & NODE_ADD_KEYS_SET_EX_ACCEPT_BIT,f->granted_keys, (f->reserved_accept & NODE_ADD_KEYS_SET_EX_CSA_BIT) > 0 );
    } else if(ev == NM_EV_ADD_SECURITY_DSK_SET) {
      ZW_NODE_ADD_DSK_SET_FRAME_EX* f = (ZW_NODE_ADD_DSK_SET_FRAME_EX*) event_data;
      uint8_t dsk_len = f->accet_reserved_dsk_len & NODE_ADD_DSK_SET_DSK_LEN_MASK;

      DBG_PRINTF("DSK accept bit %u, dsk len %u\n",
          f->accet_reserved_dsk_len & NODE_ADD_DSK_SET_EX_ACCEPT_BIT,
          f->accet_reserved_dsk_len & NODE_ADD_DSK_SET_DSK_LEN_MASK);

      if(dsk_len <= 16) {
        sec2_dsk_accept((f->accet_reserved_dsk_len & NODE_ADD_DSK_SET_EX_ACCEPT_BIT)>0, f->dsk, dsk_len );
        nms.dsk_valid = TRUE;
        memcpy(nms.just_included_dsk, f->dsk, dsk_len);
      } else {
        sec2_dsk_accept(0, 0, 2 );
      }

    } else if (ev == NM_EV_TIMEOUT) {
      nm_fsm_post_event(NM_NODE_ADD_STOP, 0);
    }
    break;
   case NM_WAIT_FOR_PROBE_AFTER_ADD:
    if (ev == NM_EV_NODE_PROBE_DONE)
    {
      rd_node_database_entry_t* ndbe = (rd_node_database_entry_t*) event_data;
      if (ndbe->nodeid != nms.tmp_node) {
         break;
      }

      rd_ep_database_entry_t* ep = rd_ep_first(ndbe->nodeid);
      ZW_NODE_ADD_STATUS_1BYTE_FRAME* r = (ZW_NODE_ADD_STATUS_1BYTE_FRAME*) &nms.buf;
      uip_ipv4addr_t a;
      int len;

      if(nms.cmd == NODE_ADD) {
        if (ep->state == EP_STATE_PROBE_DONE)
        {
          /*
           * Here we send status back to LAN side. Options can be:
           * 1. Node Add Status for classic node (SmartStart or not)
           * 2. Extended Node Add Status for LR SmartStart node
           *
           * Both ZW frame format are the same until KEX_FAIL so
           * ZW_NODE_ADD_STATUS_1BYTE_FRAME can be used for both.
           */
          // r->properties1 and r->properties2 were filled in earlier.
          r->basicDeviceClass = ndbe->nodeType;
          r->genericDeviceClass = ep->endpoint_info[0];
          r->specificDeviceClass = ep->endpoint_info[1];
          size_t max;

          if ((nms.flags & NMS_FLAG_REPORT_DSK) && (nms.buf.ZW_NodeAddStatus1byteFrame.cmd == NODE_ADD_STATUS)) {
          /* Save space for DSK, dsk length, supported keys and reserved fields in node add status command */
             max = ((sizeof(nms.raw_buf) - offsetof(ZW_NODE_ADD_STATUS_1BYTE_FRAME,commandClass1))
                    -  (3 + sizeof(nms.just_included_dsk)));
          } else {
          /* save space  for zero dsk length, supported keys and reserved fields iin node add status command */
             max = (sizeof(nms.raw_buf) - offsetof(ZW_NODE_ADD_STATUS_1BYTE_FRAME,commandClass1)) - 3;
          }

          /* First 2 bytes of endpoing_info are genericDeviceClass and specificDeviceClass */
          if ((ep->endpoint_info_len - 2)> max) {
            ERR_PRINTF("node info length is more than size of nms buffer. Truncating\n");
            assert(0);
          }
          /* Add all occurences of COMMAND_CLASS_ASSOCIATION
           * with _IP_ASSOCIATION */
          len = mem_insert(&r->commandClass1, &ep->endpoint_info[2],
          COMMAND_CLASS_ASSOCIATION, COMMAND_CLASS_IP_ASSOCIATION, ep->endpoint_info_len - 2, max);
          if (len > max) {
            assert(0);
          }
           /* The magic number 6 below because nodeInfoLength also covers itself and following fields 
              BYTE      nodeInfoLength;
              BYTE      properties1;
              BYTE      properties2;
              BYTE      basicDeviceClass;
              BYTE      genericDeviceClass;
              BYTE      specificDeviceClass;          */
          r->nodeInfoLength = len  + 6;

          nms.buf_len = r->nodeInfoLength;  //6 for fields below nodeInfoLength and above commandClass1 in ZW_NODE_ADD_STATUS_1BYTE_FRAME
        }
        else
        {
          /* Node probing failed, we know nothing about the supported CC so it
           * should be an empty set. Here we set the length to 6 for following
           * fields only.
           * BYTE      nodeInfoLength;
           * BYTE      properties1;
           * BYTE      properties2;
           * BYTE      basicDeviceClass;
           * BYTE      genericDeviceClass;
           * BYTE      specificDeviceClass;
           */
          r->nodeInfoLength = 6;
          nms.buf_len = r->nodeInfoLength;
        }
        nms.buf_len += 6;// 6 for fields above nodeInfoLength in ZW_NODE_ADD_STATUS_1BYTE_FRAME

      } else if ((nms.cmd == FAILED_NODE_REPLACE) && 
                 (((uint8_t*)&nms.buf)[0] == COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION ) &&
                 (((uint8_t*)&nms.buf)[1] == FAILED_NODE_REPLACE_STATUS)) {
        ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX* reply = (ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX*) &nms.buf;
        reply->grantedKeys = sec2_gw_node_flags2keystore_flags(ndbe->security_flags);
        reply->kexFailType = (nms.inclusion_flags >> 16) & 0xFF; //TODO
        nms.inclusion_flags = 0;
        //ERR_PRINTF("Setting the granted keys :%x \n", reply->grantedKeys);
        goto skip; //stuff below is for add node status command
      }

      ((uint8_t*)&nms.buf)[nms.buf_len] = sec2_gw_node_flags2keystore_flags(ndbe->security_flags); //Granted keys
      ((uint8_t*)&nms.buf)[nms.buf_len+1] = (nms.inclusion_flags >> 16) & 0xFF;
      nms.buf_len +=2;
      nms.inclusion_flags = 0;

      /* Extended Node Add Status has no DSK fields */
      if (nms.buf.ZW_NodeAddStatus1byteFrame.cmd == NODE_ADD_STATUS)
      {
        if (nms.flags & NMS_FLAG_REPORT_DSK)
        {
          /* Check if there is enough space for 1 byte dsk length and dsk itself */
          if (nms.buf_len > (sizeof(nms.raw_buf) - (1 + sizeof(nms.just_included_dsk)))) {
              ERR_PRINTF("Copying the DSK length and DSK at wrong offset. Correcting.\n");
              nms.buf_len = (sizeof(nms.raw_buf) - (1 + sizeof(nms.just_included_dsk)));
              assert(0);
          }
          /* Add node DSK to add node callback*/
          (((uint8_t*)&nms.buf)[nms.buf_len++]) = sizeof(nms.just_included_dsk);
          memcpy(&(((uint8_t*)&nms.buf)[nms.buf_len]), nms.just_included_dsk, sizeof(nms.just_included_dsk));
          nms.buf_len+= sizeof(nms.just_included_dsk);
        }
        else
        {
          /* report 0-length DSK */
          uint8_t *nmsbuf = (uint8_t*)(&(nms.buf));
          nmsbuf[nms.buf_len] = 0;
          nms.buf_len++;
        }
      }
skip:
      nms.state = NM_WAIT_DHCP;

      /*Check if ip address has already been assigned*/
      if (cfg.ipv4disable || ipv46nat_ipv4addr_of_node(&a, nms.tmp_node))
      {
        if((cfg.mb_conf_mode != DISABLE_MAILBOX) && rd_get_node_mode(nms.tmp_node) == MODE_MAILBOX ) {
          mb_put_node_to_sleep_later(nms.tmp_node);
        }

        goto send_reply;
      }

      ctimer_set(&nms.timer, 5000, timeout, 0);
    }
    break;
  case NM_WAIT_DHCP:
    if ((ev == NM_EV_DHCP_DONE && nms.tmp_node == *(uint16_t*) event_data) || (ev == NM_EV_TIMEOUT))
    {
      if((cfg.mb_conf_mode != DISABLE_MAILBOX) && rd_get_node_mode(nms.tmp_node) == MODE_MAILBOX ) {
        mb_put_node_to_sleep_later(nms.tmp_node);
      }
      goto send_reply;
    }

    break;
  case NM_SET_DEFAULT:
    if( ev == NM_EV_MDNS_EXIT ) {
      ApplicationDefaultSet();
      bridge_reset();

      /*Register that we have entered a new network */
      /* This should be done immediately after ApplicationDefaultSet() */
      /* Cannot call zip_process here, since we are already in a sync
       * call to zip_process */
      process_post_synch(&zip_process, ZIP_EVENT_NEW_NETWORK, 0);

      /*Create an async application reset */
      process_post(&zip_process, ZIP_EVENT_RESET, 0);
      controller_role = SUC;
      SendReplyWhenNetworkIsUpdated();
    } else if( ev == NM_EV_TIMEOUT) {
      WRN_PRINTF("Set default timed out and did not get callback\n");
      nms.buf.ZW_DefaultSetCompleteFrame.cmdClass =
        COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC;
      nms.buf.ZW_DefaultSetCompleteFrame.cmd = DEFAULT_SET_COMPLETE;
      nms.buf.ZW_DefaultSetCompleteFrame.status = DEFAULT_SET_BUSY;
      nms.buf.ZW_DefaultSetCompleteFrame.seqNo = nms.seq;
      nms.buf_len = sizeof(nms.buf.ZW_DefaultSetCompleteFrame);
      // Soft Reset to make sure the callback does not come at later point
      ZW_SoftReset();
      nms.state = NM_IDLE;
      goto send_reply;
    }
    break;
  case NM_WAIT_FOR_MDNS:
    if( ev == NM_EV_MDNS_EXIT ) {
      if(!(nms.flags & NMS_FLAG_CONTROLLER_REPLICATION)) {
        /* TODO-KM: What about controller shift? */
        bridge_reset();
      }

      if (nms.flags & NMS_FLAG_LEARNMODE_NEW) {
         /* Send the LEARN_MODE_SET_STATUS we have in buf and set up
          * buf with an LEARN_MODE_INTERVIEW_COMPLETED to be sent when
          * interviewing is done. */
        ZW_SendDataZIP(&nms.conn, (BYTE*) &nms.buf, nms.buf_len, 0);

        ZW_LEARN_MODE_SET_STATUS_FRAME_EX *f = (ZW_LEARN_MODE_SET_STATUS_FRAME_EX*) &nms.buf;

        f->status = LEARN_MODE_INTERVIEW_COMPLETED;
        nms.buf_len = sizeof(ZW_LEARN_MODE_SET_STATUS_FRAME_EX);

        nms.state = NM_WAIT_FOR_PROBE_BY_SIS;
        ctimer_set(&nms.timer, 6000, timeout, 0);
      } else {
         /* Do not send the LEARN_MODE_SET_STATUS until after the gw
          * is up again. */
          process_post(&zip_process, ZIP_EVENT_RESET, 0);
          SendReplyWhenNetworkIsUpdated();
      }
    }
    break;
  case NM_WAIT_FOR_PROBE_BY_SIS:
    if( ev == NM_EV_TIMEOUT ) {
        /* rd_probe_lock(FALSE); dont unlock here, we will reset and then unlock */
        /*Create an async application reset */
        process_post(&zip_process, ZIP_EVENT_RESET, 0);
        nms.state = NM_WAIT_FOR_OUR_PROBE;
    } else if (ev  == NM_EV_FRAME_RECEIVED) {
      ctimer_set(&nms.timer, 6000, timeout, 0);
    }
    break;
  case NM_WAIT_FOR_OUR_PROBE:
    if (ev == NM_EV_ALL_PROBED) {
        if ((nms.flags & NMS_FLAG_LEARNMODE_NEW )) {
            /* Wait until probing is done to send LEARN_MODE_INTERVIEW_COMPLETED */ 
            DBG_PRINTF("Sending LEARN_MODE_INTERVIEW_COMPLETED\n");
            SendReplyWhenNetworkIsUpdated();
        }
    }
    break;
  case NM_WAIT_FOR_SECURE_LEARN:
    if(ev == NM_EV_ADD_SECURITY_KEY_CHALLENGE) {

      /*Update Command classes, according to our new network state. */
      SetPreInclusionNIF(SECURITY_SCHEME_2_ACCESS);

      ZW_LEARN_MODE_SET_STATUS_FRAME_EX *f = (ZW_LEARN_MODE_SET_STATUS_FRAME_EX*) &nms.buf;
      s2_node_inclusion_challenge_t *challenge_evt = (s2_node_inclusion_challenge_t *)event_data;


      /*Fill in the dsk part of the answer, the rest of the answer is filled in later*/
      memcpy(f->dsk,challenge_evt->public_key,16);
      sec2_dsk_accept(1,f->dsk,2);
    } else if(ev == NM_EV_SECURITY_DONE) {
      ZW_LEARN_MODE_SET_STATUS_FRAME_EX *f = (ZW_LEARN_MODE_SET_STATUS_FRAME_EX*) &nms.buf;
      uint32_t inclusion_flags;

      inclusion_flags = *(uint32_t*) event_data;

      security_init();
      DBG_PRINTF("inclusion flags ...... %x\n",inclusion_flags);
      f->cmdClass =
      COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC;
      f->cmd = LEARN_MODE_SET_STATUS;
      f->reserved = 0;
      f->seqNo = nms.seq;
      f->newNodeId = MyNodeID;
      f->granted_keys = sec2_gw_node_flags2keystore_flags(inclusion_flags & 0xFF);
      f->kexFailType = (inclusion_flags >> 16) & 0xFF;
      f->status = (NODE_FLAG_KNOWN_BAD & inclusion_flags) ? ADD_NODE_STATUS_SECURITY_FAILED : ADD_NODE_STATUS_DONE; 
      nms.buf_len = sizeof(ZW_LEARN_MODE_SET_STATUS_FRAME_EX);
      nms.state = NM_WAIT_FOR_MDNS;
      /*Clear out our S0 key*/
      if((NODE_FLAG_SECURITY0 & inclusion_flags) ==0) {
        keystore_network_key_clear(KEY_CLASS_S0);
      }
      /* If we are non-securely included, we need to clear our S2 keys here.
       * We cannot rely on the S2 security modules to do it for us since
       * S2 bootstrapping might never started. */
      if((NODE_FLAGS_SECURITY2 & inclusion_flags) == 0) {
        WRN_PRINTF("Clearing all S2 keys - nonsecure inclusion or no S2 keys granted\n");
        keystore_network_key_clear(KEY_CLASS_S2_ACCESS);
        keystore_network_key_clear(KEY_CLASS_S2_AUTHENTICATED);
        keystore_network_key_clear(KEY_CLASS_S2_UNAUTHENTICATED);
      }
      /*This is a new network, start sending mDNS goodbye messages,  */
      /* NetworkManagement_mdns_exited will be called at some point */
      rd_exit();
    } else if(ev == NM_EV_LEARN_SET) {
      ZW_LEARN_MODE_SET_FRAME* f = (ZW_LEARN_MODE_SET_FRAME*)event_data;
      if(f->mode == ZW_SET_LEARN_MODE_DISABLE) {
        nms.seq = f->seqNo; //Just because this was how we did in 2.2x
        /* Aborting S2 will cause S2 to return a SECURITY_DONE with fail. */
        sec2_abort_join();
      }
    }
    break;
  case NM_WIAT_FOR_SUC_INCLUSION:
    if(ev == NM_EV_PROXY_COMPLETE) {
      uint32_t flags = NODE_FLAG_SECURITY0 |
          NODE_FLAG_SECURITY2_UNAUTHENTICATED|
          NODE_FLAG_SECURITY2_AUTHENTICATED|
          NODE_FLAG_SECURITY2_ACCESS; /*TODO this is not a proper view*/

      /* GW can only probe those security keys it knows */
      flags &= GetCacheEntryFlag(MyNodeID);
      nms.state = NM_WAIT_FOR_SECURE_ADD;
      nm_fsm_post_event(NM_EV_SECURITY_DONE, &flags);
    }
    break;
  case NM_PROXY_INCLUSION_WAIT_NIF:
    if(ev == NM_EV_TIMEOUT) {
      nms.state = NM_IDLE;
      inclusion_controller_send_report(INCLUSION_CONTROLLER_STEP_FAILED);
      /* Notify that nms is idle */
      process_post(&zip_process, ZIP_EVENT_COMPONENT_DONE, 0);
    } else if ( ev == NM_EV_NODE_INFO ) {
      NODEINFO ni;

      if(nms.cmd == NODE_ADD) {
        memset(&nms.buf, 0, sizeof(nms.buf));
        nms.buf.ZW_NodeAddStatus1byteFrame.cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
        nms.buf.ZW_NodeAddStatus1byteFrame.cmd = NODE_ADD_STATUS;
        nms.buf.ZW_NodeAddStatus1byteFrame.seqNo = nms.seq;
        nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength = 1;
        nms.buf_len = nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength + 6;

        /* Get the Capabilities and Security fields. */
        ZW_GetNodeProtocolInfo(nms.tmp_node, &ni);
        nms.buf.ZW_NodeAddStatus1byteFrame.properties1 = ni.capability;
        nms.buf.ZW_NodeAddStatus1byteFrame.properties2 = ni.security;

        nms.flags = NMS_FLAG_S2_ADD| NMS_FLAG_PROXY_INCLUSION;

        /* Simulate the add process */
        nms.state = NM_NODE_FOUND;
        nm_fsm_post_event(NM_EV_ADD_CONTROLLER,event_data);

        nm_fsm_post_event(NM_EV_ADD_NODE_STATUS_DONE,event_data);

      } else {
        ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX* reply = (ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX*) &nms.buf;
        nms.cmd = FAILED_NODE_REPLACE;
        nms.state = NM_REPLACE_FAILED_REQ;
        reply->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
        reply->cmd = FAILED_NODE_REPLACE_STATUS;
        reply->seqNo = nms.seq;
        reply->nodeId = nms.tmp_node;
        reply->status = ZW_FAILED_NODE_REPLACE_FAILED;
        reply->kexFailType=0x00;
        reply->grantedKeys =0x00;
        nms.buf_len = sizeof(ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX);

        nms.flags = NMS_FLAG_S2_ADD| NMS_FLAG_PROXY_INCLUSION;
        nm_fsm_post_event(NM_EV_REPLACE_FAILED_DONE,event_data);
      }

    }
    break;
  case NM_LEARN_MODE:
  {
    if(ev == NM_EV_TIMEOUT) {
      if(nms.count == 0) {
        /* We must stop Learn Mode classic before starting NWI learn mode */
        ZW_SetLearnMode(ZW_SET_LEARN_MODE_DISABLE, NULL);
        if(nms.flags & NMS_FLAG_LEARNMODE_NWI) {
          ZW_SetLearnMode(ZW_SET_LEARN_MODE_NWI,LearnModeStatus);
        } else if(nms.flags & NMS_FLAG_LEARNMODE_NWE) {
          ZW_SetLearnMode(ZW_SET_LEARN_MODE_NWE,LearnModeStatus);
        }
      }

      if( (nms.flags & (NMS_FLAG_LEARNMODE_NWI | NMS_FLAG_LEARNMODE_NWE)) && (nms.count < 4)) {
        if(nms.flags & NMS_FLAG_LEARNMODE_NWI) {
          ZW_ExploreRequestInclusion();
        } else {
          ZW_ExploreRequestExclusion();
        }

        int delay =  CLOCK_SECOND*4 + (rand() & 0xFF);
        ctimer_set(&nms.timer,delay, timeout, 0);
        nms.count++;
      } else {
        LearnTimerExpired();
      }
    } else if(ev == NM_EV_LEARN_SET) {
      ZW_LEARN_MODE_SET_FRAME* f = (ZW_LEARN_MODE_SET_FRAME*) event_data;

      if (f->mode == ZW_SET_LEARN_MODE_DISABLE)
      {
        nms.seq = f->seqNo; //Just because this was how we did in 2.2x
        LearnTimerExpired();
      }
    }
    break;
  }
  case NM_WAIT_FOR_SELF_DESTRUCT:
  case NM_WAIT_FOR_SELF_DESTRUCT_RETRY:
    if (ev == NM_EV_TIMEOUT)
    {
      uint8_t nop_frame[1] = {0};
      ts_param_t ts;
      ts_set_std(&ts, nms.tmp_node);

      if (nms.state == NM_WAIT_FOR_SELF_DESTRUCT) {
          nms.state = NM_WAIT_FOR_TX_TO_SELF_DESTRUCT;
      } else {
          nms.state = NM_WAIT_FOR_TX_TO_SELF_DESTRUCT_RETRY;
      }

      if(!ZW_SendDataAppl(&ts, nop_frame, sizeof(nop_frame),nop_send_done,0) ) {
              nop_send_done(TRANSMIT_COMPLETE_FAIL,0, NULL);
      }
    }
    break;
  case NM_WAIT_FOR_TX_TO_SELF_DESTRUCT:
  case NM_WAIT_FOR_TX_TO_SELF_DESTRUCT_RETRY:
    /* As a preparation for removing the self-destructed node,
     * we must attempt TX to it. Otherwise the protocol will
     * not allow us to do ZW_RemoveFailed() on it. */
    if (ev == NM_EV_TX_DONE_SELF_DESTRUCT)
    {
      /* Perform remove failed */
      if (nms.state == NM_WAIT_FOR_TX_TO_SELF_DESTRUCT) {
        nms.state = NM_WAIT_FOR_SELF_DESTRUCT_REMOVAL;
        DBG_PRINTF("Removing self destruct nodeid %u\n",nms.tmp_node);
      } else {
        nms.state = NM_WAIT_FOR_SELF_DESTRUCT_REMOVAL_RETRY;
        DBG_PRINTF("Retry of removing self destruct nodeid %u\n",nms.tmp_node);
      }
      nms.buf_len += nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength - 1;
      assert(nms.tmp_node);
      ctimer_set(&nms.timer, CLOCK_SECOND*20, timeout, 0); /* TODO: Is 20 sec. reasonable? */
      if (ZW_RemoveFailedNode(nms.tmp_node,
          RemoveSelfDestructStatus) != ZW_FAILED_NODE_REMOVE_STARTED)
      {
        DBG_PRINTF("Remove self-destruct failed\n");
        RemoveSelfDestructStatus(ZW_FAILED_NODE_NOT_REMOVED);
      }
    }
    break;

  case NM_WAIT_FOR_SELF_DESTRUCT_REMOVAL:
  case NM_WAIT_FOR_SELF_DESTRUCT_REMOVAL_RETRY:
    /* Send the NODE_ADD_STATUS zip packet with proper status code to both unsoc dest and
     * reset the NM state.
     * We return ADD_NODE_STATUS_FAILED on ZW_FAILED_NODE_REMOVE and ADD_NODE_SECURITY_FAILED
     * otherwise. This is required by spec.
     */
    if (ev == NM_EV_REMOVE_FAILED_OK)
    {
      nms.buf.ZW_NodeAddStatus1byteFrame.status = ADD_NODE_STATUS_FAILED;
      send_to_both_unsoc_dest((uint8_t*)&nms.buf, nms.buf_len, ResetState);
      /* Delete the removed node-id from the RD and DHCP. */
      /* In a corner case, the device may believe it was included and
       * security succeeded and then not self-destruct (eg, it is out
       * of reach).  In that case, the device must be expicitly reset
       * by a user. */
       ApplicationControllerUpdate(UPDATE_STATE_DELETE_DONE,
                                   nms.tmp_node, 0, 0, NULL);
      /* Unlock probe engine, but there is no node to probe, so just
       * cancel it. */
      rd_probe_cancel();
    }
    else if (ev == NM_EV_REMOVE_FAILED_FAIL)
    {
      //this is first attemp removed failed fail. Try again in 240s.
      if (nms.state == NM_WAIT_FOR_SELF_DESTRUCT_REMOVAL) {
        nms.state = NM_WAIT_FOR_SELF_DESTRUCT_RETRY;
        ctimer_set(&nms.timer, SMART_START_SELF_DESTRUCT_RETRY_TIMEOUT, timeout, 0);
      } else if (nms.state == NM_WAIT_FOR_SELF_DESTRUCT_REMOVAL_RETRY) {
        nms.buf.ZW_NodeAddStatus1byteFrame.status = ADD_NODE_STATUS_SECURITY_FAILED;
        /* Unlock the probe engine. */
        rd_probe_cancel();
        /* TODO: we may want to probe the new node before we re-start
         * smart start. */
        send_to_both_unsoc_dest((uint8_t*)&nms.buf, nms.buf_len, ResetState);
      }
    }
    else if (ev == NM_EV_TIMEOUT)
    {
      /* Protocol should always call back, but if it did not, we reset as a fall-back*/
      DBG_PRINTF("Timed out waiting for ZW_RemoveFailed() of self-destruct node\n");
      /* Unlock the probe engine */
      rd_probe_cancel();
      ResetState(0, 0, 0);
    }
    break;

  case NM_WAIT_FOR_NODE_INFO_PROBE:
     if (ev == NM_EV_NODE_PROBE_DONE) {
        rd_node_database_entry_t *ndbe = (rd_node_database_entry_t *) event_data;
        if (ndbe->nodeid == nms.tmp_node) {
           ZW_NODE_INFO_CACHED_REPORT_1BYTE_FRAME* f = (ZW_NODE_INFO_CACHED_REPORT_1BYTE_FRAME*) &nms.buf;
           int len = BuildNodeInfoCachedReport(ndbe, f);
           nm_send_reply(f, 10 + len);
        }
        /* If this is not the node probe NM is looking for, do nothing. */
     }
     break;

  case NM_WAITING_FOR_NODE_NEIGH_UPDATE:
     if (ev == NM_EV_TIMEOUT) {
       /* When there are FLIRS devices in the network the protocol will not
        * activate the status callback (see SWPROT-3666). We do that here to
        * avoid blocking network management forever.
        */
       RequestNodeNeighborUpdateStatus(REQUEST_NEIGHBOR_UPDATE_FAILED);
     }
     break;

  case NM_LEARN_MODE_STARTED:
  case NM_NETWORK_UPDATE:
  case NM_WAITING_FOR_PROBE:
  case NM_REMOVING_ASSOCIATIONS:
  case NM_SENDING_NODE_INFO:
  case NM_WAITING_FOR_NODE_REMOVAL:
  case NM_WAITING_FOR_FAIL_NODE_REMOVAL:
  case NM_WAITING_FOR_RETURN_ROUTE_ASSIGN:
  case NM_WAITING_FOR_RETURN_ROUTE_DELETE:

    break;
  }
  return;

  send_reply:
//  nms.state = NM_IDLE; Reset state will sent the FSM to IDLE
  if(nms.flags & NMS_FLAG_PROXY_INCLUSION) {
    if(nms.buf.ZW_NodeAddStatus1byteFrame.status  == ADD_NODE_STATUS_DONE) {
      inclusion_controller_send_report(INCLUSION_CONTROLLER_STEP_OK);
    } else {
      inclusion_controller_send_report(INCLUSION_CONTROLLER_STEP_FAILED);
    }
  }
  /* Smart Start inclusion requires an extra step after sending reply */
  if (!(nms.flags & NMS_FLAG_SMART_START_INCLUSION)) {
    nm_send_reply(&nms.buf, nms.buf_len);
  } else {
    if (nms.buf.ZW_NodeAddStatus1byteFrame.status == ADD_NODE_STATUS_FAILED) {
      ERR_PRINTF(
          "Sending network management reply: Smart Start inclusion failed\n");
    } else {
      DBG_PRINTF(
          "Sending network management reply: Smart Start inclusion success\n");
    }
    send_to_both_unsoc_dest((uint8_t *)&nms.buf, nms.buf_len,
                            wait_for_middleware_probe);
    //ZW_SendDataZIP(&nms.conn, (BYTE*) &nms.buf, nms.buf_len, wait_for_middleware_probe);
  }
}

static void nop_send_done(uint8_t status, void* user, TX_STATUS_TYPE *t) {
  UNUSED(t);
  nm_fsm_post_event(NM_EV_TX_DONE_SELF_DESTRUCT, 0);
}

void NetworkManagement_nif_notify(nodeid_t bNodeID,uint8_t* pCmd,uint8_t bLen) {
  LEARN_INFO info;
  info.bStatus = ADD_NODE_STATUS_ADDING_END_NODE;
  info.bSource = bNodeID;
  info.pCmd = pCmd;
  info.bLen = bLen;

  nm_fsm_post_event(NM_EV_NODE_INFO,&info);
}

void NetworkManagement_dsk_challenge(s2_node_inclusion_challenge_t *challenge_evt) {
  nm_fsm_post_event(NM_EV_ADD_SECURITY_KEY_CHALLENGE,challenge_evt);
}

void NetworkManagement_key_request(s2_node_inclusion_request_t* inclusion_request) {
  nm_fsm_post_event(NM_EV_ADD_SECURITY_REQ_KEYS,inclusion_request);
}

void NetworkManagement_start_proxy_inclusion(nodeid_t node_id) {
  nm_fsm_post_event(NM_EV_START_PROXY_INCLUSION, &node_id);
}

void NetworkManagement_start_proxy_replace(nodeid_t node_id) {
  nm_fsm_post_event(NM_EV_START_PROXY_REPLACE, &node_id);
}

void NetworkManagement_all_nodes_probed()
{
  nm_fsm_post_event(NM_EV_ALL_PROBED, 0);
}

/* Post NM_EV_NODE_PROBE_DONE */
void NetworkManagement_node_probed(void *node) {
   nm_fsm_post_event(NM_EV_NODE_PROBE_DONE, node);
}

static void
SecureInclusionDone(int status)
{
  uint32_t safe_status = status;
  nm_fsm_post_event(NM_EV_SECURITY_DONE, &safe_status);
}

static void
timeout(void* none)
{
  nm_fsm_post_event(NM_EV_TIMEOUT,0);
}

void
NetworkManagement_IPv4_assigned(nodeid_t node)
{
  nm_fsm_post_event(NM_EV_DHCP_DONE,&node);

  if (nms.waiting_for_ipv4_addr == node)
  {
    nms.waiting_for_ipv4_addr = 0;
    nm_send_reply(&nms.buf, nms.buf_len);
    ZW_LTimerCancel(nms.networkManagementTimer);
  }
}

void
NetworkManagement_VirtualNodes_removed()
{
  if (nms.state == NM_REMOVING_ASSOCIATIONS)
  {
    nm_send_reply(&nms.buf, nms.buf_len);
    ZW_LTimerCancel(nms.networkManagementTimer);
  }
}

void NetworkManagement_NetworkUpdateStatusUpdate(u8_t flag)
{
  networkUpdateStatusFlags |= flag;

  if (ipv46nat_all_nodes_has_ip() || cfg.ipv4disable )
  {
    networkUpdateStatusFlags |= NETWORK_UPDATE_FLAG_DHCPv4;
  }

  if(bridge_state != booting) {
    networkUpdateStatusFlags |= NETWORK_UPDATE_FLAG_VIRTUAL;
  }

  DBG_PRINTF("update flag 0x%x 0x%x\n",flag,networkUpdateStatusFlags);
  if (networkUpdateStatusFlags
      == (NETWORK_UPDATE_FLAG_DHCPv4 | NETWORK_UPDATE_FLAG_PROBE | NETWORK_UPDATE_FLAG_VIRTUAL))
  {
    nm_send_reply(&nms.buf, nms.buf_len);
    ZW_LTimerCancel(nms.networkManagementTimer);
  }
}

static void
network_update_timeout()
{
  NetworkManagement_NetworkUpdateStatusUpdate(
  NETWORK_UPDATE_FLAG_DHCPv4 | NETWORK_UPDATE_FLAG_PROBE);
}

/**
 * Setup the transmission of nms.buf when the Network update, node probing and Ipv4 assignment of
 * new nodes has completed.
 */
static void
SendReplyWhenNetworkIsUpdated()
{
  if (nms.networkManagementTimer != 0xFF)
  {
    ZW_LTimerCancel(nms.networkManagementTimer);
  }
  nms.state = NM_WAITING_FOR_PROBE;
  networkUpdateStatusFlags = 0;

  if ((bridge_state == initialized) || (controller_role != SUC))
  {
    networkUpdateStatusFlags |= NETWORK_UPDATE_FLAG_VIRTUAL;
  }

  /* Wait 65 secs */
  nms.networkManagementTimer = ZW_LTimerStart(network_update_timeout, 0xFFFF, TIMER_ONE_TIME);

  rd_probe_lock(FALSE);

  /*Check the we actually allocated the timer */
  if (nms.networkManagementTimer == 0xFF)
  {
    network_update_timeout();
  }
}

/**
 * Reset the network management state to #NM_IDLE.
 *
 * Cancel the NM timer and clear all the flags in #nms.  Set
 * #NETWORK_UPDATE_FLAG_DISABLED on #networkUpdateStatusFlags.
 *
 * Try to restart Smart Start.  Post #ZIP_EVENT_NETWORK_MANAGEMENT_DONE to \ref ZIP_Router.
 */
static void
ResetState(BYTE dummy, void* user, TX_STATUS_TYPE *t)
{
  DBG_PRINTF("Reset Network management State\n");
  ZW_LTimerCancel(nms.networkManagementTimer);
  nms.networkManagementTimer = 0xFF;
  nms.cmd = 0;
  nms.waiting_for_ipv4_addr = 0;
  nms.buf_len = 0;
  nms.flags =0;
  networkUpdateStatusFlags = 0x80;
  nms.state = NM_IDLE;
  nms.granted_keys=0;

  NetworkManagement_queue_nm_done_event();
  NetworkManagement_smart_start_init_if_pending();

  /* Notify that NMS is IDLE */
  process_post(&zip_process, ZIP_EVENT_NETWORK_MANAGEMENT_DONE, 0);
}
static void
__ResetState(BYTE dummy)
{
  ResetState(dummy, 0, NULL);
}

/**
 * Timeout of learn mode.
 *
 * Tell the protocol to stop learn mode.  Restore gateway state by
 * calling the #LearnModeStatus() callback, simulating learn mode fail.
 *
 * Called from \ref nm_fsm_post_event() when the nm timer expires in
 * #NM_LEARN_MODE or if the client cancels learn mode.
 *
 * In cancel, the gateway receives a #LEARN_MODE_SET command with mode
 * \a ZW_SET_LEARN_MODE_DISABLE.
 *
 * \note The gateway is always in #NM_LEARN_MODE when this function is
 * called, but since this is asynchronous, there is a tiny probability
 * that a callback with #LEARN_MODE_STARTED is in the queue for us.
 * In that case, the protocol is already committed, and it would be
 * wrong to cancel learn mode.
 *
 */
static void
LearnTimerExpired(void)
{
  LEARN_INFO inf;
  LOG_PRINTF("Learn timed out or canceled\n");
  /*Restore command classes as they were */
  ApplicationInitNIF();
  ZW_SetLearnMode(ZW_SET_LEARN_MODE_DISABLE, 0);
  inf.bStatus = LEARN_MODE_FAILED;
  LearnModeStatus(&inf);
}

/**
 * Timeout for remove node.
 */
static void
RemoveTimerExpired(void)
{
  LEARN_INFO inf;

  ZW_LTimerCancel(nms.addRemoveNodeTimerHandle);
  LOG_PRINTF("Remove timed out or canceled\n");
  ZW_RemoveNodeFromNetwork(REMOVE_NODE_STOP, 0);
  inf.bStatus = REMOVE_NODE_STATUS_FAILED;
  inf.bSource = 0;
  RemoveNodeStatusUpdate(&inf);
}

/**
 * Generic wrapper to send a reply to the host whom we are talking to.
 */
static void
nm_send_reply(void* buf, u16_t len)
{
  unsigned char *c = (uint8_t * )buf;
  DBG_PRINTF("Sending network management reply: class: 0x%02x cmd: 0x%02x\n", c[0], c[1]);
  ZW_SendDataZIP(&nms.conn, (BYTE*) buf, len, ResetState);
}

void
NetworkManagement_s0_started()
{
  nm_fsm_post_event(NM_EV_S0_STARTED, 0);
}


void
NetworkManagement_frame_notify()
{
  nm_fsm_post_event(NM_EV_FRAME_RECEIVED, 0);
}

/**
 * Callback for ZW_AddNodeToNetwork
 */
static void
AddNodeStatusUpdate(LEARN_INFO* inf)
{
  nm_fsm_post_event(inf->bStatus, inf);
}

/**
 * Callback for remove node
 */
static void
RemoveNodeStatusUpdate(LEARN_INFO* inf)
{
  static nodeid_t removed_nodeid;
  ZW_NODE_REMOVE_STATUS_V4_FRAME* r = (ZW_NODE_REMOVE_STATUS_V4_FRAME*) &nms.buf;
  DBG_PRINTF("RemoveNodeStatusUpdate status=%d node %d\n", inf->bStatus, inf->bSource);
  switch (inf->bStatus)
  {
  case ADD_NODE_STATUS_LEARN_READY:
    memset(&nms.buf, 0, sizeof(nms.buf));
    /* Start remove timer */
    nms.addRemoveNodeTimerHandle = ZW_LTimerStart(RemoveTimerExpired,
    ADD_REMOVE_TIMEOUT, 1);
    break;
  case REMOVE_NODE_STATUS_NODE_FOUND:
    break;
  case REMOVE_NODE_STATUS_REMOVING_END_NODE:
  case REMOVE_NODE_STATUS_REMOVING_CONTROLLER:
    if (is_lr_node(inf->bSource)) {
      r->nodeid = 0xff;
      r->extendedNodeidMSB = inf->bSource >> 8;
      r->extendedNodeidLSB = inf->bSource & 0xff;
    } else {
      r->extendedNodeidMSB = 0; 
      r->extendedNodeidLSB = 0;
      r->nodeid = inf->bSource;
    }
    removed_nodeid = inf->bSource;
    break;
  case REMOVE_NODE_STATUS_DONE:
    DBG_PRINTF("Node Removed %d\n", removed_nodeid);
    nms.state = NM_REMOVING_ASSOCIATIONS;
    /* Application controller update will call ip_assoc_remove_by_nodeid()
     * which will post a ZIP_EVENT_NM_VIRT_NODE_REMOVE_DONE, which will then call
     * NetworkManagement_VirtualNodes_removed */
    ApplicationControllerUpdate(UPDATE_STATE_DELETE_DONE, removed_nodeid, 0, 0, NULL);
    NetworkManagement_queue_purge_node(removed_nodeid);
    /*no break*/
  case REMOVE_NODE_STATUS_FAILED:
    r->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
    r->cmd = NODE_REMOVE_STATUS;
    r->status = inf->bStatus;
    r->seqNo = nms.seq;

    nms.buf_len = sizeof(ZW_NODE_REMOVE_STATUS_V4_FRAME);
    if ((inf->bStatus == REMOVE_NODE_STATUS_FAILED) || nodemask_nodeid_is_invalid(removed_nodeid))
    {
      r->nodeid = 0;
      r->extendedNodeidMSB= 0;
      r->extendedNodeidLSB= 0;
    }

    ZW_LTimerCancel(nms.addRemoveNodeTimerHandle);
    ZW_RemoveNodeFromNetwork(REMOVE_NODE_STOP, 0);
    nm_send_reply(r, sizeof(ZW_NODE_REMOVE_STATUS_V4_FRAME));
    break;
  }

}

/**
 * Remove failed node callback
 */
static void
RemoveFailedNodeStatus(BYTE status)
{
  ZW_FAILED_NODE_REMOVE_STATUS_V4_FRAME* f = (ZW_FAILED_NODE_REMOVE_STATUS_V4_FRAME*) &nms.buf;
  BYTE s = NM_FAILED_NODE_REMOVE_FAIL;

  DBG_PRINTF("RemoveFailedNodeStatus status: %d\n", status);

  /* Mapping ZW_RemoveFailedNode return values to requirement CC.0034.01.08.11.001 */
  if (status == ZW_NODE_OK || status == ZW_FAILED_NODE_NOT_REMOVED) {
     s = NM_FAILED_NODE_REMOVE_FAIL;
  } else if (status == ZW_FAILED_NODE_NOT_FOUND ) {
     s = NM_FAILED_NODE_NOT_FOUND;
  } else if (status == ZW_FAILED_NODE_REMOVED) {
     s = NM_FAILED_NODE_REMOVE_DONE;
  }

  f->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
  f->cmd = FAILED_NODE_REMOVE_STATUS;
  f->seqNo = nms.seq;
  f->status = s;
  if (is_lr_node(nms.tmp_node)) {
    f->nodeId = 0xff;
    f->extendedNodeIdMSB = nms.tmp_node >> 8;
    f->extendedNodeIdLSB = nms.tmp_node & 0xff;
  } else {
    f->nodeId = nms.tmp_node;
    f->extendedNodeIdMSB = 0;
    f->extendedNodeIdLSB = 0;
  }
  nm_send_reply(f, sizeof(ZW_FAILED_NODE_REMOVE_STATUS_V4_FRAME));

  if (status == ZW_FAILED_NODE_REMOVED)
  {
    DBG_PRINTF("Failed node Removed%d\n", nms.tmp_node);
    /* Trigger resource directory update and remove associations.  Set
     * should_send_nodelist and restart SmartStart. */
    ApplicationControllerUpdate(UPDATE_STATE_DELETE_DONE, nms.tmp_node, 0, 0, NULL);
  }
  /* Protocol has tested the node and found it working. Hence we are not allowed
   * to Failed Node Remove it and we should report it working again. */ 
  rd_node_database_entry_t *n;
  if (status == ZW_NODE_OK) {
    n = rd_node_get_raw(nms.tmp_node);
    if (n) {
      rd_set_failing(n, FALSE); 
    }
  }
}

/**
 * Remove a self-destructed node callback
 * Notify the FSM of status of RemoveFailed for self-destructed node.
 *
 * \param status One of the ZW_RemoveFailedNode() callback statuses:
 *        ZW_NODE_OK
 *        ZW_FAILED_NODE_REMOVED
 *        ZW_FAILED_NODE_NOT_REMOVED.
 *
 * ZW_NODE_OK must be considered a _FAIL because that will send _SECURITY_FAILED
 * to unsolicited destination, which is what the spec requires in case a
 * failed smart start node could not be removed from the network.
 */
static void
RemoveSelfDestructStatus(BYTE status)
{
  if (status == ZW_FAILED_NODE_REMOVED)
  {
    nm_fsm_post_event(NM_EV_REMOVE_FAILED_OK, 0);
  }
  else
  {
    nm_fsm_post_event(NM_EV_REMOVE_FAILED_FAIL, 0);
  }
}


/*
 * Replace failed node callback
 */
static void
ReplaceFailedNodeStatus(BYTE status)
{
  switch (status)
  {
  case ZW_FAILED_NODE_REPLACE:
    LOG_PRINTF("Ready to replace node....\n");
    break;
  case ZW_FAILED_NODE_REPLACE_DONE:
    nm_fsm_post_event(NM_EV_REPLACE_FAILED_DONE,0);
    break;
  case ZW_NODE_OK:

    /* no break */
  case ZW_FAILED_NODE_REPLACE_FAILED:
    nm_fsm_post_event(NM_EV_REPLACE_FAILED_FAIL,0);
    break;
  }
}

void
NetworkManagement_mdns_exited()
{
  nm_fsm_post_event(NM_EV_MDNS_EXIT,0);
}

/**
 * Set default callback.
 *
 * Used in the \a ZW_SetDefault() call when NMS is processing \a
 * DEFAULT_SET.  NMS should be in state #NM_SET_DEFAULT.
 *
 * Prepare NMS reply buffer and call rd_exit() to tear down mDNS and RD.
 */
static void
SetDefaultStatus()
{
  nms.buf.ZW_DefaultSetCompleteFrame.cmdClass =
  COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC;
  nms.buf.ZW_DefaultSetCompleteFrame.cmd = DEFAULT_SET_COMPLETE;
  nms.buf.ZW_DefaultSetCompleteFrame.seqNo = nms.seq;
  nms.buf.ZW_DefaultSetCompleteFrame.status = DEFAULT_SET_DONE;
  nms.buf_len = sizeof(nms.buf.ZW_DefaultSetCompleteFrame);
  DBG_PRINTF("Controller reset done\n");

  NetworkManagement_queue_purge_all();
  rd_exit();
}

static void RequestNodeNeighborUpdat_callback_after_s2_inclusion(BYTE status)
{
   if (nms.state == NM_WAIT_FOR_NEIGHBOR_UPDATE_AFTER_SECURE_ADD) {
    switch (status)
    {
    case REQUEST_NEIGHBOR_UPDATE_STARTED:
      break;
    case REQUEST_NEIGHBOR_UPDATE_DONE:
    case REQUEST_NEIGHBOR_UPDATE_FAILED:
      nm_fsm_post_event(NM_EV_NEIGHBOR_UPDATE_AFTER_SECURE_ADD_DONE,0);
      break;
    }
  }   
}
/*
 * ZW_RequestNeighborUpdate callback
 */
static void
RequestNodeNeighborUpdateStatus(BYTE status)
{
  /* We need to ensure we're still in the correct state.
   *
   * As a workaround to SWPROT-3666 the gateway has wrapped the call to
   * ZW_RequestNodeNeighborUpdate() in a timer. If/when the protocol is fixed we
   * can risk that the callback comes from the protocol after the gateway has
   * timed out and moved on to something else.
   */
  if (nms.state == NM_WAITING_FOR_NODE_NEIGH_UPDATE) {
    switch (status)
    {
    case REQUEST_NEIGHBOR_UPDATE_STARTED:
      break;
    case REQUEST_NEIGHBOR_UPDATE_DONE:
    case REQUEST_NEIGHBOR_UPDATE_FAILED:
      nms.buf.ZW_NodeNeighborUpdateStatusFrame.cmdClass =
      COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
      nms.buf.ZW_NodeNeighborUpdateStatusFrame.cmd =
      NODE_NEIGHBOR_UPDATE_STATUS;
      nms.buf.ZW_NodeNeighborUpdateStatusFrame.seqNo = nms.seq;
      nms.buf.ZW_NodeNeighborUpdateStatusFrame.status = status;
      nm_send_reply(&nms.buf, sizeof(nms.buf.ZW_NodeNeighborUpdateStatusFrame));
      break;
    }
  }
}
#if 0
static void
reset_delayed()
{
  process_post(&zip_process, ZIP_EVENT_RESET, 0);
}
#endif


/**
 * Return true is this is a clean network containing only the ZIP router and
 * if this is a new network compared to what we have en RAM
 */
static void
isCleanNetwork(BOOL* clean_network, BOOL* new_network)
{
  BYTE ver, capabilities, len;
  BYTE node_list[MAX_CLASSIC_NODEMASK_LENGTH] = {0};
  BYTE c, v;
  DWORD h;
  nodeid_t n;
  int i;
  uint16_t lr_nodelist_len = 0;
  uint8_t lr_nodelist[MAX_LR_NODEMASK_LENGTH] = {0};

  MemoryGetID((BYTE*) &h, &n);
  SerialAPI_GetInitData(&ver, &capabilities, &len, node_list, &c, &v);

  *new_network = (h != homeID);

  node_list[(n - 1) >> 3] &= ~(1 << ((n - 1) & 0x7));
  for (i = 0; i < MAX_CLASSIC_NODEMASK_LENGTH; i++)
  {
    if (node_list[i])
    {
      *clean_network = FALSE;
      return;
    }
  }
  SerialAPI_GetLRNodeList(&lr_nodelist_len, (uint8_t *)&lr_nodelist);
  for ( i = 0; i < lr_nodelist_len; i++) {
    if (lr_nodelist[i])
    {
      *clean_network = FALSE;
      return;
    }
  }

  *clean_network = TRUE;
}

const char *learn_mode_status_str(int ev)
{

  static char str[25];
  switch (ev)
  {
  case LEARN_MODE_STARTED              : return  "LEARN_MODE_STARTED";
  case LEARN_MODE_DONE                 : return  "LEARN_MODE_DONE";
  case LEARN_MODE_FAILED               : return  "LEARN_MODE_FAILED";
  case LEARN_MODE_INTERVIEW_COMPLETED  : return  "LEARN_MODE_INTERVIEW_COMPLETED";
  default:
    sprintf(str, "%d", ev);
    return str;
  }

}
static void
LearnModeStatus(LEARN_INFO* inf)
{
  BOOL clean_network, new_network;
  static nodeid_t old_nodeid;

  if((nms.state != NM_LEARN_MODE) && (nms.state != NM_LEARN_MODE_STARTED) ) {
    ERR_PRINTF("LearnModeStatus callback while not in learn mode\n");
    return;
  }

  ZW_LEARN_MODE_SET_STATUS_FRAME* f = (ZW_LEARN_MODE_SET_STATUS_FRAME*) &nms.buf;
  DBG_PRINTF("learn mode %s\n", learn_mode_status_str(inf->bStatus));

  switch (inf->bStatus)
  {
  case LEARN_MODE_STARTED:
    rd_probe_lock(TRUE);
    /* Set my nodeID to an invalid value, to keep controller updates from messing things up*/
    old_nodeid = MyNodeID;
    MyNodeID = 0;
    nms.tmp_node = inf->bSource;
    nms.state = NM_LEARN_MODE_STARTED;

    /* Start security here to make sure the timers are started in time */
    security_learn_begin(SecureInclusionDone);
    sec2_start_learn_mode(nms.tmp_node, SecureInclusionDone);
    break;
  case LEARN_MODE_DONE:

    /*There are three outcomes of learn mode
     * 1) Controller has been included into a new network
     * 2) Controller has been excluded from a network
     * 3) Controller replication or controller shift
     * */
    isCleanNetwork(&clean_network, &new_network);

    if((ZW_GetControllerCapabilities() & CONTROLLER_IS_SECONDARY) == 0) {
      // OK, we are actually not really SUC at this point but later on ApplicationInitNIF will
      // make us SUC
      controller_role = SUC;
    }

    nms.state = NM_WAIT_FOR_SECURE_LEARN;

    if (clean_network || inf->bSource == 0)
    {
      WRN_PRINTF("Z/IP Gateway has been excluded.\n");
      NetworkManagement_queue_purge_all();
      /* DHCP client adjustments related to leaving one network and
         entering another. */
      /*Stop the DHCP process, since its sensitive for NODEid changes*/
      process_exit(&dhcp_client_process);
      /* If node id 1 was not the gateway, delete the 46nat entry
         corresponding to node 1 and send a DHCP release before using
         nodeid 1 again. */
      if (old_nodeid != 1) {
        DBG_PRINTF("Deleting nat entry for node 1\n");
        ipv46nat_del_entry(1);
      }
      MyNodeID = 1;
      /* Enable DHCP to hang on to the IP addr of the gateway. */

      ipv46nat_rename_node(old_nodeid, MyNodeID);
      /* Simulate NM_EV_SECURITY_DONE in the NMS.  This will change
       * state to NM_WAIT_FOR_MDNS.  Since this is a clean network, we
       * change that to SET_DEFAULT. */
      SecureInclusionDone(0);
      nms.state = NM_SET_DEFAULT;

      process_start(&dhcp_client_process, 0);
    }
    else if (new_network)
    {
      sec2_inclusion_neighbor_discovery_complete();
      /*Make sure there is no old entry in the nat */
      ipv46nat_del_entry(inf->bSource);
      ipv46nat_rename_node(old_nodeid,inf->bSource);

      /*Stop the DHCP process, since its sensitive for NODEid changes*/
      process_exit(&dhcp_client_process);
      rd_mark_node_deleted(old_nodeid);

      /*Update home id and node id, security engine needs to know correct nodeid */

      MemoryGetID((BYTE*) &homeID, &MyNodeID);
      MyNodeID = inf->bSource;

      /* Release the old ipv4 */
      ipv46nat_del_all_nodes();

      refresh_ipv6_addresses();
      keystore_public_key_debug_print();
      sec2_refresh_homeid();

      if( nms.flags & NMS_FLAG_LEARNMODE_NEW) {
        /* Acquire an IPv4 address from DHCP for the node who included
         * us. */
        process_start(&dhcp_client_process,0);
        ipv46nat_add_entry(nms.tmp_node);
      }

      /*Register that we have entered a new network */
      process_post_synch(&zip_process, ZIP_EVENT_NEW_NETWORK, 0);
    }
    else
    {
      nms.flags |= NMS_FLAG_CONTROLLER_REPLICATION; /* or controller shift */

      /*Update home id and node id, security engine needs to know correct nodeid */
      MemoryGetID((BYTE*) &homeID, &MyNodeID);

      /*This was a controller replication, ie. this is not a new network. */
      WRN_PRINTF("This was a controller replication\n");
      /* Security keys are unchanged, return existing flags */
      SecureInclusionDone(sec2_get_my_node_flags());
    }

    return;
  case LEARN_MODE_FAILED:
      /* We can only get learn mode FAILED on the serial API during
       * inclusion, and only rarely.  The including controller will
       * assume that we are actually included. */
      /* We also use FAILED internally in LearnTimerExpired().  The
       * timeout handler has already called ApplicationInitNIF() and
       * disabled learn mode, which is not necessary when getting this
       * callback from serial. */
    rd_probe_lock(FALSE);
    f->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC;
    f->cmd = LEARN_MODE_SET_STATUS;
    f->seqNo = nms.seq;
    f->status = inf->bStatus;
    f->newNodeId = 0;
    f->reserved = 0;
    nm_send_reply(f, sizeof(ZW_LEARN_MODE_SET_STATUS_FRAME));
    break;
  }
}

/* Find out the smallest value (len) allowing to advertise all bits set 
 * in the bitmask (used to generate failed node list or node list)
 * For e.g. if there is 1 bit set which is in second byte, then this funciton
 * will return 2. */
uint16_t find_min_bitmask_len(uint8_t *buffer, uint16_t length)
{
    uint16_t i = 0;
    while((i < length) && (buffer[i] != 0)) {
      i++;
    }
    return i;
}

static uint16_t BuildFailedNodeListFrame(uint8_t* buffer, uint8_t seq)
{
  nodeid_t i;
  rd_node_database_entry_t *n;
  nodemask_t nlist = {0};
  uint16_t lr_len = 0;

  ZW_FAILED_NODE_LIST_REPORT_1BYTE_V4_FRAME* f = (ZW_FAILED_NODE_LIST_REPORT_1BYTE_V4_FRAME*)buffer; 
  f->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY;
  f->cmd = FAILED_NODE_LIST_REPORT;
  f->seqNo = seq;

  for (i = 1; i <= ZW_MAX_NODES; i++) //i is node id here
  {
    n = rd_node_get_raw(i);
    if (!n)
      continue;

    if (i == MyNodeID)
      continue;

    if ((n->mode & MODE_FLAGS_FAILED) &&(n->state == STATUS_FAILING)) {
        nodemask_add_node(i, nlist);
    } else if ((n->state == STATUS_PROBE_FAIL) && (RD_NODE_MODE_VALUE_GET(n) != MODE_MAILBOX)) {
       /* Mailbox nodes are likely to fail a node info cached get, but
        * other nodes should not fail a probe unless they are also failing. */
        nodemask_add_node(i, nlist);
    }
  }
  /* Calculate minimum len of failed node list bitmask that needs to be sent
   * and not whole ZW_MAX_NODES/8 */ 
  lr_len = find_min_bitmask_len(NODEMASK_GET_LR(nlist), MAX_NODEMASK_LENGTH);

  memcpy(&f->failedNodeListData1, nlist, MAX_CLASSIC_NODEMASK_LENGTH );
  f->extendedNodeidMSB = lr_len >> 8;
  f->extendedNodeidLSB = lr_len & 0xff;
  /* Copy only from ZW_LR_NODEMASK_OFFSET to upto where (len) the last byte 
   * has a bit set in extended failed node list*/
  f->extendedNodeListData1 = 0;
  if (lr_len != 0) {
    memcpy(&f->extendedNodeListData1, NODEMASK_GET_LR(nlist), lr_len);
  }
  return (sizeof(ZW_FAILED_NODE_LIST_REPORT_1BYTE_V4_FRAME) + (lr_len != 0 ? lr_len - 1:0));
}


static uint16_t BuildNodeListFrame(uint8_t * frame,uint8_t seq) {
  BYTE ver, capabilities, len, c, v;
  BYTE *nlist;
  nodeid_t i;
  int j = 0;
  ZW_NODE_LIST_REPORT_1BYTE_V4_FRAME* f = (ZW_NODE_LIST_REPORT_1BYTE_V4_FRAME*) frame;
  uint16_t lr_nodelist_len = 0;
  uint8_t lr_nodelist[MAX_LR_NODEMASK_LENGTH] = {0};

  f->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY;
  f->cmd = NODE_LIST_REPORT;
  f->seqNo = seq;
  f->nodeListControllerId = ZW_GetSUCNodeID();
  nlist = &(f->nodeListData1);
  f->status = (f->nodeListControllerId == MyNodeID) ?  NODE_LIST_REPORT_LATEST_LIST : NODE_LIST_REPORT_NO_GUARANTEE;

  SerialAPI_GetInitData(&ver, &capabilities, &len, nlist, &c, &v);
  for (i = 0; i < MAX_CLASSIC_NODEMASK_LENGTH; i++)
  {
    nlist[i] &= ~virtual_nodes_mask[i];
  }

  SerialAPI_GetLRNodeList(&lr_nodelist_len, (uint8_t *)&lr_nodelist);
  /* Calculate minimum len of node list bitmask that needs to be sent and not
   * whole ZW_MAX_NODES/8 */ 
  lr_nodelist_len = find_min_bitmask_len((uint8_t *)&lr_nodelist, MAX_LR_NODEMASK_LENGTH);
  DBG_PRINTF("LR nodelist length: 0x%02X\n", lr_nodelist_len);
  nlist[MAX_CLASSIC_NODEMASK_LENGTH] = lr_nodelist_len >> 8;
  nlist[MAX_CLASSIC_NODEMASK_LENGTH+1] = lr_nodelist_len & 0xff;
  memcpy(&nlist[MAX_CLASSIC_NODEMASK_LENGTH+2], &lr_nodelist, lr_nodelist_len);

  if (f->nodeListControllerId == 0)
  {
    c = ZW_GetControllerCapabilities();
    /*This is a non sis network and I'm a primary */
    if ((c & CONTROLLER_NODEID_SERVER_PRESENT) == 0 && (c & CONTROLLER_IS_SECONDARY) == 0)
    {
      f->status = NODE_LIST_REPORT_LATEST_LIST;
      f->nodeListControllerId = MyNodeID;
    }
  }
  //remove 2 from the length for nodeListData1 and extendednodeListData1 from
  //ZW_NODE_LIST_REPORT_1BYTE_V4_FRAME frame 
  return (MAX_CLASSIC_NODEMASK_LENGTH +
          sizeof(ZW_NODE_LIST_REPORT_1BYTE_V4_FRAME) - 2 + lr_nodelist_len); 
}


static void
NetworkUpdateCallback(BYTE bStatus)
{
  ZW_NETWORK_UPDATE_REQUEST_STATUS_FRAME *f = (ZW_NETWORK_UPDATE_REQUEST_STATUS_FRAME*) &nms.buf;

  f->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC;
  f->cmd = NETWORK_UPDATE_REQUEST_STATUS;
  f->seqNo = nms.seq;
  f->status = bStatus;
  nms.buf_len = sizeof(ZW_NETWORK_UPDATE_REQUEST_STATUS_FRAME);

  if (rd_probe_new_nodes() > 0)
  {
    SendReplyWhenNetworkIsUpdated();
  }
  else
  {
    nm_send_reply(&nms.buf, nms.buf_len);
  }
}

static void
AssignReturnRouteStatus(BYTE bStatus)
{

  ZW_RETURN_ROUTE_ASSIGN_COMPLETE_FRAME* f = (ZW_RETURN_ROUTE_ASSIGN_COMPLETE_FRAME*) &nms.buf;
 
  f->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
  f->cmd = RETURN_ROUTE_ASSIGN_COMPLETE;
  f->seqNo = nms.seq;
  f->status = bStatus;
  nm_send_reply(f, sizeof(ZW_RETURN_ROUTE_ASSIGN_COMPLETE_FRAME));
}

static void
DeleteReturnRouteStatus(BYTE bStatus)
{
  ZW_RETURN_ROUTE_DELETE_COMPLETE_FRAME *f = (ZW_RETURN_ROUTE_DELETE_COMPLETE_FRAME *) &nms.buf;
  f->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
  f->cmd = RETURN_ROUTE_DELETE_COMPLETE;
  f->seqNo = nms.seq;
  f->status = bStatus;
  nm_send_reply(f, sizeof(ZW_RETURN_ROUTE_DELETE_COMPLETE_FRAME));
}

static int BuildNodeInfoCachedReport(rd_node_database_entry_t *node,
                                     ZW_NODE_INFO_CACHED_REPORT_1BYTE_FRAME* f) {
  int len;
  uint32_t age_sec;
  u8_t status;
  u8_t security_flags;
  NODEINFO ni;

  memset(&nms.buf, 0, sizeof(nms.raw_buf));
  f->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY;
  f->cmd = NODE_INFO_CACHED_REPORT;
  f->seqNo = nms.seq;

  len = 0;
  if (node) {
    rd_ep_database_entry_t* ep = rd_ep_first(node->nodeid);

    ZW_GetNodeProtocolInfo(node->nodeid, &ni);
    security_flags = GetCacheEntryFlag(node->nodeid);
    f->properties2 = ni.capability;
    f->properties3 = ni.security;
    f->reserved = sec2_gw_node_flags2keystore_flags(security_flags & 0xFF);
    f->basicDeviceClass = ni.nodeType.basic;
    f->genericDeviceClass = ni.nodeType.generic;
    f->specificDeviceClass = ni.nodeType.specific;

    status = (node->state == STATUS_DONE ?
              NODE_INFO_CACHED_REPORT_STATUS_STATUS_OK :
              NODE_INFO_CACHED_REPORT_STATUS_STATUS_NOT_RESPONDING);
    age_sec = (clock_seconds() - node->lastUpdate);

    f->properties1 = (status << 4) | (ilog2(age_sec/60) & 0xF);

    if (ep && ep->endpoint_info && (ep->endpoint_info_len >= 2)) {
      size_t max = sizeof(nms.raw_buf) - offsetof(ZW_NODE_INFO_CACHED_REPORT_1BYTE_FRAME,
                                                  nonSecureCommandClass1);
      len = mem_insert(&f->nonSecureCommandClass1, &ep->endpoint_info[2],
                       COMMAND_CLASS_ASSOCIATION, COMMAND_CLASS_IP_ASSOCIATION,
                       ep->endpoint_info_len - 2, max);
      if (len > max) {
        assert(0);
      }
    }
  } else {
    /* We know nothing about this node. */
    f->properties1 = NODE_INFO_CACHED_REPORT_STATUS_STATUS_UNKNOWN << 4;
  }
  return len;
}



/**
 * This is where network management is actually performed.
 */
static command_handler_codes_t
NetworkManagementAction(ZW_APPLICATION_TX_BUFFER* pCmd, BYTE bDatalen)
{
//  memset(&nms.buf, 0, sizeof(nms.buf));
//  nms.buf.ZW_Common.cmdClass = pCmd->ZW_Common.cmdClass;
  switch (pCmd->ZW_Common.cmdClass)
  {
  case COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY:
    switch (pCmd->ZW_Common.cmd)
    {
    case NODE_LIST_GET:
      nm_fsm_post_event(NM_EV_REQUEST_NODE_LIST,0);
      break;
    case FAILED_NODE_LIST_GET:
      nm_fsm_post_event(NM_EV_REQUEST_FAILED_NODE_LIST,0);
      break;
    case NODE_INFO_CACHED_GET:
      {
        if(nms.state != NM_IDLE) return COMMAND_BUSY;

        ZW_NODE_INFO_CACHED_GET_V4_FRAME* get_frame = (ZW_NODE_INFO_CACHED_GET_V4_FRAME*)pCmd;
        rd_ep_database_entry_t *ep;
        uint32_t age_sec;
        uint8_t maxage_log;
        nodeid_t nid = get_frame->nodeId;
        if (nid == 0) {
          nid = MyNodeID;
        } else if (nid == 0xff) { // This is LR node, take extended node ID
          nid = (get_frame->extendedNodeIdMSB << 8) | get_frame->extendedNodeIdLSB;
        }

        if (nodemask_nodeid_is_invalid(nid)) {
          ZW_NODE_INFO_CACHED_REPORT_1BYTE_FRAME* f = (ZW_NODE_INFO_CACHED_REPORT_1BYTE_FRAME*) &nms.buf;
          int len = BuildNodeInfoCachedReport(NULL, f); // This will send NODE_INFO_CACHED_REPORT_STATUS_STATUS_UNKNOWN 
          nm_send_reply(f, 10 + len);
          break;
        }

        ep = rd_ep_first(nid);
        if (ep) {
           /* Find out if the data we have is too old */
          age_sec = clock_seconds() - ep->node->lastUpdate;
          maxage_log = (get_frame->properties1 & 0xF);
          if (!(nid == MyNodeID))
          {
             DBG_PRINTF("Seconds since last update: %i Node info cached get max age seconds:%lu\n", age_sec, ageToTime(maxage_log));
             /* If our data is too old, we have to probe again and reply asynchronously. */
             if ((maxage_log != 0xF && (age_sec > ageToTime(maxage_log))) || maxage_log == 0) {
                nms.tmp_node = nid;
                nms.state = NM_WAIT_FOR_NODE_INFO_PROBE;
                /* Make the probe start asynchronously */
                rd_probe_lock(TRUE);
                rd_register_new_node(nid, 0x00);
                rd_probe_lock(FALSE);
                return COMMAND_HANDLED;
             }
          }
        }
        /* TODO: Change NM to be able to send synchronous replies even when state is not IDLE */
        /* We can send a reply immediately. */
        ZW_NODE_INFO_CACHED_REPORT_1BYTE_FRAME* f = (ZW_NODE_INFO_CACHED_REPORT_1BYTE_FRAME*) &nms.buf;
        rd_node_database_entry_t *node = rd_get_node_dbe(nid);
        int len = BuildNodeInfoCachedReport(node, f);

        rd_free_node_dbe(node);
        nm_send_reply(f, 10 + len);
      }
      break;
    case NM_MULTI_CHANNEL_END_POINT_GET:
      {
        rd_node_database_entry_t *node_entry;
        ZW_NM_MULTI_CHANNEL_END_POINT_GET_V4_FRAME* get_frame = (ZW_NM_MULTI_CHANNEL_END_POINT_GET_V4_FRAME*)pCmd;
        ZW_NM_MULTI_CHANNEL_END_POINT_REPORT_V4_FRAME* report_frame = (ZW_NM_MULTI_CHANNEL_END_POINT_REPORT_V4_FRAME*) &nms.buf;


        if(nms.state != NM_IDLE) return COMMAND_BUSY; //TODO move into fsm;
        if (bDatalen < sizeof(ZW_NM_MULTI_CHANNEL_END_POINT_GET_FRAME))
          return COMMAND_PARSE_ERROR;

        nodeid_t nodeid;
        if (get_frame->nodeID == 0xff) {
            if (bDatalen < sizeof(ZW_NM_MULTI_CHANNEL_END_POINT_GET_V4_FRAME))
              return COMMAND_PARSE_ERROR;
            nodeid = (get_frame->extendedNodeidMSB << 8) | 
                     (get_frame->extendedNodeidLSB);
        } else {
            nodeid = get_frame->nodeID;
        }
        node_entry = rd_get_node_dbe(nodeid);
        if (!node_entry)
          return COMMAND_PARSE_ERROR;

        memset(&nms.buf, 0, sizeof(nms.buf));
        report_frame->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY;
        report_frame->cmd = NM_MULTI_CHANNEL_END_POINT_REPORT;
        report_frame->seqNo = nms.seq;
        report_frame->nodeID = get_frame->nodeID;
        if (get_frame->nodeID != 0xff) {
          report_frame->extendedNodeidMSB = 0; 
          report_frame->extendedNodeidLSB = 0; 
        } else {
          report_frame->extendedNodeidMSB = get_frame->extendedNodeidMSB; 
          report_frame->extendedNodeidLSB = get_frame->extendedNodeidLSB;
        }
        /* -1 for the root NIF */
        report_frame->individualEndPointCount = node_entry->nEndpoints - 1 - node_entry->nAggEndpoints;
        report_frame->aggregatedEndPointCount = node_entry->nAggEndpoints;
        rd_free_node_dbe(node_entry);
        nm_send_reply(report_frame, sizeof(ZW_NM_MULTI_CHANNEL_END_POINT_REPORT_V4_FRAME));
      }
      break;
    case NM_MULTI_CHANNEL_CAPABILITY_GET:
      {
        rd_ep_database_entry_t *ep_entry;
        ZW_NM_MULTI_CHANNEL_CAPABILITY_GET_V4_FRAME* get_frame = (ZW_NM_MULTI_CHANNEL_CAPABILITY_GET_V4_FRAME*)pCmd;
        ZW_NM_MULTI_CHANNEL_CAPABILITY_REPORT_V4_FRAME* report_frame = (ZW_NM_MULTI_CHANNEL_CAPABILITY_REPORT_V4_FRAME*) &nms.buf;

        if(nms.state != NM_IDLE) return COMMAND_BUSY; //TODO move into fsm;
        if (bDatalen < sizeof(ZW_NM_MULTI_CHANNEL_CAPABILITY_GET_FRAME))
          return COMMAND_PARSE_ERROR;

        nodeid_t nodeid;
        if (get_frame->nodeID == 0xff) {
            if (bDatalen < sizeof(ZW_NM_MULTI_CHANNEL_CAPABILITY_GET_V4_FRAME))
              return COMMAND_PARSE_ERROR;
            nodeid = (get_frame->extendedNodeidMSB << 8) | 
                     (get_frame->extendedNodeidLSB);
        } else {
            nodeid = get_frame->nodeID;
        }
        ep_entry = rd_get_ep(nodeid, get_frame->endpoint & 0x7F);

        if (NULL == ep_entry)
        {
          return COMMAND_PARSE_ERROR;
        }

        memset(&nms.buf, 0, sizeof(nms.buf));
        report_frame->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY;
        report_frame->cmd = NM_MULTI_CHANNEL_CAPABILITY_REPORT;
        report_frame->seqNo = nms.seq;
        report_frame->nodeID = get_frame->nodeID;
        report_frame->commandClassLength = ep_entry->endpoint_info_len - 2;
        report_frame->endpoint = get_frame->endpoint & 0x7F;
        memcpy(&report_frame->genericDeviceClass, ep_entry->endpoint_info, ep_entry->endpoint_info_len);
        if (get_frame->nodeID == 0xff) {
          memcpy(&report_frame->genericDeviceClass+ep_entry->endpoint_info_len, &get_frame->extendedNodeidMSB, 1);
          memcpy(&report_frame->genericDeviceClass+ep_entry->endpoint_info_len+1, &get_frame->extendedNodeidLSB, 1);
        } else {
          memset(&report_frame->genericDeviceClass+ep_entry->endpoint_info_len, 0, 1);
          memset(&report_frame->genericDeviceClass+ep_entry->endpoint_info_len+1, 0, 1);
        }

        nm_send_reply(report_frame, sizeof(ZW_NM_MULTI_CHANNEL_CAPABILITY_REPORT_V4_FRAME) + ep_entry->endpoint_info_len);
      }
      break;
    case NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_GET:
      {
        rd_ep_database_entry_t *ep_entry;
        ZW_NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_GET_V4_FRAME* get_frame = (ZW_NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_GET_V4_FRAME*)pCmd;
        ZW_NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_REPORT_V4_FRAME* report_frame = (ZW_NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_REPORT_V4_FRAME*) &nms.buf;

        if(nms.state != NM_IDLE) return COMMAND_BUSY; //TODO move into fsm;
        if (bDatalen < sizeof(ZW_NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_GET_FRAME))
          return COMMAND_PARSE_ERROR;

        nodeid_t nodeid;
        if (get_frame->nodeID == 0xff) {
            if (bDatalen <
                  sizeof(ZW_NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_GET_V4_FRAME))
              return COMMAND_PARSE_ERROR;
            nodeid = (get_frame->extendedNodeidMSB << 8) | 
                     (get_frame->extendedNodeidLSB);
        } else {
            nodeid = get_frame->nodeID;
        }
        ep_entry = rd_get_ep(nodeid, get_frame->aggregatedEndpoint & 0x7F);
        if (NULL == ep_entry || 0 == ep_entry->endpoint_aggr_len)
        {
          return COMMAND_PARSE_ERROR;
        }

        report_frame->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY;
        report_frame->cmd = NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_REPORT;
        report_frame->seqNo = nms.seq;
        report_frame->nodeID = get_frame->nodeID;
        report_frame->aggregatedEndpoint = get_frame->aggregatedEndpoint & 0x7F;
        report_frame->memberCount = ep_entry->endpoint_aggr_len;
        memcpy(&report_frame->memberEndpoint1, ep_entry->endpoint_agg, ep_entry->endpoint_aggr_len);
        if (get_frame->nodeID == 0xff) {
          memcpy(&report_frame->memberEndpoint1+ep_entry->endpoint_aggr_len, &get_frame->extendedNodeidMSB, 1);
          memcpy(&report_frame->memberEndpoint1+ep_entry->endpoint_aggr_len+1, &get_frame->extendedNodeidLSB, 1);
        } else {
          memset(&report_frame->memberEndpoint1+ep_entry->endpoint_aggr_len, 0, 1);
          memset(&report_frame->memberEndpoint1+ep_entry->endpoint_aggr_len+1, 0, 1);
        }
        nm_send_reply(report_frame, sizeof(ZW_NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_REPORT_V4_FRAME) + ep_entry->endpoint_aggr_len);
      }
      break;
    default:
      return COMMAND_NOT_SUPPORTED;
    }

    break;
  case COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC:
    switch (pCmd->ZW_Common.cmd)
    {
    case DEFAULT_SET:
      if(nms.state != NM_IDLE) {
         /* nms.conn points to the right zip client, otherwise we would be rejected in NetworkManagementCommandHandler() instead */
         NetworkManagementReturnFail(&nms.conn, pCmd, bDatalen);
         break;
      }

      DBG_PRINTF("Setting default\n");
      IndicatorDefaultSet(); /* Reset the indicator */
      nms.state = NM_SET_DEFAULT;
      ctimer_set(&nms.timer, 15 * CLOCK_SECOND, timeout, 0);
      ZW_SetDefault(SetDefaultStatus);
      break;
    case LEARN_MODE_SET:
      nm_fsm_post_event(NM_EV_LEARN_SET,pCmd);
      break;
    case NODE_INFORMATION_SEND:
        /* If the the Gateway is asked to send a broadcast NIF,it will report the supported command classes
         * depending on the granted security class:
         * ->If the Gateway is granted S2 keys, the Gateway will advertise the command classes
         *   that are supported non securely.
         * ->If the Gateway is granted no key, the Gateway will advertise non secure plus
         *   net scheme (“highest granted key”) supported command classes.
         */
      if(nms.state != NM_IDLE) return COMMAND_BUSY; //TODO move into fsm;

      ZW_SendNodeInformation(pCmd->ZW_NodeInformationSendFrame.destinationNodeId,
          pCmd->ZW_NodeInformationSendFrame.txOptions, __ResetState);
      nms.state = NM_SENDING_NODE_INFO;
      break;
    case NETWORK_UPDATE_REQUEST:
      if(nms.state != NM_IDLE) return COMMAND_BUSY; //TODO move into fsm;

      if (ZW_RequestNetWorkUpdate(NetworkUpdateCallback))
      {
        /* Asking SUC/SIS. */
        nms.state = NM_NETWORK_UPDATE;
      }
      else
      {
        /*I'm the SUC/SIS or i don't know the SUC/SIS*/
        if (ZW_GetSUCNodeID() > 0)
        {
          NetworkUpdateCallback(ZW_SUC_UPDATE_DONE);
        }
        else
        {
          NetworkUpdateCallback(ZW_SUC_UPDATE_DISABLED);
        }
      }
      break;
    case DSK_GET:
    {
      ZW_APPLICATION_TX_BUFFER dsk_get_buf;
      ZW_DSK_RAPORT_FRAME_EX* f = (ZW_DSK_RAPORT_FRAME_EX*)&dsk_get_buf;
      uint8_t priv_key[32];
      int i;
      f->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC;
      f->cmd = DSK_RAPORT;
      f->seqNo = ((uint8_t*)pCmd)[2];
      f->add_mode = ((uint8_t*)pCmd)[3] & DSK_GET_ADD_MODE_BIT;

      memset(priv_key,0,sizeof(priv_key));

      gw_keystore_private_key_read(priv_key, (f->add_mode & DSK_GET_ADD_MODE_BIT)>0 );
      crypto_scalarmult_curve25519_base(f->dsk,priv_key);
      memset(priv_key,0,sizeof(priv_key));

      ZW_SendDataZIP(&nms.conn,f,sizeof(ZW_DSK_RAPORT_FRAME_EX),0);
    }
      break;
    default:
      return COMMAND_NOT_SUPPORTED;
    }
    break;
  case COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION:
    /*If there is neither a SIS or we are primary controller, we cannot perform inclusion*/
    if (!((ZW_GetControllerCapabilities() & CONTROLLER_NODEID_SERVER_PRESENT) || ZW_IsPrimaryCtrl()))
      return COMMAND_NOT_SUPPORTED;

    switch (pCmd->ZW_Common.cmd)
    {
    case NODE_ADD:
      {
        uint8_t mode = ADD_NODE_ANY;

       // SiLabs patch ZGW-3368
       /* Enable ADD_NODE_OPTION_SFLND when inclusion will take more than 60 seconds.
        * The timeout of S2 inclusion on the device is 65 seconds however this doesn't
        * take into account the S2 protocol and devices have been found that timeout at
        * 63.7 seconds. */

       if(should_skip_flirs_nodes_be_used()) {
          mode |= ADD_NODE_OPTION_SFLND;
       }

        if (!(pCmd->ZW_NodeAddFrame.txOptions & TRANSMIT_OPTION_LOW_POWER))
        {
          mode |= ADD_NODE_OPTION_NORMAL_POWER;
        }

        if (pCmd->ZW_NodeAddFrame.txOptions & TRANSMIT_OPTION_EXPLORE)
        {
          mode |= ADD_NODE_OPTION_NETWORK_WIDE;
        }

        nms.txOptions = TRANSMIT_OPTION_ACK | TRANSMIT_OPTION_AUTO_ROUTE | pCmd->ZW_NodeAddFrame.txOptions;

        if (pCmd->ZW_NodeAddFrame.mode == ADD_NODE_STOP)
        {
          DBG_PRINTF("Add node stop\n");
          nm_fsm_post_event(NM_NODE_ADD_STOP, 0);
        }
        else if (pCmd->ZW_NodeAddFrame.mode == ADD_NODE_ANY)
        {
          nm_fsm_post_event(NM_EV_NODE_ADD, &mode);
        }
        else if (pCmd->ZW_NodeAddFrame.mode == ADD_NODE_ANY_S2)
        {
          nm_fsm_post_event(NM_EV_NODE_ADD_S2, &mode);
        }
      }
      break;
    case NODE_REMOVE:
      if(nms.state != NM_IDLE && nms.state != NM_WAITING_FOR_NODE_REMOVAL) return COMMAND_BUSY; //TODO move into fsm;

      if (pCmd->ZW_NodeRemoveFrame.mode == REMOVE_NODE_STOP)
      {
        RemoveTimerExpired();
      }
      else
      {
        ZW_RemoveNodeFromNetwork(pCmd->ZW_NodeRemoveFrame.mode, RemoveNodeStatusUpdate);
        nms.state = NM_WAITING_FOR_NODE_REMOVAL;
      }

      break;
    case FAILED_NODE_REMOVE:
      if(nms.state != NM_IDLE) return COMMAND_BUSY; //TODO move into fsm;

      nms.state = NM_WAITING_FOR_FAIL_NODE_REMOVAL;
      ZW_FAILED_NODE_REMOVE_V4_FRAME* get_frame = (ZW_FAILED_NODE_REMOVE_V4_FRAME*)pCmd;
      if (get_frame->nodeId == 0xff) {
        nms.tmp_node = get_frame->extendedNodeIdMSB << 8;
        nms.tmp_node |= get_frame->extendedNodeIdLSB;
      } else {
        nms.tmp_node = get_frame->nodeId;
      }
      BYTE ret = 0;
      ret = ZW_RemoveFailedNode(nms.tmp_node,
                                RemoveFailedNodeStatus);
      if (ret == ZW_FAILED_NODE_NOT_FOUND) {
          DBG_PRINTF("Node is not in failed node list. So can not be failed removed\n");
          RemoveFailedNodeStatus(ZW_FAILED_NODE_NOT_FOUND);
      } else if (ret != ZW_FAILED_NODE_REMOVE_STARTED) {
            RemoveFailedNodeStatus(ZW_FAILED_NODE_NOT_REMOVED);
      }
      break;
    case FAILED_NODE_REPLACE:
      {
        ZW_FAILED_NODE_REPLACE_FRAME* f = (ZW_FAILED_NODE_REPLACE_FRAME*) pCmd;

        if (f->mode == START_FAILED_NODE_REPLACE)
        {
          nm_fsm_post_event(NM_EV_REPLACE_FAILED_START,pCmd);
        } else if(f->mode == START_FAILED_NODE_REPLACE_S2)
        {
          nm_fsm_post_event(NM_EV_REPLACE_FAILED_START_S2,pCmd);
        }
        else if (f->mode == STOP_FAILED_NODE_REPLACE)
        {
          nm_fsm_post_event(NM_EV_REPLACE_FAILED_STOP,pCmd);
        }
      }
      break;
    case NODE_NEIGHBOR_UPDATE_REQUEST:
      {
        if(nms.state != NM_IDLE) return COMMAND_BUSY; //TODO move into fsm;

        ZW_NODE_NEIGHBOR_UPDATE_REQUEST_FRAME *f = (ZW_NODE_NEIGHBOR_UPDATE_REQUEST_FRAME *) pCmd;
        nms.state = NM_WAITING_FOR_NODE_NEIGH_UPDATE;
        /* Using a timeout here is a workaround for a defect in the protocol
         * (see SWPROT-3666) causing it to not always activate the
         * RequestNodeNeighborUpdateStatus callback when the network has FLIRS
         * devices.
         */
        clock_time_t tout = rd_calculate_inclusion_timeout(TRUE);
        ctimer_set(&nms.timer, tout, timeout, 0);
        ZW_RequestNodeNeighborUpdate(f->nodeId, RequestNodeNeighborUpdateStatus);
      }
      break;
    case RETURN_ROUTE_ASSIGN:
      {
        if(nms.state != NM_IDLE) return COMMAND_BUSY; //TODO move into fsm;

        ZW_RETURN_ROUTE_ASSIGN_FRAME *f = (ZW_RETURN_ROUTE_ASSIGN_FRAME*) pCmd;
        nms.state = NM_WAITING_FOR_RETURN_ROUTE_ASSIGN;
        if ((f->destinationNodeId == MyNodeID) && (ZW_GetSUCNodeID() == MyNodeID)) {
          DBG_PRINTF("Assign route is from ZIP Gateway node id and ZIP Gateway is also the SUC. Sending Assign SUC return route to %d as well\n", f->sourceNodeId);
          if(!ZW_AssignSUCReturnRoute(f->sourceNodeId, AssignReturnRouteStatus))
          {
            AssignReturnRouteStatus(TRANSMIT_COMPLETE_FAIL);
          }
        } else {
          DBG_PRINTF("Sending assign return route to %d\n", f->sourceNodeId);
          if (!ZW_AssignReturnRoute(f->sourceNodeId, f->destinationNodeId, AssignReturnRouteStatus))
          {
            AssignReturnRouteStatus(TRANSMIT_COMPLETE_FAIL);
          }
        }
     }
      break;
    case RETURN_ROUTE_DELETE:
      {
        if(nms.state != NM_IDLE) return COMMAND_BUSY; //TODO move into fsm;

        ZW_RETURN_ROUTE_DELETE_FRAME *f = (ZW_RETURN_ROUTE_DELETE_FRAME*) pCmd;
        nms.state = NM_WAITING_FOR_RETURN_ROUTE_DELETE;
        if(ZW_DeleteReturnRoute(f->nodeId, DeleteReturnRouteStatus) != TRUE) {
           DeleteReturnRouteStatus(TRANSMIT_COMPLETE_FAIL);
        }
      }
      break;
    case NODE_ADD_KEYS_SET:
      nm_fsm_post_event(NM_EV_ADD_SECURITY_KEYS_SET,pCmd);
    break;
    case NODE_ADD_DSK_SET:
      nm_fsm_post_event(NM_EV_ADD_SECURITY_DSK_SET,pCmd);
      break;
    } /* switch(command in COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION) */
    break;

  default:
    return CLASS_NOT_SUPPORTED;
  } /* switch(COMMAND_CLASS)*/
  return COMMAND_HANDLED;
}

/**
 * Return a appropriate failure code to sender.
 */
static void 
NetworkManagementReturnFail(zwave_connection_t* c, const ZW_APPLICATION_TX_BUFFER* pCmd, BYTE bDatalen)
{
  BYTE len = 0;
  ZW_APPLICATION_TX_BUFFER buf;
  memset(&buf, 0, sizeof(buf));
  buf.ZW_Common.cmdClass = pCmd->ZW_Common.cmdClass;
  buf.ZW_NodeAddFrame.seqNo = pCmd->ZW_NodeAddFrame.seqNo;

  /*Special cases where we have some error code */
  switch (pCmd->ZW_Common.cmdClass)
  {
  case COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC:
    switch (pCmd->ZW_Common.cmd)
    {
    case DEFAULT_SET:
      buf.ZW_DefaultSetCompleteFrame.cmd = DEFAULT_SET_COMPLETE;
      buf.ZW_DefaultSetCompleteFrame.status = DEFAULT_SET_BUSY;
      len = sizeof(buf.ZW_DefaultSetCompleteFrame);
      break;
    case LEARN_MODE_SET:
      buf.ZW_LearnModeSetStatusFrame.cmd = LEARN_MODE_SET_STATUS;
      buf.ZW_LearnModeSetStatusFrame.status = LEARN_MODE_FAILED;
      len = sizeof(buf.ZW_LearnModeSetStatusFrame);
      break;
    case NETWORK_UPDATE_REQUEST:
      {
        ZW_NETWORK_UPDATE_REQUEST_STATUS_FRAME *f = (ZW_NETWORK_UPDATE_REQUEST_STATUS_FRAME*) &buf;
        f->cmd = NETWORK_UPDATE_REQUEST_STATUS;
        f->status = ZW_SUC_UPDATE_ABORT;
        len = sizeof(ZW_NETWORK_UPDATE_REQUEST_STATUS_FRAME);
      }
      break;
    }
    break;
  case COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION:
    switch (pCmd->ZW_Common.cmd)
    {
    case NODE_ADD:
      buf.ZW_NodeAddStatus1byteFrame.cmd = NODE_ADD_STATUS;
      buf.ZW_NodeAddStatus1byteFrame.status = ADD_NODE_STATUS_FAILED;
      len = sizeof(buf.ZW_NodeAddStatus1byteFrame) - 1;
      break;
    case NODE_REMOVE:
      {
         ZW_NODE_REMOVE_STATUS_V4_FRAME* r = (ZW_NODE_REMOVE_STATUS_V4_FRAME*) &buf;
         r->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
         r->cmd = NODE_REMOVE_STATUS;
         r->status = REMOVE_NODE_STATUS_FAILED;
         len = sizeof(ZW_NODE_REMOVE_STATUS_V4_FRAME);
      }
      break;
    case FAILED_NODE_REMOVE:
      {
        ZW_FAILED_NODE_REMOVE_STATUS_V4_FRAME* f = (ZW_FAILED_NODE_REMOVE_STATUS_V4_FRAME*) &buf;
        f->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
        f->cmd = FAILED_NODE_REMOVE_STATUS;
        f->status = ZW_FAILED_NODE_NOT_REMOVED;
        len = sizeof(ZW_FAILED_NODE_REMOVE_STATUS_V4_FRAME);
      }
      break;
    case FAILED_NODE_REPLACE:
      buf.ZW_FailedNodeReplaceStatusFrame.cmd =
      FAILED_NODE_REPLACE_STATUS;
      buf.ZW_FailedNodeReplaceStatusFrame.status =
      ZW_FAILED_NODE_REPLACE_FAILED;
      len = sizeof(buf.ZW_FailedNodeReplaceStatusFrame);
      break;
    case NODE_NEIGHBOR_UPDATE_REQUEST:
      {
        ZW_NODE_NEIGHBOR_UPDATE_STATUS_FRAME* f = (ZW_NODE_NEIGHBOR_UPDATE_STATUS_FRAME*) &buf;
        f->cmd = NODE_NEIGHBOR_UPDATE_STATUS;
        f->status = REQUEST_NEIGHBOR_UPDATE_FAILED;
        len = sizeof(ZW_NODE_NEIGHBOR_UPDATE_STATUS_FRAME);
      }
      break;
    }

    break;
  }


  if (len==0)
  {
    buf.ZW_ApplicationBusyFrame.cmdClass = COMMAND_CLASS_APPLICATION_STATUS;
    buf.ZW_ApplicationBusyFrame.cmd = APPLICATION_BUSY;
    buf.ZW_ApplicationBusyFrame.status = APPLICATION_BUSY_TRY_AGAIN_LATER;
    buf.ZW_ApplicationBusyFrame.waitTime = 0;
    len = sizeof(buf.ZW_ApplicationBusyFrame);
  }

  ZW_SendDataZIP(c, (BYTE*) &buf, len, 0);
}

/**
 * Command handler for network management commands
 */
static command_handler_codes_t
NetworkManagementCommandHandler(zwave_connection_t *conn, BYTE* pData, uint16_t bDatalen)
{
  mb_abort_sending();

  if(NetworkManagement_queue_if_target_is_mailbox(conn,(ZW_APPLICATION_TX_BUFFER*)pData, bDatalen)) {
    return COMMAND_POSTPONED; 
  } else {
    return NetworkManagementCommandHandler_internal(conn,pData,bDatalen);
  }
}

command_handler_codes_t
NetworkManagementCommandHandler_internal(zwave_connection_t *c, BYTE* pData, uint16_t bDatalen)
{
  ZW_APPLICATION_TX_BUFFER* pCmd = (ZW_APPLICATION_TX_BUFFER*) pData;

  DBG_PRINTF("NetworkManagementCommandHandler %x %x\n",pData[0], pData[1]);


   if(get_queue_state() != QS_IDLE) {
     ClassicZIPNode_AbortSending();
   }

   /*FIXME, we could argue that we should not have the second check, in that case it would only
   * be the unsolicited destination, which could accept the inclusion request */
  if(nms.state == NM_IDLE) {
    /* Save info about who we are talking with */
    nms.conn = *c;

    /*Keep the sequence nr which is common for all network management commands*/
    nms.seq = pCmd->ZW_NodeAddFrame.seqNo;
  } else if (uip_ipaddr_cmp(&c->ripaddr, &nms.conn.ripaddr) && (c->rport == nms.conn.rport))
  {
    //Allow
  } else {
    LOG_PRINTF("Another network management session (%dx.%dx) is in progress (%dx.%dx)\n", nms.class, nms.cmd,
        pCmd->ZW_Common.cmdClass, pCmd->ZW_Common.cmd);
    /* Return the proper failure code accoding the the request */
    goto send_fail;
  }

  return NetworkManagementAction((ZW_APPLICATION_TX_BUFFER*) pData, bDatalen);


send_fail:
  NetworkManagementReturnFail(c, pCmd, bDatalen);
  return COMMAND_HANDLED; /*TODO Busy might be more appropriate*/
}

uip_ipaddr_t*
NetworkManagement_getpeer()
{
  return &nms.conn.ripaddr;
}

/** Return 0 when init is pending on DHCP assignment,
 * 1 when init is complete */
int NetworkManagement_Init()
{
//  uip_ds6_route_t* a;

  if (network_management_init_done)
    return 1;

  ZW_LTimerCancel(nms.networkManagementTimer);
  nms.networkManagementTimer = 0xFF;
  DBG_PRINTF("NM Init\n");

  waiting_for_middleware_probe = FALSE;

  /*Make sure that the controller is not in any odd state */
  network_management_init_done = 1;
  return 1;
}

/* Replace all occurences of 'old' with 'new' in buffer buf */
void
mem_replace(unsigned char *buf, char old, char new, size_t len)
{
  char *p;
  while ((p = memchr(buf, old, len)) != NULL)
  {
    *p = new;
  }
}

/* Insert character 'add' after 'find', return the new length */
size_t
mem_insert(u8_t* dst, const u8_t *src, u8_t find, u8_t add, size_t len, size_t max)
{
  size_t m = max;
  size_t k = 0;

  while ((len--) && (max--) && (k < m)) //Do not write beyound max
  {
    if (*src == find)
    {
      *dst++ = *src++;
      k++;
      if (k >= m) { //if we are going beyond max we should return right away
         return k;
      }

      *dst++ = add;
      k++;
    }
    else
    {
      *dst++ = *src++;
      k++;
    }
  }

  return k;
}

/**
 * Get the state of the network management module
 * @return
 */
nm_state_t
NetworkManagement_getState()
{
  return nms.state;
}

bool NetworkManagement_idle(void) {
   return ((NetworkManagement_getState() == NM_IDLE)
           && !waiting_for_middleware_probe);
}

BOOL
NetworkManagement_is_Unsolicited2_peer()
{
  return (nms.cmd && uip_ipaddr_cmp(&cfg.unsolicited_dest2, &nms.conn.ripaddr)
      && cfg.unsolicited_port2 == UIP_HTONS(nms.conn.rport));
}

BOOL
NetworkManagement_is_Unsolicited_peer()
{
  return (nms.cmd && uip_ipaddr_cmp(&cfg.unsolicited_dest, &nms.conn.ripaddr)
      && cfg.unsolicited_port == UIP_HTONS(nms.conn.rport));
}

void NetworkManagement_SendFailedNodeList_To_Unsolicited()
{
  uint8_t buf[sizeof(ZW_FAILED_NODE_LIST_REPORT_1BYTE_V4_FRAME) + sizeof(nodemask_t)];  
  uint16_t len;

  len = BuildFailedNodeListFrame((uint8_t *)&buf, random_rand() & 0xFF);
  send_to_both_unsoc_dest( (uint8_t*)&buf,len,0);
  return;
}

BOOL NetworkManagement_SendNodeList_To_Unsolicited()
{
  uint8_t buf[sizeof(ZW_FAILED_NODE_LIST_REPORT_1BYTE_V4_FRAME) + sizeof(nodemask_t)];  
  uint16_t len;
  
  /* 
   * ZGW-2556: We will not send a nodelist to the unsolicited destination 
   * if we are still in doing something. This is because sending the nodelist
   * may trigger other networkmanagement actions, which will fail is our FSM is
   * not idle.
   */
  if(nms.state != NM_IDLE) {
    return FALSE;
  }

  len = BuildNodeListFrame((uint8_t *)&buf, random_rand() & 0xFF);
  send_to_both_unsoc_dest( (uint8_t*)&buf, len, 0);
  
  return TRUE;
}


/**
 * Send to both unsolicited destinations. This is done without ACK.
 */
void send_to_both_unsoc_dest(const uint8_t * frame, uint16_t len, ZW_SendDataAppl_Callback_t cbFunc)
{
  zwave_connection_t uconn;

  memset(&uconn,0,sizeof(zwave_connection_t));

  if(set_conn_to_unsol1( &uconn )) {
      ZW_SendDataZIP(&uconn, frame, len, NULL);
      DBG_PRINTF("Sending 0x%2.2x%2.2x to Unsolicited Destination 1\n", frame[0], frame[1]);
  }
  if(set_conn_to_unsol2( &uconn )) {
      ZW_SendDataZIP(&uconn, frame, len, NULL);
      DBG_PRINTF("Sending 0x%2.2x%2.2x to Unsolicited Destination 2\n", frame[0], frame[1]);
  }

  if (uip_is_addr_unspecified(&cfg.unsolicited_dest)
      && uip_is_addr_unspecified(&cfg.unsolicited_dest2)) {
     DBG_PRINTF("No Unsolicited Destination, not sending frame 0x%2.2x%2.2x of length %d\n",
                frame[0], frame[1], len);
  }

  /*Just fake the callback */
  if(cbFunc) {
    cbFunc(TRANSMIT_COMPLETE_OK,0,0);
  }
}

void
NetworkManagement_smart_start_inclusion(uint8_t inclusion_options, uint8_t *smart_start_homeID, bool is_lr_smartstart_prime)
{
  struct node_add_smart_start_event_data ev_data;
  ev_data.smart_start_homeID = smart_start_homeID;
  ev_data.inclusion_options = inclusion_options;

  struct provision * entry = provisioning_list_dev_get_homeid(ev_data.smart_start_homeID);

  if ((entry) && (entry->status == PVS_STATUS_PENDING))
  { 
    if (entry->bootmode == PVS_BOOTMODE_LONG_RANGE_SMART_START) {
      if (is_lr_smartstart_prime == false) {
        return;
      }
      ev_data.inclusion_options |= ADD_NODE_OPTION_LR;
    }
   /* Enable ADD_NODE_OPTION_SFLND when there are more than 20 FLIRS nodes
    * in the network. SiLabs patch ZGW-3368 */
   if (should_skip_flirs_nodes_be_used()) {
     ev_data.inclusion_options |= ADD_NODE_OPTION_SFLND;
   }
    nm_fsm_post_event(NM_EV_NODE_ADD_SMART_START, &ev_data);
  }

  else
  {
     LOG_PRINTF("Reject smart start inclusion - node 0x%02x%02x%02x%02x not in provisioning list or is not pending\n",
                ev_data.smart_start_homeID[0], ev_data.smart_start_homeID[1],
                ev_data.smart_start_homeID[2], ev_data.smart_start_homeID[3]);
  }
}

void NetworkManagement_smart_start_init_if_pending() {
   if ((cfg.enable_smart_start==0)
       /* Middleware is probing a previously node. */
       || waiting_for_middleware_probe
       /* Network Management is busy, eg, already adding a node. */
       || (nms.state != NM_IDLE)) {
      return;
   }
   if (provisioning_list_pending_count() > 0) {
     // SiLabs patch ZGW-3368
     uint8_t mode = ADD_NODE_SMART_START | ADD_NODE_OPTION_NETWORK_WIDE;
     /* Enable ADD_NODE_OPTION_SFLND when there are more than 20 FLIRS nodes
      * in the network */
     if(should_skip_flirs_nodes_be_used()) {
        mode |= ADD_NODE_OPTION_SFLND;
     }
     ZW_AddNodeToNetwork(mode, AddNodeStatusUpdate);

   } else {
      ZW_AddNodeToNetwork(ADD_NODE_STOP, NULL);
   }
}

void NetworkManagement_INIF_Received(nodeid_t bNodeID, uint8_t INIF_rxStatus,
    uint8_t *INIF_NWI_homeid)
{
  ZW_INCLUDED_NIF_REPORT_FRAME_EX inif;
  char buf[512];

  if (!(INIF_rxStatus & RECEIVE_STATUS_FOREIGN_HOMEID))
  {
     DBG_PRINTF("Ignoring INIF recvd with our homde id\n");
     return;
  }
  struct provision *ple = provisioning_list_dev_get_homeid(INIF_NWI_homeid);
  if(ple) {
    inif.cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
    inif.cmd = COMMAND_INCLUDED_NIF_REPORT;
    inif.seqNo = random_rand() & 0xFF;
    inif.reserved_len = ple->dsk_len & 0x1F;

    memcpy(&inif.dsk, ple->dsk, sizeof(inif.dsk));
    send_to_both_unsoc_dest((uint8_t*)&inif, sizeof inif, NULL);

    int  len = CC_provisioning_list_build_report((uint8_t*)buf,ple,rand() & 0xFF);
    send_to_both_unsoc_dest( (uint8_t*)buf, len, NULL);
  } else {
    LOG_PRINTF("INIF received but item is not in provisioning list HOMEID %4X\n", *((int32_t*)INIF_NWI_homeid));
  }
}

static void
middleware_probe_timeout(void* none)
{
  waiting_for_middleware_probe = FALSE;
  if (nms.state == NM_IDLE) {
     process_post(&zip_process, ZIP_EVENT_COMPONENT_DONE, &extend_middleware_probe_timeout);
  }
  NetworkManagement_smart_start_init_if_pending();
}

void extend_middleware_probe_timeout(void)
{
  if (waiting_for_middleware_probe) {
    ctimer_set(&ss_timer, SMART_START_MIDDLEWARE_PROBE_TIMEOUT, middleware_probe_timeout, 0);
  }
}

nodeid_t NM_get_newly_included_nodeid()
{
  return nm_newly_included_ss_nodeid;
}

/**
 * Signal inclusion done to middleware, start waiting for middleware probe to complete.
 *
 * We need to avoid starting the next Smart Start inclusion while the middleware is still
 * in the process of probing the previous smart start included node. In the absence of an explicit
 * handshaking mechanism, we use a heuristic to infer when the probe is finished: We wait
 * until nothing has been sent to the newly included node for
 * #SMART_START_MIDDLEWARE_PROBE_TIMEOUT seconds.  Then we re-enable
 * Smart Start Add Mode.
 */
static void
wait_for_middleware_probe(BYTE dummy, void* user, TX_STATUS_TYPE *t)
{
  waiting_for_middleware_probe = TRUE;
  extend_middleware_probe_timeout();
  ResetState(0, 0, NULL); /* Have to reset at this point so middleware can probe */
}

REGISTER_HANDLER(NetworkManagementCommandHandler, 0, COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC, NETWORK_MANAGEMENT_BASIC_VERSION_V2, SECURITY_SCHEME_0);
REGISTER_HANDLER(NetworkManagementCommandHandler, 0, COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY, NETWORK_MANAGEMENT_PROXY_VERSION_V4, SECURITY_SCHEME_0);
REGISTER_HANDLER(NetworkManagementCommandHandler, 0, COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION, NETWORK_MANAGEMENT_INCLUSION_VERSION_V4, SECURITY_SCHEME_0);
