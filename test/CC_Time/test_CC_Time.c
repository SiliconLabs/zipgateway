/* © 2018 Silicon Laboratories Inc.
 */

/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/
#include "command_handler.h"
#include "test_CC_Time.h"
#include "ZW_classcmd.h"
#include "test_helpers.h"
#include "test_CC_helpers.h"
#include <stdlib.h>

/****************************************************************************/
/*                      PRIVATE TYPES and DEFINITIONS                       */
/****************************************************************************/

/* Needed by time() mock */
//
//a fix to bild the gateway on mac os
#ifdef __time_t
typedef __time_t time_t;
#endif
#ifndef __time_t
typedef long int time_t;
#endif

/* Needed by localtime() mock */
typedef struct tm {
   int tm_sec;         /* seconds,  range 0 to 59          */
   int tm_min;         /* minutes, range 0 to 59           */
   int tm_hour;        /* hours, range 0 to 23             */
   int tm_mday;        /* day of the month, range 1 to 31  */
   int tm_mon;         /* month, range 0 to 11             */
   int tm_year;        /* The number of years since 1900   */
   int tm_wday;        /* day of the week, range 0 to 6    */
   int tm_yday;        /* day in the year, range 0 to 365  */
   int tm_isdst;       /* daylight saving time             */
}tm;

/****************************************************************************/
/*                              EXPORTED DATA                               */
/****************************************************************************/

/****************************************************************************/
/*                              PRIVATE DATA                                */
/****************************************************************************/

// Date time data that will be returned by the mocked functions
#define TEST_YEAR   1989
#define TEST_MONTH  4
#define TEST_DAY    27
#define TEST_HOUR   10
#define TEST_MIN    20
#define TEST_SEC    30

/* Time tm information struct with with a dummy date and time for this test.
 */
struct tm test_time_info = {
    .tm_year    = TEST_YEAR - 1900,
    .tm_mon     = TEST_MONTH - 1,
    .tm_mday    = TEST_DAY,
    .tm_hour    = TEST_HOUR,
    .tm_min     = TEST_MIN,
    .tm_sec     = TEST_SEC,
    .tm_isdst = -1
};


/****************************************************************************/
/*                               MOCK FUNCTIONS                             */
/****************************************************************************/

time_t time(time_t *t){
    test_print(4, "--> time() called. (MOCK)\n");
    return 0;
}

struct tm * localtime(time_t *t){
    test_print(4, "--> localtime() called. (MOCK)\n");
    return &test_time_info;
}



/****************************************************************************/
/*                              TEST HELPERS                                */
/****************************************************************************/

/****************************************************************************/
/*                              TEST CASES                                  */
/****************************************************************************/


/**
 * Tests what hapens when receiving a Date Get Command (Time Command Class)
 */
void test_TimeHandler_DateGet(void)
{
  const char *tc_name = "Date Get Command";
  start_case(tc_name, 0);

  /*----------------------------------------------------------*/
  test_print(3, "# Send a Date Get Command\n");

  uint8_t cmd_frame_date_command[] = {
      COMMAND_CLASS_TIME,
      DATE_GET
  };

   uint8_t exp_frame_date_command[] = {
      COMMAND_CLASS_TIME,
      DATE_REPORT,
      (TEST_YEAR>>8), //MSB
      TEST_YEAR & 0xFF, //LSB
      TEST_MONTH,
      TEST_DAY
  };


  check_cmd_handler_results(TimeHandler,
                            cmd_frame_date_command, sizeof(cmd_frame_date_command),
                            exp_frame_date_command, sizeof(exp_frame_date_command),
                            COMMAND_HANDLED);

  close_case(tc_name);
}


/**
 * Tests what hapens when receiving a Time Get Command (Time Command Class)
 */
void test_TimeHandler_TimeGet(void)
{
  const char *tc_name = "Time Get Command";
  start_case(tc_name, 0);

  /*----------------------------------------------------------*/
  test_print(3, "# Send a Time Get Command\n");

  uint8_t cmd_frame_time_command[] = {
      COMMAND_CLASS_TIME,
      TIME_GET
  };

   uint8_t exp_frame_time_command[] = {
      COMMAND_CLASS_TIME,
      TIME_REPORT,
      TEST_HOUR,
      TEST_MIN,
      TEST_SEC
  };


  check_cmd_handler_results(TimeHandler,
                            cmd_frame_time_command, sizeof(cmd_frame_time_command),
                            exp_frame_time_command, sizeof(exp_frame_time_command),
                            COMMAND_HANDLED);

  close_case(tc_name);
}


/**
 * Tests what hapens when receiving an unknown command within Time Command Class
 */
void test_TimeHandler_UnknownCommand(void)
{
  const char *tc_name = "Time Unknown Command (0xFF)";
  start_case(tc_name, 0);

  /*----------------------------------------------------------*/

  test_print(3, "# Send an unknown command\n");

  uint8_t cmd_frame_unknown[] = {
      COMMAND_CLASS_TIME,
      0xFF                           // Unknown Command
  };

  check_cmd_handler_results(TimeHandler,
                            cmd_frame_unknown, sizeof(cmd_frame_unknown),
                            0, 0,
                            COMMAND_NOT_SUPPORTED);

  close_case(tc_name);
}

/**
 * Tests what hapens when receiving a frame too short (no command) with Time Command Class
 */
void test_TimeHandler_NoCommand(void)
{
  const char *tc_name = "Time no Command included in the frame";
  start_case(tc_name, 0);

  /*----------------------------------------------------------*/

  test_print(3, "# Send a frame without a command field included\n");

  uint8_t cmd_frame_without_command[] = {
      COMMAND_CLASS_TIME
  };

  check_cmd_handler_results(TimeHandler,
                            cmd_frame_without_command, sizeof(cmd_frame_without_command),
                            0, 0,
                            COMMAND_NOT_SUPPORTED);

  close_case(tc_name);
}



/**
 * TimeHandler test begins here
 */
int main()
{
  test_TimeHandler_DateGet();
  test_TimeHandler_TimeGet();
  test_TimeHandler_UnknownCommand();
  test_TimeHandler_NoCommand();

  close_run();
  return numErrs;
}
