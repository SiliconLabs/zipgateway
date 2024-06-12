#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <pkgconfig.h>
#include <parse_config.h>
#include <limits.h>
#include "nvm_tools.h"
#include "zgw_backup.h"
#include "Serialapi.h"
#include "ZIP_Router_logging.h"
#include "zip_router_config.h"
#include "sdk_versioning.h"
#include "zgw_backup.h"
#include "zgw_backup_ipc.h"
#ifndef DISABLE_DTLS
#include "DTLS_server.h"
#endif

extern const char *linux_conf_database_file;

#define MANIFEST_FILE_NAME "manifest"
extern const char* linux_conf_provisioning_cfg_file;
extern const char* linux_conf_provisioning_list_storage_file;
extern const char* linux_conf_tun_script;
extern const char* linux_conf_fin_script;
static char bkup_dir[PATH_MAX]; // this is set by handl_comm_file() basically sent by backup script

void zgw_backup_send_failed(void)
{
  zgw_backup_send_msg("backup failed");
}

static void send_done_to_backup_script(void)
{
   zgw_backup_send_msg("backup done");
}

static void send_backup_started_to_script(void)
{
   zgw_backup_send_msg("backup started");
}

char manifest_file[PATH_MAX + sizeof(MANIFEST_FILE_NAME)];
static int copy_to_bkup_dir(const char* file)
{
  char cmd[PATH_MAX + PATH_MAX + 4]; // We need space for two paths and cp command syntax
  snprintf(cmd,sizeof(cmd), "cp %s %s", file, bkup_dir); 
  system(cmd); 
  return 1;
}

static int record_to_manifest(const char* varname, const char* path)
{
  int fd;
  int ret; 
  char line[PATH_MAX+20];
  size_t len;

  if (strcmp(path, "bridgeX.XX") == 0) {
     ERR_PRINTF("ERROR: The protocol version is unsupported\n");
     return 0;
  }
  fd = open(manifest_file, O_RDWR | O_APPEND);
  if (fd < 0) {
     ERR_PRINTF("record_to_manifest: error %s (%d) opening manifest file\n",
                strerror(errno), errno);
     return 0;
  }
  len = snprintf(line, sizeof(line), "%s=%s\n", varname, path); 
  ret = write(fd, line, len);
  if (ret < len) {
     ERR_PRINTF("Error %s (%d) in writing \"%s\" to %s",
                strerror(errno), errno, line, manifest_file);
     close(fd);
     return 0;
  }

  close(fd);
  return 1;
}

static int make_backup(void)
{
  char buff[PATH_MAX + sizeof("nvm_backup")];
  uint8_t chip_library;
  const char *nvm_id;

  if(!record_to_manifest("GW_VERSION", (PACKAGE_VERSION))) return 0;

  // Find out the library running on the chip.
  BYTE zw_protocol_version[14] = {};
  PROTOCOL_VERSION zw_protocol_version2;
  chip_library = ZW_Version(zw_protocol_version);
  ZW_GetProtocolVersion(&zw_protocol_version2);
  printf("SDK version:%0d.%02d.%02d:\n",
              zw_protocol_version2.protocolVersionMajor,
              zw_protocol_version2.protocolVersionMinor,
              zw_protocol_version2.protocolVersionRevision);
  nvm_id  = GenerateNvmIdFromSdkVersion(
              zw_protocol_version2.protocolVersionMajor,
              zw_protocol_version2.protocolVersionMinor,
              zw_protocol_version2.protocolVersionRevision,
              chip_library, chip_desc.my_chip_type);

  if(!record_to_manifest("GW_PROTOCOL_VERSION", nvm_id)) return 0; 

  snprintf(buff, sizeof(buff), "%lu", clock_time());
  if(!record_to_manifest("GW_TIMESTAMP", (buff))) return 0;

  if(!record_to_manifest("GW_ZipCaCert", cfg.ca_cert)) return 0;
  if(!record_to_manifest("GW_ZipCert", cfg.cert)) return 0;
  if(!record_to_manifest("GW_ZipPrivKey", cfg.priv_key)) return 0;

  if(!copy_to_bkup_dir(get_cfg_filename())) return 0;
  if(!record_to_manifest("GW_CONFIG_FILE_PATH", get_cfg_filename())) return 0;

  if(!copy_to_bkup_dir(linux_conf_database_file)) return 0;
  if(!record_to_manifest("GW_Databasefile", linux_conf_database_file)) return 0;

  if(!copy_to_bkup_dir(linux_conf_provisioning_cfg_file)) return 0;
  if(!record_to_manifest("GW_ProvisioningConfigFile", linux_conf_provisioning_cfg_file)) return 0;

  if(!copy_to_bkup_dir(linux_conf_provisioning_list_storage_file)) return 0;
  if(!record_to_manifest("GW_PVSStorageFile", linux_conf_provisioning_list_storage_file)) return 0;

  snprintf(buff, sizeof(buff), "%snvm_backup", bkup_dir);
  if(!ZW_NVM_Backup(buff, chip_desc.my_chip_type )) {
    printf("ERROR: nvm backup failed\n");
    return 0;
  } else {
    if(!record_to_manifest("NVM_Backup", buff)) return 0;
    return 1;
  }
}

int zgw_backup_init(void)
{
  if (!zgw_backup_initialize_comm(bkup_dir)) {
    zgw_backup_send_failed();
    return 0;
  }

  snprintf(manifest_file, sizeof(manifest_file), "%s%s", bkup_dir, MANIFEST_FILE_NAME);
  LOG_PRINTF("Using manifest file: %s\n", manifest_file);
  if (!zgw_file_truncate(manifest_file)) {
    ERR_PRINTF("ERROR accessing manifest file: %s \n", manifest_file);
    zgw_backup_send_failed();
    return 0;
  }
  return 1;
}

void zgw_backup_now(void)
{
  DBG_PRINTF("zgw_backup_now\n");
#ifndef DISABLE_DTLS
  dtls_close_all();
#endif
  send_backup_started_to_script();
  if (make_backup()) {
    send_done_to_backup_script();
  } else {
    zgw_backup_send_failed();
  }
}
