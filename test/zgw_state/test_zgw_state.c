/* © 2019 Silicon Laboratories Inc. */

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
#include "Serialapi.h"
#include "uip.h"
#include <unistd.h>
#include "zgw_backup.h"
#include "provisioning_list_files.h"

#include "dev/eeprom.h"
#include "mock_conhandle.h"
#include "mock_dhcp.h"
//#include "mock_parse_config.h"
#include "mock_contiki_main.h"
#include "mock_tapdev.h"
#include "mock_contiki_autostart.h"

#include "test_zgw_state.h"

zgw_log_id_define(zstateTest);
zgw_log_id_default_set(zstateTest);

/** Test the idle state detection in ZIP_Router that is used to control backups.
 *
 * The test cases work by sending an event to ZIP_Router to request a backup.
 *
 * To test a success scenario, a test case has to set up the comm file and the
 * backup directory before requesting the backup.
 *
 * When the backup starts, zipgateway sends a signal.  The test case
 * can intercept this by providing its own PID in the comm file.  Then
 * the test case can validate the message in the communication file.
 *
 * After backup is completed, zipgateway sends a signal again.  The test case can then check the message.
 *
 * If we also want to unit test the actual backup, we could add
 * validation of the backup directory after completion.
 *
 * To test a "failed request" scenario, it can set up the comm file
 * incorrectly.
 *
 * It would also be possible to initate a backup by sending a sigusr1
 * signal to the test process, but no cases like that have been
 * implemented.
 */

/* *********************************** */
/* We don't want all the stubs from zipgateway_main_stubs, since we are
 * cutting out comm_file, so we need some stubs here. */
#include "ZIP_Router.h"

char** prog_argv;
int prog_argc;
process_event_t serial_line_event_message;
/* end of gw stubs */
/* *********************************** */


typedef void (*test_case_t)(void);

/* The test case that is running right now.
 * The test case init function is used to identify the case.
 */
test_case_t curr_case = NULL;

/* Hack to stop test_process. */
static bool test_process_stop_flag = false;

/* NVM file mock */
const char* linux_conf_nvm_file = "nvm.dat";


PROCINIT(&etimer_process);
PROCESS(test_process, "Unit Test Process");
PROCESS_NAME(test_process);

AUTOSTART_TESTPROCESSES(&test_process);

/* Hook into the ZIP_Router.c function, so that the test cases can
 * validate ZIP_Router state. */
extern bool zgw_component_idle(int comp);

/* Test process controls */
bool waiting_for_mdns = false;

/* The test case would like to know when the gateway lan is ready. */
bool waiting_for_lan_if = false;


/* end of helpers */

typedef void (*case_function_t)(void);

/* ****************
 * backup case helpers
 * **************** */
static const char *zgw_backup_communication_file = "/tmp/zgw.file";

static char zgw_manifest_file[] = "manifest";

static char test_bkup_inbox[PATH_MAX];


/* What the mock should put into the comm file. */
/* TODO: make the test case use its own PID and mock the behaviour of the backup script. */
//const char * start_text = "42 /tmp/ ";

/* Pid for the comm file */
int my_pid;

/* States of the backup test case (ie, NOT the gateway) */
enum backup_states {
   backup_idle,
   backup_comm_file_initialized,
   backup_request_sent, // request sent from test case (probably sits in contiki queue now)
   backup_request_recvd, // request recvd by ZIP Router
   backup_waiting, // Not sure test script can detect this
   backup_started,
   backup_completed,
   backup_expect_fail,
   backup_failed,
};

typedef enum backup_states backup_states_t;

char* backup_state_names[] = {
   "backup_idle",
   "backup_comm_file_initialized",
   "request_sent", // request sent from test case (probably sits in contiki queue now)
   "request_recvd", // request recvd by ZIP Router
   "waiting", // Not sure test script can detect this
   "backup_started",
   "completed",
   "expect_fail",
   "backup_failed",
   "FIXME"
};

#define backup_state_get_name(s) backup_state_names[s]

backup_states_t backup_state = backup_idle;

/* What did the test case signal handler read in the communication file. */
enum comm_file_replies {
   backup_msg_idle,
   backup_msg_comm_init, // The pid and path from script to zgw
   backup_msg_started,
   backup_msg_done,
   backup_msg_failed,
   comm_file_error
};

typedef enum comm_file_replies comm_file_msg_t;

/* In this unit test framework, the test program runs both the zgw and
 * a mock of the backup script.  So it sends signals to itself.
 *
 * The signal handler helper handles the signals sent as part of the
 * communication between the zgw and the backup script.  This way, the
 * test cases can validate that the gateway responds as expected to a
 * backup request. */

/** The last message read on the communication channel by the test
 * script.  Idle at the start of a case (the test case *writes*
 * backup_msg_comm_init, it does not read it).
 */
comm_file_msg_t last_read_comm_msg = backup_msg_idle;

/** The last message read on the communication channel by the test
 * script, if the last_read_comm_msg slot is busy.  (sometimes backup
 * sends two signals before returning to the test case).
 */
comm_file_msg_t comm_done_msg = backup_msg_idle;

/** Convert the string read on the communication channel to an enum.
 * (Happy cases only) */
comm_file_msg_t comm_msg_parse(int bytes_read) {
   comm_file_msg_t res = comm_file_error;

   zgw_log(zwlog_lvl_info, "reading comm msg %s\n", test_bkup_inbox);
   if ((bytes_read >= 11)
       && strncmp(test_bkup_inbox, "backup done", 11) == 0) {
      return backup_msg_done;
   } else if ((bytes_read >= 13)
              && strncmp(test_bkup_inbox, "backup failed", 13) == 0) {
      res = backup_msg_failed;
   } else if (bytes_read >= 14) {
      if (strncmp(test_bkup_inbox, "backup started", 14) == 0) {
         res = backup_msg_started;
      } else {
         /* lazy, but not important - we could check the correct pid and path */
         res = backup_msg_comm_init;
      }
   }
   return res;
}

#ifdef NO_SIGNALS
#else
/** Read a comm message from the comm file and parse it to return an enum. */
/* This helper only understands the legal messages in the comm file.
 * To test handling of incorrect messages, a separate helper is
 * needed. */
comm_file_msg_t comm_file_read(void) {
   comm_file_msg_t res = comm_file_error;
   int fd = open(zgw_backup_communication_file, O_RDONLY);
   if (fd < 0) {
      return res;
   }
   int bytes_read = read(fd, test_bkup_inbox, PATH_MAX);
   close(fd);

   res = comm_msg_parse(bytes_read);
   return res;
}
#endif

/* *********************************** */
/* Backup test-case setup helper functions. */
/* *********************************** */
int create_backup_dir() {
   int fd_dir = -1;

   fd_dir = open(TEST_BACKUP_DIR, O_DIRECTORY);
   if (fd_dir < 0) {
      zgw_log(zwlog_lvl_warning,
              "backup dir open failed: %s, trying to create it.\n", strerror(errno));
      if (mkdir(TEST_BACKUP_DIR, S_IRWXU) == 0) {
         zgw_log(zwlog_lvl_configuration, "Backup directory created\n");
         fd_dir = open(TEST_BACKUP_DIR, O_DIRECTORY);
      } else {
         zgw_log(zwlog_lvl_warning, "backup dir create also failed: %s\n", strerror(errno));
         return -1;
      }
   }
   return fd_dir;
}

/* Create the comm file and the manifest file.  If they don't exist, zgw backup fails. */
bool backup_files_setup_case1() {
   int fd, fd_dir;

   fd = open(zgw_backup_communication_file, O_RDWR |O_TRUNC|O_CREAT, S_IRWXU);
   if (fd < 0) {
      zgw_log(zwlog_lvl_warning, "open(%s) failed with %s\n",
              zgw_backup_communication_file, strerror(errno));
   }
   my_pid = getpid();
   dprintf(fd, "%d ", my_pid);
   dprintf(fd, TEST_BACKUP_DIR);
   dprintf(fd, " ");
   close(fd);

  /* The comm file has been reset, reset comm_msg and read in the comm_msg next time we get a signal. */
  last_read_comm_msg == backup_msg_idle;

  fd_dir = create_backup_dir();

  if (fd_dir < 0) {
     return false;
  }

  fd = openat(fd_dir, zgw_manifest_file, O_RDWR |O_TRUNC|O_CREAT, S_IRWXU);
  if (fd < 0) {
     zgw_log(zwlog_lvl_warning, "open(%s) at %s failed with %s",
             zgw_manifest_file,
             TEST_BACKUP_DIR,
             strerror(errno));
  }
  /* do stg, not important what */
  write(fd, " ", 1);
  close(fd);
  close(fd_dir);
  return true;
}


/* Set up the backup files to test missing pid scenario. */
bool backup_files_setup_missing_pid() {
   int fd, fd_dir;

   fd = open(zgw_backup_communication_file, O_RDWR |O_TRUNC|O_CREAT, S_IRWXU);
   if (fd < 0) {
      zgw_log(zwlog_lvl_warning, "open(%s) failed with %s",
              zgw_backup_communication_file, strerror(errno));
   }
   write(fd, "foo ", 4);
   my_pid = getpid();
   dprintf(fd, "%d ", my_pid);
   write(fd, TEST_BACKUP_DIR, strlen(TEST_BACKUP_DIR));
   write(fd, " ", 1);
   close(fd);

   fd_dir = create_backup_dir();
   if (fd_dir < 0) {
      return false;
   }

   fd = openat(fd_dir, zgw_manifest_file, O_RDWR |O_TRUNC|O_CREAT, S_IRWXU);
   if (fd < 0) {
      zgw_log(zwlog_lvl_warning, "open(%s) failed with %s",
              zgw_manifest_file, strerror(errno));
   }
   /* do stg, not important what */
   write(fd, " ", 1);
   close(fd);
   close(fd_dir);
   return true;
}


/* *********************************** */
/* Missing pid test case. */
/* *********************************** */

/* Case 2 Send a backup request with an error in the PID in the comm file.  Check
 * that backup is not initiated and that no signal is received. */
struct etimer case2_timer;

void case2_close();

void case2_init(void) {
   start_case("missing pid", NULL);

   bool res = backup_files_setup_missing_pid();
   check_true(res, "backup files set up without pid");

   backup_state = backup_request_sent; /* Next line mocks the sending */
   res = zgw_backup_init();
   check_true(res == 0, "Backup file initialization should complain\n");
   backup_state = backup_request_recvd;

#ifdef NO_SIGNALS
   /* If we are running NO_SIGNAL, we cannot test that ZGW does not
    * send the signal, since that part of the code is mocked.
    *
    * The mock "reads" the message into last_read_comm_msg, without getting a signal.
    *
    * So the case goes straight to validation at this point.
    */
#else
   /* If we are running with signals, a signal would have been sent by
    * now if the ZGW was buggy.
    *
    * So the case should check that it did not get a signal.  This is
    * done in case2_signal_handler().
    *
    * To terminate the case, check that the case has not read the comm
    * file yet, read the comm file, and check that the message in the
    * comm file is "failed". */
   check_true(last_read_comm_msg == backup_msg_idle, "Case has not read the comm file yet");
   last_read_comm_msg = comm_file_read();
#endif
   case2_close();
}

#ifdef NO_SIGNALS
/* The ZGW should only send a signal if PID is not 0, but the
 * unit test never sends signals when NO_SIGNALS is set. */
#else
void case2_signal_handler(comm_file_msg_t new_comm_msg) {
   last_read_comm_msg = new_comm_msg;
   test_print(0, "No signal should be sent when PID is not set correctly\n");
   numErrs++;
}
#endif

/* To be run when case2 timer times out */
void case2_close() {
   /* If there had been a signal in the interval, the signal handler
    * would have marked a fail in the case */
   /* Check that zgw_backup wrote fail */
   check_true(last_read_comm_msg == backup_msg_failed,
              "zipgateway sent fail to the comm file\n");
   /* ask zip_router if backup component is active. */
   check_true(zgw_component_idle(0x0400),
              "Backup component is idle\n");
   close_case("missing pid");
   /* Reset for next case */
   curr_case == NULL;
   last_read_comm_msg = backup_msg_idle;
   backup_state = backup_idle;
   process_post(&test_process, CASE_DONE, &case2_init);
}

/* Case specific interpretation of the message that the signal handler
 * found in the comm file (whether it is from gw or from mock backup
 * script).
 *
 * Case 2 tests the handling of missing PID in the comm file.  It
 * would be incorrect for the gateway to send a signal if there is no
 * valid PID.  That is not what is tested in this function.  */
void case2_handle_signal(comm_file_msg_t *msg) {
   /* case 2 expects a fail signal almost immediately. */
   check_true(msg == &last_read_comm_msg, "Expected signal received");
   check_true(backup_state == backup_comm_file_initialized
              && (last_read_comm_msg == backup_msg_failed),
              "Missing PID in comm file causes backup to fail\n");
}


/* *********************************** */
/* Delayed backup due to zgw boot test case */
/* *********************************** */
/* Case 1 Send a backup request to the zgw before it is booted, check
 * that backup only starts when zgw initialization is complete.
 *
 * The case uses three helpers, a "send the request" helper, a
 * "validate that backup does not happen immediately" helper, and a
 * "validate that backup starts when zgw is initialized" helper.
 *
 * The correct point to invoke each of the helpers must be found based
 * on what the zgw sends to the LAN and serial mocks, and by snooping
 * on contiki timers and processes.  The test can snoop on contiki
 * because it owns the main loop.
 *
 * */
void case1_close();

void case1_init(void);

void case1_validate_delay();

void case1_init(void) {
    start_case("case 1", NULL);

    bool res = backup_files_setup_case1();
    check_true(res, "backup files set up correctly");
    backup_state = backup_comm_file_initialized;
    res = zgw_backup_init();
    check_true(res == 1, "Backup file initialization succeeded\n");
    if (res) {
       backup_state = backup_request_sent;
       process_post(&zip_process, ZIP_EVENT_BACKUP_REQUEST, NULL);
       /* TODO: set a timestamp to make sure backup is not immediate. */
    } else {
       /* bail out of the test case here */
       case1_close();
    }
}

/* TODO: Call this function. */
void case1_validate_delay() {
   /* TODO: check boot state/idle state and validate. */
   check_true(1, "FIXME");
}

void case1_close() {
   /* TODO: validate the contents of the backup dir */
   process_post(&test_process, CASE_DONE, &case1_init);
   last_read_comm_msg = backup_msg_idle;
   backup_state = backup_idle;
   close_case("case 1");
   test_process_stop_flag = true; /* this is the last case */
}

static void case1_handle_signal_received(comm_file_msg_t *msg);

/* *********************************** */
/* Signal handler */
/* *********************************** */

#ifdef NO_SIGNALS
#else
/* Do the last msg/new msg juggling before calling the shared code. */
void case1_sigusr1_handler(comm_file_msg_t new_comm_msg) {
   if (last_read_comm_msg == backup_msg_started) {
      /* gw sends the two signals synchronously before and after
       * backup, so test case needs to store both. */
      comm_done_msg = new_comm_msg;
      case1_handle_signal_received(&comm_done_msg);
   } else if (last_read_comm_msg == backup_msg_idle) {
      last_read_comm_msg = new_comm_msg;
      case1_handle_signal_received(&last_read_comm_msg);
   } else {
      last_read_comm_msg = new_comm_msg;
      case1_handle_signal_received(&last_read_comm_msg);
   }
}

/* Signal handler for incoming sigusr1 */
void sigusr1_handler(int num) {
   comm_file_msg_t new_comm_msg;

   zgw_log(zwlog_lvl_warning,
      "Received SIGUSR1 in state %s, asking gateway to backup or sending status to backup script.\n",
      backup_state_get_name(backup_state));

   /* Check that we only get signals when the test case expects them. */
   check_true(last_read_comm_msg == backup_msg_idle
              || last_read_comm_msg == backup_msg_started,
              "ZGW sends signals in the expected order\n");

   new_comm_msg = comm_file_read();

   if (curr_case == &case2_init) {
      case2_signal_handler(new_comm_msg);
   } else if (curr_case == &case1_init) {
      case1_sigusr1_handler(new_comm_msg);
   } else {
      zgw_log(zwlog_lvl_error, "Unexpected signal\n");
      numErrs++;
   }
}
#endif

/* *********************************** */
/* Case 1 comm message validator */
/* *********************************** */
/* print out stuff and move through the backup stages.
 * This is the delayed handling of the signal-received event that is done via contiki.
 * TODO: validate stages and move case_close here */
void case1_handle_signal_received(comm_file_msg_t *msg) {
   zgw_log(zwlog_lvl_warning, "Handle signal in case 1, backup state %s\n",
           backup_state_get_name(backup_state));

   check_not_null(msg, "Legal signal received");
   if (msg == NULL) {
      process_post(&test_process, CASE_DONE, &case1_init);
      return;
   }

   if (msg == &comm_done_msg) {
      /* we expect msg to be backup done or backup failed.
       * We expect to have received a backup started already. */
      check_true((backup_state == backup_started)
                 && (*msg == backup_msg_done),
                 "Backup done message received in backup_started.");
      backup_state = backup_completed;
      comm_done_msg = backup_msg_idle;
   } else {

      /* Any other message: Check backup state and advance */
      switch (*msg) {
      case backup_msg_started:
         /* we only get this after done is also sent. */
         check_true((backup_state == backup_request_sent)
                    || (backup_state == backup_request_recvd)
                    || (backup_state == backup_waiting),
                    "Backup started in legal state.\n");
         if ((backup_state == backup_request_sent)
             || (backup_state == backup_request_recvd)
             || (backup_state == backup_waiting)) {
            zgw_log(zwlog_lvl_info, "ZGW said it started backup.\n");
            backup_state = backup_started;
         }
         break;
      case backup_msg_done:
         check_true(backup_state == backup_started,
            "Backup completed in legal state.\n");
         if (backup_state == backup_started) {
            zgw_log(zwlog_lvl_warning, "ZGW says backup is done now.\n");
            backup_state = backup_completed;
         }
         break;
      case backup_msg_failed:
         if (backup_state == backup_started || backup_state == backup_comm_file_initialized) {
            zgw_log(zwlog_lvl_warning, "ZGW says backup failed.\n");
         } else {
            zgw_log(zwlog_lvl_warning, "ZGW says backup is failed, but backup was not started correctly.\n");
         }
         backup_state = backup_failed;
         break;
      case backup_msg_comm_init: // The pid and path from script to zgw
         /* Not used in this test case.
          * We could call the function in zgw_backup.c to initialize the
          * backup at this point, if we want to support testing this way. */
         if (backup_state == backup_idle) {
            backup_state = backup_request_sent;
         }
         break;
      default:
         check_true(false, "Comm file message is legal");
         break;
      }
      /* last_read_comm_msg = backup_msg_idle; */
   }
   if (backup_state == backup_failed || backup_state == backup_completed) {
      /* terminal states */
      check_true((backup_state != backup_failed), "Backup should succeed\n");
      case1_close();
   }
   /* for the other states, we just continue. */
   /* TODO: set up a timer? */
}

void test_init() {
   process_start(&zip_process, 0);
   process_post(&test_process, INIT_DONE, NULL);
}

/* mDNS mock */
bool ip_packet_is_mdns(uint8_t *pkt) {
   /* FIXME */
   return true;
}

void mock_mdns(uint8_t *pkt) {
   /* Mostly, do not reply to mdns requests, just keep the test going while
    * waiting for mdns timeout. */
   /* TODO: make the mdns timer go off faster to speed up testing? */
   zgw_log(zwlog_lvl_warning, "wait for mdns timer to accepct name\n");
   waiting_for_mdns = true;
   lan_pkt_len = 0;
   return;
}

#ifdef NO_SIGNALS
/* NO_SIGNALS is needed because ctest cannot test code with signals.
 * So to test the signals, we can run the cases stand-alone.  To run
 * in ctest context, we need something that does not stop ctest.
 */

/** Pretend to write the status message (cmd) to the file.  Pretend to send the signal.
 * Pretend to read the message.
 * Do the same validation and test case execution as when the test script receives a signal.
 */
void zgw_backup_send_msg(const char* cmd) {
   zgw_log(zwlog_lvl_info, "Signal mock sends %s\n", cmd);
   /* Pretend to write the string to a file and then read it from the file into test_bkup_inbox. */
   /* copy string incl terminator */
   strncpy(test_bkup_inbox, cmd, PATH_MAX);

   /* Check that we are in a state where we would expect ZGW to send a message */
   if (last_read_comm_msg == backup_msg_idle) {
      /* This is the first msg sent from zipgateway after it received
       * a backup request. */
      check_true(last_read_comm_msg == backup_msg_idle,
                 "ZGW sent message to backup script at start\n");

      /* Pretend to read the message from the file */
      /* Parse the string and set last_read_comm_msg.  Ie, skip
       * writing it to the file and then reading it. */
      last_read_comm_msg = comm_msg_parse(strlen(cmd));

      if (curr_case == &case2_init) {
         /* Nothing to validate except the last msg */
         return;
      }
      case1_handle_signal_received(&last_read_comm_msg);
   } else if (last_read_comm_msg == backup_msg_started) {
      comm_done_msg =  comm_msg_parse(strlen(cmd));
      /* gw sends the two signals synchronously before and after backup */
      case1_handle_signal_received(&comm_done_msg);
   } else {
      case1_handle_signal_received(&last_read_comm_msg);
   }
   return;
}

int zgw_file_truncate(char *filename) {
  int fd;

  fd = open(filename, O_RDWR | O_TRUNC);
  if (fd < 0) {
     zgw_log(zwlog_lvl_error, "Opening file %s failed with %s (%d)\n",
             filename, strerror(errno), errno);
     return 0;
  } else {
     close(fd);
     return 1;
  }
}

int zgw_backup_initialize_comm(char *bkup_dir) {
  int fd;
  int bytes_read;
  char *ptr;

  fd = open(zgw_backup_communication_file, O_RDWR);
  if (fd < 0) {
     zgw_log(zwlog_lvl_error, "ERROR, backup comm file cannot be opened.\n");
     return 0;
  }
  bytes_read = read(fd, test_bkup_inbox, PATH_MAX);
  if (bytes_read == 0) {
     zgw_log(zwlog_lvl_error, "ERROR, backup comm file is empty.\n");
     return 0;
  }
  LOG_PRINTF("backup_pid and backup path: %s\n", test_bkup_inbox);
  pid_t backup_pid = strtoul(test_bkup_inbox, &ptr, 10);
  if (backup_pid <= 1) {
    zgw_log(zwlog_lvl_error, "ERROR, backup_pid is not sent\n");
    close(fd);
    return 0;
  }
  //strtoul() sets ptr to end of number in the string
  strncpy(bkup_dir,ptr+1, bytes_read - (ptr - test_bkup_inbox) - 2);
  close(fd);
  return 1;
}

#endif

struct etimer watchdog;

/* Main test process runner. */

PROCESS_THREAD(test_process, ev, data)
{
    PROCESS_BEGIN()

    etimer_set(&watchdog, 10*CLOCK_SECOND);
    /* TODO: check status of gw initialization in each iteration of
     * the loop and compare with the test cases expectations. */
    while(1)
    {
       if (ev == PROCESS_EVENT_INIT) {
          zgw_log(zwlog_lvl_warning, "processing init\n");
          test_init();
          zgw_log(zwlog_lvl_warning, "init complete\n");
       } else if (ev == INIT_DONE) {
          /* everyone else did their init */
           /* now wait for contiki timer to get tcpip ready event */
          zgw_log(zwlog_lvl_configuration, "Contiki init phase is completed, now wait for LAN IF.\n");
          waiting_for_lan_if = true;
       } else if (ev == tcpip_event) {
          if (data == (void*) TCP_READY) {
             /* Now we are able to send IPv6 packages */
             waiting_for_lan_if = false;
             zgw_log(zwlog_lvl_configuration, "Contiki tcp (LAN IF) is up now.\n");
             process_post(&test_process, RUN_CASE, case2_init);
          }
       } else if (ev == RUN_CASE) {
          case_function_t test_func = (case_function_t)data;
          zgw_log(zwlog_lvl_warning, "Starting new test case\n");
          curr_case = test_func;
          etimer_restart(&watchdog);
          test_func();
       } else if (ev == CASE_DONE) {
          case_function_t test_func = (case_function_t)data;
          if (test_func == case2_init) {
             process_post(&test_process, RUN_CASE, case1_init);
          } else {
             zgw_log(zwlog_lvl_configuration, "No more cases, terminating\n");
             process_post(PROCESS_BROADCAST, PROCESS_EVENT_EXIT, 0);
             break;
          }
       } else if (ev == INIT_LAN_PACKET_RECEIVED) {
          uint8_t *pkt = (uint8_t*)data;
          zgw_log(zwlog_lvl_control, "Handle LAN packet in test process.\n");
          if (lan_pkt_len < 34) {
             zgw_log(zwlog_lvl_debug, "TODO: implement parsing of this IP packet, len %zu\n", lan_pkt_len);
          } else if ((pkt[14] & 0xf0) == 0x60) {
             zgw_log(zwlog_lvl_debug, "Packet from ZGW is IPv6\n");
          } else {
             // we assume only one uip_buf is needed, let's see if that holds.
             if (ip_packet_is_dhcp(pkt)) {
                mock_dhcp_server(pkt);
             } else if (ip_packet_is_mdns(pkt)) {
                mock_mdns(pkt);
             } else {
                zgw_log(zwlog_lvl_error, "Unknown packet\n");
             }
          }
       } else if (ev == PROCESS_EVENT_TIMER) {
          if (data == &watchdog) {
             /* TODO: make this a per-case logic */
             check_true(test_process_stop_flag, "Test case can complete before the watchdog times out");
             zgw_log(zwlog_lvl_error, "Watchdog stops test\n");
             test_process_stop_flag = true;
             /* Flush contiki, then die. */
             process_post(&test_process, PROCESS_EVENT_EXIT, 0);
          }
       } else {
          if (waiting_for_mdns) {
             zgw_log(zwlog_lvl_configuration, "waiting for mdns probes\n");
          } else {
             zgw_log(zwlog_lvl_configuration, "test_process exit\n");
             process_post(PROCESS_BROADCAST, PROCESS_EVENT_EXIT, 0);
             break;
          }
       }
      PROCESS_WAIT_EVENT()
      ;
    }

  PROCESS_END()
}

/** This function should to copy the data in uip_buf and uip_len if we
 * actually want to use it for anything. */
void lan_mock_callback() {
   zgw_log(zwlog_lvl_info,
           "Test case received on LAN from ZGW: %d bytes\n", uip_len);
   process_post(&test_process, INIT_LAN_PACKET_RECEIVED, uip_buf);
}


/* zip router events
   0 - reset,
   1 - tunnel ready,
   2 - node_ipv4_assigned,
   3 - dhcp_timeout,
   4 - send done,
   5 - new network,
   6
   8 - bridge initialized,
   9 - all ipv4 assigned,
   10 - component done,
   11 - backup request
 */


bool zgwlog_init(void) {
   /* Start the logging system. */
   zgw_log_setup("zgw_test.log");   
   return true;
}

bool setup_signals(void) {
#ifdef NO_SIGNALS
#else
    /* Set up signal handlers if we test with real signals */
    signal(SIGUSR1, sigusr1_handler);
    signal(SIGRTMIN, sigusr1_handler);
#endif
    return true;
}

bool setup_lan_mock(void) {
    /* Connect the lan output to the test process. */
    mock_lan_set_tc_callback(lan_mock_callback);
    return true;
}


mock_contiki_init_step_t test_setup_functions[] = {
   {setup_signals,
    "Set up signal handlers"},
   {setup_lan_mock,
    "Connect the lan output to the test case"},
   {&setup_landev_output,
    "Connect mock tapdev_send to zwave_send"},
   {NULL, "Terminator"}
};
