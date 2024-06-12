/* © 2017 Silicon Laboratories Inc.
 */
/*
 * s2_keystore.c

 *
 *  Created on: Nov 2, 2015
 *      Author: aes
 */
#include "TYPES.H"
#include "s2_keystore.h"
#include "gw_s2_keystore.h"
#include "Security_Scheme0.h"

#include "curve25519.h"
#include "ctr_drbg.h"
#include "ZIP_Router_logging.h"
#include "zip_router_config.h"
#include "CC_NetworkManagement.h"
#include "Serialapi.h"

extern uint8_t ecdh_dynamic_key[32];

bool keystore_network_key_clear(uint8_t keyclass) {
  uint8_t random_bytes[64];
  uint8_t assigned_keys;
  uint8_t i;

  AES_CTR_DRBG_Generate(&s2_ctr_drbg, random_bytes);

  if(keyclass == KEY_CLASS_ALL) {
    keystore_network_key_clear(KEY_CLASS_S0);
    keystore_network_key_clear(KEY_CLASS_S2_UNAUTHENTICATED);
    keystore_network_key_clear(KEY_CLASS_S2_AUTHENTICATED);
    keystore_network_key_clear(KEY_CLASS_S2_ACCESS);
    keystore_network_key_clear(KEY_CLASS_S2_AUTHENTICATED_LR);
    keystore_network_key_clear(KEY_CLASS_S2_ACCESS_LR);
    return 1;
  }

  if( keystore_network_key_write(keyclass,random_bytes) ) {
    nvm_config_get(assigned_keys,&assigned_keys);
    assigned_keys &= ~keyclass;
    nvm_config_set(assigned_keys,&assigned_keys);
    memset(random_bytes,0,sizeof(random_bytes));
    return 1;
  }
  return 0;
}

void gw_keystore_private_key_read(uint8_t *buf, int dynamic) {
  BYTE nvr_version;

  if(dynamic) {    
    memcpy(buf,ecdh_dynamic_key,32);
    printf(" dynamic \n");
  } else {

    if (ZW_GECKO_CHIP_TYPE(chip_desc.my_chip_type)) {
      printf("800 or 700");
      ZW_NVRGetValue(offsetof(NVR_FLASH_STRUCT,aSecurityPrivateKey),NVR_SECURITY_PRIVATE_KEY_SIZE,buf );
    } else {
      ZW_NVRGetValue( offsetof(NVR_FLASH_STRUCT,bRevision),1,&nvr_version);
      if((0xff!=nvr_version) && (nvr_version>= 2)) {
        ZW_NVRGetValue(offsetof(NVR_FLASH_STRUCT,aSecurityPrivateKey),NVR_SECURITY_PRIVATE_KEY_SIZE,buf );
      } else {
        nvm_config_get(ecdh_priv_key,buf);
      }
    }
    printf(" static \n");
  }
}

void keystore_private_key_read(uint8_t *buf) {
  if((NetworkManagement_getState() == NM_WAIT_FOR_SECURE_LEARN)
      || (NetworkManagement_getState() == NM_LEARN_MODE_STARTED)
      || (NetworkManagement_getState() == NM_LEARN_MODE)) {
    gw_keystore_private_key_read(buf,0);
  } else {
    gw_keystore_private_key_read(buf,1);
  }
}

void keystore_secondary_private_key_read(uint8_t *buf) {
  //Use dynamic key
  gw_keystore_private_key_read(buf,1);
}

static void print_ecdh_key(uint8_t* buf) {
  LOG_PRINTF("ECDH Public key is \n");
  for(int i=0; i < 16; i++) {
    uint16_t d = (buf[2*i]<<8) | buf[2*i +1];
    printf("%05hu-", d);
    if( (i&3)==3 )printf("\n");
  }
}

void keystore_public_key_debug_print(void) {
   uint8_t buf[32];
   keystore_public_key_read(buf);
}

void keystore_public_key_read(uint8_t *buf)
{
  uint8_t priv_key[32];

  keystore_private_key_read(priv_key);
  crypto_scalarmult_curve25519_base(buf,priv_key);
  memset(priv_key,0,sizeof(priv_key));
  print_ecdh_key(buf);
}

void keystore_secondary_public_key_read(uint8_t *buf) {
  uint8_t priv_key[32];
  keystore_secondary_private_key_read(priv_key);
  crypto_scalarmult_curve25519_base(buf,priv_key);
  memset(priv_key,0,sizeof(priv_key));
  print_ecdh_key(buf);
}

#define STR_CASE(x) \
  case x:           \
    return #x;

const char *security_keyclass_name(int keyclass)
{
  static char message[25];
  switch (keyclass) {
  STR_CASE(KEY_CLASS_S0)
  STR_CASE(KEY_CLASS_S2_UNAUTHENTICATED)
  STR_CASE(KEY_CLASS_S2_AUTHENTICATED)
  STR_CASE(KEY_CLASS_S2_ACCESS)
  STR_CASE(KEY_CLASS_S2_AUTHENTICATED_LR)
  STR_CASE(KEY_CLASS_S2_ACCESS_LR)
  default:
      snprintf(message, sizeof(message), "%d", keyclass);
      return message;
  }
}

bool keystore_network_key_read(uint8_t keyclass, uint8_t *buf)
{
  uint8_t assigned_keys;

  nvm_config_get(assigned_keys,&assigned_keys);
  if(0==(keyclass & assigned_keys)) {
    return 0;
  }

  switch(keyclass)
  {
  case KEY_CLASS_S0:
    nvm_config_get(security_netkey,buf);
  break;
  case KEY_CLASS_S2_UNAUTHENTICATED:
    nvm_config_get(security2_key[0],buf);
    break;
  case KEY_CLASS_S2_AUTHENTICATED:
    nvm_config_get(security2_key[1],buf);
    break;
  case KEY_CLASS_S2_ACCESS:
    nvm_config_get(security2_key[2],buf);
    break;
  case KEY_CLASS_S2_AUTHENTICATED_LR:
    nvm_config_get(security2_lr_key[0], buf);
    break;
  case KEY_CLASS_S2_ACCESS_LR:
    nvm_config_get(security2_lr_key[1], buf);
    break;
  default:
    assert(0);
    return 0;
  }

  DBG_PRINTF("Key class 0x%02x: %s\n",keyclass, security_keyclass_name(keyclass));
  print_key(buf);

  return 1;
}


bool keystore_network_key_write(uint8_t keyclass, const uint8_t *buf)
{
  uint8_t assigned_keys;

  switch(keyclass)
  {
  case KEY_CLASS_S0:
    nvm_config_set(security_netkey,buf);
    sec0_set_key(buf);
  break;
  case KEY_CLASS_S2_UNAUTHENTICATED:
    nvm_config_set(security2_key[0],buf);
    break;
  case KEY_CLASS_S2_AUTHENTICATED:
    nvm_config_set(security2_key[1],buf);
    break;
  case KEY_CLASS_S2_ACCESS:
    nvm_config_set(security2_key[2],buf);
    break;
  case KEY_CLASS_S2_AUTHENTICATED_LR:
    nvm_config_set(security2_lr_key[0], buf);
    break;
  case KEY_CLASS_S2_ACCESS_LR:
    nvm_config_set(security2_lr_key[1], buf);
    break;
  default:
    assert(0);
    return 0;
  }

  nvm_config_get(assigned_keys,&assigned_keys);
  assigned_keys |= keyclass;
  nvm_config_set(assigned_keys,&assigned_keys);

  return 1;
}
