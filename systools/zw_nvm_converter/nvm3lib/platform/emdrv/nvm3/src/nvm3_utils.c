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

#include "nvm3_utils.h"

/// @cond DO_NOT_INCLUDE_WITH_DOXYGEN

void nvm3_utilsComputeBergerCode(uint8_t *pResult, void *pInput, uint8_t numberOfBits)
{
  uint8_t sum;
  uint32_t word = *((uint32_t *)pInput);
  uint32_t mask;

  // Clear bits that are outside the wanted bits
  if (numberOfBits < 32U) {
    mask = (1UL << numberOfBits) - 1U;
    word = word & mask;
  }

  // Count bits set:
  // From http://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetNaive
  word = word - ((word >> 1) & 0x55555555U);
  word = (word & 0x33333333U) + ((word >> 2) & 0x33333333U);
  sum = (uint8_t)((((word + (word >> 4)) & 0xF0F0F0FU) * 0x1010101U) >> 24U);

  // Count bit cleared and accumulate
  *pResult = *pResult + (numberOfBits - sum);
}

/// @endcond
