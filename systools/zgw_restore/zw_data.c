/* © 2019 Silicon Laboratories Inc. */

#include <stdlib.h>
#include "zw_data.h"
#include <stdbool.h>
#include <strings.h>

/* Create an independent controller structure */
zw_controller_t * zw_controller_init(uint8_t SUCid) {
   zw_controller_t *ctrl = malloc(sizeof(zw_controller_t));
   if (ctrl) {
      bzero(ctrl, sizeof(zw_controller_t));
      ctrl->SUCid = SUCid;
   }
   return ctrl;
}

bool zw_controller_add_node(zw_controller_t * ctrl,
                            nodeid_t node_id, zw_node_data_t *node_data) {
   if ((node_id <= ZW_MAX_NODES)
       && (ctrl->included_nodes[node_id-1] == NULL)
       && (node_id == node_data->node_id)) {
      ctrl->included_nodes[node_id-1] = node_data;
      return true;
   } else {
      return false;
   }
   return false;
}
