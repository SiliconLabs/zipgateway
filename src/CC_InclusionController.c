/* © 2017 Silicon Laboratories Inc.
 */
/*
 * CC_ControllerInclusion.c
 *
 *  Created on: May 30, 2016
 *      Author: aes
 */

#include "zip_router_ipv6_utils.h" /* nodeOfIP */
#include "zw_network_info.h" /* MyNodeID */
#include "NodeCache.h" /* GetCacheEntryFlag */
#include "CC_InclusionController.h"
#include "CC_NetworkManagement.h"
#include "Serialapi.h"
#include "ZW_classcmd_ex.h"
#include "ZW_SendDataAppl.h"
#include "command_handler.h"
#include "security_layer.h"
#include "sys/ctimer.h"
#include <stdint.h>
#include "ZW_ZIPApplication.h"
#include "ZIP_Router_logging.h"
#include"ResourceDirectory.h"

typedef enum  {EV_REQUEST_SUC_INCLUSION,EV_SUC_STARTED,EV_INITIATE, EV_COMPLETE,EV_TX_DONE_OK,
  EV_TX_DONE_FAIL, EV_TIMEOUT, EV_ADD_NODE_DONE, EV_S0_INCLUSION,EV_S0_INCLUSION_DONE} handler_events_t;

const char* handler_event_name(handler_events_t ev)
{
  static char str[25];

  switch(ev)
  {
  case EV_REQUEST_SUC_INCLUSION:    return "EV_REQUEST_SUC_INCLUSION";
  case EV_SUC_STARTED          :    return "EV_SUC_STARTED"; 
  case EV_INITIATE             :    return "EV_INITIATE"; 
  case EV_COMPLETE             :    return "EV_COMPLETE"; 
  case EV_TX_DONE_OK           :    return "EV_TX_DONE_OK"; 
  case EV_TX_DONE_FAIL         :    return "EV_TX_DONE_FAIL"; 
  case EV_TIMEOUT              :    return "EV_TIMEOUT"; 
  case EV_ADD_NODE_DONE        :    return "EV_ADD_NODE_DONE"; 
  case EV_S0_INCLUSION         :    return "EV_S0_INCLUSION"; 
  case EV_S0_INCLUSION_DONE    :    return "EV_S0_INCLUSION_DONE"; 
  default:
    sprintf(str, "%d", ev);
    return str;
  }
};



static struct {
  enum {STATE_IDLE, STATE_WAIT_FOR_SUC_ACCEPT, STATE_WAIT_FOR_ADD, STATE_WAIT_S0_INCLUSION,STATE_PERFORM_S0_INCLUSION } state;
  struct ctimer timer;
  inclusion_controller_cb_t complete_func;

  nodeid_t node_id;
  union {
    nodeid_t inclusion_conrtoller;
    nodeid_t suc_node;
  };
  uint8_t is_replace;
  zwave_connection_t connection;
} handler_state;

const char* handler_state_name(int state)
{
  static char str[25];

  switch(state)
  {
  case STATE_IDLE                 : return "STATE_IDLE"; 
  case STATE_WAIT_FOR_SUC_ACCEPT  : return "STATE_WAIT_FOR_SUC_ACCEPT"; 
  case STATE_WAIT_FOR_ADD         : return "STATE_WAIT_FOR_ADD"; 
  case STATE_WAIT_S0_INCLUSION    : return "STATE_WAIT_S0_INCLUSION"; 
  case STATE_PERFORM_S0_INCLUSION : return "STATE_PERFORM_S0_INCLUSION"; 
  default:
    sprintf(str, "%d", state);
    return str;
  }
};
static void handler_fsm(handler_events_t ev, void* data) ;


static void timeout(void* user ) {
  handler_fsm(EV_TIMEOUT,0);
}

static void send_done(uint8_t status, void* user, TX_STATUS_TYPE *t) {
  UNUSED(t);
  handler_fsm(status == TRANSMIT_COMPLETE_OK ? EV_TX_DONE_OK : EV_TX_DONE_FAIL ,0 );
}

static void S0InclusionDone(int status) {
	int step_status;
	step_status = status ? STEP_OK : STEP_FAILED;
	handler_fsm(EV_S0_INCLUSION_DONE,&status);
}

static void handler_fsm(handler_events_t ev, void* event_data) {
  DBG_PRINTF("Inclusion Controller handler_fsm: state %s event %s\n",handler_state_name(handler_state.state), handler_event_name(ev));
  switch (handler_state.state) {
  case STATE_IDLE:
    if(ev == EV_REQUEST_SUC_INCLUSION) {
      handler_state.state = STATE_WAIT_FOR_SUC_ACCEPT;
      handler_state.complete_func = (inclusion_controller_cb_t)event_data;
      handler_state.suc_node = ZW_GetSUCNodeID();

      ZW_INCLUSION_CONTROLLER_INITIATE_FRAME frame;
      ts_param_t ts;
      ts_set_std(&ts,handler_state.suc_node);

      frame.cmdClass = COMMAND_CLASS_INCLUSION_CONTROLLER;
      frame.cmd = INCLUSION_CONTROLLER_INITIATE;
      frame.node_id = handler_state.node_id;
      frame.step_id = handler_state.is_replace ? STEP_ID_PROXY_REPLACE : STEP_ID_PROXY_INCLUSION ;

      if(!ZW_SendDataAppl(&ts,&frame,sizeof(frame),send_done,0) ) {
        send_done(TRANSMIT_COMPLETE_FAIL,0, NULL);
      }
    } else if(ev == EV_INITIATE) {
      ZW_INCLUSION_CONTROLLER_INITIATE_FRAME* frame = (ZW_INCLUSION_CONTROLLER_INITIATE_FRAME*) event_data;
      handler_state.node_id = frame->node_id;

      handler_state.state = STATE_WAIT_FOR_ADD;
      StopNewNodeProbeTimer();

      if(frame->step_id == STEP_ID_PROXY_INCLUSION) {
        NetworkManagement_start_proxy_inclusion(frame->node_id);
      } else if(frame->step_id == STEP_ID_PROXY_REPLACE) {
        NetworkManagement_start_proxy_replace(frame->node_id);
      }  else {

        handler_state.state = STATE_IDLE;
        ERR_PRINTF("invalid inclusion step.");
        return;
      }

    }
    break;
  case STATE_WAIT_FOR_SUC_ACCEPT:
    if(ev  == EV_TX_DONE_OK) {
      //ctimer_set(&handler_state.timer, CLOCK_SECOND *3, timeout, 0);
      ctimer_set(&handler_state.timer, CLOCK_SECOND *( 60* 4 +2), timeout, 0);
    } else if(ev == EV_COMPLETE) {
      ZW_INCLUSION_CONTROLLER_COMPLETE_FRAME* frame =(ZW_INCLUSION_CONTROLLER_COMPLETE_FRAME*) event_data;
      if((frame->step_id == STEP_ID_PROXY_INCLUSION) || (frame->step_id == STEP_ID_PROXY_REPLACE)) {
        handler_state.state = STATE_IDLE;
        handler_state.complete_func(1);
      }
    } else if ((ev == EV_TX_DONE_FAIL) || (ev == EV_TIMEOUT)) {
      handler_state.state = STATE_IDLE;
      handler_state.complete_func(0);
    } else if(ev == EV_INITIATE) {
      ZW_INCLUSION_CONTROLLER_INITIATE_FRAME* frame =(ZW_INCLUSION_CONTROLLER_INITIATE_FRAME*) event_data;

      if(frame->step_id == STEP_ID_S0_INCLUSION) {
        handler_state.state = STATE_PERFORM_S0_INCLUSION;
        if(GetCacheEntryFlag(MyNodeID) & NODE_FLAG_SECURITY0) {
            security_add_begin(handler_state.node_id,
               TRANSMIT_OPTION_ACK | TRANSMIT_OPTION_EXPLORE | TRANSMIT_OPTION_AUTO_ROUTE,
               isNodeController(handler_state.node_id), S0InclusionDone);
        } else {
            WRN_PRINTF("Cannot do S0 inclusion\n");
            S0InclusionDone(STEP_NOT_SUPPORTED);
        }
      }
    }
    break;
  case STATE_PERFORM_S0_INCLUSION:
    if(EV_S0_INCLUSION_DONE) {
      ZW_INCLUSION_CONTROLLER_COMPLETE_FRAME frame;
      ts_param_t ts;

      ts_set_std(&ts,handler_state.suc_node);
      ts.force_verify_delivery = TRUE;

      frame.cmdClass = COMMAND_CLASS_INCLUSION_CONTROLLER;
      frame.cmd = INCLUSION_CONTROLLER_COMPLETE;
      frame.step_id = STEP_ID_S0_INCLUSION;
      frame.status = *((int*)event_data);

      handler_state.state = STATE_WAIT_FOR_SUC_ACCEPT;

      if(!ZW_SendDataAppl(&ts,&frame,sizeof(frame),send_done,0) ) {
        send_done(TRANSMIT_COMPLETE_FAIL,0, NULL);
      }
    }
    break;
  case STATE_WAIT_FOR_ADD:
    if(ev == EV_ADD_NODE_DONE) {

      ZW_INCLUSION_CONTROLLER_COMPLETE_FRAME frame;
      ts_param_t ts;

      ts_set_std(&ts,handler_state.inclusion_conrtoller);
      ts.force_verify_delivery = TRUE;

      frame.cmdClass = COMMAND_CLASS_INCLUSION_CONTROLLER;
      frame.cmd = INCLUSION_CONTROLLER_COMPLETE;
      frame.step_id = STEP_ID_PROXY_INCLUSION;
      frame.status = *((uint8_t*)event_data);

      if(!ZW_SendDataAppl(&ts,&frame,sizeof(frame),0,0)) {
      /* Dummy check. Code intentionally kept blank */
      }
      handler_state.state = STATE_IDLE;
    }
    else if(ev == EV_S0_INCLUSION) {
      ZW_INCLUSION_CONTROLLER_INITIATE_FRAME frame;
      ts_param_t ts;

      handler_state.complete_func = (inclusion_controller_cb_t)event_data;
      ts_set_std(&ts,handler_state.inclusion_conrtoller);

      frame.cmdClass = COMMAND_CLASS_INCLUSION_CONTROLLER;
      frame.cmd = INCLUSION_CONTROLLER_INITIATE;
      frame.node_id = handler_state.node_id;
      frame.step_id = STEP_ID_S0_INCLUSION;

      if(!ZW_SendDataAppl(&ts,&frame,sizeof(frame),send_done,0) ) {
        send_done(TRANSMIT_COMPLETE_FAIL,0, NULL);
      }

      handler_state.state = STATE_WAIT_S0_INCLUSION;
      ctimer_set(&handler_state.timer, CLOCK_SECOND *30, timeout, 0);
    }
    break;
  case STATE_WAIT_S0_INCLUSION:
    if( ev == EV_TIMEOUT) {
      handler_state.state = STATE_WAIT_FOR_ADD;
      handler_state.complete_func(0);
    } else if(ev == EV_COMPLETE) {
      ZW_INCLUSION_CONTROLLER_COMPLETE_FRAME* frame =(ZW_INCLUSION_CONTROLLER_COMPLETE_FRAME*) event_data;
      if(frame->step_id == STEP_ID_S0_INCLUSION) {
        handler_state.state = STATE_WAIT_FOR_ADD;
        handler_state.complete_func(frame->status);
      }
    }
    break;
  }
}

static command_handler_codes_t inclusuion_controller_handler(zwave_connection_t *connection, uint8_t* frame, uint16_t length)
{
  nodeid_t sending_node = nodeOfIP(&connection->ripaddr);
 security_scheme_t highest_node_scheme = 
      highest_scheme(GetCacheEntryFlag(sending_node));
 
  // As the inclusion controller command class opens of the non-secure nodes to
  // trigger handout of the security keys we do some explicit validation of the 
  // sender:
  //  - The node sending is using its highest supported scheme
  //  - Probe of the requesting node must be done.
  //  - The node must not be known to have failed its secure inclusion.
  //  - The requester must be a controller.
  if(
    
    !scheme_compare(connection->scheme,highest_node_scheme) ||
     (rd_get_node_probe_flags(sending_node)!=RD_NODE_FLAG_PROBE_HAS_COMPLETED)||
     isNodeBad(sending_node) ||
     !rd_check_nif_security_controller_flag(sending_node)
  ) {
    WRN_PRINTF("Rejected inclusion controller command.");
    return COMMAND_HANDLED;
  }

  switch(frame[1]) {
  case INCLUSION_CONTROLLER_INITIATE:
    if(handler_state.state== STATE_IDLE) {
      handler_state.inclusion_conrtoller = sending_node;
    }
    handler_fsm(EV_INITIATE,frame);
    break;
  case INCLUSION_CONTROLLER_COMPLETE:
    handler_fsm(EV_COMPLETE,frame);
    break;
  default:
    return COMMAND_NOT_SUPPORTED;
  }
  return COMMAND_HANDLED;
}

void inclusion_controller_you_do_it(inclusion_controller_cb_t complete_func) {
  handler_fsm(EV_S0_INCLUSION,complete_func);

}
void inclusion_controller_send_report(inclusion_cotroller_status_t status) {
  handler_fsm(EV_ADD_NODE_DONE,&status);
}

void inclusion_controller_started(void) {
  handler_fsm(EV_SUC_STARTED,0);
}



void request_inclusion_controller_handover( nodeid_t node_id,uint8_t is_replace,inclusion_controller_cb_t complete_func  )
{
  if(handler_state.state != STATE_IDLE && complete_func) {
    complete_func(0);
  } else {
    handler_state.node_id = node_id;
    handler_state.is_replace = is_replace;
    handler_fsm(EV_REQUEST_SUC_INCLUSION,complete_func);
  }
}

void controller_inclusuion_init() {
  handler_state.state = STATE_IDLE;
}

REGISTER_HANDLER(
    inclusuion_controller_handler,
    controller_inclusuion_init,
    COMMAND_CLASS_INCLUSION_CONTROLLER, 1, NO_SCHEME);
