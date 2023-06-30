/* © 2023 Silicon Laboratories Inc.
 */
/*
 * CC_DevideResetLocally.c
 *
 *  Created on: June 29, 2023
 *      Author: anrivoal
 */

#include "Serialapi.h"
#include "command_handler.h"
#include "zip_router_config.h"
#include "ZIP_Router_logging.h"
#include "zip_router_ipv6_utils.h" /* nodeOfIP */
#include "ZW_controller_api_ex.h"
#include <unistd.h>

extern ZW_APPLICATION_TX_BUFFER txBuf;

static uint8_t resetNode;

static struct ctimer reset_timer;

static void RemoveFailedNodeStatus (BYTE status) {
  DBG_PRINTF("RemoveFailedNodeStatus status: %d\n", status);
    if (status == ZW_FAILED_NODE_REMOVED) {
        DBG_PRINTF("Failed Node %d removed\n", resetNode);
        ApplicationControllerUpdate(UPDATE_STATE_DELETE_DONE, resetNode, 0, 0, NULL);
    }
    else if (status == ZW_NODE_OK){
        DBG_PRINTF("Node is operating correctly\n");
    }
    else {
        DBG_PRINTF("Remove Failed node failed\n");
    }
}

static void NopSendDone(uint8_t status, void* user, TX_STATUS_TYPE *t) {
  UNUSED(t);
  if (ZW_RemoveFailedNode(resetNode, RemoveFailedNodeStatus) != ZW_FAILED_NODE_REMOVE_STARTED)
  {
    RemoveFailedNodeStatus(ZW_FAILED_NODE_NOT_REMOVED);
  }
}

static void sendNOP(void* none) {
    uint8_t nop_frame[1] = {0};
    ts_param_t ts;
    ts_set_std(&ts, resetNode);
    DBG_PRINTF("Sending Nop to %d\n",resetNode);
    if(!ZW_SendDataAppl(&ts, nop_frame, sizeof(nop_frame), NopSendDone, 0) ) {
        NopSendDone(TRANSMIT_COMPLETE_FAIL, 0, NULL);
    }
}

static command_handler_codes_t
DeviceResetLocallyHandler(zwave_connection_t *c, uint8_t* frame, uint16_t length)
{
  ZW_APPLICATION_TX_BUFFER* pCmd = (ZW_APPLICATION_TX_BUFFER*) frame;
  switch (pCmd->ZW_Common.cmd)
  {
    case DEVICE_RESET_LOCALLY_NOTIFICATION:
      resetNode = nodeOfIP(&c->ripaddr);
      ctimer_set(&reset_timer, 2, sendNOP, 0);
      break;
  default:
    return COMMAND_NOT_SUPPORTED;;
  }
  return COMMAND_HANDLED;

}

REGISTER_HANDLER(
    DeviceResetLocallyHandler,
    0,
    COMMAND_CLASS_DEVICE_RESET_LOCALLY, DEVICE_RESET_LOCALLY_VERSION, NO_SCHEME);
