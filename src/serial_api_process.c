/* © 2014 Silicon Laboratories Inc.
 */

#include "contiki.h"
#include "net/uip.h"
#include "Serialapi.h"
#include "ZW_controller_api.h"
#include "ZW_controller_api_ex.h"
#include "ZW_ZIPApplication.h"

#include "ZIP_Router.h" /* ApplicationCommandHandlerSerial */

#include "Bridge.h"
#include "security_layer.h"
#include "ZIP_Router_logging.h"
#include "txmodem.h"
#include "ZW_basis_api.h"

PROCESS(serial_api_process, "ZW Serial api");
extern void SerialFlush();
extern uint8_t SupportsSerialAPISetup_func(uint8_t);
extern int ZWGeckoFirmwareUpdate(void * fw_desc,void *chip_desc,
                          char *filename, size_t len);

PROTOCOL_VERSION zw_protocol_version2 = {0};
BYTE serial_ok = FALSE;

/*TODO For some reason this is not in WIN32 environments by ZW_controller_api.h */
#ifdef WIN32
extern void
ApplicationControllerUpdate(
    BYTE bStatus, /*IN  Status of learn mode */
    BYTE bNodeID, /*IN  Node id of the node that send node info */
    BYTE *pCmd,   /*IN  Pointer to Application Node information */
    BYTE bLen);   /*IN  Node info length                        */
#endif

const struct SerialAPI_Callbacks serial_api_callbacks =
  { ApplicationCommandHandlerSerial, 0,
      ApplicationControllerUpdate, 0, 0, 0, 0,
      ApplicationCommandHandlerSerial, SerialAPIStarted};

struct chip_descriptor chip_desc = {CHIP_DESCRIPTOR_UNINITIALIZED, CHIP_DESCRIPTOR_UNINITIALIZED};

/* Serial port polling loop */
static void
pollhandler(void)
{
  if (SerialAPI_Poll())
  {
    //Schedule more polling if needed
    process_poll(&serial_api_process);
  }

  secure_poll();
}

int serial_process_serial_init(const char *serial_port)
{
  unsigned char buf[14];
  int type;
  int fd = 0;


  LOG_PRINTF("Serial Process init\n");
  if (serial_port)
  {
    LOG_PRINTF("Using serial port %s\n", serial_port);
    serial_ok = SerialAPI_Init(serial_port, &serial_api_callbacks);
    if (!serial_ok)
    {
      ERR_PRINTF("Error opening serial API\n");
      process_exit(&serial_api_process);
      return 1;
    }
  }
  else
  {
    ERR_PRINTF("Please set serial port name\n");
    process_exit(&serial_api_process);
    return 1;
  }

  while ((type = ZW_Version(buf)) == 0)
  {
    WRN_PRINTF("Unable to communicate with serial API.\n");
  }
  LOG_PRINTF("Version: %s, type %i\n", buf, type);

  ZW_GetProtocolVersion(&zw_protocol_version2);
  DBG_PRINTF("SDK: %d.%02d.%02d, SDK Build no: %d\n",
      zw_protocol_version2.protocolVersionMajor,
      zw_protocol_version2.protocolVersionMinor,
      zw_protocol_version2.protocolVersionRevision,
      zw_protocol_version2.zaf_build_no);
  DBG_PRINTF(" SDK git hash: ");
  int i;
  for (i = 0; i < 16; i++) {
    printf("%x", zw_protocol_version2.git_hash_id[i]);
  }
  printf("\n");


  /* Abort if we find out that the Z-Wave module is not running a bridge library */
  if (ZW_LIB_CONTROLLER_BRIDGE != type)
  {
    ERR_PRINTF("Z-Wave dongle MUST be a Bridge library\n");
    serial_ok = FALSE;
    process_exit(&serial_api_process);
    return 1;
  }

  /* RFRegion and TXPowerlevel requires Z-Wave module restarts */
  int soft_reset = 0;
  SerialAPI_GetChipTypeAndVersion(&(chip_desc.my_chip_type), &(chip_desc.my_chip_version));
  /*
          * Set the TX powerlevel only if
          * 1. If it's not 500 series
          * 2. If the module supports the command and sub-command
          * 3. Valid powerlevel setting exists in zipgateway.cfg
          * 4. Current settings != settings in zipgateway.cfg
          */
  if ((chip_desc.my_chip_type == ZW_CHIP_TYPE)) {
      WRN_PRINTF("The module is 500 series. Ignoring NormalTxPowerLevel, "
                 "Measured0dBmPower, ZWRFRegion and MaxLRTxPowerLevel from config file.\n");
      goto exit;
  }
  if (!SerialAPI_SupportsCommand_func(FUNC_ID_SERIALAPI_SETUP) ||
      !SupportsSerialAPISetup_func(SERIAL_API_SETUP_CMD_TX_POWERLEVEL_GET) ||
      !SupportsSerialAPISetup_func(SERIAL_API_SETUP_CMD_TX_POWERLEVEL_SET)) {
      WRN_PRINTF("The module does not support FUNC_ID_SERIALAPI_SETUP,"
                 "SERIAL_API_SETUP_CMD_TX_POWERLEVEL_GET or "
                 "SERIAL_API_SETUP_CMD_TX_POWERLEVEL_SET. Ignoring NormalTxPowerLevel,"
                 "Measured0dBmPower from config file\n");
      goto skip_setting_power_level;
  }
  if (cfg.is_powerlevel_set != 1) {
      WRN_PRINTF("Either NormalTxPowerLevel or Measured0dBmPower or both not set"
                 "in config file. Need both to be set. Ignoring both.\n");
      goto skip_setting_power_level;
  }

  TX_POWER_LEVEL current_txpowerlevel = ZW_TX_POWERLEVEL_GET();
  if ((current_txpowerlevel.normal != cfg.tx_powerlevel.normal) || 
       (current_txpowerlevel.measured0dBm != cfg.tx_powerlevel.measured0dBm)) {
    LOG_PRINTF("Setting TX powerlevel to %d and output powerlevel to %d\n", 
               cfg.tx_powerlevel.normal, cfg.tx_powerlevel.measured0dBm);
    if (ZW_TXPowerLevelSet(cfg.tx_powerlevel)) {
      soft_reset = 1;
    } else {
      ERR_PRINTF("Failed to set the TX powerlevel through serial API\n");
    }
  } else {
    WRN_PRINTF("current tx_powerlevel.normal:%d and tx_powerlevel.measured0dBm:%d are same "
               "as what is set in config file %d %d. Ignoring NormalTxPowerLevel,"
                 "Measured0dBmPower from config file\n$", 
               current_txpowerlevel.normal, current_txpowerlevel.measured0dBm, 
               cfg.tx_powerlevel.normal, cfg.tx_powerlevel.measured0dBm);
  }
skip_setting_power_level:

  /*
          * Only set the region if
          * 1. it supprts Region Get/Set and not 500 series
          * 2. ZWRFRegion exists in zipgateway.cfg
          * 3. The retrieved current region is valid
          * 4. Current region != region in zipgateway.cfg
          */
  if (!SerialAPI_SupportsCommand_func(FUNC_ID_SERIALAPI_SETUP) ||
      !SupportsSerialAPISetup_func(SERIAL_API_SETUP_CMD_RF_REGION_GET) ||
      !SupportsSerialAPISetup_func(SERIAL_API_SETUP_CMD_RF_REGION_SET)) {
      WRN_PRINTF("The module does not support "
                 "FUNC_ID_SERIALAPI_SETUP/SERIAL_API_SETUP_CMD_RF_REGION_GET/"
                 "SERIAL_API_SETUP_CMD_RF_REGION_SET. Ignoring ZWRFRegion from config file\n");
      goto exit;
  }
  uint8_t current_region = ZW_RFRegionGet();
  if ((cfg.rfregion != 0xFE) && (current_region != 0xFE) && (current_region != cfg.rfregion))
  {
    LOG_PRINTF("Setting RF Region to %02x\n", cfg.rfregion);
    if (ZW_RFRegionSet(cfg.rfregion))
    {
      soft_reset = 1;
    }
    else
    {
      ERR_PRINTF("Failed to set the RF Region through serial API\n");
    }
  } else {
    WRN_PRINTF("Current RF Region:%d is same as what is set in config file: %d. not setting RF region\n",
               current_region, cfg.rfregion);
  }
  /* RFRegion and TX_Powerlevel requires Z-Wave module restart to take
          * effect.
          */
  if (soft_reset == 1)
  {
    LOG_PRINTF("Resetting the Z-Wave chip for changes to take effect\n");
    ZW_SoftReset();
    while ((type = ZW_Version(buf)) == 0)
    {
      LOG_PRINTF("Trying to communicate with serial API.\n");
    }
    LOG_PRINTF("Done resetting the Z-Wave chip\n");
    if ((ZW_RFRegionGet() != RF_US_LR) && (cfg.rfregion == RF_US_LR)) {
      ERR_PRINTF("RF region is not US LR after reset?\n");
    }
  }
skip_setting_rf_region: 

  if (ZW_RFRegionGet() == RF_US_LR) {
      if (!cfg.is_max_lr_powerlevel_set) {
        goto exit;
      }
      if (!SerialAPI_SupportsCommand_func(FUNC_ID_SERIALAPI_SETUP) ||
          !SupportsSerialAPISetup_func(SERIAL_API_SETUP_CMD_MAX_LR_TX_PWR_SET) ||
          !SupportsSerialAPISetup_func(SERIAL_API_SETUP_CMD_MAX_LR_TX_PWR_GET)) {
          ERR_PRINTF("Module does not support SERIAL_API_SETUP_CMD_MAX_LR_TX_PWR_GET/"
                     "SERIAL_API_SETUP_CMD_MAX_LR_TX_PWR_SET. Ignoring MaxLRTxPowerLevel from config file. \n");
          goto exit;
      }

      int16_t current_max_lr_txpowerlevel = ZW_MAXLRTXPowerLevelGet();
      LOG_PRINTF("Current MaxLRTxPowerLevel: %d \n", current_max_lr_txpowerlevel);
      if (current_max_lr_txpowerlevel == cfg.max_lr_tx_powerlevel) {
          WRN_PRINTF("Current MaxLRTxPowerLevel:%d is same as in config file: %d. Not changing it\n",
                     current_max_lr_txpowerlevel, cfg.max_lr_tx_powerlevel);
          goto exit;
      }

      if (cfg.max_lr_tx_powerlevel < -60) {
          WRN_PRINTF("Allowed minimum value for MAX LR TX powerlevel is -60. "
                     "So using -60 instead of %d. \n", cfg.max_lr_tx_powerlevel);
          cfg.max_lr_tx_powerlevel = -60;
      }
      if (cfg.max_lr_tx_powerlevel > 200) {
          WRN_PRINTF("Allowed maximum value for MAX LR TX powerlevel is 200. "
                     "So using 200 instead of %d. \n", cfg.max_lr_tx_powerlevel);
          cfg.max_lr_tx_powerlevel = 200;
      }

      LOG_PRINTF("Setting MAX LR TX powerlevel to %d \n", cfg.max_lr_tx_powerlevel);
      if (ZW_MAXLRTXPowerLevelSet(cfg.max_lr_tx_powerlevel)) {
         ZW_SoftReset();
         while ((type = ZW_Version(buf)) == 0)
         {
           LOG_PRINTF("Trying to communicate with serial API.\n");
         }
         LOG_PRINTF("Done resetting the Z-Wave chip\n");
         current_max_lr_txpowerlevel = ZW_MAXLRTXPowerLevelGet();
         if (current_max_lr_txpowerlevel != cfg.max_lr_tx_powerlevel) {
           LOG_PRINTF("Error Setting MAX LR TX powerlevel to %d. Current is: %d \n",
                      cfg.max_lr_tx_powerlevel, current_max_lr_txpowerlevel);
         }
      } else {
        ERR_PRINTF("Failed to set the MaxLRTxPowerLevel through serial API\n");
      }
  }
exit:
  if (ZW_RFRegionGet() == RF_US_LR) {
    if (SerialAPI_EnableLR() == false) {
      LOG_PRINTF("Failed to enable Z-Wave Long Range capability\n");
    }
  } else {
    if (SerialAPI_DisableLR() == false) {
      LOG_PRINTF("Fail to disable Z-Wave Long Range capability\n");
    }
  }
  ZW_AddNodeToNetwork(ADD_NODE_STOP, 0);
  ZW_RemoveNodeFromNetwork(REMOVE_NODE_STOP, 0);
  ZW_SetLearnMode(ZW_SET_LEARN_MODE_DISABLE, 0);

  if (ZW_GECKO_CHIP_TYPE(chip_desc.my_chip_type))
  {
    SerialAPI_WatchdogStart();
  }

  process_poll(&serial_api_process);
  return 0;
}


PROCESS_THREAD(serial_api_process, ev, data)
{
  /****************************************************************************/
  /*                              PRIVATE DATA                                */
  /****************************************************************************/
  PROCESS_POLLHANDLER(pollhandler());
  PROCESS_EXITHANDLER(SerialAPI_Destroy());
  PROCESS_BEGIN();
  while (1)
  {
    switch (ev)
    {
    case PROCESS_EVENT_INIT:
      return serial_process_serial_init((const char *)data);
    break;
    }
    PROCESS_WAIT_EVENT();
  }
  SerialFlush();
  PROCESS_END()
}
