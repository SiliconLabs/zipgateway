/* © 2014 Silicon Laboratories Inc.
 */
#include "test_helpers.h"
#include "provisioning_list.h"
#include "provisioning_list_files.h"
#include "pvs_cfg.h"
#include "pvs_cfg_test_help.h"

/**
\ingroup pvs_test
@{

*/

static FILE *log_strm;

int main(void);

/* Test cases */
void test_device_errors(void);
void test_tlv_errors(void);

// TODO: more validation of errors
void test_device_errors(void)
{
    start_case("device errors", log_strm);
    provisioning_list_clear();
    steal_file(PROVISIONING_LIST_STORE_FILENAME_DEFAULT, "pvs_dummy.dat");

    provisioning_list_init(NULL, "../../../test/pvs/files/pvs_device_errors.cfg");

    provisioning_list_print();
    check_zero(provisioning_list_get_count(),
               "Do not import from config file with device errors.");
    close_case("device errors");
}

// TODO: more validation of errors
void test_tlv_errors(void)
{
    start_case("TLV errors", log_strm);
    provisioning_list_clear();
    steal_file(PROVISIONING_LIST_STORE_FILENAME_DEFAULT, "pvs_dummy.dat");

    provisioning_list_init(NULL, "../../../test/pvs/files/pvs_tlv_errors.cfg");

    provisioning_list_print();
    check_zero(provisioning_list_get_count(),
               "Do not import from config file with tlv errors.");

    close_case("TLV errors");
}

/** Note that this test uses relative file paths */
int main()
{
    verbosity = test_case_start_stop;

    log_strm = test_create_log(TEST_CREATE_LOG_NAME);

    provisioning_list_init(NULL, NULL);

    provisioning_list_clear();

    test_device_errors();

    test_tlv_errors();

    provisioning_list_clear();

    close_run();

    fclose(log_strm);

    return numErrs;
}

/**
   @}
*/
