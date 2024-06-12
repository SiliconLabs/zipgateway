/* © 2019 Silicon Laboratories Inc. */

#include "zgw_data.h"
#include "zgw_restore_cfg.h"

#include "provisioning_list_types.h"
#include "provisioning_list_files.h"
#include "provisioning_list.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "zgw_restore_cfg.h"
#define RESTORE_PVL_DAT_FILENAME "provisioning_list_store.dat"
#define RESTORE_PVL_CFG_FILENAME "zipgateway_provisioning_list.cfg"


// initializing the provisioning _list_store.dat and zipgateway_ provisioning_list.cfg to the default file
static int zgw_restore_pvl_file_init (void) {
	char linux_provisioning_cfg_file[PATH_MAX];
	char linux_provisioning_list_storage_file[PATH_MAX];

	linux_provisioning_cfg_file[0]= 0;
	linux_provisioning_list_storage_file[0]= 0;

	strcat( linux_provisioning_cfg_file, installation_path_get() );
	strcat( linux_provisioning_cfg_file, RESTORE_PVL_CFG_FILENAME);

	strcat( linux_provisioning_list_storage_file, data_dir_path_get() );
	strcat( linux_provisioning_list_storage_file, RESTORE_PVL_DAT_FILENAME);

	provisioning_list_init(linux_provisioning_list_storage_file,
			linux_provisioning_cfg_file); // create and initialize the provisioning _list_store.dat


	return 0;
}

static int zgw_restore_pvl (void){

	/*To do: write node provisioning data from the internal data structure to the persisting
	 *file for restoring a Z/IP Gateway from a back-up */
	return 0;

}

int zgw_restore_pvl_file(void) {
	int res = 0;

	res = zgw_restore_pvl_file_init();
	if (res){
		printf("Cannot initializing provisioning_list_store.dat and zipgateway_ provisioning_list.cfg\n");
		return res;
	}

	res = zgw_restore_pvl();
	if(res){
		printf("Cannot restore provisioning_list_store.dat\n");
		return res;
	}


   return 0;
}
