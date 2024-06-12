/* © 2019 Silicon Laboratories Inc. */
/****************************************************************************
 *
 * Copyright (c) 2001-2016
 * Sigma Designs, Inc.
 * All Rights Reserved
 *
 *---------------------------------------------------------------------------
 *
 * Description: Node mask manipulation function prototypes
 *
 * Author:   Peter Shorty
 *
 * Last Changed By:  $Author: jsi $
 * Revision:         $Revision: 31343 $
 * Last Changed:     $Date: 2015-04-17 10:57:14 +0200 (fr, 17 apr 2015) $
 *
 ****************************************************************************/

/****************************************************************************/
/*		                        INCLUDE FILES		                              */
/****************************************************************************/
//#include "config_lib.h"
//#include "ZW_typedefs.h"
#include "zwave_controller_network_info_storage.h"
#include "ZW_nodemask_api.h"

/****************************************************************************
 *                          EXPORTED FUNCTIONS                              *
 ****************************************************************************/


/*
 * Function:    Set the node bit in a node bitmask
 *
 * Parameters:  pMask   Nodemask
 *              bNodeID Node ID that should be set in the mask
 *
 * Return:      void
 *
 * External:
 */
void
ZW_NodeMaskSetBit(
  uint8_t* pMask,
  uint16_t bNodeID)
{
  bNodeID--;
  *(pMask+(bNodeID>>3)) |= (0x1 << (bNodeID & 7));
}


/*
 * Function: Clear the node bit in a node bitmask
 *
 * Parameters:  pMask   Nodemask
 *              bNodeID Node ID that should be cleared in the mask
 *
 * Return:   void
 *
 * External:
 */
void
ZW_NodeMaskClearBit(
  uint8_t* pMask,
  uint16_t bNodeID)
{
  bNodeID--;
  *(pMask+(bNodeID >> 3)) &= ~(0x1 << (bNodeID & 7));
}


/*
 * Function:    Clear all bits in a nodemask
 *
 * Parameters:  pMask   Nodemask that should be cleared
 *
 * Return:      void
 *
 * External:
 */
void
ZW_NodeMaskClear(
  uint8_t* pMask,
  uint32_t bLength)
{
  /* Clear entire node mask */
  if (bLength)
  {
    do
    {
      *pMask = 0;
      pMask++;
    } while (--bLength);
  }
}


/*
 * Function:    Check and count number of bit that is set in a nodemask
 *
 * Parameters:  pMask   pointer to Nodemask that should be counted
 *              bLength length of Nodemask to count
 *
 * Return:      Number of bits set in the nodemask.
 *
 * External:
 */
uint8_t
ZW_NodeMaskBitsIn(
  const uint8_t* pMask,
  uint32_t bLength)
{
  uint8_t t, count = 0;

  if (bLength)
  {
  	do
  	{
      for (t = 0x01; t; t += t)
      {
        if (*pMask & t)
        {
          count++;
        }
      }
      pMask++;
	  } while (--bLength);
  }
  return count;
}


/*
 * Function:    Check if a node is in a nodemask
 *
 * Parameters:  pMask   Nodemask
 *              bNodeID Node ID that should be checked
 *
 * Return:      0 - not in nodemask, !=0 - found in nodemask
 *
 * External:
 */
uint8_t
ZW_NodeMaskNodeIn(
  const uint8_t* pMask,
  uint16_t bNode)
{
  bNode--;
  return ( ((*(pMask+(bNode>>3)) >> (bNode & 7)) & 0x01) );
}

/*
 * Function:    Find the next NodeId that is set in a nodemask
 *
 * Parameters:
 *   currentNodeId                  =  last NodeId found (0 for first call)
 *   pMask                          -> Nodemask that should be searched
 *
 * Return:
 *   If found                       = Next NodeId from the nodemask.
 *   If not found                   = 0
 *
 * External:
 */
uint8_t
ZW_NodeMaskGetNextNode(
  uint16_t currentNodeId,
  const uint8_t* pMask)
{
  /* TO#1501 fix - was (currentNodeId < MAX_NODEMASK_LENGTH), which of course */
  /*               is wrong as nodeID can be bigger than 29 */
  while (currentNodeId < ZW_CLASSIC_MAX_NODES)
  {
    if ((*(pMask + (currentNodeId >> 3)) >> (currentNodeId & 7)) & 0x01)
    {
      return (currentNodeId + 1);
    }
    currentNodeId++;
  }
  return 0;
}
