/*
 * parse_config.c
 *
 *  Created on: Jan 14, 2011
 *      Author: aes
 */
#include "lib/list.h"

/*For strdup! */
#define _SVID_SOURCE

/* for strncasecmp(3) */
#include <strings.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <provisioning_list_files.h>
#include "parse_config.h"
#include "ZIP_Router_logging.h"

int getopt(int argc, char * const argv[], const char *optstring);

extern char *optarg;
extern int optind, opterr, optopt;

#include <getopt.h>

#include "net/uiplib.h"
#include "zip_router_config.h" // for struct cfg
#include "RF_Region_Set_Validator.h" // for checking the RF Region value

extern int prog_argc;
extern char** prog_argv;

const char* linux_conf_database_file;
const char* linux_conf_provisioning_cfg_file;
const char* linux_conf_provisioning_list_storage_file;
const char* linux_conf_tun_script;
const char* linux_conf_fin_script;
#ifdef NO_ZW_NVM
const char* linux_conf_nvm_file;
#endif

struct pair
{
  struct pair* next;
  char* key;
  char* val;
};

static struct pair* cfg_values = 0;

static char* cfgfile = INSTALL_SYSCONFDIR "/" PACKAGE_TARNAME ".cfg";

char *get_cfg_filename()
{
  return cfgfile;
}

static char* trim(char* b)
{
  char* e = strrchr(b, '\0'); /* Find the final null */
  while (b < e && isspace(*b)) /* Scan forward */
    ++b;
  while (e > b && isspace(*(e - 1))) /* scan back from end */
    --e;
  *e = '\0'; /* terminate new string */
  return b;
}

void config_exit()
{
  struct pair* p;

  while (cfg_values)
  {
    p = cfg_values->next;
    free(cfg_values->key);
    free(cfg_values->val);
    free(cfg_values);
    cfg_values = p;
  }

}

static int config_open()
{
  FILE * f;
  char line[1024];
  char* k, *v;
  struct pair* p;

  LOG_PRINTF("Opening config file %s\n", cfgfile);
  f = fopen(cfgfile, "r");
  if (!f)
    return 0;
  config_exit();
  while (!feof(f))
  {
    if (!fgets(line, sizeof(line), f))
      break;
    k = strtok(line, "=");
    v = strtok(0, "=");
    if (k && v)
    {
      p = (struct pair*) malloc(sizeof(struct pair));
      p->key = strdup(trim(k));
      p->val = strdup(trim(v));
      p->next = cfg_values;
      cfg_values = p;
    }
  }
  fclose(f);
  return 1;
}

static const char* config_get_val(const char* key, const char* def)
{
  struct pair* p;
  for (p = cfg_values; p != 0; p = p->next)
  {
    if (strcmp(key, p->key) == 0)
      return p->val;
  }
  return def;
}

static void parse_prog_args()
{
  int opt;
  optind = 1;

  while ((opt = getopt(prog_argc, prog_argv, "mnc:s:f:d:t:p:w:e")) != -1)
  {
    switch (opt)
    {
    case 'n':
      cfg.clear_eeprom = 1;
      break;
    case 'c':
      cfgfile = optarg;
      break;
    case 's':
      cfg.serial_port = optarg;
      break;
    case 'f':
      cfg.serial_log = optarg;
      break;
#ifdef NO_ZW_NVM
    case 'm':
      linux_conf_nvm_file = optarg;
      break;
#endif
    case 'p':
      linux_conf_provisioning_cfg_file = optarg;
      break;
    case 'w':
      linux_conf_provisioning_list_storage_file = optarg;
      break;
    case 't':
      linux_conf_tun_script = optarg;
      break;
    case 'x':
      linux_conf_fin_script = optarg;
      break;
    case 'd':
      linux_conf_database_file = optarg;
      break;
    case 'e':
      fprintf(stderr, "[-e eeprom_file] is not supported. Instead please indicate a database file with -d\n");
      /* FALL THROUGH */
    default: /* '?' */
      fprintf(stderr, "Usage: %s [-c cfgfile] [-d database_file] [-s serial_dev] [-f serial_log] [-p provisioning_config_file] [-w provisioning_list_storage_file] [-t tun_script]\n",
          prog_argv[0]);
      exit(EXIT_FAILURE);
    }
  }
}


static int hex2int(char c) {
  if(c >= '0' && c <= '9') {
    return c-'0';
  } else if(c >= 'a' && c <= 'f') {
    return c-'a' + 0xa;
  } else if(c >= 'A' && c <= 'F') {
    return c-'A' + 0xa;
  } else {
    return -1;
  }
}
/*---------------------------------------------------------------------------*/

void ConfigInit()
{
  char *t;
  int val ;
  const char *s, *z, *d;
  uint8_t *c;
  u8_t *len;
  memset(&cfg, 0, sizeof(cfg));

  parse_prog_args();

  config_open();
  {
    if (config_get_val("Eepromfile", NULL) != NULL)
    {
      fprintf(stderr, "Configuration key \"Eepromfile\", found in zipgateway.cfg, is no longer supported. Please remove this line and use \"ZipGwDatabase\" instead\n");
      exit(EXIT_FAILURE);
    }
    cfg.serial_port = config_get_val("ZipSerialAPIPortName", "null");

    uiplib_ipaddrconv(config_get_val("ZipUnsolicitedDestinationIp6", "0::0"), &(cfg.unsolicited_dest));
    cfg.unsolicited_port = atoi(config_get_val("ZipUnsolicitedDestinationPort", "41230"));

    uiplib_ipaddrconv(config_get_val("ZipUnsolicitedDestination2Ip6", "0::0"), &(cfg.unsolicited_dest2));
    cfg.unsolicited_port2 = atoi(config_get_val("ZipUnsolicitedDestination2Port", "41231"));

    cfg.serial_log = config_get_val("SerialLog", 0);

    s = config_get_val("ZipPortal", 0);
    if (s)
    {
      strcpy(cfg.portal_url, s);
      ;
    } else
    {
      cfg.portal_url[0] = 0;
    }
    cfg.portal_portno = atoi(config_get_val("ZipPortalPort", "44123"));

    cfg.ca_cert = config_get_val("ZipCaCert", DATA_DIR "ca_x509.pem");
    cfg.cert = config_get_val("ZipCert", DATA_DIR "x509.pem");
    cfg.priv_key = config_get_val("ZipPrivKey", DATA_DIR "key.pem");
    linux_conf_database_file = config_get_val("ZipGwDatabase", DATA_DIR PACKAGE_NAME ".db");
    linux_conf_provisioning_list_storage_file = config_get_val("PVSStorageFile", PROVISIONING_LIST_STORE_FILENAME_DEFAULT);
#ifdef NO_ZW_NVM
    linux_conf_nvm_file = config_get_val("Nvmfile", DATA_DIR "nvm.dat");
#endif
    linux_conf_tun_script = config_get_val("TunScript", INSTALL_SYSCONFDIR "/" PACKAGE_TARNAME ".tun");
    linux_conf_fin_script = config_get_val("FinScript", INSTALL_SYSCONFDIR "/" PACKAGE_TARNAME ".fin");
    linux_conf_provisioning_cfg_file = config_get_val("ProvisioningConfigFile", PROVISIONING_CONFIG_FILENAME_DEFAULT);

    cfg.log_level = atoi(config_get_val("LogLevel", "0"));

    uiplib_ipaddrconv(config_get_val("ZipPanIp6", "::"), &(cfg.cfg_pan_prefix));
    uiplib_ipaddrconv(config_get_val("ZipLanIp6", "::"), &(cfg.cfg_lan_addr));
    cfg.cfg_lan_prefix_length = atoi(config_get_val("ZipLanIp6PrefixLength", "64"));

    uiplib_ipaddrconv(config_get_val("ZipLanGw6", "::"), &(cfg.gw_addr));
    cfg.tun_prefix_length = atoi(config_get_val("ZipTunIp6PrefixLength", "128"));
    uiplib_ipaddrconv(config_get_val("ZipTunPrefix", "::"), &(cfg.tun_prefix));

    cfg.ipv4disable = atoi(config_get_val("DebugZipIp4Disable", "0"));
    cfg.client_key_size = atoi(config_get_val("ZipClientKeySize", "1024"));

    cfg.manufacturer_id = strtol(config_get_val("ZipManufacturerID", "0"), NULL, 0);
    cfg.product_id = strtol(config_get_val("ZipProductID", "1"), NULL, 0);
    cfg.product_type = strtol(config_get_val("ZipProductType", "1"), NULL, 0);
    cfg.hardware_version = strtol(config_get_val("ZipHardwareVersion", "1"), NULL, 0);
    d = config_get_val("ZipDeviceID", uip_lladdr.addr); 
    if (memcmp(d, uip_lladdr.addr, sizeof(uip_lladdr.addr)) == 0) {
      cfg.device_id_len = sizeof(uip_lladdr.addr); 
      memcpy(cfg.device_id, uip_lladdr.addr, sizeof(uip_lladdr.addr));
    } else {
      cfg.device_id_len = 0; 
      while(*d && cfg.device_id_len < sizeof(cfg.device_id)) {
        val = hex2int(*d++);
        if(val < 0) break;
        cfg.device_id[cfg.device_id_len]  = ((val) &0xf) <<4;
        val = hex2int(*d++);
        if(val < 0) break;
        cfg.device_id[cfg.device_id_len] |= (val & 0xf);
        cfg.device_id_len++;
      }
    }
    DBG_PRINTF("device_id_len: %d, device_id: ", cfg.device_id_len);
    int i;
    for (i = 0; i < cfg.device_id_len; i++) {
        printf("%02X", cfg.device_id[i]);
    }
    printf("\n");

    cfg.mb_port = uip_htons(atoi(config_get_val("ZipMBPort", "41230")));
    uiplib_ipaddrconv(config_get_val("ZipMBDestinationIp6", "0::0"), &(cfg.mb_destination));

    cfg.mb_conf_mode = atoi(config_get_val("ZipMBMode", "1"));
    if (cfg.mb_conf_mode == 1) {
      WRN_PRINTF("Mailbox is enabled\n");
    } else if (cfg.mb_conf_mode == 0) {
      WRN_PRINTF("Mailbox is disabled\n");
    }

    cfg.node_identify_script = config_get_val("ZipNodeIdentifyScript", "zipgateway_node_identify_generic.sh");

    s = config_get_val("ZipPSK", "123456789012345678901234567890AA");
    cfg.psk_len=0;
    while(*s && cfg.psk_len < sizeof(cfg.psk)) {
      val = hex2int(*s++);
      if(val < 0) break;
      cfg.psk[cfg.psk_len]  = ((val) &0xf) <<4;
      val = hex2int(*s++);
      if(val < 0) break;
      cfg.psk[cfg.psk_len] |= (val & 0xf);

      cfg.psk_len++;
    }

    /*Parse extra classes*/
    c = cfg.extra_classes;
    len = &cfg.extra_classes_len;
    *len = 0;
    t = strtok((char*) config_get_val("ExtraClasses", ""), " ");
    while (t)
    {
      if (!strncasecmp(t, "0xF100", 6)) // we found marker
      {
        c = cfg.sec_extra_classes;
        len = &cfg.sec_extra_classes_len;
        *len = 0;
        t = strtok(NULL, " ");
      } else {
        *c = strtoul(t, NULL, 0) & 0xFF;

        if (*c != 0)
           c++;

        (*len)++;
        t = strtok(NULL, " ");
        if (!t)
          break;
      }
    }

    *c = 0;

    char *endptr;
    int8_t normal, measured0dBm;
    /* Assume powerlevel setting exists */
    cfg.is_powerlevel_set = 1;
    /* Parse normal TX power into string */
    s = config_get_val("NormalTxPowerLevel", NULL);
    /* Parse measured 0dbm power into string */
    z = config_get_val("Measured0dBmPower", NULL);
    /* Set the powerlevel only if both fields are present */
    if (s && z) {
      normal = strtol(s, &endptr, 0);
      if (*endptr != '\0' || endptr == s) {
        /* Not a number, unset powerlevel flag */
        cfg.is_powerlevel_set = 0;
      }
      else {
        cfg.tx_powerlevel.normal = normal;
      }

      measured0dBm = strtol(z, &endptr, 0);
      if (*endptr != '\0' || endptr == z) {
        /* Not a number, unset powerlevel flag */
        cfg.is_powerlevel_set = 0;
      }
      else {
        cfg.tx_powerlevel.measured0dBm = measured0dBm;
      }
    }
    else {
      /* Incomplete powerlevel value pairs, unset powerlevel flag */
      cfg.is_powerlevel_set = 0;
    }

    endptr = NULL;
    uint8_t region;
    /* Parse RF region into string */
    s = config_get_val("ZWRFRegion", NULL);
    /* Try to convert the string into unsigned int */
    if (s != NULL) {
      region = strtoul(s, &endptr, 0);
      /* Invalid if further characters are found after number or it's not numeric at all */
      if (*endptr != '\0' || endptr == s) {
        /* 0xFE for invalid rfregion indicated */
        cfg.rfregion = 0xFE;
      }
      else {
        /*Filtering the RF Region with valid values*/
        cfg.rfregion = RF_REGION_CHECK(region);
      }
    }
    else {
      cfg.rfregion = 0xFE;
    }
    if (cfg.rfregion == 0x20 ) { //Japan 
      cfg.zw_lbt = atoi(config_get_val("ZWLBT", "50"));
      if ((cfg.zw_lbt < 34) || (cfg.zw_lbt > 78)) {
        cfg.zw_lbt = 50;
      }
    } else {
      cfg.zw_lbt = atoi(config_get_val("ZWLBT", "64"));
      if ((cfg.zw_lbt < 34) || (cfg.zw_lbt > 78)) {
        cfg.zw_lbt = 64;
      }
    }
    endptr = NULL;
    int16_t max_lr_tx_powerlevel;
    cfg.is_max_lr_powerlevel_set = 1;
    /* Parse MAX LR TX powerlevel into string */
    s = config_get_val("MaxLRTxPowerLevel", NULL);
    /* Try to convert the string into unsigned int */
    if (s != NULL) {
      max_lr_tx_powerlevel = strtol(s, &endptr, 0);
      /* Invalid if further characters are found after number or it's not numeric at all */
      if (*endptr != '\0' || endptr == s) {
        /* 0 for invalid max LR tx powerlevel indicated */
        cfg.is_max_lr_powerlevel_set = 0;
      }
      else {
        cfg.max_lr_tx_powerlevel = max_lr_tx_powerlevel;;
      }
    } else {
      cfg.is_max_lr_powerlevel_set = 0;
    }

    cfg.single_classic_temp_association = atoi(
      config_get_val("ZipAssociationLimit", "0"));

    if ((0 != cfg.single_classic_temp_association) &&
      (1 != cfg.single_classic_temp_association)) {
      WRN_PRINTF("Wrong configuration value for "
                 "\"ZipAssociationLimit\" (%d). Ignoring",
                 cfg.single_classic_temp_association);

      cfg.single_classic_temp_association = 0;
    }
  }

  /*We wan't command line to override config file.*/
  parse_prog_args();
}


void config_update(const char* key, const char* value) {
  FILE * f_in,*f_out;
  char cfgfile_out[256];
  char line[1024];
  char line_bak[1024];

  char* k;
  struct pair* p;
  int sz;
  int key_was_found = 0;
  if (strlen(cfgfile) < (256 - (6+1)))
  {
    int fd;
    snprintf(cfgfile_out, strlen(cfgfile) + 7, "%s%s", cfgfile, "XXXXXX");
    fd = mkstemp(cfgfile_out);
    if(fd == -1) {
      ERR_PRINTF("Could not create temp config file for updating: %s, Error: %s \n", cfgfile_out, strerror(errno));
      goto fail_nthing;
    }
    f_out = fdopen(fd, "w");
    if (!f_out)
    {
      ERR_PRINTF("Could not create temp config file for updating with fdopen(): %s, Error: %s \n", cfgfile_out, strerror(errno));
      close(fd);
      goto fail_nthing;
    }
  } else {
    goto fail_nthing;
  }

  f_in = fopen(cfgfile, "r");
  if (!f_in)
  {
    ERR_PRINTF("Could not open config file for updating: %s, Error: %s \n", cfgfile, strerror(errno));
    goto fail_out;

  }

  while (!feof(f_in))
  {
    if (!fgets(line, sizeof(line), f_in))
      break;

    strncpy(line_bak,line,sizeof(line_bak));
    k = trim(strtok(line_bak, "="));
    //Replace value
    if(strcmp(key,k) ==0) {
      key_was_found=1;
      sz = snprintf(line,sizeof(line),"%s = %s\n", key,value);
    } else {
      sz = strlen(line);
    }
    if(fwrite(line,sz,1,f_out) != 1)
    {
      ERR_PRINTF("Could not write full data to config file, Error: %s \n",strerror(errno));
      goto fail;
    }
  }

  //Append at the end
  if(key_was_found==0) {
    sz = snprintf(line,sizeof(line),"%s = %s\n", key,value);
    if(fwrite(line,sz,1, f_out) != 1)
    {
      ERR_PRINTF("Could not write full data to config file: Error: %s\n", strerror(errno));
      goto fail;
    }
  }
  /*
  ZipUnsolicitedDestinationIp6
  ZipUnsolicitedDestinationIp6
*/
  fclose(f_out);
  fclose(f_in);

  if(rename(cfgfile_out,cfgfile) != 0)
  {
    ERR_PRINTF("Rename of %s to %s failed: Error:%s \n", cfgfile_out, cfgfile, strerror(errno));
    goto fail_nthing;
  }
  return; /* Returning */
fail:
  fclose(f_in);
fail_out:
  fclose(f_out);
fail_nthing:
  ERR_PRINTF("Persisting changes to config file didnt work");
  return;

}
