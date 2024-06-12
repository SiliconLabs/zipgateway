/* (c) 2020 Silicon Laboratories Inc. */

#ifndef __MOCK_CONTIKI_AUTOSTART_H__
#define __MOCK_CONTIKI_AUTOSTART_H__

#include "process.h"

#if AUTOSTART_ENABLE

/** The test processes that will be auto-started by contiki main.
 *
 * \ingroup CTF
 *
 * Autostart happens just before entering the main execution loop, but
 * after the basic initialization of the underlying system, including
 * contiki. */
#define AUTOSTART_TESTPROCESSES(...)					\
struct process * const autostart_testprocesses[] = {__VA_ARGS__, NULL}

#else /* AUTOSTART_ENABLE */
#define AUTOSTART_TESTPROCESSES(...)					\
extern int _dummy
#endif /* AUTOSTART_ENABLE */

extern struct process * const autostart_testprocesses[];
#endif
