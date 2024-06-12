/* (c) 2020 Silicon Laboratories Inc. */

#include "process.h"
#include "procinit.h"
#include "stdbool.h"

#include "serial_api_process.h"

#include "etimer.h"
#include "mock_contiki_main.h"
#include "mock_conhandle.h"

#include <lib/zgw_log.h>

zgw_log_id_declare(mockCkti);
zgw_log_id_default_set(mockCkti);

PROCESS_NAME(test_process);

/** Default contiki loop for a contiki-based test that uses the serial mock.
 *
 * ZGW contiki main loop does:
 *
 * - process_run() until contiki is flushed - this handles all poll events and one process.
 *
 * - bail out if zip_process has stopped.
 *
 * - compute delay based on next etimer timeout (so that we get less
 *   imprecise timeouts) and a max delay (so that ZGW is responsive on
 *   the interfaces).
 *
 * - select(delay) on the external dependencies, so that the ZGW is
 *   not a busy wait-loop.  If there are any hits, read the input and
 *   poll the corresponding process (serial_api_process or
 *   tapdev_process).  Special handling of stdin ("serial_line"):
 *   input is read, but the process is not polled, just scheduled
 *   normally.
 *
 * - poll etimer.
 *
 * The mock main loop bails out if test_process stops running instead.
 * It "mock" selects on the mock conhandle and polls the ZGW
 * serial_api_process if there is data.  It polls etimer as usual.
 *
 */
bool ctf_loop(struct process *test_process) {
   while(1) {

      /* Keep going as long as there are events on the event queue or poll has
       * been requested (process_run() processes one event every time it's called)
       */
      while (process_run()) {
         zgw_log(zwlog_lvl_data, "Keep going, process_run() is true\n");
      }

      useconds_t delay = CTF_DEFAULT_SELECT_DELAY;
      clock_time_t time_next_etimer_timeout  = etimer_next_expiration_time();
      clock_time_t time_now  = clock_time();

      /* If time_next_etimer_timeout is 0, there are no outstanding timers. */
      if (time_next_etimer_timeout != 0) {
         if (time_next_etimer_timeout < time_now) {
            /* A contiki timer already expired, better get on with it,
             * but ZGW still waits in select for LAN/PAN events first. */
            delay = CTF_MINIMUM_SELECT_DELAY;
         } else {
            if (time_next_etimer_timeout < (time_now + CTF_DEFAULT_SELECT_DELAY)) {
               /* There is a contiki timeout before the default select timeout. */
               delay = time_next_etimer_timeout - time_now;
            }
         }
      }

      /* Check for external events... */
      /* Ask the mock protocol if it wants to inject stg into the gateway at this point. */
      if (mock_conhandle_select(delay)) {
         /* Poll the process that handles an external event. */
         process_poll(&serial_api_process);
      }
      /* Check if there are time-out events by polling etimer. */
      etimer_request_poll();

      /* Bail out if master process has stopped running for any reason */
      if (!process_is_running(test_process))
      {
         break;
      }
   }
   return true;
}
