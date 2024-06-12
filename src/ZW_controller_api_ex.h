/* © 2017 Silicon Laboratories Inc.
 */

/*************************************************************************** 
* 
* Description: Additions to the ZW_controller_api.h from embedded SDK
*              (and imported into the include folder of ZIP GW source).
*              Eventually, everything in this file should be migrated to the
*              include/ZW_controller_api.h file.
* 
* Author:   Jakob Buron 
* 
****************************************************************************/
#ifndef ZW_CONTROLLER_API_EX_H_
#define ZW_CONTROLLER_API_EX_H_

#define ADD_NODE_HOME_ID 8
#define ADD_NODE_SMART_START 9

extern void
ApplicationControllerUpdate(
  BYTE bStatus,     /*IN  Status of learn mode */
  uint16_t bNodeID,     /*IN  Node id of the node that send node info */
  BYTE* pCmd,       /*IN  Pointer to Application Node information */
  BYTE bLen,        /*IN  Node info length */
  BYTE *prospectID);


#endif /* ZW_CONTROLLER_API_EX_H_ */
