/* © 2017 Silicon Laboratories Inc.
 */
/*
 * CC_PowerLevel.c
 *
 *  Created on: Nov 9, 2016
 *      Author: aes
 */

#include "Serialapi.h"
#include "command_handler.h"
#include "CC_PowerLevel.h"
#include "router_events.h" /* Notify when this component goes back to idle. */
#include "RD_types.h"

uint8_t ChipType;
uint8_t ChipVersion;

clock_time_t  powerLevelTimerEnd;
static struct ctimer powerLevelTimer;

/** mimick power level set value for 700 series */
BYTE RFPOWER = normalPower;

/** State of the powerlevel test*/
static struct
{
  nodeid_t node;
  BYTE powerLevel;
  WORD count;
  WORD ack;
  BYTE status;
} powerlevel_test;

/**
 * Power level SET wrapper for the CC to work on both 500 and 700 series
 */
static void SetPowerLevel(BYTE RF_Power)
{
  /* Fetch the cached chip type. */
  SerialAPI_GetChipTypeAndVersion(&ChipType, &ChipVersion);
  if (ChipType == ZW_CHIP_TYPE) {
     ZW_RFPowerLevelSet(RF_Power);
  } else {
     RFPOWER = RF_Power; // holds RF_Power level till it reset
  }
  if (RF_Power == normalPower) {
     process_post(&zip_process, ZIP_EVENT_COMPONENT_DONE, 0);
  }
}

/**
 * Power level GET wrapper for the CC to work on both 500 and 700 series
 */
static BYTE GetPowerLevel()
{
  SerialAPI_GetChipTypeAndVersion(&ChipType, &ChipVersion);
  if (ChipType == ZW_CHIP_TYPE) {
    RFPOWER = ZW_RFPowerLevelGet();
  }
  return RFPOWER;
}

static void
powerLevelTest(BYTE txStatus)
{
  powerlevel_test.count--;

  if (txStatus == TRANSMIT_COMPLETE_OK)
  {
    powerlevel_test.ack++;
  }

  if (powerlevel_test.count > 0
      && ZW_SendTestFrame(powerlevel_test.node, powerlevel_test.powerLevel, powerLevelTest))
  {
    return;
  }

  powerlevel_test.status =
      ((powerlevel_test.count) > 0 || (powerlevel_test.ack == 0)) ?
          POWERLEVEL_TEST_NODE_REPORT_ZW_TEST_FAILED : POWERLEVEL_TEST_NODE_REPORT_ZW_TEST_SUCCES;

  powerlevel_test.count = 0;
  SetPowerLevel(normalPower);
}

static void
resetPowerLevel(void* data)
{
  if(ChipType == 5){
    SetPowerLevel(normalPower);}
  else
    RFPOWER = normalPower;
   
  powerLevelTimerEnd = 0;
}

/* Use the buffer from ZW_ZIPApplication in this command handler. */
extern   ZW_APPLICATION_TX_BUFFER txBuf;

static command_handler_codes_t
PowerLevelHandler(zwave_connection_t *c, uint8_t* frame, uint16_t length)
{
  ZW_APPLICATION_TX_BUFFER* pCmd = (ZW_APPLICATION_TX_BUFFER*) frame;

  switch (pCmd->ZW_Common.cmd)
  {
  case POWERLEVEL_SET:
    if (pCmd->ZW_PowerlevelSetFrame.powerLevel > miniumPower)
    {
      return COMMAND_PARSE_ERROR;
    }

    if (pCmd->ZW_PowerlevelSetFrame.powerLevel == normalPower)
    {
      ctimer_stop(&powerLevelTimer);
      resetPowerLevel(0);
    }
    else
    {
      if (pCmd->ZW_PowerlevelSetFrame.timeout == 0)
      {
        return COMMAND_PARSE_ERROR;
      }
      SetPowerLevel(pCmd->ZW_PowerlevelSetFrame.powerLevel);
      ctimer_set(&powerLevelTimer, pCmd->ZW_PowerlevelSetFrame.timeout * 1000UL, resetPowerLevel, 0);

      powerLevelTimerEnd = clock_time() + pCmd->ZW_PowerlevelSetFrame.timeout * 1000UL;
    }
    break;
  case POWERLEVEL_GET:
    txBuf.ZW_PowerlevelReportFrame.cmdClass = COMMAND_CLASS_POWERLEVEL;
    txBuf.ZW_PowerlevelReportFrame.cmd = POWERLEVEL_REPORT;
    txBuf.ZW_PowerlevelReportFrame.powerLevel = GetPowerLevel();
    txBuf.ZW_PowerlevelReportFrame.timeout = powerLevelTimerEnd > 0 ? ((powerLevelTimerEnd - clock_time()) / 1000) : 0;

    ZW_SendDataZIP(c, (BYTE*) &txBuf, sizeof(txBuf.ZW_PowerlevelReportFrame), NULL);
    break;
  case POWERLEVEL_TEST_NODE_SET:

    if (powerlevel_test.count == 0)
    {
      if (pCmd->ZW_PowerlevelTestNodeSetFrame.powerLevel > miniumPower)
      {
        return COMMAND_PARSE_ERROR;
      }
      powerlevel_test.node = pCmd->ZW_PowerlevelTestNodeSetFrame.testNodeid;
      powerlevel_test.powerLevel = pCmd->ZW_PowerlevelTestNodeSetFrame.powerLevel;
      powerlevel_test.count = pCmd->ZW_PowerlevelTestNodeSetFrame.testFrameCount1 << 8
          | pCmd->ZW_PowerlevelTestNodeSetFrame.testFrameCount2;
      powerlevel_test.ack = 0;
      if (powerlevel_test.count == 0)
      {
        return COMMAND_PARSE_ERROR;
      }
      powerlevel_test.status = POWERLEVEL_TEST_NODE_REPORT_ZW_TEST_INPROGRESS;
      SetPowerLevel(powerlevel_test.powerLevel);
      if (ZW_SendTestFrame(powerlevel_test.node, powerlevel_test.powerLevel, powerLevelTest))
      {
        //
      }
      else
      {
        powerlevel_test.count = 0;
        powerlevel_test.status = POWERLEVEL_TEST_NODE_REPORT_ZW_TEST_FAILED;
        SetPowerLevel(normalPower);
      }
    } else {
       return COMMAND_BUSY;
    }
    break;
  case POWERLEVEL_TEST_NODE_GET:
    txBuf.ZW_PowerlevelTestNodeReportFrame.cmdClass = COMMAND_CLASS_POWERLEVEL;
    txBuf.ZW_PowerlevelTestNodeReportFrame.cmd = POWERLEVEL_TEST_NODE_REPORT;
    txBuf.ZW_PowerlevelTestNodeReportFrame.testNodeid = powerlevel_test.node;
    txBuf.ZW_PowerlevelTestNodeReportFrame.statusOfOperation = powerlevel_test.status;
    txBuf.ZW_PowerlevelTestNodeReportFrame.testFrameCount1 = (powerlevel_test.ack >> 8) & 0xFF;
    txBuf.ZW_PowerlevelTestNodeReportFrame.testFrameCount2 = (powerlevel_test.ack >> 0) & 0xFF;

    ZW_SendDataZIP(c, (BYTE*) &txBuf, sizeof(txBuf.ZW_PowerlevelTestNodeReportFrame), NULL);
    break;
  default:
    return COMMAND_NOT_SUPPORTED;;
  }
  return COMMAND_HANDLED;
}

static void PowerLevelInit()
{
  memset(&powerlevel_test,0,sizeof(powerlevel_test));
  powerLevelTimerEnd = 0;
  ctimer_stop(&powerLevelTimer);
}

bool PowerLevel_idle(void) {
   return ((powerlevel_test.count == 0)
           && ctimer_expired(&powerLevelTimer));
}

REGISTER_HANDLER(
    PowerLevelHandler,
    PowerLevelInit,
    COMMAND_CLASS_POWERLEVEL, POWERLEVEL_VERSION, NET_SCHEME);
