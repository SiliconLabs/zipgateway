/* © 2014 Silicon Laboratories Inc.
 */

#include "conhandle.h"
#include "port.h"
#include <ZW_SerialAPI.h>
#include "ZIP_Router_logging.h"
#define RX_ACK_TIMEOUT_DEFAULT  150
#define RX_BYTE_TIMEOUT_DEFAULT 150

/****************************************************************************/
/*                      PRIVATE TYPES and DEFINITIONS                       */
/****************************************************************************/
/* serial Protocol handler states */
typedef enum
{
  stateSOFHunt = 0,
  stateLen = 1,
  stateType = 2,
  stateCmd = 3,
  stateData = 4,
  stateChecksum = 5
} T_CON_TYPE;


/****************************************************************************/
/*                              PRIVATE DATA                                */
/****************************************************************************/
BYTE serBuf[SERBUF_MAX];
BYTE serBufLen;

static BYTE con_state;
static BYTE bChecksum_RX;
static PORT_TIMER timeOutACK;
static PORT_TIMER timeOutRX;


static BOOL rxActive = FALSE;
static char AckNakNeeded= FALSE;


/****************************************************************************/
/*                           EXPORTED FUNCTIONS                             */
/****************************************************************************/

/*===============================   ConTxFrame   =============================
**
**   Transmit frame via serial port by adding SOF, Len, Type, cmd and Chksum.
**   Frame  : SOF-Len-Type-Cmd-DATA-Chksum
**    where SOF is Start of frame byte
**          Len is length of bytes between and including Len to last DATA byte
**          Type is Response or Request
**          Cmd Serial application command describing what DATA is
**          DATA as it says
**          Chksum is a XOR checksum taken on all BYTEs from Len to last DATA byte
**
**          NOTE: If Buf is NULL then the previously used cmd, type, Buf and len
**          is used again (Retransmission)
**
**--------------------------------------------------------------------------*/
void          /*RET Nothing */
 ConTxFrame(
  BYTE cmd,   /* IN Command */
  BYTE type,  /* IN frame Type to send (Response or Request) */
  BYTE *Buf, /* IN pointer to BYTE buffer containing DATA to send */
  BYTE len)   /* IN the length of DATA to transmit */
{
  uint8_t tx_buffer[255];
  uint8_t *c;
  uint8_t i, bChecksum;
  c = tx_buffer;


  *c++ =SOF;
  *c++ = len+3;
  *c++ = type;
  *c++ = cmd;

  memcpy(c,Buf,len);
  c+= len;

  bChecksum = 0xFF;
  for (i = 0; i < len+3; i++)
  {
    bChecksum ^= tx_buffer[i+1];
  }
  *c++=bChecksum;

  SerialPutBuffer(tx_buffer,len+5);
  AckNakNeeded = 1;  // Now we need an ACK...
  PORT_TIMER_RESTART(timeOutACK);
}

/*==============================   ConUpdate   =============================
**
**   Parses serial data sent from external controller module (PC-controller).
**   Should be frequently called by main loop.
**
**   Return: conIdle          if nothing has happened
**           conFrameReceived if valid frame was received
**           conFrameSent     if transmitted frame was ACKed by other end
**           conFrameErr      if received frame has error in Checksum
**           conRxTimeout     if a Rx timeout happened
**           conTxTimeout     if a Tx timeout waiting for ACK happened
**
**--------------------------------------------------------------------------*/
enum T_CON_TYPE     /*RET conState - See above */
ConUpdate(BYTE acknowledge) /* IN do we send acknowledge and handle frame if received correctly */
{
  int c,i;
  enum T_CON_TYPE retVal = conIdle;

  if (SerialCheck())
  {
    do
    {
      c = SerialGetByte();
      if(c<0) {
        break;
      }
      switch (con_state)
      {
        case stateSOFHunt :
          if (c == SOF)
          {
            bChecksum_RX = 0xff;
            serBufLen = 0;
            rxActive = 1;  // now we're receiving - check for timeout
            con_state++;
            PORT_TIMER_RESTART(timeOutRX);
          }
          else
          {
            if (AckNakNeeded)
            {
              if (c == ACK)
              {
                retVal = conFrameSent;
                AckNakNeeded = 0;  // Done
              }
              else if (c == NAK)
              {
                retVal = conTxErr;
                AckNakNeeded = 0;
              }
              else if (c == CAN)
              {
                retVal = conTxWait;
                AckNakNeeded = 0;
              }
              else
              {
                // Bogus character received...
              }
            }
          }
          break;
        case stateLen:
          if ((c < FRAME_LENGTH_MIN) || (c > FRAME_LENGTH_MAX))
          {
            con_state = stateSOFHunt; // Restart looking for SOF
            rxActive = 0;  // Not really active now...
            break;
          }
          serBuf[0] = c;
          serBufLen = c;
          SerialGetBuffer(serBuf+1,serBufLen);

          bChecksum_RX = 0xFF;
          for(i=0; i < serBufLen+1; i++) {
            bChecksum_RX ^= serBuf[i];
          }
          if (acknowledge)
          {
            if (bChecksum_RX == 0)
            {
              SerialPutByte(ACK);
              SerialFlush();                //Start sending frame to the host.
              retVal = conFrameReceived;  // Tell THE world that we got a packet
            }
            else
            {
              SerialPutByte(NAK);       // Tell them something is wrong...
              SerialFlush();                //Start sending frame to the host.
              retVal = conFrameErr;
              ERR_PRINTF("****************** CRC ERROR ***********************\n");
            }
          } else
          {
            // We are in the process of looking for an acknowledge to a callback request
            // Drop the new frame we received - we don't have time to handle it.
            // Send a CAN to indicate what is happening...
            SerialPutByte(CAN);
            SerialFlush();                //Start sending frame to the host.
          }
          /*no break*/
        default :
          con_state = stateSOFHunt; // Restart looking for SOF
          rxActive = 0;  // Not really active now...
          break;
      }
      /* FIXME The second call to SerialCheck can be removed,
       * since we already read the whole frame. */
    } while ((retVal == conIdle) && SerialCheck());
  }
  /* Check for timeouts - if no other events detected */
  if (retVal == conIdle)
  {
    /* Are in the middle of collecting a frame and have we timed out? */
    if (rxActive && PORT_TIMER_EXPIRED(timeOutRX))
    {
      /* Reset to SOF hunting */
      con_state = stateSOFHunt;
      rxActive = 0; /* Not inframe anymore */
      retVal = conRxTimeout;
    }
    /* Are we waiting for ACK and have we timed out? */
    if (AckNakNeeded && PORT_TIMER_EXPIRED(timeOutACK))
    {
      /* Not waiting for ACK anymore */
      AckNakNeeded = 0;
      /* Tell upper layer we could not get the frame through */
      retVal = conTxTimeout;
    }
  }
  return retVal;
}


/*==============================   ConInit   =============================
**
**   Initialize the module.
**
**--------------------------------------------------------------------------*/
int                /*RET  Nothing */
ConInit(const char* serial_port )             /*IN   Nothing */
{
  BYTE rc;
  serBufLen = 0;
  con_state = stateSOFHunt;

  PORT_TIMER_INIT(timeOutACK, RX_ACK_TIMEOUT_DEFAULT*10);
  PORT_TIMER_INIT(timeOutRX,  RX_BYTE_TIMEOUT_DEFAULT*10);
  rc = SerialInit(serial_port);
  if(rc) {
    /*Send ACK in case that the target expects one.*/
    SerialPutByte(ACK);
  }
  return rc;
}

void ConDestroy() {
  SerialClose();
}

const char* ConTypeToStr(enum T_CON_TYPE t) {
  switch(t) {
  case conIdle:            // returned if nothing special has happened
    return "conIdle";
  case conFrameReceived:   // returned when a valid frame has been received
    return "conFrameReceived";
  case conFrameSent:       // returned if frame was ACKed by the other end
    return "conFrameSent";
  case conFrameErr:        // returned if frame has error in Checksum
    return "conFrameErr";
  case conRxTimeout:       // returned if Rx timeout has happened
    return "conRxTimeout";
  case conTxTimeout:        // returned if Tx timeout (waiting for ACK) ahs happened
    return "conTxTimeout";
  case conTxErr:           // We got a NAK after sending
    return "conTxErr";
  case conTxWait:          // We got a CAN while sending
    return "conTxWait";
  }
  return NULL;
}
