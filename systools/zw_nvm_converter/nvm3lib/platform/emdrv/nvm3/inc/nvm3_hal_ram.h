/***************************************************************************//**
 * @file
 * @brief NVM3 driver HAL for memory mapped RAM
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

#ifndef NVM3_HAL_RAM_H
#define NVM3_HAL_RAM_H

#include "nvm3_hal.h"
#include "nvm3_hal_host.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************//**
 * @addtogroup emdrv
 * @{
 ******************************************************************************/

/***************************************************************************//**
 * @addtogroup NVM3
 * @{
 ******************************************************************************/

/***************************************************************************//**
 * @addtogroup NVM3HalRam
 * @brief NVM3 HAL RAM module
 * @{
 * @details
 * This module provides the RAM interface to the NVM.
 *
 * @note The features available through the handle are used by the NVM3 and
 * should not be used directly by any applications.
 ******************************************************************************/

/*******************************************************************************
 *************************** ADDONS FOR Z/IP GATEWAY ***************************
 ******************************************************************************/

/***************************************************************************//**
 * @brief
 *   Load binary content from memory buffer to NVM3 RAM.
 *
 * @param[in] *bin_image
 *   Pointer to the memory buffer with binary image. Data will be copied to the
 *   nvm3 RAM driver.
 * @param[in] image_size
 *   Size of the binary image (must be less than or equal to the size passed to
 *   nvm3_halOpen())
 *
 * @return
 *   Returns the result of load operation.
 ******************************************************************************/
Ecode_t nvm3_halRamSetBin(const uint8_t *bin_image, size_t image_size);

/***************************************************************************//**
 * @brief
 *   Save copy of the NVM3 RAM content to the memory buffer.
 *
 * @param[out] **nvm_buf_out
 *   Pointer to the buffer pointer. This function will use malloc() to to allocate a
 *   large enough buffer. The caller is responsible to call free() on the buffer
 *   pointer.
 *
 * @return
 *   Size of allocated memory buffer containing NVM RAM content.
 ******************************************************************************/
size_t nvm3_halRamGetBin(uint8_t **nvm_buf_out);

/*******************************************************************************
 ***************************   GLOBAL VARIABLES   ******************************
 ******************************************************************************/

extern const nvm3_HalHandle_t nvm3_halRamHandle;
extern const nvm3_HalConfig_t nvm3_halRamConfig;

/** @} (end addtogroup NVM3Hal) */
/** @} (end addtogroup NVM3) */
/** @} (end addtogroup emdrv) */

#ifdef __cplusplus
}
#endif

#endif /* NVM3_HAL_RAM_H */
