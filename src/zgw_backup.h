#ifndef ZGW_BACKUP_H
#define ZGW_BACKUP_H

/**
 * \ingroup ZIP_Router
 * \defgroup zgw_backup Zipgateway back-up component.
 *
 * @{
 */

/** Send a "backup failed" message to the backup script. */
void zgw_backup_send_failed(void);

/** Executes the backup.
 *
 * The zipgateway must be in idle state before calling this function.
 *
 *  - closes all DTLS connections
 *  - Stops the radio
 *  - Copies the Z-Wave NVM 
 *  - Tuns on the radio again / Resets the module
 *  - crates manifest file with following format
 *
 *  GW_VERSION="version"
 *  ZW_PROTOCOL_VERSION="version"
 *  GW_TIMESTAMP="timestamp"
 *  GW_CONFIG_FILE_PATH="path"
 *  GW_ZipCaCert="path"
 *  GW_ZipCert="path"
 *  GW_ZipPrivKey="path"
 *  GW_ZipNodeIdentifyScript="path"
 *  GW_PVSStorageFile="path"
 *  GW_ProvisioningConfigFile="path"
 *  GW_Eepromfile="path"
 *  ZW_NVM_FILE="path"
 *
 * Stores all backup contents to file with names stores in bkup_dir variable, which is sent
 * by backup script
 *
 * On completion, "backup done" is sent to the backup script that
 * requested the backup.
 *
 * If something fails in the backup, "backup failed" is sent to the
 * backup script.
 *
 */
void zgw_backup_now(void);

/** Initialize the communication file (zgw_backup_communication_file) 
 *
 * If initialization fails, "backup failed" is sent to backup script which
 * triggers the backup
 */ 
int zgw_backup_init(void);

/** @} */
#endif
