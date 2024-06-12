/* © 2018 Silicon Laboratories Inc.
 */


/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/
#include "test_helpers.h"
#include "test_utils.h"
#include "test_CC_helpers.h"
#include <string.h>


/****************************************************************************/
/*                          Network Management helpers                      */
/****************************************************************************/

/**
 * Stub for CC_NetworkManagement.c
 */
void NetworkManagement_smart_start_init_if_pending(void)
{
  //stub
}

/****************************************************************************/
/*                          Resource Directory helpers                      */
/****************************************************************************/

/**
 * The location may contain . but not start or end with them
 * Stub for the definition in ResourceDirectory.c
 */
u8_t
validate_location(char* name, u8_t len)
{
  if (name[0] == '_')
    return 0;
  if (name[0] == '.')
    return 0;
  if (name[len - 1] == '.')
    return 0;
  return 1;
}

/**
 * Validate a the Name part of the service name
 * Stub for the definition in ResourceDirectory.c
 */
u8_t
validate_name(char* name, u8_t len)
{
  int i;
  if (name[0] == '_')
    return 0;

  for (i = 0; i < len; i++)
  {
    if (name[i] == '.')
      return 0;
  }
  return 1;
}


/****************************************************************************/
/*                              SendData Helpers                            */
/****************************************************************************/

/**
 * Mock of ZW_SendDataZIP.
 *
 * Take the arguments and store them for verification.
 * 
 * This allows verification of data sent on the network by a command
 * class handler.
 */
void
ZW_SendDataZIP(zwave_connection_t *c,
               const void *dataptr,
               u16_t datalen,
               ZW_SendDataAppl_Callback_t cbFunc)
{
  ZW_SendDataZIP_args.c = c;
  memcpy(ZW_SendDataZIP_args.dataptr, dataptr, min(sizeof(ZW_SendDataZIP_args.dataptr), datalen));
  ZW_SendDataZIP_args.datalen = datalen;
  ZW_SendDataZIP_args.cbFunc = cbFunc;
}

void
check_cmd_handler_results(command_handler_codes_t (*cmd_handler)(zwave_connection_t *, uint8_t *, uint16_t),
                          uint8_t *in_data, uint16_t in_len,
                          uint8_t *expected_out_data, uint16_t expected_out_len,
                          command_handler_codes_t expected_ret)
{
  memset(&ZW_SendDataZIP_args, 0, sizeof(ZW_SendDataZIP_args));

  /* Invoke the command handler */
  command_handler_codes_t ret = cmd_handler(&dummy_connection, in_data, in_len);

  check_true(expected_ret == ret,
             "Command handler return value");

  check_true(expected_out_len == ZW_SendDataZIP_args.datalen,
             "Data length passed to ZW_SendDataZIP");

  uint16_t cmp_len = min(expected_out_len, ZW_SendDataZIP_args.datalen);

  check_mem(expected_out_data, (const uint8_t*)ZW_SendDataZIP_args.dataptr, cmp_len, 
             "Byte %02d: expected: 0x%02x was: 0x%02x\n",
             "Data passed to ZW_SendDataZIP");
}
