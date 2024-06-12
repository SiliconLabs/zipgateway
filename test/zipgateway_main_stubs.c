/* © 2020 Silicon Laboratories Inc. */

#include "ZIP_Router.h"

char** prog_argv;
int prog_argc;
process_event_t serial_line_event_message;

#ifdef NO_SIGNALS
void send_command_to_backup_script(const char* cmd) {
   return;
}

int handle_comm_file(char *bkup_dir) {
   return 1;
}
#endif
char* linux_conf_database_file;
