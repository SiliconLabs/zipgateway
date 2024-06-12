/* Copyright 2019  Silicon Laboratories Inc. */

#ifndef BRIDGE_IP_ASSOC_H_
#define BRIDGE_IP_ASSOC_H_

#include "Bridge.h"
#include "Bridge_temp_assoc.h"

/**
 * \addtogroup ip_emulation
 * @{
 */

/**
 * Maximum number of associations created by IP Association command class.
 * Since IP associations are persisted to the EEPROM file, great care should be
 * taken before this number is modified.
 */
#define MAX_IP_ASSOCIATIONS              200

/*
 * EEPROM layout of IP associations:
 * - ip_association_table_length (2 bytes): Number of IP associations (N)
 * - ip_association_table[N]     (array of N structures: ip_association_t 1..N)
 *
 * NOTE:
 * Even though the IP association table only contains MAX_IP_ASSOCIATIONS
 * elements the EEPROM table size was previously defined to account for a length
 * field plus the temp associations (temp associations are not persisted).
 * To avoid changing the EEPROM size, the table size definition is kept as
 * before.
 */
#define IP_ASSOCIATION_TABLE_EEPROM_SIZE (sizeof(uint16_t) + (MAX_IP_ASSOCIATIONS + MAX_TEMP_ASSOCIATIONS) * sizeof(ip_association_t))

/**
 * IP association types.
 * The enum values must not be changed since they are persisted to the EEPROM file.
 */
typedef enum IP_ASSOC_TYPE {
    PERMANENT_IP_ASSOC = 1,
    LOCAL_IP_ASSOC     = 2,
    PROXY_IP_ASSOC     = 3
} ip_assoc_type_t;

#define IP_ASSOC_TYPE_COUNT 3 /**< Number of values in enum IP_ASSOC_TYPE */

    /* The logical structure of the IP associations data structure is shown in tree below. 
     * It starts with association source node ID as the top of the tree.
     * The second level leaves are association source endpoints and the last level is groupings
     *           association source node-id.
     *              /                 \
     *             /                   \
     *       association            association
     *      source endpoint        source endpoint
     *        /      \               /       \
     *       /        \             /         \
     *grouping      grouping    grouping    grouping
     *  | | |       | | |          | | |       | | |
     *associations associations associations associations
     *
     * However, the data structure stored in memory resembles a flat table.
     */

/**
 * IP association structure.
 *
 * IP associations are persistent associations created with the IP Association
 * command class.
 */
typedef struct {
  void *next; /**< First item must be a pointer for the LIST macros */
  nodeid_t virtual_id; /**< This MUST be the first item following "next" (see ip_assoc_add_to_association_table()) */
  ip_assoc_type_t type; /**< unsolicited or association */
  uip_ip6addr_t resource_ip; /**< Association Destination IP */
  uint8_t resource_endpoint;  /**< From the IP_Association command. Association destination endpoint */
  uint16_t resource_port; /**< Association Destination port. NB: Stored in network byte order */
  uint8_t virtual_endpoint;   /**< From the ZIP_Command command */
  uint8_t grouping; /**< Grouping identifier */
  nodeid_t han_nodeid;   /**< Association Source node ID*/
  uint8_t han_endpoint; /**< Association Source endpoint*/
  uint8_t was_dtls;     /**< Was IP frame received via DTLS? */
  uint8_t mark_removal; /**< Set as a result of remove commands */
} ip_association_t;

/**
 * Callback function type taking a ip_association_t pointer as parameter.
 */
typedef void (*ip_assoc_cb_func_t)(ip_association_t *);

void ip_assoc_init(void);

/**
 * Save the IP associations table to persistent storage.
 */
void ip_assoc_persist_association_table(void);

/**
 * Load the IP association table from the persistent storage.
 */
void ip_assoc_unpersist_association_table(void);

/**
 * Command handler for Z/IP IP Association commands.
 *
 * @param c Z/IP connection object
 * @param payload Payload of Z/IP package
 * @param len Length of payload (in bytes)
 * @param was_dtls Was IP frame received via DTLS?
 * @return TRUE if the command was handled, FALSE otherwise
 */
BOOL handle_ip_association(zwave_connection_t* c,
                           const u8_t* payload,
                           u8_t len,
                           BOOL was_dtls);

BOOL is_ip_assoc_create_in_progress(void);

void ip_assoc_remove_by_nodeid(nodeid_t node_id);

/**
 * Locate an IP association by its virtual node ID.
 *
 * @param virtnode Virtual node ID
 * @return The IP association for virtual node virtnode
 * @return NULL if no IP association exist for virtnode
 */
ip_association_t * ip_assoc_lookup_by_virtual_node(nodeid_t virtnode);

void ip_assoc_print_association_table(void);

/**
 * @}
 */

#endif // BRIDGE_IP_ASSOC_H_