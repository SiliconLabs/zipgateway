#include <unity.h>
#include "s2_keystore.h"
#include "ZIP_Router.h"
#include "CC_NetworkManagement.h"
#include "Serialapi.h"
nm_state_t the_nms_state;
uint8_t the_nvr_version;
const uint8_t my_priv_key[] = "MyPrivateKey.123MyPrivateKey.123";
extern uint8_t ecdh_dynamic_key[32];

union nvm
{
    zip_nvm_config_t config;
    uint8_t buffer[0x10000];
} nvm;

void __wrap_zw_appl_nvm_read(u16_t start,void* dst,u8_t size) {
    memcpy(dst,&nvm.buffer[start],size);
}



nm_state_t __wrap_NetworkManagement_getState()
{
  return the_nms_state;
}

void __wrap_ZW_NVRGetValue(BYTE offset, BYTE bLength,BYTE*pNVRValue) {
    if( offset == offsetof(NVR_FLASH_STRUCT,bRevision) ) {
        *pNVRValue = the_nvr_version;
    } else if(offset == offsetof(NVR_FLASH_STRUCT,aSecurityPrivateKey)) {
        memcpy(pNVRValue,my_priv_key,bLength);
    } else {
        TEST_ASSERT_MESSAGE(0,"Wrong offset");
    }
}

#define ZW_GECKO_CHIP_TYPE_MOCK 7
void test_read_private_ecdh_key() {
    uint8_t key[32];
    chip_desc.my_chip_type = ZW_GECKO_CHIP_TYPE_MOCK;
    the_nms_state = NM_IDLE;
    memcpy(nvm.config.ecdh_priv_key, ".This is the App NVM dynamic key",32);
    memcpy(ecdh_dynamic_key,         ".....This is the dynamic key....",32 );

    keystore_private_key_read(key);   
    TEST_ASSERT_EQUAL_STRING_LEN(ecdh_dynamic_key,key,32);

    the_nms_state = NM_LEARN_MODE;
    keystore_private_key_read(key);   
    TEST_ASSERT_EQUAL_STRING_LEN(my_priv_key,key,32);

    chip_desc.my_chip_type =  ZW_CHIP_TYPE;
    the_nvr_version = 2;
    keystore_private_key_read(key);   
    TEST_ASSERT_EQUAL_STRING_LEN(my_priv_key,key,32);

    the_nvr_version = 1;
    keystore_private_key_read(key);   
    TEST_ASSERT_EQUAL_STRING_LEN(nvm.config.ecdh_priv_key,key,32);
}
