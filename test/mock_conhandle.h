/* (c) 2020 Silicon Laboratories Inc. */

#ifndef MOCK_CONHANDLE_H_
#define MOCK_CONHANDLE_H_

#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

#include <lib/zgw_log.h>

zgw_log_id_declare(mockCon);

/** Check if the mock wants to inject a frame.
 *
 * Eg, when adding a node, protocol will send an unsolicited (from the
 * gateway's POW) node-found to zgw.
 *
 * Should be called from the main loop instead of "select" on the serial interface. */
bool mock_conhandle_select(useconds_t timeout);

/** Interface to the mock protocol.
 *
 * Send a frame to the zipgateway DUT.
 */
void mock_conhandle_inject_frame(uint8_t *frame);
#endif
