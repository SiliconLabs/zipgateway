/* © 2019 Silicon Laboratories Inc. */

#include <stdlib.h>

#include "zgwr_pvl.h"
#include "zgw_data.h"
#include "zgw_restore_cfg.h"
#include "lib/zgw_log.h"

#include "test_helpers.h"
#include "test_restore_help.h"

zgw_log_id_define(test_restore_pvl);
zgw_log_id_default_set(test_restore_pvl);

void test_zgwr_pvl_verify(){
  cfg_init();

	restore_cfg.installation_path = "./";
	/*It check if the provisioning_list.cfg and provisioning_list_store.dat files are created*/
    
    // TODO re-enable this test, we should stub the provisioning_list_init function
    
    //check_true((int)zgw_restore_pvl_file(), "pvl data restore file created");

	/*To Do:check if the node provisioning data are written to the persisting file from
	 *internal data structure, for restoring  Z/IP Gateway from a back-up*/

}

int main (void)
{
    verbosity = test_case_start_stop;

    /* Start the logging system at the start of main. */
    zgw_log_setup("restore_pvl.log");

    /* Log that we have entered this function. */
    zgw_log_enter();

    /* Log at level 1 */
    zgw_log(1, "ZGWlog test arg %d\n", 7);

    test_zgwr_pvl_verify();

    close_run();

    /* We are now exiting this function */
    zgw_log_exit();

    /* Close down the logging system at the end of main(). */
    zgw_log_teardown();

   return numErrs;
}
