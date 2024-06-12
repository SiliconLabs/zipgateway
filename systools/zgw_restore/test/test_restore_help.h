/* © 2019 Silicon Laboratories Inc.
 */
#ifndef _TEST_RESTORE_HELP_H_
#define _TEST_RESTORE_HELP_H_

#include <lib/zgw_log.h>
#include "zgw_data.h"

zgw_log_id_declare(tst_rest_api);
zgw_log_id_declare(tst_js_parse);
zgw_log_id_declare(test_restore_eeprom);
zgw_log_id_declare(test_restore_pvl);


/** Some sanity checks on a hand-built controller structure.
 * Careful about using this on error-test cases.
 */
int check_mock_controller(const zw_controller_t *ctrl);

#endif
