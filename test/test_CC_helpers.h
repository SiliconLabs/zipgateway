/* © 2018 Silicon Laboratories Inc.
 */

#ifndef TEST_CC_HELPERS_H_
#define TEST_CC_HELPERS_H_

/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/
#include <command_handler.h>

/****************************************************************************/
/*                          Network Management helpers                      */
/****************************************************************************/

/**
 * Dummy argument for simulating the connection argument when testing
 * the command parts of a command class handler.
 */
zwave_connection_t dummy_connection;


/****************************************************************************/
/*                              SendData Helpers                            */
/****************************************************************************/

/**
 * Helper struct for the #ZW_SendDataZIP mock.
 */
struct {
 zwave_connection_t        *c;
 char                       dataptr[1280];  /* Largest IPv6 package size */
 uint16_t                   datalen;
 ZW_SendDataAppl_Callback_t cbFunc;
} ZW_SendDataZIP_args;

/**
 * Call a command handler and check that it generated and sent the expected
 * message and returned the expected return code.
 *
 * If any of the checks fail, the test case will be marked as failed.
 *
 * \param cmd_handler       function pointer to command handler to call.
 * \param in_data           data to pass to command handler.
 * \param in_len            length of data passed to command handler.
 * \param expected_out_data data the command handler is expected to send with
 *                          ZW_SendDataZIP. Set to NULL if the check of output
 *                          data should be skipped (in case the input data does
 *                          not cause the command handler to send any data with
 *                          ZW_SendDataZIP).
 * \param expected_out_len  expected length of data the command handler sends
 *                          with ZW_SendDataZIP.
 * \param expected_ret      expected return code from the command handler.
 */
void check_cmd_handler_results(command_handler_codes_t (*cmd_handler)(zwave_connection_t *, uint8_t *, uint16_t),
                               uint8_t *in_data, uint16_t in_len,
                               uint8_t *expected_out_data, uint16_t expected_out_len,
                               command_handler_codes_t expected_ret);

#endif
