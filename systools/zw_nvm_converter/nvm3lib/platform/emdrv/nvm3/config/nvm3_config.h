/***************************************************************************//**
 * @file
 * @brief NVM3 configuration file.
 *******************************************************************************
 * # License
 * <b>Copyright 2018 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc.  Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement.  This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/
#ifndef NVM3_CONFIG_H
#define NVM3_CONFIG_H

/***************************************************************************//**
 * @addtogroup emdrv
 * @{
 ******************************************************************************/

/***************************************************************************//**
 * @addtogroup NVM3
 * @{
 ******************************************************************************/

/*** Driver instrumentation options */
#define NVM3_TRACE_PORT_NONE               0               // Nothing is printed
#define NVM3_TRACE_PORT_PRINTF             1               // Print is available
#define NVM3_TRACE_PORT_UNITYPRINTF        2               // Unity print is available

//#define NVM3_TRACE_PORT                    NVM3_TRACE_PORT_NONE
#define NVM3_TRACE_PORT                    NVM3_TRACE_PORT_PRINTF

/*** Event level
     0 Critical: Trace only critical events
     1 Warning : Trace warning events and above
     2 Info    : Trace info events and above
 */
#define NVM3_TRACE_LEVEL_ERROR             0
#define NVM3_TRACE_LEVEL_WARNING           1
#define NVM3_TRACE_LEVEL_INFO              2
#define NVM3_TRACE_LEVEL_LOW               3

#define NVM3_TRACE_LEVEL                   NVM3_TRACE_LEVEL_WARNING

#define NVM3_ASSERT_ON_ERROR               false

/** @} (end addtogroup NVM3) */
/** @} (end addtogroup emdrv) */

#endif /* NVM3_CONFIG_H */
