/* © 2020 Silicon Laboratories Inc. */

/* ************************************************ */
/* Mock the config file reader */
/* ************************************************ */
#include <string.h>
#include "parse_config.h"
#include "RF_Region_Set_Validator.h"
#include "zip_router_config.h"
#include "mock_config.h"
#include <lib/zgw_log.h>
#include "uiplib.h"

zgw_log_id_define(mockCfg);
zgw_log_id_default_set(mockCfg);

#define MOCK_DATA_DIR "/tmp/"

/* data */

const char* linux_conf_database_file = "zipgateway.db";
const char* linux_conf_provisioning_cfg_file=TEST_SRC_DIR"./zipgateway_provisioning_list.cfg";
const char* linux_conf_provisioning_list_storage_file="./pvs.dat";
const char* linux_conf_id_script = "zipgateway_node_identify_generic.sh";
const char* linux_conf_tun_script;
const char* linux_conf_fin_script;


/* functions */

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

static char dummy_port[] = "./dummy_port.txt";

void mock_ConfigInit(void) {
   char *t;
   int val ;
   const char *s, *z;
   uint8_t *c;
   u8_t *len;
   memset(&cfg, 0, sizeof(cfg));
   
   zgw_log(zwlog_lvl_configuration, "Mock-configuring the gateway\n");
   cfg.serial_port = dummy_port;
   cfg.node_identify_script = linux_conf_id_script;
   
   uiplib_ipaddrconv("0::0", &(cfg.unsolicited_dest));
   cfg.unsolicited_port = atoi("4123");
   
   uiplib_ipaddrconv(("0::0"), &(cfg.unsolicited_dest2));
   cfg.unsolicited_port2 = atoi("4123");
   
   cfg.serial_log = 0;

    cfg.portal_url[0] = 0;
    cfg.portal_portno = atoi("44123");

    cfg.ca_cert = config_get_val("ZipCaCert", MOCK_DATA_DIR "ca_x509.pem");
    cfg.cert = config_get_val("ZipCert", MOCK_DATA_DIR "x509.pem");
    cfg.priv_key = config_get_val("ZipPrivKey", MOCK_DATA_DIR "key.pem");

//    linux_conf_eeprom_file = config_get_val("Eepromfile", MOCK_DATA_DIR "eeprom.dat");
//    linux_conf_provisioning_list_storage_file = config_get_val("PVSStorageFile", PROVISIONING_LIST_STORE_FILENAME_DEFAULT);
//    linux_conf_tun_script = config_get_val("TunScript", INSTALL_SYSCONFDIR "/" PACKAGE_TARNAME ".tun");
//    linux_conf_fin_script = config_get_val("FinScript", INSTALL_SYSCONFDIR "/" PACKAGE_TARNAME ".fin");
//    linux_conf_provisioning_cfg_file = config_get_val("ProvisioningConfigFile", PROVISIONING_CONFIG_FILENAME_DEFAULT);

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

    cfg.mb_port = uip_htons(atoi(config_get_val("ZipMBPort", "41230")));
    uiplib_ipaddrconv(config_get_val("ZipMBDestinationIp6", "0::0"), &(cfg.mb_destination));

    cfg.mb_conf_mode = atoi(config_get_val("ZipMBMode", "1"));

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

}
