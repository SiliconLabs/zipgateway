/* © 2019 Silicon Laboratories Inc. */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <contiki-net.h>
#include "ZW_ZIPApplication.h"
#include "Serialapi.h"
#include "ZW_classcmd_ex.h"
#include "ZW_classcmd.h"

#include "zip_router_ipv6_utils.h" /* nodeOfIP */
#include "ZIP_Router.h" /* zip_process */
#include "NodeCache.h"
#include "uip-ds6.h"
#include "sys/rtimer.h"
#include "ZW_udp_server.h"
#include "ZW_tcp_client.h"
#include "CC_FirmwareUpdate.h"
#include "ZW_SendDataAppl.h"
#include "command_handler.h"

#include "Bridge.h"
#include "DataStore.h"
#include "ZIP_Router_logging.h"
#include "zgw_crc.h"
#include "Serialapi.h"

#define UPGRADE_ZWAVE_TIMEOUT  500   //5 seconds
#define UPGRADE_UDP_TIMEOUT    300   //3 seconds
#define UPGRADE_START_TIMEOUT  300
#define UPGRADE_TIMER_REPEATS  3

#define NON_BLOCK_MODE	0x0;
#define BLOCK_MODE	0x1

static u8_t updatemode = BLOCK_MODE;
static u16_t upgradeTimeout = UPGRADE_UDP_TIMEOUT;

void
uip_debug_ipaddr_print(const uip_ipaddr_t *addr);

PROCESS_NAME(serial_api_process);

/** Firmware IDs */
/*16 bit value*/
enum
{
   /** Series 500 or Series 700 firmware. */
  ZW_FW_ID = 0,
};

/*8 bit value*/
/** Firmware target.
 * Index of the Firmware in the target list given in the FW MD Report. */
enum
{
  ZW_FW_TARGET_ID = 0,
};

typedef struct _zw_fw_header_st
{
  u8_t Signature[4];
  u32_t FileLen;
} zw_fw_header_st_t;

#define WAIT_TIME_ZERO				0x0
#define WAIT_TIME_ZIPR_FW_UPDATE    30     //Seconds
#define WAIT_TIME_ZW_FW_UPDATE	    60     //Seconds

#define WAIT_TIME_FW_SSL_UPDATE    240    //Seconds

#define ERROR_INVALID_CHECKSUM_FATAL				0x0
#define ERROR_TIMED_OUT								0x1
#define ERROR_LASTUSED								ERROR_TIMED_OUT
#define UPGRADE_SUCCESSFUL_NO_POWERCYCLE			0xFE
#define UPGRADE_SUCCESSFUL_POWERCYCLE				0xFF

#define FW_UPDATE_MAX_SEGMENT_SIZE 500
#define FW_UPDATE_MIN_SEGMENT_SIZE 40

enum
{
  INVALID_COMBINATION = 0x00,
  OUT_OF_BAND_AUTH_REQUIRED = 0x01,
  EXCEEDS_MAX_FRAGMENT_SIZE = 0x02,
  FIRMWARE_NOT_UPGRADABALE = 0x03,
  INVALID_HARDWARE_VERSION = 0x4,
  //FIRMWARE_UPDATE_IN_PROGESS = 0x05, // This status code is not in the spec

  INITIATE_FWUPDATE = 0xFF,
};

#define MAX_RETRY   3   //(1st time + 2 retries)

static void
FwUpdate_Reset_Var(void);
static void
FwUpdateMdReqReport_SendTo(zwave_connection_t* c, BYTE status);
static void
FwUpdate_StatusReport_Send(BYTE status, WORD waitTimeSec);
static void
FwUpdateDataGet_Send(void);

static void
activate_image(uint16_t firmware_id, uint8_t firmware_target, uint16_t crc,
               uint32_t firmware_len, uint8_t activation);

static void
FwUpdate_Handler(BYTE* pData, WORD bDatalen);
static void
FwUpdate_Timeout(void);
static void
FwStatus_Timeout(void);
static void
FwUpdateActivationReport_SendTo(zwave_connection_t *c, BYTE status);
static void send_firmware_prepare_report(zwave_connection_t *c, u8_t status);


#define BLOCK_REQ_NO  255

static u16_t blockno = 0;
static u16_t reqno = 0;

static u8_t upgradeTimer = 0xFF;
static u8_t statusTimer = 0xFF;
static u8_t retrycnt = 0;
static u8_t status_retrycnt = 0;
static struct image_descriptor fw_desc;

/** The name template for 700 series fw images. */
char fw_filename[256] = "/tmp/zipgateway_current_fw_fileXXXXXX";

/** The firmware update file name. */
char fw_curr_filename[256] = {0};

FILE *fw_file = NULL;

uint16_t build_crc;

static uint8_t bActivationRequest;
static u8_t bVersion; //the version which we are currently using

static u16_t fwmss = 0x0;
static zwave_connection_t ups;

extern uint8_t gisZIPRReady;

extern u16_t
chksum(u16_t sum, const u8_t *data, u16_t len);

ZW_FIRMWARE_UPDATE_MD_REQUEST_REPORT_V3_FRAME gfwUpdateMdReqReportV3;
ZW_FIRMWARE_MD_REPORT_1ID_V5_FRAME gfwMdReportV5;
ZW_FIRMWARE_UPDATE_MD_GET_V3_FRAME gupdateMdGet;
ZW_FIRMWARE_UPDATE_MD_STATUS_REPORT_V3_FRAME gstatusReport;

/** Create a temporary file for a 700 series fw image.
 * The file name is based on \ref fw_filename will be put in \ref
 * fw_curr_filename.
 *
 * \return Open file pointer or NULL if something failed. */
static bool fw_tmp_file_create(void);

/* TODO: we may want to store the image in memory instead of using a
   file.  In that case, we probably want to use the index
   parameter. */
/** Append a block to \ref fw_file.
 * If fw_file is NULL, the function writes nothing and returns FALSE.
 *
 * \param buf Pointer to the data to append.
 * \param len The number of bytes to append.
 * \return TRUE is write succeeded, FALSE otherwise.
 */
static bool fw_tmp_file_append(uint8_t *buf, uint16_t len);

/**
 * Write a block to specified offset to \ref fw_file
 * \param offset Position, measured in bytes, to be written with respect to whence
 * \param whence SEEK_SET, SEEK_CUR, SEEK_END indicates the offset is relative to the start of the file, the current position indicator, or end-of-file, respectively.
 * \param buf pointer to the data to write
 * \param len the number of bytes to write.
 * \return bytes get written
 */
static size_t fw_tmp_file_write(long offset, int whence, uint8_t *buf, uint16_t len);

/**
 * Get the size of \ref fw_file
 * \return file size of fw_file
 */
static long fw_tmp_file_size();


#ifndef AXTLS
#include <openssl/md5.h>
uint8_t
MD5_ValidatFwImg(uint32_t len) REENTRANT
{
  uint32_t noOf256blks = 0, residue = 0, index = 0, i = 0;
  uint8_t buf[450] =
    { 0 };
  uint8_t digest[16] =
    { 0 };
  fw_file = fopen(fw_curr_filename, "r");

  MD5_CTX context;

  noOf256blks = len / sizeof(buf);

  residue = len % sizeof(buf);

  MD5_Init(&context);

  for (i = 0; i < noOf256blks; i++)
  {
    fread(buf, sizeof(buf), 1, fw_file);
    MD5_Update(&context, buf, sizeof(buf));
    index += sizeof(buf);
  }

  if (residue)
  {
    fread(buf, residue, 1, fw_file);
    MD5_Update(&context, buf, residue);
    index += residue;
#if 0
    printf("residue:  index = 0x%lx residue=0x%lx : \r\n", index, residue);
    for(i=0; i < residue; i++)
    {
      printf("0x%bx ",buf[i]);
    }
    printf("\r\n");
#endif

  }

  MD5_Final(digest, &context);

  //read md5 checksum
  fread(buf, 16, 1, fw_file);

  //printf("baseaddr = 0x%lx,  len = 0x%lx index = 0x%lx\r\n", baseaddr, len, index);

  printf("calculated checksum : \r\n");
  for (i = 0; i < 16; i++)
  {
    printf("0x%xc ", digest[i]);
  }
  printf("\r\n");

#if 0
  printf("Read index: \r\n");
  for(i=0; i < 16; i++)
  {
    printf("0x%bx ",buf[i]);
  }
  printf("\r\n");
#endif
  fclose(fw_file);
  if (!memcmp(digest, buf, 16))
  {
    printf("MD5 checksum passed.\r\n");
    return TRUE;
  }
  else
  {
    printf("MD5 checksum failed.\r\n");
    return FALSE;
  }
}
#endif

u8_t
gconfig_ValidatZwFwImg(u32_t downloadlen, u32_t maxlen)
{
  zw_fw_header_st_t zwfw_header;

  fw_file = fopen(fw_curr_filename, "r");
  fread((void *)&zwfw_header, sizeof(zwfw_header), 1, fw_file);
  fclose(fw_file);

  /*Convert to network byte order*/
  zwfw_header.FileLen = UIP_HTONL(zwfw_header.FileLen);

  printf("gconfig_ValidatZwFwImg: downloadlen = 0x%x headerlen = 0x%x \r\n", downloadlen, zwfw_header.FileLen);

  //printf("MAX_FW_LEN = 0x%lx\r\n", (unsigned long)maxlen);
  if (downloadlen < zwfw_header.FileLen)
  {
    printf("Download len and Fw header len not matched.\r\n");
    return FALSE;
  }
  else if (!((zwfw_header.Signature[0] == 'Z') && (zwfw_header.Signature[1] == 'W') && (zwfw_header.Signature[2] == 'F')
      && (zwfw_header.Signature[3] == 'W')))
  {
    printf("Firmware signature doesn't match.\r\n");
    return FALSE;
  }
  else if ((zwfw_header.FileLen == 0) || (zwfw_header.FileLen > maxlen))
  {
    printf("Invalid firmware len in the header.\r\n");
    return FALSE;
  }
  else
  {
    return MD5_ValidatFwImg(zwfw_header.FileLen);
  }
}

/*
 * Call back function for the firmware meta data get request send
 */
static void
fwupdate_get_callback(BYTE txstatus, void* user, TX_STATUS_TYPE *t)
{

  DBG_PRINTF("fwupdate_get_callback: txstatus = 0x%02x\r\n", txstatus);

  if (nodeOfIP(&ups.ripaddr) || ZW_IsZWAddr(&ups.ripaddr))
  {
    if (txstatus == TRANSMIT_COMPLETE_OK)
    {
      if (upgradeTimer != 0xFF)
      {
        ZW_LTimerCancel(upgradeTimer);
        upgradeTimer = 0xFF;
      }

      upgradeTimer = ZW_LTimerStart(FwUpdate_Timeout, upgradeTimeout,
      UPGRADE_TIMER_REPEATS);
    }
    else
    {
      if (upgradeTimer != 0xFF)
      {
        ZW_LTimerCancel(upgradeTimer);
        upgradeTimer = 0xFF;
      }

      if (retrycnt >= MAX_RETRY)
      {
        FwUpdate_StatusReport_Send(ERROR_TIMED_OUT, WAIT_TIME_ZERO);
        ERR_PRINTF("fwupdate_get_callback: Error timed out\r\n");
      }
      else
      {
        DBG_PRINTF("fwupdate_get_callback: retrycnt = %u\r\n", retrycnt);
        FwUpdateDataGet_Send();
        retrycnt++;
      }
    }
  }
  else
  {
    if (upgradeTimer == 0xFF)
    {
      upgradeTimer = ZW_LTimerStart(FwUpdate_Timeout, upgradeTimeout,
      UPGRADE_TIMER_REPEATS);
      DBG_PRINTF("Starting the upgrade timer.. upgradeTimer = 0x%02x\r\n", upgradeTimer);
    }
  }
}

/*
 * Call back function for the firmware meta data status send
 */

static void
fwupdate_status_callback(BYTE txstatus, void* user, TX_STATUS_TYPE *t)
{

  DBG_PRINTF("fwupdate_status_callback: txstatus = 0x%02x\r\n", txstatus);

  if (nodeOfIP(&ups.ripaddr) || ZW_IsZWAddr(&ups.ripaddr))
  {
    if (txstatus == TRANSMIT_COMPLETE_OK)
    {
      ZW_LTimerCancel(upgradeTimer);
      ZW_LTimerCancel(statusTimer);
      FwUpdate_Reset_Var();
    }
    else
    {
      if (status_retrycnt >= MAX_RETRY)
      {
        FwUpdate_Reset_Var();
        ERR_PRINTF("FwStatus_Timeout: Error timed out\r\n");
      }
      else
      {
        status_retrycnt++;
        //gstatusReport is global status structure..report still be stored here
        ZW_SendDataZIP(&ups, (BYTE*) &gstatusReport, sizeof(ZW_FIRMWARE_UPDATE_MD_STATUS_REPORT_V3_FRAME),
            fwupdate_status_callback);
      }
    }
  }
  else
  {
    if (txstatus == TRANSMIT_COMPLETE_OK)
    {

      ZW_LTimerCancel(upgradeTimer);
      ZW_LTimerCancel(statusTimer);
      FwUpdate_Reset_Var();
    }
    else
    {
      if (statusTimer == 0xFF)
      {
        statusTimer = ZW_LTimerStart(FwStatus_Timeout, upgradeTimeout,
        UPGRADE_TIMER_REPEATS);
      }
    }
  }
}

/*
 *  Firmware update command class handler
 *  Returns TRUE if received command is supported command.
 *  Returns FALSE if received command is non-supported command(e.g. meta data get and firmware update status)
 */
command_handler_codes_t
Fwupdate_Md_CommandHandler(zwave_connection_t *c,uint8_t* pData, uint16_t bDatalen)
{

  ZW_APPLICATION_TX_BUFFER * pCmd = (ZW_APPLICATION_TX_BUFFER*) pData;

  DBG_PRINTF("Fwupdate_Md_CommandHandler: pCmd->ZW_Common.cmd = %02x fw_desc.desc = %u\n", pCmd->ZW_Common.cmd,
      fw_desc.firmware_id);

  switch (pCmd->ZW_Common.cmd)
  {
  case FIRMWARE_MD_GET_V5:
    {
       ZW_FIRMWARE_MD_REPORT_1ID_V5_FRAME *fwMdReportV5 = &gfwMdReportV5;

      memset(fwMdReportV5, 0, sizeof(ZW_FIRMWARE_MD_REPORT_1ID_V5_FRAME));

      fwMdReportV5->cmdClass = COMMAND_CLASS_FIRMWARE_UPDATE_MD_V5;
      fwMdReportV5->cmd = FIRMWARE_MD_REPORT_V5;

      fwMdReportV5->manufacturerId1 = (cfg.manufacturer_id >> 8) & 0xff;
      fwMdReportV5->manufacturerId2 = (cfg.manufacturer_id >> 0) & 0xff;

      fwMdReportV5->firmware0Id1 = 0x00;
      fwMdReportV5->firmware0Id2 = 0x00;
      fwMdReportV5->firmware0Checksum1 = 0x00;
      fwMdReportV5->firmware0Checksum2 = 0x00;
      fwMdReportV5->firmwareUpgradable = 0x00;

      fwMdReportV5->numberOfFirmwareTargets = ZIPGW_NUM_FW_TARGETS;
      if (nodeOfIP(&c->ripaddr) || ZW_IsZWAddr(&c->ripaddr))
      {
        fwMdReportV5->maxFragmentSize1 = (FW_UPDATE_MIN_SEGMENT_SIZE >> 8) & 0xFF;
        fwMdReportV5->maxFragmentSize2 = FW_UPDATE_MIN_SEGMENT_SIZE & 0xFF;
      }
      else
      {
        fwMdReportV5->maxFragmentSize1 = (FW_UPDATE_MAX_SEGMENT_SIZE >> 8) & 0xFF;
        fwMdReportV5->maxFragmentSize2 = FW_UPDATE_MAX_SEGMENT_SIZE & 0xFF;
      }
      fwMdReportV5->firmwareId11 = cfg.hardware_version >> 8;
      fwMdReportV5->firmwareId12 = cfg.hardware_version & 0xFF;
      fwMdReportV5->hardwareVersion = cfg.hardware_version;
      ZW_SendDataZIP(c, (BYTE*) fwMdReportV5,
                     sizeof(ZW_FIRMWARE_MD_REPORT_1ID_V5_FRAME),
                     NULL);
    }
    break;

  case FIRMWARE_UPDATE_MD_REPORT_V3:
    {
      if (fw_desc.firmware_id == ZW_FW_ID)
      {
        FwUpdate_Handler(pData, bDatalen);
      }
      else
      {
        DBG_PRINTF("FIRMWARE_UPDATE_MD_REPORT_V3: Device is not ready\r\n");
      }
    }
    break;

  case FIRMWARE_UPDATE_ACTIVATION_SET_V4:
    {
      uint8_t hw_version;
      uint16_t manuId;

      WORD manufacturerId = (pCmd->ZW_FirmwareUpdateActivationSetV4Frame.manufacturerId1 << 8)
          | pCmd->ZW_FirmwareUpdateActivationSetV4Frame.manufacturerId2;
      WORD firmwareID = (pCmd->ZW_FirmwareUpdateActivationSetV4Frame.firmwareId1 << 8)
          | pCmd->ZW_FirmwareUpdateActivationSetV4Frame.firmwareId2;
      WORD crc = (pCmd->ZW_FirmwareUpdateActivationSetV4Frame.checksum1 << 8)
          | pCmd->ZW_FirmwareUpdateActivationSetV4Frame.checksum2;

      BYTE fwTarget = pCmd->ZW_FirmwareUpdateActivationSetV4Frame.firmwareTarget;

      hw_version = ((uint8_t*) pCmd)[9];

      if ((bDatalen > sizeof(pCmd->ZW_FirmwareUpdateActivationSetV4Frame)) && (hw_version != cfg.hardware_version))
      {
        FwUpdateActivationReport_SendTo(c, INVALID_COMBINATION);
        return COMMAND_HANDLED;
      }

      if (fw_desc.firmware_id != 0xFFFF)
      {
        FwUpdateActivationReport_SendTo(c, 0x01); //Generic error
        return COMMAND_HANDLED;
      }

      ups = *c;
      activate_image(firmwareID, fwTarget, crc, fw_desc.firmware_len, 1);
      return COMMAND_HANDLED;
    }
    break;

  case FIRMWARE_UPDATE_MD_REQUEST_GET_V4:
    {
      WORD manufacturerId = (pCmd->ZW_FirmwareUpdateMdRequestGetV3Frame.manufacturerId1 << 8)
          | pCmd->ZW_FirmwareUpdateMdRequestGetV3Frame.manufacturerId2;
      WORD firmwareID = (pCmd->ZW_FirmwareUpdateMdRequestGetV3Frame.firmwareId1 << 8)
          | pCmd->ZW_FirmwareUpdateMdRequestGetV3Frame.firmwareId2;
      BYTE fwTarget = pCmd->ZW_FirmwareUpdateMdRequestGetV3Frame.firmwareTarget;
      uint16_t fragmentSize = (pCmd->ZW_FirmwareUpdateMdRequestGetV3Frame.fragmentSize1 << 8)
          | pCmd->ZW_FirmwareUpdateMdRequestGetV3Frame.fragmentSize2;
      uint16_t crc = (pCmd->ZW_FirmwareUpdateMdRequestGetV3Frame.checksum1 << 8)
          | pCmd->ZW_FirmwareUpdateMdRequestGetV3Frame.checksum2;

      uint16_t max_allowed_fragSize;

      if (bDatalen < 8)
      {
        ERR_PRINTF("Parse error FW Upd Req Get\n");
        return COMMAND_PARSE_ERROR;
      }

      //Validate the md request get command
      if (fw_desc.firmware_id != 0xFFFF)
      {
        //firmware upgrade in progress return error from here
        /* We should really be returning FIRMWARE_UPDATE_IN_PROGRESS,
         * but unfortunately it did not make it into the spec */
        FwUpdateMdReqReport_SendTo(c, FIRMWARE_NOT_UPGRADABALE);
        DBG_PRINTF("Firmware/Cert Upgrade in Progress. !!!\r\n");
        return COMMAND_HANDLED;
      }

      bActivationRequest = 0;
      if (bDatalen == 8)
      { //Verison 1 and 2
        bVersion = 2;
        fwTarget = ZW_FW_TARGET_ID;
        firmwareID = ZW_FW_ID;
        fragmentSize = 40;

      }
      if (bDatalen >= 12)
      { //Version 4
        bVersion = 4;

        bActivationRequest = pData[11] & 1;
      }
      if (bDatalen >= 13)
      {
        bVersion = 5;
      }

      if (manufacturerId != cfg.manufacturer_id)
      {
        FwUpdateMdReqReport_SendTo(c, INVALID_COMBINATION);
        return COMMAND_HANDLED;
      }

      if (bVersion >= 5)
      { //Version 5
        if (pData[12] != cfg.hardware_version)
        {
          ERR_PRINTF("Wrong hardware version, pData[12]: 0x%02x cfg.hardware_version: 0x%02x\n", pData[12], cfg.hardware_version);
          FwUpdateMdReqReport_SendTo(c, INVALID_HARDWARE_VERSION);
          return COMMAND_HANDLED;
        }
      }

      if ((0 != nodeOfIP(&c->ripaddr))
           || ZW_IsZWAddr(&c->ripaddr)) /* TODO: Is ZW_IsZWAddr() even relevant any more? */
                                        /* Or should it be replaced by odeOfIP everywhere? */
       {
         max_allowed_fragSize = FW_UPDATE_MIN_SEGMENT_SIZE;
       }
       else
       {
         max_allowed_fragSize = FW_UPDATE_MAX_SEGMENT_SIZE;
       }
      if ((fragmentSize > max_allowed_fragSize)
          || (fragmentSize == 0))
      {
         DBG_PRINTF("Requested fragment size: %d\n", fragmentSize);
        //Invalid max segment size
        FwUpdateMdReqReport_SendTo(c, EXCEEDS_MAX_FRAGMENT_SIZE);
        return COMMAND_HANDLED;
      }

      // Return NOT_UPGRADABLE for all cases
      DBG_PRINTF("Invalid Firmware Target.\r\n");
      FwUpdateMdReqReport_SendTo(c, FIRMWARE_NOT_UPGRADABALE);
      return COMMAND_HANDLED;

      break;
    }

  /* Firmware download(backup) is not supported and backup/recover is the recommended way. */
  case FIRMWARE_UPDATE_MD_PREPARE_GET:
    {
      ZW_FIRMWARE_UPDATE_MD_PREPARE_GET_V5_FRAME* f = (ZW_FIRMWARE_UPDATE_MD_PREPARE_GET_V5_FRAME*) pData;
      uint16_t firmwareID;
      uint8_t returnStatus;

      if (bDatalen < sizeof(ZW_FIRMWARE_UPDATE_MD_PREPARE_GET_V5_FRAME))
      {
        return COMMAND_PARSE_ERROR;
      }
      firmwareID = (f->firmwareId1 << 8) + f->firmwareId2;

      DBG_PRINTF("Firmware ID 0x%04x firmware target 0x%02x "
                 "not available for backup\n", firmwareID , f->firmwareTarget);
      returnStatus = FIRMWARE_NOT_UPGRADABALE;
      send_firmware_prepare_report(c, returnStatus);
      return COMMAND_HANDLED;
    }
    break;

  case FIRMWARE_UPDATE_MD_GET_V3:
    {
      ZW_FIRMWARE_UPDATE_MD_GET_V3_FRAME *f = (ZW_FIRMWARE_UPDATE_MD_GET_V3_FRAME *) pData;

      WRN_PRINTF("No supported firmware target found for downloading\n");
      return COMMAND_HANDLED;
    }
    break;

  default:
    DBG_PRINTF("This FW update command is not for the GW. cmd = 0x%02x bDatalen = %u \r\n", pCmd->ZW_Common.cmd,
        bDatalen);
    return COMMAND_NOT_SUPPORTED;
    break;
  }

  return COMMAND_HANDLED;
}

static void
send_firmware_prepare_report(zwave_connection_t *c, u8_t status)
{
  ZW_FIRMWARE_UPDATE_MD_PREPARE_REPORT_V5_FRAME f;

  f.cmdClass = COMMAND_CLASS_FIRMWARE_UPDATE_MD_V3;
  f.cmd = FIRMWARE_UPDATE_MD_PREPARE_REPORT;

  if(INITIATE_FWUPDATE == status) {
    uint8_t* buf;
    uint16_t crc;
    unsigned long size = 0;
    fw_file = fopen(fw_curr_filename, "r");
    if (fw_file == NULL) {
      ERR_PRINTF("Firmware temporary file open failed\n");
    }
    size = fw_tmp_file_size();

    buf = (uint8_t*) malloc(size);
    size_t read_count = fread(buf, size, 1, fw_file);
    if (read_count != size) {
      ERR_PRINTF("Firmware read size is not expected\n");
    }
    fclose(fw_file);
    crc = zgw_crc16(CRC_INIT_VALUE, buf, size);
    free(buf);

    f.firmwareChecksum1 = (crc>>8) & 0xFF;
    f.firmwareChecksum2 = (crc>>0) & 0xFF;
  } else {
    f.firmwareChecksum1 = 0;
    f.firmwareChecksum2 = 0;
  }
  f.status = status;
  ZW_SendDataZIP(c, &f, sizeof(f), NULL);
}

/*
 * Firmware update status send timeout handler
 */
static void
FwStatus_Timeout(void)
{

  if (status_retrycnt >= MAX_RETRY)
  {
    FwUpdate_Reset_Var();
    ERR_PRINTF("FwStatus_Timeout: Error timed out\r\n");
  }
  else
  {
    status_retrycnt++;
    ZW_SendDataZIP(&ups, (BYTE*) &gstatusReport, sizeof(ZW_FIRMWARE_UPDATE_MD_STATUS_REPORT_V3_FRAME),
        fwupdate_status_callback);
  }
  return;
}

/*
 * Firmware meta data get timeout handler
 */

static void
FwUpdate_Timeout(void)
{
  if (retrycnt >= MAX_RETRY)
  {
    FwUpdate_StatusReport_Send(ERROR_TIMED_OUT, WAIT_TIME_ZERO);
    ERR_PRINTF("FwUpdate_Timeout: Error timed out\r\n");
  }
  else
  {
    DBG_PRINTF("FwUpdate_Timeout: calling re-request blockno = %u\r\n", blockno);
    FwUpdateDataGet_Send();
    retrycnt++;
  }

  return;
}

/*
 * Send Firmware update meta data get request to peer/requested node
 */
static void
FwUpdateDataGet_Send(void)
{
  ZW_FIRMWARE_UPDATE_MD_GET_V3_FRAME *updateMdGet = &gupdateMdGet;

  DBG_PRINTF("FwUpdateDataGet_Send  blockno = %u\r\n", blockno);

  updateMdGet->cmdClass = COMMAND_CLASS_FIRMWARE_UPDATE_MD_V3;
  updateMdGet->cmd = FIRMWARE_UPDATE_MD_GET_V3;

  if (updatemode == BLOCK_MODE)
  {
    updateMdGet->numberOfReports = BLOCK_REQ_NO; //Block request
    reqno = blockno;
  }
  else
  {
    updateMdGet->numberOfReports = 1; //we will send ACK for each report
  }

  updateMdGet->properties1 = (blockno >> 8) & 0x7F;
  updateMdGet->reportNumber2 = blockno & 0xFF;

  ZW_SendDataZIP(&ups, (BYTE*) updateMdGet, sizeof(ZW_FIRMWARE_UPDATE_MD_GET_V3_FRAME), fwupdate_get_callback);

  return;
}

/*
 * Send Firmware update meta data req report to specified destination and port number
 */
void
FwUpdateMdReqReport_SendTo(zwave_connection_t *c, BYTE status)
{
  ZW_FIRMWARE_UPDATE_MD_REQUEST_REPORT_V3_FRAME * fwUpdateMdReqReportV3 = &gfwUpdateMdReqReportV3;

  DBG_PRINTF("FwUpdateMdReqReport_Send with status = %02x\r\n", status);

  fwUpdateMdReqReportV3->cmdClass = COMMAND_CLASS_FIRMWARE_UPDATE_MD_V3;
  fwUpdateMdReqReportV3->cmd = FIRMWARE_UPDATE_MD_REQUEST_REPORT_V3;

  fwUpdateMdReqReportV3->status = status;

  ZW_SendDataZIP(c, (BYTE*) fwUpdateMdReqReportV3, sizeof(ZW_FIRMWARE_UPDATE_MD_REQUEST_REPORT_V3_FRAME), NULL);

  return;
}

/*
 * Send Firmware update activation report
 */
static void
FwUpdateActivationReport_SendTo(zwave_connection_t *c, BYTE status)
{
  ZW_FIRMWARE_UPDATE_ACTIVATION_STATUS_REPORT_V5_FRAME f;

  f.cmdClass = COMMAND_CLASS_FIRMWARE_UPDATE_MD_V4;
  f.cmd = FIRMWARE_UPDATE_ACTIVATION_STATUS_REPORT_V4;
  f.manufacturerId1 = (cfg.manufacturer_id >> 8) & 0xFF;
  f.manufacturerId2 = (cfg.manufacturer_id >> 0) & 0xFF;
  f.firmwareId1 = (fw_desc.firmware_id >> 8) & 0xFF;
  f.firmwareId2 = (fw_desc.firmware_id >> 0) & 0xFF;
  f.checksum1 = (fw_desc.crc >> 8) & 0xff;
  f.checksum2 = (fw_desc.crc >> 0) & 0xff;
  f.firmwareTarget = fw_desc.target;
  f.firmwareUpdateStatus = status;
  f.hardwareVersion = cfg.hardware_version;

  ZW_SendDataZIP(c, (BYTE*) &f, sizeof(ZW_FIRMWARE_UPDATE_ACTIVATION_STATUS_REPORT_V5_FRAME), NULL);

  return;
}

/*
 * Send firmware status report to peer/requested node
 */
void
FwUpdate_StatusReport_Send(BYTE status, WORD waitTimeSec)
{
  ZW_FIRMWARE_UPDATE_MD_STATUS_REPORT_V3_FRAME *statusReport = &gstatusReport;

  statusReport->cmdClass = COMMAND_CLASS_FIRMWARE_UPDATE_MD_V3;
  statusReport->cmd = FIRMWARE_UPDATE_MD_STATUS_REPORT_V3;
  statusReport->status = status;
  statusReport->waittime1 = (waitTimeSec >> 8) & 0xFF;
  statusReport->waittime2 = waitTimeSec & 0xFF;

  status_retrycnt++;

  DBG_PRINTF("FwUpdate_StatusReport_Send with satus = %02x\r\n", status);

  ZW_SendDataZIP(&ups, (BYTE*) statusReport, sizeof(ZW_FIRMWARE_UPDATE_MD_STATUS_REPORT_V3_FRAME),
      fwupdate_status_callback);

  return;
}

static void
FwUpdate_Reset_Var(void)
{
  retrycnt = 0;
  status_retrycnt = 0;
  upgradeTimer = 0xFF;
  statusTimer = 0xFF;
  blockno = 0;
  fw_desc.firmware_id = 0xFFFF;
  fwmss = 0;
  reqno = 0;
}

static bool fw_tmp_file_create() {
   int fd;

   if (strlen(fw_curr_filename) == 0) {
      /* TODO: delete the old file, if it exists? */
   }
   memcpy(fw_curr_filename, fw_filename, strlen(fw_filename));
   fd = mkstemp(fw_curr_filename);
   if (fd == -1) {
      ERR_PRINTF("Could not create temp file (%s) for firmware image: Error: %s\n",
                 fw_filename, strerror(errno));
      return false;
   } else {
      DBG_PRINTF("Created temp file %s for firmware image\n", fw_filename);
   }
   fw_file = fdopen(fd, "w");
   if (!fw_file) {
      ERR_PRINTF("Could not open temp file (%s) for firmware update: Error: %s\n",
                 fw_filename, strerror(errno));
      close(fd);
      return false;
   }
   fclose(fw_file);
   return true;
}

/** Gecko and 500 series file writer. */
static bool fw_tmp_file_append(uint8_t *buf, uint16_t len) {
   size_t written = 0;
   int file_error = FALSE;

   fw_file = fopen(fw_curr_filename, "a");
   if (fw_file) {
      DBG_PRINTF("fw_file opened\n");
   }
   if (fw_file) {
      written = fwrite(buf, len, 1, fw_file);
      file_error = fclose(fw_file);
      fw_file = NULL;
      if (written != 1) {
         ERR_PRINTF("Could not write to temp file (%s) for firmware update: Error: %s\n",
                    fw_curr_filename, strerror(errno));
         return FALSE;
      }
      if (file_error) {
         ERR_PRINTF("Could not close temp file (%s) for firmware update: Error: %s\n",
                    fw_filename, strerror(errno));
         return FALSE;
      }
      return TRUE;
   } else {
      DBG_PRINTF("No file\n");
      return FALSE;
   }
}

static size_t fw_tmp_file_write(long offset, int whence, uint8_t *buf, uint16_t len) {
  fw_file = fopen(fw_curr_filename, "w");
  if (fw_file == NULL) {
    ERR_PRINTF("Firmware temporary file open failed\n");
    return 0;
  }
  fseek(fw_file, offset, whence);
  size_t written = fwrite(buf, len, 1, fw_file);
  fclose(fw_file);
  return written;
}

static long fw_tmp_file_size() {
  fw_file = fopen(fw_curr_filename, "r");
  if (fw_file) {
    fseek(fw_file, 0, SEEK_END);
    return ftell(fw_file);
  }
  return 0;
}

/*
 * ZIPR Firmware and Z-wave FW meta data reports handler
 */
void
FwUpdate_Handler(BYTE* pData, WORD bDatalen)
{
  ZW_APPLICATION_TX_BUFFER* pCmd = (ZW_APPLICATION_TX_BUFFER*) pData;
#ifdef __ASIX__
  uint16_t payload_len = 0;
#endif
  uint32_t index = 0, dat_len = 0, downloadlen = 0;
  uint16_t calcrc = CRC_INIT_VALUE;
  int status = 0;
  //uint8_t buf[410] = {0};

  if (blockno
      != (((pCmd->ZW_FirmwareUpdateMdReport1byteV3Frame.properties1 & 0x7F) << 8)
          | (pCmd->ZW_FirmwareUpdateMdReport1byteV3Frame.reportNumber2)))
  {
    DBG_PRINTF("Invalid/Duplicate block no = %u\r\n",
        (unsigned )(((pCmd->ZW_FirmwareUpdateMdReport1byteV3Frame.properties1 & 0x7F) << 8)
            | (pCmd->ZW_FirmwareUpdateMdReport1byteV3Frame.reportNumber2)));
    return;
  }

  if ((bDatalen - (sizeof(ZW_FIRMWARE_UPDATE_MD_REPORT_1BYTE_V3_FRAME) - 1)) == 0)
  {
    DBG_PRINTF("Firmware packet with zero len received.\r\n");
    return;
  }

  //Stop the timer
  ZW_LTimerCancel(upgradeTimer);
  upgradeTimer = 0xFF;

  //it is the last block
  if (pCmd->ZW_FirmwareUpdateMdReport1byteV3Frame.properties1 & 0x80)
  {
    DBG_PRINTF("Last byte received. \n");
    dat_len = bDatalen - (sizeof(ZW_FIRMWARE_UPDATE_MD_REPORT_1BYTE_V3_FRAME) - 1);
    if (dat_len > fwmss)
    {
      dat_len = fwmss;
    }
  }
  else
  {
    dat_len = fwmss;
  }

  if (blockno == 1)
  {
    uint8_t block_ok = 0;
    BYTE* p = &(pCmd->ZW_FirmwareUpdateMdReport1byteV3Frame.data1);
    DBG_PRINTF("First byte received.\n");

    if (ZW_GECKO_CHIP_TYPE(chip_desc.my_chip_type)) {
       if (((strncmp((char*) p, "ZWFW", 4) == 0) ||
            (strncmp((char*) p, "CERT", 4) == 0))) {
          block_ok = FALSE;
          DBG_PRINTF("Expected 700-series file.\n");
       } else {
          block_ok = TRUE;
       }
    } else if (fw_desc.firmware_id == ZW_FW_ID)
    {
      block_ok = (strncmp((char*) p, "ZWFW", 4) == 0);
      DBG_PRINTF("Checking for ZWFW\n");
    }
    if (!block_ok)
    {
      DBG_PRINTF("Wrong FW file received..\r\n");
      FwUpdate_StatusReport_Send(ERROR_INVALID_CHECKSUM_FATAL, WAIT_TIME_ZERO);
      return;
    }
  }

  //Verify the received block
  //includes the command header data and 2 byte crc
  if (zgw_crc16(calcrc, (BYTE *) pData, dat_len + sizeof(ZW_FIRMWARE_UPDATE_MD_REPORT_1BYTE_V3_FRAME) - 1))
  {
    DBG_PRINTF("Checksum verify failed for the blockno = %u\r\n", blockno);
    if (retrycnt >= MAX_RETRY)
    {
      FwUpdate_StatusReport_Send(ERROR_INVALID_CHECKSUM_FATAL, WAIT_TIME_ZERO);
    }
    else
    {
      retrycnt++;
      DBG_PRINTF("Retrying block no = %u\r\n", blockno);
      FwUpdateDataGet_Send();
    }
    return;
  }

  index = (uint32_t) fwmss * (blockno - 1);

  uint32_t max_len;

  /* Check the max allowed fw size */
  if (chip_desc.my_chip_type == ZW_CHIP_TYPE) {
     max_len = MAX_ZW_WITH_MD5_FWLEN;
  } else if (ZW_GECKO_CHIP_TYPE(chip_desc.my_chip_type)) {
     max_len = MAX_ZW_GECKO_FWLEN;
  }

  if ((index + dat_len) > max_len)
  {
    ERR_PRINTF("Firmware image too large, received len = 0x%lx, maxlen = 0x%x\n",
               (unsigned long )(index + dat_len),
               max_len);
    FwUpdate_StatusReport_Send(ERROR_INVALID_CHECKSUM_FATAL,
                               WAIT_TIME_ZERO);
    return;
  }

  bool res = false;

  if (index == 0) {
     res = fw_tmp_file_create();
     if (res == false) {
       ERR_PRINTF("Firmware update temporary creation failed.\n");
     }
  }
  /* Write the payload to a temporary file */
  res = fw_tmp_file_append(&pCmd->ZW_FirmwareUpdateMdReport1byteV3Frame.data1, dat_len);
  if (!res) {
     FwUpdate_StatusReport_Send(FIRMWARE_UPDATE_MD_STATUS_REPORT_INSUFFICIENT_MEMORY_V4,
                                WAIT_TIME_ZERO);
     return;
  }

  build_crc = zgw_crc16(build_crc, &pCmd->ZW_FirmwareUpdateMdReport1byteV3Frame.data1, dat_len);

  blockno++;
  retrycnt = 1;

  /* Last frame */
  if (pCmd->ZW_FirmwareUpdateMdReport1byteV3Frame.properties1 & 0x80)
  {
    if (build_crc != fw_desc.crc)
    {
      DBG_PRINTF("Invalid checksum: Download failed.\r\n");
      FwUpdate_StatusReport_Send(ERROR_INVALID_CHECKSUM_FATAL, WAIT_TIME_ZERO);
      return;
    }

    //Append image description to the end of temp file
    downloadlen = (index + dat_len);
    fw_desc.firmware_len = downloadlen;
    if (chip_desc.my_chip_type == ZW_CHIP_TYPE) {
      fw_tmp_file_append((u8_t*) &fw_desc, sizeof(struct image_descriptor));
    }

    if (bActivationRequest)
    {
      FwUpdate_StatusReport_Send(
      FIRMWARE_UPDATE_MD_STATUS_REPORT_SUCCESSFULLY_WAITING_FOR_ACTIVATION_V4, WAIT_TIME_ZERO);
    }
    else
    {
      activate_image(fw_desc.firmware_id, fw_desc.target, fw_desc.crc, fw_desc.firmware_len, 0);
    }
  }
  else
  {
    /* Expecting more frames */
    if (updatemode == BLOCK_MODE)
    {
      if (((reqno + blockno - 1) % BLOCK_REQ_NO) == 1)
      {
        //DBG_PRINTF("FwUpdateDataGet_Send: called  blockno = %u\r\n", blockno);
        FwUpdateDataGet_Send();
      }
      else if (upgradeTimer == 0xFF)
      {
        upgradeTimer = ZW_LTimerStart(FwUpdate_Timeout, upgradeTimeout,
        UPGRADE_TIMER_REPEATS);
      }
    }
    else
    {
      FwUpdateDataGet_Send();
    }
  }

  return;
error:
  FwUpdate_StatusReport_Send(ERROR_INVALID_CHECKSUM_FATAL, WAIT_TIME_ZERO);
  return;
}

static void
activate_image(uint16_t firmware_id, uint8_t firmware_target, uint16_t crc, uint32_t firmware_len, uint8_t activation)
{
  uint8_t status;

  status = FALSE;
  if (activation && (!ZW_GECKO_CHIP_TYPE(chip_desc.my_chip_type) && (firmware_id != ZW_FW_ID))) {
    struct image_descriptor desc;
    /* Make sure activation is requested for the same image that was previously uploaded. */
    FILE *fd = fopen(fw_filename, "r");
    fseek(fd, -sizeof(struct image_descriptor), SEEK_END);
    fread(&desc, sizeof(struct image_descriptor), 1, fd);
    fclose(fd);

    if (desc.firmware_id != firmware_id || desc.target != firmware_target || desc.crc != crc) { // We dont need this check if its ZW_FW_ID for ZW_GECKO_CHIP_TYPE (700/800 series)
      FwUpdateActivationReport_SendTo(&ups, INVALID_COMBINATION);
      return;
    } else {
      firmware_len = desc.firmware_len;
    }
  }

  if (ZW_GECKO_CHIP_TYPE(chip_desc.my_chip_type)) {
          status = ZWGeckoFirmwareUpdate(&fw_desc, &chip_desc,
                                         fw_curr_filename, strlen(fw_filename));
  } else {
    /* Validate the received 500 image */
    if (gconfig_ValidatZwFwImg(firmware_len, MAX_ZW_FWLEN))
    {
      status = ZWFirmwareUpdate(TRUE, fw_curr_filename, firmware_len);
    }
    else
    {
      status = FALSE;
    }
  }

  if (activation)
  {
    FwUpdateActivationReport_SendTo(&ups, status ? UPGRADE_SUCCESSFUL_POWERCYCLE : 0x1);
  }
  else
  {
    if (status)
    {
      FwUpdate_StatusReport_Send(UPGRADE_SUCCESSFUL_POWERCYCLE,
          gisZIPRReady == ZIPR_READY ? WAIT_TIME_FW_SSL_UPDATE : WAIT_TIME_ZW_FW_UPDATE);
    }
    else
    {
      FwUpdate_StatusReport_Send((status + ERROR_LASTUSED), WAIT_TIME_ZERO);
    }
  }

}

void
Fwupdate_MD_init()
{
  FwUpdate_Reset_Var();
}

REGISTER_HANDLER(
    Fwupdate_Md_CommandHandler,
    Fwupdate_MD_init,
    COMMAND_CLASS_FIRMWARE_UPDATE_MD, FIRMWARE_UPDATE_MD_VERSION_V5, SECURITY_SCHEME_0);
