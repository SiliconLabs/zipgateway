/* © 2017 Silicon Laboratories Inc.
 */
#ifndef SERIAL_API_PROCESS_H_
#define SERIAL_API_PROCESS_H_
#include <process.h>
/** \ingroup processes
 * \defgroup ZIP_Serial Z/IP Serial API process
 * Handles all serial communication with the Z-Wave module
 * @{
 */
PROCESS_NAME(serial_api_process);

/**
 * Re-initialize the low level serial communication. This will
 * re-read the supported serial functions as well as the chip type.
 */
int serial_process_serial_init(const char *serial_port);
/** @} */
#endif
