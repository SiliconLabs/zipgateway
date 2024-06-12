#include <unity.h>

#include "ZIP_Router.h"
#include"S2_wrap.h"
#include"s2_keystore.h"
#include <stdio.h>

union nvm
{
    zip_nvm_config_t config;
    uint8_t buffer[0x10000];
} nvm;


int __wrap_dev_urandom(
  int randomWordLen,
  uint8_t *randomWord) {
      memcpy(randomWord,"AAAAAAAA",8);
      return 1;
  }


void __wrap_zw_appl_nvm_read(u16_t start,void* dst,u8_t size) {
    memcpy(dst,&nvm.buffer[start],size);
}

void __wrap_zw_appl_nvm_write(u16_t start,const void* dst,u8_t size){
    memcpy(&nvm.buffer[start],dst,size);
}

void test_key_generation_all() {
    memset(&nvm.buffer,0,sizeof(nvm.buffer));
    keystore_network_generate_key_if_missing();

    TEST_ASSERT_EQUAL_STRING_LEN(nvm.config.security_netkey, "AAAAAAAAAAAAAAAA",16);
    TEST_ASSERT_EQUAL_STRING_LEN(nvm.config.security2_key[0],"AAAAAAAAAAAAAAAA",16);
    TEST_ASSERT_EQUAL_STRING_LEN(nvm.config.security2_key[1],"AAAAAAAAAAAAAAAA",16);
    TEST_ASSERT_EQUAL_STRING_LEN(nvm.config.security2_key[2],"AAAAAAAAAAAAAAAA",16);
}

void test_key_generation_not_s0() {
    memset(&nvm.buffer,0,sizeof(nvm.buffer));
    nvm.config.assigned_keys= KEY_CLASS_S0;
    keystore_network_generate_key_if_missing();

    TEST_ASSERT_EQUAL(0,nvm.config.security_netkey[0]);
    TEST_ASSERT_EQUAL_STRING_LEN(nvm.config.security2_key[0],"AAAAAAAAAAAAAAAA",16);
    TEST_ASSERT_EQUAL_STRING_LEN(nvm.config.security2_key[1],"AAAAAAAAAAAAAAAA",16);
    TEST_ASSERT_EQUAL_STRING_LEN(nvm.config.security2_key[2],"AAAAAAAAAAAAAAAA",16);
}

void test_key_generation_not_c0() {
    memset(&nvm.buffer,0,sizeof(nvm.buffer));
    nvm.config.assigned_keys= KEY_CLASS_S2_UNAUTHENTICATED;
    keystore_network_generate_key_if_missing();

    TEST_ASSERT_EQUAL_STRING_LEN("AAAAAAAAAAAAAAAA",nvm.config.security_netkey,16);
    TEST_ASSERT_EQUAL(0,nvm.config.security2_key[0][0]);
    TEST_ASSERT_EQUAL_STRING_LEN("AAAAAAAAAAAAAAAA",nvm.config.security2_key[1],16);
    TEST_ASSERT_EQUAL_STRING_LEN("AAAAAAAAAAAAAAAA",nvm.config.security2_key[2],16);
}


void test_key_generation_not_c1() {
    memset(&nvm.buffer,0,sizeof(nvm.buffer));
    nvm.config.assigned_keys= KEY_CLASS_S2_AUTHENTICATED;
    keystore_network_generate_key_if_missing();

    TEST_ASSERT_EQUAL_STRING_LEN("AAAAAAAAAAAAAAAA",nvm.config.security_netkey,16);
    TEST_ASSERT_EQUAL_STRING_LEN("AAAAAAAAAAAAAAAA",nvm.config.security2_key[0],16);
    TEST_ASSERT_EQUAL(0,nvm.config.security2_key[1][0]);
    TEST_ASSERT_EQUAL_STRING_LEN("AAAAAAAAAAAAAAAA",nvm.config.security2_key[2],16);
}

void test_key_generation_not_c2() {
    memset(&nvm.buffer,0,sizeof(nvm.buffer));
    nvm.config.assigned_keys= KEY_CLASS_S2_ACCESS;
    keystore_network_generate_key_if_missing();

    TEST_ASSERT_EQUAL_STRING_LEN("AAAAAAAAAAAAAAAA",nvm.config.security_netkey,16);
    TEST_ASSERT_EQUAL_STRING_LEN("AAAAAAAAAAAAAAAA",nvm.config.security2_key[0],16);
    TEST_ASSERT_EQUAL_STRING_LEN("AAAAAAAAAAAAAAAA",nvm.config.security2_key[1],16);
    TEST_ASSERT_EQUAL(0,nvm.config.security2_key[2][0]);
}
