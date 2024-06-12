/* © 2020 Silicon Laboratories Inc. */
#ifndef MOCK_CONTIKI_MAIN_H_
#define MOCK_CONTIKI_MAIN_H_

#include <stdbool.h>
#include <process.h>

/**
 * \defgroup CTF Contiki Test Framework.
 *
 * A framework for creating automated test cases of ZGW that includes
 * a running contiki.
 */

/** Default wait time in mock select.
 *
 * \ingroup CTF
 */
#define CTF_DEFAULT_SELECT_DELAY 100

/**
 * Minimum timeout in mock select.
 *
 * Used to emulate ZGW behaviour.
 *
 * \ingroup CTF
 */

#define CTF_MINIMUM_SELECT_DELAY 10

/** The test should provide the implementation of this function. */
bool zgwlog_init(void);

/** Type of a component initializer function.
 *
 * \ingroup CTF
 *
 * Initializers are run before the contiki OS is initialized, ie,
 * there are no processes and no contiki timers.
 *
 * To keep initialization simple, a component should only have an
 * initialization function if it is stricly necessary.
 *
 * For example, if a component is a contiki process, initialization
 * should preferably happen when the process receives the contiki \ref
 * PROCESS_EVENT_INIT event.  Initializer functions should only be
 * created if the component initialization can fail critically or if
 * two components are mutually dependent.
 */
typedef bool (*mock_contiki_init_func_t)(void);

/** Struct to hold the system initializers.
 *
 * \ingroup CTF
 *
 * These initializers are executed in order just before starting the
 * contiki processes that are defined in the autostart list.
 *
 * If a test case uses a ZGW contiki process that also has an
 * initializer, it is important to include both (in respectively the
 * init_steps and the autostart).
 *
 * Special note for tests that use ZIP_Router.c: Note that if
 * ZIP_Router.c is included, it will do most of the initializations
 * and also the process_start for the rest of the processes.  So these
 * test cases should not start ZGW processes themselves.
 *
 * If any of the initializers fail, the whole system will fail to start.
 */
typedef struct mock_contiki_init_step {
   /** Initializer function */
   mock_contiki_init_func_t func;
   /** Pointer to a textual description of the initialization step. */
   char* description;
} mock_contiki_init_step_t;

/** The array of sysstem initializers.
 *
 * \ingroup CTF
 *
 * A default set is provided in mock_contiki_init.c
 *
 * A test case can replace the defaults with the components needed for
 * the test.
 */
extern mock_contiki_init_step_t test_setup_functions[];

/**
 * Initialize the Contiki-based Test Framework for zipgateway.
 *
 * \ingroup CTF
 *
 * Runs the initializers in \ref test_setup_functions.
 *
 * Except for the contiki OS initialization, the following steps are performed:
 * - Read the command line arguments and the config file and set up
 *   \ref cfg (the struct zip_router_config).
 *
 * - Initialize the logging system.
 *
 * - Open the (mock) serial port, if that is included in the test.
 *
 * -
 *
 * If any of these steps fail, the entire initialization fails and the ZGW stops.
 *
 * \return true if all is well, false otherwise.
 */
bool ctf_init(void);


/** Run the main process loop.
 *
 * \ingroup CTF
 *
 * Loop until \p test_process stops running.
 *
 * In the loop,
 * - ask if the mock protocol wants to inject a frame on the (mock) serial interface,
 * - polls the (mocked) external interfaces,
 * - calls the contiki schedule
 *
 * TODO: Ask if the running test case wants to inject a LAN or PAN frame.
 */
bool ctf_loop(struct process *test_process);
#endif
