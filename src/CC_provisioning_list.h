/* © 2017 Silicon Laboratories Inc.
 */
#ifndef CC_PROVISIONING_LIST_H_
#define CC_PROVISIONING_LIST_H_

/**
 *  \ingroup CMD_handler
 *  \defgroup PVL_CC_handler Provisioning List CC Handler
 *
 * @{
 */

#include <command_handler.h>
#include <provisioning_list.h>

/**
 * Initialize the CC_provisioning_list module.
 */
void CC_provisioning_list_init(void);

/**
 *  Provisioning List Command Handler
 *  \return command_handler_codes_t
 */
command_handler_codes_t
PVL_CommandHandler(zwave_connection_t *c,uint8_t* pData, uint16_t bDatalen);



/**
 * Build a provision list report, and fill the buffer.
 *
 * \param buf Destination to receive the report. The buffer must be big enough to hold the data
 * \param entry Entry to send, if entry is empty an empty report will be sent.
 * \param seqNo Sequence number to use in the report.
 * \return the length of data filled.
 */

int CC_provisioning_list_build_report(uint8_t* buf,provisioning_list_t *entry,int8_t seqNo);

/** @} */
#endif /* CC_PROVISIONING_LIST_H_ */
