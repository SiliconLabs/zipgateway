/* © 2014 Silicon Laboratories Inc.
 */
#include <string.h>
#include "test_helpers.h"
#include "provisioning_list.h"
#include "provisioning_list_files.h"
#include "pvs_cfg.h"

/* Misc pvs test helpers */

/* Test data */

uint8_t happy_bootmode[14] = {PVS_BOOTMODE_S2, PVS_BOOTMODE_SMART_START, PVS_BOOTMODE_SMART_START, PVS_BOOTMODE_SMART_START,
                            PVS_BOOTMODE_S2, PVS_BOOTMODE_S2, PVS_BOOTMODE_S2, PVS_BOOTMODE_S2,
                            PVS_BOOTMODE_SMART_START, PVS_BOOTMODE_S2, PVS_BOOTMODE_S2, PVS_BOOTMODE_SMART_START,
                            PVS_BOOTMODE_S2, PVS_BOOTMODE_S2
};

static uint8_t val_ni_ch[] = {0xe4, 0xbd, 0xa0, 0x00};

struct pvs_tlv tlv_name_lamp = {NULL, PVS_TLV_TYPE_NAME, 11, (uint8_t*)"loftslampe"};
struct pvs_tlv tlv_name_sensor = {NULL, PVS_TLV_TYPE_NAME, 7, (uint8_t*)"sensor"};
struct pvs_tlv tlv_name_lamp2 = {NULL, PVS_TLV_TYPE_NAME, 10, (uint8_t*)"bordlampe"};
struct pvs_tlv tlv_name_child = {NULL, PVS_TLV_TYPE_NAME, 28, (uint8_t*)"This is *mine*!! Hands off!"};
struct pvs_tlv tlv_name_ni_ch = {NULL, PVS_TLV_TYPE_NAME, 4, val_ni_ch};

uint8_t val_name_long[] = "01234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123";
struct pvs_tlv tlv_name_long = {NULL, PVS_TLV_TYPE_NAME, 255, val_name_long};

struct pvs_tlv tlv_254_empty = {NULL, 254, 1, (uint8_t*)""};
struct pvs_tlv tlv_255_empty = {NULL, 254, 1, (uint8_t*)""};

struct pvs_tlv tlv_loc_ni_ch = {NULL, PVS_TLV_TYPE_LOCATION, 4, val_ni_ch};
struct pvs_tlv tlv_loc_bedroom_dk = {NULL, PVS_TLV_TYPE_LOCATION, 13, (uint8_t*)"soveværelse"};
struct pvs_tlv tlv_loc_thebedroom_dk = {NULL, PVS_TLV_TYPE_LOCATION, 14, (uint8_t*)"soveværelset"};
struct pvs_tlv tlv_loc_bedroom_fr = {NULL, PVS_TLV_TYPE_LOCATION, 17, (uint8_t*)"Chambre d'Irène"};
struct pvs_tlv tlv_loc_childs_room_dk = {NULL, PVS_TLV_TYPE_LOCATION, 15, (uint8_t*)"Børneværelse"};

struct pvs_tlv tlv_loc_oakchest_fr = {NULL, PVS_TLV_TYPE_LOCATION, 18, (uint8_t*)"Commode en chêne"};
struct pvs_tlv tlv_loc_cottage_q_dk = {NULL, PVS_TLV_TYPE_LOCATION, 22, (uint8_t*)"Sommerhus, 'Hornbæk'"};
struct pvs_tlv tlv_loc_cottage_dk = {NULL, PVS_TLV_TYPE_LOCATION, 19, (uint8_t*)"Sommerhus Hornbæk"};
struct pvs_tlv tlv_loc_cottage_nl_dk = {NULL, PVS_TLV_TYPE_LOCATION, 20, (uint8_t*)"Sommerhus\n Hornbæk"};
struct pvs_tlv tlv_loc_cottage_backslash_dk = {NULL, PVS_TLV_TYPE_LOCATION, 20, (uint8_t*)"Sommerhus\\nHornbæk"};
struct pvs_tlv tlv_loc_cottage_tab_dk = {NULL, PVS_TLV_TYPE_LOCATION, 19, (uint8_t*)"Sommerhus	Hornbæk"};
uint8_t val_fuwa[] = {0xe5, 0xaf, 0x8c, 0xe5, 0x8d, 0x8e, 0x20, 0xe9, 0xa5, 0xad, 0xe5, 0xba, 0x97, 0x00};
static struct pvs_tlv tlv_loc_restaurant_ch =  {NULL, PVS_TLV_TYPE_LOCATION, 14, val_fuwa};

uint8_t val_sl_C[] = {0xc2, 0xa9, 0x20, 0x53, 0x69, 0x6c, 0x69, 0x63, 0x6f, 0x6e, 0x20, 0x4c, 0x61, 0x62, 0x73, 0x00};
struct pvs_tlv tlv_loc_sl = {NULL, PVS_TLV_TYPE_LOCATION, 16, val_sl_C};

uint8_t val_hex_54[] = {0x54, 0xff, 0x37, 0x81, 0x00};
uint8_t val_hex_5401[] = {0x54, 0xFF, 0x37, 0x81, 0x01};
struct pvs_tlv tlv_prodid_54 = {NULL, PVS_TLV_TYPE_PRODUCT_ID, 5, val_hex_54};
struct pvs_tlv tlv_prodid_5401 = {NULL, PVS_TLV_TYPE_PRODUCT_ID, 5, val_hex_5401};
struct pvs_tlv tlv_0_54 = {NULL, 0, 5, val_hex_54};
struct pvs_tlv tlv_255_54 = {NULL, 255, 5, val_hex_54};

struct pvs_tlv *happy_tlvs0[] = {NULL};
struct pvs_tlv *happy_tlvs1[] = {&tlv_prodid_54, &tlv_loc_thebedroom_dk, &tlv_name_lamp, NULL};
struct pvs_tlv *happy_tlvs2[] = {&tlv_prodid_54, &tlv_name_lamp2, &tlv_loc_oakchest_fr,
                                 &tlv_254_empty, &tlv_255_54, &tlv_0_54, NULL};
struct pvs_tlv *happy_tlvs3[] = {&tlv_prodid_5401, &tlv_loc_ni_ch, NULL};
struct pvs_tlv *happy_tlvs4[] = {&tlv_loc_cottage_q_dk, &tlv_name_ni_ch, NULL};
struct pvs_tlv *happy_tlvs5[] = {&tlv_loc_sl, &tlv_name_sensor, NULL};
struct pvs_tlv *happy_tlvs6[] = {&tlv_loc_childs_room_dk, &tlv_name_child, NULL};
struct pvs_tlv *happy_tlvs7[] = {&tlv_loc_cottage_tab_dk, &tlv_name_long, NULL};
struct pvs_tlv *happy_tlvs8[] = {&tlv_prodid_54, NULL};
struct pvs_tlv *happy_tlvs9[] = {&tlv_loc_cottage_nl_dk, &tlv_name_sensor, NULL};
struct pvs_tlv *happy_tlvs10[] = {&tlv_loc_cottage_backslash_dk, &tlv_name_sensor, NULL};
struct pvs_tlv *happy_tlvs11[] = {&tlv_0_54, &tlv_loc_bedroom_fr, &tlv_name_lamp, NULL};
struct pvs_tlv *happy_tlvs12[] = {&tlv_loc_childs_room_dk, &tlv_name_child, NULL};
struct pvs_tlv *happy_tlvs13[] = {&tlv_loc_restaurant_ch, &tlv_name_long, NULL};
struct pvs_tlv *happy_tlvs14[] = {NULL};

struct pvs_tlv **tlvs[] = {happy_tlvs0, happy_tlvs1, happy_tlvs2, happy_tlvs3,
                           happy_tlvs4, happy_tlvs5, happy_tlvs6, happy_tlvs7,
                           happy_tlvs8, happy_tlvs9, happy_tlvs10, happy_tlvs11,
                           happy_tlvs12, happy_tlvs13, happy_tlvs14};




/* Validators */

void steal_file(const char *oldname, const char *newname){
    test_print(3, "Steal file %s\n", oldname);
    (void)rename(oldname, newname);
}


void check_file_exists(const char *filename, const char *msg)
{
    FILE *strm;

    strm = fopen(filename, "r");
    if (strm) {
        test_print(2, "PASS - found %s. %s\n", filename, msg);
        (void)fclose(strm);
    } else {
        test_print(0, "FAIL - did not find %s. %s\n", filename, msg);
        numErrs++;
    }
}

void check_tlv(struct pvs_tlv *template, struct pvs_tlv *tlv)
{
    check_not_null(tlv, "TLV exists");
    if (tlv == NULL) {
        return;
    }
    test_print(3, "Compare lengths, got %u, expected %u\n", tlv->length, template->length);
    check_true(tlv->length == template->length, "Length is correct");
    if (tlv->type == 100 || tlv->type == 101) {
        int res = strcmp((char*)(tlv->value), (char*)(template->value));
        test_print(3, "Compare %s and %s\n", tlv->value, template->value);
        check_true(res==0, "String is imported correctly");
    } else {
        int res = strncmp((char*)(tlv->value), (char*)(template->value), tlv->length);
        check_true(res==0, "Value is imported correctly");
    }
}

void check_device(struct provision *pvs, uint8_t ii)
{
    struct pvs_tlv **templates;
    uint8_t jj = 0;

    test_print(3, "Checking device %d\n", ii);
    check_not_null(pvs, "DSK imported");

    if (pvs == NULL) {
        return;
    }
    check_true(pvs->bootmode == happy_bootmode[ii], "Boot mode imported");
    templates = tlvs[ii];
    while (templates[jj] != NULL) {
        test_print(3, "Check tlv type %u\n", templates[jj]->type);
        check_tlv(templates[jj], provisioning_list_tlv_get(pvs, templates[jj]->type));
        jj++;
        /* Negative test, no examples have type 17*/
        check_null(provisioning_list_tlv_get(pvs, 17), "Incorrect tlv not imported.");
        if (11 == ii) {
            check_null(provisioning_list_tlv_get(pvs, 1), "Incorrect tlv 0 not imported.");
        }
    }
}
