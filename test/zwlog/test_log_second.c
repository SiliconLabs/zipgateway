#include <zgw_log.h>
#include "test_log.h"

zgw_log_id_default_set(feat_1);

/** \ingroup logtesthelpers
 * In this file feat_1 is default feature group.
 *
 * For each level, enable level for feat_1, log to level and log to level above.
 * Expect only the first log line to be printed.
 */

int test_log_set_levels(void) {
  
  zgw_log_lvl_set(feat_1, zwlog_lvl_critical);
  zgw_log(zwlog_lvl_critical, "This should now be printed at lvl %d\n", zwlog_lvl_critical);
  zgw_log(zwlog_lvl_error, "This should NOT be printed at lvl %d\n", zwlog_lvl_error);

  zgw_log_lvl_set(feat_1, 1);
  zgw_log(1, "This should now be printed at lvl %d\n", 1);
  zgw_log(2, "This should NOT be printed at lvl %d\n", 2);

  zgw_log_lvl_set(feat_1, 2);
  zgw_log(2, "This should now be printed at lvl %d\n", 2);
  zgw_log(3, "This should NOT be printed at lvl %d\n", 3);

  zgw_log_lvl_set(feat_1, 3);
  zgw_log(3, "This should now be printed at lvl %d\n", 3);
  zgw_log(4, "This should NOT be printed at lvl %d\n", 4);

  zgw_log_lvl_set(feat_1, 4);
  zgw_log(4, "This should now be printed at lvl %d\n", 4);
  zgw_log(5, "This should NOT be printed at lvl %d\n", 5);

  zgw_log_lvl_set(feat_1, 5);
  zgw_log(5, "This should now be printed at lvl %d\n", 5);
  zgw_log(6, "This should NOT be printed at lvl %d\n", 6);

  return 0;
}
