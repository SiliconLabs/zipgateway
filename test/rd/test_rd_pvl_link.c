/* © 2018 Silicon Laboratories Inc.
 */

#include "RD_internal.h"
#include <string.h>
#include <lib/zgw_log.h>
#include "test_helpers.h"
#include "provisioning_list_types.h"
#include "provisioning_list.h"
#include <stdlib.h>

zgw_log_id_define(rd_pvl_test);
zgw_log_id_default_set(rd_pvl_test);

/**
\defgroup rd_test Ressource Directory unit test.

Test Plan

Test RD link helpers

Basic
- Lookup non-included device
- Lookup included device
- Look up failing device
 - Lookup non-existing device
- Lookup removed device

Store to file of rd/pvl link
- Lookup included device after pvl .dat re-read
- Lookup included device after pvl .dat and eeprom re-read
- Lookup removed device after pvl .dat re-read
- Lookup failing device after pvl .dat re-read

*/


FILE *log_strm = NULL;

static uint8_t dsk5[] =       {10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};

static void test_dsk_lookup(void);

static uint8_t mock_rd_add_endpoint(rd_node_database_entry_t* n, uint8_t epid) {
  rd_ep_database_entry_t* ep;

  ep = (rd_ep_database_entry_t*) malloc(sizeof(rd_ep_database_entry_t));
  if (!ep) {
    return 0;
  }

  memset(ep, 0, sizeof(rd_ep_database_entry_t));

  ep->endpoint_id = epid;
  ep->state = EP_STATE_PROBE_INFO;
  ep->node = n;

  /*A name of null means the default name should be used.*/
  ep->endpoint_name_len = 0;
  ep->endpoint_name = 0;
  list_add(n->endpoints, ep);
  n->nEndpoints++;

  return 1;
}

int main()
{
    verbosity = test_case_start_stop;

    zgw_log_setup("rd_pvl_link.log");
//    log_strm = test_create_log(TEST_CREATE_LOG_NAME);

//    start_case("Hello world", log_strm);
//    check_true(1, "Framework setup completed");
//    close_case("Hello world");

    test_dsk_lookup();

    close_run();
//    fclose(log_strm);

    /* Close down the logging system at the end of main(). */
    zgw_log_teardown();

    return numErrs;
}

static uint8_t *name2 = (uint8_t *)"Bulb";
static uint8_t *location1 = (uint8_t *)"soveværelse\n";

static void test_dsk_lookup(void) {
    int res;
    struct provision *sms_pvs1;
    struct pvs_tlv *tmp_tlv;

    rd_node_database_entry_t *rd_dbe;
    rd_node_database_entry_t *tmp;
    uint8_t nodeid5 = 6;

   start_case("Lookup non-included device", log_strm);
   rd_dbe = rd_lookup_by_dsk(sizeof(dsk5), dsk5);
   check_null(rd_dbe, "dsk5 is not in RD, so we should get NULL");
   close_case("Lookup non-included device");

   start_case("Lookup included device", log_strm);
   
   provisioning_list_init(NULL, NULL);
   zgw_log(3, "Create provision for SmS device\n");
   sms_pvs1 = provisioning_list_dev_add(sizeof(dsk5), dsk5, PVS_BOOTMODE_SMART_START);
   if (sms_pvs1) {
      provisioning_list_tlv_set(sms_pvs1, PVS_TLV_TYPE_LOCATION, (uint8_t)strlen((char*)location1)+1, location1);
      provisioning_list_tlv_set(sms_pvs1, PVS_TLV_TYPE_NAME, (uint8_t)strlen((char*)name2)+1, name2);
   }

   rd_dbe = rd_node_entry_alloc(nodeid5);
   mock_rd_add_endpoint(rd_dbe, 0);

   rd_node_add_dsk(nodeid5, sizeof(dsk5), dsk5);
   tmp = rd_lookup_by_dsk(sizeof(dsk5), dsk5);
   check_true(rd_dbe == tmp, "After adding, dsk5 should be found in RD");
   check_true(tmp->nodeid == 6, "Nodeid 6 was assigned to dsk5 in RD");

   uint8_t buf[32];
   rd_ep_database_entry_t* ep = list_head(rd_dbe->endpoints);
   check_true(ep && rd_get_ep_name(ep, (char*)buf, 32) == 4,
              "Size of name is converted correctly");
   check_mem(name2, buf, 4, "Endpoint name pos %u expected 0x%2x, found 0x%2x\n",
             "Endpoint name copied from PVL to RD");

   check_true(ep && rd_get_ep_location(ep, (char*)buf, 32) == 14,
              "Size of location is converted correctly");
   check_mem(location1, buf, sizeof(location1),
             "Endpoint location pos %u expected 0x%2x, found 0x%2x\n",
             "Endpoint location copied from PVL to RD");
   close_case("Lookup included device");

   /* Node id tlv */
   start_case("Node id of included device", log_strm);
   check_true(provisioning_list_pending_count() == 0,
              "Included device is not included in pending-count.");
   tmp = rd_lookup_by_dsk(sizeof(dsk5), dsk5);
   check_true(tmp->nodeid == nodeid5, "Find device by dsk.");
   close_case("Node id of included device");

//   start_case("Node id of non-included device", log_strm);
//   close_case("Node id of non-included device");

   /* inclusion tlv */

//   start_case("Look up failing device", log_strm);
//   close_case("Look up failing device");

//   start_case("Lookup removed device", log_strm);
//   close_case("Lookup removed device");

//   start_case("Lookup non-existing device", log_strm);
//   close_case("Lookup non-existing device");

}
