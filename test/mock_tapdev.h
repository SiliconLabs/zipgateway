/* (c) 2019 Silicon Laboratories Inc. */

#ifndef MOCK_TAPDEV_H_
#define MOCK_TAPDEV_H_

#include <stdlib.h>
#include <stdbool.h>
#include "uip.h"
#include "mock_contiki_main.h"

extern size_t lan_pkt_len;

typedef void (*lan_send_notifier_t)(void);

/** Initalize LAN side connections in ZIP_Router.c.
 *
 * \ingroup CTF
 *
 * \return true */
bool setup_landev_output(void);

/**
 * Install a callback, so that the test case can get
 * send-notifications from the mock LAN.
 *
 * \ingroup CTF
 *
 */
void mock_lan_set_tc_callback(lan_send_notifier_t f);

uint8_t lan_out_mock(uip_lladdr_t *lladdr);

#endif
