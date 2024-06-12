/* (c) 2020 Silicon Laboratories Inc. */

#include "uip.h"
#include "process.h"
#include "procinit.h"
#include "autostart.h"
#include "sys/ctimer.h"
#include "stdbool.h"

#include "mock_parse_config.h"
#include "mock_contiki_main.h"
#include "mock_tapdev.h"
#include "mock_contiki_autostart.h"

#include <lib/zgw_log.h>

zgw_log_id_declare(mockCkti);
zgw_log_id_default_set(mockCkti);

/** The contiki specific startup things */
void contiki_setup() {
   zgw_log_enter();

   /* To initialize Contiki OS first call these init functions. */
   process_init();
   ctimer_init();
   procinit_init();

   /* Initialize a ring buffer (line reader) and start the
    * "serial-line" process.  The contiki serial line process reads
    * from the ring buffer and broadcasts a "serial-line" events
    * when it gets an EOL.
    *
    * serial_line_input() is used to write to the ring buffer.
    *
    * In ZGW, serial-line is not used for serial, but for stdin.
    * Input is passed to serial_line_input_byte() from the main loop
    * when there is an event on the stdin file descriptor.
    *
    * Note that this input is not contiki-polled, like lan or pan
    * input, it is just put in the ring buffer.  It will only be
    * processed when the serial-line-process is scheduled.
    *
    * Not needed for this demo program.
    */
   //serial_line_init();
   /* Make standard output unbuffered. */
   //setvbuf(stdout, (char *)NULL, _IONBF, 0);

   zgw_log_exit();
}

bool ctf_test_setup(void) {
   uint8_t ii = 0;

   while (test_setup_functions[ii].func != NULL) {
      if (test_setup_functions[ii].func()) {
         zgw_log(zwlog_lvl_configuration,
                 "Initialized %s\n",
                 test_setup_functions[ii].description);
         ii++;
      } else {
         zgw_log(zwlog_lvl_critical,
                 "Initializing %s failed.\n",
                 test_setup_functions[ii].description);
         return false;
      }
   }
   return true;
}

/** Initialize and start up contiki OS.  Initialize and start up the test processes. */
bool ctf_init(void) {
   /* Initialize test logging */
   zgwlog_init();

   /* Import the system configuration. */
   /* (TODO: This function should probably take argv).*/
   mock_ConfigInit();

   /* Initialize system components.  Initialize contiki OS and start
    * autostart processes if initialization works. */
   if (ctf_test_setup()) {

      /* Initialize contiki OS - timers and processes. */
      contiki_setup();

      /* Start the system processes. */
      autostart_start(autostart_testprocesses);
      return true;
   } else {
      return false;
   }
}
