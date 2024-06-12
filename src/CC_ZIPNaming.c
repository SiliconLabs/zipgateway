/* © 2017 Silicon Laboratories Inc.
 */
/*
 * CC_ZIPNaming.c
 *
 *  Created on: Nov 9, 2016
 *      Author: aes
 */

#include "Serialapi.h"
#include "command_handler.h"
#include "ZIP_Router_logging.h"
#include "zip_router_ipv6_utils.h" /* nodeOfIP */
#include "ResourceDirectory.h"
extern ZW_APPLICATION_TX_BUFFER txBuf;

static zwave_connection_t naming_reply;

/** Callback for RD.
 *
 * Register with RD when a node needs to be re-probed.
 * Should be called by RD when the probe has completed.
 *
 * \param ep Pointer to the endpoint entry.
 * \param user id of the report frame.
 */
static void send_ep_name_or_location_reply(rd_ep_database_entry_t* ep, void* user)
{
  u8_t *f = (u8_t*) &txBuf;
  u8_t l;

  f[0] = COMMAND_CLASS_ZIP_NAMING;
  f[1] = (uintptr_t) user & 0xFF;

  if ((uintptr_t) user == ZIP_NAMING_NAME_REPORT)
  {
    l = rd_get_ep_name(ep, (char*) &f[2], 64);
  }
  else
  {
    l = rd_get_ep_location(ep, (char*) &f[2], 64);
  }
  ZW_SendDataZIP(&naming_reply, f, l + 2, 0);
  memset(&naming_reply, 0, sizeof(naming_reply));
}

command_handler_codes_t
ZIPNamingHandler(zwave_connection_t *c, uint8_t* pData, uint16_t bDatalen)
{
  ZW_APPLICATION_TX_BUFFER* pCmd = (ZW_APPLICATION_TX_BUFFER*) pData;
  nodeid_t nodeid = nodeOfIP(&c->lipaddr);
  rd_ep_database_entry_t * ep = rd_ep_first(nodeid);

  if (ep == NULL) {
     /* It is a syntax error to address a non-existing node */
     return COMMAND_PARSE_ERROR;
  }

  switch (pCmd->ZW_Common.cmd)
  {
  case ZIP_NAMING_NAME_SET:
    {
      ZW_ZIP_NAMING_NAME_SET_1BYTE_FRAME* f = (ZW_ZIP_NAMING_NAME_SET_1BYTE_FRAME*) pData;
      rd_update_ep_name(ep, (char*) &f->name1, bDatalen - 2);
      break;
    }
  case ZIP_NAMING_NAME_GET:
     /* Fallthrough */
  case ZIP_NAMING_LOCATION_GET:
    naming_reply = *c;
    void* report_id;
    /* FIXME: can we do report_id = pCmd->ZW_Common.cmd + 1 ? */
    if (pCmd->ZW_Common.cmd == ZIP_NAMING_NAME_GET) {
       report_id = (void*)ZIP_NAMING_NAME_REPORT;
    } else {
       report_id = (void*)ZIP_NAMING_LOCATION_REPORT;
    }

    if (ep->state == EP_STATE_PROBE_DONE || ep->state == EP_STATE_PROBE_FAIL) {
       /* TODO: This is the old behaviour, but it looks funny.
        * If state is FAIL, we should probe, except for sleeping nodes. */
       send_ep_name_or_location_reply(ep, (void*)report_id);
    } else {
       /* state is probably EP_STATE_PROBE_INFO. */
       /* Try to start a probe. */
       if (rd_register_node_probe_notifier(nodeid, (void*)report_id,
                                           send_ep_name_or_location_reply)) {
          rd_node_probe_update(ep->node);
          DBG_PRINTF("Executed the command already\n");
       } else {
          DBG_PRINTF("zipgateway is busy, out of probe notifier slots\n");
          return COMMAND_BUSY;
       }
    }
    break;
  case ZIP_NAMING_LOCATION_SET:
    {
      /*TODO Handle simultaneous sessions */
      naming_reply = *c;
      ZW_ZIP_NAMING_LOCATION_SET_1BYTE_FRAME* f = (ZW_ZIP_NAMING_LOCATION_SET_1BYTE_FRAME*) pData;
      rd_update_ep_location(rd_get_ep(nodeid, 0), (char*) &f->location1, bDatalen - 2);
      break;
    }
  default:
    return COMMAND_NOT_SUPPORTED;
  }
  return COMMAND_HANDLED;
}

REGISTER_HANDLER(ZIPNamingHandler, 0, COMMAND_CLASS_ZIP_NAMING, ZIP_NAMING_VERSION, SECURITY_SCHEME_UDP);
