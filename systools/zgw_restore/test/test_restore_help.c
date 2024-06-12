/* © 2019 Silicon Laboratories Inc.  */

#include "zgw_data.h"

int check_mock_controller(const zw_controller_t *ctrl) {
   /*  remove this test code for hand built unit test controllers */
   if (ctrl != NULL) {
      for (int ii = 1; ii <= ZW_MAX_NODES; ii++) {
         zw_node_data_t *zw_node_data = ctrl->included_nodes[ii-1];
         if (zw_node_data != NULL) {
            if (ii != zw_node_data->node_id) {
               printf("Inconsistent controller data for node %u, at index %u\n",
                      zw_node_data->node_id, ii);
               return -2;
            }
         }
      }
   }
   return 0;
}

