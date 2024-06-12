/* © 2020 Silicon Laboratories Inc. */

#ifndef MOCK_BRIDGE_CONTROLLER_H_
#define MOCK_BRIDGE_CONTROLLER_H_

#include <stdint.h>
/**
 * \defgroup MP Mock Prococol Framework
 *
 * \ingroup CTF
 *
 * Mocks/stubs some of the protocol behaviour, so that, e.g.,
 * ZIP_Router.c can initialize a test ZGW without a real underlying
 * protocol implementation.
 */

/** Whether the test needs any more serial interface with the serial
 * mock.  (This only covers the responses set up specifically for this
 * case, not the automated responses.)
 *
 * \ingroup MP
 *
 * This can be used to block the mock from replying during part of the
 * test. */
extern bool bridge_controller_mock_active;

/* FIXME: remove this - currently mock_conhandle uses it. */
extern bool reply_expected;
/* FIXME: remove this - currently mock_conhandle uses it. */
extern bool reply_ready;

/** Find the default reply to the serial command \p cmd.
 *
 * \ingroup MP
 *
 * Looks up in the mp_std_replies.
 */
uint8_t* mp_find_reply_by_cmd(uint8_t cmd);


/* hacked guess-gorithm to find out if the mock/test case should reply to the frame \p cmd.
 *
 * \ingroup MP
 *
 * The frame was just sent from the zipgateway.
 *
 * Format SOF; len+3; type; cmd; data.
*/
void mp_parse_frame(uint8_t cmd, uint8_t type, uint8_t len, uint8_t *tx_buf_last_sent);

/** Check if the mock wants to inject a frame.
 *
 * \ingroup MP
 *
 * Eg, when adding a node, protocol will send an unsolicited (from the
 * gateway's POW) node-found to zgw.
 *
 * Called from \ref mock_conhandle_select() to mock a real select() call.
 *
 * Note that this works by injecting a frame when asked, unlike a real
 * select, which is driven by actual external stimuli.
 */
bool mp_check_state(void);
#endif
