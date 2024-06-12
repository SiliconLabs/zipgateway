/* © 2020 Silicon Laboratories Inc. */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <signal.h>
#include <lib/zgw_log.h>
#include "test_helpers.h"
#include "process.h"
#include "procinit.h"
#include "sys/etimer.h"
#include "router_events.h"
#include "zip_router_config.h"
#include "uip.h"
#include <unistd.h>


/* To test ZIP_Router.c, include the eeprom file:
   #include "dev/eeprom.h" */

/* To test on the LAN side, hook into the LAN send function:
   #include "mock_tapdev.h"
*/

#include "mock_contiki_main.h"
#include "test_contiki.h"
#include "mock_contiki_autostart.h"

zgw_log_id_define(ctkiTest);
zgw_log_id_default_set(ctkiTest);

/** A default type for the test case functions that are sent to the
 * test process. */
typedef void (*test_case_t)(void);

/* The test case that is running right now.
 * The test case init function is used to identify the case.
 */
test_case_t curr_case = NULL;

static bool test_process_stop_flag = false;

/* NVM file mock */
const char* linux_conf_nvm_file = "nvm.dat";


PROCINIT(&etimer_process);
PROCESS(test_process, "Unit Test Process");
PROCESS_NAME(test_process);

/* Test process controls */
bool waiting_for_mdns = false;

/* put some sleeps in ConUpdate in the beginning, to wait for the
 * lan-controlling timer. */
bool waiting_for_lan_if = false;


/* end of helpers */

typedef void (*case_function_t)(void);

/* Non-process cases */
void case1_close();

void case1_init(void);

#include "hello.h"
void case1_init(void) {
   bool res;
   start_case("case 1", NULL);

   res = hello();
   check_true(res == 1, "Hello from case 1 succeeded\n");
   if (res) {
      case1_close();
   }
}

void case1_close() {
   process_post(&test_process, CASE_DONE, &case1_init);
   close_case("case 1");
   test_process_stop_flag = true; /* this is the last case */
}


void test_init() {
/* The test can install a callback to receive the frames sent on the LAN side:
   mock_lan_set_tc_callback(NULL); */

/* Start zip_process here if that should be included in the test */
    /* Test of ZIP_Router.c needs an eeprom file */
//    eeprom_init();
//   process_start(&zip_process, 0);

   /* Post an event to myself that will arrive when zip_process has initialized. */
   process_post(&test_process, INIT_DONE, NULL);

/* Start the first test case if it should run immediately (or
 * immediately after zip_process has been initialized */
   process_post(&test_process, RUN_CASE, case1_init);
}

struct etimer watchdog;

/* Main test process runner. */

PROCESS_THREAD(test_process, ev, data)
{
    PROCESS_BEGIN()

    etimer_set(&watchdog, 1*CLOCK_SECOND);

    zgw_log(zwlog_lvl_warning, "Hello from the test process\n");

    /* TODO: check status of gw initialization in each iteration of
     * the loop and compare with the test cases expectations. */
    while(1) {

       if (ev == PROCESS_EVENT_INIT) {
          zgw_log(zwlog_lvl_warning, "processing init\n");
          /* Call a test case specific initializer */
          test_init();
          zgw_log(zwlog_lvl_warning, "test process init complete\n");

       } else if (ev == INIT_DONE) {
           /* If the test is running with zip_process, it now waits for contiki to get the tcpip ready event */
          waiting_for_lan_if = true;

       } else if (ev == tcpip_event) {
          if (data == (void*) TCP_READY) {
             /* Now we are able to send IPv6 packages */
             waiting_for_lan_if = false;
             zgw_log(zwlog_lvl_error, "Contiki tcp is up now, go back to fast serial polling.\n");
             process_post(&test_process, RUN_CASE, case1_init);
          }

       } else if (ev == RUN_CASE) {
          case_function_t test_func = (case_function_t)data;
          zgw_log(zwlog_lvl_warning, "Starting new test case\n");
          curr_case = test_func;
          etimer_restart(&watchdog);
          test_func();

       } else if (ev == CASE_DONE) {
          case_function_t test_func = (case_function_t)data;
          zgw_log(zwlog_lvl_error, "No more cases, terminating\n");
          process_post(PROCESS_BROADCAST, PROCESS_EVENT_EXIT, 0);
          break;

       } else if (ev == INIT_LAN_PACKET_RECEIVED) {
          uint8_t *pkt = (uint8_t*)data;
          /* If the test case has LAN side interaction, it can be notified here. */
          zgw_log(zwlog_lvl_control, "Handle LAN packet in test process.\n");
          zgw_log(zwlog_lvl_warning, "TODO: implement parsing of this IP packet\n");

       } else if (ev == PROCESS_EVENT_TIMER) {
          if (data == &watchdog) {
             check_true(test_process_stop_flag, "Test case can complete before the watchdog times out");
             zgw_log(zwlog_lvl_error, "Watchdog stops test\n");
             test_process_stop_flag = true;
             /* Flush contiki, then die. */
             process_post(PROCESS_BROADCAST, PROCESS_EVENT_EXIT, 0);
          }
          /* If the test case uses another etimer than watchdog, timeout are handled here */
       } else if (ev == PROCESS_EVENT_EXIT) {
          process_post(PROCESS_BROADCAST, PROCESS_EVENT_EXIT, 0);
          break;

       } else {
          /* If something happens that the test case does not expect, that is an error.*/
          zgw_log(zwlog_lvl_error, "test_process unexpected exit\n");
          process_post(PROCESS_BROADCAST, PROCESS_EVENT_EXIT, 0);
          break;
       }
      PROCESS_WAIT_EVENT()
      ;
    }

  PROCESS_END()
}
