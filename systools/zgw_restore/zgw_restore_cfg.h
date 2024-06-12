/* © 2019 Silicon Laboratories Inc. */

#ifndef _ZGW_RESTORE_CFG_H
#define _ZGW_RESTORE_CFG_H

/**
 * \ingroup zgw-restore
@{
*/

/**
 * Configuration parameters for the restore program.
 */
struct ZGW_restore_config {
   const char *json_filename;
   const char *serial_port;
   const char *installation_path;
   const char *data_path;
   const char *zgw_cfg_filename;
};

typedef struct ZGW_restore_config ZGW_restore_config_t;

extern ZGW_restore_config_t restore_cfg;

void cfg_init(void);

const char *serial_port_get(void);
const char *json_filename_get(void);
/**
 * Return the location of the /etc directory og the gw.
 */
const char *installation_path_get(void);
/**
 * Return the location of ZGW datafiles, eeprom.dat and provisioning_list_store.dat.
 */
const char * data_dir_path_get(void);

/** @} */
#endif
