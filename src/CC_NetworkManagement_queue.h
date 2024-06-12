/**
 * @copyright Copyright (c) 2021 Silicon Laboratories Inc.
 * 
 * This module implement a queue allowing to perform network management commands
 * on sleeping devices. The commands will be queued and executed once the node
 * wakes up.
 */

#include "ZW_classcmd.h"
#include "ZW_udp_server.h"

/**
 * @brief Queue Networkmanagement command if the target node is a mailbox node
 *
 * In the case the the command passed here is a network management command
 * operating on a particular node, the command is queued if the node is a wakeup
 * node.
 *
 * If the mailbox happens to be already serving the node, then this command
 * returns false, because this means that the node is awake.
 *
 * @param conn Connetion object used to known where to reply to
 * @param pCmd Command to possibly queue
 * @param bDatalen Length of command
 * @return true The command has been queued, i.e. it should not be processed now
 * @return false  The target node of the command is not a mailbox node.
 */
bool NetworkManagement_queue_if_target_is_mailbox(zwave_connection_t*conn, ZW_APPLICATION_TX_BUFFER* pCmd, BYTE bDatalen);

/**
 * @brief Remove all queued commends for a given node
 * 
 * This command will remove all commands to the given node from the queue. A
 * NACK will be send to the original sender for each delete command.
 * 
 * @param node 
 */
void NetworkManagement_queue_purge_node( nodeid_t node);

/**
 * @brief Remove all commands in the queue
 * 
 * A NACK will be send to the original sender for each queued command.
 */
void NetworkManagement_queue_purge_all( );

/**
 * @brief Notify the queue that a node has been woken
 * 
 * This function will check if we have any queued commands for the given node.
 * If there ware queued command the commands will be executed. When the last
 * command has been completed mb_wakeup_notify is called. 
 * 
 * @param node  Node that has woken up
 * @return true This queue has started executing the commands.
 * @return false No commands was queued for this node.
 */
bool NetworkManagement_queue_wakeup_event( nodeid_t node);

/**
 * @brief Notify the network management queue that network management has completed
 * 
 * This is used to progress the queue and send the next command.
 */
void NetworkManagement_queue_nm_done_event();

