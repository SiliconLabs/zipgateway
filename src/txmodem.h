/* © 2018 Silicon Laboratories Inc.  */

#include <stdint.h>

/**
 * Send binary file over the serial port using the XMODEM protocol.
 * 
 * @param filename File to send
 */
uint8_t xmodem_tx(const char *filename);
