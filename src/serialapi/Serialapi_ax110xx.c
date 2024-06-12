/* © 2014 Silicon Laboratories Inc.
 */

#include "contiki.h"
#include "contiki-conf.h"
#include "asix-types.h"
#include "uart.h"
#include "Hsuart.h"
#include "port.h"
#include <printf.h>

extern uint8_t gIsSslHshakeInProgress;

int SerialInit(const char* port) {

	UART_Init(2); //hardcoded UART 2 init

	HSUR_Start();

	return 1; // return value is important to upper layers. It should not be zero
}

void SerialClose() {
	//Do nothing - embedded system - always ON
}

int SerialGetByte(U8_T *out_char) {
	U8_T out_ch = 0;
	int ret_val = -1;

	ret_val = HSUR_GetCharNb2(&out_ch);
	(*out_char) = out_ch;
	//printd("SerialGetByte: ch=%bu out_ch=%bu ret_val=%d\n", out_ch, (*out_char), ret_val);

	return ret_val;
}

void SerialPutByte(unsigned char c) {
	HSUR_PutChar(c); //Using UART2 putchar which is suitable for raw char writing
}

int SerialCheck() {
	//DONE: Always return 1 as SerialGetByte returns -1 when no more data which breaks the loop
	//return 1;

	//Sasidhar: As we have less heap-space, return false when SSL handshake is in progress.
	if(gIsSslHshakeInProgress)
	{
		//printf("SerialCheck:\r\n");
		return 0;
	}
	else
	{
		return HSUR_GetRxBufCount();
	}
}

void SerialFlush() {
	//Nothing to do
}
