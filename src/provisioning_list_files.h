/* © 2017 Silicon Laboratories Inc.
 */
#ifndef PROVISIONING_LIST_FILES_H_
#define PROVISIONING_LIST_FILES_H_

#include <parse_config.h>

/**
 * \ingroup pvslist
 * @{
 */

/** Default name of the persistent storage file for the provisioning list.
 */
#ifndef PROVISIONING_LIST_STORE_FILENAME_DEFAULT
#define PROVISIONING_LIST_STORE_FILENAME_DEFAULT DATA_DIR "provisioning_list_store.dat"
#endif

/** Default name of the initial provisioning configuration file.
 */
#ifndef PROVISIONING_CONFIG_FILENAME_DEFAULT
#define PROVISIONING_CONFIG_FILENAME_DEFAULT INSTALL_SYSCONFDIR "/" PACKAGE_TARNAME "_provisioning_list.cfg"
#endif

/**
 * @}
 */
#endif
