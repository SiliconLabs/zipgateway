/* © 2018 Silicon Laboratories Inc.
 */
#ifndef MULTICAST_GROUP_MANAGER_H_
#define MULTICAST_GROUP_MANAGER_H_
#include <stdint.h>
#include "zgw_nodemask.h"

/** \defgroup mcast_gm Multicast Group Manager
 * \ingroup transport
 *
 * Manage the mapping between lists of nodes and mpan group IDs.
 *
 * Rule: Expand MPAN groups when possible to match new node
 * list. Never shrink - create new MPAN group instead.
 *
 * If no more groups, re-use the oldest group, i.e.., the group
 * manager must keep track of the last used time for each group.
 *
 * @{
 *
 */

/** Type of a multicast group id */
typedef uint8_t mcast_groupid_t;

/**
 * Initialize the group manager.
 *
 * This should be called when the gateway enters a new PAN network.
 */
void mcast_group_init(void);

/**
* Get a list of nodes corresponding to a multicast group ID.  Caller
* must supply pointer to allocated \ref nodemask_t variable. This
* function will fill it out.
*
* @param[in] group_id Multicast group_id
* @param[inout] nodemask a bit mask of 232 bits each bit represents a node.
* @return TRUE nodelist found, FALSE nodelist was not found.
*/
int mcast_group_get_nodemask_by_id(mcast_groupid_t group_id, nodemask_t nodemask);

/**
 * Create or look up a group ID corresponding to a list of nodes represented as a bitmask.
 *
 * Find and return the group's current ID, if \p nodemask is already in the manager.
 *
 * If \p nodemask is not already in the manager, find the closest
 * matching subset, expand that group to contain all of nodemask, and
 * return its ID.
 *
 * If no subsets exist, create a new group in the next unused
 * slot.
 *
 * If no more slots exist, find the least recently used group,
 * discard it, and reallocate the group_id to the new group.
 *
 * The nodemask parameter is owned by caller.
 *
 * @param nodemask A bit mask with 232 bits, representing the list of nodes.
 * @return A new or existing group_id representing the list.
 */
mcast_groupid_t mcast_group_get_id_by_nodemask(const nodemask_t nodemask);

/** @}
 * */
#endif /* MULTICAST_GROUP_MANAGER_H_ */
