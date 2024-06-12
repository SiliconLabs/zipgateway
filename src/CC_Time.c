/* © 2018 Silicon Laboratories Inc.
 */

#include "command_handler.h"
#include "ZW_classcmd.h"
#include <time.h>

/**
 * Command handler for the Time Get command.
 *
 * The Gateway will send return a Time Report frame containing
 * the current time (hours, minutes and seconds).
 *
 * \see TimeHandler.
 */
command_handler_codes_t
TimeHandler_TimeGet(zwave_connection_t *conn, const uint8_t *frame, uint16_t length)
{
  /* Returns the current time when receiving a Time Get Command */
  time_t current_time;
  struct tm* current_time_info;

  time(&current_time); // Fetch the current time into current_time.
  current_time_info = localtime(&current_time);

  ZW_TIME_REPORT_FRAME report =
  {
    .cmdClass                  = COMMAND_CLASS_TIME,
    .cmd                       = TIME_REPORT,
    .hourLocalTime             = current_time_info->tm_hour,
    .minuteLocalTime           = current_time_info->tm_min,
    .secondLocalTime           = current_time_info->tm_sec
  };

  ZW_SendDataZIP(conn, (BYTE*) &report, sizeof(report), NULL);

  return COMMAND_HANDLED;
}

/**
 * Command handler for the Date Get command.
 *
 * The Gateway will send return a Date Report frame containing
 * the current date (day, month and year).
 *
 * \see TimeHandler.
 */
command_handler_codes_t
TimeHandler_DateGet(zwave_connection_t *conn, const uint8_t *frame, uint16_t length)
{
  /* Returns the current date of the year when receiving a Date Get Command */
  time_t current_time;
  struct tm* current_time_info;

  time(&current_time); // Fetch the current time into current time.
  current_time_info = localtime(&current_time);

  ZW_DATE_REPORT_FRAME report =
  {
    .cmdClass         = COMMAND_CLASS_TIME,
    .cmd              = DATE_REPORT,
    .year1            = (current_time_info->tm_year+1900) >> 8, //MSB
    .year2            = (current_time_info->tm_year+1900) & 0xFF, //LSB
    .month            = current_time_info->tm_mon+1,
    .day              = current_time_info->tm_mday
  };

  ZW_SendDataZIP(conn, (BYTE*) &report, sizeof(report), NULL);

  return COMMAND_HANDLED;
}



/**
 * \ingroup CMD_handler
 * \defgroup Time_CC_handler Time CC Handler
 * The Z/IP Gateway supports the Time Command Class, version 1.
 * 
 * This Command Class is used to distribute the current time/date in the Z-Wave network.
 * The Time Command Class is always present in the NIF and supported non-securely,
 * in order to answer time and date requests from nodes bootstrapped at any security
 * level.
 * This Command Class handler assumes that the local time on the Z/IP Gateway is set 
 * correctly.
 * 
 * @{
 */

/**
 * Time Command Class handler: this is the entry point for every frame
 * containing the Time Command Class, which reponds to time and date requests
 * with the appropriate report.
 * 
 * Dispatches incoming commands to the appropriate command handler functions.
 * 
 * \param conn   The Z-wave connection.
 * \param frame  The incoming command frame.
 * \param length Length in bytes of the incoming frame.
 * \return \ref command_handler_codes_t representing the command handling status.
 */
command_handler_codes_t
TimeHandler(zwave_connection_t *conn, uint8_t *frame, uint16_t length)
{
  command_handler_codes_t  rc       = COMMAND_NOT_SUPPORTED; // Not supported by default
  ZW_COMMON_FRAME         *zw_frame = (ZW_COMMON_FRAME*) frame;

  /* Make sure we got the minimum required frame size */
  if (length < sizeof(ZW_COMMON_FRAME))
  {
    return COMMAND_NOT_SUPPORTED; //Supervision should return NO_SUPPORT if the command could not be parsed
  }

  switch (zw_frame->cmd)
  {
    case TIME_GET:
        rc = TimeHandler_TimeGet(conn, frame, length);
      break;

    case DATE_GET:
        rc = TimeHandler_DateGet(conn, frame, length);
      break;

    default:
      rc = COMMAND_NOT_SUPPORTED;
    break;
  }

  return rc;
}
/** 
 * @} 
 */ 

/**
 * Register the Time Command Class handler. 
 *
 * \ref TimeHandler will be called whenever a
 * frame with the Time Command Class is received.
 */
REGISTER_HANDLER(TimeHandler, 0, COMMAND_CLASS_TIME, TIME_VERSION, NO_SCHEME);
