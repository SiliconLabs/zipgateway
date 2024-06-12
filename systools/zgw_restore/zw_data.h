/* © 2019 Silicon Laboratories Inc. */

#ifndef _ZW_DATA_H
#define _ZW_DATA_H

#include <stdint.h>
#include "stdbool.h"

#include "ZW_transport_api.h"
#include "RD_types.h"

/* =========================== Z-Wave Node Info Data Types ========================== */

/**
 * \defgroup restore-db-zw Restore Tool Internal Data for Z-Wave
 * \ingroup restore-db
@{
 */

/**
 * Z-Wave type
 */
typedef struct zw_node_type {
   uint8_t basic;
   uint8_t generic;
   uint8_t specific;
} zw_node_type_t;

/**
 * Z-Wave type
 */
typedef struct zw_node_data {
   nodeid_t node_id;
   uint8_t *neighbors; /**< Not relevant for ZGW */
   uint8_t capability;
   uint8_t security; /**< FLIRS properties, etc. Not related to S0/S2 security */
   zw_node_type_t node_type;
} zw_node_data_t;

/**
 * Z-Wave type
 *
 * NVM data
 */
typedef struct zw_controller {
   /** The ID of the SUC/SIS in the network. */
   uint8_t SUCid;
   uint8_t *cc_list;

   /** Array of pointers to Z-Wave node data from the bridge
    * controller.  (node_id-1) indexed. */
   zw_node_data_t *included_nodes[ZW_MAX_NODES];
} zw_controller_t;



/* Put data into the internal representation.
 */

/**
 * Add the controller to the global backup data.
 *
 * This function keeps the cc_list.  The nodelist is cleared, so that
 * it is ready for adding new nodes.
 *
 * \param SUCid Node ID of the SUC/SIS node in the network.
 * \param cc_list An array of uint8_t containing command classes.
 * \return Pointer to the controller in the global backup structure.
 */
zw_controller_t* zw_controller_add(uint8_t SUCid, uint8_t *cc_list);

/** Create a stand-alone Z-Wave controller structure.
 *
 * Allocate and initialize a separate ZW-controller structure.  The
 * controller's node ID and home ID must be available in the usual
 * globals.  The SUC ID is used to determine if the controller itself
 * is SUC/SIS.
 *
 * \param SUCid The node ID of the SUC/SIS of the Z-Wave network.
 * \return Pointer to a newly allocated controller structure.
 */
zw_controller_t * zw_controller_init(uint8_t SUCid);

/** Add a node to a controller structure.
 * This function steals the node_data.
 *
 * \return false if the node already exists or if the nodeid is out of range.
 */
bool zw_controller_add_node(zw_controller_t * ctrl,
                            nodeid_t nodeID, zw_node_data_t *node_data);

/** Add a Z-Wave node to a controller structure.
 * This function allocates data to store the node.
 *
 * \return false if the node already exists or if the nodeid is out of range.
 */
bool zw_node_add(zw_controller_t *zw_controller, nodeid_t node_id,
                uint8_t capability, uint8_t security,
                const zw_node_type_t *node_type);

/*
 * Get data from the internal representation.
 */

/**
 * @}
 */

#endif
