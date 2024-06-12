/* © 2019 Silicon Laboratories Inc. */

#include <stddef.h>
#include <string.h>

#include "zgw_restore_cfg.h"
#include "pkgconfig.h"

#include "net/uip.h" /* Contiki uip_ip6addr_t */

#define DATA_DIR INSTALL_LOCALSTATEDIR "/lib/" PACKAGE_TARNAME "/"

ZGW_restore_config_t restore_cfg;

void cfg_init(void) {
   memset(&restore_cfg, 0, sizeof(restore_cfg));
   /* Default values */
   restore_cfg.serial_port = 0;
   restore_cfg.json_filename = 0;
   restore_cfg.installation_path = INSTALL_SYSCONFDIR "/";
   restore_cfg.zgw_cfg_filename = INSTALL_SYSCONFDIR "/zipgateway.cfg";
   restore_cfg.data_path = DATA_DIR;
}

/* Read stuff from internal representation
 *
 */

const char *serial_port_get(void) {
   return restore_cfg.serial_port;
}

const char *json_filename_get(void) {
   return restore_cfg.json_filename;
}

const char *installation_path_get(void) {
   return restore_cfg.installation_path;
}

const char * data_dir_path_get(void) {
   return restore_cfg.data_path;
}
