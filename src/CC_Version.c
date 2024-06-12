/* © 2017 Silicon Laboratories Inc.
 */
/*
 * CC_Version.c
 *
 *  Created on: Nov 9, 2016
 *      Author: aes
 */

#include "CC_FirmwareUpdate.h" /* for the firmware targets */
#include "CC_Gateway.h"        /* IsCCInNodeInfoSetList */
#include "RD_DataStore.h"      /* for the eeprom version */
#include "Serialapi.h"
#include "ZIP_Router_logging.h"
#include "ZW_ZIPApplication.h" /* net_scheme */
#include "ZW_classcmd_ex.h"
#include "command_handler.h"
#include "pkgconfig.h"
#include "security_layer.h"
#include "zip_router_config.h"
#include <stdlib.h>

extern ZW_APPLICATION_TX_BUFFER txBuf;
/*Macro to compress this case statement*/
#define VERSION_CASE(a)                                                        \
  case COMMAND_CLASS_##a:                                                      \
    txBuf.ZW_VersionCommandClassReportFrame.commandClassVersion = a##_VERSION; \
    break;

#define VERSION_CASE2(a, b)                                                    \
  case COMMAND_CLASS_##a:                                                      \
    txBuf.ZW_VersionCommandClassReportFrame.commandClassVersion =              \
        a##_VERSION_##b;                                                       \
    break;

/* If the request is from PAN side return the version as 0 in case of LAN only
 * CCs for e.g. (ZIP and ZIP_ND) */
#define VERSION_CASE_LAN_ONLY(a)                                               \
  case COMMAND_CLASS_##a:                                                      \
    if (c->scheme == SECURITY_SCHEME_UDP) {                                    \
      txBuf.ZW_VersionCommandClassReportFrame.commandClassVersion =            \
          a##_VERSION;                                                         \
    } else {                                                                   \
      txBuf.ZW_VersionCommandClassReportFrame.commandClassVersion = 0;         \
    }                                                                          \
    break;

#define VERSION_CASE2_LAN_ONLY(a, b)                                           \
  case COMMAND_CLASS_##a:                                                      \
    if (c->scheme == SECURITY_SCHEME_UDP) {                                    \
      txBuf.ZW_VersionCommandClassReportFrame.commandClassVersion =            \
          a##_VERSION_##b;                                                     \
    } else {                                                                   \
      txBuf.ZW_VersionCommandClassReportFrame.commandClassVersion = 0;         \
    }                                                                          \
    break;

#define NUM_FW_TARGETS                                                         \
  1 /* Number of additional FW targets reported in Version Report */

static command_handler_codes_t VersionHandler(zwave_connection_t *c,
                                              uint8_t *frame, uint16_t length) {
  char buf[64];
  ZW_APPLICATION_TX_BUFFER *pCmd = (ZW_APPLICATION_TX_BUFFER *)frame;

  switch (pCmd->ZW_Common.cmd) {
  case VERSION_GET: {
    ZW_VERSION_REPORT_2BYTE_V2_FRAME *f =
        (ZW_VERSION_REPORT_2BYTE_V2_FRAME *)&txBuf;
    f->cmdClass = COMMAND_CLASS_VERSION;
    f->cmd = VERSION_REPORT;
    f->zWaveLibraryType = ZW_Version((BYTE *)buf);

    f->zWaveProtocolVersion = atoi(buf + 7);
    f->zWaveProtocolSubVersion = atoi(buf + 9);

    Get_SerialAPI_AppVersion(&f->firmware0Version, &f->firmware0SubVersion);

    f->numberOfFirmwareTargets = NUM_FW_TARGETS;
    f->hardwareVersion = cfg.hardware_version;
    /* Gateway version */
    f->variantgroup1.firmwareVersion = PACKAGE_VERSION_MAJOR;
    f->variantgroup1.firmwareSubVersion = PACKAGE_VERSION_MINOR;
    /* eeprom version */
    rd_data_store_version_get(&(f->variantgroup2.firmwareVersion),
                              &(f->variantgroup2.firmwareSubVersion));

    ZW_SendDataZIP(c, (BYTE *)&txBuf, sizeof(ZW_VERSION_REPORT_2BYTE_V2_FRAME),
                   NULL);
    break; // VERSION_GET
  }
  case VERSION_COMMAND_CLASS_GET:
    /*If asked non-secre only answer on what we support secure*/
    if (IsCCInNodeInfoSetList(
            pCmd->ZW_VersionCommandClassGetFrame.requestedCommandClass,
            scheme_compare(c->scheme, net_scheme))) {
      DBG_PRINTF("Version GET handled by backend 2\n");
      return CLASS_NOT_SUPPORTED;
    }

    txBuf.ZW_VersionCommandClassReportFrame.cmdClass = COMMAND_CLASS_VERSION;
    txBuf.ZW_VersionCommandClassReportFrame.cmd = VERSION_COMMAND_CLASS_REPORT;
    txBuf.ZW_VersionCommandClassReportFrame.requestedCommandClass =
        pCmd->ZW_VersionCommandClassGetFrame.requestedCommandClass;

    switch (pCmd->ZW_VersionCommandClassGetFrame.requestedCommandClass) {
    case COMMAND_CLASS_SECURITY:
      txBuf.ZW_VersionCommandClassReportFrame.commandClassVersion =
          is_sec0_key_granted() ? SECURITY_VERSION : 0x00;
      break;
      VERSION_CASE_LAN_ONLY(ZIP_ND)
      VERSION_CASE2_LAN_ONLY(ZIP, V5)
      VERSION_CASE2(TRANSPORT_SERVICE, V2)
      VERSION_CASE(SECURITY_2)
      VERSION_CASE(CRC_16_ENCAP)
      VERSION_CASE(APPLICATION_STATUS)
    default:
      txBuf.ZW_VersionCommandClassReportFrame.commandClassVersion =
          ZW_comamnd_handler_version_get(
              c->scheme,
              pCmd->ZW_VersionCommandClassGetFrame.requestedCommandClass);
      break;
    }
    ZW_SendDataZIP(c, (BYTE *)&txBuf,
                   sizeof(txBuf.ZW_VersionCommandClassReportFrame), NULL);
    break; // VERSION_COMMAND_CLASS_GET

  case VERSION_CAPABILITIES_GET:
    DBG_PRINTF("VERSION_CAPABILITIES_GET\n");

    ZW_VERSION_CAPABILITIES_REPORT_V3_FRAME *fc =
        (ZW_VERSION_CAPABILITIES_REPORT_V3_FRAME *)&txBuf;
    fc->cmdClass = COMMAND_CLASS_VERSION;
    fc->cmd = VERSION_CAPABILITIES_REPORT;
    fc->properties = 0x07;
    ZW_SendDataZIP(c, (BYTE *)&txBuf,
                   sizeof(ZW_VERSION_CAPABILITIES_REPORT_V3_FRAME), NULL);
    break;
  case VERSION_ZWAVE_SOFTWARE_GET:
    DBG_PRINTF("VERSION_ZWAVE_SOFTWARE_GET\n");

    PROTOCOL_VERSION zw_protocol_version = {0};
    ZW_GetProtocolVersion(&zw_protocol_version);

    ZW_Version((BYTE *)buf);        /* Protocol version */

    ZW_VERSION_ZWAVE_SOFTWARE_REPORT_V3_FRAME *fz =
        (ZW_VERSION_ZWAVE_SOFTWARE_REPORT_V3_FRAME *)&txBuf;
    fz->cmdClass = COMMAND_CLASS_VERSION;
    fz->cmd = VERSION_ZWAVE_SOFTWARE_REPORT;
    fz->sDKversion1 = PACKAGE_VERSION_MAJOR;
    fz->sDKversion2 = PACKAGE_VERSION_MINOR;
    fz->sDKversion3 = PACKAGE_PATCH_LEVEL;
    fz->applicationFrameworkAPIVersion1 = 0; // (MSB)
    fz->applicationFrameworkAPIVersion2 = 0;
    fz->applicationFrameworkAPIVersion3 = 0;  // (LSB)
    fz->applicationFrameworkBuildNumber1 = 0; // (MSB)
    fz->applicationFrameworkBuildNumber2 = 0; // (LSB)
    fz->hostInterfaceVersion1 = zw_protocol_version.protocolVersionMajor;
    fz->hostInterfaceVersion2 = zw_protocol_version.protocolVersionMinor;
    fz->hostInterfaceVersion3 = zw_protocol_version.protocolVersionRevision;
    fz->hostInterfaceBuildNumber1 = 0;         // (MSB)
    fz->hostInterfaceBuildNumber2 = 0;         // (LSB)
    fz->zWaveProtocolVersion1 = atoi(buf + 7); // (MSB)
    fz->zWaveProtocolVersion2 = atoi(buf + 9);
    fz->zWaveProtocolVersion3 = 0;     // (LSB)
    fz->zWaveProtocolBuildNumber1 = 0; // (MSB) /*TODO: How to get this?*/
    fz->zWaveProtocolBuildNumber2 = 0; // (LSB)
    fz->applicationVersion1 = 0;       //(MSB)
    fz->applicationVersion2 = 0;
    fz->applicationVersion = 0;      // (LSB)
    fz->applicationBuildNumber1 = 0; //(MSB)
    fz->applicationBuildNumber2 = 0; //(LSB)
    ZW_SendDataZIP(c, (BYTE *)&txBuf,
                   sizeof(ZW_VERSION_ZWAVE_SOFTWARE_REPORT_V3_FRAME), NULL);
    break;
  default:
    return COMMAND_NOT_SUPPORTED;
  }
  return COMMAND_HANDLED;
}

REGISTER_HANDLER(VersionHandler, 0, COMMAND_CLASS_VERSION, VERSION_VERSION_V3,
                 NET_SCHEME);
