/***************************************************************************//**
 * @file
 * @brief NVM3 storage emulation in RAM to run on host machine.
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

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "nvm3_config.h"
#include "nvm3_hal_ram.h"
#include "nvm3.h"
#include "nvm3_trace.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

/*******************************************************************************
 ******************************   MACROS   *************************************
 ******************************************************************************/

#define TRACE_LEVEL_ERROR         NVM3_TRACE_LEVEL_ERROR
#define NVM3HAL_READBACK          1

/*******************************************************************************
 ***************************   TYPEDEFS   **************************************
 ******************************************************************************/

/** @cond DO_NOT_INCLUDE_WITH_DOXYGEN */

typedef struct WriteFailure {
  nvm3_HalFailureFn_t callbackFct;      // Failure callback function
  uint32_t errorMask;                   // Failure 32 bit mask
  uint8_t  cycleCnt;                    // Trigger error afer cycleCnt access
  bool     errorTriggered;              // Failure trigger (=true)
} WriteFailure_t;

typedef struct NVM3HAL_EraseFailure_T {
  nvm3_HalFailureFn_t callbackFct;      // Failure callback function
  uint32_t *errorMask;                  // Failure 32 bit mask
  bool     errorTriggered;              // Failure trigger (=true)
} NVM3HAL_EraseFailure_T;

typedef struct NVM3HAL_RamMem {
  nvm3_HalPtr_t nvmAddr;                // Flash base address provided by upper layer
  size_t nvmSize;                       // Flash size
  void *pRamBeg;                        // Pointer to flash base address
  void *pRamEnd;                        // Pointer to flash end address
  nvm3_HalInfo_t halInfo;
  nvm3_HalInitMemFn_t initMemFct;       // External function call to initialize the emulated flash content
  WriteFailure_t wrFailure;             // Power outage emulation during write operation
  NVM3HAL_EraseFailure_T eraseFailure;  // Power outage emulation during erase operation
} NVM3HAL_RamMem_t;

/*******************************************************************************
 ***************************   LOCAL VARIABLES   *******************************
 ******************************************************************************/

static NVM3HAL_RamMem_t nvm3HalHandle = {
  .halInfo.deviceFamily = 0,
  .halInfo.writeSize = NVM3_HAL_WRITE_SIZE_16,
  .halInfo.memoryMapped = 1,
  .halInfo.pageSize = 0,
  .halInfo.systemUnique = 0,
  .initMemFct = NULL
};

static nvm3_HalNvmAccessCode_t nvmAccess = NVM3_HAL_NVM_ACCESS_NONE;

/** @endcond */

/*******************************************************************************
 **************************   LOCAL FUNCTIONS   ********************************
 ******************************************************************************/

// Should have had length
static inline bool nvmAdrValid(void *nvmAdr)
{
  size_t ofs = (size_t)nvmAdr - (size_t)nvm3HalHandle.nvmAddr;
  return (nvmAdr >= nvm3HalHandle.nvmAddr) && (ofs < nvm3HalHandle.nvmSize);
}

static Ecode_t nvm3_halRamOpen(nvm3_HalPtr_t nvmAdr, size_t nvmSize)
{
  static bool hasBeenOpened = false;

  // Note: The following variables will not be changed by open:
  //nvm3HalHandle.wrFailure.errorTriggered
  //nvm3HalHandle.eraseFailure.errorTriggered

  if (hasBeenOpened)
  {
    /* If using more than one nvm3 partition/instances then nvm3_open() will
     * be called multiple times each time calling nvm3_halOpen() with the
     * nvmAdr and nvmSize of each partition.
     * Instead we want to call nvm3_halOpen() directly only once with the
     * total nvm area to use (for all partitions). When nvm3_halOpen() later
     * is called from nvm3_open() (multiple times) then we will simply skip
     * the rest of this function.
     */
    return ECODE_NVM3_OK;
  }

  if (nvm3HalHandle.halInfo.pageSize == 0) {
    return ECODE_NVM3_ERR_INT_EMULATOR;
  }
  nvm3HalHandle.nvmAddr = nvmAdr;
  nvm3HalHandle.nvmSize = nvmSize;
  nvm3HalHandle.pRamBeg = nvmAdr;
  nvm3HalHandle.pRamEnd = (void *)((size_t)nvm3HalHandle.pRamBeg + nvmSize);
  nvm3HalHandle.eraseFailure.errorMask = (uint32_t *)malloc(nvm3HalHandle.halInfo.pageSize);
  if (nvm3HalHandle.eraseFailure.errorMask == NULL) {
    return ECODE_NVM3_ERR_INT_EMULATOR;
  }

  if (!hasBeenOpened) {
    hasBeenOpened = true;
    memset(nvm3HalHandle.pRamBeg, 0xff, nvm3HalHandle.nvmSize);
  }

  if (nvm3HalHandle.initMemFct != NULL) {
    nvm3HalHandle.initMemFct(nvm3HalHandle.nvmAddr, nvm3HalHandle.pRamBeg, nvm3HalHandle.nvmSize);
  }

  nvmAccess = NVM3_HAL_NVM_ACCESS_NONE;

  return ECODE_NVM3_OK;
}

static void nvm3_halRamClose(void)
{
  if (nvm3HalHandle.eraseFailure.errorMask != NULL) {
    free(nvm3HalHandle.eraseFailure.errorMask);
    nvm3HalHandle.eraseFailure.errorMask = NULL;
  }

  nvmAccess = NVM3_HAL_NVM_ACCESS_NONE;
}

static Ecode_t nvm3_halRamGetInfo(nvm3_HalInfo_t *halInfo)
{
  *halInfo = nvm3HalHandle.halInfo;

  nvmAccess = NVM3_HAL_NVM_ACCESS_NONE;

  return ECODE_NVM3_OK;
}

static void nvm3_halRamAccess(nvm3_HalNvmAccessCode_t access)
{
  nvmAccess = access;
}

static Ecode_t nvm3_halRamReadWords(nvm3_HalPtr_t nvmAdr, void *dst, size_t wordCnt)
{
  uint32_t *pSrc = nvmAdr;
  uint32_t *pDst = dst;

  if ((nvmAccess != NVM3_HAL_NVM_ACCESS_RD) && (nvmAccess != NVM3_HAL_NVM_ACCESS_RDWR)) {
    return ECODE_NVM3_ERR_READ_FAILED;
  }

  if (!nvmAdrValid(nvmAdr)) {
    return ECODE_NVM3_ERR_INT_ADDR_INVALID;
  }

  while (wordCnt > 0) {
    *pDst = *pSrc;
    pDst++;
    pSrc++;
    wordCnt--;
  }

  return ECODE_NVM3_OK;
}

static Ecode_t nvm3_halRamWriteWords(nvm3_HalPtr_t nvmAdr, void const *src, size_t wordCnt)
{
  Ecode_t sta = ECODE_NVM3_OK;
  uint32_t *pDst = nvmAdr;
  const uint32_t *pSrc = src;

  //nvm3_tracePrint(NVM3_TRACE_LEVEL_LOW, "      nvm3_halWriteWords, adr=%p, wordCnt=%u.\n", nvmAdr, cnt);

  if (nvmAccess != NVM3_HAL_NVM_ACCESS_RDWR) {
    return ECODE_NVM3_ERR_WRITE_FAILED;
  }

  assert(((size_t)nvmAdr & 0x03) == 0);
  assert(((size_t)pDst & 0x03) == 0);
  if (wordCnt == 0) {
    nvm3_tracePrint(TRACE_LEVEL_ERROR, "NVM3 ERROR - nvm3_halWriteWords: cnt=0.\n");
  }
  assert(wordCnt != 0);

  while (0 < wordCnt) {
    if (!nvmAdrValid(nvmAdr)) {
      return ECODE_NVM3_ERR_INT_ADDR_INVALID;
    }
#if NVM3HAL_READBACK
    if (nvm3HalHandle.halInfo.writeSize == NVM3_HAL_WRITE_SIZE_16) {
      uint32_t dataSrc = *pSrc;
      uint32_t dataDst = *pDst;
      if ((dataSrc & dataDst) != dataSrc) {
        // Can't write if dst bits are not 1
        nvm3_tracePrint(TRACE_LEVEL_ERROR, "NVM3 ERROR - nvm3_halWriteWords: nvmAdr=%p, cnt=%zu, ERROR src=0x%x, dst=0x%x at adr=%p.\n", nvmAdr, wordCnt, dataSrc, dataDst, pDst);
        assert(!NVM3_ASSERT_ON_ERROR);
        return ECODE_NVM3_ERR_INT_WRITE_TO_NOT_ERASED;
      }
    } else {
      uint32_t dataDst = *pDst;
      if (dataDst != 0xffffffffL) {
        // Can't write on flash if not FFFFFFFF
        nvm3_tracePrint(TRACE_LEVEL_ERROR, "NVM3 ERROR - nvm3_halWriteWords: nvmAdr=%p, cnt=%zu, ERROR exp=0xffffffff, act=0x%x at adr=%p.\n", nvmAdr, wordCnt, dataDst, pDst);
        assert(!NVM3_ASSERT_ON_ERROR);
        return ECODE_NVM3_ERR_INT_WRITE_TO_NOT_ERASED;
      }
    }
#endif
    // Move data
    if (nvm3HalHandle.wrFailure.errorTriggered) {
      // Simulate write failure
      if (nvm3HalHandle.wrFailure.cycleCnt > 0) {
        nvm3HalHandle.wrFailure.cycleCnt--;
      }
      if (nvm3HalHandle.wrFailure.cycleCnt == 0) {
        //*** Limit error begin
        uint32_t dataDst = *pDst;
        if ((((*pSrc) & 0xffff) == (dataDst & 0xffff)) && ((dataDst & 0xffff0000) == 0xffff0000)) {
          // High update
          nvm3HalHandle.wrFailure.errorMask &= 0xffff0000;
        }
        if ((((*pSrc) & 0xffff0000) == (dataDst & 0xffff0000)) && ((dataDst & 0xffff) == 0xffff)) {
          // Low update
          nvm3HalHandle.wrFailure.errorMask &= 0xffff;
        }
        //*** Limit error end

        uint32_t wrData = *pSrc | nvm3HalHandle.wrFailure.errorMask;
        *pDst = wrData;
        // Only trigger the callback if there are added bit errors
        if (wrData != *pSrc) {
          nvm3HalHandle.wrFailure.errorTriggered = false;
          // If no callback function defined, return write error
          sta = ECODE_NVM3_ERR_INT_EMULATOR;
          if (nvm3HalHandle.wrFailure.callbackFct) {
            nvm3HalHandle.wrFailure.callbackFct();
          }
        }
      } else {
        *pDst = *pSrc;
      }
    } else {
      *pDst = *pSrc;
    }

    //nvm3_tracePrint(NVM3_TRACE_LEVEL_LOW, "      -> adr=%p, dat=0x%x=%u.\n", pDst, *pDst, *pDst);

    pSrc++;
    pDst++;
    wordCnt--;
  }

  return sta;
}

static Ecode_t nvm3_halRamPageErase(nvm3_HalPtr_t nvmAdr)
{
  uint32_t *pDst = nvmAdr;

  if (nvmAccess != NVM3_HAL_NVM_ACCESS_RDWR) {
    return ECODE_NVM3_ERR_ERASE_FAILED;
  }

  // Test for page alignment
  assert(((size_t)nvmAdr & (nvm3HalHandle.halInfo.pageSize - 1)) == 0);

  if (!nvmAdrValid(nvmAdr)) {
    return ECODE_NVM3_ERR_INT_ADDR_INVALID;
  }

  /* Erase, write all memory to 0xffffffff.
       Simulate write failure triggered by power outage */
  if (nvm3HalHandle.eraseFailure.errorTriggered == false) {
    memset(pDst, 0xff, nvm3HalHandle.halInfo.pageSize);
  } else {
    for (uint32_t i = 0; i < nvm3HalHandle.halInfo.pageSize >> 2; i++) {
      *pDst = *pDst | nvm3HalHandle.eraseFailure.errorMask[i];
      pDst++;
    }
    if (nvm3HalHandle.eraseFailure.callbackFct) {
      nvm3HalHandle.eraseFailure.callbackFct();
    }
  }

  return ECODE_NVM3_OK;
}

//************************************

static void nvm3_halRamSetPageSize(size_t pageSize)
{
  nvm3HalHandle.halInfo.pageSize = pageSize;
}

static void nvm3_halRamSetWriteSize(uint8_t writeSize)
{
  nvm3HalHandle.halInfo.writeSize = writeSize;
}

static void nvm3_halRamSetDeviceFamily(uint16_t deviceFamily)
{
  nvm3HalHandle.halInfo.deviceFamily = deviceFamily;
}

static void nvm3_halRamRegisterInitMem(nvm3_HalInitMemFn_t initMemFct)
{
  nvm3HalHandle.initMemFct = initMemFct;
}

static void nvm3_halRamTriggerWriteFailure(uint32_t errorMask, uint8_t cycleCnt)
{
  nvm3HalHandle.wrFailure.errorTriggered = (cycleCnt != 0xff);
  nvm3HalHandle.wrFailure.errorMask = errorMask;
  nvm3HalHandle.wrFailure.cycleCnt = cycleCnt;
}

static void nvm3_halRamRegisterWrFailure(nvm3_HalFailureFn_t wrFailureFct)
{
  nvm3HalHandle.wrFailure.callbackFct = wrFailureFct;
}

static void nvm3_halRamTriggerEraseFailure(uint32_t *errorMask)
{
  memcpy(nvm3HalHandle.eraseFailure.errorMask, errorMask, nvm3HalHandle.halInfo.pageSize);
  nvm3HalHandle.eraseFailure.errorTriggered = true;
}

static void nvm3_halRamRegisterEraseFailure(nvm3_HalFailureFn_t wrFailureFct)
{
  nvm3HalHandle.eraseFailure.callbackFct = wrFailureFct;
}

/*******************************************************************************
 *************************** ADDONS FOR Z/IP GATEWAY ***************************
 ******************************************************************************/

Ecode_t nvm3_halRamSetBin(const uint8_t *bin_image, size_t image_size)
{
  if ((image_size <= nvm3HalHandle.nvmSize) &&
      (NULL != nvm3HalHandle.pRamBeg) && 
      (NULL != bin_image))
  {
    memcpy(nvm3HalHandle.pRamBeg, bin_image, image_size);
    return ECODE_NVM3_OK;
  }
  else
  {
    return ECODE_NVM3_ERR_INT_EMULATOR;
  }
}

size_t nvm3_halRamGetBin(uint8_t **nvm_buf_out)
{
  if ((NULL != nvm3HalHandle.pRamBeg) && (0 != nvm3HalHandle.nvmSize))
  {
    uint8_t *nvm_buf  = (uint8_t *) malloc(nvm3HalHandle.nvmSize);
    if (NULL != nvm_buf)
    {
      memcpy(nvm_buf, nvm3HalHandle.pRamBeg, nvm3HalHandle.nvmSize);
      *nvm_buf_out = nvm_buf;

      return nvm3HalHandle.nvmSize;
    }
  }
  return 0;
}

/*******************************************************************************
 ***************************   GLOBAL VARIABLES   ******************************
 ******************************************************************************/

const nvm3_HalHandle_t nvm3_halRamHandle = {
  .open = nvm3_halRamOpen,
  .close = nvm3_halRamClose,
  .getInfo = nvm3_halRamGetInfo,
  .access = nvm3_halRamAccess,
  .pageErase = nvm3_halRamPageErase,
  .readWords = nvm3_halRamReadWords,
  .writeWords = nvm3_halRamWriteWords,
};

const nvm3_HalConfig_t nvm3_halRamConfig = {
  .setPageSize = nvm3_halRamSetPageSize,
  .setWriteSize = nvm3_halRamSetWriteSize,
  .setDeviceFamily = nvm3_halRamSetDeviceFamily,
  .registerInitMem = nvm3_halRamRegisterInitMem,
  .triggerWriteFailure = nvm3_halRamTriggerWriteFailure,
  .registerWriteFailure = nvm3_halRamRegisterWrFailure,
  .triggerEraseFailure = nvm3_halRamTriggerEraseFailure,
  .registerEraseFailure = nvm3_halRamRegisterEraseFailure,
};
