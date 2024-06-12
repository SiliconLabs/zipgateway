/* © 2019 Silicon Laboratories Inc. */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <zgw_nodemask.h>
#include <Serialapi.h>

//#include "port.h"

#include "json.h"
#include "zgw_data.h"
#include "zgw_restore_cfg.h"
#include "zgwr_json_parser.h"
#include "zgwr_pvl.h"
#include "zgwr_eeprom.h"
#include "zgwr_serial.h"


void exit_print_usage(const char* exe_name) {
   fprintf(stderr, "Usage: %s [-s serial_dev] -j backup_file [-i installation_path] [-d data_path] [-o]\n",
            exe_name);

   fprintf(stderr,
   "\t -s: Device name of the serialport which has the Z-Wave controller module attached\n"
   "\t -j: Location of the json backup file to use for the restore\n"
   "\t -o: Offline mode, read all data json file without using the controller module\n"
   );
   fprintf(stderr,
   "\t -i: Installation prefix, path to the gateway configuration files (default %s)\n",restore_cfg.installation_path);
   fprintf(stderr, 
   "\t -d: Location of the zipgateway data files, zipgateway.db and provision_list.dat (default %s)\n",restore_cfg.data_path);

   exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
   json_object *zgw_backup_obj = NULL;
   int res = 0; /* aka success */
   int opt;
   int offline = 0;
   zw_controller_t* ctrl=0;

   cfg_init();

   while ((opt = getopt(argc, argv, "s:j:i:c:d:o")) != -1) {
      switch (opt) {
         /* Input locations */
      case 'j':
         /* The json file */
         restore_cfg.json_filename = optarg;
         break;
      case 's':
         restore_cfg.serial_port = optarg;
         break;
         /* Output locations */
      case 'i':
         restore_cfg.installation_path = optarg;
         break;
      case 'd':
         /* TODO: rather get this from json file? */
         restore_cfg.data_path = optarg;
         break;
      case 'c':
         /* The json file */
         restore_cfg.zgw_cfg_filename = optarg;
         break;
      case 'o':
          offline = 1;
          break;
      default: /* '?' */
         exit_print_usage(argv[0]);
      }
   }
   if(restore_cfg.json_filename == NULL) {
      exit_print_usage(argv[0]);
   }

   if(offline == 0) {
    if(restore_cfg.serial_port == NULL) {
        exit_print_usage(argv[0]);
    }

    // 1. Open serial interface and read protocol data for the controller and
    // the nodes and insert this into the internal data structure.
    if ((!zgwr_serial_init())) {
        printf("Cannot use serial interface.\n");
        return -1;
    }
    // Fill zw_node_data_t zw_controller_t structures
    res = zgw_restore_serial_data_read(&ctrl);
    if (res) {
        return res;
    }
   }

   // 2. Read file into json
   zgw_backup_obj = zgw_restore_json_read();
   if (!zgw_backup_obj) {
      return -1;
   }

   // 3. Parse json into internal data structure
   // - and validate against controller data during this step
   printf("Parsing json object\n");
   res = zgw_restore_parse_backup(zgw_backup_obj, ctrl);
   if (res) {
      // Print better error message, if json-c makes it possible
      printf("Cannot use migration/backup file.\n");
      return res;
   }

   // 4. Sanitize results
   res = zip_gateway_backup_data_sanitize();
   if (res) {
      printf("Inconsistencies in migration/backup files.\n");
      return res;
   }

   if(offline == 0){
    // 4. Write Appl NVM from internal data
    res = zgw_restore_nvm_config_write();
    if (res) {
        printf("Failed to set the Z/IP Gateway NVM data.\n");
        return res;
    }

     zgwr_serial_close();
   }
   
   // 5. Write eeprom.dat from internal data structure
   res = zgw_restore_eeprom_file();
   if (res) {
      printf("Failed to write the Z/IP Gateway Resource Directory data.\n");
      return res;
   }

   // 6. Write pvl.dat from internal data structure
   res = zgw_restore_pvl_file();
   if (res) {
      // print error message
      return res;
   }

   if (offline) {
    printf("\033[95m"
       "\n\tIMPORTANT! This is an offline convertion, network keys will not be\n"
       "\tset in the module NVM. Also make sure that the json file matches\n"
       "\tthe Z-Wave NVM, home id, nodelist etc...\n\n\n"
       "\033[0m"
       );
   }
   return res;
}
