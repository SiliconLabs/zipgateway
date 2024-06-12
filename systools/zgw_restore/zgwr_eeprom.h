/* © 2019 Silicon Laboratories Inc. */

#ifndef _ZGWR_EEPROM_H
#define _ZGWR_EEPROM_H

/** \defgroup eeprom-writer Z/IP Gateway Resource Directory File Generator for Restore Tool
 * Write the eeprom.dat file from the data in zip_gateway_backup_data::zgw.
 * \ingroup zgw-restore
@{
 */

#ifndef RESTORE_EEPROM_FILENAME
/** Restore eeprom filename */
#define RESTORE_EEPROM_FILENAME "zipgateway.db"
#endif
/**
 * Write data from the global storage in the eeprom.dat format.
 *
 * Path to the EEPROM file is found in the configuration structure.
 */
int zgw_restore_eeprom_file(void);

/**
@}
*/

#endif
