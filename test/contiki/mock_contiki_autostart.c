/* © 2020 Silicon Laboratories Inc. */

#include <stdbool.h>
#include <process.h>
#include "mock_contiki_autostart.h"
#include "mock_contiki_main.h"
#include "test_helpers.h"

#include <lib/zgw_log.h>

zgw_log_id_declare(ctkiTest);
zgw_log_id_default_set(ctkiTest);

PROCESS_NAME(test_process);

bool zgwlog_init(void) {
   /* Start the logging system with a file name. */
   zgw_log_setup("test_contiki.log");
  return true;

}

bool dummy_func(void) {
   zgw_log(zwlog_lvl_configuration,
           "Dummy initializer\n");
  return true;
}

mock_contiki_init_step_t test_setup_functions[] = {
   /* Demo dummy */
   {dummy_func, "Dummy step\n"},
   {NULL, "Terminator"}};

AUTOSTART_TESTPROCESSES(&test_process);
