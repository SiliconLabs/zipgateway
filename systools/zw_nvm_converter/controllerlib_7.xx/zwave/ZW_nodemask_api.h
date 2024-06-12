/**
 * @file
 * @brief Functions used to manipulate bits (Node ID) in a NodeMask array.
 * @copyright 2019 Silicon Laboratories Inc.
 */
#ifndef _ZW_NODEMASK_API_H_
#define _ZW_NODEMASK_API_H_

/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/
#include "ZW_transport_api_small.h"

/****************************************************************************/
/*                     EXPORTED TYPES and DEFINITIONS                       */
/****************************************************************************/

/*The max length of a node mask*/
#define MAX_CLASSIC_NODEMASK_LENGTH   (ZW_CLASSIC_MAX_NODES/8)
#define MAX_LR_NODEMASK_LENGTH        (ZW_LR_MAX_NODES/8)

typedef uint8_t CLASSIC_NODE_MASK_TYPE[MAX_CLASSIC_NODEMASK_LENGTH];
typedef uint8_t LR_NODE_MASK_TYPE[MAX_LR_NODEMASK_LENGTH];


/****************************  NodeMask  ************************************
** Functions used to manipulate bits (Node ID) in a byte array (NodeMask array)
**
*****************************************************************************/

/*===========================   ZW_NodeMaskSetBit   =========================
**    Set the node bit in a node bitmask.
**
** void           RET   Nothing
** ZW_NodeMaskSetBit(
** uint8_t* pMask,         IN   pointer nodemask
** uint8_t bNodeID);        IN   node to set in nodemask
**--------------------------------------------------------------------------*/
#define ZW_NODE_MASK_SET_BIT(pMask, bNodeID) ZW_NodeMaskSetBit(pMask, bNodeID)

/*========================   NodeMaskClearBit   =============================
**    Set the node bit in a node bitmask.
**
** void       RET   Nothing
** ZW_NodeMaskClearBit(
** uint8_t* pMask,     IN   nodemask
** uint8_t bNodeID);    IN   node to clear in nodemask
**--------------------------------------------------------------------------*/
#define ZW_NODE_MASK_CLEAR_BIT(pMask, bNodeID) ZW_NodeMaskClearBit(pMask, bNodeID)

/*===========================   ZW_NodeMaskClear   ==========================
**    Clear all bits in a nodemask.
**
** void       RET   Nothing
** ZW_NodeMaskClear(
** uint8_t* pMask,     IN   nodemask
** uint8_t bLength);    IN   length of nodemask
**--------------------------------------------------------------------------*/
#define ZW_NODE_MASK_CLEAR(pMask, bLength) ZW_NodeMaskClear(pMask, bLength)

/*==========================   ZW_NodeMaskBitsIn   ==========================
**    Check is any bit is set in a nodemask.
**
** uint8_t       RET   Number of bits set in nodemask
** ZW_NodeMaskBitsIn(
** uint8_t* pMask,     IN   pointer to nodemask
** uint8_t bLength);    IN   length of nodemask
**--------------------------------------------------------------------------*/
#define ZW_NODE_MASK_BITS_IN(pMask, bLength) ZW_NodeMaskBitsIn(pMask, bLength)

/*==========================   ZW_NodeMaskNodeIn   ==========================
**    Check if a node is in a nodemask.
**
** uint8_t       RET   ZERO if not in nodemask, NONEZERO if in nodemask
** ZW_NodeMaskNodeIn(
** uint8_t* pMask,     IN   pointer to nodemask to check for bNode
** uint8_t bNode);      IN   bit number that should be checked
**--------------------------------------------------------------------------*/
#define ZW_NODE_MASK_NODE_IN(pMask, bNode) ZW_NodeMaskNodeIn(pMask, bNode)



/****************************************************************************/
/*                              EXPORTED DATA                               */
/****************************************************************************/

/****************************************************************************/
/*                           EXPORTED FUNCTIONS                             */
/****************************************************************************/

/*===========================   ZW_NodeMaskSetBit   =========================
**    Set the node bit in a node bitmask.
**
**    Side effects
**
**--------------------------------------------------------------------------*/
extern void           /*RET   Nothing                 */
ZW_NodeMaskSetBit(
uint8_t* pMask,         /* IN   pointer nodemask        */
uint16_t bNodeID);         /* IN   node to set in nodemask */

/*========================   NodeMaskClearBit   =============================
**    Set the node bit in a node bitmask.
**
**    Side effects
**
**--------------------------------------------------------------------------*/
extern void       /*RET   Nothing                   */
ZW_NodeMaskClearBit(
uint8_t* pMask,     /* IN   nodemask                  */
uint16_t bNodeID);     /* IN   node to clear in nodemask */

/*===========================   ZW_NodeMaskClear   ==========================
**    Clear all bits in a nodemask.
**
**    Side effects
**
**--------------------------------------------------------------------------*/
extern void       /*RET   Nothing             */
ZW_NodeMaskClear(
uint8_t* pMask,     /* IN   nodemask            */
uint32_t bLength);     /* IN   length of nodemask  */

/*==========================   ZW_NodeMaskBitsIn   ==========================
**    Check is any bit is set in a nodemask
**
**--------------------------------------------------------------------------*/
extern uint8_t       /*RET   Number of bits set in nodemask  */
ZW_NodeMaskBitsIn(
const uint8_t* pMask,     /* IN   pointer to nodemask             */
uint32_t bLength);     /* IN   length of nodemask              */

/*==========================   ZW_NodeMaskNodeIn   ==========================
**    Check if a node is in a nodemask.
**
**--------------------------------------------------------------------------*/
extern uint8_t       /*RET   ZERO if not in nodemask, NONEZERO if in nodemask  */
ZW_NodeMaskNodeIn(
const uint8_t* pMask,     /* IN   pointer to nodemask to check for bNode            */
uint16_t bNode);      /* IN   bit number that should be checked                 */

/*==========================   ZW_NodeMaskGetNextNode   =====================
** Function:    Find the next NodeId that is set in a nodemask.
**
** Parameters:
**   currentNodeId                  =  last NodeId found (0 for first call)
**   pMask                          -> Nodemask that should be searched
**
** Return:
**   If found                       = Next NodeId from the nodemask.
**   If not found                   = 0
**
**--------------------------------------------------------------------------*/
extern uint8_t
ZW_NodeMaskGetNextNode(
  uint16_t currentNodeId,
  const uint8_t* pMask);

#endif /* _ZW_NODEMASK_API_H_ */
