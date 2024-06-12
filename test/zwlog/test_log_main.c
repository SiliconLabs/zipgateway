#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

#include "zgw_log.h"
#include "test_log.h"
#include "test_log_int.h"
//#include "feeder.h"

/**********************
 * Local declarations *
 **********************/

void test_log_features_2(void);
void test_log_file_trim(int num_loops);

/*********************
 * Local definitions *
 *********************/

zgw_log_id_define(feat_1);
zgw_log_id_define(feat_2);
zgw_log_id_default_set(feat_1);

/** which helper function to call - default print levels */
int casenum = 0;
int repl = 1;

char *log_filename = NULL;

#if !defined(ZGW_LOG)
/* use a conflicting type to trigger early error */
int32_t feat_1 = -1;
#endif

/** \ingroup logtesthelpers
 *
 * Print to two feature groups, the default feature and to another feature
 * from the same function at all levels.
 *
 * In this file feat_1 is default feature group.
 * Log function enter.
 * Log to default logid at all levels.
 * Log to feat_2 at all levels.
 * Log function exit.
 */
void test_log_features_2(void) {
  zgw_log_enter();

  /* Actual test */
  zgw_log(0, "Feat 1, level %d\n", 0);
  zgw_log_to(feat_2, 0, "Feat 2, level %d\n", 0);
  zgw_log(1, "Feat 1, level %d\n", 1);
  zgw_log_to(feat_2, 1, "Feat 2, level %d\n", 1);
  zgw_log(2, "Feat 1, level %d\n", 2);
  zgw_log_to(feat_2, 2, "Feat 2, level %d\n", 2);
  zgw_log(3, "Feat 1, level %d\n", 3);
  zgw_log_to(feat_2, 3, "Feat 2, level %d\n", 3);
  zgw_log(4, "Feat 1, level %d\n", 4);
  zgw_log_to(feat_2, 4, "Feat 2, level %d\n", 4);
  zgw_log(5, "Feat 1, level %d\n", 5);
  zgw_log_to(feat_2, 5, "Feat 2, level %d\n", 5);
  zgw_log(6, "Feat 1, level %d\n", 6);
  zgw_log_to(feat_2, 6, "Feat 2, level %d\n", 6);
  zgw_log(7, "Feat 1, level %d\n", 7);
  zgw_log_to(feat_2, 7, "Feat 2, level %d\n", 7);
  zgw_log(8, "Feat 1, level %d\n", 8);
  zgw_log_to(feat_2, 8, "Feat 2, level %d\n", 8);
  zgw_log(9, "Feat 1, level %d\n", 9);
  zgw_log_to(feat_2, 9, "Feat 2, level %d\n", 9);

  zgw_log_exit();
}

void test_log_file_trim(int num_loops) {
  for (int ii = 0; ii<num_loops; ii++) {
    zgw_log(1, "Logging a lot of characters to fill the log file.\n");
  }
}

/************************
 * External definitions *
 ************************/

/* options: -f <filename> -u <unitcase-num> -r
 */
int test_log_parse_opts(int argc, char *argv[]) {
  int opt;
  const char *optstr = "f:u:r";

  while ((opt = getopt(argc, argv, optstr)) != -1) {
#ifdef ZGW_LOG
    if (opt == 'f') {
#ifdef ZGW_LOG_LOG_TO_FILE
      log_filename = optarg;
#else
      printf("Test Helper: Log to file not supported, ignoring '-f %s' and logging to stdout\n", 
	     optarg);
#endif
    } else 
#endif
    if (opt == 'u') {
      casenum = atoi(optarg);
    } else if (opt == 'r') {
        printf("running loop\n");
      repl = 1;
    } else {
      fprintf(stderr, "Test script error in options.\n");
    }
  }
  return 0;
}

int main(int argc, char* argv[]) {
  printf("Test Helper: Starting test interface\n");
  int res = 0;
  
  test_log_parse_opts(argc, argv);

  zgw_log_setup(log_filename);
  zgw_log(0, "Setup done\n");

  printf("Test Helper: Running case %d\n", 
	 casenum);
  switch (casenum) {
  case 1:
    test_log_features_2();
    break;
  case 2:
    test_log_set_levels();
    break;
  case 3:
    test_log_file_trim(1000);
    break;
  case 4:
    test_log_trimming_very_long_name();
  default:
    {
      printf("Test Helper: No one-shot test requested.\n");
      /* if (repl) { */
	/* feeder_repl(); */
      /* } */
    }
  }
  zgw_log_teardown();

  return 0;
}
