/* © 2017 Silicon Laboratories Inc.
 */
/*
 * CC_Manufacturer_Specific.c
 *
 *  Created on: Nov 9, 2016
 *      Author: aes
 */


#include "Serialapi.h"
#include "command_handler.h"
#include "zip_router_config.h"

extern   ZW_APPLICATION_TX_BUFFER txBuf;

static command_handler_codes_t
ManufacturerHandler(zwave_connection_t *c, uint8_t* frame, uint16_t length)
{
  ZW_APPLICATION_TX_BUFFER* pCmd = (ZW_APPLICATION_TX_BUFFER*) frame;
  switch (pCmd->ZW_Common.cmd)
  {
    case MANUFACTURER_SPECIFIC_GET:
      txBuf.ZW_ManufacturerSpecificReportFrame.cmdClass =
      COMMAND_CLASS_MANUFACTURER_SPECIFIC;
      txBuf.ZW_ManufacturerSpecificReportFrame.cmd =
      MANUFACTURER_SPECIFIC_REPORT;
      txBuf.ZW_ManufacturerSpecificReportFrame.manufacturerId1 =
          (cfg.manufacturer_id >> 8) & 0xff;
      txBuf.ZW_ManufacturerSpecificReportFrame.manufacturerId2 =
          (cfg.manufacturer_id >> 0) & 0xff;
      txBuf.ZW_ManufacturerSpecificReportFrame.productTypeId1 =
          (cfg.product_type >> 8) & 0xFF;
      txBuf.ZW_ManufacturerSpecificReportFrame.productTypeId2 =
          (cfg.product_type >> 0) & 0xFF;
      txBuf.ZW_ManufacturerSpecificReportFrame.productId1 = (cfg.product_id
          >> 8) & 0xFF;
      txBuf.ZW_ManufacturerSpecificReportFrame.productId2 = (cfg.product_id
          >> 0) & 0xFF;
      ZW_SendDataZIP(c, (BYTE*) &txBuf,
          sizeof(txBuf.ZW_ManufacturerSpecificReportFrame), NULL);
      break;
    case DEVICE_SPECIFIC_GET_V2:
    {
      ZW_DEVICE_SPECIFIC_REPORT_1BYTE_V2_FRAME *f =
          (ZW_DEVICE_SPECIFIC_REPORT_1BYTE_V2_FRAME*) &txBuf;
      f->cmdClass = COMMAND_CLASS_MANUFACTURER_SPECIFIC;
      f->cmd = DEVICE_SPECIFIC_REPORT_V2;
      f->properties1 = DEVICE_SPECIFIC_GET_DEVICE_ID_TYPE_SERIAL_NUMBER_V2;
      f->properties2 =
          ((DEVICE_SPECIFIC_REPORT_DEVICE_ID_DATA_FORMAT_BINARY_V2
              << DEVICE_SPECIFIC_REPORT_PROPERTIES2_DEVICE_ID_DATA_FORMAT_SHIFT_V2)
              | 0x6);
      //Return MAC address of the Device as Serial Number
      memcpy(&f->deviceIdData1, cfg.device_id, cfg.device_id_len);
      ZW_SendDataZIP(c, (BYTE*) &txBuf,
          sizeof(ZW_DEVICE_SPECIFIC_REPORT_1BYTE_V2_FRAME) - 1 + cfg.device_id_len, NULL);
    }
      break;
  default:
    return COMMAND_NOT_SUPPORTED;;
  }
  return COMMAND_HANDLED;

}


REGISTER_HANDLER(
    ManufacturerHandler,
    0,
    COMMAND_CLASS_MANUFACTURER_SPECIFIC_V2, MANUFACTURER_SPECIFIC_VERSION_V2, NET_SCHEME);
