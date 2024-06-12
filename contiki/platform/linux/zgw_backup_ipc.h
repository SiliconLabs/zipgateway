/* © 2020 Silicon Laboratories Inc. */

/** Truncate the contents of file \p filename.
 *
 * Open the file with read/write/truncate and close it again.
 *
 * \param filename The full pathname of the file.
 * \return true if open succeeds, false otherwise.
 */
int zgw_file_truncate(const char *filename);

/** Send a message to the process that requested a backup.
 *
 * The process is identified by the PID read from the communication
 * file at the start of backup.
 *
 * Delete the provious contents of the predefined backup
 * communication file.  Write \p msg instead.
 *
 * Send SIGUSR1 to the PID.  If the file cannot be opened, an error is
 * written to the gateway log and the signal is still sent.
 *
 * \param msg The string to write to the file.
 */
void zgw_backup_send_msg(const char* msg);

/** Initialize the communication channel used by zipgateway to report status on a backup request.
 *
 * This function also reads the backup directory from the
 * communication channel and returns it in \p bkup_dir.
 *
 * \param bkup_dir Return parameter. The directory where the zipgateway should create the backup.
 *
 * \return true if backup can continue, false if the communication channel cannot be created.
 *
 */
int zgw_backup_initialize_comm(char *bkup_dir);

