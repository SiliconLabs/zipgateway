/* (c) 2020 Silicon Laboratories Inc. */

#include "process.h"
#include "procinit.h"
#include "stdbool.h"
#include <lib/zgw_log.h>

#include "mock_contiki_main.h"
#include "test_helpers.h"

zgw_log_id_define(mockCkti);
zgw_log_id_default_set(mockCkti);

PROCESS_NAME(test_process);

/* Default main function for a contiki-based test */
int main() {
    verbosity = test_case_start_stop;

    if (!ctf_init()) {
       zgw_log(zwlog_lvl_critical, "Cannot initialize test case\n");
       return 1;
    }

    /* Log that we have entered this function.  Only works after log
     * has been initialized. :-( */
    zgw_log_enter();

    ctf_loop(&test_process);

    close_run();

    /* We are now exiting this function */
    zgw_log_exit();

    /* Close down the logging system at the end of main(). */
    zgw_log_teardown();

    return numErrs;
}
