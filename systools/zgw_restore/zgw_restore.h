/* © 2019 Silicon Laboratories Inc. */

#ifndef _ZGW_RESTORE_H
#define _ZGW_RESTORE_H

#include <lib/zgw_log.h>
zgw_log_id_declare(zgwr);

/** \ingroup systools
 * \defgroup zgw-restore ZGW Restore Tool
 *
 * From a Z/IP Gateway migration package, this tool restores the state
 * of a (stopped) Z/IP Gateway process (ZGW) by creating and populating
 * the ZGW storage files with state and network data.
 *
 * The Restore Tool uses a restored bridge controller and a
 * backup/migration Z/IP Gateway data file (\ref zgw_json_schema) to
 * generate the persistent runtime state of a Z/IP Gateway process.
 */

typedef enum zgw_restore_field_status {
   zgwr_field_has_default = 0, /**< Initialized. */
   zgwr_field_from_serial = 1, /**< Read from serial_api. */
   zgwr_field_from_zw_json = 2, /**< Read from controller JSON file.*/
   zgwr_field_from_zgw_json = 3, /**< Read from ZGW JSON file.*/
   zgwr_field_deduced = 4 /**< Computed from the values of other fields. */
} zgw_restore_field_status_t;

#endif
