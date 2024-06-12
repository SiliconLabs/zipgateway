/*
 * CC_Supervision.c
 *
 *  Created on: Dec 13, 2016
 *      Author: aes
 */


#include "command_handler.h"
#include "ZW_classcmd.h"
#include "zip_router_ipv6_utils.h" /* nodeOfIP */
#include "ZIP_Router_logging.h"
#include "ResourceDirectory.h"
/* Duplicate detection */
static uip_ip6addr_t last_src_ip;
static uint8_t last_sessionid;

static command_handler_codes_t
SupervisionCommandHandler(zwave_connection_t *c, BYTE* pData, uint16_t bDatalen)
{
  ZW_SUPERVISION_REPORT_FRAME rep;
  command_handler_codes_t rc;

  if(pData[1] != SUPERVISION_GET) {
  /* Returning CLASS_NOT_SUPPORTED, lets other commands in the Supervision CC be forwarded to unsoliticited destination. For e.g. Supervision Report. */
    return CLASS_NOT_SUPPORTED;
  }

  ZW_SUPERVISION_GET_FRAME* f =(ZW_SUPERVISION_GET_FRAME*) pData;

  rep.cmdClass = COMMAND_CLASS_SUPERVISION;
  rep.cmd =  SUPERVISION_REPORT;
  rep.duration=0;


  /* Duplicate detection */
  if(( (f->sessionid & 0x3F) == last_sessionid) &&
    ((c->rx_flags & RECEIVE_STATUS_TYPE_MASK)  == RECEIVE_STATUS_TYPE_SINGLE) &&
    (uip_ipaddr_cmp(&last_src_ip, &c->ripaddr)))
  {
     return COMMAND_HANDLED;
  }
  uip_ipaddr_copy(&last_src_ip,&c->ripaddr);
  last_sessionid = f->sessionid & 0x3F;
  rep.sessionid = f->sessionid & 0x3F; //Note we do not support updates;

  // TRUE since we unwrapping SUPERVISION encapsulation
  rc = ZW_command_handler_run(c,pData+4,f->encapsulatedCommandLength, TRUE);

  /* If multicast, handle the command but not to reply it */
  if(((c->rx_flags & RECEIVE_STATUS_TYPE_MASK)  != RECEIVE_STATUS_TYPE_SINGLE) ||
      uip_is_addr_mcast(&c->lipaddr)) {
    return COMMAND_HANDLED;
  }

  //This was not supported by the gateway, check if the unsolicited destination should handle it.
  if((rc == CLASS_NOT_SUPPORTED) && rd_check_security_for_unsolicited_dest(nodeOfIP(&c->ripaddr),c->scheme,pData+4,f->encapsulatedCommandLength)) {
    //Let the unsolicited destination handle this command.
    return CLASS_NOT_SUPPORTED;
  }

  switch(rc) {
  case COMMAND_HANDLED:
    rep.status = SUPERVISION_REPORT_SUCCESS;
    break;

  case COMMAND_PARSE_ERROR:
  case COMMAND_BUSY: // There is no BUSY status in Supervision, we indicate instead that the requested operation failed.
    rep.status = SUPERVISION_REPORT_FAIL;
    break;

  case COMMAND_NOT_SUPPORTED:
  case CLASS_NOT_SUPPORTED:
  case COMMAND_POSTPONED:
  case COMMAND_CLASS_DISABLED:
    rep.status = SUPERVISION_REPORT_NO_SUPPORT;
    break;
  }

  ZW_SendDataZIP(c,&rep,sizeof(rep),0);

  return COMMAND_HANDLED;
}

REGISTER_HANDLER(SupervisionCommandHandler, 0, COMMAND_CLASS_SUPERVISION, SUPERVISION_VERSION, NO_SCHEME);
