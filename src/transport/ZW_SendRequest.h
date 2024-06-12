/* © 2014 Silicon Laboratories Inc.
 */

#ifndef ZW_SENDREQUEST_H_
#define ZW_SENDREQUEST_H_

/** \ingroup transport
 * \defgroup Send_Request Send Request
 *
 * Send a request i.e., a Z-Wave GET style command, and trigger a callback when
 * the command is received.
 * @{
 */
#include "TYPES.H"
#include "ZW_basis_api.h"
#include "ZW_SendDataAppl.h"
#include "sys/cc.h"

/**
 * Callback to  \ref ZW_SendRequest.
 *
 * \param txStatus see \ref ZW_SendData
 * \param rxStatus see \ref SerialAPI_Callbacks::ApplicationCommandHandler
 * \param pCmd pointer to received data, NULL if no response was received.
 * \param cmdLength length of received data 0, if no response was received.
 * \param user User defined argument, send with \ref ZW_SendRequest
 *
 * \return True if more responses are expected. False if none is expected
 * For example, a multi channel endpoint find expects more multi channel find report
 * frames in response if the device has a big number of endpoints which cannot
 * fit in on the Z-wave frame.
 */
typedef int( *ZW_SendRequst_Callback_t)(
		BYTE txStatus,
		BYTE rxStatus,
		ZW_APPLICATION_TX_BUFFER *pCmd,
		WORD cmdLength,
		void* user) CC_REENTRANT_ARG;

/**
 * Send a request to a node and trigger the callback once the
 * response is received.
 *
 * \param p See \ref ZW_SendDataAppl
 * \param pData See \ref ZW_SendDataAppl
 * \param dataLength See \ref ZW_SendDataAppl
 * \param responseCmd Expected command to receive. The command class is derived from the first
 * byte of pData.
 * \param user User defined value which will be return in \ref ZW_SendRequst_Callback_t
 *
 * \return True if the command was sent.
 */
BYTE ZW_SendRequest(
    ts_param_t* p,
  const BYTE *pData,
  BYTE  dataLength,
  BYTE  responseCmd,
  WORD  timeout,
  void* user,
  ZW_SendRequst_Callback_t callback);

/**
 * Application command handler to match incoming frames to pending requests.
 */
BOOL SendRequest_ApplicationCommandHandler(ts_param_t* p,
    ZW_APPLICATION_TX_BUFFER *pCmd, WORD cmdLength);


/**
 * Initialize the ZW_SendRequest state machine.
 */
void ZW_SendRequest_init();

/*
 * Abort send request for particular nodeid.
 * This command is used when removing the node. All pending Send Requests to that node need to be aborted.
 */
void ZW_Abort_SendRequest(uint8_t nodeid);

/**
 * @}
 */

#endif /* ZW_SENDREQUEST_H_ */
