/* © 2014 Silicon Laboratories Inc.
 */

#include "provisioning_list.h"
#include <string.h>
#include "test_helpers.h"

/**
\defgroup pvs_test Provisioning list unit test.

Test Plan

Test device handling.

Basic

 - Add device
 - Add more than one device
 - Add device that already exists
 - Add device with same dsk prefix
 - Modify device that exists
 - Modify device that does not exist

 - Remove device that exists
 - Remove device that does not exist

 - Look up device that exists
 - Look up device that does not exist
 - Look up device by challenge
 - Look up device by challenge - mismatch
 - Look up device by challenge - length mismatch
 - Challenge too short

 - Look up device by homeid
 - Look up device with too short dsk by homeid
 - Look up device by homeid that does not match
 - Look up device by homeid that only matches because of the masking of the last bit
 - Look up device by homeid that only matches because of the masking of the first bit
 - Look up device by homeid that only matches because of the masking of the second bit


 - Get number of pending devices

 - Get number of devices

- Clear entire list (when it has holes and tlvs)

Advanced

- Add device when there are holes in the array
- Delete device when there are holes in the array
- Look up device when there are holes in the array

 - Run with gcov
- Write "sanity checker" to run over store and check that everything is consistent.

Test TLV handling.

 - Add TLV to DSK
 - Add second TLV
 - Add TLV that already exists
 - Add TLV with length 0
 - Modify TLV

 - Look up tlv by pointer
 - Look up tlv by dsk

 - Add TLV to NULL pointer
 - Add TLV to pointer to removed device

 - Remove first TLV
 - Remove non-first TLV
 - Remove last TLV
 - Remove TLV that does not exist when there are no tlvs
 - Remove TLV that does not exist when there are some tlvs
 - Remove TLV from invalid (removed) device
- Remove all TLVs (ie, delete a dsk that has a long list of tlvs)

 - Get the critical bit of a critical tlv
 - Get the critical bit of a non-critical tlv

Pseudo tlvs
 - Set status
 - Set status on non-existing device
 - Set bootmode
 - Set bootmode on non-existing device

Test persistent storage.

Invalid file initialization scenarios:
 - Initialize with NULL pointer (fall back to default)
 - Initialize with invalid filename (eg non-existing path) (fall back to default)
 - Initialize with file with invalid contents (fall back to default)
- Initialize with file with invalid contents that appear correct in the beginning (use file, abort import)

Invalid default file scenarios:
- Initialize with invalid argument and default storage is NULL  (requires recompilation)
- Initialize with invalid argument and default storage is not a file  (requires recompilation)
- Initialize with invalid argument and default storage is a file with invalid content (use anyway)

Valid file initialization scenarios:
 - Initialize with non-existing file (create)
 - Initialize with non-existing default file (create)
 - Initialize with valid existing file (import and use)
 - Initialize with valid existing default file (import and use)

Initializing from file scenarios (no need to distinguish default or configured file here):
 - Initialize from empty file
 - Initialize from valid file with no contents (pvs list was empty)
 - Initialize from valid file with one element
 - Initialize from valid file with one element with one tlv
- Initialize from valid file with one element with several tlvs
 - Initialize from valid file with more than one element

Storing to file scenarios:

 - Store to file when empty
 - Store to file after add one device
- Store to file after set device that adds
- Store to file after set device that modifies
 - Store to file after remove device
 - Store to file when array has holes
- Store to file after removing all devices

- Store to file after add first tlv
- Store to file after add on top of several tlvs
- Store to file after modify first tlv
- Store to file after modify one of several tlvs
- Store to file after delete first tlv
- Store to file after delete one of several tlvs
- Store to file after delete all of several tlvs

 - Store to file after changing status
 - Store to file after changing mode

Content syntax scenarios:

- Use of different lengths in DSKs
- Use of all possible uint8 values in DSKs
- Use of all possible values in status field
- Use of all possible values in class field

- Use of all ASCII characters in TLVs
 - Use of all possible types in tlv type
 - Use of unknown types in tlv type
- Use of Unicode in TLVs?
- Use of different lengths in TLVs


Test iterator

 - Create iterator
 - Create iterator from provisioning list with holes
 - Create iterator from empty provisioning list
 - Create iterator with too large DSK

 - Run through iterator with next.
 - Run through iterator with next when an element has been deleted.
 - Run through iterator when next is exhausted.
 - Run through empty iterator.
 - Look up in NULL iterator by next.

 - Run through DSKs in iterator.

 - Look up in iterator by index.
- Look up in iterator by index in the middle of looping.
 - Look up deleted device by index.
 - Look up in iterator by too high index.
 - Look up in empty iterator by index 0.
 - Look up in NULL iterator by index.

- Corrupt dsk in iterator
(dsk length corruption is not recoverable)

 - Delete iterator
 - Delete empty iterator


Test printing.

* @{
*
*/

FILE *log_strm = NULL;

static const char *linux_conf_provisioning_list_file = "foo.dat";//"provisioning_list.dat";

static void test_devices_basic(void);
static void test_devices_errors(void);
static void test_tlv_basic(void);
static void test_new_iterator(void);
static void test_new_iterator_empty(void);

static void test_provisioning_list_tlv_list(struct provision *pvs);
static void test_provisioning_list_homeid(void);
static void test_provisioning_list_pseudo_tlv_set(void);

/* **************************************** */

/* **************************************** */

static uint8_t dsk1_len = 4;
static uint8_t dsk1[4] = {12, 34, 00, 17};
static uint8_t dsk2_len = 4;
static uint8_t dsk2[4] = {56, 78, 21, 19};
//static uint8_t dsk3[10] = {56, 78, 21, 19, 77, 3, 0, 4, 0, 0};
static uint8_t dsk4_len = 10;
static uint8_t dsk4[10] = {56, 78, 21, 19, 0, 3, 0, 4, 0, 0};
static uint8_t dsk6_len = 10;
static uint8_t dsk6[10] = {57, 78, 21, 19, 0, 3, 0, 4, 0, 0};
static uint8_t dsklarge_len = 17;
static uint8_t dsklarge[] =       {10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 51, 62, 73, 76, 0, 0};
static uint8_t *name2 = (uint8_t *)"Bulb";
//static uint8_t *location2 = (uint8_t *)"Backyard";
static uint8_t *name1 = (uint8_t *)"Sensor";
//uint8_t *location1 = (uint8_t *)"Living Room\n";
static uint8_t *location1 = (uint8_t *)"soveværelse\n";
static uint8_t prod_type_len = 25;
static uint8_t prod_type[25] = {0x00, 0x45, 0x45, 0x45,
                         0x45, 0x45, 0x45, 0x45,
                         0x45, 0xAE, 0x45, 0xFF,
                         0x45, 0x45, 0x45, 0x03,
                         0x45, 0x45, 0x45, 0x45,
                         0x45, 0x45, 0x45, 0x45, 0x00};
static uint8_t dsk5[] =       {10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};

static uint8_t *name3 = (uint8_t *)"Node3";
static uint8_t *location3 = (uint8_t *)"Location3";
static uint8_t dskh1[] = {10, 11, 12, 13, 14, 15, 16, 17,  0x18, 0x19, 0x7A, 0x51, 0x62, 73, 76};
static uint8_t homeid1[4] =                               {0xD8, 0x19, 0x7A, 0x50}; // Matches dskh1 because of masks
static uint8_t dskh2[] = {10, 11, 12, 13, 14, 15, 16, 17,  0x48, 0x71, 0x20, 0xBF, 62, 73, 76};
static uint8_t homeid2[4] =                               {0xC8, 0x71, 0x20, 0xBE}; // Matches dskh2 because of masks
//static uint8_t dskh3[] = {10, 11, 12, 13, 14, 15, 16, 17,  0x18, 0x19, 0x20, 0x51, 62, 73, 76};
static uint8_t homeid3[4] =                               {0xD8, 0x19, 0x7B, 0x50};
static uint8_t homeidlong[5] =                            {0xD8, 0x19, 0x7A, 0x50, 0x56};

int main()
{
    log_strm = test_create_log(TEST_CREATE_LOG_NAME);

//    provisioning_list_init(NULL);
    provisioning_list_init(linux_conf_provisioning_list_file, NULL);

//    test_pvs_list_load_from_file(linux_conf_provisioning_list_file);

    if (verbosity > 1) {
        provisioning_list_print();
    }

    provisioning_list_clear();

    start_case("Print empty", log_strm);
    provisioning_list_print();
    close_case("Print empty");

    test_new_iterator_empty();

    test_devices_basic();

    test_tlv_basic();

    test_devices_errors();

    test_new_iterator();

//    provisioning_list_print();
//    provisioning_list_clear();
//    provisioning_list_print();

    test_provisioning_list_homeid();

    test_provisioning_list_pseudo_tlv_set();

    close_run();
    fclose(log_strm);

    return numErrs;
}

/* Assumes the list is empty */
static void test_new_iterator_empty(void)
{
    provisioning_list_iter_t *iter;
//    uint8_t cnt = provisioning_list_get_count();
    uint8_t ii;
//    uint8_t del_idx = cnt+1;
    struct provision *pvs;

    test_print_suite_title(1, "Empty iterator");

    start_case("Create iterator from empty provisioning list", log_strm);
    iter = provisioning_list_iterator_get(42);
    check_zero((int)(iter->next), "Initial next index should be 0");
    check_true(iter->cnt == 0, "iter should contain zero devices");
    check_null(iter->entries, "iter should have empty entry list");
    close_case("Create iterator from empty provisioning list");

    start_case("Run through empty iterator.", log_strm);
    for (ii = 0; ii < iter->cnt; ii++) {
        pvs = provisioning_list_iter_get_next(iter);
        check_true(0, "There should be no iterations on empty iterator");
    }
    close_case("Run through empty iterator.");

    start_case("Look up in empty iterator by next.", log_strm);
    pvs = provisioning_list_iter_get_next(iter);
    check_null(pvs, "Lookup in empty iterator should return NULL");
    close_case("Look up in empty iterator by next.");

    start_case("Look up in empty iterator by index 0.", log_strm);
    pvs = provisioning_list_iter_get_index(iter, 0);
    check_null(pvs, "Too high index should return NULL");
    close_case("Look up in empty iterator by index 0.");

    start_case("Look up in iterator by too high index.", log_strm);
    pvs = provisioning_list_iter_get_index(iter, 1);
    check_null(pvs, "Too high index should return NULL");
    close_case("Look up in iterator by too high index.");

    start_case("Delete empty iterator", log_strm);
    provisioning_list_iterator_delete(iter);
    close_case("Delete empty iterator");

}


/* Assumes dsk4 is in the list and not in position 0 or 1 */
static void test_new_iterator(void)
{
    provisioning_list_iter_t *iter;
    uint8_t cnt = provisioning_list_get_count();
    uint8_t ii, jj;
    uint8_t del_idx = cnt+1;
    struct provision *pvs;

    test_print_suite_title(1, "Iterator");

    start_case("Create iterator", log_strm);
    iter = provisioning_list_iterator_get(0xfedcba98);
    check_zero((int)iter->next, "Initial next index should be 0");
    test_print(3, "Cnt is %u, iter cnt is %u\n", cnt, iter->cnt);
    check_true(0xfedcba98 == iter->id, "Id is stored in iterator");
    check_true(iter->cnt == cnt, "iter should contain current num of devices");
    close_case("Create iterator");

    start_case("Run through iterator with next when an element has been deleted.", log_strm);
    provisioning_list_dev_remove(dsk4_len, dsk4);
    for (ii = 0; ii < iter->cnt; ii++) {
        pvs = provisioning_list_iter_get_next(iter);
        if (pvs) {
            test_print(2, "Found dsk in position %d: ", ii);
            if (verbosity > 2) {
                for (jj = 0; jj < pvs->dsk_len; jj++) {
                    printf("%02X", pvs->dsk[jj]);
                }
                printf("\n");
            }
        } else {
            del_idx = ii;
            test_print(2, "Position %d has been deleted\n", ii);
            check_true(iter->entries[ii].dsk_len == dsk4_len, "Iter has cached dsk length");
            check_zero(strncmp((char*)iter->entries[ii].dsk, (char*)dsk4, iter->entries[ii].dsk_len),
                       "Iter has cached dsk");
        }
    }
    check_true(iter->next == cnt, "iter should have exhausted all devices");
    close_case("Run through iterator with next when an element has been deleted.");

    start_case("Run through iterator when next is exhausted.", log_strm);
    pvs = provisioning_list_iter_get_next(iter);
    check_null(pvs, "NULL is returned when iterator is exhausted");
    close_case("Run through iterator when next is exhausted.");

    start_case("Look up in iterator by index.", log_strm);
    pvs = provisioning_list_iter_get_index(iter, 0);
    check_not_null(pvs, "index 0 should still be accessible");
    pvs = provisioning_list_iter_get_index(iter, 1);
    check_not_null(pvs, "index 1 should still be accessible");
    close_case("Look up in iterator by index.");

    start_case(" Look up deleted device by index.", log_strm);
    pvs = provisioning_list_iter_get_index(iter, del_idx);
    check_null(pvs, "Deleted index should still be in-accessible");
    close_case(" Look up deleted device by index.");

    start_case("Look up in iterator by too high index.", log_strm);
    pvs = provisioning_list_iter_get_index(iter, iter->cnt+1);
    check_null(pvs, "Too high index should return NULL");
    close_case("Look up in iterator by too high index.");

    start_case("Delete iterator", log_strm);
    provisioning_list_iterator_delete(iter);
    iter = NULL;
    close_case("Delete iterator");

    start_case("Look up in NULL iterator by next.", log_strm);
    pvs = provisioning_list_iter_get_next(iter);
    check_null(pvs, "Looking up in NULL iter returns NULL");
    close_case("Look up in NULL iterator by next.");

    start_case("Look up in NULL iterator by index.", log_strm);
    pvs = provisioning_list_iter_get_index(iter, 0);
    check_null(pvs, "Looking up in NULL iter returns NULL");
    close_case("Look up in NULL iterator by index.");

    start_case("Print with holes", log_strm);
    provisioning_list_print();
    close_case("Print with holes");

    // Since we deleted dsk4, the provisioning list should now have holes.
    start_case("Create iterator from provisioning list with holes", log_strm);
    cnt = provisioning_list_get_count();
    iter = provisioning_list_iterator_get(0x9987);
    check_zero((int)iter->next, "Initial next index should be 0");
    check_true(iter->cnt == cnt, "iter should contain current num of devices");
    close_case("Create iterator from provisioning list with holes");

    start_case("Run through iterator with next.", log_strm);
    for (ii = 0; ii < iter->cnt; ii++) {
        pvs = provisioning_list_iter_get_next(iter);
        check_not_null(pvs, "Look up pvs in iter");
        if (pvs) {
            test_print(3, "Found dsk in position %d: 0x", ii);
            for (jj = 0; jj < pvs->dsk_len; jj++) {
                test_print(3, "%02X", pvs->dsk[jj]);
            }
            test_print(3, "\n");
        }
    }
    check_true(iter->next == cnt, "iter should exhaust all devices");
    close_case("Run through iterator with next.");

    start_case("Run through DSKs in iterator.", log_strm);
    for (ii = 0; ii < iter->cnt; ii++) {
        test_print(3, "Read dsk in iter position %d: 0x", ii);
        for (jj = 0; jj < iter->entries[ii].dsk_len; jj++) {
            test_print(3, "%02X", iter->entries[ii].dsk[jj]);
        }
        test_print(3, "\n");
    }
    provisioning_list_iterator_delete(iter);
    close_case("Run through DSKs in iterator.");


    start_case("Create iterator with too large DSK", log_strm);
    provisioning_list_dev_set(dsklarge_len, dsklarge,
                              PVS_BOOTMODE_SMART_START);
    cnt = provisioning_list_get_count();
    iter = provisioning_list_iterator_get(0);
    check_true(iter->cnt == (cnt-1), "iter should not contain too large dsk");
    for (ii = 0; ii < iter->cnt; ii++) {
        test_print(3, "Read dsk in iter position %d: 0x", ii);
        for (jj = 0; jj < iter->entries[ii].dsk_len; jj++) {
            test_print(3, "%02X", iter->entries[ii].dsk[jj]);
        }
        test_print(3, "\n");
    }

    provisioning_list_iterator_delete(iter);
    close_case("Create iterator with too large DSK");
}


static void test_devices_errors(void)
{
    int res;
    uint8_t ii;
    struct provision *new_pvs1;

    test_print_suite_title(1, "Devices, error scenarios");

    start_case("Create device with undersized dsks (0-3)", log_strm);
    for (ii = 0; ii < 4; ii++)
    {
        new_pvs1 = provisioning_list_dev_add(ii, dsk1, PVS_BOOTMODE_S2);
        check_null(new_pvs1, "Adding undersized dsk fails");
        new_pvs1 = provisioning_list_dev_set(ii, dsk1, PVS_BOOTMODE_S2);
        check_null(new_pvs1, "Setting new device with undersized dsk fails");
    }
    close_case("Create device with undersized dsks (0-3)");

    start_case("Look up device with undersized dsks (0-3)", log_strm);
    new_pvs1 = provisioning_list_dev_set(dsk1_len, dsk1, PVS_BOOTMODE_S2);
    for (ii = 0; ii < 4; ii++)
    {
        new_pvs1 = provisioning_list_dev_get(ii, dsk1);
        check_null(new_pvs1, "Looking up with undersized dsk fails");
        new_pvs1 = provisioning_list_dev_set(ii, dsk1, PVS_BOOTMODE_S2);
        check_null(new_pvs1, "Setting existing device with undersized dsk fails");
    }
    close_case("Look up device with undersized dsks (0-3)");

    start_case("Remove device with undersized dsks (0-3)", log_strm);
    new_pvs1 = provisioning_list_dev_set(dsk1_len, dsk1, PVS_BOOTMODE_S2);
    for (ii = 0; ii < 4; ii++)
    {
        res = provisioning_list_dev_remove(ii, dsk1);
        check_true(res == PVS_ERROR, "Deleting with undersized dsk fails");
    }
    close_case("Remove device with undersized dsks (0-3)");
}

static void test_devices_basic(void)
{
    int res;
    struct provision *new_pvs1;
    struct provision *new_pvs2;
    struct provision *new_pvs4;
    struct provision *new_pvs5;
    struct provision *test_pvs = NULL;
    struct provision *test_pvs2 = NULL;

    test_print_suite_title(1, "Device provisions, basic");

    check_zero((int)provisioning_list_get_count(),
               "List should be empty");
    check_zero((int)provisioning_list_pending_count(),
               "Pending devices should be zero");

    start_case("Add device ---\n    Add DSK dsk1", log_strm);
    new_pvs1 = provisioning_list_dev_add(dsk1_len, dsk1, PVS_BOOTMODE_S2);
    /* Check that add worked */
    test_pvs = provisioning_list_dev_get(dsk1_len, dsk1);
    check_not_null((void*)test_pvs, "DSK 1 not found");
    check_true(new_pvs1 == test_pvs, "There is exactly one pointer to DSK 1");
    check_zero(strncmp((char*)test_pvs->dsk, (char*)dsk1, test_pvs->dsk_len),
               "dsk of device 1 has changed");
    check_true(provisioning_list_get_count() == 1,
               "List length should be 1");

    /* Negative test, look for non-existing device */
    test_pvs2 = provisioning_list_dev_get(dsk2_len, dsk2);
    check_null((void*)test_pvs2, "Uninitialized DSK2 found");
    close_case("Add device ---\n    Add DSK dsk1");

    // We now have one device in slot 1

    start_case("Add more than one device ---\n    Add DSK dsk2", log_strm);
    new_pvs2 = provisioning_list_dev_add(dsk2_len, dsk2, PVS_BOOTMODE_S2);
    /* Check that add worked */
    test_pvs2 = provisioning_list_dev_get(dsk2_len, dsk2);
    check_not_null((void*)test_pvs2, "DSK 2 not found");
    check_zero(new_pvs2 != test_pvs2, "DSK 2 has two pointers");
    check_true(provisioning_list_get_count() == 2,
               "List length should be 2");
    close_case("Add more than one device ---\n    Add DSK dsk2");

    // We now have two devices in slot 1 and 2

    start_case("Add device that already exists ---\n    Add DSK dsk2", log_strm);
    test_pvs = provisioning_list_dev_add(dsk2_len, dsk2, PVS_BOOTMODE_S2);
    check_null(test_pvs, "Added dsk2 twice");
    check_true(provisioning_list_get_count() == 2,
               "List length should be 2");
    close_case("Add device that already exists ---\n    Add DSK dsk2");

    // Nothing changed

    start_case("Add device with same dsk prefix", log_strm);
    new_pvs4 = provisioning_list_dev_add(dsk4_len, dsk4, PVS_BOOTMODE_SMART_START);
    check_not_null(new_pvs4, "Device added");
    check_zero(new_pvs4 == new_pvs2, "DSK4 did not get it's own entry.");
    check_true(provisioning_list_get_count() == 3,
               "List length should be 3");
    close_case("Add device with same dsk prefix");

    start_case("Get number of pending devices", log_strm);
    // We now have three devices in slot 1, 2, and 3, but 1 and 2 are S2
    check_true(provisioning_list_pending_count() == 1,
               "Pending devices should be 1");
    close_case("Get number of pending devices");

    start_case("Add device with same dsk prefix", log_strm);
    new_pvs5 = provisioning_list_dev_add(dsk6_len, dsk6, PVS_BOOTMODE_LONG_RANGE_SMART_START);
    check_not_null(new_pvs5, "Device added");
    check_zero(new_pvs5 == new_pvs2, "DSK4 did not get it's own entry.");
    check_true(provisioning_list_get_count() == 4,
               "List length should be 4");
    close_case("Add device with same dsk prefix");

    start_case("Get number of pending devices", log_strm);
    // We now have three devices in slot 1, 2, and 3, but 1 and 2 are S2
    check_true(provisioning_list_pending_count() == 2,
               "Pending devices should be 1");
    close_case("Get number of pending devices");


    start_case("Set status", log_strm);
    new_pvs4->status = PVS_STATUS_IGNORED;
    check_true(provisioning_list_pending_count() == 1,
               "Num. of Pending devices should be 0 after ignoring one");
    close_case("Set status");

    start_case("Set status on non-existing device", log_strm);
    new_pvs4->status = PVS_STATUS_IGNORED;
    check_true(provisioning_list_pending_count() == 1,
               "Num. of Pending devices should be 0 after ignoring one");
    close_case("Set status on non-existing device");

    start_case("Remove device that exists ---\n    Remove DSK dsk1", log_strm);
    res = provisioning_list_dev_remove(dsk1_len, dsk1);
    check_true(provisioning_list_get_count() == 3,
               "List length should be 3");
//    provisioning_list_print();
    close_case("Remove device that exists ---\n    Remove DSK dsk1");

    // We now have two devices in slot 2 and 3

    start_case("Look up device that  exists ---\n    Look up DSK dsk2", log_strm);
    /* Also verify removal */
    check_not_null((void*)provisioning_list_dev_get(dsk2_len, dsk2),
                   "DSK2 not found");
    close_case("Look up device that  exists ---\n    Look up DSK dsk2");

    /* Check that it is indeed removed */
    start_case("Look up device that does not exist ---\n    Look up DSK dsk1", log_strm);
    check_null((void*)provisioning_list_dev_get(dsk1_len, dsk1),
               "DSK1 should have been removed");
    close_case("Look up device that does not exist ---\n    Look up DSK dsk1");

    start_case("Remove device that does not exist ---\n    Remove DSK dsk1 again", log_strm);
    res = provisioning_list_dev_remove(dsk1_len, dsk1);
    check_true(res == PVS_ERROR,
               "Removing non-existent device should fail");
    check_true(provisioning_list_get_count() == 3,
               "List length should be 3");
//    provisioning_list_print();
    close_case("Remove device that does not exist ---\n    Remove DSK dsk1 again");

    start_case("Look up device that  exists ---\n    Look up DSK dsk2", log_strm);
    /* Also verify removal */
    check_not_null((void*)provisioning_list_dev_get(dsk2_len, dsk2),
                   "DSK2 not found");
    close_case("Look up device that  exists ---\n    Look up DSK dsk2");

    /* Check that it is indeed removed */
    start_case("Look up device that does not exist ---\n    Look up DSK dsk1", log_strm);
    check_null((void*)provisioning_list_dev_get(dsk1_len, dsk1),
               "DSK1 should have been removed");
    close_case("Look up device that does not exist ---\n    Look up DSK dsk1");

    // Nothing changed

    start_case("Modify device that exists ---\n    Modify DSK dsk2", log_strm);
    test_pvs = provisioning_list_dev_set(dsk2_len, dsk2, PVS_BOOTMODE_SMART_START);
    check_not_null((void*)test_pvs, "DSK 2 modification failed");
    close_case("Modify device that exists ---\n    Modify DSK dsk2");

    start_case("Modify device that does not exist ---\n    Modify DSK dsk2", log_strm);
    test_print(2, "\n\nRemove DSK dsk2\n");
    res = provisioning_list_dev_remove(dsk2_len, dsk2);
    check_true(res == PVS_SUCCESS,
               "Removing DSK2 should succeed");
    /* Search dsk2 */
    check_null((void*)provisioning_list_dev_get(dsk2_len, dsk2),
               "DSK 2 should have been removed");

    // We now have one device in slot 3

    test_pvs = provisioning_list_dev_set(dsk2_len, dsk2, PVS_BOOTMODE_S2);
    check_not_null((void*)test_pvs, "DSK 2 set failed to create entry");
    close_case("Modify device that does not exist ---\n    Modify DSK dsk2");
}

static void test_tlv_basic(void)
{
    struct provision *new_pvs = provisioning_list_dev_add(dsk1_len, dsk1, PVS_BOOTMODE_S2);
    struct pvs_tlv *tmp_tlv;
    uint16_t ii;

    test_print_suite_title(1, "TLV, basic");

//    test_provisioning_list_tlv_list(new_pvs);

    check_not_null(new_pvs, "Create pvs for tlv cases");
    if (new_pvs) {
        test_provisioning_list_tlv_list(new_pvs);

        test_print_suite_title(1, "TLV, advanced");
        start_case("Test Danish", log_strm);
//    provisioning_list_tlv_set(new_pvs, PVS_TLV_TYPE_NAME, strlen((char*)name1)+1, name1);
        provisioning_list_tlv_set(new_pvs, PVS_TLV_TYPE_LOCATION, (uint8_t)strlen((char*)location1)+1, location1);

        /* Add some more tlvs before delete */
        provisioning_list_tlv_set(new_pvs, PVS_TLV_TYPE_NAME, (uint8_t)strlen((char*)name2)+1, name2);
        close_case("Test Danish");

        test_print_suite_title(1, "Use of all possible types in tlv type");
        start_case("Add", log_strm);
        for (ii = 0; ii < 256; ii++)
        {

            provisioning_list_tlv_set(new_pvs, ii, (uint8_t)strlen((char*)name2)+1, name2);
        }
        // Smoke test the new settings.
        tmp_tlv = provisioning_list_tlv_get(new_pvs, PVS_TLV_TYPE_LOCATION);
        check_true((int)tmp_tlv->length == strlen((char*)name2)+1, "Location tlv has been overwritten");
        if (verbosity > 1) {
            provisioning_list_print();
        }
        close_case("Add");

        start_case("Modify", log_strm);
        for (ii = 0; ii < 256; ii++)
        {

            provisioning_list_tlv_set(new_pvs, ii, (uint8_t)strlen((char*)location1)+1, location1);
        }
        // Smoke test the new settings.
        tmp_tlv = provisioning_list_tlv_get(new_pvs, PVS_TLV_TYPE_NAME);
        check_true((int)tmp_tlv->length == strlen((char*)location1)+1, "Name tlv has been overwritten");
        close_case("Modify");

        start_case("Remove", log_strm);
        test_print(3, "Remove a few items in random places\n");
        provisioning_list_tlv_remove(new_pvs, 137);
        provisioning_list_tlv_remove(new_pvs, 14);
        provisioning_list_tlv_remove(new_pvs, 255);

        for (ii = 0; ii < 256; ii++)
        {
            provisioning_list_tlv_remove(new_pvs, ii);
        }
#ifdef PVS_TEST
        check_true(new_pvs->num_tlvs == 0, "Deleted all tlvs");
#endif
        close_case("Remove");
    }
}

static void test_provisioning_list_tlv_list(struct provision *pvs)
{
    int res = 0;

    static char *new_name = "Something-else. with =-;'\"";
    static char *new_id = "String id";
    struct pvs_tlv *tlv = NULL;
    uint8_t unknown_tlv_type = 99;
    struct pvs_tlv *test_tlv1 = NULL;
    struct pvs_tlv *test_tlv2 = NULL;
    struct provision *tmp_pvs;

    uint8_t tmp_dsk[16] = {88, 90, 128, 00,
                           17, 3, 10, 245,
                           255, 0, 192, 168,
                           10, 10, 5, 9};
    char *tmp_name = "I am a really long name (aka: more than 128 chars); that some weirdo has decided is a good name for his device, without considering the ensuing IT problems that he might trigger (or a thorough system tester)";
    struct pvs_tlv *tmp_tlv;

    start_case("Remove TLV that does not exist when there are no tlvs", log_strm);
    res = provisioning_list_tlv_remove(pvs, PVS_TLV_TYPE_NAME);
    check_true(res == PVS_ERROR, "Removing non-existing tlv should return error");
    close_case("Remove TLV that does not exist when there are no tlvs");

    start_case("Add TLV to DSK ---\n    Creating new tlv", log_strm);
    res = provisioning_list_tlv_set(pvs, PVS_TLV_TYPE_NAME,
                                    (uint8_t)strlen((char*)name1)+1, (uint8_t*)name1);
    close_case("Add TLV to DSK ---\n    Creating new tlv");

    /* Check that add worked */
    start_case("Look up tlv by pointer", log_strm);
    test_tlv1 = provisioning_list_tlv_get(pvs, PVS_TLV_TYPE_NAME);
    check_not_null((void*)test_tlv1,
                   "New tlv should be returned from get (by pointer)");
    close_case("Look up tlv by pointer");

    start_case("Look up tlv by dsk", log_strm);
    test_tlv2 = provisioning_list_tlv_dsk_get(dsk1_len, dsk1, PVS_TLV_TYPE_NAME);
    check_zero(test_tlv1 != test_tlv2,
               "New tlv should be returned from get by dsk");
    close_case("Look up tlv by dsk");

    start_case("Look up tlv by non-existing dsk", log_strm);
    test_tlv2 = provisioning_list_tlv_dsk_get(sizeof(dsk5), dsk5, PVS_TLV_TYPE_NAME);
    check_null(test_tlv2,
               "Getting a tlv using a non-exising dsk fails.");
    close_case("Look up tlv by non-existing dsk");

    start_case("Add second TLV ---\n    Creating location tlv", log_strm);
    provisioning_list_tlv_set(pvs, PVS_TLV_TYPE_LOCATION,
                              (uint8_t)strlen((char*)location1)+1, location1);
    check_not_null((void*)provisioning_list_tlv_get(pvs, PVS_TLV_TYPE_LOCATION),
                   "new tlv should be found");
    close_case("Look up tlv by non-existing dsk");

    start_case("Add TLV with length 0", log_strm);
    res = provisioning_list_tlv_set(pvs, PVS_TLV_TYPE_PRODUCT_ID,
                                    0, location1);
    check_true(res == PVS_ERROR, "Adding TLV with length 0 gives error");
    res = provisioning_list_tlv_set(pvs, PVS_TLV_TYPE_PRODUCT_ID,
                                    0, NULL);
    check_true(res == PVS_ERROR, "Adding TLV with length 0 gives error");
    // insert bogus test to test testframework
//    check_true(res != PVS_ERROR, "not really");
//    check_zero(res == PVS_ERROR, "not really");
//    check_null(pvs, "weird text");
//    check_not_null(NULL, "weirder text");
    res = provisioning_list_tlv_set(pvs, PVS_TLV_TYPE_PRODUCT_ID,
                                    1, NULL);
    check_true(res == PVS_ERROR, "Adding TLV with null pointer gives error");
    close_case("Add TLV with length 0");

    start_case("Look up new TLV by device pointer", log_strm);
    test_tlv1 = provisioning_list_tlv_get(pvs, PVS_TLV_TYPE_NAME);
    check_not_null((void*)test_tlv1,
                   "New tlv should be returned from get (by pointer)");
    close_case("Look up new TLV by device pointer");

    test_print_suite_title(1, "Use of unknown types in tlv type");
    start_case("Add unknown TLV", log_strm);
    provisioning_list_tlv_set(pvs, unknown_tlv_type, 4, location1);
    close_case("Add unknown TLV");

    start_case("Look up unknown TLV", log_strm);
    test_tlv2 = provisioning_list_tlv_get(pvs, unknown_tlv_type);
    check_not_null(test_tlv2,
                   "Unknown tlv should be findable");
    check_true(test_tlv2->length == 4, "Length of unknown type is 4");
    close_case("Look up unknown TLV");

    start_case("Remove TLV that does not exist when there are some tlvs", log_strm);
    res = provisioning_list_tlv_remove(pvs, PVS_TLV_TYPE_PRODUCT_ID);
    check_true(res == PVS_ERROR, "Removing non-existing tlv should return error");
    close_case("Remove TLV that does not exist when there are some tlvs");

    start_case("Remove TLV from NULL device", log_strm);
    res = provisioning_list_tlv_remove(NULL, PVS_TLV_TYPE_NAME);
    check_true(res == PVS_ERROR, "Removing tlv from NULL device should return error");
    close_case("Remove TLV from NULL device");

    start_case("Add TLV to NULL device", log_strm);
    res = provisioning_list_tlv_set(NULL, PVS_TLV_TYPE_NAME, (uint8_t)strlen(new_name)+1, (uint8_t*)&new_name);
    check_true(res == PVS_ERROR, "Adding tlv to NULL device should return error");
    close_case("Add TLV to NULL device");

    start_case("Look up tlv by NULL pointer", log_strm);
    test_tlv2 = provisioning_list_tlv_get(NULL, PVS_TLV_TYPE_NAME);
    check_null((void*)test_tlv2,
               "Getting a TLV from a NULL pointer should return NULL");
    close_case("Look up tlv by NULL pointer");

    /* Invalid device */
    /* First create device and tlvs */
    start_case("Remove TLV from invalid (removed) device", log_strm);
    tmp_pvs = provisioning_list_dev_add(sizeof(tmp_dsk), tmp_dsk, PVS_BOOTMODE_S2);
    test_print(3, "Length of funny name %d\n", (uint8_t)strlen(tmp_name));
    provisioning_list_tlv_set(tmp_pvs, PVS_TLV_TYPE_NAME, (uint8_t)strlen(tmp_name) + 1, (uint8_t*)(tmp_name));
    provisioning_list_tlv_set(tmp_pvs, PVS_TLV_TYPE_LOCATION, (uint8_t)strlen((char*)location1) + 1, location1);
    tmp_tlv = provisioning_list_tlv_get(tmp_pvs, PVS_TLV_TYPE_NAME);
    check_zero(strncmp(tmp_name, (char*)tmp_tlv->value, tmp_tlv->length), "name tlv should be returned");
    check_true(strlen(tmp_name) + 1 == tmp_tlv->length, "name tlv has correct length");
    check_true(tmp_tlv->value[tmp_tlv->length-1] == 0, "name is null terminated");

    /* Then remove device and check tlv operations on it */
    provisioning_list_dev_remove(sizeof(tmp_dsk), tmp_dsk);
    res = provisioning_list_tlv_remove(tmp_pvs, PVS_TLV_TYPE_NAME);
    check_true(res == PVS_ERROR, "Removing tlv from non-existing device should return error");
    res = provisioning_list_tlv_remove(tmp_pvs, PVS_TLV_TYPE_PRODUCT_ID);
    check_true(res == PVS_ERROR, "Removing non-existing tlv from non-existing device should return error");
    res = provisioning_list_tlv_set(tmp_pvs, PVS_TLV_TYPE_LOCATION, (uint8_t)strlen((char*)location1) + 1, location1);
    check_true(res == PVS_ERROR, "Adding tlv to non-existing device should return error");
    close_case("Remove TLV from invalid (removed) device");

    start_case("Look up tlv by removed device", log_strm);
    test_tlv2 = provisioning_list_tlv_get(tmp_pvs, PVS_TLV_TYPE_NAME);
    check_null((void*)test_tlv2,
               "Getting a TLV from a removed device should return NULL");
    close_case("Look up tlv by removed device");

    start_case("Look up tlv by removed device DSK", log_strm);
    test_tlv2 = provisioning_list_tlv_dsk_get(sizeof(tmp_dsk), tmp_dsk, PVS_TLV_TYPE_NAME);
    check_null((void*)test_tlv2,
               "Getting a TLV from a removed device by DSK should return NULL");
    close_case("Look up tlv by removed device DSK");

    start_case("Get the critical bit of an elective tlv type", log_strm);
    check_true(provisioning_list_tlv_crt_flag(PVS_TLV_TYPE_LOCATION) == 0,
               "PVS_TLV_TYPE_LOCATION flag is 0");
    check_true(provisioning_list_tlv_crt_flag(PVS_TLV_TYPE_NAME) == 0,
               "PVS_TLV_TYPE_NAME flag is 0");
    check_true(provisioning_list_tlv_crt_flag(PVS_TLV_TYPE_PRODUCT_TYPE) == 0,
               "PVS_TLV_TYPE_PRODUCT_TYPE flag is 0");
    check_true(provisioning_list_tlv_crt_flag(PVS_TLV_TYPE_PRODUCT_ID) == 0,
               "PVS_TLV_TYPE_PRODUCT_ID flag is 0");
    check_true(provisioning_list_tlv_crt_flag(PVS_TLV_TYPE_MAX_INCL_REQ_INTERVAL) == 0,
               "PVS_TLV_TYPE_MAX_INCL_REQ_INTERVAL flag is 0");
    check_true(provisioning_list_tlv_crt_flag(PVS_TLV_TYPE_UUID16) == 0,
               "PVS_TLV_TYPE_UUID16 flag is 0");
    check_true(provisioning_list_tlv_crt_flag(PVS_TLV_TYPE_NETWORK_INFO) == 0,
               "PVS_TLV_TYPE_NETWORK_INFO flag is 0");
    close_case("Get the critical bit of an elective tlv type");

    start_case("Get the critical bit of a critical tlv type", log_strm);
    /* check_true(provisioning_list_tlv_crt_flag(PVS_TLV_TYPE_STATUS) == 1, */
    /*            "PVS_TLV_TYPE_STATUS flag is 1"); */
    /* check_true(provisioning_list_tlv_crt_flag(PVS_TLV_TYPE_BOOTSTRAP_MODE) == 1, */
    /*            "PVS_TLV_TYPE_BOOTSTRAP_MODE flag is 1"); */
    check_true(provisioning_list_tlv_crt_flag(PVS_TLV_TYPE_ADV_JOIN) == 1,
               "PVS_TLV_TYPE_ADV_JOIN flag is 1");
    close_case("Get the critical bit of a critical tlv type");

    // Back to original device - add more tlvs
    provisioning_list_tlv_set(pvs, PVS_TLV_TYPE_PRODUCT_ID,
                              (uint8_t)strlen(new_id)+1, (uint8_t*)new_id);
    provisioning_list_tlv_set(pvs, PVS_TLV_TYPE_PRODUCT_TYPE,
                              prod_type_len, prod_type);


    start_case("Modify TLV ---\n    Overwrite name TLV", log_strm);
    /* set the same type to stg else */
    res = provisioning_list_tlv_set(pvs, PVS_TLV_TYPE_NAME, (uint8_t)strlen(new_name)+1, (uint8_t*)new_name);
    check_true(res == PVS_SUCCESS, "Modify name TLV returns SUCCESS");
    /* look it up */
    tlv = provisioning_list_tlv_get(pvs, PVS_TLV_TYPE_NAME);
    check_not_null(tlv, "Find pointer to name tlv after modification");
    if (tlv) {
        check_zero(strncmp((char*)tlv->value, new_name, tlv->length),
                   "Name has changed to the new name.");
    }
    close_case("Modify TLV ---\n    Overwrite name TLV");

    start_case("Modify TLV not first in list ---\n    Overwrite product id TLV", log_strm);
    /* set the same type to stg else */
    res = provisioning_list_tlv_set(pvs, PVS_TLV_TYPE_PRODUCT_ID, (uint8_t)strlen(new_id)+1, (uint8_t*)new_id);
    /* look it up */
    tlv = provisioning_list_tlv_get(pvs, PVS_TLV_TYPE_PRODUCT_ID);
    check_not_null(tlv, "Find prod id tlv");
    if (tlv) {
        check_zero(strncmp((char*)tlv->value, new_id, tlv->length),
                   "Check that product id was modified correctly");
    }
    close_case("Modify TLV not first in list ---\n    Overwrite product id TLV");

    start_case("Delete TLV that exists ---\n    Delete name TLV", log_strm);
    res = provisioning_list_tlv_remove(pvs, PVS_TLV_TYPE_NAME);
    /* look up the deleted thing */
    tlv = provisioning_list_tlv_get(pvs, PVS_TLV_TYPE_NAME);
    check_null(tlv, "Name tlv has been deleted");

    /* delete it again, even though it does not exist */
    res = provisioning_list_tlv_remove(pvs, PVS_TLV_TYPE_NAME);
    check_true(res == PVS_ERROR, "Removing non-existing tlv should fail");
    close_case("Delete TLV that exists ---\n    Delete name TLV");

    /* delete all of them */
    test_print(2, "Remove all TLVs");
    test_print(2, "Add name TLV again (it will be first).\n");
    provisioning_list_tlv_set(pvs, PVS_TLV_TYPE_NAME, (uint8_t)strlen(new_name)+1, (uint8_t*)new_name);

    start_case("Remove first TLV and check that the others are still accessible", log_strm);
    provisioning_list_tlv_remove(pvs, PVS_TLV_TYPE_NAME);
    check_not_null(provisioning_list_tlv_get(pvs, PVS_TLV_TYPE_LOCATION), "Location is still there");
    check_not_null(provisioning_list_tlv_get(pvs, PVS_TLV_TYPE_PRODUCT_ID), "Prod id is still there");
    check_not_null(provisioning_list_tlv_get(pvs, PVS_TLV_TYPE_PRODUCT_TYPE), "Prod type is still there");
    check_null(provisioning_list_tlv_get(pvs, PVS_TLV_TYPE_NAME), "Name is deleted");
    close_case("Remove first TLV and check that the others are still accessible");

    start_case("Remove last TLV and check that the others are still accessible and the list is terminated", log_strm);
    provisioning_list_tlv_remove(pvs, PVS_TLV_TYPE_LOCATION);
    check_not_null(provisioning_list_tlv_get(pvs, PVS_TLV_TYPE_PRODUCT_ID), "Prod id is still there");
    check_not_null(provisioning_list_tlv_get(pvs, PVS_TLV_TYPE_PRODUCT_TYPE), "Prod type is still there");
    check_null(provisioning_list_tlv_get(pvs, PVS_TLV_TYPE_NAME), "Name is deleted");
    close_case("Remove last TLV and check that the others are still accessible and the list is terminated");

    start_case("Remove TLV from the middle and check that the others are still accessible", log_strm);
    provisioning_list_tlv_remove(pvs, PVS_TLV_TYPE_PRODUCT_ID);
    check_null(provisioning_list_tlv_get(pvs, PVS_TLV_TYPE_NAME), "Name is deleted");
    check_not_null(provisioning_list_tlv_get(pvs, unknown_tlv_type), "Unknown is still there");
    close_case("Remove TLV from the middle and check that the others are still accessible");

    provisioning_list_tlv_remove(pvs, PVS_TLV_TYPE_PRODUCT_TYPE);

    start_case("Remove unknown TLV", log_strm);
    provisioning_list_tlv_remove(pvs, unknown_tlv_type);
#ifdef PVS_TEST
    check_true(pvs->num_tlvs == 0, "Delete all tlvs");
#endif
    close_case("Remove unknown TLV");
    return;
}


/* Test homeid lookup with some funky ids.
 *
 * Homeid should match byte 8 to 11, except that the first two bits of
 * these are always 1 in a home id and the last bit is ignored.  */
static void test_provisioning_list_homeid(void)
{
    struct provision *pvs1;
    struct provision *pvs2;
    struct provision *tmppvs1;
    struct provision *tmppvs2;

    test_print_suite_title(1, "Home id");

    // Assume the provisioning list contains plenty of short dsks at this point
    start_case("Look up device with too short dsk by homeid", log_strm);
    tmppvs1 = provisioning_list_dev_get_homeid(homeid1);
    check_null(tmppvs1, "homeid 1 does not match any of the short dsks");
    close_case("Look up device with too short dsk by homeid");

    start_case("Lookup device by homeid\n    Look up device by homeid that only matches because of the masking of the last bit\n    Look up device by homeid that only matches because of the masking of the second bit", log_strm);
    pvs1 = provisioning_list_dev_add(sizeof(dskh1), dskh1, PVS_BOOTMODE_SMART_START);
    tmppvs1 = provisioning_list_dev_get_homeid(homeid1);
    check_true(pvs1 == tmppvs1, "homeid 1 matches dskh1");
    close_case("Lookup device by homeid\n    Look up device by homeid that only matches because of the masking of the last bit\n    Look up device by homeid that only matches because of the masking of the second bit");

    start_case("Look up device by homeid that does not match", log_strm);
    tmppvs2 =  provisioning_list_dev_get_homeid(homeid2);
    check_null(tmppvs2, "homeid 2 does not match dskh1");
    tmppvs2 =  provisioning_list_dev_get_homeid(homeid3);
    check_null(tmppvs2, "homeid 3 does not match dskh1");
    close_case("Look up device by homeid that does not match");

    start_case("Look up device by homeid that only matches because of the 4 bytes limit", log_strm);
    tmppvs1 =  provisioning_list_dev_get_homeid(homeidlong);
    check_true(pvs1 == tmppvs1, "homeidlong matches dskh1");
    close_case("Look up device by homeid that only matches because of the 4 bytes limit");

    start_case("Look up device by homeid that only matches because of the masking of the first bit", log_strm);
    pvs2 = provisioning_list_dev_add(sizeof(dskh2), dskh2, PVS_BOOTMODE_SMART_START);
    tmppvs2 = provisioning_list_dev_get_homeid(homeid2);
    check_true(pvs2 == tmppvs2, "homeid 2 matches dskh2");
    close_case("Look up device by homeid that only matches because of the masking of the first bit");

    provisioning_list_clear();
}

/* Test the provisioning_list_status_set() function.
 *
 * Test approach:
 *  - add a provision and verify we can change the status.
 *  TODO: Verify it is actually persisted.
 *
 */
static void test_provisioning_list_pseudo_tlv_set(void)
{
    struct provision *pvs = provisioning_list_dev_add(sizeof(dsk5), dsk5, PVS_BOOTMODE_S2);
    int res = 0;
    uint8_t nodeid5 = 6;

    test_print(1, "\n === Setting up PVL ===\n");

    test_print(2, "\n--- Add DSK dsk5\n");
    provisioning_list_tlv_set(pvs, PVS_TLV_TYPE_NAME, (uint8_t)strlen((char*)name3)+1, name3);
    provisioning_list_tlv_set(pvs, PVS_TLV_TYPE_LOCATION, (uint8_t)strlen((char*)location3)+1, location3);

    check_true(pvs->status == PVS_STATUS_PENDING, "default status is pending");

    provisioning_list_status_set(pvs, PVS_STATUS_PASSIVE);
    check_true(pvs->status == PVS_STATUS_PASSIVE, "status changed to PASSIVE");

    start_case("Set status on non-existing device", log_strm);
    provisioning_list_status_set(NULL, PVS_STATUS_PASSIVE);
    check_true(pvs->status == PVS_STATUS_PASSIVE, "status changed to PASSIVE");
    close_case("Set status on non-existing device");

   start_case("Set bootmode", log_strm);
   res = provisioning_list_bootmode_set(pvs, PVS_BOOTMODE_SMART_START);
   check_true(res == PVS_SUCCESS, "Setting bootmode on valid device should return success");
   check_true(pvs->bootmode==PVS_BOOTMODE_SMART_START, "Bootmode should now be smart start");
   close_case("Set bootmode");

   start_case("Set bootmode on non-existing device", log_strm);
   res = provisioning_list_bootmode_set(NULL, PVS_BOOTMODE_S2);
   check_true(res == PVS_ERROR, "Setting bootmode on NULL should give error");
   close_case("Set bootmode on non-existing device");

}
/**
 * @}
 */
