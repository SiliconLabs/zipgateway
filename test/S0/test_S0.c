
#include<unity.h>

#include "../../src/transport/Security_Scheme0.c"

/*********************************************** Stubs ***********************************************/
int cb_count=0;
int sd_count=0;
int sd_size=0;
uint8_t sd_data[128];
ts_param_t sd_p;
void* sd_user;
static int sec0_cb_count = 0;
TX_STATUS_TYPE ts;

ZW_SendDataAppl_Callback_t sd_callback;

void ctimer_set(struct ctimer *c, clock_time_t t,
		void (*f)(void *), void *ptr)
{

}

void ctimer_stop(struct ctimer *c) {

}

/* Reset test counters. To be called between test cases. */
static void test_reset() {
  cb_count=0;
  sd_count=0;
  sd_size=0;
  memset(&sd_p, 0xAB, sizeof(sd_p));
  sec0_cb_count = 0;
  memset(&ts, 0xAB, sizeof(ts));
  const uint8_t s0_key[16] = {0x98, 0x70,0x7E, 0xE1, 0x48, 0xBB, 0xC8, 0x53, 0xB2, 0xFA, 0x38, 0x81, 0x8A, 0x65, 0x59, 0x73};
  sec0_set_key(s0_key);
}

clock_time_t clock_time() {
    return 0x42;
}

u8_t send_data(ts_param_t* p, const u8_t* data, u16_t len,
    ZW_SendDataAppl_Callback_t cb, void* user)
{
    sd_callback = cb;
    sd_size = len;
    memcpy(sd_data,data,len);
    sd_p = *p;
    sd_user = user;
    sd_count++;

    printf("SD (%d) : ", len);
    for(int i=0; i < len; i++) {
        printf("%02x",sd_data[i]);
    }
    printf("\n");
    return TRUE;
}

void ts_param_make_reply(ts_param_t* dst, const ts_param_t* src) {

}


void PRNGOutput(uint8_t* out) {
    memcpy(out,"CCCCCCCC",8 );
}


void zw_appl_nvm_write(u16_t start,const void* dst,u8_t size) {

}

BOOL SerialAPI_AES128_Encrypt(const BYTE *ext_input, BYTE *ext_output, const BYTE *ext_key) {
    for(int i=0; i < 16; i++) {
        ext_output[i] = ext_input[i]^ext_key[i];
    }
    return TRUE;
}

BOOL secure_learn_active() {
    return FALSE;
}

void _xassert(const char* msg, int expr) {
    puts(msg);
}

void sec0_sd_callback(BYTE txStatus,void* user, TX_STATUS_TYPE *txStatEx) {
    sec0_cb_count++;
    printf("sec0_sd_callback from line %u\n", (unsigned int)user);
}

int dev_urandom(int len, uint8_t *buf)
{ FILE * fp;
  size_t read;
  int error = 0; 

  printf("-------in my dev_urandom\n");
  
  fp = fopen("/dev/urandom", "r");
  read = fread(buf, 1, len, fp);
  if (read < len) {
      printf("Error: Random number generation failed\n");
      error = 1;
  }

  fclose(fp);

  return error ? 0 : 1;
}

/****************************************Test cases*******************************************************/



/*
 * From ZGW-2666, send_data callback is found to throw segfault if no ack on S0
 * frame. This test is to verify that msg1_callback works correctly when no ack
 * of S0 message is received while nonce get/report are still well-transmited.
 */
void test_null_ts() {
    sd_count = 0;
    ts_param_t p= {.dnode = 2, .snode=1 };
    uint8_t dec_msg[20];
    const char msg[] = "HelloWorld";
    sec0_init();

    TEST_ASSERT_TRUE(sec0_send_data(&p,msg,sizeof(msg),sec0_sd_callback,(void*)__LINE__));

    /*Verify that we are sending a nonce get*/
    TEST_ASSERT_EQUAL(1,sd_count);
    TEST_ASSERT_EQUAL(COMMAND_CLASS_SECURITY, sd_data[0]);
    TEST_ASSERT_EQUAL(SECURITY_NONCE_GET, sd_data[1]);

    sd_callback(TRANSMIT_COMPLETE_OK,sd_user,&ts);

    sec0_register_nonce(2,1,(const uint8_t*)"AAAAAAAA");
    /* Did we send a SECURITY_MESSAGE_ENCAPSULATION*/
    TEST_ASSERT_EQUAL(2,sd_count);
    TEST_ASSERT_EQUAL(COMMAND_CLASS_SECURITY, sd_data[0]);
    TEST_ASSERT_EQUAL(SECURITY_MESSAGE_ENCAPSULATION, sd_data[1]);

    /* Trigger callback with transmit fail as if no ack is received */
    sd_callback(TRANSMIT_COMPLETE_FAIL,sd_user,0);
}
void test_s0_rx_tx_two_long_frame() {
    sd_count = 0;
    sec0_cb_count = 0;
    ts_param_t p= {.dnode = 1, .snode=6 };
    uint8_t zero[120] = {0};
    uint8_t dec_msg[120] = {0};
    uint8_t should_be_zero[120] = {0};
   const char msg[] = {0xC9,0x3E,0x13,0x68,0xDE,0x5D,0xD5,0xF5,0xF9,0x25,0x7C,0x87,0xF0,0x19,0xF0,0xCA,0x9D,0x29,0xAD,0x1B,0xD1,0x10,0xBF,0x24,0x7A,0x0A,0x60,0x87,0x27,0x55,0x80,0xB4,0x19,0xAA,0x02,0x8B,0x9D,0xBB,0x65,0xCB,0x64,0x54,0x34,0x88,0xD8,0x24,0x6A,0x17,0x98,0xD7,0xB4,0xF9,0xFF,0xF3,0x1D,0xF0,0x4D,0xBE,0xA3,0x96,0x24,0x8D,0x3B,0xD5,0xB4,0x60,0xC4,0x86,0xCD,0x61,0x1D,0xE3,0xE1,0x76,0xA1,0x1D,0x55,0x2A,0xE1,0x41,0x9A,0xC9,0x54,0xD7,0x5B,0xA7,0xF0,0x5A,0x72,0x67,0x39,0x79,0x51,0x28,0xEC,0x59,0x40,0x45,0x4C,0x6E,0x02,0x8F,0x0D,0xDB,0x6D,0xEA,0x17,0xE8,0xC4,0x68,0x26,0xC5,0x77,0xE3,0x53,0xC5,0xDE,0x8D,0x7B,0xDC};
    uint8_t msg1[] = {0x98, 0x81, 0xFC, 0x98, 0x5F, 0x87, 0xBA, 0xF0, 0x9B, 0x92,
                      0xD0, 0xB5, 0xDF, 0x30, 0x3F, 0xA4, 0xBE, 0x90, 0xED,
                      0x84, 0xBF, 0x39, 0xEF, 0xBC, 0xD4, 0x7F, 0x2E, 0x16, 0xE9, 
                      0x84, 0xBF, 0x39, 0xEF, 0xBC, 0xD4, 0x7F, 0x2E, 0x16, 0xE9, 
                      0x84, 0xBF, 0x39, 0xEF, 0xBC, 0xD4, 0x7F, 0x2E, 0x16, 0xE9, 
                      0x84, 0xBF, 0x39, 0xEF, 0xBC, 0xD4, 0x7F, 0x2E, 0x16, 0xE9, 
                      0xA1, 0x7D, 0x62, 0xfc, 0x30, 0x51, 0x5f, 0xa0, 0xe4, 0x5f}; 
    uint8_t msg2[] = {0x98, 0x81, 0xFC, 0x98, 0x5F, 0x87, 0xBA, 0xF0, 0x9B, 0x92,
                      0xF0, 0x8B, 0xB5, 0xDF, 0x30, 0x3F, 0xA4, 0xBE, 0x90, 0xED,
                      0xF6, 0x2F, 0x70, 0xDA, 0xF5, 0x69, 0x36, 0x05, 0x76, 0x2A,
                      0xF6, 0x2F, 0x70, 0xDA, 0xF5, 0x69, 0x36, 0x05, 0x76, 0x2A,
                      0xF6, 0x2F, 0x70, 0xDA, 0xF5, 0x69, 0x36, 0x05, 0x76, 0x2A,
                      0xF6, 0x2F, 0x70, 0xDA, 0xF5, 0x69, 0x36, 0x05, 0x76, 0x2A,
                      0xF6, 0x2F, 0x70, 0xDA, 0xF5, 0x69, 0x36, 0x05, 0x76, 0x2A,
                      0xF6, 0x2F, 0x70, 0xDA, 0xF5, 0x69, 0x36, 0x05, 0x76, 0x2A,
                      0xA1, 0x21, 0x8B, 0x2D, 0x59, 0x3E, 0x4B, 0x18, 0x50, 0xC2,
                      0x84, 0xBF, 0x39, 0xEF, 0xBC, 0xD4, 0x7F, 0x2E, 0x16, 0xE9, 
                      0xA1, 0x7D, 0xfe, 0x15, 0x60, 0x33, 0x16, 0xb6, 0xc9, 0xcd}; 
 
   const uint8_t nonce[8] = {
                            0x7D,
                            0x44,
                            0x41,
                            0x78,
                            0xD7,
                            0xE5,
                            0x1F,
                            0x40};
   const uint8_t nonce1[8] = {
                            0x7D,
                            0x44,
                            0x41,
                            0x78,
                            0xD7,
                            0xE5,
                            0x1D,
                            0x40};

    test_reset();
    sec0_init();

    register_nonce(1,6,0,nonce);

    int len = sec0_decrypt_message(6,1,msg1,sizeof(msg1),dec_msg);
    TEST_ASSERT_EQUAL(len, 0);
    register_nonce(1,6,0,nonce1);
    len = sec0_decrypt_message(6,1,msg2,sizeof(msg2),dec_msg);
    TEST_ASSERT_EQUAL(len, 0);
    TEST_ASSERT_EQUAL_MEMORY(dec_msg, zero, sizeof(dec_msg));  
    TEST_ASSERT_EQUAL_MEMORY(should_be_zero, zero, sizeof(dec_msg));  

}
void test_s0_rx_tx_long_frame1() {
    sd_count = 0;
    sec0_cb_count = 0;
    ts_param_t p= {.dnode = 1, .snode=6 };
    uint8_t zero[120] = {0};
    uint8_t dec_msg[120] = {0};
    uint8_t should_be_zero[120] = {0};
   const char msg[] = {0xC9,0x3E,0x13,0x68,0xDE,0x5D,0xD5,0xF5,0xF9,0x25,0x7C,0x87,0xF0,0x19,0xF0,0xCA,0x9D,0x29,0xAD,0x1B,0xD1,0x10,0xBF,0x24,0x7A,0x0A,0x60,0x87,0x27,0x55,0x80,0xB4,0x19,0xAA,0x02,0x8B,0x9D,0xBB,0x65,0xCB,0x64,0x54,0x34,0x88,0xD8,0x24,0x6A,0x17,0x98,0xD7,0xB4,0xF9,0xFF,0xF3,0x1D,0xF0,0x4D,0xBE,0xA3,0x96,0x24,0x8D,0x3B,0xD5,0xB4,0x60,0xC4,0x86,0xCD,0x61,0x1D,0xE3,0xE1,0x76,0xA1,0x1D,0x55,0x2A,0xE1,0x41,0x9A,0xC9,0x54,0xD7,0x5B,0xA7,0xF0,0x5A,0x72,0x67,0x39,0x79,0x51,0x28,0xEC,0x59,0x40,0x45,0x4C,0x6E,0x02,0x8F,0x0D,0xDB,0x6D,0xEA,0x17,0xE8,0xC4,0x68,0x26,0xC5,0x77,0xE3,0x53,0xC5,0xDE,0x8D,0x7B,0xDC};
    uint8_t msg1[] = {0x98, 0x81, 0xFC, 0x98, 0x5F, 0x87, 0xBA, 0xF0, 0x9B, 0x92,
                         0, 0x8B, 0xB5, 0xDF, 0x30, 0x3F, 0xA4, 0xBE, 0x90, 0xED,
                      0xF6, 0x2F, 0x70, 0xDA, 0xF5, 0x69, 0x36, 0x05, 0x76, 0x2A,
                      0xA1, 0x21, 0x8B, 0x2D, 0x59, 0x3E, 0x4B, 0x18, 0x50, 0xC2,
                      0x4A, 0xC0, 0x7A, 0x5B, 0x21, 0xCE, 0xD3, 0x7C, 0x42, 0x0E,
                      0xAE, 0x70, 0x87, 0x67, 0x7F, 0xF2, 0xC8, 0xF3, 0xEB, 0x00,
                      0x88, 0x33, 0x43, 0x0F, 0x68, 0x8F, 0x8D, 0x09, 0xFF, 0xDB,
                      0x41, 0xC1, 0x92, 0x7B, 0x1B, 0xF6, 0xEB, 0x08, 0xDE, 0x2C,
                      0x98, 0x76, 0x86, 0xF5, 0xA5, 0xF2, 0xEA, 0x7F, 0xC6, 0x78,
                      0xBD, 0x02, 0x96, 0xD3, 0x6D, 0xAB, 0x3C, 0x62, 0x27, 0x36,
                      0x26, 0x41, 0xAE, 0xB4, 0x37, 0xAC, 0x97, 0x02, 0xCE, 0x80,
                      0x36, 0xE3, 0x76, 0x66, 0xBE, 0x79, 0x39, 0x44, 0x1F, 0xEE, 
                      0x84, 0xBF, 0x39, 0xEF, 0xBC, 0xD4, 0x7F, 0x2E, 0x16, 0xE9, 
                      0x84, 0xBF, 0x39, 0xEF, 0xBC, 0xD4, 0x7F, 0x2E, 0x16, 0xE9, 
                      0x84, 0xBF, 0x39, 0xEF, 0xBC, 0xD4, 0x7F, 0x2E, 0x16, 0xE9, 
                      0xA1, 0x7D, 0xe8, 0x61, 0x9a, 0xda, 0x1e, 0x3b, 0xe7, 0xac}; 
   const uint8_t nonce[8] = {
                            0x7D,
                            0x44,
                            0x41,
                            0x78,
                            0xD7,
                            0xE5,
                            0x1F,
                            0x40};

    test_reset();
    sec0_init();

    register_nonce(1,6,0,nonce);

    int len = sec0_decrypt_message(6,1,msg1,sizeof(msg1),dec_msg);
    TEST_ASSERT_EQUAL(len, 0);
    TEST_ASSERT_EQUAL_MEMORY(dec_msg, zero, sizeof(dec_msg));  
    TEST_ASSERT_EQUAL_MEMORY(should_be_zero, zero, sizeof(dec_msg));  
}

void test_s0_rx_tx() {
    sd_count = 0;
    sec0_cb_count = 0;
    ts_param_t p= {.dnode = 2, .snode=1 };
    uint8_t dec_msg[20];
    const char msg[] = "HelloWorld";
    test_reset();
    sec0_init();

    TEST_ASSERT_TRUE(sec0_send_data(&p,msg,sizeof(msg),sec0_sd_callback,(void*)__LINE__));

    /*Verify that we are sending a nonce get*/
    TEST_ASSERT_EQUAL(1,sd_count);
    TEST_ASSERT_EQUAL(COMMAND_CLASS_SECURITY, sd_data[0]);
    TEST_ASSERT_EQUAL(SECURITY_NONCE_GET, sd_data[1]);

    sd_callback(TRANSMIT_COMPLETE_OK,sd_user,&ts);

    sec0_register_nonce(2,1,(const uint8_t*)"AAAAAAAA");
    /* Did we send a SECURITY_MESSAGE_ENCAPSULATION*/
    TEST_ASSERT_EQUAL(2,sd_count);
    TEST_ASSERT_EQUAL(COMMAND_CLASS_SECURITY, sd_data[0]);
    TEST_ASSERT_EQUAL(SECURITY_MESSAGE_ENCAPSULATION, sd_data[1]);

    sd_callback(TRANSMIT_COMPLETE_OK,sd_user,&ts);
    /*Verify that the secure tranmission is compleeted*/
    TEST_ASSERT_EQUAL(1,sec0_cb_count);

    /*Now act as a receiver, add three nonces and verify that we will use the right one */
    register_nonce(2,1,0,(const uint8_t*)"AAAAAAAA");
    register_nonce(2,1,0,(const uint8_t*)"DDDDDDDD");
    register_nonce(2,1,0,(const uint8_t*)"EEEEEEEE");

    TEST_ASSERT_NOT_EQUAL(0, sec0_decrypt_message(1,2,sd_data,sd_size,dec_msg));
    TEST_ASSERT_EQUAL_STRING(msg,dec_msg);

    /*Verify that we will not try to decypt the message a second time ie. reuse the nonce from before */
    TEST_ASSERT_EQUAL(0 ,sec0_decrypt_message(1,2,sd_data,sd_size,dec_msg) );

    /* Register the nonce again */
    register_nonce(2,1,0,(const uint8_t*)"AAAAAAAA"); 
    for(int i=0; i < 10; i++) {  //Make the nonce timeout
        nonce_timer_timeout(0);
    }
    /*Check that we will not use the old nonce.*/
    TEST_ASSERT_EQUAL(0 ,sec0_decrypt_message(1,2,sd_data,sd_size,dec_msg) );
}

/**
 * Test that repeated S0 nonce reports are ignored and not reused for encrypting a second message.
 */
void test_s0_blacklist() {
    ts_param_t p= {.dnode = 2, .snode=1 };
    uint8_t dec_msg[20];
    const char msg[] = "HelloWorld";
    const char msg2[] = "HelloWorld2";
    test_reset();
    sec0_init();

    TEST_ASSERT_TRUE(sec0_send_data(&p,msg,sizeof(msg),sec0_sd_callback,(void*)__LINE__));

    /*Verify that we are sending a nonce get*/
    TEST_ASSERT_EQUAL(1,sd_count);
    TEST_ASSERT_EQUAL(COMMAND_CLASS_SECURITY, sd_data[0]);
    TEST_ASSERT_EQUAL(SECURITY_NONCE_GET, sd_data[1]);

    sd_callback(TRANSMIT_COMPLETE_OK,sd_user,&ts);

    sec0_register_nonce(2,1,(const uint8_t*)"AAAAAAAA");
    /* Did we send a SECURITY_MESSAGE_ENCAPSULATION*/
    TEST_ASSERT_EQUAL(2,sd_count);
    TEST_ASSERT_EQUAL(COMMAND_CLASS_SECURITY, sd_data[0]);
    TEST_ASSERT_EQUAL(SECURITY_MESSAGE_ENCAPSULATION, sd_data[1]);

    sd_callback(TRANSMIT_COMPLETE_OK,sd_user,&ts);
    /*Verify that the secure tranmission is compleeted*/
    TEST_ASSERT_EQUAL(1,sec0_cb_count);

    TEST_ASSERT_TRUE(sec0_send_data(&p,msg2,sizeof(msg2),sec0_sd_callback,(void*)__LINE__));

    /* Send a second msg and verify that we are sending a nonce get (because the nonce report was blacklisted) */
    TEST_ASSERT_EQUAL(3,sd_count);
    TEST_ASSERT_EQUAL(COMMAND_CLASS_SECURITY, sd_data[0]);
    TEST_ASSERT_EQUAL(SECURITY_NONCE_GET, sd_data[1]);

    sd_callback(TRANSMIT_COMPLETE_OK,sd_user,&ts);

    /* Pretend we (the enc msg sender) received the same AAAA...AA nonce again. It should be blacklisted */
    sec0_register_nonce(2,1,(const uint8_t*)"AAAAAAAA");

    /* Verify we did not use the blacklisted nonce */
    TEST_ASSERT_EQUAL(3,sd_count);

    /* Now deliver a fresh nonce and verify that we send the encrypted message */
    sec0_register_nonce(2,1,(const uint8_t*)"BBBBBBBB");

    /* Did we send a SECURITY_MESSAGE_ENCAPSULATION? */
    TEST_ASSERT_EQUAL(4,sd_count);
    TEST_ASSERT_EQUAL(COMMAND_CLASS_SECURITY, sd_data[0]);
    TEST_ASSERT_EQUAL(SECURITY_MESSAGE_ENCAPSULATION, sd_data[1]);

    /* Now act as a receiver. We created the BBBB..BB nonce internally and register it */
    register_nonce(2,1,0,(const uint8_t*)"BBBBBBBB");
    /*Verify that we can decrypt the frame and the BBBB..BB nonce was indeed used */
    TEST_ASSERT_NOT_EQUAL(0, sec0_decrypt_message(1,2,sd_data,sd_size,dec_msg));
    TEST_ASSERT_EQUAL_STRING(msg2,dec_msg);

}

/**
 * Test low-level functions for the nonce blacklist
 */
void test_nonce_blacklist() {
  uint8_t zero_nonce[8] = {0, 0, 0, 0, 0, 0, 0, 0};

  test_reset();
  sec0_init();

  TEST_ASSERT_FALSE(sec0_is_nonce_blacklisted(0,0, zero_nonce));
  sec0_blacklist_add_nonce(2, 1, (const uint8_t*)"11111111");
  TEST_ASSERT_TRUE(sec0_is_nonce_blacklisted(2, 1, (const uint8_t*)"11111111"));
  TEST_ASSERT_FALSE(sec0_is_nonce_blacklisted(3, 1, (const uint8_t*)"11111111"));
  TEST_ASSERT_FALSE(sec0_is_nonce_blacklisted(2, 4, (const uint8_t*)"11111111"));

  /* Now add more nonces than blacklist can hold and ensure that the oldest nonce is no longer blacklisted */
  sec0_blacklist_add_nonce(1, 2, (const uint8_t*)"22222222");
  sec0_blacklist_add_nonce(1, 2, (const uint8_t*)"33333333");
  sec0_blacklist_add_nonce(1, 2, (const uint8_t*)"44444444");
  sec0_blacklist_add_nonce(1, 2, (const uint8_t*)"55555555");
  sec0_blacklist_add_nonce(1, 2, (const uint8_t*)"66666666");
  sec0_blacklist_add_nonce(1, 2, (const uint8_t*)"77777777");
  sec0_blacklist_add_nonce(1, 2, (const uint8_t*)"88888888");
  sec0_blacklist_add_nonce(1, 2, (const uint8_t*)"99999999");
  sec0_blacklist_add_nonce(1, 2, (const uint8_t*)"AAAAAAAA");
  sec0_blacklist_add_nonce(1, 2, (const uint8_t*)"BBBBBBBB");
  TEST_ASSERT_FALSE(sec0_is_nonce_blacklisted(1, 2, (const uint8_t*)"11111111"));

  /* Test that all ten most recent nonces are indeed blacklisted*/
  TEST_ASSERT_TRUE(sec0_is_nonce_blacklisted(1, 2, (const uint8_t*)"22222222"));
  TEST_ASSERT_TRUE(sec0_is_nonce_blacklisted(1, 2, (const uint8_t*)"33333333"));
  TEST_ASSERT_TRUE(sec0_is_nonce_blacklisted(1, 2, (const uint8_t*)"44444444"));
  TEST_ASSERT_TRUE(sec0_is_nonce_blacklisted(1, 2, (const uint8_t*)"55555555"));
  TEST_ASSERT_TRUE(sec0_is_nonce_blacklisted(1, 2, (const uint8_t*)"66666666"));
  TEST_ASSERT_TRUE(sec0_is_nonce_blacklisted(1, 2, (const uint8_t*)"77777777"));
  TEST_ASSERT_TRUE(sec0_is_nonce_blacklisted(1, 2, (const uint8_t*)"88888888"));
  TEST_ASSERT_TRUE(sec0_is_nonce_blacklisted(1, 2, (const uint8_t*)"99999999"));
  TEST_ASSERT_TRUE(sec0_is_nonce_blacklisted(1, 2, (const uint8_t*)"AAAAAAAA"));
  TEST_ASSERT_TRUE(sec0_is_nonce_blacklisted(1, 2, (const uint8_t*)"BBBBBBBB"));
}

void test_sec0_reset_netkey()
{  uint8_t s0_key[S0_KEY_SIZE]={0};

   sec0_reset_netkey();
   memcpy(s0_key, networkKey, sizeof(s0_key));
   sec0_reset_netkey();

   TEST_ASSERT_NOT_EQUAL(memcmp(s0_key, networkKey, sizeof(s0_key)), 0);
}
