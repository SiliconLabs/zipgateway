#include <unity.h>

#include "ZIP_Router.h"
#include "command_handler.h"
#include "ZW_udp_server.h"
#include "zw_frame_buffer.h"


int rx_ptr;
zw_frame_ip_buffer_element_t fbs[128];



void __wrap_ApplicationIpCommandHandler(zwave_connection_t *c, void *pData, u16_t bDatalen) {
    fbs[rx_ptr].conn = *c;
    fbs[rx_ptr].frame_len = bDatalen;
    memcpy(fbs[rx_ptr].frame_data,pData,bDatalen);
    rx_ptr++;
}

void test_multicmd() {
    zwave_connection_t c;
    command_handler_codes_t rc;
    rx_ptr = 0;

    uint8_t multicmd_ok[] = {COMMAND_CLASS_MULTI_CMD, MULTI_CMD_ENCAP, 2,2,0xAA,0xBB,2,0xCC,0xDD};

    rc = ZW_command_handler_run(&c,multicmd_ok,sizeof(multicmd_ok),FALSE);
    TEST_ASSERT_EQUAL( COMMAND_HANDLED, rc );
    TEST_ASSERT_EQUAL( 2,fbs[0].frame_len );
    TEST_ASSERT_EQUAL( 0xAA,fbs[0].frame_data[0] );
    TEST_ASSERT_EQUAL( 0xBB,fbs[0].frame_data[1] );

    TEST_ASSERT_EQUAL( 2,fbs[1].frame_len );
    TEST_ASSERT_EQUAL( 0xCC,fbs[1].frame_data[0] );
    TEST_ASSERT_EQUAL( 0xDD,fbs[1].frame_data[1] );

    uint8_t multicmd_short1[] = {COMMAND_CLASS_MULTI_CMD, MULTI_CMD_ENCAP, 2,2,0xAA,0xBB,2,0xCC};
    rc = ZW_command_handler_run(&c,multicmd_short1,sizeof(multicmd_short1),FALSE);
    TEST_ASSERT_EQUAL( COMMAND_PARSE_ERROR, rc );


    uint8_t multicmd_short2[] = {COMMAND_CLASS_MULTI_CMD, MULTI_CMD_ENCAP, 2,2,0xAA,0xBB};
    rc = ZW_command_handler_run(&c,multicmd_short2,sizeof(multicmd_short2),FALSE);
    TEST_ASSERT_EQUAL( COMMAND_PARSE_ERROR, rc );

}
