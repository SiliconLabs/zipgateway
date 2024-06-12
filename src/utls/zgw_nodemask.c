/* © 2018 Silicon Laboratories Inc.
 */

/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/
#include <ZIP_Router_logging.h>
#include "zgw_nodemask.h"

/****************************************************************************/
/*                              EXPORTED FUNCTIONS                          */
/****************************************************************************/
int nodemask_add_node(uint16_t nodeID, nodemask_t nodelist_mask)
{
  if (nodemask_nodeid_is_invalid(nodeID)) {
    return 0;
  }
  if (is_classic_node(nodeID)) {
    NODEMASK_ADD_NODE(nodeID - 1, nodelist_mask);
  } else {
    NODEMASK_ADD_NODE(nodeID, nodelist_mask);
  }
  return 1;
}

int nodemask_test_node(uint16_t nodeID, const nodemask_t nodelist_mask)
{
  if (nodemask_nodeid_is_invalid(nodeID)) {
    return 0;
  }
  if (is_classic_node(nodeID)) {
    return NODEMASK_TEST_NODE(nodeID - 1, nodelist_mask);
  } else {
    return NODEMASK_TEST_NODE(nodeID, nodelist_mask);
  }
}

int nodemask_remove_node(uint16_t nodeID, nodemask_t nodelist_mask)
{
  if (nodemask_nodeid_is_invalid(nodeID)) {
    return 0;
  }
  if (is_classic_node(nodeID)) {
    NODEMASK_REMOVE_NODE(nodeID - 1, nodelist_mask);
  } else {
    NODEMASK_REMOVE_NODE(nodeID, nodelist_mask);
  }
  return 1;
}

