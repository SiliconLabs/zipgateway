/**
  © 2019 Silicon Laboratories Inc. 
*/



#include "command_handler.h"


/*
 * This fucntions is for handling wakeup informations from mailbox nodes.
 * It should be noted that this handler is not registred with the command handler
 * framework, as its a controlling command class. The command handler framework is 
 * for handling supporting commnads.
 * 
 * @param c                Pointer to the coneciton object
 * @param frame            Frame data
 * @param length           Frame data length
 * @return                 Handler code
 */
command_handler_codes_t
WakeUpHandler(zwave_connection_t *c, uint8_t *frame, uint16_t length);
