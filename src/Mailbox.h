/* © 2014 Silicon Laboratories Inc.
 */

#ifndef MAILBOX_H_
#define MAILBOX_H_

/****************************  Mailbox.c  ****************************
 *           #######
 *           ##  ##
 *           #  ##    ####   #####    #####  ##  ##   #####
 *             ##    ##  ##  ##  ##  ##      ##  ##  ##
 *            ##  #  ######  ##  ##   ####   ##  ##   ####
 *           ##  ##  ##      ##  ##      ##   #####      ##
 *          #######   ####   ##  ##  #####       ##  #####
 *                                           #####
 *          Z-Wave, the wireless language.
 *
 *              Copyright (c) 2012
 *              Zensys A/S
 *              Denmark
 *
 *              All Rights Reserved
 *
 *    This source file is subject to the terms and conditions of the
 *    Zensys Software License Agreement which restricts the manner
 *    in which it may be used.
 *
 *---------------------------------------------------------------------------
 *
 * Description: ...
 *
 * Author:   aes
 *
 * Last Changed By:  $Author: $
 * Revision:         $Revision: $
 * Last Changed:     $Date: $
 *
 ****************************************************************************/
#include "contiki.h"
#include "contiki-net.h"
#include "Serialapi.h"
#include "command_handler.h"
#include "RD_types.h"

/**
 * \defgroup mailbox Z/IP Mailbox service
 *
 * The Mail Box provides support for any IP application to communicate with
 * Wake Up Nodes without them having to implement or understand the
 * Wake Up Command class. The principle of the mail box functionality means
 * that sending application does not need any knowledge about the receiving
 * node sleeping state. The sending application will simply receive a “Delayed”
 * packet each minute, indicating that ACK is expected at a later point, and
 *  that the application should not attempt retransmission.
 *
 * In addition, the Mailbox supports the Mailbox Command Class,
 * which allows a lightweight Z/IP Gateway to offload the mailbox functionality
 * to a more powerful mailbox service, such as a portal.
 *
 * The Z/IP Mailbox service is enabled by default.
 * The Z/IP client can use directly the Z/IP Gateway Mailbox functionality, or
 * the Z/IP clients may implement their own mailbox service. If the clients use their own mailbox service,
 * they can disable the Z/IP Gateway mailbox service using Mailbox Configuration Set Command,
 * or updating the ZipMBMode in Z/IP Gateway CONFIGURATION FILE ( <a href="zip.html">Manual page of Z/IP Gateway </a>).
 *
 * As mentioned above, the mailbox settings are configured using Z/IP Gateway configuration file or sending Z-Wave commands from Z/IP Clients.
 * When new mailbox settings are pushed from the portal, the Z/IP Gateway configuration file (zipgateway.cfg) will be updated.
 * This allows the Z/IP Gateway to persist the mailbox operation mode after reboot.
 *
 * @{
 */

/**
 * Maximum number of entries the mailbox can hold. This number is a shared capacity among all sleeping devices in the network.
 */
#define MAX_MAILBOX_ENTRIES 2000

/**
 * Send post the package in uip_buf.
 */
uint8_t mb_post_uipbuf(uip_ip6addr_t* proxy,uint8_t handle);


/**
 * Call when a node was found to be failing.
 */
void
mb_failing_notify(nodeid_t node);


/**
 * Send a node to sleep after no transmissions were sent to it for a while.
 */
void
mb_put_node_to_sleep_later(nodeid_t node);

/**
 * Purge all messages in the mailbox queue destined for a particular node.
 * Call instead of mb_failing_notify() for nodes without the fixed wakeup interval.
 */
void mb_purge_messages(nodeid_t node);

/**
 * Initialize the mailbox module.
 */
void
mb_init();

/**
 * Call when a Wake Up Notification (WUN) is received from the Z-Wave interface.
 * \param node NodeID sending the WUN
 * \param is_broadcast Flag set to true if WUN was received as broadcast.
 *
 */
void
mb_wakeup_event(
    nodeid_t node,
    u8_t is_broadcast);


uint8_t mb_enabled();

/**
 * Return true if mailbox is in state MB_IDLE.
 */
bool mb_idle(void);

/**
 * Return true if the mailbox is busy.
 *
 * Busy means actively sending, i.e., state is neither
 * MB_STATE_IDLE nor MB_STATE_SEND_NO_MORE_INFO_DELAYED.
 *
 * \note This is not the opposite of idle.
 */
uint8_t mb_is_busy(void);


/** Event telling the mailbox that we have successfully sent a frame to the node */
void mb_node_transmission_ok_event(nodeid_t node);

/**
 * @brief Returns the nodeid of the node currently being served by the mailbox.
 * 
 * 
 * @return node_id_t 0 if the mailbox is not active. 
 */
nodeid_t mb_get_active_node();

/**
 * Abort mailbox transmission. This function has no effect if the mailbox is not active.
 */
void mb_abort_sending();

uint8_t is_zip_pkt(uint8_t *);
/**
 * @}
 */
#endif /* MAILBOX_H_ */

