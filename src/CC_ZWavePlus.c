/* © 2017 Silicon Laboratories Inc.
 */
/*
 * CC_ZwavePlus.c
 *
 *  Created on: Nov 9, 2016
 *      Author: aes
 */

/*
 * CC_Manufacturer_Specific.c
 *
 *  Created on: Nov 9, 2016
 *      Author: aes
 */

#include "Serialapi.h"
#include "command_handler.h"
#include "ZW_classcmd.h"
#include "ZW_classcmd_ex.h"

extern ZW_APPLICATION_TX_BUFFER txBuf;

static const ZW_ZWAVEPLUS_INFO_REPORT_V2_FRAME zwaveplus_info_frame =
  { COMMAND_CLASS_ZWAVEPLUS_INFO, ZWAVEPLUS_INFO_REPORT, ZWAVEPLUS_VERSION_V2,
  ZWAVEPLUS_INFO_REPORT_ROLE_TYPE_CONTROLLER_CENTRAL_STATIC,
  ZWAVEPLUS_INFO_REPORT_NODE_TYPE_ZWAVEPLUS_FOR_IP_GATEWAY, (ICON_TYPE_GENERIC_GATEWAY >> 8) & 0xFF,
  ICON_TYPE_GENERIC_GATEWAY & 0xFF, (ICON_TYPE_GENERIC_GATEWAY >> 8) & 0xFF,
  ICON_TYPE_GENERIC_GATEWAY & 0xFF };

static command_handler_codes_t
ZWavePlusHandler(zwave_connection_t *c, uint8_t* frame, uint16_t length)
{
  ZW_APPLICATION_TX_BUFFER* pCmd = (ZW_APPLICATION_TX_BUFFER*) frame;
  switch (pCmd->ZW_Common.cmd)
  {
  case ZWAVEPLUS_INFO_GET:
    ZW_SendDataZIP(c, (BYTE*) &zwaveplus_info_frame, sizeof(zwaveplus_info_frame),
    NULL);
    break;
  default:
    return COMMAND_NOT_SUPPORTED;
  }
  return COMMAND_HANDLED;
}

REGISTER_HANDLER(ZWavePlusHandler, 0, COMMAND_CLASS_ZWAVEPLUS_INFO_V2, ZWAVEPLUS_INFO_VERSION_V2, NO_SCHEME);
