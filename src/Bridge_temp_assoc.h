/* Copyright 2019  Silicon Laboratories Inc. */

#ifndef BRIDGE_TEMP_ASSOC_H_
#define BRIDGE_TEMP_ASSOC_H_

#include "Bridge.h"

/**
 * \addtogroup ip_emulation
 * @{
 */

/**
 * Number of temporary associations.
 * Also equals the number of preallocated virtual nodes.
 */
#define MAX_LR_TEMP_ASSOCIATIONS         4
#define MAX_CLASSIC_TEMP_ASSOCIATIONS    4
#define MAX_TEMP_ASSOCIATIONS            (MAX_LR_TEMP_ASSOCIATIONS + MAX_CLASSIC_TEMP_ASSOCIATIONS)
#define PREALLOCATED_VIRTUAL_NODE_COUNT  MAX_CLASSIC_TEMP_ASSOCIATIONS /**<  \deprecated use MAX_TEMP_ASSOCIATIONS */

/**
 * <b>Temporary associations</b> are automatically created whenever a Z/IP
 * client communicates with a Z-Wave node. The maximum number of temporary
 * associations known to the gateway is defined in #MAX_TEMP_ASSOCIATIONS. If
 * all temp associations are is use and there is a need for yet another new temp
 * association, the oldest association is reused.
 *
 * Since each temporary association is exposed to the Z-Wave network via a
 * virtual node, the gateway allocates #MAX_TEMP_ASSOCIATIONS virtual nodes
 * whenever it is started. The virtual node IDs allocated for temp associations
 * are persisted to the EEPROM file.
 *
 * A temporary association is associating a Z/IP client (identified by its IP
 * address and port) to a virtual node ID. Even if the Z/IP client is
 * communicating with many Z-Wave nodes, only a single temporary association
 * (and virtual node id) is allocated for that client.
 *
 * \note If more than #MAX_TEMP_ASSOCIATIONS clients are communicating with Z/IP
 * gateway at the same time, the oldest temp associations will be taken/re-used
 * by the clients connecting last.
 *
 * \note In addition to the pool of #MAX_TEMP_ASSOCIATIONS temp associations, a
 * global variable handles a temp association with the Z/IP client
 * configured as the unsolicited destination. See unsol_temp_assoc.
 */
typedef struct {
  void *next; /**< First item must be a pointer for the LIST macros */
  nodeid_t virtual_id; /**< Currently active id (equal either to virtual_node_id_static or MyNodeID). This MUST be the first item following "next" (see temp_assoc_add_to_association_table()) */
  nodeid_t virtual_id_static; /**< Never changes after initial assignment. This MUST be the first item following "virtual_id" (see temp_assoc_add_to_association_table()) */
  uip_ip6addr_t resource_ip;  /**< Association destination IP */
  uint8_t resource_endpoint;  /**< Association destination endpoint */
  uint16_t resource_port; /**< Association destination port. NB: Stored in network byte order */
  uint8_t was_dtls;     /**< Was IP frame received via DTLS? */
  bool is_long_range;
} temp_association_t;

/**
 * temp_assoc_fw_lock_t is used to track temporary associations that are created
 * by a firmware update (OTA) command. Those associations should be locked from
 * changes until they are released or the lock times out.
 */
typedef struct {
  temp_association_t *locked_a;
  struct ctimer reset_fw_timer;
} temp_assoc_fw_lock_t;

extern temp_assoc_fw_lock_t temp_assoc_fw_lock;

void temp_assoc_init(void);

void temp_assoc_resume_init(void);

/**
 * Save to persistent storage the list of virtual node IDs that have been
 * pre-allocated for temporary associations.
 */
void temp_assoc_persist_virtual_nodeids(void);

/**
 * Load virtual node IDs for temp associations from persistent storage.
 */
void temp_assoc_unpersist_virtual_nodeids(void);

/**
 * Start adding the virtual nodes preallocated for temporary associations after
 * a zero-second delay.
 *
 * The reason for the zero delay is unclear. It is possible that the reason
 * is to decouple the call
 * to temp_assoc_add_virtual_nodes from the current call-stack.
 * This is legacy functionality.
 */
void
temp_assoc_delayed_add_virtual_nodes(void);

/**
 * Create a temporary association and assign to a virtual node.
 *
 * @param  was_dtls Indicates whether the request from the ZIP client was received via DTLS.
 * @return pointer to a temporary association
 * @return NULL if error
 */
temp_association_t *
temp_assoc_create(uint8_t was_dtls);

/**
 * Mark the temp association as locked by the firmware update.
 *
 * Protects a temporary association created by a firmware update request from
 * being reused.
 *
 * Since a firmware update is a long-running operation, several requests for new
 * temporary associations could arrive in that timespan. At some point, the
 * firmware update temp association will be the oldest and a candidate for being
 * reused. Locking it protects it from that.
 *
 * \note Only the temporary association last used with this function will be
 *       locked.
 *
 * @param a the temporary association to lock from being reused.
 */
void
temp_assoc_register_fw_lock(temp_association_t *a);

/**
 * Locate a temporary association by its virtual node ID.
 *
 * @param virtual_nodeid Virtual node ID assigned to temp association
 * @return Pointer to temporary association with a matching virtual node ID
 * @return NULL if no match was found
 */
temp_association_t *
temp_assoc_lookup_by_virtual_nodeid(nodeid_t virtual_nodeid);


/**
 * Release the firmware update lock held on a temporary association.
 *
 * \note Globally, there is only one firmware lock. This function will release it
 * even if it does not point to the association passed in the user parameter.
 *
 * It is assumed this function is called on timeout of a timer set just after
 * the association referenced by user data pointer has been locked. This should
 * ensure that the correct association is locked when this
 * function is called.
 *
 * @param user pointer to temporary association assumed to be locked for
 *             firmware update
 */
void temp_assoc_fw_lock_release_on_timeout(void *user);

void temp_assoc_print_association_table(void);

void temp_assoc_print_virtual_node_ids(void);

/**
 * @}
 */
#endif // BRIDGE_TEMP_ASSOC_H_
