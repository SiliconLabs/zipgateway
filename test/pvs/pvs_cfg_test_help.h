/* © 2017 Silicon Laboratories Inc.
 */
#ifndef PVS_CFG_TEST_HELP_H_
#define PVS_CFG_TEST_HELP_H_

#include "test_helpers.h"
#include "provisioning_list.h"
#include "provisioning_list_files.h"
#include "pvs_cfg.h"
#include <lib/zgw_log.h>

zgw_log_id_define(pvs_test);

/* The provisioning list never reads the file after init, so we can
 * just rename it to remove it (or to store a snapshot). */
void steal_file(const char *oldname, const char *newname);

void check_file_exists(const char *filename, const char *msg);

/* PVS text cfg specific validators */
void check_device(struct provision *pvs, uint8_t ii);
void check_tlv(struct pvs_tlv *template, struct pvs_tlv *tlv);

#endif
