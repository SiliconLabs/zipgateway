/* © 2018 Silicon Laboratories Inc.
 */
#ifndef ZGW_NODEMASK_H_
#define ZGW_NODEMASK_H_
#include <stdint.h>

#include "ZW_transport_api.h" /* For ZW_MAX_NODES */

/* Defines to manipulate bit arrays such as our nodemasks */
#define BIT8_TST(bitnum,array) ((array[(bitnum)>>3] >> ((bitnum) & 7)) & 0x1)
#define BIT8_SET(bitnum,array) (array)[(bitnum)>>3] |= (1<<((bitnum) & 7))
#define BIT8_CLR(bitnum,array) (array)[(bitnum)>>3] &= ~(1<<((bitnum) & 7))

#define is_classic_node(nodeid) (((nodeid) > 0) && ((nodeid) <= ZW_CLASSIC_MAX_NODES))

#define is_lr_node(nodeid) (((nodeid) >= ZW_LR_MIN_NODE_ID) && ((nodeid) <= ZW_LR_MAX_NODE_ID))
/**
 * Test if a nodeid is in the valid range (1 to ZW_MAX_NODES).
 */
#define nodemask_nodeid_is_valid(nodeid) (is_classic_node(nodeid) || is_lr_node(nodeid))

/**
 * Test if a nodeid is outside the valid range (1 to ZW_MAX_NODES).
 */
#define nodemask_nodeid_is_invalid(nodeid) ((!is_classic_node(nodeid)) && (!is_lr_node(nodeid)))

/**
 * Test if node is in the nodemask.
 *
 * \param nodeid The id of the node to be checked
 * \param nodemask Node list represented as a bitmask of type \ref nodemask_t
 * \return 0 if the bit corresponding to nodeid is not in nodemask, 1 otherwise.
 */
#define NODEMASK_TEST_NODE(nodeid, nodemask) BIT8_TST((nodeid), nodemask)

/**
 * Add a node to a nodemask.
 *
 * \param nodeid The id of the node to be added.
 * \param nodemask Node list represented as a bitmask of type \ref nodemask_t
 * \return 0 if the bit corresponding to nodeid is not in nodemask, 1 otherwise.
 */
#define NODEMASK_ADD_NODE(nodeid, nodemask) BIT8_SET((nodeid), nodemask)

/**
 * Remove a node from a nodemask.
 *
 * \param nodeid The id of the node to be removed.
 * \param nodemask Node list represented as a bitmask of type \ref nodemask_t.
 * \return 0 if the bit corresponding to nodeid is not in nodemask, 1 otherwise.
 */
#define NODEMASK_REMOVE_NODE(nodeid, nodemask) BIT8_CLR((nodeid), nodemask)

/**
 * Get the long range nodelist 
 *
 * \param nodemask Long range node list represented as a bitmask of type \ref nodemask_t with offset
 */
#define NODEMASK_GET_LR(nodemask) ((nodemask) + ZW_LR_NODEMASK_OFFSET)

/**
 * Test if two nodemasks are identical.
 *
 * The masks must be non-overlapping.
 *
 * \return 0 if the masks are identical, non-zero otherwise.
 */
#define nodemask_equal(m1, m2) memcmp((m1), (m2), sizeof(nodemask_t))

/**
 * Clears all bits in nodemask.
 *
 * \param nodemask Node list represented as a bitmask of type \ref nodemask_t.
 */
#define nodemask_clear(nodemask) memset(nodemask, 0, sizeof(nodemask_t))

/**
 * Copy one nodemask to another.
 *
 * \param dest Nodemask to copy to.
 * \param src  Nodemask to copy from.
 * \return The value of dest.
 */
#define nodemask_copy(dest, src) memcpy((dest), (src), sizeof(nodemask_t));

/**
 * Type for the nodelist, represented as bits, as used on the
 * serialapi. \see SerialAPI_GetInitData().
 */
typedef uint8_t nodemask_t[ZW_MAX_NODES/8 + 1]; // +1 to accomodate LR node ID 4000

/**
 * Add a nodeID to a node list bitmask.
 * @param nodeID must be in range 1..232 or range 256..4000.
 * @param[inout] nodelist_mask Node list to modify.
 * @return 0 on succes, 1 otherwise.
 */
int nodemask_add_node(uint16_t nodeID, nodemask_t nodelist_mask);

/**
 * Test if nodeID is in nodelist_mask
 * @param nodeID must be in range 1..232 or range 256..4000.
 * @param[in] nodelist_mask Node list to test.
 * @return 1 if found, 0 otherwise.
 */
int nodemask_test_node(uint16_t nodeID, const nodemask_t nodelist_mask);

/**
 * Remove a nodeID from a node list bitmask.
 * @param nodeID must be in range 1..232 or range 256..4000.
 * @param[inout] nodelist_mask Node list to modify.
 * @return 0 on succes, 1 otherwise.
 */
int nodemask_remove_node(uint16_t nodeID, nodemask_t nodelist_mask);

#endif
