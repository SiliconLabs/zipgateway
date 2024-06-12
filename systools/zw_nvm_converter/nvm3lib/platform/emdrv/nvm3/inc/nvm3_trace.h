/***************************************************************************//**
 * @file
 * @brief NVM3 trace macros
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
#ifndef NVM3_TRACE_H
#define NVM3_TRACE_H

#include "nvm3_config.h"
#include <stdint.h>
#if (NVM3_TRACE_PORT == NVM3_TRACE_PORT_PRINTF)
#include <stdio.h>
#endif
#if NVM3_TRACE_PORT == NVM3_TRACE_PORT_UNITYPRINTF
#include "unity.h"
#endif

/***************************************************************************//**
 * @addtogroup emdrv
 * @{
 ******************************************************************************/

/***************************************************************************//**
 * @addtogroup NVM3SYSVIEW
 * @{
 ******************************************************************************/

/*** */
/// @cond DO_NOT_INCLUDE_WITH_DOXYGEN

// Temporary solution, shoud use NVM3_TRACE_LEVEL as well
#define NVM3_TRACE_ENABLED (NVM3_TRACE_PORT != NVM3_TRACE_PORT_NONE)

#ifdef NVM3_HOST_BUILD
#define UnityPrintf(...) nvm3_tracePrint(NVM3_TRACE_LEVEL, __VA_ARGS__)
#define UNITY_PRINT_EOL nvm3_tracePrint(NVM3_TRACE_LEVEL, "\n")
#endif

#if (NVM3_TRACE_PORT == NVM3_TRACE_PORT_PRINTF)
#define nvm3_tracePrint(lev, ...) do { if (lev <= NVM3_TRACE_LEVEL) { printf(__VA_ARGS__); } } while (0)
#elif (NVM3_TRACE_PORT == NVM3_TRACE_PORT_UNITYPRINTF)
#define nvm3_tracePrint(lev, ...) do { if (lev <= NVM3_TRACE_LEVEL) { UnityPrintf(__VA_ARGS__); } } while (0)
#else
#define nvm3_tracePrint(lev, ...)
#endif

/// @endcond

/** @} (end addtogroup NVM3SYSVIEW) */
/** @} (end addtogroup emdrv) */

#endif /* NVM3_TRACE_H */
