/* © 2014 Silicon Laboratories Inc.
 */
#include "ZW_SendRequest.h"
#include "ZW_SendDataAppl.h"
#include "sys/ctimer.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "ZIP_Router_logging.h"


#define NUM_REQS 4
#define NOT_SENDING 0xFF
#define ROUTING_RETRANSMISSION_DELAY 250

typedef enum
{
  REQ_SENDING, REQ_WAITING, REQ_DONE,
} req_state_t;

typedef struct send_request_state
{
  list_t *list;
  ts_param_t param;
  req_state_t state;
  BYTE class;
  BYTE cmd;
  WORD timeout;
  void* user;
  ZW_SendRequst_Callback_t callback;
  struct ctimer timer;
  clock_time_t round_trip_start;
} send_request_state_t;

MEMB(reqs,struct send_request_state,NUM_REQS);
LIST(reqs_list);

static void
request_timeout(void* d)
{
  send_request_state_t* s = (send_request_state_t*) d;
  s->param.dnode = 0;

  s->state = REQ_DONE;
  ctimer_stop(&s->timer);
  list_remove(reqs_list,s);
  memb_free(&reqs,s);

  WRN_PRINTF("SendRequest timeout waiting for 0x%2x 0x%2x\n", s->class, s->cmd);
  if (s->callback)
  {
    s->callback(TRANSMIT_COMPLETE_FAIL, 0, 0, 0, s->user);
  }
}

static void
ZW_SendRequest_Callback(BYTE status, void* user, TX_STATUS_TYPE *t)
{
  send_request_state_t* s = (send_request_state_t*)user;
  clock_time_t round_trip_end = clock_time();
  clock_time_t round_trip_duration; 



  if (s->round_trip_start < round_trip_end ) {
    round_trip_duration = round_trip_end - s->round_trip_start;
  } else {
    round_trip_duration = 10;
  }

  /*250 ms added to be safe for delays with routing and hops and retransmission */
  round_trip_duration += ROUTING_RETRANSMISSION_DELAY;

  //DBG_PRINTF("round_trip_duration : %lu\n", round_trip_duration);
  if ((status == TRANSMIT_COMPLETE_OK) && (s->state == REQ_SENDING))
  {
    s->state = REQ_WAITING;
    ctimer_set(&s->timer, (s->timeout * 10) + round_trip_duration, request_timeout, s);
  }
  else
  {
    request_timeout(s);
  }
}

/**
 * Send a request to a node, and trigger the callback once the
 * response is received
 *
 * NOTE! If there is a destination endpoint, then there must be room for 4 bytes before pData
 *
 */
BYTE
ZW_SendRequest(ts_param_t* p, const BYTE *pData,
BYTE dataLength,
BYTE responseCmd, WORD timeout, void* user, ZW_SendRequst_Callback_t callback)
{
  send_request_state_t* s;

  if (!callback)
    goto fail;

  s = memb_alloc(&reqs);

  if (!s)
    goto fail;

  list_add(reqs_list,s);

  ts_param_make_reply(&s->param, p); //Save the node/endpoint which is supposed to reply

  s->state = REQ_SENDING;
  s->class = pData[0];
  s->cmd = responseCmd;
  s->user = user;
  s->callback = callback;
  s->timeout = timeout;

  s->round_trip_start = clock_time();
  if (ZW_SendDataAppl(p, pData, dataLength, ZW_SendRequest_Callback, s))
  {
    return TRUE;
  }
  else
  {
    list_remove(reqs_list,s);
    memb_free(&reqs,s);
  }

fail:
  ERR_PRINTF("ZW_SendRequest fail\n");
  return FALSE;
}

void ZW_Abort_SendRequest(uint8_t n)
{
//  DBG_PRINTF("--------------- Inside ZW_Abort_SendRequest()\n");
  send_request_state_t* s;
  for (s = list_head(reqs_list); s; s=list_item_next(s)) {
//    DBG_PRINTF("--------------- Comparing n=%bu with dnode=%bu, snode=%bu\n", n, s->param.dnode, s->param.snode);
    if (s->state == REQ_WAITING && (s->param.snode == n))
      {
        s->state = REQ_DONE;
        ctimer_stop(&s->timer);
        list_remove(reqs_list,s);
        memb_free(&reqs,s);
        s->callback(TRANSMIT_COMPLETE_FAIL, 0, 0, 0, s->user);
      }
  }
}

BOOL
SendRequest_ApplicationCommandHandler(ts_param_t* p,
    ZW_APPLICATION_TX_BUFFER *pCmd, WORD cmdLength)
{
  send_request_state_t* s;
  for (s = list_head(reqs_list); s; s=list_item_next(s))
  {
    if (s->state == REQ_WAITING && ts_param_cmp(&s->param, p)
        && s->class == pCmd->ZW_Common.cmdClass
        && s->cmd == pCmd->ZW_Common.cmd
         )
    {
      /*Only accept the command if it was received with the same security scheme which was used
       * durring transmission */
      if(s->param.scheme != p->scheme) {
        WRN_PRINTF("Message with wrong scheme received was %i expected %i\n",p->scheme,s->param.scheme);
        continue;
      }

      s->state = REQ_DONE;

      ctimer_stop(&s->timer);
      list_remove(reqs_list,s);
      if(s->callback(TRANSMIT_COMPLETE_OK, p->rx_flags, pCmd, cmdLength, s->user)) {
        WRN_PRINTF("ZW_SendRequest Callback returned 1. There are more reports"
                   " expected.\n");
        s->state = REQ_WAITING;
        ctimer_set(&s->timer, (s->timeout * 10), request_timeout, s);
        list_add(reqs_list,s);
      } else {
        memb_free(&reqs,s);
      }
      return TRUE;
    }
  }
  return FALSE;
}

void
ZW_SendRequest_init()
{
  memb_init(&reqs);
  list_init(reqs_list);
}
