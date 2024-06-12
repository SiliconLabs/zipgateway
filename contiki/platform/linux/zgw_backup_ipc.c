/* © 2020 Silicon Laboratories Inc. */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <errno.h>
#include "ZIP_Router_logging.h"

/**
 * Handle inter-process communication with the script that requests backups.
 */

static const char *zgw_backup_communication_file = "/tmp/zgw.file";

static char bkup_inbox[PATH_MAX];

/** The PID written in the communication file by the process
 * requesting the backup.
 *
 * Only single processes are accepted.  If the PID is less than 1, the
 * communication channel cannot be established.  The zipgateway will
 * still write "backup failed" to the communication file.
 */
static pid_t backup_pid = 0;

int zgw_file_truncate(const char *filename) {
  int fd;

  fd = open(filename, O_RDWR | O_TRUNC);
  if (fd < 0) {
     ERR_PRINTF("Opening file %s failed with %s (%d)\n",
                filename, strerror(errno), errno);
     return 0;
  } else {
     close(fd);
     return 1;
  }
}

void zgw_backup_send_msg(const char* msg) {
  int fd;
  fd = open(zgw_backup_communication_file, O_WRONLY|O_TRUNC);
  if (fd < 0) {
    ERR_PRINTF("Error %s (%d) opening backup comm file %s",
               strerror(errno), errno, zgw_backup_communication_file);
  } else {
     write(fd, msg, strlen(msg));
     close(fd);
  }
  if (backup_pid > 0) {
     if (kill(backup_pid, SIGUSR1)!=0) {
        ERR_PRINTF("Error %s notifying PID=%i\n", strerror(errno), backup_pid);
     }
  } else {
     ERR_PRINTF("Backup script PID was never initialized, cannot send signal.\n");
     ERR_PRINTF("Please check communication file %s for status.\n",
                zgw_backup_communication_file);
  }
}

int zgw_backup_initialize_comm(char *bkup_dir) {
  int fd;
  int bytes_read;
  char *ptr;

  /* Reset backup_pid from the last backup */
  backup_pid = 0;

  fd = open(zgw_backup_communication_file, O_RDWR);
  if (fd < 0) {
     ERR_PRINTF("ERROR, backup comm file cannot be opened (%s, %d).\n",
                strerror(errno), errno);
     return 0;
  }
  bytes_read = read(fd, bkup_inbox, PATH_MAX);
  close(fd);

  if (bytes_read == 0) {
     ERR_PRINTF("ERROR, backup comm file is empty.\n");
     return 0;
  }  

  LOG_PRINTF("backup_pid and backup path: %s\n", bkup_inbox);

  /* strtol() sets ptr to first non-number char in the string */
  long int read_number = strtol(bkup_inbox, &ptr, 10);

  if ((read_number <= 0) || (read_number == LONG_MAX)) {
     ERR_PRINTF("Error in backup_pid %ld (%s), cannot communicate with backup requester.\n",
                read_number, strerror(errno));
     return 0;
  }
  backup_pid = read_number;

  /* Read the directory */
  strncpy(bkup_dir, ptr+1, bytes_read - (ptr - bkup_inbox) - 2);

  return 1;
}

