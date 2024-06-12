/* © 2019 Silicon Laboratories Inc. */

#include "zgw_data.h"

#define test_MyNodeID 1
#define test_homeID 0xffabba00

int generate_network(zgw_node_data_t **zgw_node, uint16_t num_nodes);

int teardown_network(zgw_node_data_t *zgw_node[ZW_MAX_NODES]);
