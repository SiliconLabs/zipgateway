/*
 * S2_multicast_auto.h
 *
 *  Created on: 8 Jun 2018
 *      Author: anesbens
 */

#ifndef SRC_TRANSPORT_S2_MULTICAST_AUTO_H_
#define SRC_TRANSPORT_S2_MULTICAST_AUTO_H_

#include "ZW_SendDataAppl.h"

/**
 * Send one or more multicast messages to a list of nodes which does not necessarily belong to the
 * same security group or may even be non-secure.
 *
 * \param p                 ts_param containing the package transmission details.
 * \param data              Data package to send.
 * \param data_len          Length of data to send.
 * \param send_sc_followups Should single cast follow-ups be sent after the initial multicast
 * \param callback          Function to call when the multicast transmission (including the follow-ups) has completed.
 * \param user Caller data that should be passed to the callback function.
 */

uint8_t sec2_send_multicast_auto_split(ts_param_t *p, const uint8_t *data,
    uint8_t data_len, BOOL send_sc_followups,
    ZW_SendDataAppl_Callback_t callback, void *user);

/**
 *  Abort the ongoing transmission if there is any.
 */
void sec2_send_multicast_auto_split_abort();

/**
 * Initialize the module.
 */
void sec2_send_multicast_auto_init();

#endif /* SRC_TRANSPORT_S2_MULTICAST_AUTO_H_ */
