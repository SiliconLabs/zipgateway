/**
 * @copyright Copyright (c) 2021 Silicon Laboratories Inc.
 */


#include "ZIP_Router.h"
#include "command_handler.h"
#include "ResourceDirectory.h"
#include "CC_NetworkManagement.h"
#include "ZW_classcmd.h"
#include "ZW_classcmd_ex.h"
#include "ctimer.h"
#include "Mailbox.h"

typedef struct {
  list_t* next;
  nodeid_t destination_node;
  zwave_connection_t conn;
  ZW_APPLICATION_TX_BUFFER cmd;
  uint8_t cmd_len;
  struct ctimer nak_waiting;
} nm_queue_element_t;


#define WAITING_TIMEOUT (60*CLOCK_SECOND)
#define MAX_QUEUED_NM_OPS 10

MEMB(nm_op_queue_elements, nm_queue_element_t, MAX_QUEUED_NM_OPS);
LIST(nm_op_queue);

bool NetworkManagement_queue_wakeup_event( nodeid_t node);
// This is used as a state variable to keep track on
// which node we are currently servicing. If the is 0 
// we are not in the process of serviceing a node. 
static node_t current_executing_node = 0;

static void NAK_Waiting_Timeout(void* user) {
  nm_queue_element_t* e = (nm_queue_element_t*)user;
  if(e) {
    send_udp_ack(&e->conn, RES_WAITNG );
    ctimer_reset(&e->nak_waiting);
  }
}

/**
 * @brief Return the node id of the network management operation.
 * 
 * @param pCmd 
 * @param bDatalen 
 * @return uint8_t 
 */
static nodeid_t NetworkManagement_GetTargetNode(ZW_APPLICATION_TX_BUFFER* pCmd) {
  uint8_t cc = pCmd->ZW_Common.cmdClass;
  uint8_t cmd = pCmd->ZW_Common.cmd;

  if(cc == COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY && cmd == NODE_INFO_CACHED_GET) {
    ZW_NODE_INFO_CACHED_GET_V4_FRAME* get_frame = (ZW_NODE_INFO_CACHED_GET_V4_FRAME*)pCmd;
    if((get_frame->properties1 & 0xF)==0) {
        nodeid_t nid = get_frame->nodeId;
        if (nid == 0xff) { // This is LR node, take extended node ID
          return  (get_frame->extendedNodeIdMSB << 8) | get_frame->extendedNodeIdLSB;
        } else {
          return nid;
        }
    }
  } else if(cc == COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC && cmd == NODE_INFORMATION_SEND) {
    return ( pCmd->ZW_NodeInformationSendFrame.destinationNodeId );
  } else if(cc == COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION && cmd == NODE_NEIGHBOR_UPDATE_REQUEST) {
    return  pCmd->ZW_NodeNeighborUpdateRequestFrame.nodeId;
  } else if(cc == COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION && cmd == RETURN_ROUTE_ASSIGN) {
    return  pCmd->ZW_ReturnRouteAssignFrame.sourceNodeId;
  } else if(cc == COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION && cmd == RETURN_ROUTE_DELETE) {
    return  pCmd->ZW_ReturnRouteDeleteFrame.nodeId;
  }
  return 0;
}

static bool
NetworkManagement_queue_command(zwave_connection_t*conn,nodeid_t target_node, ZW_APPLICATION_TX_BUFFER* pCmd, BYTE bDatalen)
{
  nm_queue_element_t* e = memb_alloc(&nm_op_queue_elements);
  if(!e) {
    ERR_PRINTF("Unable to queue network management operation space");
    return false;
  }
  e->cmd = *pCmd;
  e->cmd_len = bDatalen;
  e->destination_node = target_node;
  e->conn = *conn;
  send_udp_ack(&e->conn, RES_WAITNG ); // Send an inital NACK waiting right away
  ctimer_set(&e->nak_waiting, WAITING_TIMEOUT, NAK_Waiting_Timeout,e);
  list_add(nm_op_queue,e);
  return true;
}

static nm_queue_element_t* NetworkManagement_queue_find_element(nodeid_t node) {
  nm_queue_element_t* e;
  for( e = list_head(nm_op_queue); e != NULL ; e = list_item_next(e)) {
    if(e->destination_node == node) {
      return e;
    }
  }
  return NULL;
}

static void NetworkManagement_queue_remove_element(nm_queue_element_t* e,zwave_udp_response_t status) {
  send_udp_ack(&e->conn,status);
  ctimer_stop(&e->nak_waiting);
  list_remove(nm_op_queue,e);
  memb_free(&nm_op_queue_elements,e);

}

void NetworkManagement_queue_purge_all( ) {
  while(list_head(nm_op_queue)) {
    NetworkManagement_queue_remove_element(list_head(nm_op_queue),RES_NAK);
  }
}

static void check_if_awake_cb(uint8_t status, void* user, TX_STATUS_TYPE *t)
{
  nodeid_t *node = (nodeid_t *)user;

  if (status == TRANSMIT_COMPLETE_OK) {
      DBG_PRINTF("The node: %d is awake. Emptying NM mailbox for it.\n", *node);
      NetworkManagement_queue_wakeup_event(*node);
  }
}

void check_if_awake(nodeid_t node)
{
  uint8_t nop_frame[1] = {0};
  ts_param_t ts;
  ts_set_std(&ts, node);
  ts.tx_flags = TRANSMIT_OPTION_ACK; //just try direct transmission

  if(!ZW_SendDataAppl(&ts, nop_frame, sizeof(nop_frame), check_if_awake_cb, &node) ) {
          check_if_awake_cb(TRANSMIT_COMPLETE_FAIL, &node, NULL);
  }
}

bool
NetworkManagement_queue_if_target_is_mailbox(zwave_connection_t*conn, ZW_APPLICATION_TX_BUFFER* pCmd, BYTE bDatalen) {
  node_t target_node = NetworkManagement_GetTargetNode(pCmd);
  if((target_node != 0) && (target_node != 0xFF)) {
    
    // If the mailbox is already servicing the node, the node is still awake,
    // Hence we can send the command right away.
    if(mb_get_active_node() == target_node) {
      return false;
    }
    
    rd_node_mode_t node_mode = rd_get_node_mode( target_node );
    if( (node_mode & 0xff) == MODE_MAILBOX ) {
      check_if_awake(target_node);
      DBG_PRINTF("Destination node is %i a mailbox node queueing the NM command\n",target_node);
      return NetworkManagement_queue_command(conn,target_node,pCmd,bDatalen);
    }
  }
  return false;
}


void NetworkManagement_queue_purge_node( nodeid_t node) {
  while(1) {
    nm_queue_element_t* e = NetworkManagement_queue_find_element(node);
    if(e) {
      NetworkManagement_queue_remove_element(e,RES_NAK);
    } else {
      break;
    }
  }
}


bool NetworkManagement_queue_wakeup_event( nodeid_t node) {
  nm_queue_element_t* e = NetworkManagement_queue_find_element(node);
  
  if(e && ((current_executing_node==0) || (current_executing_node==node))) {
    if(NetworkManagementCommandHandler_internal(&e->conn,(uint8_t*)&e->cmd,e->cmd_len) != COMMAND_BUSY) {
      current_executing_node = node;
      DBG_PRINTF("Executing queued NM command to %i\n",current_executing_node);
      NetworkManagement_queue_remove_element(e,RES_ACK);
      return true;      
    }
  }
  return false;
}

void NetworkManagement_queue_nm_done_event() {
  /*
   * if current_executeing_node is set, the NM 
   * module was executing a command on behalf of this queue.
   */
  if(current_executing_node != 0) {
    DBG_PRINTF("Done executing command to %i\n",current_executing_node);
    node_t done_node = current_executing_node;
    //Check if there are more commands for this node.
    if( ! NetworkManagement_queue_wakeup_event(current_executing_node) ) {
      //if there are no more ops for this node let the mailbox take over
      current_executing_node = 0;
      mb_wakeup_event(done_node,false);
    } 
  }
}
