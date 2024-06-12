/* © 2014 Silicon Laboratories Inc. */

#ifndef RESOURCEDIRECTORY_H
#define RESOURCEDIRECTORY_H

#include "TYPES.H"
#include "ZW_SendDataAppl.h"
#include "sys/ctimer.h"
#include "sys/clock.h"

/**
 * \ingroup ZIP_MDNS
 * \defgroup ZIP_Resource Z/IP Resource Directory
 *
 * The Resource Directory is a database containing information about
 * all nodes in the PAN network.
 *
 * This module probes/interviews nodes in the PAN network for their
 * supported command classes, security, multi-endpoint capabilities,
 * etc., and stores the results.
 *
 * #### Dead Node Detection ####
 *
 * The Resource Directory also tries to keep track of the health state
 * of the nodes in the PAN network by tracking whether the last
 * communication with a node was successfull.
 *
 * For #MODE_ALWAYSLISTENING nodes and FLIRS nodes
 * (#MODE_FREQUENTLYLISTENING), the node is set to failing
 * (#STATUS_FAILING) if it fails to respond to a frame sent by a
 * client.
 *
 * Since a wake-up node (#MODE_MAILBOX) may be unresponsive even when
 * it is healthy, the Resource Directory only sets it failing if it
 * has not been heard from in 3 times its wake-up interval.  The RD
 * does this by periodically checking the mailbox nodes
 * (#rd_check_for_dead_nodes_worker()).
 *
 * \note This implies that a mailbox node with wake-up interval 0 is never set
 * failing.
 *
 * In either case, as soon as the Z/IP Gateway receives a frame from a
 * failing node, it will mark it as not failing (#STATUS_DONE).  The
 * received frame could be a command or an ACK.
 *
 * This tracking only starts after the initial inteview has been
 * completed successfully.  If a new interview is requested
 * (#NODE_INFO_CACHED_GET), tracking is again suspended until
 * successful completion.
 *
 * \see #rd_set_failing, #rd_node_is_alive(), #rd_node_is_unreachable().
 *
 *
 * #### mDNS Support ####
 *
 * The Resource Directory works closely with the \ref ZIP_MDNS. \ref
 * ZIP_MDNS uses the information collected in the resource directory
 * to send replies on the LAN side.  When nodes are added or removed
 * on the PAN side, the Resource Directory may trigger gratuitous mDNS
 * packets on the LAN side (e.g., mDNS goodbye).
 *
 * #### Network Management Interaction ####
 * The Resource Directory also works closely with \ref NW_CMD_handler.
 * The NetworkManagement module can create and destroy entries in the
 * database and triggers/blocks the probe machine.
 *
 * @{
 */

#include "RD_internal.h"

/**
 * Flag to indicate that a node has been deleted from the network.
 *
 * This flag is set on the #rd_node_database_entry_t::mode field.
 *
 * When a node has been removed from the PAN network, the gateway
 * keeps the data-structures associated with the node for a brief time
 * longer, so that it can announce the removal on mDNS.
 */
#define MODE_FLAGS_DELETED 0x0100
#define MODE_FLAGS_FAILED  0x0200
#define MODE_FLAGS_LOWBAT  0x0400


#define RD_ALL_NODES 0

/**
 * 
 */
typedef struct rd_group_entry {
  char name[64];
  list_t* endpoints;
} rd_group_entry_t;

/**
 * Register a new node or update an existing node in the RD database.
 *
 * If the node does not have an IPv4 address, this will also start DHCP.
 *
 * If the node exists in RD, and is in a final probe state, the RD probe
 * engine will re-interview the node.
 *
 * If the node does not exist in RD, an rd_node_database_entry for the node is created.
 *
 * The entry is populated with updated protocol info, the \ref
 * rd_node_database_entry::node_properties_flags are set from the
 * parameter, and endpoint 0 (the root device) is created.
 *
 * Finally, the node is queued in the RD probe engine to be
 * interviewed.
 *
 * The caller should set #RD_NODE_FLAG_JUST_ADDED on the
 * node_properties_flag if the zipgateway is still adding the node.
 * The #RD_NODE_FLAG_ADDED_BY_ME should be set if the zipgateway
 * handled the security negotiations on the node, i.e., the security
 * keys are known.
 *
 * @param node Node id of the node to create/update.
 * @param node_properties_flags Node property flags (only set for new nodes).
 */
void rd_register_new_node(nodeid_t node, uint8_t node_properties_flags);
/**
 * Do a complete re-discovery and "hijack" wakeup nodes
 */
void rd_full_network_discovery();

/**
 * Re-discover nodes which are not in the database
 * @return the number of new nodes to probe
 */
u8_t rd_probe_new_nodes();

/**
 * Remove a node from resource database
 */
void rd_remove_node(nodeid_t node);


/**
 * Initialize the resource directory and the nat table (and DHCP).
 *
 * @param lock boolean, set true if node probe machine should be locked after init
 * @return TRUE if the homeID has changed since the last rd_init.
 */
int rd_init(uint8_t lock);

/**
 * Gracefully shut down Resource Directory and mDNS.
 *
 * Mark all nodes and endpoints as \ref MODE_FLAGS_DELETED.  Queue up
 * sending of goodbye packets for all endpoints.  Call mdns_exit() to
 * tell the mDNS process to shut down when the queue is empty.
 *
 * \note It is mandatory to call rd_destroy() before calling rd_init()
 * again.  No actual freeing of memory is done in rd_exit(), since
 * mDNS needs access to the nodes until the goodbyes are sent.
 */
void rd_exit();

/**
 * Finally free memory allocated by RD
 */
void rd_destroy();

/**
 *  Lock/Unlock the node probe machine. When the node probe lock is enabled, all probing will stop.
 *  Probing is resumed when the lock is disabled. The probe lock is used during a add node process or during learn mode.
 *  \param enable Boolean - whether to enable the lock.
 */
void rd_probe_lock(uint8_t enable);

/**
 * Unlock the probe machine but do not resume probe engine.
 *
 * If probe was locked during NM add node, but the node should not be
 * probed because it is a self-destructing smart start node, this
 * function resets the probe lock.
 *
 * When removal of the node succeeds, \ref current_probe_entry will be
 * reset when the node is deleted.  We also clear \ref
 * current_probe_entry here so that this function can be used in the
 * "removal failed" scenarios.
 */
void rd_probe_cancel(void);

/**
 * Retrieve a
 */
rd_ep_database_entry_t* rd_lookup_by_ep_name(const char* name,const char* location);
rd_node_database_entry_t* rd_lookup_by_node_name(const char* name);

rd_group_entry_t* rd_lookup_group_by_name(const char* name);

/**
 * \param node A valid node id or #RD_ALL_NODES.
 */
rd_ep_database_entry_t* rd_ep_first(nodeid_t node);
rd_ep_database_entry_t* rd_ep_iterator_group_begin(rd_group_entry_t*);
rd_ep_database_entry_t* rd_ep_next(nodeid_t node,rd_ep_database_entry_t* ep);
rd_ep_database_entry_t* rd_group_ep_iterator_next(rd_group_entry_t* ge,rd_ep_database_entry_t* ep);

/**
 * Used with \ref rd_ep_class_support()
 */
#define SUPPORTED_NON_SEC   0x1
/**
 * Used with \ref rd_ep_class_support()
 */
#define CONTROLLED_NON_SEC  0x2
/**
 * Used with \ref rd_ep_class_support()
 */
#define SUPPORTED_SEC       0x4
/**
 * Used with \ref rd_ep_class_support()
 */
#define CONTROLLED_SEC      0x8
/**
 * Used with \ref rd_ep_class_support()
 */
#define SUPPORTED  0x5
/**
 * Used with \ref rd_ep_class_support()
 */
#define CONTROLLED 0xA

/**
 * Look up a command class in an end point and return capability mask.
 *
 * \param ep Pointer to the #rd_ep_database_entry for the endpoint.
 * \param cls The command class to look for.
 */
int rd_ep_class_support(rd_ep_database_entry_t* ep, uint16_t cls);

/** Called from \ref ApplicationControllerUpdate, when a node info is
 * received or if the ZW_RequestNodeInfo is failed.
 *
 * @param bStatus if the request went ok
 * \param bNodeID Node id of the node that send node info
 * \param nif Pointer to Application Node information
 * \param nif_len Node info length.
 */
void rd_nif_request_notify(uint8_t bStatus, nodeid_t bNodeID, uint8_t* nif, uint8_t nif_len);

/** Register a node probe-completion notifier.
 *
 * \param node_id The node that needs to be probed.
 * \param user Caller data.
 * \param callback Notifier function.
 * \return 1 if the callback has been registered, 0 otherwise.
 */
int rd_register_node_probe_notifier(nodeid_t node_id, void* user,
                                    void (*callback)(rd_ep_database_entry_t* ep, void* user));

/**
 * Get nodedatabase entry from data store. free_node_dbe() MUST
 * be called when the node entry is no longer needed.
 */
rd_node_database_entry_t* rd_get_node_dbe(nodeid_t nodeid);

/**
 * MUST be called when a node entry is no longer needed.
 */
void rd_free_node_dbe(rd_node_database_entry_t* n);


/**
 * Lookup node id in \ref node_db.
 *
 * \ingroup node_db
 *
 * @param node The node id
 * @return Returns true if node is registered in database
 */
u8_t rd_node_exists(nodeid_t node);

/**
 * Get an endpoint entry in the \ref node_db from nodeid and epid.
 *
 * @return A pointer to an endpoint entry or NULL.
 */
rd_ep_database_entry_t* rd_get_ep(nodeid_t nodeid, uint8_t epid);

/**
 * Get the entire mode field of the node with id \p nodeid.
 *
 * The mode field contains the \ref rd_node_mode_t enum and the mode
 * flags, eg, \ref MODE_FLAGS_DELETED.
 *
 * \param nodeid A nodeid.
 * \return The node's mode and mode flags if the node exists, MODE_FLAGS_DELETED if \p nodeid is not in the network.
 */
rd_node_mode_t rd_get_node_mode(nodeid_t nodeid);

rd_node_state_t rd_get_node_state(nodeid_t nodeid);

/**
 * Get the probe flags of one node
 */
uint16_t rd_get_node_probe_flags(nodeid_t nodeid);


/** Notify \ref node_db that a node is not responding.
 *
 * Should be called when communication with a node failed, e.g., when
 * sending a command to the node failed.
 *
 * If the node is currently being probed, it will not be changed.
 * If the node is \ref MODE_MAILBOX, it will not be changed.
 *
 * Otherwise, if the mode has STATUS_DONE, it will be changed to
 * STATUS_FAILING and the fail will be announced.
 *
 * \param node The node id.
 */
void rd_node_is_unreachable(nodeid_t node);

/** Notify \ref node_db that a node is alive.
 *
 * Should be called whenever a node has been found to be alive,
 * i.e., when a command was successfully sent to the node or when we
 * received a command from the node.
 *
 * If the node was STATUS_FAILING, it will be set to STATUS_DONE and
 * the MODE_FLAGS_FAILED will be cleared.
 *
 * \param node The node id.
 */
void rd_node_is_alive(nodeid_t node);

/** Set failing status of a node in \ref node_db.
 */
void rd_set_failing(rd_node_database_entry_t* n,uint8_t failing);

/**
 * Sets name/location of an endpoint. This will trigger a mdns probing. Note that
 * the name might be changed if the probe fails.
 */

void rd_update_ep_name(rd_ep_database_entry_t* ep,char* name,u8_t size);

void rd_update_ep_location(rd_ep_database_entry_t* ep,char* name,u8_t size);

/**
 * Update both the name and location of an endpoint.
 *
 * For creating a name/location during inclusion use \ref rd_add_name_and_location().
 */
void
rd_update_ep_name_and_location(rd_ep_database_entry_t* ep, char* name,
    u8_t name_size, char* location, u8_t location_size);

/**
 * return true if a probe is in progress
 */
u8_t rd_probe_in_progress();

/**
 * Mark the mode of node MODE_FLAGS_DELETED so that
 */
void rd_mark_node_deleted(nodeid_t nodeid);

/**
 * Check node database to see if there are any more nodes to probe.
 *
 * If a probe is already ongoing (#current_probe_entry is not NULL), resume that probe.
 *
 * Then probe all nodes not in one of the final states: #STATUS_DONE,
 * #STATUS_PROBE_FAIL, or #STATUS_FAILING.
 *
 * Post #ZIP_EVENT_ALL_NODES_PROBED when all nodes are in a final
 * state and the probe machine is unlocked.
 */
void rd_probe_resume();

/** Start probe of \a n or resume probe of #current_probe_entry.
 *
 * Do nothing if probe machine is locked, bridge is \ref booting, \ref
 * ZIP_MDNS is not running or another node probe is on/going.
 *
 * Set #current_probe_entry to \a n if it is not set.
 *
 * When probe is complete, store node data in eeprom file, send out
 * mDNS notification for all endpoints, and trigger ep probe callback
 * if it exists.
 *
 * Finally call \ref rd_probe_resume(), to trigger probe of the next
 * node.
 *
 * \param n A node to probe.
 */
void rd_node_probe_update(rd_node_database_entry_t* n);

/**
 * Check whether a frame should be forwarded to the unsolicited destination or not, based on
 *    - its command type supporting/controlling,
 *    - the security level which the frame was received on
 *    - if its present in the secure/non-secure nif.
 *
 *    @param rnode    Remote node
 *    @param scheme   Scheme which was used to send the frame
 *    @param pData    Data of frame
 *    @param bDatalen length of data.
 */
int rd_check_security_for_unsolicited_dest( nodeid_t rnode,  security_scheme_t scheme, void *pData, u16_t bDatalen );

/** Get the timeout value for add node function based on the types of nodes
 *
 * @param is_controller If the node being added is controller or not
*/
clock_time_t rd_calculate_inclusion_timeout(BOOL is_controller);

/* This is used for enabling
 * ADD_NODE_OPTION_SFLND mode in ZW_AddNodeToNetwork() 
 */
clock_time_t rd_calculate_s2_inclusion();


/**
 * @brief Check if the node is a controller.
 * 
 * This function checks is controller bit is set in the NIF protocol
 * field called security. All controllers will have this bit set, 
 * no matter what their device type is.
 */
bool rd_check_nif_security_controller_flag( nodeid_t node );
bool sleeping_node_is_in_firmware_upgrade(nodeid_t node);
/**  @} */
#endif

