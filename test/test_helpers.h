/* © 2017 Silicon Laboratories Inc.
 */

#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <stdint.h>
#include <stdio.h>
#include <lib/zgw_log.h>

#define TEST_CREATE_LOG_NAME __FILE__".log"

/** \defgroup test_helpers Simple Test Framework
 *
 * The simple test framework provides a small set of unit test helpers to handle
 *
 * - Case definition and counting
 * - Condition checking
 * - Failure counting
 * - Final summary
 * - Logging to file or stdout with controllable verbosity
 *
 * The framework fits with ctest as the primary test runner.
 *
 * @{
 */

/** Total number of errors (aka failed checks) in this test case.
 */
extern int numErrs;

/** The verbosity of test logging.
 *
 * As long as the test case does not have an opinion, log
 * everything. */
extern int verbosity;

/** Verbosity levels */
enum {
    test_always = 0,
    test_suite_start_stop = 1,
    test_case_start_stop = 2,
    test_comment = 3,
    test_verbose = 4,
    test_number_of_levels
} test_verbosity_levels;

#define INLINE_TEST_HELPERS

/** Validator function.  Check if argument is zero and increment numErrs if not.
 *
 * Print msg and fail statement to stdout on failure.
 * Print message and passed statement to log at level 2 on success.
*/
#ifdef INLINE_TEST_HELPERS
#define check_zero(ii, msg) {\
   if ((ii)) {\
       test_print(0, "FAIL - value is %d. %s\n", ii, msg);\
       numErrs++;\
   } else {\
       test_print(2, "PASS. %s\n", msg);\
   }\
}
#else
void check_zero(int ii, char *msg);
#endif

/** Validator function.  Check if argument is zero and increment numErrs if not.
 *
 * Print msg and fail statement to stdout on failure.
 * Print message and passed statement to log at level 2 on success.
*/
#ifdef INLINE_TEST_HELPERS
#define check_not_null(pvs, msg) {\
    if ((pvs)) {\
        test_print(2, "PASS - Pointer %p found. %s\n", pvs, msg);\
    } else {\
        test_print(0, "FAIL - %s\n", msg);\
        numErrs++;\
    }\
}
#else
void check_not_null(/*@null@*/void *pvs, char *msg);
#endif

/** Validator function.  Check if argument is zero and increment numErrs if not.
 *
 * Print msg and fail statement to stdout on failure.
 * Print message and passed statement to log at level 2 on success.
*/
#ifdef INLINE_TEST_HELPERS
#define check_null(pvs, msg) {\
   if (!(pvs)) {\
        test_print(2, "PASS. %s\n", msg);\
    } else {\
        test_print(0, "FAIL - pointer %p found. %s\n", pvs, msg);\
        numErrs++;\
    }\
}
#else
void check_null(/*@null@*/void *pvs, char *msg);
#endif

/** Validator function.  Check if argument is zero and increment numErrs if not.
 *
 * Print msg and fail statement to stdout on failure.
 * Print message and passed statement to log at level 2 on success.
*/
#ifdef INLINE_TEST_HELPERS
#define check_true(ii, msg) {\
   if((ii)) test_print(2, "PASS. %s\n", msg);\
   else {\
     test_print(0, "FAIL, check is false. %s\n", msg);\
     numErrs++;\
   }\
}
#else
void check_true(int8_t ii, char *msg);
#endif

/** Validator function.  Check if two values are equal
 *
 * Print msg and fail statement to stdout on failure.
 * Print message and passed statement to log at level 2 on success.
*/
#ifdef INLINE_TEST_HELPERS
#define check_equal(actual, expected, msg) {\
   if((actual) == (expected)) test_print(2, "PASS. %s\n", msg);\
   else {\
     test_print(0, "FAIL, actual: %ld not equal to expected: %ld. %s\n", (long) actual, (long) expected, msg);\
     numErrs++;\
   }\
}
#else
void check_equal(long actual, long expected, char *msg);
#endif


/**
 * Compare two byte arrays and print the differences (if any).
 *
 */
void check_mem(const uint8_t *expected, const uint8_t *was, uint16_t len, const char *errmsg, const char *msg);

FILE *test_create_log(const char *logname);

/** Print out final result of the test run.
 *
 * Should typically be called at the end of main(), before returning
 * numErrs. */
void close_run();


/** Print out start text for a related group of validations on that could be considered one case.
 *
 * Sometimes several validations are needed to confirm a certain
 * behaviour (eg, first check that a pointer is not null, then check
 * that it points to the right thing).  This function allows you to
 * group the subsequent validations under a common name and print log
 * statements reflecting this hierarchy.
 */
#ifdef INLINE_TEST_HELPERS
#define start_case(str, logfile)                        \
   start_case_setup(str); \
   zgw_log(2, "--- CASE %s ---\n", str);
/**
 * Helper function for \ref start_case().
 *
 */
void start_case_setup(const char *str);
#else
void start_case(const char *str, /*@null@*/FILE *logfile);
#endif

/** Print out a summary of the results of the validations since this case was started.

 This function does not perform any validation or affect the result in
 any way.  It is only useful if you want more structured logging. */
#ifdef INLINE_TEST_HELPERS
#define close_case_test(str)                                            \
   close_case_inl(str, (*zgw_default_id)->zwlog_feat_val,               \
                  (*zgw_default_id)->zwlog_feat_name,                   \
                  __FILE__, __LINE__, __func__)

#define close_case(str)                                                 \
   do {                                                                 \
      if (!check_case(str)) { zgw_log(2, "--- CASE %s ---\n", str); }   \
      int caseErrs = close_case_errs_cnt(str);                          \
      if (caseErrs == 0) {                                              \
         zgw_log(2,  "CASE PASSED\n");                                  \
         zgw_log(2,  "--- END %s ---\n", str);                          \
      } else {                                                          \
         zgw_log(2, "CASE FAILED with %d errors\n", caseErrs);          \
         zgw_log(2, "--- END %s ---\n", str);                           \
      }                                                                 \
   } while (0)
void close_case_inl(const char *str, int zwlog_feat_val, const char* id_name,
                    const char *fileStr, int line, const char *funcStr);
/**
 * Helper function for \ref close_case();
 */
int check_case(const char *str);
/**
 * Helper function for \ref close_case();
 */
int close_case_errs_cnt(const char *str);
#else
void close_case(const char *str);
#endif


#ifdef ZGW_LOG
zgw_log_id_declare(TC);
#define test_print(lvl, fmt, ...) do {zgw_log_to(TC, lvl, fmt, ##__VA_ARGS__); } while (0)
#else
/** Print to stdout if verbosity is at least lvl */
#define test_print(lvl, fmt, ...) __test_print(lvl, fmt, ##__VA_ARGS__)

/** Helper function for non-log printing */
void __test_print(int lvl, const char *fmt, ...);
#endif

/** Print with some fancy formatting */
void test_print_suite_title(int lvl, const char *fmt, ...);

 /* @}
 */
#endif
