/***************************************************************************//**
 * @file
 * @brief NVM3 utility functions
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

#ifndef NVM3_UTILS_H
#define NVM3_UTILS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @cond DO_NOT_INCLUDE_WITH_DOXYGEN

/***************************************************************************//**
 * @brief
 *  This function calculates the Berger Code of the supplied input variable.
 *  The Berger Code is calculated by looking at the binary value of the variable
 *  and counting the number of binary zeros.
 *
 * @param[in, out] pResult
 *   A pointer to a variable that will accumulate the berger code for
 *   the input variable specified in the next two variables.
 *
 * @param[in] pInput
 *   A pointer to the variable that contains data that is used.
 *
 * @param[in] numberOfBits
 *   The number of bits in the input variable used in the calculation.
 *   The calculation is starting from the least significant bit in the input
 *   variable.
 ******************************************************************************/
void nvm3_utilsComputeBergerCode(uint8_t *pResult, void *pInput, uint8_t numberOfBits);

/// @endcond

#ifdef __cplusplus
}
#endif

#endif /* NVM3_UTILS_H */
