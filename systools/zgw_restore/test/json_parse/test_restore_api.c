/* © 2019 Silicon Laboratories Inc. */

#include <stdlib.h>
#include <time.h>
#include "ZW_typedefs.h"
#include "ZW_classcmd.h"

#include "zgw_data.h"

#include "sys/clock.h"
#include "lib/zgw_log.h"

#include "test_helpers.h"
#include "test_restore_help.h"

zgw_log_id_define(tst_rest_api);
zgw_log_id_default_set(tst_rest_api);

void build_tc1_data() {
   /* zero the global */
   uint8_t nodeID = 1;
   uint8_t SUCid = 1;
   uint8_t *cc_list=NULL; /* Not needed for restore, so 0 is OK */
   zw_controller_t * ctrl;
   zip_gateway_backup_manifest_t manifest = {7,8,1234};

   zgw_data_init(&manifest,
                 NULL, NULL, NULL);

   ctrl = zw_controller_add(SUCid,
                            cc_list);

   uint8_t capability = 0xFF; /* TODO: this must be a controller. */
   uint8_t security = 0x20; /* TODO: find the real flirs setting here */
   zw_node_type_t node_type1 = {BASIC_TYPE_CONTROLLER, /* TODO: check this */
                                GENERIC_TYPE_STATIC_CONTROLLER,
                                SPECIFIC_TYPE_GATEWAY};
   int res = zw_node_add(ctrl, nodeID,
                         capability,
                         security,
                         &node_type1);
   node_type1.basic = BASIC_TYPE_SLAVE;
   node_type1.generic = GENERIC_TYPE_SENSOR_BINARY;
   node_type1.specific = SPECIFIC_TYPE_ALARM_SENSOR;
   res = zw_node_add(ctrl, 2,
                     0xbb,
                     0x40,
                     &node_type1);

}


int main(void) {
   const zip_gateway_backup_data_t *bu = NULL;
   const zgw_data_t *zgw_data = NULL;

   verbosity = test_case_start_stop;

   /* Start the logging system at the start of main. */
   zgw_log_setup("restore_api.log");

   /* Log that we have entered this function. */
   zgw_log_enter();

   /* Log at level 1 */
   zgw_log(1, "ZGWlog test arg %d\n", 7);

   build_tc1_data();

   bu = zip_gateway_backup_data_get();

   check_not_null((void*)bu, "Backup DB has been created");
   if (bu) {
      check_true(bu->manifest.backup_version_major == 7,
                 "Version installed correctly");
   }

   zgw_data = zgw_data_get();
   check_not_null((void*)zgw_data, "ZGW data has been created in backup object.");

   close_run();

   /* Close down the logging system at the end of main(). */
   zgw_log_teardown();

   return numErrs;
}
