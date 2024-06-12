/* © 2014 Silicon Laboratories Inc.
 */

#ifndef CONHANDLE_H_
#define CONHANDLE_H_

#include "Serialapi.h"

/****************************************************************************/
/*                     EXPORTED TYPES and DEFINITIONS                       */
/****************************************************************************/
/* return values for ConUpdate */
enum T_CON_TYPE
{
  conIdle,            // returned if nothing special has happened
  conFrameReceived,   // returned when a valid frame has been received
  conFrameSent,       // returned if frame was ACKed by the other end
  conFrameErr,        // returned if frame has error in Checksum
  conRxTimeout,       // returned if Rx timeout has happened
  conTxTimeout,        // returned if Tx timeout (waiting for ACK) ahs happened
  conTxErr,           // We got a NAK after sending
  conTxWait,          // We got a CAN while sending
};

const char* ConTypeToStr(enum T_CON_TYPE t);

/* defines for accessing serial protocol data */
#define serFrameLen (*serBuf)
#define serFrameType (*(serBuf + 1))
#define serFrameCmd (*(serBuf + 2))
#define serFrameDataPtr (serBuf + 3)

/****************************************************************************/
/*                              EXPORTED DATA                               */
/****************************************************************************/
/* serial buffer size */
#define SERBUF_MAX  0xff
#define FRAME_LENGTH_MIN  3
#define FRAME_LENGTH_MAX  SERBUF_MAX



void          /*RET Nothing */
 ConTxFrame(
  BYTE cmd,   /* IN Command */
  BYTE type,  /* IN frame Type to send (Response or Request) */
  BYTE *Buf, /* IN pointer to BYTE buffer containing DATA to send */
  BYTE len);   /* IN the length of DATA to transmit */


enum T_CON_TYPE     /*RET conState - See above */
ConUpdate(BYTE acknowledge); /* IN do we send acknowledge and handle frame if received correctly */

int ConInit(const char* serial_port);
void ConDestroy();

extern BYTE serBuf[SERBUF_MAX];
extern BYTE serBufLen;


#endif /* CONHANDLE_H_ */
