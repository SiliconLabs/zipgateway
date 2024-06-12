/* © 2017 Silicon Laboratories Inc.
 */



/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/
#include <ZW_classcmd_ex.h>
#include "test_helpers.h"
#include "test_CC_helpers.h"
#include <CC_provisioning_list.h>
#include <provisioning_list.h>
#include <ZW_SendDataAppl.h>
#include "RD_internal.h"
#include <stdlib.h>

/****************************************************************************/
/*                      PRIVATE TYPES and DEFINITIONS                       */
/****************************************************************************/
#define MOCK_CACHE_DEPTH 10 /* How many calls does the mocks cache. Note: Only some mocks implement this. Others default to 1. */

#define MOCK_PROV_LIST_MODULE

/****************************************************************************/
/*                              EXPORTED DATA                               */
/****************************************************************************/

/****************************************************************************/
/*                              PRIVATE DATA                                */
/****************************************************************************/

/* Dummy struct provision returned by all mocks when appropriate */
struct provision dummy_prov_struct;


/****************************************************************************/
/*                              PRIVATE FUNCTIONS                           */
/****************************************************************************/


//#define MOCK_PROV_LIST_MODULE /* Undef to use real PVL module */
#ifdef MOCK_PROV_LIST_MODULE

/* Not used yet */
rd_node_database_entry_t* rd_lookup_by_dsk(uint8_t dsklen, const uint8_t* dsk)
{
   return NULL;
}

struct  {
  struct provision *pvs;
  uint8_t type;
  uint8_t len;
  uint8_t *val;
} provisioning_list_tlv_set_args[MOCK_CACHE_DEPTH];
static unsigned int provisioning_list_tlv_set_call_count = 0;

uint8_t provisioning_list_tlv_crt_flag(pvs_tlv_type_t tlv_type) {
   return 0;
}

int provisioning_list_tlv_set(struct provision *pvs, uint8_t type, uint8_t len, uint8_t *val)
{
  static uint8_t val_buf[MOCK_CACHE_DEPTH][2000];
  unsigned int idx = provisioning_list_tlv_set_call_count++;
  /* Cache the value of the pointer, so we dont fail when being called with ptr to shortlived stack
   * objects */
  memcpy(val_buf[idx], val, len);

  provisioning_list_tlv_set_args[idx].pvs = pvs;
  provisioning_list_tlv_set_args[idx].type = type;
  provisioning_list_tlv_set_args[idx].len = len;
  provisioning_list_tlv_set_args[idx].val = val_buf[idx];
  return 1;
}

int provisioning_list_clear_called = 0;
void provisioning_list_clear() {
  provisioning_list_clear_called++;
}

struct {
  uint8_t dsk_len;
  uint8_t *dsk;
} provisioning_list_dev_remove_args;

int provisioning_list_dev_remove(uint8_t dsk_len, uint8_t *dsk)
{
  provisioning_list_dev_remove_args.dsk_len = dsk_len;
  provisioning_list_dev_remove_args.dsk = dsk;
  return 1;
}

struct  {
  uint8_t dsk_len;
  uint8_t *dsk;
  provisioning_bootmode_t bootmode;
} provisioning_list_dev_set_args;

struct provision * provisioning_list_dev_set(uint8_t dsk_len, uint8_t *dsk,
                                             provisioning_bootmode_t class)
{
   test_print(1, "Calling set\n");
  provisioning_list_dev_set_args.dsk_len = dsk_len;
  provisioning_list_dev_set_args.dsk = dsk;
  provisioning_list_dev_set_args.bootmode = class;

  return &dummy_prov_struct;
}

struct  {
  uint8_t dsk_len;
  uint8_t *dsk;
  provisioning_bootmode_t bootmode;
} provisioning_list_dev_add_args;

struct provision * provisioning_list_dev_add(uint8_t dsk_len, uint8_t *dsk,
                                             provisioning_bootmode_t class)
{
   test_print(1, "Calling add\n");
  provisioning_list_dev_add_args.dsk_len = dsk_len;
  provisioning_list_dev_add_args.dsk = dsk;
  provisioning_list_dev_add_args.bootmode = class;

  return &dummy_prov_struct;
}

int provisioning_list_bootmode_set(struct provision * pvs, provisioning_bootmode_t bootmode)
{
   test_print(1, "Calling bootmode_set\n");
   pvs->bootmode = bootmode;
   return 0;
}

void provisioning_list_iterator_delete(provisioning_list_iter_t *iter)
{
  UNUSED(iter);
  return;
}

provisioning_list_iter_t dummy_iter = {0, 0, 0, NULL};

provisioning_list_iter_t * provisioning_list_iterator_get(uint32_t id)
{
  dummy_iter.id = id;
  return (provisioning_list_iter_t*)&dummy_iter;
}

struct provision * provisioning_list_iter_get_next(provisioning_list_iter_t *iter)
{
  UNUSED(iter);
  return &dummy_prov_struct;
}

struct {
  uint8_t dsk_len;
  uint8_t *dsk;
} provisioning_list_dev_get_args;

struct provision * provisioning_list_dev_get(uint8_t dsk_len, const uint8_t *dsk)
{
  provisioning_list_dev_get_args.dsk_len = dsk_len;
  provisioning_list_dev_get_args.dsk = malloc(dsk_len);
  if (provisioning_list_dev_get_args.dsk == NULL) {
     provisioning_list_dev_get_args.dsk_len = 0;
     return NULL;
  } else {
     memcpy(provisioning_list_dev_get_args.dsk, dsk, dsk_len);
  }
  return NULL;
}

struct {
  struct provision *pvs;
  uint8_t type;
} provisioning_list_tlv_get_args;
static unsigned int provisioning_list_tlv_get_call_count = 0;

struct pvs_tlv * provisioning_list_tlv_get(struct provision *pvs, uint8_t type)
{
  provisioning_list_tlv_get_call_count++;
  provisioning_list_tlv_get_args.pvs = pvs;
  provisioning_list_tlv_get_args.type = type;
  return (struct pvs_tlv*)0x42;
}
#endif /* MOCK_PROV_LIST_MODULE */

int provisioning_list_status_set(struct provision *pvs, pvs_status_t status)
{
  UNUSED(pvs);
  UNUSED(status);
  return 1;
}



static void _check_true(uint8_t x, char *msg, int lineno)
{
  char buf[2000];
  snprintf(buf, sizeof buf,  "%s%i", msg, lineno);
  check_true(x, buf);
}
#define CT(x) _check_true(x, "test in line: ", __LINE__)
#define CT2(x, msg) _check_true(x, msg ". Test in line: ", __LINE__)

static void mock_reset(void)
{
#ifdef MOCK_PROV_LIST_MODULE
  provisioning_list_tlv_set_call_count = 0;
  provisioning_list_tlv_get_call_count = 0;
#endif /* MOCK_PROV_LIST_MODULE */
}



/****************************************************************************/
/*                              EXPORTED FUNCTIONS                          */
/****************************************************************************/

static void test_CC_pvl_basic(void)
{
  static uint8_t cmd_pl_set[] = {COMMAND_CLASS_PROVISIONING_LIST, COMMAND_PROVISION_SET,
    0x03, 0x10,        // Seqno, DSK length
    0x7a, 0x55, 0x49, 0x94, 0x55, 0xae, 0x8f, 0x57, 0x29, 0x69, 0x45, 0x4c, 0x23, 0x3a, 0x72, 0x69,
    100, 0x03, 'a', 'b', 'c',    // Name
    102, 0x08, '1', '2', '3', '1', '2', '3', '1', '2',  // Location
  };


  uint8_t res = PVL_CommandHandler(&dummy_connection, cmd_pl_set, sizeof cmd_pl_set);
  /* Check for _add with default bootstrap mode, name set, location set, status set default pending*/
  test_print(1, "res: %u, dsk len: %u\n", res, provisioning_list_dev_add_args.dsk_len);
  CT(memcmp(provisioning_list_dev_get_args.dsk, &cmd_pl_set[4],
            provisioning_list_dev_get_args.dsk_len) == 0);
  CT(provisioning_list_dev_get_args.dsk_len == 0x10);
  CT(provisioning_list_dev_add_args.dsk == &cmd_pl_set[4]);
  CT(provisioning_list_dev_add_args.dsk_len == 0x10);
  test_print(1, "bootmode %u\n", dummy_prov_struct.bootmode);
  CT(provisioning_list_dev_add_args.bootmode == PVS_BOOTMODE_SMART_START);

  CT2(provisioning_list_tlv_set_call_count == 2, "_tlv_set should be called 3 times");
  CT2(provisioning_list_tlv_set_args[0].pvs == &dummy_prov_struct &&
      provisioning_list_tlv_set_args[0].type == PVS_TLV_TYPE_NAME &&
      provisioning_list_tlv_set_args[0].len == 3 &&
      memcmp(provisioning_list_tlv_set_args[0].val, &cmd_pl_set[17], 3),
      "Set NAME TLV correctly");


  CT2(provisioning_list_tlv_set_args[1].pvs == &dummy_prov_struct &&
      provisioning_list_tlv_set_args[1].type == PVS_TLV_TYPE_LOCATION &&
      provisioning_list_tlv_set_args[1].len == 8 &&
      memcmp(provisioning_list_tlv_set_args[1].val, &cmd_pl_set[22], 8),
      "Set LOCATION TLV correctly");

  CT2(dummy_prov_struct.status == PVS_STATUS_PENDING,
      "set STATUS correctly");

  //static uint8_t cmd_pl_set2[] = {COMMAND_CLASS_PROVISIONING_LIST, COMMAND_PROVISION_SET,
  //   0x03, 0x10,
  //   0x12, 0x34, 0x56, 0x94, 0x55, 0xae, 0x8f, 0x57, 0x29, 0x69, 0x45, 0x4c, 0x23, 0x3a, 0x88, 0x99,
  //   100, 0x03, 'x', 'y', 'z',
  //   101, 0x04, '5', '6', ' ', '7',
  // };
  //
  ///**
  // * PL Set - No TLV
  // */
  //static uint8_t cmd_pl_set3[] = {COMMAND_CLASS_PROVISIONING_LIST, COMMAND_PROVISION_SET,
  //    0x03, 0x10,
  //    0x88, 0x99, 0x56, 0x94, 0x55, 0xae, 0x8f, 0x57, 0x29, 0x69, 0x45, 0x4c, 0x23, 0x3a, 0x88, 0x99,
  //};
  //
  //static uint8_t cmd_pl_delete[] = {COMMAND_CLASS_PROVISIONING_LIST, COMMAND_PROVISION_DELETE,
  //   0x00, 0x10,
  //   0x7a, 0x55, 0x49, 0x94, 0x55, 0xae, 0x8f, 0x57, 0x29, 0x69, 0x45, 0x4c, 0x23, 0x3a, 0x72, 0x69,
  //};
  //
  mock_reset();


  //
  // static uint8_t cmd_pl_get2[] = {COMMAND_CLASS_PROVISIONING_LIST, COMMAND_PROVISION_GET,
  //    0x00, 0x10,
  //    0x12, 0x34, 0x56, 0x94, 0x55, 0xae, 0x8f, 0x57, 0x29, 0x69, 0x45, 0x4c, 0x23, 0x3a, 0x88, 0x99,
  // };



}

int main()
{
    CC_provisioning_list_init();

    test_CC_pvl_basic();

    close_run();

    return numErrs;
}
