/* © 2014 Silicon Laboratories Inc.
 */
#include "Serialapi.h"
#include "contiki.h"
#include "ZW_SendDataAppl.h"
#include "Security_Scheme0.h"
#include "zw_network_info.h"
#include "NodeCache.h"
#include "transport_service2.h"
#include "list.h"
#include "memb.h"
#include "etimer.h"
#include "security_layer.h"
#include "ClassicZIPNode.h"
#include "CC_InstallationAndMaintenance.h"
#include "node_queue.h"
#include "S2_wrap.h"
#include "S2_multicast_auto.h"
#include<stdlib.h>
#include "ZW_transport_api.h"
#include "ZIP_Router_logging.h"
#include "zgw_crc.h"
#include "zw_frame_buffer.h"
#include "CommandAnalyzer.h"
#include "ZW_classcmd_ex.h"
#include "ZW_ZIPApplication.h"
/*
 * SendData Hierarchy, each, level wraps the previous. A higher level call MUST only call lower level calls.
 *
 * SendRequest          - available from application space, send a command and wait for a reply
 * SendDataAppl         - available from application space, send a command and put it in a queue
 * SendEndpoint         - internal call, single session call add endpoint header
 * SendSecurity         - internal call, single session call add security header
 * SendTransportService - internal call, single session call do transport service fragmentation
 * SendData             - internal call, single session send the data
 */

typedef struct
{
  void* next;
  zw_frame_buffer_element_t* fb;
  void* user;
  ZW_SendDataAppl_Callback_t callback;
  struct ctimer discard_timer;         /* Timer for discarding element if it stays in the queue too long */
  uint8_t reset_span; /* This flag will be set on sending activation set to the end node. On successfull ack
                         S2 Span for the destinaiton node will be reset */
} send_data_appl_session_t;

static uint8_t lock = 0;
static struct etimer emergency_timer;
static struct etimer backoff_timer;
static nodeid_t backoff_node; //Node on which the backoff timer is started

LIST(session_list);
MEMB(session_memb, send_data_appl_session_t, 8);

static uint8_t lock_ll = 0;
LIST(send_data_list);


static security_scheme_t
ZW_SendData_scheme_select(const ts_param_t* param, const u8_t* data, u8_t len);


PROCESS(ZW_SendDataAppl_process, "ZW_SendDataAppl_process process");

enum
{
  SEND_EVENT_SEND_NEXT, SEND_EVENT_SEND_NEXT_LL,SEND_EVENT_SEND_NEXT_DELAYED
};

void
ts_set_std(ts_param_t* p, nodeid_t dnode)
{
  p->dendpoint = 0;
  p->sendpoint = 0;
  p->snode = MyNodeID;
  p->dnode = dnode;
  nodemask_clear(p->node_list);
  p->rx_flags = 0;
  p->tx_flags = TRANSMIT_OPTION_ACK | TRANSMIT_OPTION_AUTO_ROUTE
      | TRANSMIT_OPTION_EXPLORE;
  p->scheme = AUTO_SCHEME;
  p->discard_timeout = 0;
  p->force_verify_delivery = FALSE;
  p->is_mcast_with_folloup = FALSE;
  p->is_multicommand = FALSE;
}

void
ts_param_make_reply(ts_param_t* dst, const ts_param_t* src)
{
  dst->snode = src->dnode;
  dst->dnode = src->snode;
  dst->sendpoint = src->dendpoint;
  dst->dendpoint = src->sendpoint;
  dst->scheme = ZW_SendData_scheme_select(src, 0, 2);
  dst->discard_timeout = 0;


  dst->tx_flags =
      ((src->rx_flags & RECEIVE_STATUS_LOW_POWER) ?
          TRANSMIT_OPTION_LOW_POWER : 0) | TRANSMIT_OPTION_ACK
          | TRANSMIT_OPTION_EXPLORE | TRANSMIT_OPTION_AUTO_ROUTE;
}

/**
 * Returns true if source and destinations are identical
 */
u8_t
ts_param_cmp(ts_param_t* a1, const ts_param_t* a2)
{
  return (a1->snode == a2->snode && a1->dnode == a2->dnode
      && a1->sendpoint == a2->sendpoint && a1->dendpoint == a2->dendpoint);
}

static send_data_appl_session_t* current_session;
static send_data_appl_session_t* current_session_ll;

void
ZW_SendDataAppl_CallbackEx(uint8_t status, void*user, TX_STATUS_TYPE *ts) REENTRANT
{
  send_data_appl_session_t* s = (send_data_appl_session_t*) user;
  uint32_t backoff_interval = 0;

  if (!lock)
  {
    ERR_PRINTF("Double callback! ");
    return;
  }
  lock = FALSE;
  
    /*Check if this is a get message, and set the backoff accordinly */
  if((status == TRANSMIT_COMPLETE_OK) && (ts!=NULL) && CommandAnalyzerIsGet(s->fb->frame_data[0],s->fb->frame_data[1])) {
    /* Make some room for the report */
    backoff_interval = ts->wTransmitTicks*10+250;
    backoff_node = s->fb->param.dnode;
    process_post_synch(&ZW_SendDataAppl_process, SEND_EVENT_SEND_NEXT_DELAYED, (void*)(uintptr_t)backoff_interval);
  } else {
    backoff_interval = 0;
    // This is an async post, the next element will only be send when contiki has scheduled the event.
    process_post(&ZW_SendDataAppl_process, SEND_EVENT_SEND_NEXT, NULL);
  }
  if(status == TRANSMIT_COMPLETE_OK) {
      if (s->reset_span) {
        sec2_reset_span(s->fb->param.dnode);
        s->reset_span = 0;
      }
  }
  zw_frame_buffer_free(s->fb);
  memb_free(&session_memb, s);

  /*if(status==TRANSMIT_COMPLETE_FAIL) {
   DBG_PRINTF("Actual transmitter fault!");
   }*/

  if (s->callback)
  {
    s->callback(status, s->user, ts);
  }
  
}

security_scheme_t
highest_scheme(uint8_t scheme_mask)
{
  if(scheme_mask & NODE_FLAG_SECURITY2_ACCESS) {
    return SECURITY_SCHEME_2_ACCESS;
  } else if(scheme_mask & NODE_FLAG_SECURITY2_AUTHENTICATED) {
    return SECURITY_SCHEME_2_AUTHENTICATED;
  } else if(scheme_mask & NODE_FLAG_SECURITY2_UNAUTHENTICATED) {
    return SECURITY_SCHEME_2_UNAUTHENTICATED;
  }else if(scheme_mask & NODE_FLAG_SECURITY0) {
    return SECURITY_SCHEME_0;
  } else {
    return NO_SCHEME;
  }
}

static security_scheme_t
ZW_SendData_scheme_select(const ts_param_t* param, const u8_t* data, u8_t len)
{
  u8_t dst_scheme_mask=0; //Mask of schemes supported by destination
  u8_t src_scheme_mask=0;
  static security_scheme_t scheme;
  dst_scheme_mask = GetCacheEntryFlag(param->dnode);
  src_scheme_mask = GetCacheEntryFlag(MyNodeID);
  security_scheme_t dst_highest_scheme = highest_scheme(dst_scheme_mask);
  /* Check that this node node has at least one security scheme */

  if ((len < 2) || (dst_scheme_mask & NODE_FLAG_KNOWN_BAD) || (src_scheme_mask & NODE_FLAG_KNOWN_BAD) )
  {
    if((param->scheme == USE_CRC16) &&
       SupportsCmdClass(param->dnode, COMMAND_CLASS_CRC_16_ENCAP) &&
       (dst_highest_scheme == NO_SCHEME)) {
      DBG_PRINTF("Node has NODE_FLAG_KNOWN_BAD set in either dst_scheme_mask or"
                 " src_scheme_mask, but supports CRC16");
      return USE_CRC16;
    } else {
      DBG_PRINTF("Node has NODE_FLAG_KNOWN_BAD set in either dst_scheme_mask or"
                 " src_scheme_mask or frame length is less than 2\n");

     return NO_SCHEME;
    }
  }

  //Common schemes
  /* FIXME-MCAST: at this point, the scheme mask of the node should
   * always be a subset of the scheme mask of the gateway. */
  dst_scheme_mask &= src_scheme_mask;

  switch (param->scheme) {
  case AUTO_SCHEME:

    scheme=  highest_scheme(dst_scheme_mask);
    if(scheme == NO_SCHEME && SupportsCmdClass(param->dnode, COMMAND_CLASS_CRC_16_ENCAP)) {
      return USE_CRC16;
    } else {
      return scheme;
    }
  case NO_SCHEME:
    return NO_SCHEME;
  case USE_CRC16:
    return USE_CRC16;
  case SECURITY_SCHEME_2_ACCESS:
    if(dst_scheme_mask & NODE_FLAG_SECURITY2_ACCESS) return param->scheme;
    break;
  case SECURITY_SCHEME_2_AUTHENTICATED:
    if(dst_scheme_mask & NODE_FLAG_SECURITY2_AUTHENTICATED) return param->scheme;
    break;
  case SECURITY_SCHEME_2_UNAUTHENTICATED:
    if(dst_scheme_mask & NODE_FLAG_SECURITY2_UNAUTHENTICATED) return param->scheme;
    break;
  case SECURITY_SCHEME_0:
    if(dst_scheme_mask & NODE_FLAG_SECURITY0) return param->scheme;
    break;
  default:
    break;
  }
  WRN_PRINTF("Scheme %x NOT supported by destination %i \n",param->scheme,param->dnode);
  return param->scheme;
}

/**
 * Timeout function that removes an expired session_memb from the send_data_list and memb_frees it.
 */
static void
do_discard_timeout_memb(void * data)
{
  send_data_appl_session_t* s = (send_data_appl_session_t*) data;
  char retval;

  if (!s) {
     return;
  }
  list_remove(send_data_list,s);
  ERR_PRINTF("Discarding %p because maximum delay was exceeded in dest\n", s);
  retval = memb_free(&session_memb, s);
  if (retval != 0) {
    ERR_PRINTF("memb_free() failed in %s(). Return code %d, ptr %p\n", __func__, retval, s);
  }
  if (s->callback)
  {
    s->callback(TRANSMIT_COMPLETE_FAIL, s->user, NULL);
  }
}
/**
 * Callback function activated when a callback is received from the SendData API functions on the Z-Wave chip.
 *
 *\param status  Transmit status code
 *\param ts      Transmit status report
 */
static void
send_data_callback_func(u8_t status, TX_STATUS_TYPE* ts)
{
  enum en_queue_state queue_state = get_queue_state();

  if(!lock_ll) {
    ERR_PRINTF("Double callback?\n");
    return;
  }
  
  send_data_appl_session_t *s = list_pop(send_data_list);

  /* Call only if short queue (queue_state = 1) and status is TRANSMIT_COMPLETE_OK */
  /* Call all the time queue state is long queue (2)*/
  /* Do not call if queue state is idle(0) */
  if (((queue_state == QS_SENDING_FIRST) && (status == TRANSMIT_COMPLETE_OK)) || (queue_state == QS_SENDING_LONG))
  {
      ima_send_data_done(s->fb->param.dnode,status,ts);
  }
  etimer_stop(&emergency_timer);

#if NO_BRIDGE
  clasic_session_keepalive(s->param.dnode);
#endif

  //}
  zw_frame_buffer_free(s->fb);
  memb_free(&session_memb, s);
  lock_ll = FALSE;
  if (s)
  {
    if (s->callback)
    {
      s->callback(status, s->user, ts);
    }
  }
  else
  {
    ASSERT(0);
  }

  process_post(&ZW_SendDataAppl_process, SEND_EVENT_SEND_NEXT_LL, NULL);
}

/**
 * Callback for SerialAPI_ZW_SendDataMulti_Bridge
 *
 * Simply forwards the call to send_data_callback_func() without a txStatusReport structure.
 */
static void
send_data_multi_bridge_callback_func(u8_t status)
{
  send_data_callback_func(status, 0);
}


/**
 * Low level send data. This call will not do any encapsulation except transport service encap
 */
u8_t
send_data(ts_param_t* p, const u8_t* data, u16_t len,
    ZW_SendDataAppl_Callback_t cb, void* user)
{
  send_data_appl_session_t* s;

  s = memb_alloc(&session_memb);
  if (s == 0)
  {
    DBG_PRINTF("OMG! No more queue space");
    return FALSE;
  }

  
  s->fb = zw_frame_buffer_create(p,data,len); 
  if(s->fb == NULL) {
    return FALSE;
  }
  s->user = user;
  s->callback = cb;

  list_add(send_data_list, s);

  if(s->fb->param.discard_timeout) {
    DBG_PRINTF("Starting %.0fms discard timer on send_data_list element: %p\n",((float)(s->fb->param.discard_timeout))/CLOCK_SECOND*1000, s);
    ctimer_set(&s->discard_timer, s->fb->param.discard_timeout, do_discard_timeout_memb, s);
  }

  process_post(&ZW_SendDataAppl_process, SEND_EVENT_SEND_NEXT_LL, NULL);
  return TRUE;
}

/**
 * Send data to an endpoint and do endpoint encap security encap CRC16 or transport service encap
 * if needed. This function is not reentrant. It will only be called from the ZW_SendDataAppl event tree
 * @param p
 * @param data
 * @param len
 * @param cb
 * @param user
 * @return
 */
u8_t
send_endpoint(ts_param_t* p, const u8_t* data, u16_t len, ZW_SendDataAppl_Callback_t cb, void* user)
{
  u16_t new_len;
  security_scheme_t scheme;
  static u8_t new_buf[UIP_BUFSIZE]; //Todo we should have some max frame size

  new_len = len;

  if (len > sizeof(new_buf))
    return FALSE;

  if (p->dendpoint || p->sendpoint)
  {
    new_buf[0] = COMMAND_CLASS_MULTI_CHANNEL_V2;
    new_buf[1] = MULTI_CHANNEL_CMD_ENCAP_V2;
    new_buf[2] = p->sendpoint;
    new_buf[3] = p->dendpoint;
    new_len += 4;

    if (new_len > sizeof(new_buf))
      return FALSE;

    memcpy(&new_buf[4], data, len);
  }
  else
  {
    memcpy(new_buf, data, len);
  }

#ifdef TEST_MULTICAST_TX
  /*Multicast AUTO_SCHEME is handled separately */
  if ((p->tx_flags & TRANSMIT_OPTION_MULTICAST) && (p->scheme == AUTO_SCHEME) ) {
    return sec2_send_multicast_auto_split(p, new_buf, new_len, p->is_mcast_with_folloup, cb, user);
  }
#endif

  /*Select the right security shceme*/
  scheme = ZW_SendData_scheme_select(p, data, len);
  LOG_PRINTF("Sending with scheme %s\n", network_scheme_name(scheme));
  switch (scheme)
  {
  case USE_CRC16:
    /* CRC16 Encap frame if destination supports it and if its a single fragment frame
     *
     *
     * */
    if (new_len < META_DATA_MAX_DATA_SIZE && new_buf[0] != COMMAND_CLASS_TRANSPORT_SERVICE
        && new_buf[0] != COMMAND_CLASS_SECURITY && new_buf[0] != COMMAND_CLASS_SECURITY_2
        && new_buf[0] != COMMAND_CLASS_CRC_16_ENCAP)
    {
      WORD crc;
      memmove(new_buf + 2, new_buf, new_len);
      new_buf[0] = COMMAND_CLASS_CRC_16_ENCAP;
      new_buf[1] = CRC_16_ENCAP;
      crc = zgw_crc16(CRC_INIT_VALUE, (BYTE*) new_buf, new_len + 2);
      new_buf[2 + new_len] = (crc >> 8) & 0xFF;
      new_buf[2 + new_len + 1] = (crc >> 0) & 0xFF;
      new_len += 4;
    }
    return send_data(p, new_buf, new_len, cb, user);
  case NO_SCHEME:
    if (p->tx_flags & TRANSMIT_OPTION_MULTICAST) {
      WRN_PRINTF("TODO: implement non-secure multicast\n");
      return FALSE;
    } else {
      return send_data(p, new_buf, new_len, cb, user);
    }
    break;
  case SECURITY_SCHEME_0:
    if (p->tx_flags & TRANSMIT_OPTION_MULTICAST) {
      WRN_PRINTF("Attempt to transmit multicast with S0\n");
      return FALSE;
    } else {
      return sec0_send_data(p, new_buf, new_len, cb, user);
    }
    break;
  case SECURITY_SCHEME_2_ACCESS:
  case SECURITY_SCHEME_2_AUTHENTICATED:
  case SECURITY_SCHEME_2_UNAUTHENTICATED:
    p->scheme = scheme;
#ifdef TEST_MULTICAST_TX
    if (p->tx_flags & TRANSMIT_OPTION_MULTICAST) {
      return sec2_send_multicast(p, new_buf, new_len, p->is_mcast_with_folloup, cb, user);
    } else {
#endif
      return sec2_send_data(p, new_buf, new_len, cb, user);
#ifdef TEST_MULTICAST_TX
    }
#endif
    break;
  default:
    return FALSE;
    break;
  }
}

static void
send_first()
{
  current_session = list_pop(session_list);

  if (!current_session)
  {
    return;
  }

  lock = TRUE;

  if (!send_endpoint(&current_session->fb->param, current_session->fb->frame_data,
      current_session->fb->frame_len, ZW_SendDataAppl_CallbackEx, current_session))
  {
    ZW_SendDataAppl_CallbackEx(TRANSMIT_COMPLETE_ERROR, current_session, NULL);
  }
}

uint8_t
ZW_SendDataAppl(ts_param_t* p, const void *pData, uint16_t dataLength,
    ZW_SendDataAppl_Callback_t callback, void* user)
{
  send_data_appl_session_t* s;
  uint8_t *c = (uint8_t *)pData; //for debug message
  const uint8_t lr_nop[] = {COMMAND_CLASS_NO_OPERATION_LR, 0};

 s = memb_alloc(&session_memb);
  if (s == 0)
  {
    DBG_PRINTF("OMG! No more queue space");
    return 0;
  }
  /* ZGW-3373: SPAN is reset for the node where Firmware activation set frame is
   * sent to prevent S2 from dropping frame because the sequence number of 
   * activation report matching to last two frames S2 duplication detection keeps
   * track of. Protocol had an issue(fixed now) of not saving SPAN and sequence number at the
   * right time. This code will take care of nodes without that protocol fix.
   * When the node reboots and sends firmware activation report, GW will try to
   * sync by sending s2 nonce report and only one fw act report frame will be dropped
   */
  if ((c[0] == COMMAND_CLASS_FIRMWARE_UPDATE_MD_V4) && (c[1] == FIRMWARE_UPDATE_ACTIVATION_SET_V4)) {
    DBG_PRINTF("Intercepting Fimware activation set command\n");
    s->reset_span = 1;
  }
  if ((c[0] == COMMAND_CLASS_NO_OPERATION) && is_lr_node(p->dnode)) {
    DBG_PRINTF("Intercepting non-LR NOP being sent to long range node\n");
    s->fb = zw_frame_buffer_create(p, lr_nop, sizeof(lr_nop));
    DBG_PRINTF("Sending %d->%d, class: 0x%x, cmd: 0x%x, len: %d\n",
               p->snode, p->dnode, COMMAND_CLASS_NO_OPERATION_LR, 0, 2);
  } else {
    s->fb = zw_frame_buffer_create(p, pData, dataLength);
    LOG_PRINTF("Sending %d->%d, [%s]\n",
               p->snode, p->dnode, print_frame(pData, dataLength));
  }
 
  if (s->fb == NULL)
  {
    memb_free(&session_memb, s);
    ERR_PRINTF("ZW_SendDataAppl: malloc failed\r\n");
    return 0;
  }
  s->user = user;
  s->callback = callback; //ZW_SendDataAppl_CallbackEx

  list_add(session_list, s);
  process_post(&ZW_SendDataAppl_process, SEND_EVENT_SEND_NEXT, NULL);

  /*return a handle that we may use to abort the tranmission */
  return ( ((void*)s -session_memb.mem ) /session_memb.size) + 1;
}


void
ZW_SendDataApplAbort(uint8_t handle ) {
  send_data_appl_session_t* s;
  uint8_t h = handle -1;

  if(handle==0 || handle > session_memb.num) {
    ERR_PRINTF("ZW_SendDataAppl_Abort: Invalid handle\n");
    return;
  }

  s = (send_data_appl_session_t*)( session_memb.mem + session_memb.size*h);


  if(s == current_session) {
    /*Transmission is in progress so stop the module from making more routing attempts
     * This will trigger a transmit complete fail or ok for the current transmission. At some
     * point this will call ZW_SendDataAppl_CallbackEx which will free the session */
    ZW_SendDataAbort();

    /*Cancel security timers in case we are waiting for some frame from target node*/
    sec0_abort_all_tx_sessions();

#ifdef TEST_MULTICAST_TX
    /* Stop iterating over security groups if sending multicast */
    sec2_send_multicast_auto_split_abort();

    /* Stop iterating over nodes (to send single cast follow-ups) if sending multicast */
    sec2_abort_multicast();
#endif
    /* Cancel transport service timer, in case we are waiting for some frame from target node.
     * TODO*/
    //TransportService_SendDataAbort();
  } else {
    if (!list_contains(session_list, s))
    {
      /* This is an orphaned callback. Ignore it.*/
      return;
    }

    /*Remove the session from the list */
    list_remove(session_list,s);

    /*De-allocate the session */
    memb_free(&session_memb, s);
    if (s->callback)
    {
      s->callback(TRANSMIT_COMPLETE_FAIL, s->user, NULL);
    }
  }
}

/* The component is idle if both the application level queue and the
 * low level queue are empty and there are no current sessions from
 * either queue.
 */ 
bool ZW_SendDataAppl_idle(void) {
   return ((current_session == NULL)
           && (current_session_ll == NULL)
           && (list_head(session_list) == NULL)
           && (list_head(send_data_list) == NULL));
}

void
ZW_SendDataAppl_init()
{

  lock = FALSE;
  lock_ll = FALSE;
  sec0_abort_all_tx_sessions();
  list_init(session_list);
  list_init(send_data_list);
  memb_init(&session_memb);

  process_exit(&ZW_SendDataAppl_process);
  process_start(&ZW_SendDataAppl_process, NULL);
}


/**
 * Calculate a priority for the scheme
 */
static int
scheme_prio(security_scheme_t a)
{
  switch (a)
  {
  case SECURITY_SCHEME_UDP:
    return 0xFFFF;
  case SECURITY_SCHEME_2_ACCESS:
    return 4;
  case SECURITY_SCHEME_2_AUTHENTICATED:
    return 3;
  case SECURITY_SCHEME_2_UNAUTHENTICATED:
    return 2;
  case SECURITY_SCHEME_0:
    return 1;
  default:
    return 0;
  }
}

/**
 * return true if a has a larger or equal scheme to b
 */
int
scheme_compare(security_scheme_t a, security_scheme_t b)
{
  return scheme_prio(a) >= scheme_prio(b);
}


void ZW_SendDataAppl_FrameRX_Notify(const ts_param_t *c, const uint8_t* frame, uint16_t length) {
  if(!etimer_expired(&backoff_timer) && (c->snode == backoff_node)) {
    //ERR_PRINTF("Backoff timer stopped\n");
    /*Stop the backoff timer and send the next message */
    etimer_stop(&backoff_timer);
    process_post(&ZW_SendDataAppl_process, SEND_EVENT_SEND_NEXT, NULL);
  }
}


PROCESS_THREAD(ZW_SendDataAppl_process, ev, data)
{
  BYTE rc;
  PROCESS_BEGIN()
  ;

  while (1)
  {
    switch (ev)
    {
      case PROCESS_EVENT_TIMER:
        if (data == (void*) &emergency_timer)
        {
          //ERR_PRINTF("Missed serialAPI callback!");
          send_data_callback_func(TRANSMIT_COMPLETE_FAIL,0);
        } else if(data == (void*) &backoff_timer) {
          DBG_PRINTF("Backoff timer expired\n");
          process_post(&ZW_SendDataAppl_process, SEND_EVENT_SEND_NEXT, NULL);
        }
        
        break;
      case SEND_EVENT_SEND_NEXT:
        if (!lock && etimer_expired(&backoff_timer) )
        {
          send_first();
        }
        break;
      case SEND_EVENT_SEND_NEXT_DELAYED:
        {
          uint32_t backoff_interval = (uint32_t)(uintptr_t)(data);
          //DBG_PRINTF("Stating backoff if %i ms\n",backoff_interval);
          etimer_set(&backoff_timer, backoff_interval);
        }
        break;

      case SEND_EVENT_SEND_NEXT_LL:
        current_session_ll = list_head(send_data_list);
        if (current_session_ll && lock_ll == FALSE)
        {
          if(current_session_ll->fb->param.discard_timeout)
          {
            //Prevent a timer to discard the frame if the session has a discard_timeout defined.
            DBG_PRINTF("Stopping discard timer of send_data_list element: %p\n", current_session_ll);
            ctimer_stop(&current_session_ll->discard_timer);
          }

          lock_ll = TRUE;
          rc = FALSE;

          if (current_session_ll->fb->frame_len >= META_DATA_MAX_DATA_SIZE)
          {
            if (SupportsCmdClass(current_session_ll->fb->param.dnode,
                COMMAND_CLASS_TRANSPORT_SERVICE))
            {
              //ASSERT(current_session_ll->param.snode == MyNodeID); //TODO make transport service bridge aware
              rc = ZW_TransportService_SendData(&current_session_ll->fb->param,
                  (u8_t*) current_session_ll->fb->frame_data,
                  current_session_ll->fb->frame_len,
                  send_data_callback_func);
            }
            else
            {
              WRN_PRINTF("Frame is too long for target.\n");
              rc = FALSE;
            }
          }
#ifdef TEST_MULTICAST_TX
          else if (current_session_ll->param.dnode == NODE_MULTICAST)
          {
            rc = SerialAPI_ZW_SendDataMulti_Bridge(current_session_ll->param.snode,
                                                   current_session_ll->param.node_list,
                                                   (BYTE*) current_session_ll->pData,
                                                   current_session_ll->dataLength,
                                                   current_session_ll->param.tx_flags,
                                                   send_data_multi_bridge_callback_func);
          }
#endif
          else
          {
            if (current_session_ll->fb->param.snode != MyNodeID)
            {
              rc = ZW_SendData_Bridge(current_session_ll->fb->param.snode,
                                      current_session_ll->fb->param.dnode,
                                      (BYTE*) current_session_ll->fb->frame_data,
                                      current_session_ll->fb->frame_len,
                                      current_session_ll->fb->param.tx_flags,
                                      send_data_callback_func);
            }
            else
            {
              current_session_ll->fb->param.snode = 0x00ff;
              rc = ZW_SendData(current_session_ll->fb->param.dnode,
                               (BYTE*) current_session_ll->fb->frame_data,
                               current_session_ll->fb->frame_len,
                               current_session_ll->fb->param.tx_flags,
                               send_data_callback_func);
            }
          }

          if (!rc)
          {
#ifdef TEST_MULTICAST_TX
            if (current_session_ll->param.dnode == NODE_MULTICAST)
            {
              ERR_PRINTF("ZW_SendDataMulti_Bridge returned FALSE\n");
              send_data_multi_bridge_callback_func(TRANSMIT_COMPLETE_FAIL);
            }
            else
            {
#endif
              ERR_PRINTF("ZW_SendData(_Bridge) returned FALSE\n");
              send_data_callback_func(TRANSMIT_COMPLETE_FAIL, 0);
#ifdef TEST_MULTICAST_TX
            }
#endif
          }
          else
          {
            ima_send_data_start(current_session_ll->fb->param.dnode);
            etimer_set(&emergency_timer, 65 * 1000UL);
          }
        }
    }
    PROCESS_WAIT_EVENT()
    ;
  }
PROCESS_END();
}
