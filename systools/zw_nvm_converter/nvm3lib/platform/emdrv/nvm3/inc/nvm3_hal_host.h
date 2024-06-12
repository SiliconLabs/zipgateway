/***************************************************************************//**
 * @file
 * @brief NVM3 host driver HAL
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

#ifndef NVM3_HAL_HOST_H
#define NVM3_HAL_HOST_H

#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 ******************************    MACROS    **********************************
 *****************************************************************************/

#ifdef NVM3_HOST_BUILD
#define __STATIC_INLINE static inline
#endif

/*** Not able to include em_common.h on host, redefine
     the attribute here */
#ifdef _MSC_VER
#define SL_ATTRIBUTE_ALIGN(X)
#define inline __inline
#elif defined (__clang__)
#define SL_ATTRIBUTE_ALIGN(X) __attribute__ ((aligned(X)))
#else
#define SL_ATTRIBUTE_ALIGN(X) __attribute__ ((aligned(X)))
#endif

#define SL_ALIGN(X)

/// @cond DO_NOT_INCLUDE_WITH_DOXYGEN

#define nvm3_halSetPageSize(hal, a)             hal->setPageSize(a)
#define nvm3_halSetWriteSize(hal, a)            hal->setWriteSize(a)
#define nvm3_halSetDeviceFamily(hal, a)         hal->setDeviceFamily(a)
#define nvm3_halSetDeviceFamily(hal, a)         hal->setDeviceFamily(a)
#define nvm3_halRegisterInitMem(hal, a)         hal->registerInitMem(a)
#define nvm3_halTriggerWriteFailure(hal, a, b)  hal->triggerWriteFailure(a, b)
#define nvm3_halRegisterWrFailure(hal, a)       hal->registerWriteFailure(a)
#define nvm3_halTriggerEraseFailure(hal, a)     hal->triggerEraseFailure(a)
#define nvm3_halRegisterEraseFailure(hal, a)    hal->registerEraseFailure(a)

/// @endcond

/******************************************************************************
 ******************************   TYPEDEFS   **********************************
 *****************************************************************************/

/***************************************************************************//**
 * @brief
 *   A pointer to the callback, which is called to initialize NVM memory.
 *
 * @details
 *   This callback can be used to fill NVM memory with data before any
 *   NVM3 function accesses it.
 *
 ******************************************************************************/
typedef void (*nvm3_HalInitMemFn_t)(void *adrNvm, void *adrRam, size_t len);

/***************************************************************************//**
 * @brief
 *   A pointer to the callback, which is called when an error is emulated.
 *
 * @details
 *   This callback is called after an error is emulated to let the
 *   application exit(), or similarly to emulate a power out.
 *
 ******************************************************************************/
typedef void (*nvm3_HalFailureFn_t)(void);

/***************************************************************************//**
 * @brief
 *   Set the flash page size. Must be called prior to the HAL open function.
 *   Changing page size on-the-fly is not allowed.
 *
 * @details
 *   This function is used to set the page size that will be reported by
 *   the getDeviceinfo function.
 *
 ******************************************************************************/
typedef void (*nvm3_HalSetPageSize_t)(size_t);

/***************************************************************************//**
 * @brief
 *   Set the Flash write size. Must be called prior to the HAL open function.
 *   Changing write size on-the-fly is not allowed.
 *
 * @details
 *   This function is used to set the write size to either one or two writes
 *   pr. word that will be reported by the getDeviceinfo function.
 *
 ******************************************************************************/
typedef void (*nvm3_HalSetWriteSize_t)(uint8_t writeSize);

/***************************************************************************//**
 * @brief
 *   Set the device family. Must be called prior to the HAL open function.
 *   Changing device family on-the-fly is not allowed.
 *
 * @details
 *   This function is used to set the device family that will be reported by
 *   the getDeviceinfo function.
 *
 ******************************************************************************/
typedef void (*nvm3_HalSetDeviceFamily_t)(uint16_t deviceFamily);

/***************************************************************************//**
 * @brief
 *   Register a memory initialization function that HAL will call during
 *   initialization to populate memory with data.
 *
 * @param[in] initMemFct
 *   Function callback that the application layer uses to
 *   call NVM3HAL_LoadBin() and populate memory.
 *
 ******************************************************************************/
typedef void (*nvm3_HalRegisterInitMem_t)(nvm3_HalInitMemFn_t initMemFct);

/***************************************************************************//**
 * @brief
 *   Trigger a write failure during the next write operation.
 *
 * @param[in] errorMask
 *   A bit mask to apply to the next write cycle to the Flash.
 *
 * @param[in] cycleCnt
 *   A number of write cycles to wait before triggering the
 *   failure case.
 *
 ******************************************************************************/
typedef void (*nvm3_HalTriggerWriteFailure_t)(uint32_t errorMask, uint8_t cycleCnt);

/***************************************************************************//**
 * @brief
 *   Register a callback function to use during a write failure
 *   operation. After a write failure operation is executed, the
 *   callback function is used to emulate a target reboot.
 *
 * @param[in] wrFailureFct
 *   A function callback to the application layer.
 *
 ******************************************************************************/
typedef void (*nvm3_HalRegisterWrFailure_t)(nvm3_HalFailureFn_t wrFailureFct);

/***************************************************************************//**
 * @brief
 *   Trigger a write failure during the next erase operation.
 *
 * @param[in] errorMask
 *   An error mask to apply to the next erase to Flash call.
 *
 ******************************************************************************/
typedef void (*nvm3_HalTriggerEraseFailure_t)(uint32_t *errorMask);

/***************************************************************************//**
 * @brief
 *   Register a callback function to use during the erase failure
 *   operation. After an erase failure operation is executed, the
 *   callback function is used to emulate a target reboot.
 *
 * @param[in] wrFailureFct
 *   A function callback to the application layer.
 *
 ******************************************************************************/
typedef void (*nvm3_HalRegisterEraseFailure_t)(nvm3_HalFailureFn_t wrFailureFct);

typedef struct {
  nvm3_HalSetPageSize_t           setPageSize;
  nvm3_HalSetWriteSize_t          setWriteSize;
  nvm3_HalSetDeviceFamily_t       setDeviceFamily;
  nvm3_HalRegisterInitMem_t       registerInitMem;
  nvm3_HalTriggerWriteFailure_t   triggerWriteFailure;
  nvm3_HalRegisterWrFailure_t     registerWriteFailure;
  nvm3_HalTriggerEraseFailure_t   triggerEraseFailure;
  nvm3_HalRegisterEraseFailure_t  registerEraseFailure;
} nvm3_HalConfig_t;

#ifdef __cplusplus
}
#endif

#endif /* NVM3_HAL_HOST_H */
