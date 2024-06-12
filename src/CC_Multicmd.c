/**
  © 2019 Silicon Laboratories Inc. 
*/

#include "TYPES.H"
#include "ZW_classcmd.h"
#include "ZW_ZIPApplication.h"
#include "command_handler.h"

static command_handler_codes_t
MultiCommandHandler(zwave_connection_t *c, uint8_t* frame, uint16_t length)
{
   /* COMMAND_CLASS_MULTI_CMD + MULTI_CMD_ENCAP + number of cmds */
  switch(frame[1]) {
    case MULTI_CMD_ENCAP:
      {
        if(length< 5) return COMMAND_PARSE_ERROR; //

        uint8_t no_commnads = frame[2];
        uint8_t off=3;
        int i;
        for(i=0; i < no_commnads ; i++) {
          if(off >= length) return COMMAND_PARSE_ERROR;
          uint8_t cmd_len = frame[off+0];
          if( (off+1+cmd_len) > length) return COMMAND_PARSE_ERROR;
          zwave_connection_t c_local = *c;
          ApplicationIpCommandHandler(&c_local, &frame[off+1], cmd_len);
          off+= cmd_len +1;
        }
      }
      return COMMAND_HANDLED;
    default:
      return COMMAND_NOT_SUPPORTED;
  }
}

REGISTER_HANDLER(MultiCommandHandler, 0, COMMAND_CLASS_MULTI_CMD, MULTI_CMD_VERSION, NO_SCHEME);

