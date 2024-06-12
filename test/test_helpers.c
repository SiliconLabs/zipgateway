/* © 2017 Silicon Laboratories Inc.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <lib/zgw_log.h>
#include "test_helpers.h"
#include <errno.h>

zgw_log_id_define(TC);
zgw_log_id_default_set(TC);


int numErrs = 0;
int numCases = 0;
int numCasesFail = 0;

int verbosity = test_number_of_levels;

const char* currCase = NULL;
int numErrsStartCase = 0;
FILE *caseLogFile = NULL;

static void test_print_int(int lvl, char *fmt, ...);
static const char *path_trim(const char *name);

/**
 * This version is for Linux with '/' path delimiter.
 *
 */
static const char *path_trim(const char *name) {
  const char *res = strrchr(name, '/');

  /* Remove the path */
  if (res) {
    /* Skip the '/' */
    res++;
  } else {
    /* There was no path in the name, so just use as is */
    res = name;
  }
  return res;
}

// TODO add paths
FILE *test_create_log(const char *logname) {
    FILE *strm;

    if (logname == NULL) {
        printf("Test error: please provide a log file name.\n");
        numErrs++;
    }
    logname = path_trim(logname);
    printf("Logging to: %s\n", logname);
    strm = fopen(logname, "w");
    if (strm == NULL) {
        numErrs++;
        printf("Test error %s (%d)\n", strerror(errno), errno);
    }
    return strm;
}

void start_case_setup(const char *str) {
    test_print_int(2, "\n--- CASE %s ---\n", str);
    numCases++;
    currCase = str;
    numErrsStartCase = numErrs;
}

#ifndef INLINE_TEST_HELPERS
void start_case(const char *str, FILE *logfile)
{
    start_case_setup(str);
    caseLogFile = logfile;
    if (logfile) {
        fprintf(logfile, "\n--- CASE %s ---\n", str);
    }
    zgw_log(2, "--- CASE %s ---\n", str);
}
#endif

/* If caseName is not the current case, start a new case and
 * incorporate the unfinished previous case into it. */
int check_case(const char *str) {
    if (strcmp(str, currCase) == 0) {
       return 1;
    } else {
        test_print_int(2, "\n--- CASE %s ---\n", str);
        if (caseLogFile) {
            fprintf(caseLogFile, "\n--- CASE %s ---\n", str);
        }
        /* This is not supposed to happen.  We don't know when the
         * previous case was supposed to have ended and this one
         * started, so just attribute everything from everyone. */
        numErrsStartCase = 0;
       return 0;
    }
}

int close_case_errs_cnt(const char *str) {
    int caseErrs = numErrs - numErrsStartCase;
    if (caseErrs == 0) {
        test_print_int(2, "    CASE PASSED\n--- END %s ---\n", str);
        if (caseLogFile) {
            fprintf(caseLogFile,  "CASE PASSED\n--- END %s ---\n", str);
        }
    } else {
        numCasesFail++;
        test_print_int(2, "    CASE FAILED with %d errors\n--- END %s ---\n",
                   caseErrs, str);
        if (caseLogFile) {
            fprintf(caseLogFile, "CASE FAILED with %d errors\n--- END %s ---\n",
                    caseErrs, str);
        }
    }
    return caseErrs;
}

#ifndef INLINE_TEST_HELPERS
void close_case(const char *str) {
   if (!check_case(str)) {
      zgw_log(2, "--- CASE %s ---\n", str);
   }
   if (close_case_errs_cnt(str) == 0) {
      zgw_log(2,  "CASE PASSED\n--- END %s ---\n", str);
   } else {
      zgw_log(2, "CASE FAILED with %d errors\n--- END %s ---\n",
              caseErrs, str);
   }
}
#else
#ifdef ZGW_LOG
void close_case_inl(const char *str, int zwlog_feat_val, const char* id_name,
                    const char *fileStr, int line, const char *funcStr) {
    int caseErrs = 0;
    if (strcmp(str, currCase) == 0) {
        caseErrs = numErrs - numErrsStartCase;
    } else {
        test_print_int(2, "\n--- CASE %s ---\n", str);
        if (caseLogFile) {
            fprintf(caseLogFile, "\n--- CASE %s ---\n", str);
        }
        zgw_log_to_int(2, zwlog_feat_val, id_name, fileStr, line, funcStr,
                       "--- CASE %s ---\n", str);
        caseErrs = numErrs;
    }
    if (caseErrs == 0) {
        test_print_int(2, "    CASE PASSED\n--- END %s ---\n", str);
        if (caseLogFile) {
            fprintf(caseLogFile,  "CASE PASSED\n--- END %s ---\n", str);
        }
        zgw_log_to_int(2, zwlog_feat_val, id_name, fileStr, line, funcStr,
                       "CASE PASSED\n");
        zgw_log_to_int(2, zwlog_feat_val, id_name, fileStr, line, funcStr,
                       "--- END %s ---\n", str);
    } else {
        numCasesFail++;
        test_print_int(2, "    CASE FAILED with %d errors\n--- END %s ---\n",
                   caseErrs, str);
        if (caseLogFile) {
            fprintf(caseLogFile, "CASE FAILED with %d errors\n--- END %s ---\n",
                    caseErrs, str);
        }
        zgw_log_to_int(2, zwlog_feat_val, id_name, fileStr, line, funcStr,
                       "CASE FAILED with %d errors\n--- END %s ---\n",
                       caseErrs, str);
    }
}
#endif
#endif

void close_run()
{
    if (numCases) {
        test_print_int(0, "\n\nExecuted %d cases.\n", numCases);
        zgw_log(0, "Executed %d cases.\n", numCases);
        if (numCasesFail) {
            test_print_int(0, "%d of the cases had at least one error.\n", numCasesFail);
            zgw_log(0, "%d of the cases had at least one error.\n", numCasesFail);
        }
    }
    if (numErrs) {
        test_print_int(0, "\nFAILED, %d errors\n", numErrs);
        zgw_log(0, "FAILED, %d errors\n", numErrs);
    } else {
        test_print_int(0, "\nPASSED\n");
        zgw_log(0, "PASSED\n");
    }
}

static void test_print_int(int lvl, char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    if (lvl <= verbosity) {
        vprintf(fmt, args);
    }
    va_end(args);
}

#ifndef ZGW_LOG
void __test_print(int lvl, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    if (lvl <= verbosity) {
        printf("    TC: ");
        vprintf(fmt, args);
    }
    va_end(args);
    if (lvl <= verbosity) {
        if (caseLogFile) {
            va_start(args, fmt);
            fprintf(caseLogFile, "    ");
            vfprintf(caseLogFile, fmt, args);
            va_end(args);
        }
    }
}
#endif

void test_print_suite_title(int lvl, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    if (lvl <= verbosity) {
        printf("\n===== ");
        vprintf(fmt, args);
        printf(" =====\n");
        if (caseLogFile) {
            fprintf(caseLogFile, "\n===== ");
            vfprintf(caseLogFile, fmt, args);
            fprintf(caseLogFile, " =====\n");
        }
    }
    va_end(args);
}


#ifndef INLINE_TEST_HELPERS
void check_zero(int ii, char *msg)
{
    if (ii) {
        test_print(0, "FAIL - value is %d. %s\n", ii, msg);
        numErrs++;
    } else {
        test_print(2, "PASS. %s\n", msg);
    }
}

void check_true(int8_t ii, char *msg)
{
    if (ii) {
        test_print(2, "PASS. %s\n", msg);
    } else {
        test_print(0, "FAIL, check is false. %s\n", msg);
        numErrs++;
    }
}

void check_null(void *pvs, char *msg)
{
    if (!pvs) {
        test_print(2, "PASS. %s\n", msg);
    } else {
        test_print(0, "FAIL - pointer %p found. %s\n", pvs, msg);
        numErrs++;
    }
}

void check_not_null(void *pvs, char *msg)
{
    if (pvs) {
        test_print(2, "PASS - Pointer %p found. %s\n", pvs, msg);
    } else {
        test_print(0, "FAIL - %s\n", msg);
        numErrs++;
    }
}

void check_equal(long actual, long expected, char *msg)
{
   if((actual) == (expected)) test_print(2, "PASS. %s\n", msg);
   else {
     test_print(0, "FAIL, actual: %ld not equal to expected: %ld. %s\n", (long) actual, (long) expected, msg);
     numErrs++;
   }
}
#endif

void check_mem(const uint8_t *expected, const uint8_t *was, uint16_t len, const char *errmsg, const char *msg)
{
   uint8_t has_diff = 0;
   int ii;

   for (ii = 0; ii < len; ii++) {
      if (expected[ii] != was[ii]) {
         has_diff = 1;
         test_print(0, errmsg, ii, expected[ii], was[ii]);
      }
   }
   if (has_diff) {
        test_print(0, "FAIL - %s\n", msg);
        numErrs++;
   } else {
      test_print(2, "PASS - Pointer %p has expected data. %s\n", was, msg);
   }
}
