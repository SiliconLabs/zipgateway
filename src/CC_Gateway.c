/* © 2014 Silicon Laboratories Inc.
 */
#include "CC_Gateway.h"
#include "DataStore.h"
#include "ZW_classcmd.h"
#include "ZW_classcmd_ex.h"
#include "zip_router_config.h" /* cfg */
#include "uip-debug.h"
#include "parse_config.h"
#include "ZW_tcp_client.h"
#include "ZW_udp_server.h"
#include "Serialapi.h"
#include "ZW_ZIPApplication.h"
#include "command_handler.h"
#include <stdlib.h>

static u8_t tunnelTimer = 0xFF;

#define UPGRADE_START_TIMEOUT 300
static int cmd_cls_parse(uint8_t *cmd_buf, uint8_t cmd_buf_len, uint8_t *cmd_cls_buf, uint8_t *cmd_cnt,
                         uint8_t *cmd_cls_sec_buf, uint8_t *cmd_cnt_sec);

extern void reset_the_nif();

static void parse_gw_profile(Gw_PeerProfile_St_t *pGwPeerProfile)
{

  printf("parse_gw_profile..\r\n");

  memset(cfg.portal_url, 0, sizeof(cfg.portal_url));
  cfg.portal_portno = 0;

  if (!pGwPeerProfile)
  {
    printf("parse_gw_profile NULL Pointer.\r\n");
    return;
  }

  if ((!uip_is_addr_unspecified(&pGwPeerProfile->peer_ipv6_addr)) &&
      (!uip_is_addr_linklocal(&pGwPeerProfile->peer_ipv6_addr)) &&
      (!uip_is_addr_mcast(&pGwPeerProfile->peer_ipv6_addr)) &&
      (!uip_is_addr_loopback(&pGwPeerProfile->peer_ipv6_addr)))
  {
    if (uip_is_4to6_addr(&pGwPeerProfile->peer_ipv6_addr))
    {
      sprintf(cfg.portal_url, "%d.%d.%d.%d", pGwPeerProfile->peer_ipv6_addr.u8[12], pGwPeerProfile->peer_ipv6_addr.u8[13], pGwPeerProfile->peer_ipv6_addr.u8[14],
              pGwPeerProfile->peer_ipv6_addr.u8[15]);
      cfg.portal_portno = (pGwPeerProfile->port1 << 8) | pGwPeerProfile->port2;
    }

    else
    {
      printf("Not a valid 4 to 6 Portal address.\r\n");
    }
  }
  else
  {
    if (pGwPeerProfile->peerNameLength < sizeof(cfg.portal_url))
    {
      memcpy(cfg.portal_url, &pGwPeerProfile->peerName, pGwPeerProfile->peerNameLength);
      cfg.portal_url[pGwPeerProfile->peerNameLength] = 0;
      cfg.portal_portno = (pGwPeerProfile->port1 << 8) | pGwPeerProfile->port2;
    }
  }

  printf("Portal URL: %s Port no = %u \r\n", cfg.portal_url, cfg.portal_portno);
}

static command_handler_codes_t Gateway_CommandHandler(zwave_connection_t *c, uint8_t *pData, uint16_t bDatalen)
{
  ZW_APPLICATION_TX_BUFFER_EX *pCmd = (ZW_APPLICATION_TX_BUFFER_EX *)pData;

  //printf("Gateway_CommandHandler: Received command = 0x%bx\r\n", pCmd->ZW_Common.cmd);

  switch (pCmd->ZW_Common.cmd)
  {
  case GATEWAY_MODE_SET:
  {
    Gw_Config_St_t GwConfig;

    //Read the GW configuration;
    rd_datastore_unpersist_gw_config(&GwConfig);

    if ((GwConfig.showlock & ZIP_GW_LOCK_ENABLE_BIT) != ZIP_GW_LOCK_ENABLE_BIT) //check whether lock is enabled or disabled
    {
      if (pCmd->ZW_GatewayModeSet.mode == ZIP_GW_MODE_STDALONE)
      {
        GwConfig.mode = ZIP_GW_MODE_STDALONE;
        GwConfig.peerProfile = 0; //Make peer profile invalid
        memset(cfg.portal_url, 0, sizeof(cfg.portal_url));
        TcpTunnel_ReStart();
      }
      else if (pCmd->ZW_GatewayModeSet.mode == ZIP_GW_MODE_PORTAL)
      {
        //if((GwConfig.peerProfile > 0) & (GwConfig.peerProfile <= MAX_PEER_PROFILES) )
        {
          GwConfig.mode = ZIP_GW_MODE_PORTAL;
        }
      }
    }
    //Write back GW configuration;
    rd_datastore_persist_gw_config(&GwConfig);
  }
  break;
  case GATEWAY_MODE_GET:
  {
    ZW_GATEWAY_MODE_REPORT_FRAME GatewayModeReport;
    Gw_Config_St_t GwConfig;

    //Read the GW configuration;
    rd_datastore_unpersist_gw_config(&GwConfig);

    if ((GwConfig.showlock & ZIP_GW_LOCK_SHOW_BIT_MASK) != ZIP_GW_LOCK_ENABLE_SHOW_DISABLE)
    {
      GatewayModeReport.cmdClass = COMMAND_CLASS_ZIP_GATEWAY;
      GatewayModeReport.cmd = GATEWAY_MODE_REPORT;
      GatewayModeReport.mode = GwConfig.mode;
      ZW_SendDataZIP(c, (BYTE *)&GatewayModeReport, sizeof(ZW_GATEWAY_MODE_REPORT_FRAME), NULL);
    }
  }
  break;

  case GATEWAY_PEER_SET:
  {
    Gw_Config_St_t GwConfig;
    u16_t len = 0;
    rd_datastore_unpersist_gw_config(&GwConfig);

    if ((GwConfig.showlock & ZIP_GW_LOCK_ENABLE_BIT) != ZIP_GW_LOCK_ENABLE_BIT) //check whether lock is enabled or disabled
    {
      if (GwConfig.mode == ZIP_GW_MODE_PORTAL)
      {
        if (pCmd->ZW_GatewayPeerSet.peerProfile == 1)
        {
          //check for the valid IPv6 address
          if ((!uip_is_addr_linklocal((uip_ip6addr_t *)&pCmd->ZW_GatewayPeerSet.ipv6Address1)) &&
              (!uip_is_addr_mcast((uip_ip6addr_t *)&pCmd->ZW_GatewayPeerSet.ipv6Address1)) &&
              (!uip_is_addr_loopback((uip_ip6addr_t *)&pCmd->ZW_GatewayPeerSet.ipv6Address1)))
          {
            if (uip_is_addr_unspecified((uip_ip6addr_t *)&pCmd->ZW_GatewayPeerSet.ipv6Address1) && (pCmd->ZW_GatewayPeerSet.peerNameLength == 0))
            {
              printf("Invalid profile settings.\r\n");
              break;
            }
            if (!uip_is_addr_unspecified(
                    (uip_ip6addr_t *)&pCmd->ZW_GatewayPeerSet.ipv6Address1) &&
                !uip_is_4to6_addr(
                    (uip_ip6addr_t *)&pCmd->ZW_GatewayPeerSet.ipv6Address1))
            {
              // Allow only 4 to 6 addresses for now.
              printf("Profile IPv6 addr is not 4 to 6 Ipv6 address.\r\n");
              break;
            }
            GwConfig.peerProfile = pCmd->ZW_GatewayPeerSet.peerProfile;
            GwConfig.actualPeers = 1;

            // write peer profile setting info
            rd_datastore_persist_gw_config(&GwConfig);

            // write the profile to database
            rd_datastore_persist_peer_profile(1, (Gw_PeerProfile_St_t *)&pCmd->ZW_GatewayPeerSet.ipv6Address1);

            parse_gw_profile(
                (Gw_PeerProfile_St_t *)&pCmd->ZW_GatewayPeerSet.ipv6Address1);

#ifndef ANDROID
            // start the gateway to connect to new portal server.
            tunnelTimer = ZW_LTimerStart(TcpTunnel_ReStart, UPGRADE_START_TIMEOUT,
                                         TIMER_ONE_TIME);
#endif
          }
        }
      }
    }
  }
  break;
  case GATEWAY_PEER_GET:
  {
    Gw_PeerProfile_Report_St_t GwPeerProfileReport;
    Gw_Config_St_t GwConfig;
    u16_t len = 0;

    rd_datastore_unpersist_gw_config(&GwConfig);

    if ((GwConfig.showlock & ZIP_GW_LOCK_SHOW_BIT_MASK) !=
        ZIP_GW_LOCK_ENABLE_SHOW_DISABLE)
    {
      memset(&GwPeerProfileReport, 0, sizeof(Gw_PeerProfile_Report_St_t));
      GwPeerProfileReport.cmdClass = COMMAND_CLASS_ZIP_GATEWAY;
      GwPeerProfileReport.cmd = GATEWAY_PEER_REPORT;

      if ((pCmd->ZW_GatewayPeerGet.peerProfile == 1) &&
          (GwConfig.actualPeers == 1))
      {
        GwPeerProfileReport.peerProfile = pCmd->ZW_GatewayPeerGet.peerProfile;
        GwPeerProfileReport.actualPeers = GwConfig.actualPeers;

        // write the profile to database
        rd_datastore_unpersist_peer_profile(1, ((Gw_PeerProfile_St_t *)&GwPeerProfileReport.peer_ipv6_addr));

        len = sizeof(ZW_GATEWAY_PEER_REPORT_FRAME) - 1 +
              GwPeerProfileReport.peerNameLength;
      }
      else
      {
        GwPeerProfileReport.peerProfile = 0;
        GwPeerProfileReport.actualPeers = GwConfig.actualPeers;
        GwPeerProfileReport.peerNameLength = 0;
        len = sizeof(ZW_GATEWAY_PEER_REPORT_FRAME) - 1;
        printf("Invalid Peer Get received.\r\n");
      }

      ZW_SendDataZIP(c, (u8_t *)&GwPeerProfileReport, len, NULL);
    }
  }

  break;

  case GATEWAY_LOCK_SET:
  {
    Gw_Config_St_t GwConfig;

    rd_datastore_unpersist_gw_config(&GwConfig);

    if (!(pCmd->ZW_GatewayLockSet.showEnable & ZIP_GW_LOCK_ENABLE_BIT))
    {
      // Accept unlocking only from the secured source ....
      if (gisTnlPacket == TRUE)
      {
        GwConfig.showlock =
            pCmd->ZW_GatewayLockSet.showEnable & ZIP_GW_LOCK_SHOW_BIT;
        gGwLockEnable = FALSE;
        printf("Gateway Unlocked...\r\n");
      }
      else if ((GwConfig.showlock & ZIP_GW_LOCK_ENABLE_BIT) !=
               ZIP_GW_LOCK_ENABLE_BIT)
      {
        printf("Enable/disable show enable\r\n");
        GwConfig.showlock =
            (pCmd->ZW_GatewayLockSet.showEnable & ZIP_GW_LOCK_SHOW_BIT);
      }
    }
    else
    {
      if (((GwConfig.showlock & ZIP_GW_LOCK_ENABLE_BIT) !=
           ZIP_GW_LOCK_ENABLE_BIT))
      {
        printf("Locking the Gateway...\r\n");
        // Read the GW configuration;
        GwConfig.showlock = pCmd->ZW_GatewayLockSet.showEnable & 0x3;
        gGwLockEnable = TRUE;
      }
    }
    // write the GW configuration back
    rd_datastore_persist_gw_config(&GwConfig);
  }

  break;

  case UNSOLICITED_DESTINATION_SET:
  {
    if ((!uip_is_addr_linklocal(
            (uip_ip6addr_t *)&pCmd->ZW_GatewayUnsolicitDstSet
                .unsolicit_ipv6_addr[0])) &&
        (!uip_is_addr_mcast((uip_ip6addr_t *)&pCmd->ZW_GatewayUnsolicitDstSet
                                .unsolicit_ipv6_addr[0])) &&
        (!uip_is_addr_loopback((uip_ip6addr_t *)&pCmd->ZW_GatewayUnsolicitDstSet
                                   .unsolicit_ipv6_addr[0])))
    {
      char str[128];
      memcpy(&cfg.unsolicited_dest,
             &pCmd->ZW_GatewayUnsolicitDstSet.unsolicit_ipv6_addr[0], 16);
      cfg.unsolicited_port =
          ((unsigned int)pCmd->ZW_GatewayUnsolicitDstSet.unsolicitPort1 << 8) |
          pCmd->ZW_GatewayUnsolicitDstSet.unsolicitPort2;

      uip_ipaddr_sprint(str, &cfg.unsolicited_dest);
      config_update("ZipUnsolicitedDestinationIp6", str);
      sprintf(str, "%i", cfg.unsolicited_port);
      config_update("ZipUnsolicitedDestinationPort", str);
    }
  }
  break;

  case UNSOLICITED_DESTINATION_GET:
  {
    ZW_GATEWAY_UNSOLICITED_DESTINATION_REPORT_FRAME UnsolicitDstReport_st;

    UnsolicitDstReport_st.cmdClass = COMMAND_CLASS_ZIP_GATEWAY;
    UnsolicitDstReport_st.cmd = UNSOLICITED_DESTINATION_REPORT;

    memcpy(&UnsolicitDstReport_st.unsolicit_ipv6_addr[0], &cfg.unsolicited_dest,
           16);
    UnsolicitDstReport_st.unsolicitPort1 = (cfg.unsolicited_port >> 8) & 0xFF;
    UnsolicitDstReport_st.unsolicitPort2 = cfg.unsolicited_port & 0xFF;
    ZW_SendDataZIP(c, (u8_t *)&UnsolicitDstReport_st,
                   sizeof(ZW_GATEWAY_UNSOLICITED_DESTINATION_REPORT_FRAME),
                   NULL);
  }

  break;

  case COMMAND_APPLICATION_NODE_INFO_SET:
  {
    application_nodeinfo_st_t appNodeInfo_st;
    ZW_GATEWAY_APP_NODE_INFO_SET_FRAME *f =
        (ZW_GATEWAY_APP_NODE_INFO_SET_FRAME *)pData;
    memset(&appNodeInfo_st, 0, sizeof(application_nodeinfo_st_t));

    if (cmd_cls_parse(
            &f->payload[0], bDatalen - 2, &appNodeInfo_st.nonSecureCmdCls[0],
            &appNodeInfo_st.nonSecureLen, &appNodeInfo_st.secureCmdCls[0],
            &appNodeInfo_st.secureLen) != 0)
    {
      char output[64 * 5];
      char *t = output;
      char *end = output + sizeof(output);
      cfg.extra_classes_len = appNodeInfo_st.nonSecureLen;
      int i;
      for (i = 0; i < appNodeInfo_st.nonSecureLen; i++)
      {
        t += snprintf(t, end - t, "0x%02X ", appNodeInfo_st.nonSecureCmdCls[i]);
        cfg.extra_classes[i] = appNodeInfo_st.nonSecureCmdCls[i];
      }

      t += snprintf(t, end - t, "0xF100 ");

      cfg.sec_extra_classes_len = appNodeInfo_st.secureLen;
      for (i = 0; i < appNodeInfo_st.secureLen; i++)
      {
        t += snprintf(t, end - t, "0x%02X ", appNodeInfo_st.secureCmdCls[i]);
        cfg.sec_extra_classes[i] = appNodeInfo_st.secureCmdCls[i];
      }
      *t = 0;
      printf("output: %s\n", output);
      config_update("ExtraClasses", output);

      appNodeInfo_CC_Add();
    }
  }
  break;
  case COMMAND_APPLICATION_NODE_INFO_GET:
  {
    uint16_t len = 0;
    uint8_t tmplen = 0;
    uint8_t buf[128 + 2 + 2] = {
        0}; // cmd class + cmd + commands and Security marker

    ZW_GATEWAY_APP_NODE_INFO_REPORT_FRAME *pNodeAppInfo_Report =
        (ZW_GATEWAY_APP_NODE_INFO_REPORT_FRAME *)buf;

    pNodeAppInfo_Report->cmdClass = COMMAND_CLASS_ZIP_GATEWAY;
    pNodeAppInfo_Report->cmd = COMMAND_APPLICATION_NODE_INFO_REPORT;

    len += 2; // cmd class and cmd

    if (cfg.extra_classes_len != 0)
    {
      memcpy(&pNodeAppInfo_Report->payload[0], cfg.extra_classes,
             cfg.extra_classes_len);
      len += cfg.extra_classes_len;
      tmplen = cfg.extra_classes_len;
    }

    if (cfg.sec_extra_classes_len != 0)
    {
      // Fill Up the security Mark
      pNodeAppInfo_Report->payload[tmplen] = 0xF1;
      pNodeAppInfo_Report->payload[tmplen + 1] = 0x00;
      len += 2;

      memcpy(&pNodeAppInfo_Report->payload[tmplen + 2], cfg.sec_extra_classes,
             cfg.sec_extra_classes_len);
      len += cfg.sec_extra_classes_len;
    }

    ZW_SendDataZIP(c, (u8_t *)pNodeAppInfo_Report, len, NULL);
  }

  break;

  default:
    printf("Gateway_CommandHandler: Unsupported command received.\r\n");
    return COMMAND_NOT_SUPPORTED;
  }

  return COMMAND_HANDLED;
}

#define CMD_CLS_PARSE_MODE_INSECURE 0
#define CMD_CLS_PARSE_MODE_CONTROL 1
#define CMD_CLS_PARSE_MODE_SECURE 2

//Re-Used Logic from HC API code.
static int cmd_cls_parse(uint8_t *cmd_buf, uint8_t cmd_buf_len, uint8_t *cmd_cls_buf, uint8_t *cmd_cnt,
                         uint8_t *cmd_cls_sec_buf, uint8_t *cmd_cnt_sec)
{
  unsigned i;
  int mode; // 0=insecure supported command classes; 1=insecure controlled command classes;
            // 2=secure supported command classes
  uint8_t cnt;
  uint8_t cnt_sec;

  if ((cmd_buf_len == 0) || !cmd_buf || !cmd_cls_buf || !cmd_cls_sec_buf)
  {
    return 0;
  }

  //Initialize the parser
  cnt_sec = cnt = i = 0;
  mode = CMD_CLS_PARSE_MODE_INSECURE;

  while (i < cmd_buf_len)
  {
    if (*cmd_buf >= 0xF1)
    { //Check whether this is the security scheme 0 marker
      if (*(cmd_buf + 1) == 0)
      { //Change to secure mode
        mode = CMD_CLS_PARSE_MODE_SECURE;

        cmd_buf += 2;
        i += 2;
        continue;
      }

      //Extended command class
      if (mode == CMD_CLS_PARSE_MODE_INSECURE)
      {
        cmd_cls_buf[cnt] = *cmd_buf++;
        cmd_cls_buf[cnt + 1] = (cmd_cls_buf[cnt] << 8) | (*cmd_buf++);
        cnt++;
      }
      else if (mode == CMD_CLS_PARSE_MODE_SECURE)
      {
        cmd_cls_sec_buf[cnt_sec] = *cmd_buf++;
        cmd_cls_sec_buf[cnt_sec + 1] = (cmd_cls_sec_buf[cnt_sec] << 8) | (*cmd_buf++);
        cnt_sec++;
      }
      else
      {
        cmd_buf += 2;
      }

      i += 2;
    }
    else
    {
      //Check whether this is the controlled command class marker
      if (*cmd_buf == 0xEF)
      { //Change mode
        mode = CMD_CLS_PARSE_MODE_CONTROL;

        cmd_buf++;
        i++;
        continue;
      }

      //Normal command class
      if (mode == CMD_CLS_PARSE_MODE_INSECURE)
      {
        cmd_cls_buf[cnt++] = *cmd_buf++;
      }
      else if (mode == CMD_CLS_PARSE_MODE_SECURE)
      {
        cmd_cls_sec_buf[cnt_sec++] = *cmd_buf++;
      }
      else
      {
        cmd_buf++;
      }
      i++;
    }
  }
  //Parsing done
  *cmd_cnt = cnt;
  *cmd_cnt_sec = cnt_sec;
  return 1;
}

void appNodeInfo_CC_Add(void)
{
  BYTE class_buf[64] = {0};
  BYTE class_len = 0;

  reset_the_nif();

  if (cfg.extra_classes_len != 0)
  {
    AddUnsocDestCCsToGW(cfg.extra_classes, cfg.extra_classes_len);
  }

  if (cfg.sec_extra_classes_len != 0)
  {
    AddSecureUnsocDestCCsToGW(cfg.sec_extra_classes, cfg.sec_extra_classes_len);
  }

  CommandClassesUpdated();
}

/*
  Check whether input command class exist in the support node info set list
*/
BYTE IsCCInNodeInfoSetList(BYTE cmdclass, BOOL secure)
{
  BYTE class_buf[64] = {0};
  BYTE class_len = 0;
  BYTE i = 0;

  if (cfg.extra_classes_len != 0)
  {
    for (i = 0; i < cfg.extra_classes_len; i++)
    {
      if (cfg.extra_classes[i] == cmdclass)
      {
        return TRUE;
      }
    }
  }

  if (secure)
  {
    if (cfg.sec_extra_classes_len != 0)
    {
      for (i = 0; i < cfg.sec_extra_classes_len; i++)
      {
        if (cfg.sec_extra_classes[i] == cmdclass)
        {
          return TRUE;
        }
      }
    }
  }
  return FALSE;
}

REGISTER_HANDLER(
    Gateway_CommandHandler,
    0,
    COMMAND_CLASS_ZIP_GATEWAY, ZIP_GATEWAY_VERSION, SECURITY_SCHEME_UDP);
