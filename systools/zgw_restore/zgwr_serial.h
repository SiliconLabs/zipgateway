/* © 2019 Silicon Laboratories Inc. */


#ifndef _ZGWR_SERIAL_H
#define _ZGWR_SERIAL_H

#include "zgw_data.h"

int zgwr_serial_init(void);

void zgwr_serial_close(void);

/** \defgroup zgwr-serial Serial access for the ZGW Restore Tool
 * Read the bridge controller data into zip_gateway_backup_data::controller and write the ZGW data to the NVM application area.
 * \ingroup zgw-restore
@{
 */

/**
 * Read required zgw-data from the newly restored chip.
 *
 * Read protocol data for the controller and the nodes over the serial
 * if.  Insert this into the internal data structure of restore.
*/
int zgw_restore_serial_data_read(zw_controller_t **);

/**
 * Write Appl NVM from internal data.
 */
int zgw_restore_nvm_config_write(void);

/**
@}
*/
#endif
