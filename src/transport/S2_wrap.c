/* © 2017 Silicon Laboratories Inc.
 */
/*
 * S2_wrap.c
 *
 *  Created on: Aug 25, 2015
 *      Author: aes
 */
#include "ZIP_Router.h" /* ApplicationCommandHandlerZIP */

#undef BYTE
#undef UNUSED
#define x86

#include "NodeCache.h"
#include "S2.h"
#include "s2_inclusion.h"
#include "s2_keystore.h"
#include "s2_classcmd.h"

#include "TYPES.H"
#include "ZW_classcmd.h"
#include "S2_wrap.h"
#include "security_layer.h"
#include "ctimer.h"
#include "curve25519.h"
#include "zw_network_info.h" /* MyNodeID */
#include "ZW_ZIPApplication.h" /* SecureClasses */
#include "CC_NetworkManagement.h"
#include "Mailbox.h"
#include "CommandAnalyzer.h"
#include "s2_protocol.h"
#include <stdarg.h>
#include <stdio.h>
#include "ZIP_Router_logging.h"
#include "DataStore.h"
#include "random.h"
#ifdef TEST_MULTICAST_TX
#include "multicast_group_manager.h"
//#include "multicast_tlv.h"
#endif

#define S2_NUM_KEY_CLASSES (s2_get_key_count()) /* S0 key already included*/

/* Verify Delivery timeout in end nodes. Current 500 and 700 series nodes have a fixed timeout.
 * If that changes, this timeout must be calculated in a more sophisticated way. Unit: milliseconds. */
#define END_NODE_VERIFY_DELIVERY_TIMEOUT 500

extern u8_t send_data(ts_param_t* p, const u8_t* data, u16_t len,ZW_SendDataAppl_Callback_t cb,void* user);
extern void print_hex(uint8_t* buf, int len);


struct S2* s2_ctx;

/**
 * Multicast transmission
 */
struct {
  uint8_t l_node;
  uint8_t r_node;
  const uint8_t *dest_nodemask;
  const uint8_t *data;
  uint8_t  data_len;
  security_class_t s2_class;
  uint8_t s2_groupd_id;
  enum {
    MC_SEND_STATE_IDLE,
    MC_SEND_STATE_SEND_FIRST,
    MC_SEND_STATE_SEND,
    MC_SEND_STATE_SEND_FIRST_NOFOLLOWUP,
    MC_SEND_STATE_ABORT
  } state;
} mc_state;


static ZW_SendDataAppl_Callback_t s2_send_callback;
static sec2_inclusion_cb_t sec_incl_cb;
/* Holds the TX_STATUS_TYPE of ZW_SendDataXX() callback for the most recent S2 frame */
static TX_STATUS_TYPE s2_send_tx_status;

static void* s2_send_user;
static struct ctimer s2_timer;
static struct ctimer s2_inclusion_timer;
clock_time_t transmit_start_time;

static uint8_t
keystore_flags_2_node_flags(uint8_t key_store_flags)
{
  uint8_t flags =0;
  if (key_store_flags & KEY_CLASS_S0)
  {
    flags |= NODE_FLAG_SECURITY0;
  }
  if (key_store_flags & KEY_CLASS_S2_ACCESS)
  {
    flags |= NODE_FLAG_SECURITY2_ACCESS;
  }
  if (key_store_flags & KEY_CLASS_S2_AUTHENTICATED)
  {
    flags |= NODE_FLAG_SECURITY2_AUTHENTICATED;
  }
  if (key_store_flags & KEY_CLASS_S2_UNAUTHENTICATED)
  {
    flags |= NODE_FLAG_SECURITY2_UNAUTHENTICATED;
  }

  return flags;
}

void keystore_network_generate_key_if_missing()
{
  uint8_t key_store_flags;
  nvm_config_get(assigned_keys,&key_store_flags);
  uint8_t net_key[16];

  if ((key_store_flags & KEY_CLASS_S0) == 0)
  {
    DBG_PRINTF("KEY_CLASS_S0 was missing. Generating new.\n");
    S2_get_hw_random(net_key,16);
    print_hex(net_key,16);
    keystore_network_key_write(KEY_CLASS_S0, net_key);
  }
  if ((key_store_flags & KEY_CLASS_S2_ACCESS) == 0)
  {
    DBG_PRINTF("KEY_CLASS_S2_ACCESS was missing. Generating new.\n");
    S2_get_hw_random(net_key,16);
    print_hex(net_key,16);
    keystore_network_key_write(KEY_CLASS_S2_ACCESS, net_key);
  }
  if ((key_store_flags & KEY_CLASS_S2_AUTHENTICATED) == 0)
  {
    DBG_PRINTF("KEY_CLASS_S2_AUTHENTICATED was missing. Generating new.\n");
    S2_get_hw_random(net_key,16);
    print_hex(net_key,16);
    keystore_network_key_write(KEY_CLASS_S2_AUTHENTICATED, net_key);
  }
  if ((key_store_flags & KEY_CLASS_S2_UNAUTHENTICATED) == 0)
  {
    DBG_PRINTF("KEY_CLASS_S2_UNAUTHENTICATED was missing. Generating new.\n");
    S2_get_hw_random(net_key,16);
    print_hex(net_key,16);
    keystore_network_key_write(KEY_CLASS_S2_UNAUTHENTICATED, net_key);
  }
  if ((key_store_flags & KEY_CLASS_S2_AUTHENTICATED_LR) == 0)
  {
    DBG_PRINTF("KEY_CLASS_S2_AUTHENTICATED_LR was missing. Generating new.\n");
    S2_get_hw_random(net_key,16);
    print_hex(net_key,16);
    keystore_network_key_write(KEY_CLASS_S2_AUTHENTICATED_LR, net_key);
  }
  if ((key_store_flags & KEY_CLASS_S2_ACCESS_LR) == 0)
  {
    DBG_PRINTF("KEY_CLASS_S2_ACCESS_LR was missing. Generating new.\n");
    S2_get_hw_random(net_key,16);
    print_hex(net_key,16);
    keystore_network_key_write(KEY_CLASS_S2_ACCESS_LR, net_key);
  }
}

static void sec2_event_handler(zwave_event_t* ev) {
  uint16_t response;
  uint8_t flags;
  LOG_PRINTF("S2 inclusion event: %s\n",s2_inclusion_event_name(ev->event_type));
  switch(ev->event_type) {
    case S2_NODE_INCLUSION_INITIATED_EVENT:
      sec0_abort_inclusion(); //Here we abort S0 inclusion
      break;
    case S2_NODE_INCLUSION_PUBLIC_KEY_CHALLENGE_EVENT:
      NetworkManagement_dsk_challenge(&(ev->evt.s2_event.s2_data.challenge_req));
      break;
    case S2_NODE_INCLUSION_KEX_REPORT_EVENT:
      LOG_PRINTF("csa %i keys %i\n",ev->evt.s2_event.s2_data.kex_report.csa, ev->evt.s2_event.s2_data.kex_report.security_keys) ;
      NetworkManagement_key_request(&ev->evt.s2_event.s2_data.kex_report);
      break;
    case S2_NODE_INCLUSION_COMPLETE_EVENT:
      if(sec_incl_cb) {

        sec_incl_cb(keystore_flags_2_node_flags(ev->evt.s2_event.s2_data.inclusion_complete.exchanged_keys));
      }
      //< Security 2 event, inclusion of the node  to the network has been completed.
      break;
    case S2_NODE_JOINING_COMPLETE_EVENT:
      nvm_config_get(assigned_keys,&flags);
      if(sec_incl_cb) {
        sec_incl_cb(keystore_flags_2_node_flags(flags));
      }
      break;
    case S2_NODE_INCLUSION_FAILED_EVENT:
      if(sec_incl_cb) {
        sec_incl_cb(NODE_FLAG_KNOWN_BAD | (ev->evt.s2_event.s2_data.inclusion_fail.kex_fail_type << 16));
      }
      //< Security 2 event, inclusion of the node  to the network has failed. See the details
      break;
  }
}

void sec2_key_grant(uint8_t accept, uint8_t keys,uint8_t csa) {
  s2_inclusion_key_grant(s2_ctx, accept,keys,csa);
}

void sec2_dsk_accept(uint8_t accept, uint8_t* dsk, uint8_t len ) {
  s2_inclusion_challenge_response(s2_ctx, accept,  dsk,len);
}

void S2_get_commands_supported(node_t lnode,uint8_t class_id, const uint8_t** cmdClasses, uint8_t* length) {
  /*Only answer to ACCESS class*/
  if(
      ((net_scheme == SECURITY_SCHEME_2_ACCESS) && class_id==2 ) ||
      ((net_scheme == SECURITY_SCHEME_2_AUTHENTICATED) && class_id==1 ) ||
      ((net_scheme == SECURITY_SCHEME_2_UNAUTHENTICATED) && class_id==0 )
  ) {
    if(lnode == MyNodeID)  {
      *cmdClasses = &SecureClasses[0];
      /* libs2 buffer (ctxt->u.commands_sup_report_buf) can hold only (40 - 2) bytes so truncate
        the number of s2 commands supported */
      if (nSecureClasses > 38) {
         ERR_PRINTF("Truncating the number of command classes supported to 38.\n");
         *length = 38;
      } else {
         *length  = nSecureClasses;
      }
      return;
    }
  }

  *length = 0;
}


void sec2_init() {
  static uint8_t s2_cmd_class_sup_report[64];

  uint8_t retval;

  ctimer_stop(&s2_timer);
  ctimer_stop(&s2_inclusion_timer);
  if(s2_ctx) S2_destroy(s2_ctx);

  if( 0 != (retval = s2_inclusion_init(SECURITY_2_SCHEME_1_SUPPORT,KEX_REPORT_CURVE_25519,
      KEY_CLASS_S2_UNAUTHENTICATED | KEY_CLASS_S2_AUTHENTICATED | KEY_CLASS_S2_ACCESS | KEY_CLASS_S0 ))) {
    ERR_PRINTF("Failed to initialize libs2! Error code %u\n", retval);
  }

  s2_ctx = S2_init_ctx(UIP_HTONL(homeID));
  s2_inclusion_set_event_handler(&sec2_event_handler);
  sec2_create_new_dynamic_ecdh_key();
  //memset(&s2_send_tx_status, 0, sizeof s2_send_tx_status); //Redundant: Static vars are always initialized to zero
  mc_state.state = MC_SEND_STATE_IDLE;
}


uint8_t ecdh_dynamic_key[32];

void sec2_create_new_static_ecdh_key() {
  sec2_create_new_dynamic_ecdh_key();
  nvm_config_set(ecdh_priv_key,ecdh_dynamic_key);
  sec2_create_new_dynamic_ecdh_key();
}


void sec2_create_new_dynamic_ecdh_key()
{
  AES_CTR_DRBG_Generate(&s2_ctr_drbg, ecdh_dynamic_key);
  AES_CTR_DRBG_Generate(&s2_ctr_drbg, &ecdh_dynamic_key[16]);
}


uint8_t sec2_get_my_node_flags() {
  uint8_t flags;
  nvm_config_get(assigned_keys,&flags);
  return keystore_flags_2_node_flags(flags);
}

void sec2_create_new_network_keys() {
  uint8_t net_key[16];
  int i;
  int c;
  LOG_PRINTF("Creating new S2 network keys (S0 key is excluded)\n");
  for(c=0; c < S2_NUM_KEY_CLASSES-1; c++) {
    S2_get_hw_random(net_key,16);
    print_hex(net_key,16);
    keystore_network_key_write(1<<c,net_key);
  }

  memset(net_key,0, sizeof(net_key));
}

void
sec2_start_learn_mode(nodeid_t node_id, sec2_inclusion_cb_t cb)
{

  s2_connection_t s2_con;
  ZW_SendDataAppl_Callback_t cb_save;

  /*We update the context here becase the homeID has changed */
  sec2_init();

  s2_con.l_node = MyNodeID;
  s2_con.r_node = node_id;
  s2_con.zw_tx_options = TRANSMIT_OPTION_ACK | TRANSMIT_OPTION_AUTO_ROUTE | TRANSMIT_OPTION_EXPLORE;
  sec_incl_cb = cb;

  s2_inclusion_joining_start(s2_ctx,&s2_con,0);
}

void sec2_inclusion_neighbor_discovery_complete()
{
    s2_inclusion_neighbor_discovery_complete(s2_ctx);
}

void sec2_start_add_node(nodeid_t node_id, sec2_inclusion_cb_t cb ) {

  s2_connection_t s2_con;

  ZW_SendDataAppl_Callback_t cb_save;

  s2_con.l_node = MyNodeID;
  s2_con.r_node = node_id;
  s2_con.zw_tx_options = TRANSMIT_OPTION_ACK | TRANSMIT_OPTION_AUTO_ROUTE | TRANSMIT_OPTION_EXPLORE;

  sec_incl_cb = cb;
  s2_inclusion_including_start(s2_ctx,&s2_con);
}


/**
 *   search through a received multi cmd encap and find get command
 */
static uint8_t is_get_in_multi_cmd(ZW_APPLICATION_TX_BUFFER *pCmd)
{
  int i;
  int no_cmd = pCmd->ZW_MultiCommandEncapFrame.numberOfCommands; /*get number of commands to iterate */
  int len = 0;
  int off = 3; /* command_class_multi_cmd + multi_cmd_encap + number of cmds */
  uint8_t *data = (uint8_t *)pCmd;

  DBG_PRINTF("received get inside multi cmd encap\n");
  for ( i = 0; i < no_cmd; i++) {
      off += 1 + len;
      ZW_APPLICATION_TX_BUFFER* pCmd_m = (ZW_APPLICATION_TX_BUFFER*)&data[off]; /*beginning of the cmd inside multi encap*/
      len = (uint8_t)data[off-1]; /* cmd length of the cmd inside multi cmd*/
      if (CommandAnalyzerIsGet((int)pCmd_m->ZW_Common.cmdClass, (int)pCmd_m->ZW_Common.cmd))
      {
         return 1;
      }
  }
  return 0;
}


uint8_t sec2_send_data(ts_param_t* p, uint8_t* data, uint16_t len,ZW_SendDataAppl_Callback_t callback,void* user) {
  s2_connection_t s2_con;

  ZW_SendDataAppl_Callback_t cb_save;
  void* user_save;

  s2_con.l_node = p->snode;
  s2_con.r_node = p->dnode;
  s2_con.zw_tx_options = p->tx_flags;
  s2_con.tx_options = 0;
  /* This enables mailbox to set S2_TXOPTION_VERIFY_DELIVERY which waits for the sleeping ndoe to send SOS/resync if
    needed. It puts delay of 500ms, but only for first frame popped out of Mailbox after the node is woken up*/
  if(p->force_verify_delivery || mb_is_busy() || CommandAnalyzerIsGet(data[0],data[1]) ||
          ((data[0] == COMMAND_CLASS_MULTI_CHANNEL_V4) && (data[1] == MULTI_CHANNEL_CMD_ENCAP_V2) && (CommandAnalyzerIsGet(data[4], data[5]))) ||
          is_get_in_multi_cmd((ZW_APPLICATION_TX_BUFFER*)data))
  {
     s2_con.tx_options |= S2_TXOPTION_VERIFY_DELIVERY;
  }

  s2_con.class_id = p->scheme - SECURITY_SCHEME_2_UNAUTHENTICATED;

  cb_save = s2_send_callback;
  user_save = s2_send_user;

  s2_send_callback = callback;
  s2_send_user = user;
  if(S2_send_data(s2_ctx,&s2_con,data,len)) {
      return 1;
  } else {
      s2_send_callback = cb_save;
      s2_send_user = user_save;
      return 0;
  }
}

#ifdef TEST_MULTICAST_TX
/**
 * Abort multicast single-cast follow-ups
 *
 * A multicast message is usually followed by a single-cast follow-up to each
 * node in the multicast group. This function will abort that loop and
 * ultimately result in the multicast message being moved to the long queue.
 */
void sec2_abort_multicast(void)
{
  if (mc_state.state != MC_SEND_STATE_IDLE) {
    mc_state.state = MC_SEND_STATE_ABORT;
  }
}
#endif /* TEST_MULTICAST_TX */

#ifdef TEST_MULTICAST_TX
/**
 * Send multicast message.
 * \param p                 ts_param containing the package transmission details.
 * \param data              Data package to send.
 * \param data_len          Length of data to send.
 * \param send_sc_followups Should we send single cast follow-ups after the initial multicast?
 * \param callback          Function to call when the multicast transmission (including the follow-ups) has completed.
 */
uint8_t sec2_send_multicast(ts_param_t *p, const uint8_t *data, uint8_t data_len, BOOL send_sc_followups, ZW_SendDataAppl_Callback_t callback, void *user) {
  s2_connection_t s2_con;
  ZW_SendDataAppl_Callback_t cb_save;
  void* user_save;

  if(mc_state.state == MC_SEND_STATE_IDLE) {
    mc_state.l_node = p->snode;
    /* r_node and dest_nodemask are only used for single
     * cast follow-ups but we should always initialize them
     */
    mc_state.r_node = 1;
    mc_state.dest_nodemask = p->node_list;
    mc_state.data = data;
    mc_state.data_len = data_len;
    mc_state.s2_class = p->scheme - SECURITY_SCHEME_2_UNAUTHENTICATED;
    mc_state.s2_groupd_id = mcast_group_get_id_by_nodemask(p->node_list);

    /* Should we proceed with single cast follow-ups after the multicast? */
    if (send_sc_followups) {
      mc_state.state = MC_SEND_STATE_SEND_FIRST;
    } else {
      mc_state.state = MC_SEND_STATE_SEND_FIRST_NOFOLLOWUP;
    }

    s2_send_callback = callback;

    s2_con.l_node = p->snode;
    s2_con.r_node = mc_state.s2_groupd_id;
    s2_con.zw_tx_options = 0;
    s2_con.tx_options = 0;
    s2_con.class_id = mc_state.s2_class;

    cb_save = s2_send_callback;
    user_save = s2_send_user;

    s2_send_callback = callback;
    s2_send_user = user;
    DBG_PRINTF("Sending Multicast\n");
    if (S2_send_data_multicast(s2_ctx, &s2_con, mc_state.data, mc_state.data_len)) {
      return 1;
    } else {
        s2_send_callback = cb_save;
        s2_send_user = user_save;

        /* Transmission failed */
        mc_state.state = MC_SEND_STATE_IDLE;
        return 0;
    }

  }
  return 0;
}
#endif /* TEST_MULTICAST_TX */

/**
 * Emitted when the security engine is done.
 * Note that this is also emitted when the security layer sends a SECURE_COMMANDS_SUPPORTED_REPORT
 */
void S2_send_done_event(struct S2* ctxt, s2_tx_status_t status) {
  /* The two zeros are padding to align with libs2 numeric value of S2_TRANSMIT_COMPLETE_VERIFIED */
  const uint8_t s2zw_codes[] = {TRANSMIT_COMPLETE_OK,TRANSMIT_COMPLETE_NO_ACK,TRANSMIT_COMPLETE_FAIL, 0, 0, TRANSMIT_COMPLETE_OK};
  ZW_SendDataAppl_Callback_t cb_save;

  ctimer_stop(&s2_timer);

#ifdef TEST_MULTICAST_TX
  if (mc_state.state == MC_SEND_STATE_SEND_FIRST_NOFOLLOWUP)
  {
    global_mcast_status_len = 0;
    mc_state.state = MC_SEND_STATE_IDLE;
  }

  if(mc_state.state != MC_SEND_STATE_IDLE) {
    /*
     * Begin (or continue) sending single cast follow-ups to
     * all remote nodes specified in dest_nodemask
     */

    if (mc_state.state != MC_SEND_STATE_SEND_FIRST )
    {
      /* NOT initial multicast, but all single-cast follow ups are processed here*/
      uint8_t ack_status;
      global_mcast_status[global_mcast_status_len].node_ID = mc_state.r_node;
      if((S2_TRANSMIT_COMPLETE_OK == status) || (S2_TRANSMIT_COMPLETE_VERIFIED == status)) {
        ack_status = 1;
      }
      else {
        ack_status = 0;
      }
      global_mcast_status[global_mcast_status_len].ack_status = ack_status;
      global_mcast_status_len++;
      mc_state.r_node++;  /* Start from next node when searching nodemask */
    }
    else
    {
      /* Initial multicast frame, zero length and wait for singlecast-followups */
      global_mcast_status_len = 0;
    }

    if (mc_state.state == MC_SEND_STATE_ABORT)
    {
      /* Stop sending single-cast follow-ups.
       * By setting status to TRANSMIT_COMPLETE_NO_ACK we ensure that the
       * multicast request is moved to the long queue.
       */
      status = TRANSMIT_COMPLETE_NO_ACK;
      mc_state.state = MC_SEND_STATE_IDLE;
    }
    else
    {
      /* Iterate nodemask until we find a bit that is set */
      while (nodemask_nodeid_is_valid(mc_state.r_node) &&
            nodemask_test_node(mc_state.r_node, mc_state.dest_nodemask) != 1)
      {
        mc_state.r_node++;
      }

      if (nodemask_nodeid_is_invalid(mc_state.r_node)) {
        /* No more nodes found in the nodemask - we're done with the follow-ups */
        mc_state.state = MC_SEND_STATE_IDLE;
        DBG_PRINTF("Multicast transmission is done\n");
      } else {
        s2_connection_t s2_con;

        s2_con.l_node = mc_state.l_node;
        s2_con.r_node = mc_state.r_node;
        s2_con.zw_tx_options = TRANSMIT_OPTION_ACK | TRANSMIT_OPTION_AUTO_ROUTE | TRANSMIT_OPTION_EXPLORE ;
        s2_con.tx_options = (mc_state.state ==MC_SEND_STATE_SEND)  ?
            S2_TXOPTION_SINGLECAST_FOLLOWUP  | S2_TXOPTION_VERIFY_DELIVERY :
            S2_TXOPTION_FIRST_SINGLECAST_FOLLOWUP | S2_TXOPTION_VERIFY_DELIVERY;
        s2_con.class_id = mc_state.s2_class;

        DBG_PRINTF("Sending Multicast followup to node %i\n",s2_con.r_node);
        mc_state.state = MC_SEND_STATE_SEND;

        if(S2_send_data(s2_ctx,&s2_con,mc_state.data,mc_state.data_len)) {
          return;
        } else {
          status = S2_TRANSMIT_COMPLETE_FAIL;
          mc_state.state = MC_SEND_STATE_IDLE;
        }
      }
    }
  }
#endif

  cb_save = s2_send_callback;
  s2_send_callback = 0;
  if(cb_save) {
    cb_save(s2zw_codes[status],s2_send_user, &s2_send_tx_status);
  }
  memset(&s2_send_tx_status, 0, sizeof s2_send_tx_status);

}

/**
 * Emitted when a messages has been received and decrypted
 */
void S2_msg_received_event(struct S2* ctxt,s2_connection_t* src , uint8_t* buf, uint16_t len) {
  ts_param_t p;
  const int scheme_map[] = {
      SECURITY_SCHEME_2_UNAUTHENTICATED,
      SECURITY_SCHEME_2_AUTHENTICATED,
      SECURITY_SCHEME_2_ACCESS};
  ts_set_std(&p,0);


  p.scheme = scheme_map[src->class_id];
  p.snode = src->r_node;
  p.dnode = src->l_node;
  p.rx_flags = src->zw_rx_status;

  ApplicationCommandHandlerZIP(&p,(ZW_APPLICATION_TX_BUFFER*)buf,len);
}

//This is the time it takes a z-wave node to do 3*S2 frame decryption + generating a nonce report
//#define NONCE_REP_TIME 50
#define NONCE_REP_TIME 250
static void S2_send_frame_callback(BYTE txStatus,void* user, TX_STATUS_TYPE *t) {
  if (t) {
    s2_send_tx_status = *t;
  }
  S2_send_frame_done_notify((struct S2*) user,
      txStatus == TRANSMIT_COMPLETE_OK ? S2_TRANSMIT_COMPLETE_OK : S2_TRANSMIT_COMPLETE_NO_ACK,
          clock_time()-transmit_start_time + NONCE_REP_TIME);
}

/** Must be implemented elsewhere maps to ZW_SendData or ZW_SendDataBridge note that ctxt is
 * passed as a handle. The ctxt MUST be provided when the \ref S2_send_frame_done_notify is called */
uint8_t S2_send_frame(struct S2* ctxt,const s2_connection_t* conn, uint8_t* buf, uint16_t len) {
  ts_param_t p;
  ts_set_std(&p,0);
  p.snode = conn->l_node;
  p.dnode = conn->r_node;
  p.tx_flags = conn->zw_tx_options;
  LOG_PRINTF(" Sending S2_send_frame %i %d -> %d\n", len, p.snode, p.dnode);
  transmit_start_time = clock_time();
  return send_data(&p, buf,  len,S2_send_frame_callback,ctxt);
}

typedef struct
{
  uint8_t buf[50];  /* TODO: This should be enough for a nonce report. Can be shrunk to save memory. */
} nonce_rep_tx_t;

#define NUM_NONCE_BLOCK 8
/**
 *  This memblock caches outgoing nonce reports so we dont have to care if s2 overwrites the single buffer in there
 */
MEMB(nonce_rep_memb, nonce_rep_tx_t, NUM_NONCE_BLOCK);

/**
 * Callback function for S2_send_frame_no_cb()
 *
 * Cleans up the transmission
 */
static void nonce_rep_callback(BYTE txStatus,void* user, TX_STATUS_TYPE *t)
{
  nonce_rep_tx_t *m = (nonce_rep_tx_t*)user;
  if(txStatus != TRANSMIT_COMPLETE_OK) {
    DBG_PRINTF("Nonce rep tx failed, status %u, seqno %u", txStatus, m->buf[2]);
  }
  DBG_PRINTF("Freeing nonce memb %p\n", m);
  memb_free(&nonce_rep_memb, m);
  DBG_PRINTF("Nonce memb free count is now %d, freed nonce memb:  %p\n", memb_free_count(&nonce_rep_memb), m);
}

int count_free_nonce_report_block(struct memb *m)
{
   int i;
   int ret = 0;
   for(i = 0; i < m->num; ++i) {
      if(m->count[i]) {
          ret++;
      }
   }
   return ret;
}

/** Does not provide callback to libs2 (because send_done can be confused with Msg Encapsulations)
 *  Caches the buf pointer outside libs2 so we dont have to care if s2 reuses the single buffer in there.
 *
 * Must be implemented elsewhere maps to ZW_SendData or ZW_SendDataBridge note that ctxt is
 * passed as a handle. The ctxt MUST be provided when the \ref S2_send_frame_done_notify is called */
uint8_t S2_send_frame_no_cb(struct S2* ctxt,const s2_connection_t* conn, uint8_t* buf, uint16_t len) {
  ts_param_t p;
  nonce_rep_tx_t *m;
  uint8_t res=FALSE;
  m = memb_alloc(&nonce_rep_memb);
  if (m == 0)
  {
    WRN_PRINTF("No more queue space for nonce reps\n");
    return FALSE;
  }
  DBG_PRINTF("free nonce report blocks: %d\n", NUM_NONCE_BLOCK - (count_free_nonce_report_block(&nonce_rep_memb)));
  ts_set_std(&p,0);
  p.snode = conn->l_node;
  p.dnode = conn->r_node;
  p.tx_flags = conn->zw_tx_options;
  if(len > sizeof(m->buf)) {
    WRN_PRINTF("nonce_rep_tx_t buf is too small, needed %u\n", len);
  }
  memcpy(m->buf, buf, len);
  DBG_PRINTF("S2_send_frame_no_cb len %i, seqno 0x%02x m: %p conn %u -> %u.\n", len, m->buf[2], m, p.snode, p.dnode);
  /* 500-series and 700-series protocol uses a fixed verify delivery timout of 500 ms.
    * After that time, the Nonce Report is of no use and should be discarded from the send_data_list queue. */
  if ((m->buf[0] == COMMAND_CLASS_SECURITY_2) && (m->buf[1] == SECURITY_2_NONCE_REPORT)) {
    p.discard_timeout = END_NODE_VERIFY_DELIVERY_TIMEOUT;
  }
  res = send_data(&p, m->buf,  len, nonce_rep_callback, m);
  if (res) {
     DBG_PRINTF("Sending nonce memb %p for conn %u -> %u.\n", m, p.snode, p.dnode);
  } else {
     DBG_PRINTF("Freeing nonce memb %p for conn %u -> %u, since we cannot send.\n",
                m, p.snode, p.dnode);
     memb_free(&nonce_rep_memb, m);
  }
  return res;
}


/**
 * TODO
 */
uint8_t S2_send_frame_multi(struct S2* ctxt, s2_connection_t* conn, uint8_t* buf, uint16_t len){
  ts_param_t p;
  ts_set_std(&p,0);
  p.snode = conn->l_node;
  p.dnode = NODE_MULTICAST;
  nodemask_copy(p.node_list, mc_state.dest_nodemask);

  p.tx_flags = conn->zw_tx_options | TRANSMIT_OPTION_MULTICAST_AS_BROADCAST;
  LOG_PRINTF("Sending S2_send_frame_multi len=%i\n", len);
  transmit_start_time = clock_time();
  return send_data(&p, buf, len, S2_send_frame_callback, ctxt);
}


static void timeout(void* ctxt) {
  S2_timeout_notify((struct S2*)ctxt);
}
/**
 * Must be implemented elsewhere maps to ZW_TimerStart. Note that this must start the same timer every time.
 * Ie. two calls to this function must reset the timer to a new value. On timout \ref S2_timeout_event must be called.
 *
 */
void S2_set_timeout(struct S2* ctxt, uint32_t interval) {
  DBG_PRINTF("S2_set_timeout interval =%i ms\n",interval );

  ctimer_set(&s2_timer,interval,timeout,ctxt);
}

void S2_stop_timeout(struct S2* ctxt) {
  ctimer_stop(&s2_timer);
}

static void incl_timeout(void* ctxt) {
  DBG_PRINTF("incl_timeout\n");
  s2_inclusion_notify_timeout((struct S2*)ctxt);
}


uint8_t s2_inclusion_set_timeout(struct S2* ctxt, uint32_t interval) {
  DBG_PRINTF("s2_inclusion_set_timeout interval =%i ms\n",interval * 10);
  ctimer_set(&s2_inclusion_timer,interval * 10,incl_timeout,ctxt);
  return 0;
}

/**
 * Get a number of bytes from a random number generator
 */
void S2_get_hw_random(uint8_t *buf, uint8_t len) {
  uint8_t rnd[8];
  int n,left,pos;

  left = len;
  pos = 0;
  while( left > 0) {
      if(!dev_urandom(sizeof(rnd), rnd)) {
          ERR_PRINTF("S2_wrap: Failed to seed random number generator. Security is compromised\n");
      }
      n = left > 8 ? 8 : left;
      memcpy(&buf[pos],rnd, n);
      left -= n;
      pos += n;
  }
}

void /*RET Nothing                  */
security2_CommandHandler(ts_param_t* p,
const ZW_APPLICATION_TX_BUFFER *pCmd, uint16_t cmdLength) /* IN Number of command bytes including the command */
{
  s2_connection_t conn;
  conn.r_node = p->snode;
  conn.l_node = p->dnode;
  conn.zw_tx_options = p->tx_flags;
  conn.zw_rx_status = p->rx_flags;
  conn.tx_options = 0;
  conn.rx_options = p->rx_flags & ( RECEIVE_STATUS_TYPE_BROAD |  RECEIVE_STATUS_TYPE_MULTI) ? S2_RXOPTION_MULTICAST : 0;

  S2_application_command_handler(s2_ctx,&conn,(uint8_t*) pCmd,cmdLength);
}

uint8_t sec2_gw_node_flags2keystore_flags(uint8_t gw_flags) {
  uint8_t f =0;

  if(gw_flags & NODE_FLAG_SECURITY0) {
    f |= KEY_CLASS_S0;
  }
  if(gw_flags & NODE_FLAG_SECURITY2_ACCESS) {
    f |= KEY_CLASS_S2_ACCESS;
  }
  if(gw_flags & NODE_FLAG_SECURITY2_AUTHENTICATED) {
    f |= KEY_CLASS_S2_AUTHENTICATED;
  }
  if(gw_flags & NODE_FLAG_SECURITY2_UNAUTHENTICATED) {
    f |= KEY_CLASS_S2_UNAUTHENTICATED;
  }

  return f;
}

void sec2_abort_join() {
  DBG_PRINTF("S2 inclusion was aborted\n");
  if(!s2_ctx) return;

  s2_inclusion_abort(s2_ctx);
}

void sec2_refresh_homeid(void)
{
  s2_ctx->my_home_id = UIP_HTONL(homeID);
}

void S2_dbg_printf(const char *format, ...)
{
    va_list argptr;
    printf("\033[34;1m%lu ", clock_time());

    va_start(argptr, format);
    vfprintf(stdout, format, argptr);
    va_end(argptr);

    printf("\033[0m");
}
void S2_err_printf(const char *format, ...)
{
    va_list argptr;
    printf("\033[31;1m%lu ", clock_time());

    va_start(argptr, format);
    vfprintf(stdout, format, argptr);
    va_end(argptr);

    printf("\033[0m");
}
extern void s2_inclusion_stop_timeout(void)
{
  ctimer_stop(&s2_inclusion_timer);
}

void S2_resynchronization_event(
    node_t remote_node,
    sos_event_reason_t reason,
    uint8_t seqno,
    node_t local_node)
{
  uint8_t resync_event_packet[6];

  resync_event_packet[0] = COMMAND_CLASS_NETWORK_MANAGEMENT_INSTALLATION_MAINTENANCE;
  resync_event_packet[1] = COMMAND_S2_RESYNCHRONIZATION_EVENT;
  if (is_lr_node(remote_node)) {
    resync_event_packet[2] = 0xff;
    resync_event_packet[3] = reason;
    resync_event_packet[4] = remote_node >> 8;
    resync_event_packet[5] = remote_node & 0xff;
  } else {
    resync_event_packet[2] = remote_node;
    resync_event_packet[3] = reason;
    resync_event_packet[4] = 0;
    resync_event_packet[5] = 0;
  }
  send_to_both_unsoc_dest(resync_event_packet, sizeof resync_event_packet, NULL);
  DBG_PRINTF("S2 sync event for remote_node: %u, reason %u, seqno 0x%02x and local_node: %u\n",
      remote_node, reason, seqno, local_node);
}


void sec2_persist_span_table()
{
  if(s2_ctx) {
    rd_datastore_persist_s2_span_table(s2_ctx->span_table, SPAN_TABLE_SIZE);
  }
  else {
    WRN_PRINTF("Failed to persist S2 SPAN table, missing s2_ctx\n");
  }
}

void sec2_reset_span(nodeid_t node)
{
  for(size_t i = 0; i < SPAN_TABLE_SIZE; i++) {
    if ((s2_ctx->span_table[i].rnode == node))
    {
      s2_ctx->span_table[i].state = SPAN_NOT_USED;
      DBG_PRINTF("Resetting span for %d -> %d as Firmware activataion set is sent"
                 " to it and the node:%d will reset itself\n", s2_ctx->span_table[i].lnode, node, node);
    }
  }
}
void sec2_unpersist_span_table()
{
  if(s2_ctx) {
    rd_datastore_unpersist_s2_span_table(s2_ctx->span_table, SPAN_TABLE_SIZE);
    for(size_t i = 0; i < SPAN_TABLE_SIZE; i++) {
      if (s2_ctx->span_table[i].state == SPAN_NEGOTIATED)
        LOG_PRINTF("S2_SPAN (%03zu): state: %d\n", i, s2_ctx->span_table[i].state);
    }
  }
  else {
    WRN_PRINTF("Failed to unpersist S2 SPAN table, missing s2_ctx\n");
  }
}
