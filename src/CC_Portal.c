/* © 2014 Silicon Laboratories Inc.
 */
#include "CC_Portal.h"
#include "TYPES.H"
#include "ZW_classcmd.h"
#include "ZW_classcmd_ex.h"
#include "router_events.h" /* zip_process */
#include "zip_router_config.h"
#include "ZW_tcp_client.h"
#include "ZW_udp_server.h"
#include "Serialapi.h"
#include "ZW_ZIPApplication.h"
#include "command_handler.h"

uint8_t gGwLockEnable = TRUE;

command_handler_codes_t ZIP_Portal_CommandHandler(zwave_connection_t *c, uint8_t *pData, uint16_t bDatalen)
{
  ZW_APPLICATION_TX_BUFFER *pCmd = (ZW_APPLICATION_TX_BUFFER *)pData;

  switch (pCmd->ZW_Common.cmd)
  {
  case GATEWAY_CONFIGURATION_SET:
  {
    ZW_GATEWAY_CONFIGURATION_STATUS GwConfigStatus;
    ZW_GATEWAY_CONFIGURATION_SET_FRAME *f = (ZW_GATEWAY_CONFIGURATION_SET_FRAME *)pData;

    GwConfigStatus.cmdClass = COMMAND_CLASS_ZIP_PORTAL;
    GwConfigStatus.cmd = GATEWAY_CONFIGURATION_STATUS;

    if (parse_portal_config(&f->lanIpv6Address1, (bDatalen - sizeof(ZW_GATEWAY_CONFIGURATION_SET) + 1)))
    {
      GwConfigStatus.status = ZIPR_READY_OK;
      process_post(&zip_process, ZIP_EVENT_TUNNEL_READY, 0);
    }
    else
    {
      GwConfigStatus.status = INVALID_CONFIG;
    }

    ZW_SendDataZIP(c, (BYTE *)&GwConfigStatus, sizeof(ZW_GATEWAY_CONFIGURATION_STATUS), NULL);
  }
  break;

  case GATEWAY_CONFIGURATION_GET:
  {
    uint8_t buf[128] =
        {0}; //buffer should be more than size of portal config + 2
    ZW_GATEWAY_CONFIGURATION_REPORT *pGwConfigReport = (ZW_GATEWAY_CONFIGURATION_REPORT *)buf;
    portal_ip_configuration_st_t *portal_config = (portal_ip_configuration_st_t *)(&pGwConfigReport->payload[0]);

    pGwConfigReport->cmdClass = COMMAND_CLASS_ZIP_PORTAL;
    pGwConfigReport->cmd = GATEWAY_CONFIGURATION_REPORT;

    memcpy(&portal_config->lan_address, &cfg.lan_addr, IPV6_ADDR_LEN);
    portal_config->lan_prefix_length = cfg.lan_prefix_length;

    memcpy(&portal_config->tun_prefix, &cfg.tun_prefix, IPV6_ADDR_LEN);
    portal_config->tun_prefix_length = cfg.tun_prefix_length;

    memcpy(&portal_config->gw_address, &cfg.gw_addr, IPV6_ADDR_LEN);

    memcpy(&portal_config->pan_prefix, &cfg.pan_prefix, IPV6_ADDR_LEN);

    ZW_SendDataZIP(c, buf,
                   (sizeof(ZW_GATEWAY_CONFIGURATION_REPORT) - 1 + sizeof(portal_ip_configuration_st_t)), NULL);
  }
  break;

  default:
    return COMMAND_NOT_SUPPORTED;
    break;
  }
  return COMMAND_HANDLED;
}

REGISTER_HANDLER(
    ZIP_Portal_CommandHandler,
    0,
    COMMAND_CLASS_ZIP_PORTAL, ZIP_PORTAL_VERSION, SECURITY_SCHEME_UDP);
