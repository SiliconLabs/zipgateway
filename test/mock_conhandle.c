/* © 2020 Silicon Laboratories Inc. */
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

#include "conhandle.h"
#include <lib/zgw_log.h>
#include "conhandle.h"
#include "ZW_SerialAPI.h"
#include "process.h"
//#include "serial_api_process.h"

#include "test_helpers.h"
#include "mock_bridge_controller.h"

#include "ZW_typedefs.h"

#include "test_helpers.h"

/* Logging defaults for this file */
zgw_log_id_define(mockCon);
zgw_log_id_default_set(mockCon);


/* helpers */
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
  case conTxTimeout:        // returned if Tx timeout (waiting for ACK) has happened
    return "conTxTimeout";
  case conTxErr:           // We got a NAK after sending
    return "conTxErr";
  case conTxWait:          // We got a CAN while sending
    return "conTxWait";
  }
  return NULL;
}

/* things we need to stub/mock to get rid of real lan and serial interfaces */

/* Put a copy of the sent frame here, so that a test case can inspect it. */
BYTE tx_buf_last_sent[SERBUF_MAX];

/* Mock the helpers for conhandle */

/* conhandle.c */

BYTE serBuf[SERBUF_MAX];
BYTE serBufLen;
static BOOL rxActive = FALSE;
enum T_CON_TYPE con_status = conIdle;
// Whether mock con has received a frame, but not replied FrameSent yet.
// conIdle means nothing received.
// Change to conFrameSent when ConTxFrame is called.
// Change back to idle when conFrameSent has been returned from a call to conUpdate.
enum T_CON_TYPE con_txstatus = conIdle;

/* ****************************************** 
 * Internal interface functions
 * ****************************************** */
void mock_conhandle_inject_frame(uint8_t *frame) {
   /* length of frame is the first byte of the frame */
   serBufLen = *(frame);
   memcpy(serBuf, frame, serBufLen);
   zgw_log(zwlog_lvl_control, "Frame sent to gateway: %s, len %d, cmd 0x%02x. \n",
                 (serBuf[1] == REQUEST) ? "REQUEST": "RESPONSE", serBufLen, serBuf[2]);
//   for (int ii = 0; ii < *serBuf; ii++) {
//      printf("0x%02x, ", serBuf[ii]);
//   }
//   printf("\n");
   con_status = conFrameReceived;
// TODO: only set to received when frame is unsolicited??
}

/* Just pass this through.
 *
 * More logic could be added here, eg, to allow the test cases to
 * trigger "external" input. */
bool mock_conhandle_select(useconds_t timeout) {
   if (mp_check_state()) {
      return true;
   } else if (timeout > 0) {
      usleep(timeout);
   }
   return false;
}

/* ****************************************** */
/* External interface functions. (what we are mocking) */
/* ****************************************** */
int ConInit(const char* serial_port ) {
   zgw_log(zwlog_lvl_configuration, "Opening mock serial con.\n");
   return true;
}

void ConDestroy() {
   zgw_log(zwlog_lvl_configuration, "Shutting down mock serial con.\n");
   return;
}

/* To mock conhandle:
 *
 * ConTxFrame:
 *
 *  - At the protocol mock level:
 *  - Parse the frame sent with ConTxFrame to find if a reply is needed
 *  - Set mock_protocol_state based on the parsing.
 *
 *  - At the Serialapi.c/con_handle.c interface:
 *   - After ConTxFrame, Serialapi.c will call WaitResponse (which
 *     calls conUpdate) until it gets conFrameSent.  So set
 *     con_txstate to conFrameSent.  When ConUpdate is called and
 *     contxstate is conFrameSent, set con_txstate back to Idle and
 *     return conFrameSent.
 *
 *
 * ConUpdate():
 *
 *  - if con_txstate is conFrameSent, upper level is waiting for a
 *    confirmation.  Send it and go to tx state Idle.  Return conFrameSent.
 *
 *  - if upper level is expecting a reply to a previously sent frame,
 *    that should be detected in mock_protocol (or alternately
 *    controlled by the test case).  Ask mock_protocol if a reply is
 *    expected and ready.  If yes, tell mock_protocol to copy the
 *    reply to serBuf (next_incoming) and return conFrameReceived.
 *
 *  - If there are several replies to one request, this should be
 *    handled in mock_protocol.
 *
 *  - Otherwise, return conIdle.
 *
 */

/*
*   Return: conIdle          if nothing has happened
*           conFrameReceived if valid frame was received
*           conFrameSent     if transmitted frame was ACKed by other end

ConUpdate() and ConTxFrame() calls and expected return codes

- when sending frame with no reply
  ConUpdate() should not return conFrameReceived,
  then ConTxFrame() sends the frame, no return value,
  then ConUpdate() should return conFrameSent.

- when sending fram with one reply
  ConUpdate() should not return conFrameReceived,
  then ConTxFrame() sends the frame, no return value,
  then ConUpdate() should return conFrameSent,
  then ConUpdate() should return conFrameReceived and the response should be in serBuf.

- when injecting an unsolicited frame
  the conhandler mock should be idle,
  the frame should be put in serBuf as a REQUEST,
  the conhandler status should be set to conFrameReceived,
  the poll handler should be triggered (it needs two iterations, but it will re-trigger itself),
  ConUpdate should return conFrameReceived.

- when sending frame with two replies
  ConUpdate() should not return conFrameReceived,
  then ConTxFrame() sends the frame, no return value,
  then ConUpdate() should return conFrameSent,
  then ConUpdate() should return conFrameReceived and the response should be in serBuf,
  then the second response should be injected.

*/
enum T_CON_TYPE ConUpdate(BYTE acknowledge) {
   enum T_CON_TYPE retVal = conIdle;
   zgw_log_enter();

   /* The conhandle mock received a frame with ConTxFrame and the
    * gateway is still waiting for conFrameSent.
    *
    * In this scenario, the mock does not care about the rx status (it
    * is a mock). */
   if (con_txstatus == conFrameSent) {
      retVal = conFrameSent;
      /* We go straight to conIdle tx state.  This mock does not
       * support delaying pre-defined replies.
       *
       * To test a delayed reply, the test case has to inject the
       * reply frame itself. */
      con_txstatus = conIdle;
      if (reply_expected) {
         /* the most recently sent frame expects a reply */
         if (reply_ready) {
            /* the mock bridge controller has set up a reply in
             * serBuf, so the next time the gateway calls ConUpdate,
             * we should give it a conFrameReceived.
             *
             * If the mock bridge controller ran out of replies, there
             * is nothing to send, even if a reply is expected by the
             * gateway, so we continue to return conIdle. */
            zgw_log(zwlog_lvl_data,
                    "conFrameSent, reply ready, go to state conFrameReceived.\n");
            con_status = conFrameReceived;
         } else {
            /* Stay in conIdle */
            zgw_log(zwlog_lvl_data, "conFrameSent, no reply ready\n");
         }
      } else {
         zgw_log(zwlog_lvl_data, "conFrameSent, no reply expected\n");
      }
      return retVal;
   }

   if (con_status == conIdle) {
      return retVal;
   } else if (con_status == conFrameReceived) {
      /* reply has been consumed */
      reply_ready = false;
   } else {
      zgw_log(zwlog_lvl_configuration, "Unexpected conhandle status %d\n", con_status);
   }

   retVal = con_status;
   con_status = conIdle;
   return retVal;
}

/*   Transmit frame via serial port by adding SOF, Len, Type, cmd and Chksum.
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
**--------------------------------------------------------------------------*/
void ConTxFrame(
  BYTE cmd,   /* IN Command */
  BYTE type,  /* IN frame Type to send (Response or Request) */
  BYTE *Buf, /* IN pointer to BYTE buffer containing DATA to send */
  BYTE len)   /* IN the length of DATA to transmit */
{
   uint8_t tx_buffer[255];
   uint8_t *c = tx_buffer;

   if (con_txstatus != conIdle) {
      /* This must be a bug, probably in the test case.  It should not
       * be possible to transmit a second frame before conFrameSent has
       * been returned to the gateway on the first frame. */
      zgw_log(zwlog_lvl_error, "Unexpected second TX on conhandle mock\n");
   }
   zgw_log(zwlog_lvl_control, "ZGW sends frame %s, len %d, cmd 0x%02x to serial\n",
           (type == REQUEST) ? "REQUEST": "RESPONSE",
           len, cmd);
  *c++ =SOF;
  *c++ = len+3;
  *c++ = type;
  *c++ = cmd;
  memcpy(c,Buf,len);
  c+= len;
  /* skip crc in mock */

  /* After sending, Serialapi calls conUpdate() (via WaitResponse())
   * and expects conFrameSent Normally, this would wait for an ACK on
   * the serial interface, but since this is a mock, we can go
   * straight to success. */
  con_txstatus = conFrameSent;
  /* store a copy for the test case to examine */
  zgw_log(zwlog_lvl_control, "0x%02x, len(+3): 0x%02x, type: 0x%02x, cmd: 0x%02x, \n",
          tx_buffer[0], tx_buffer[1], tx_buffer[2], tx_buffer[3]);
  memcpy(tx_buf_last_sent, tx_buffer, len+4);
//  printf("\n    ");
//  for (int ii=0; ii < len+4; ii++) {
//     printf("0x%02x, ", tx_buffer[ii]);
//  }
//  printf("\n");

  /* Send the frame to the mock protocol, so that the test framework
   * can figure out what to reply. */
  mp_parse_frame(cmd, type, len, tx_buf_last_sent);
  return;
}



/* The serial interface is a layered system.
 *
 * ./src/
 *
 * serial_api_process.c - contiki proces.  Opens the serial interface
 *                        and handles Rx.
 *                        Installs a pollhandler that will be called
 *                        from the contiki main loop when select shows data on
 *                        the serial fd.  The pollhandler calls
 *                        SerialAPI_Poll(), re-triggers polling as
 *                        long as there is data, and finally calls
 *                        secure_poll() (which does what??)
 *
 * ./src/serialapi/
 *
 * Serialapi.c - implements the protocol functions from INS12350
 *               Serial API Host Appl. Prg. Guide, but also the serial
 *               interface logic.
 *               Uses internal communication interface functions
 *               SendFrame and SendFrameWithResponse on the TX side,
 *               DrainRx and Dispatch on the RX side.
 *               Provides external communication interfaces
 *               ZW_SendData() and ZW_SendData_Bridge() on the Tx side
 *               SerialAPI_Poll() on the Rx side
 *               Keeps an array of timers.
 *
 *               RX: Maintains an Rx queue (QueueFrame(), ) for ??.
 *               Dispatch() tests the incoming frame, byte
 *               pData[IDX_CMD], and calls the corresponding installed
 *               callback from callbacks.  If the command is
 *               send-data, the callback from ZW_SendData is called
 *               and cleared.  Ie, callbacks are executed in
 *               serial_api_process context. TODO: pass them on to
 *               another process context.
 *
 *               SerialAPI_Poll() is called from the poll handler of
 *               the contiki process serial_api_process.  It
 *               Dispatch()'es everything in the rx queue, calls
 *               DrainRx to queue all incoming requests (ignoring
 *               other frames), (calls ApplicationPoll, which is NULL
 *               in the ZGW), and returns true if the rx queue is not
 *               empty.
 *
 *               TX:
 *
 * conhandle.c - Generates/unpacks serial frames and handles serial
 *               protocol logic, such as waiting for ACK after
 *               sending.  Relies on the functions in port.h
 *               (linux-serial.c) to actually interface with the OS.
 *
 * in ./contiki/cpu/native/
 * linux-serial.c - handles the system file descriptor.  Also the fd
 *                  for the serial log.  Based on select/read/write.
 *                  Uses shared .h file ./src/serialapi/port.h
 *
 * contiki_main.c calls select() on the fds and calls poll on the
 * serial_api_process (and the other fds).  So when conhandle also
 * calls SerialCheck() to invoke select(), this is redundant in ZGW
 * context.  It is there so that conhandle can also function as a
 * stand-alone tool.
 *
 *
 * Note: ./contiki/core/dev/serial-line.c - reads from stdin, not from
 *       the chip.  A contiki process that maintains a ring buffer. */
