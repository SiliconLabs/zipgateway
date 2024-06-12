/* © 2014 Silicon Laboratories Inc.
 */

#ifndef ROUTER_EVENTS_H_
#define ROUTER_EVENTS_H_

#include "sys/process.h"

/**
 * \addtogroup ZIP_Router
 * @{
 */

/**
 * Events for the \ref ZIP_Router.
 */
enum {
  /**
   * The Z/IP Gateway will reset itself upon receiving this event.
   */
  ZIP_EVENT_RESET,

  /**
   * Emitted when the portal tunnel has been established.
   */
  ZIP_EVENT_TUNNEL_READY,
  /**
   * Emitted when a node has got a new ip addres, the data argument contains the nodeid
   */
  ZIP_EVENT_NODE_IPV4_ASSIGNED,

  /**
   * Emitted when the DHCP process has given up trying to aquire an IP address. The data
   * argument contains the node id
   */
  ZIP_EVENT_NODE_DHCP_TIMEOUT,
  /**
   * Emitted when we are done sending an ip datagram over the LAN
   */
  ZIP_EVENT_SEND_DONE,

  /**
   * Emitted when the Z/IP Gateway has entered a new network.
   */
  ZIP_EVENT_NEW_NETWORK,

  /**
   * Node queue has been updated
   */
  ZIP_EVENT_QUEUE_UPDATED,

  /**
   * Resource Directory has finished node probes.
   *
   * All nodes in the resource directory is in one of the final states
   * and RD is not locked.
   */
  ZIP_EVENT_ALL_NODES_PROBED,
  /** Bridge has been initialized.
   *
   * Allocation of virtual nodes may have been completed, not needed,
   * or timed out.
   */
  ZIP_EVENT_BRIDGE_INITIALIZED,
  /**
   * The DCHP discover/renew round has completed, we have acquired an
   * ip address for all nodes or timed out trying to acquire the
   * address.
   */
  ZIP_EVENT_ALL_IPV4_ASSIGNED,
  /**
   * Event triggered by CC_PowerLevel, Mailbox, and node queues when they become idle.
   *
   * Not used by, eg, Network Management, since there is a separate event for that.
   *
   * ZIP_Router can then check if a backup is requested and, if all
   * other components are idle, start the backup.
   */
  ZIP_EVENT_COMPONENT_DONE,
  /** Event triggered from signal handler when the zipgateway should
   * back up as soon as it is idle. */
  ZIP_EVENT_BACKUP_REQUEST,

  /** Network Management has completed a request and NMS state has
   * returned to #NM_IDLE.
   */
  ZIP_EVENT_NETWORK_MANAGEMENT_DONE,
  /** Event to trigger reply to node removal frame indication
   * completion of all associations and virtual node removal related to
   * that node is done */
  ZIP_EVENT_NM_VIRT_NODE_REMOVE_DONE,
  /** Event emitted by \ref ZIP_Resource when it has completed a node
   * probe (ending in STATUS_PROBE_FAIL or STATUS_DONE).  Event data
   * is FIXME.
   */
  ZIP_EVENT_NODE_PROBED,
};


PROCESS_NAME(zip_process);

/**
 * @}
 */
#endif /* ROUTER_EVENTS_H_ */
