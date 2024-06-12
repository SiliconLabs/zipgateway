/* © 2019 Silicon Laboratories Inc. */

/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/
#include "command_handler.h"
#include "ZIP_Router.h"
#include "CC_Indicator.h"
#include "test_helpers.h"
#include "test_CC_helpers.h"
#include <stdlib.h>
#include <signal.h>
/****************************************************************************/
/*                      PRIVATE TYPES and DEFINITIONS                       */
/****************************************************************************/

/* Needed by signal() mock */

typedef void (*sighandler_t)(int);

//#define SIG_DFL ((sighandler_t) 0)

/****************************************************************************/
/*                              EXPORTED DATA                               */
/****************************************************************************/

/****************************************************************************/
/*                              PRIVATE DATA                                */
/****************************************************************************/

/* Capture parameter passed to execve() */
#define MAX_NUM_EXECVE_ARGS (4)
char execve_args[MAX_NUM_EXECVE_ARGS][300] = {0};

/* Router configuration struct with the bare essentials for this test.
 * (using a dummy script name - it will not be called)
 */
struct router_config cfg = {
    .node_identify_script = "/dangerous/path/ni_blink.sh unwanted arguments"
};

/* The expected sanitized script name */
#define SANITIZED_SCRIPT_NAME (INSTALL_SYSCONFDIR "/ni_blink.sh")


/****************************************************************************/
/*                               MOCK FUNCTIONS                             */
/****************************************************************************/

sighandler_t signal(int sig, sighandler_t handler)
{
  test_print(4, "--> signal() called. (MOCK)\n");
  return SIG_DFL;
}

pid_t fork(void)
{
  test_print(4, "--> fork() called. (MOCK)\n");
  return 0;
}

int execve(const char *path, char *const argv[], char *const envp[])
{
  test_print(4, "--> execve() called. (MOCK)\n");

  for (int i = 0; i < MAX_NUM_EXECVE_ARGS; i++)
  {
    strncpy(execve_args[i], (argv[i] != 0) ? argv[i] : "", sizeof(execve_args[i]));

    /* If there's a terminating NULL then the buffer was large enough */
    ASSERT(execve_args[i][sizeof(execve_args[i]) - 1] == 0);

    /* Stop after last arg (signified by zero) */
    if (argv[i] == 0)
    {
      break;
    }
  }
  return 0;
}

BOOL timer_running = 0;

void ctimer_set(struct ctimer *c, clock_time_t t, void (*f)(void *), void *ptr)
{
  test_print(4, "--> ctimer_set() called. (MOCK)\n");
  timer_running = TRUE;
}

void ctimer_stop(struct ctimer *c)
{
  test_print(4, "--> ctimer_stop() called. (MOCK)\n");
  timer_running = FALSE;
}

BYTE nodeOfIP(const uip_ip6addr_t* ip)
{
  test_print(4, "--> nodeOfIP() called. (MOCK)\n");

  /* Report that the IP address does not belong to a Z-Wave node in
   * order to have IsZwaveMulticast() always report that the frame
   * was not received via Z-Wave multicast.
   * */
  return 0;
}


/****************************************************************************/
/*                              TEST HELPERS                                */
/****************************************************************************/

/**
 * Initialize the indicator handler before each test
 */
static void init_test()
{
  IndicatorHandler_Init();
}

/**
 * Initialize buffers used for capturing blink script arguments
 */
static void init_check_blink_script_args(void)
{
  test_print(4, "init_check_blink_script_args()\n");
  memset(execve_args, 0, sizeof(execve_args));
}

/**
 * Check the arguments that were passed to the external blink script
 */
static void check_blink_script_args(unsigned int exp_on_time_ms,
                                    unsigned int exp_off_time_ms,
                                    unsigned int exp_num_cycles)
{
  unsigned int arg = 0;

  check_true(strcmp(execve_args[0], SANITIZED_SCRIPT_NAME) == 0, "Sanitized script name for execve");

  check_true(execve_args[1] != 0
             && sscanf(execve_args[1], "%u", &arg) == 1
             && arg == exp_on_time_ms,
             "on_time_ms argument for execve");
  test_print(4, "on_time_ms: expected %u got %u\n", exp_on_time_ms, arg);

  check_true(execve_args[2] != 0
             && sscanf(execve_args[2], "%u", &arg) == 1
             && arg == exp_off_time_ms,
             "off_time_ms argument for execve");
  test_print(4, "on_time_ms: expected %u got %u\n", exp_off_time_ms, arg);

  check_true(execve_args[3] != 0
             && sscanf(execve_args[3], "%u", &arg) == 1
             && arg == exp_num_cycles,
             "num_cycles argument for execve");
  test_print(4, "on_time_ms: expected %u got %u\n", exp_num_cycles, arg);
}


/****************************************************************************/
/*                              TEST CASES                                  */
/****************************************************************************/

/**
 * Test the version one set/get commands
 */
void test_IndicatorHandler_Set_Get_V1(void)
{
  const char *tc_name = "Indicator Set/Get (Ver 1)";
  start_case(tc_name, 0);

  init_test();

  /*----------------------------------------------------------*/

  test_print(3, "# Turn on the indicator\n");

  init_check_blink_script_args();

  uint8_t cmd_frame_on[] = {
      COMMAND_CLASS_INDICATOR_V3,
      INDICATOR_SET_V3,
      0xFF                            // Indicator0 value
  };

  check_cmd_handler_results(IndicatorHandler,
                            cmd_frame_on, sizeof(cmd_frame_on),
                            0, 0,
                            COMMAND_HANDLED);

  check_blink_script_args(25500, 0, 0);

  /*----------------------------------------------------------*/

  test_print(3, "# Get status of the indicator\n");

  uint8_t cmd_frame_get[] = {
      COMMAND_CLASS_INDICATOR_V3,
      INDICATOR_GET_V3
  };

  uint8_t exp_frame_get_on[] = {
      COMMAND_CLASS_INDICATOR_V3,
      INDICATOR_REPORT,
      0xFF               // Indicator0 value
  };

  check_cmd_handler_results(IndicatorHandler,
                            cmd_frame_get, sizeof(cmd_frame_get),
                            exp_frame_get_on, sizeof(exp_frame_get_on),
                            COMMAND_HANDLED);

  /*----------------------------------------------------------*/

  test_print(3, "# Turn off the indicator\n");

  init_check_blink_script_args();

  uint8_t cmd_frame_off[] = {
      COMMAND_CLASS_INDICATOR_V3,
      INDICATOR_SET_V3,
      0x00                  // Indicator0 value
  };

  check_cmd_handler_results(IndicatorHandler,
                            cmd_frame_off, sizeof(cmd_frame_off),
                            0, 0,
                            COMMAND_HANDLED);

  check_blink_script_args(0, 0, 0);

  /*----------------------------------------------------------*/

  test_print(3, "# Get status of the indicator\n");

  uint8_t exp_frame_get_off[] = {
      COMMAND_CLASS_INDICATOR_V3,
      INDICATOR_REPORT,
      0x00                      // Indicator0 value
  };

  check_cmd_handler_results(IndicatorHandler,
                            cmd_frame_get, sizeof(cmd_frame_get),
                            exp_frame_get_off, sizeof(exp_frame_get_off),
                            COMMAND_HANDLED);

  close_case(tc_name);
}


/**
 * Test the version three set/get commands
 */
void test_IndicatorHandler_Set_Get_V3(void)
{
  const char *tc_name = "Indicator Set/Get (Ver 3)";
  start_case(tc_name, 0);

  init_test();

  /*----------------------------------------------------------*/

  test_print(3, "# Blink the indicator\n");

  init_check_blink_script_args();

  uint8_t cmd_frame_blink[] = {
      COMMAND_CLASS_INDICATOR_V3,
      INDICATOR_SET_V3,
      0x00,                            // Indicator0 value (ignore)
      0x03,                            // Reserved:3 ObjectCount:5
      INDICATOR_IND_NODE_IDENTIFY,
      INDICATOR_PROP_ON_OFF_PERIOD,
      0x0A,
      INDICATOR_IND_NODE_IDENTIFY,
      INDICATOR_PROP_ON_OFF_CYCLES,
      0x02,
      INDICATOR_IND_NODE_IDENTIFY,
      INDICATOR_PROP_ON_TIME,
      0x00
  };

  check_cmd_handler_results(IndicatorHandler,
                            cmd_frame_blink, sizeof(cmd_frame_blink),
                            0, 0,
                            COMMAND_HANDLED);

  check_blink_script_args(500, 500, 2);

  /*----------------------------------------------------------*/

  test_print(3, "# Get status of the indicator\n");

  uint8_t cmd_frame_get[] = {
      COMMAND_CLASS_INDICATOR_V3,
      INDICATOR_GET_V3,
      INDICATOR_IND_NODE_IDENTIFY
  };

  uint8_t exp_frame_get_blink[] = {
      COMMAND_CLASS_INDICATOR_V3,
      INDICATOR_REPORT,
      0xFF,                          // Indicator0 value
      0x03,                          // Reserved:3 ObjectCount:5
      INDICATOR_IND_NODE_IDENTIFY,   // Indicator ID
      INDICATOR_PROP_ON_OFF_PERIOD,
      0x0A,
      INDICATOR_IND_NODE_IDENTIFY,   // Indicator ID
      INDICATOR_PROP_ON_OFF_CYCLES,
      0x02,
      INDICATOR_IND_NODE_IDENTIFY,   // Indicator ID
      INDICATOR_PROP_ON_TIME,
      0x00
  };

  check_cmd_handler_results(IndicatorHandler,
                            cmd_frame_get, sizeof(cmd_frame_get),
                            exp_frame_get_blink, sizeof(exp_frame_get_blink),
                            COMMAND_HANDLED);

  /*----------------------------------------------------------*/

  test_print(3, "# Send unknown command\n");

  uint8_t cmd_frame_err0[] = {
      COMMAND_CLASS_INDICATOR_V3,
      0xEE,                             // Unknown command
      INDICATOR_IND_NODE_IDENTIFY
  };

  check_cmd_handler_results(IndicatorHandler,
                            cmd_frame_err0, sizeof(cmd_frame_err0),
                            0, 0,
                            COMMAND_NOT_SUPPORTED);

  /*----------------------------------------------------------*/

  test_print(3, "# Send unknown indicator ID\n");

  uint8_t cmd_frame_err1[] = {
      COMMAND_CLASS_INDICATOR_V3,
      INDICATOR_GET_V3,
      0x01                      // Unknown indicator ID
  };

  uint8_t exp_frame_err1[] = {
      COMMAND_CLASS_INDICATOR_V3,
      INDICATOR_REPORT,
      0xFF,                     // Indicator0 value
      0x01,                     // Reserved:3 ObjectCount:5
      INDICATOR_REPORT_NA_V3,   // Dummy indicator ID
      0x00,
      0x00
  };

  check_cmd_handler_results(IndicatorHandler,
                            cmd_frame_err1, sizeof(cmd_frame_err1),
                            exp_frame_err1, sizeof(exp_frame_err1),
                            COMMAND_HANDLED);

  /*----------------------------------------------------------*/

  test_print(3, "# Send truncated frame\n");

  uint8_t cmd_frame_err2[] = {
      COMMAND_CLASS_INDICATOR_V3,
      INDICATOR_SET_V3,
      0x00,
      0x03,                          // Reserved:3 ObjectCount:5
      INDICATOR_IND_NODE_IDENTIFY,
      INDICATOR_PROP_ON_OFF_PERIOD
      // Truncated frame!!
  };

  check_cmd_handler_results(IndicatorHandler,
                            cmd_frame_err2, sizeof(cmd_frame_err2),
                            0, 0,
                            COMMAND_PARSE_ERROR);

  close_case(tc_name);
}


/**
 * Test the version three Supported Get command
 */
void test_IndicatorHandler_SupportedGet_V3(void)
{
  const char *tc_name = "Indicator Supported Get";
  start_case(tc_name, 0);

  init_test();

  /*----------------------------------------------------------*/

  test_print(3, "# Discover the available indicators\n");

  uint8_t cmd_frame_00[] = {
      COMMAND_CLASS_INDICATOR_V3,
      INDICATOR_SUPPORTED_GET_V3,
      0x00                           // Indicator ID  0x00 = discovery
  };

  uint8_t exp_frame_00[] = {
      COMMAND_CLASS_INDICATOR_V3,
      INDICATOR_SUPPORTED_REPORT_V3,
      INDICATOR_IND_NODE_IDENTIFY,           // Indicator ID
      0x00,                                  // Next Indicator ID
      0x01,                                  // Reserved:3 BitmaskLen:5
      (1 << INDICATOR_PROP_ON_OFF_PERIOD) |  // Bitmask
      (1 << INDICATOR_PROP_ON_OFF_CYCLES) |
      (1 << INDICATOR_PROP_ON_TIME)
  };

  check_cmd_handler_results(IndicatorHandler,
                            cmd_frame_00, sizeof(cmd_frame_00),
                            exp_frame_00, sizeof(exp_frame_00),
                            COMMAND_HANDLED);

  /*----------------------------------------------------------*/

  test_print(3, "# Query the 'Node Identify' indicator\n");

  uint8_t cmd_frame_51[] = {
      COMMAND_CLASS_INDICATOR_V3,
      INDICATOR_SUPPORTED_GET_V3,
      INDICATOR_IND_NODE_IDENTIFY            // Indicator ID
  };

  uint8_t exp_frame_51[] = {
      COMMAND_CLASS_INDICATOR_V3,
      INDICATOR_SUPPORTED_REPORT_V3,
      INDICATOR_IND_NODE_IDENTIFY,           // Indicator ID
      0x00,                                  // Next Indicator ID
      0x01,                                  // Reserved:3 BitmaskLen:5
      (1 << INDICATOR_PROP_ON_OFF_PERIOD) |  // Bitmask
      (1 << INDICATOR_PROP_ON_OFF_CYCLES) |
      (1 << INDICATOR_PROP_ON_TIME)
  };

  check_cmd_handler_results(IndicatorHandler,
                            cmd_frame_51, sizeof(cmd_frame_51),
                            exp_frame_51, sizeof(exp_frame_51),
                            COMMAND_HANDLED);

  /*----------------------------------------------------------*/

  test_print(3, "# Query unknown indicator\n");

  uint8_t cmd_frame_nn[] = {
      COMMAND_CLASS_INDICATOR_V3,
      INDICATOR_SUPPORTED_GET_V3,
      0x01             // Indicator ID (unknown)
  };

  uint8_t exp_frame_nn[] = {
      COMMAND_CLASS_INDICATOR_V3,
      INDICATOR_SUPPORTED_REPORT_V3,
      0x00,           // Indicator ID (invalid)
      0x00,           // Next Indicator ID
      0x00            // Reserved:3 BitmaskLen:5
  };

  check_cmd_handler_results(IndicatorHandler,
                            cmd_frame_nn, sizeof(cmd_frame_nn),
                            exp_frame_nn, sizeof(exp_frame_nn),
                            COMMAND_HANDLED);

  close_case(tc_name);
}


/** Indicator Default Set */
void test_IndicatorDefaultSet(void) {
   char *name = "Indicator Default Set";
   uint8_t cmd_frame_get[] = {COMMAND_CLASS_INDICATOR_V3,
                              INDICATOR_GET_V3,
                              INDICATOR_SET_NODE_IDENTIFY_V3};
   uint8_t cmd_frame_blink[] = {COMMAND_CLASS_INDICATOR_V3,
                                INDICATOR_SET_V3, 0x00, 0x03,
                                INDICATOR_SET_NODE_IDENTIFY_V3,
                                INDICATOR_PROP_ON_OFF_PERIOD, 0x0A,
                                INDICATOR_SET_NODE_IDENTIFY_V3,
                                INDICATOR_PROP_ON_OFF_CYCLES, 0x02,
                                INDICATOR_SET_NODE_IDENTIFY_V3,
                                INDICATOR_PROP_ON_TIME, 0x00};
   uint8_t exp_frame_get_blink[] = {COMMAND_CLASS_INDICATOR_V3,
                                    INDICATOR_REPORT, 0xFF, 0x03,
                                    INDICATOR_SET_NODE_IDENTIFY_V3,
                                    INDICATOR_SET_ON_OFF_PERIOD_V3, 0x0A,
                                    INDICATOR_SET_NODE_IDENTIFY_V3,
                                    INDICATOR_SET_ON_OFF_CYCLES_V3, 0x02,
                                    INDICATOR_SET_NODE_IDENTIFY_V3,
                                    INDICATOR_SET_ON_TIME_V3, 0x00};

   start_case(name, NULL);
   IndicatorHandler_Init();
   init_check_blink_script_args();
   check_cmd_handler_results(IndicatorHandler,
                             cmd_frame_blink, sizeof(cmd_frame_blink),
                             0, 0, COMMAND_HANDLED);
   check_blink_script_args(500, 500, 2);
   check_true(timer_running, "Timer is running");

   check_cmd_handler_results(IndicatorHandler,
                             cmd_frame_get, sizeof(cmd_frame_get),
                             exp_frame_get_blink, sizeof(exp_frame_get_blink),
                             COMMAND_HANDLED);

   IndicatorDefaultSet();
   check_blink_script_args(0,0,0);
   check_true(!timer_running, "Timer is stopped");

   exp_frame_get_blink[2] = 0;
   exp_frame_get_blink[6] = 0;
   exp_frame_get_blink[9] = 0;
   exp_frame_get_blink[12] = 0;

   check_cmd_handler_results(IndicatorHandler,
                             cmd_frame_get, sizeof(cmd_frame_get),
                             exp_frame_get_blink, sizeof(exp_frame_get_blink),
                             COMMAND_HANDLED);

   close_case(name);
}


/**
 * IndicatorHandler command class test begins here
 */
int main()
{
  test_IndicatorHandler_Set_Get_V1();
  test_IndicatorHandler_Set_Get_V3();
  test_IndicatorHandler_SupportedGet_V3();
  test_IndicatorDefaultSet();

  close_run();
  return numErrs;
}
