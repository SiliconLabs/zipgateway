#ifndef _LOG_TEST_INT_H_
#define _LOG_TEST_INT_H_

/** \ingroup zgw_log_test
 *  Run-time setting of feature level.
 *
 * For each valid level, set feature level, log to the feature at that
 * level, set feature level one below, log to the feature at the
 * level.  External program checks that the first log is printed and
 * the second is not.
 */
int test_log_set_levels(void);

/** \ingroup zgw_log_test
 * Log line trimming of very long names.
 */
void test_log_trimming_very_long_name(void);

#endif
