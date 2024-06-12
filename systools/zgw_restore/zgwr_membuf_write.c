/* © 2019 Silicon Laboratories Inc. */

#include <net/uip.h>
#include <zip_router_config.h>
#include "zgw_restore_cfg.h"
#include "zgwr_serial.h"
#include "zgw_data.h"
#include "NodeCache.h"

/**
 * Write Appl NVM from internal data
 */
int zgw_restore_nvm_config_write(void) {
 //  write zip_nvm_config_t to where gateway writes it
   const  uint8_t zero = 0;
   const zgw_data_t *data = zgw_data_get(); 
   /* zip_nvm_config_t */
   u32_t magic = ZIPMAGIC;
   u8_t security_scheme = 0;

   nvm_config_set(magic,&magic);

   u16_t version = NVM_CONFIG_VERSION;
   nvm_config_set(config_version,&version);

   nvm_config_set(ula_pan_prefix,&data->zip_pan_data.ipv6_pan_prefix);
   nvm_config_set(ula_lan_addr,&data->zip_lan_data.zgw_ula_lan_addr);
   nvm_config_set(mac_addr,&data->zip_lan_data.zgw_uip_lladdr);

   if (data->zip_pan_data.zw_security_keys.assigned_keys | NODE_FLAG_SECURITY0) {
      security_scheme = 1;
   }
   nvm_config_set(security_scheme, &security_scheme);
   nvm_config_set(security_netkey, &data->zip_pan_data.zw_security_keys.security_netkey);
   nvm_config_set(security_status, &zero);
   nvm_config_set(assigned_keys,&data->zip_pan_data.zw_security_keys.assigned_keys);
   // mb destination and mb_port?

   nvm_config_set(security2_key[0],&data->zip_pan_data.zw_security_keys.security2_key[0]);
   nvm_config_set(security2_key[1],&data->zip_pan_data.zw_security_keys.security2_key[1]);
   nvm_config_set(security2_key[2],&data->zip_pan_data.zw_security_keys.security2_key[2]);
   nvm_config_set(security2_lr_key[0],&data->zip_pan_data.zw_security_keys.security2_lr_key[0]);
   nvm_config_set(security2_lr_key[1],&data->zip_pan_data.zw_security_keys.security2_lr_key[1]);
   nvm_config_set(ecdh_priv_key,&data->zip_pan_data.zw_security_keys.ecdh_priv_key);


   return 0;
}

