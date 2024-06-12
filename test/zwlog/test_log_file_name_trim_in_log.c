#include <zgw_log.h>
#include "test_log.h"
#include "test_log_int.h"

/**********************
 * Local declarations *
 **********************/

/*********************
 * Local definitions *
 *********************/

zgw_log_id_define(very_long_feature_name);
zgw_log_id_default_set(very_long_feature_name);

void test_log_trimming_very_long_name(void) {
  zgw_log(zwlog_lvl_configuration, 
	  "Log statement in file with long name and function with long name\n");
}
