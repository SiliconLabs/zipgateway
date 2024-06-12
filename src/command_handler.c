/* © 2017 Silicon Laboratories Inc.
 */
/*
 * command_handler.c
 *
 *  Created on: Apr 7, 2016
 *      Author: aes
 */
#include "zip_router_ipv6_utils.h" /* nodeOfIP */
#include "ZIP_Router.h" /* ApplicationCommandHandlerZIP */
#include "ZW_ZIPApplication.h" /* net_scheme */

#include "command_handler.h"
#include "ZIP_Router_logging.h"
#include "ZW_command_validator.h"

#ifdef __APPLE__
extern command_handler_t __start__handlers[] __asm("section$start$__TEXT$__handlers");
extern command_handler_t __stop__handlers[] __asm("section$end$__TEXT$__handlers");
#else
extern command_handler_t __start__handlers[];
extern command_handler_t __stop__handlers[];
#endif

static uint16_t* disable_command_list;
static uint32_t  disable_command_list_len;

int is_in_disabled_list(command_handler_t* fn) {
  uint32_t i;

  for(i= 0; i<disable_command_list_len; i++ ) {
    if( fn->cmdClass == disable_command_list[i] ) {
      return 1;
    }
  };
  return 0;
}

/*
 * If a handler is supported with NO_SCHEME we allow the package no matter what
 * scheme it was received on.
 *
 * If a handler has a minimal security scheme it has to be sent with the net_scheme
 * ie the highest scheme supported by the node.
 */
static int
supports_frame_at_security_level(command_handler_t *fn, security_scheme_t scheme)
{
  if(scheme==USE_CRC16 ) {
    scheme = NO_SCHEME;
  }


  if(fn->minimal_scheme == NET_SCHEME) {
    return (scheme == net_scheme) || ( scheme == SECURITY_SCHEME_UDP );
  } else if ((fn->minimal_scheme == NO_SCHEME) || ( scheme_compare(scheme , net_scheme) && scheme_compare(scheme, fn->minimal_scheme)))
  {
    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

/* If a handler has a minimal security scheme it has to be sent with the net_scheme,
 * however, there are some exeptions on certian CCs. For example, Manufacturer CC handler must only reply 
 * Manufacturer Specific Get Command non-securely if it was granted S0 network key
 * according to SDS13782 CC:0072.01.00.41.004 requiremnt.*/
static int
exception_CC_security_level_check (security_scheme_t scheme, uint16_t exemptcmdClass, uint8_t exemptCmd)
{
  if ( (net_scheme == SECURITY_SCHEME_0) && (exemptcmdClass == COMMAND_CLASS_MANUFACTURER_SPECIFIC_V2)
      && (scheme == NO_SCHEME) && (exemptCmd == MANUFACTURER_SPECIFIC_GET) ) {
      DBG_PRINTF("Gateway is included in security 0 network. So responding to non-secure manufacture specific is allowed\n");
      return TRUE;
  } else {
    return FALSE;
  }
}

command_handler_codes_t
ZW_command_handler_run(zwave_connection_t *connection, uint8_t* payload, uint16_t len, uint8_t bSupervisionUnwrapped)
{
  int version = 0;
  command_handler_t *fn, *checked_fn = NULL;
  uint8_t rc = CLASS_NOT_SUPPORTED;


  /* Parsing checking */
  validator_result_t result = ZW_command_validator(payload,len,&version);
  if( result != PARSE_OK ) {
    ERR_PRINTF("Command validation of 0x%02x : 0x%02x failed\n", payload[0], payload[1]);

    switch(result) {
    case UNKNOWN_CLASS:
      ERR_PRINTF("Unknown command class\n");
      rc = CLASS_NOT_SUPPORTED;
      break;
    case UNKNOWN_COMMAND:
      ERR_PRINTF("Unknown command\n");
      rc = COMMAND_NOT_SUPPORTED;
      break;
    case UNKNOWN_PARAMETER:
      ERR_PRINTF("Unknown parameter\n");
      /*no break*/
    case PARSE_FAIL:
      ERR_PRINTF("parse fail\n");
      rc = COMMAND_PARSE_ERROR;
      break;
    case PARSE_OK:
      rc = PARSE_OK;
      break;
    }
  }
  else
  {
    DBG_PRINTF("Cmd class 0x%02x command 0x%02x identified as version %d\n", payload[0], payload[1], version);
  }

  if (bSupervisionUnwrapped &&
      (payload[0] == COMMAND_CLASS_WAKE_UP) &&
      (payload[1] == WAKE_UP_NOTIFICATION || payload[1] == WAKE_UP_INTERVAL_REPORT))
  {
    DBG_PRINTF("Application commands %02x encapped inside supervision received.\n", payload[0]);
    ts_param_t p;
    p.tx_flags = connection->tx_flags;
    p.rx_flags = connection->rx_flags;
    p.scheme = connection->scheme;
    p.dendpoint = connection->rendpoint;
    p.sendpoint = connection->lendpoint;
    p.snode = nodeOfIP(&connection->ripaddr);
    p.dnode = nodeOfIP(&connection->lipaddr);
    ApplicationCommandHandlerZIP(&p, (ZW_APPLICATION_TX_BUFFER*) payload, len);
    return COMMAND_HANDLED;
  }

  /* Security checking */
  for (fn = __start__handlers; fn < __stop__handlers; fn++)
  {
    if (fn->cmdClass == payload[0])
    {
      if (supports_frame_at_security_level(fn, connection->scheme)
          || exception_CC_security_level_check (connection->scheme, payload[0], payload[1]))
      {
        if(is_in_disabled_list(fn) ) {
          WRN_PRINTF("Command rejected because it's disabled.\n");
          return COMMAND_CLASS_DISABLED;
        } else {
          checked_fn = fn;
        }
      } else {
        WRN_PRINTF("Command rejected because of wrong security class %s\n", network_scheme_name(connection->scheme));
        return CLASS_NOT_SUPPORTED;
      }
    }
  }

  if(rc == PARSE_OK && checked_fn != NULL) {
    return checked_fn->handler(connection, payload, len);
  } else if(checked_fn == NULL){
    /* No matched command class */
    DBG_PRINTF("Command class 0x%02x : 0x%02x not supported\n", payload[0], payload[1]);
    return CLASS_NOT_SUPPORTED;
  } else {
    /* Parse error */
    return rc;
  }

}

uint8_t
ZW_comamnd_handler_version_get(security_scheme_t scheme, uint16_t cmdClass)
{
  command_handler_t *fn;

  for (fn = __start__handlers; fn < __stop__handlers; fn++)
  {
    if( is_in_disabled_list(fn) ) continue;

    if (fn->cmdClass == cmdClass)
    {
      if (supports_frame_at_security_level(fn, scheme))
      {
        return fn->version;
      }
    }
  }
  return 0x00; //UNKNOWN_VERSION;;
}

uint8_t
ZW_command_handler_get_nif(security_scheme_t scheme, uint8_t* nif, uint8_t max_len)
{

  command_handler_t *fn;
  uint8_t n = 0;

  for (fn = __start__handlers; fn < __stop__handlers; fn++)
  {
    if (is_in_disabled_list(fn))
      continue;

    if (supports_frame_at_security_level(fn, scheme))
    {
      /*If we are building a secure nif then don't add stuff which is supported non-secure*/
      if(scheme !=NO_SCHEME && supports_frame_at_security_level(fn, NO_SCHEME)) {
        continue;
      }

      /* Z-Wave Plus info is inserted elsewhere because it MUST be listed first in NIF */
      if (fn->cmdClass == COMMAND_CLASS_ZWAVEPLUS_INFO_V2)
      {
        continue;
      }
      if ((fn->cmdClass & 0xF0) == 0xF0)
      {
        if (max_len - n < 2)
        {
          WRN_PRINTF("Max classes exceeded.\n");
          assert(0);
          return n;
        }
        nif[n++] = (fn->cmdClass >> 8) & 0xFF;
        nif[n++] = fn->cmdClass & 0xFF;
      }
      else
      {
        if (max_len - n < 1)
        {
          WRN_PRINTF("Max classes exceeded.\n");
          assert(0);
          return n;
        }
        nif[n++] = fn->cmdClass & 0xFF;
      }
    }
  }
  return n;
}

void
ZW_command_handler_init()
{
  command_handler_t *fn;

  for (fn = __start__handlers; fn < __stop__handlers; fn++)
  {
    if (fn->init)
    {
      fn->init();
    }
  }
}


void ZW_command_handler_disable_list(uint16_t *cmdList, uint8_t cmdListLen) {
  disable_command_list = cmdList;
  disable_command_list_len = cmdListLen;
}
