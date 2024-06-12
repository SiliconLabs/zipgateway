/***************************************************************************//**
 * @file
 * @brief NVM3 API implementation.
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

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "nvm3.h"
#include "nvm3_trace.h"
#include "nvm3_lock.h"
#include "nvm3_page.h"
#include "nvm3_object.h"
#include "nvm3_cache.h"
#include "nvm3_utils.h"
#include "nvm3_config.h"

#if defined(NVM3_USE_ZIGBEECRYPTO)
#include "aes.h"
#include "802-15-4-ccm.h"
#endif // End NVM3_USE_ZIGBEECRYPTO

/// @cond DO_NOT_INCLUDE_WITH_DOXYGEN

//****************************************************************************
// Macros

#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define MAX(x, y) (((x) > (y)) ? (x) : (y))

// NVM3 is designed for memories with 32-bit interface.
#define NVM3_WORD_SIZE                              (4U)

#define TRACE_LEVEL_INFO                            NVM3_TRACE_LEVEL_INFO
#define TRACE_LEVEL_LOW                             NVM3_TRACE_LEVEL_LOW
#define TRACE_LEVEL_INIT                            TRACE_LEVEL_LOW
#define TRACE_LEVEL_COUNTER                         TRACE_LEVEL_LOW
#define TRACE_LEVEL_REPACK                          TRACE_LEVEL_LOW
#define TRACE_LEVEL_RESIZE                          TRACE_LEVEL_LOW
#define TRACE_LEVEL_WRITE                           TRACE_LEVEL_LOW

// Bit masking of the page size. pageSize MUST be power of 2.
#define PAGE_SIZE_MASK(pageSize)                    ((pageSize) - 1U)

// NVM3 counter size definition in number of bytes, must be multiple of 4.
#define COUNTER_SIZE                                (204U)
#define COUNTER_MAX_INC_VAL_32                      (0xFFFFU)
#define COUNTER_MAX_INC_VAL_16                      (0xFFU)
#define COUNTER_MAX_NO_INC_16                       ((COUNTER_SIZE - sizeof(uint32_t)) / sizeof(uint16_t))
#define COUNTER_MAX_NO_INC_32                       ((COUNTER_SIZE - sizeof(uint32_t)) / sizeof(uint32_t))
#define COUNTER_NO_INC_PER_NEW_MEM_LOC_16           2U
#define COUNTER_NO_INC_PER_NEW_MEM_LOC_32           1U

// Define special Key to use for searching FIFO last valid location.
#define SEARCH_KEY                                  (0xFFFFFFFEU)

// Helper define to make code more readable.
#define COPY_OBJ_TRUE                               true
#define COPY_OBJ_FALSE                              false

#define CCM_OFFS_NONCE(dataLen)                     (0)
#define CCM_OFFS_TAG(dataLen)                       (dataLen + 4)
#define CCM_OFFS_DATA(dataLen)                      (4)
#define CCM_SIZE_NONCE                              (4)
#define CCM_SIZE_TAG                                (4)

#define COUNTER_OFFS_NONCE_W                        (0)
#define COUNTER_OFFS_DATA_W                         (1)
#define COUNTER_SIZE_NONCE                          (4)
#define COUNTER_SIZE_BASE                           (4)

#ifdef NVM3_HOST_BUILD
#define EFM_ASSERT assert
#endif

#if NVM3_ASSERT_ON_ERROR
#define NVM3_ERROR_ASSERT()   do { if (NVM3_ASSERT_ON_ERROR ) { EFM_ASSERT(false); } } while (0)  ///< Conditional assert
#else
#define NVM3_ERROR_ASSERT()                                                                       ///< Conditional assert
#endif

#if defined(NVM3_USE_MBEDTLS)
#define CCM_NONCE_SIZE    12
#endif

#if defined(NVM3_USE_ZIGBEECRYPTO)
#define CCM_NONCE_SIZE    16
#endif

//************************************
// Misc thresholds

// Small objects use a smaller header, but simplify by assuming all are using the large header
#define OBJ_FRAGMENTS(pageSize, objSize)    (((objSize + NVM3_OBJ_HEADER_SIZE_LARGE - 1) / (pageSize - NVM3_PAGE_HEADER_SIZE)) + 1U)
#define OBJ_LEN_REQ(pageSize, objSize)      (objSize + (OBJ_FRAGMENTS(pageSize, objSize) * NVM3_OBJ_HEADER_SIZE_LARGE))
#define OBJ_PAGES_REQ(pageSize, objSize)    ((OBJ_LEN_REQ(pageSize, objSize) - 1) / (pageSize - NVM3_PAGE_HEADER_SIZE) + 1)

#define EXTRA_HARD_PAGES                    (0)         // The number of spare pages for repack write errors
#define EXTRA_SIZE                          (4U)        // Just an arbitrary value

//************************************
// Make it is possible to select static or auto objects

// The library can be compiled with the nvm3_Obj_t variables either as auto or
// static allocation. The default is to use static allocation, but this
// definition could be changed for test and comparison.
#define ALLOCATE_NVM3_OBJ_STATIC            1

#if ALLOCATE_NVM3_OBJ_STATIC
#define NVM3_OBJ_T_ALLOCATION(obj)
#else
#define NVM3_OBJ_T_ALLOCATION(obj)          nvm3_Obj_t x ## obj; nvm3_ObjPtr_t p ## obj = &x ## obj
#endif

#if ALLOCATE_NVM3_OBJ_STATIC
#define pObjA       ((nvm3_Obj_t *)&nvm3_internalObjectHandleA)
#define pObjB       ((nvm3_Obj_t *)&nvm3_internalObjectHandleB)
#define pObjC       ((nvm3_Obj_t *)&nvm3_internalObjectHandleC)
#define pObjD       ((nvm3_Obj_t *)&nvm3_internalObjectHandleD)
#endif

// Defined hooks to check overlapped use of obj's
#if ALLOCATE_NVM3_OBJ_STATIC
//#define objBegin(obj) do { if (obj.objAdr != NULL) { assert(false); } } while (false)
//#define objEnd(obj) do { obj.objAdr = NULL; } while (false)
#define objBegin(obj)
#define objEnd(obj)
#else
#define objBegin(obj)
#define objEnd(obj)
#endif

#define HAL  h->halHandle

//****************************************************************************
// Typedefs

typedef enum {
  read_data,
  read_compare
} read_operation_t;

typedef bool (*FifoScanCallback_t)(nvm3_Handle_t *h, nvm3_ObjPtr_t objPtr, nvm3_ObjGroup_t objGroup, void *user);

typedef struct WriteFailure {
  nvm3_HalPtr_t addrError;        // Address of the error
} WriteFailure_t;

//****************************************************************************
// Static variables

static uint32_t cfgEraseCnt = 0;

//****************************************************************************
// Function prototypes

static uint32_t getObjContent(nvm3_Handle_t *h, size_t hdrLen, nvm3_Obj_t *obj, size_t ofs);
static uint32_t readCounter(nvm3_Handle_t *h, nvm3_Obj_t *);
static Ecode_t repackUntilGood(nvm3_Handle_t *h);
static size_t findValidPageCnt(nvm3_Handle_t *h);

//****************************************************************************
// Static functions

__STATIC_INLINE size_t pagesRepack(nvm3_Handle_t *h)
{
  // A repack may require for maximum:
  // All objects in the first page
  // And the last object may span to the next page(s). In this case at least
  // 4 bytes of the last object will be in the first page.
  size_t cntRepackA = 1;
  size_t cntRepackB = OBJ_PAGES_REQ(h->halInfo.pageSize, h->maxObjectSize - 4);
  size_t cntExtra = EXTRA_HARD_PAGES;

  return cntRepackA + cntRepackB + cntExtra;
}

__STATIC_INLINE size_t thrFull(nvm3_Handle_t *h, size_t objSize)
{
  // The full threshold is just the object size
  return OBJ_LEN_REQ(h->halInfo.pageSize, objSize);
}

__STATIC_INLINE size_t thrRepack(nvm3_Handle_t *h)
{
  // The repack threshold is the size of the pages required for a repack
  return pagesRepack(h) * (h->halInfo.pageSize - NVM3_PAGE_HEADER_SIZE);
}

__STATIC_INLINE size_t thrHard(nvm3_Handle_t *h, size_t objSize)
{
  // The hard threshold may require:
  // Size for a repack
  // And the object to write
  size_t sizeRepack = thrRepack(h);
  size_t sizeObj = OBJ_LEN_REQ(h->halInfo.pageSize, objSize);

  return sizeRepack + sizeObj;
}

__STATIC_INLINE size_t thrSoftMinimum(nvm3_Handle_t *h)
{
  // The soft threshold may require:
  // The hard threshold
  // And one object that was written after the unused size was close to the soft threshold.
  size_t sizeHard = thrHard(h, h->maxObjectSize);
  size_t sizeObj = OBJ_LEN_REQ(h->halInfo.pageSize, h->maxObjectSize);

  return sizeHard + sizeObj;
}

__STATIC_INLINE size_t thrSoftUser(nvm3_Handle_t *h)
{
  return thrSoftMinimum(h) + h->repackHeadroom;
}

__STATIC_INLINE size_t thrSize(nvm3_Handle_t *h)
{
  size_t sizeSoft = thrSoftUser(h);
  size_t sizeExtra = EXTRA_SIZE;

  return sizeSoft + sizeExtra;
}

__STATIC_INLINE bool writeFullAllowed(nvm3_Handle_t *h, size_t objSize)
{
  return h->unusedNvmSize >= thrFull(h, objSize);
}

__STATIC_INLINE bool writeHardAllowed(nvm3_Handle_t *h, size_t objSize)
{
  return h->unusedNvmSize >= thrHard(h, objSize);
}

__STATIC_INLINE bool softMinimumAvailable(nvm3_Handle_t *h)
{
  return h->unusedNvmSize >= thrSoftMinimum(h);
}

__STATIC_INLINE bool softUserAvailable(nvm3_Handle_t *h)
{
  return h->unusedNvmSize >= thrSoftUser(h);
}

//************************************

__STATIC_INLINE size_t lenAdjustedForWords(size_t byteLen)
{
  return (((byteLen + 3U) / NVM3_WORD_SIZE) * NVM3_WORD_SIZE);
}

__STATIC_INLINE void *calcAdr(void *ptr, size_t ofs)
{
  return (uint8_t *)ptr + ofs;
}

__STATIC_INLINE void *pageAdrFromIdx(nvm3_Handle_t *h, size_t idx)
{
  return (uint8_t *)h->nvmAdr + idx * h->halInfo.pageSize;
}

__STATIC_INLINE size_t pageIdxFromAdr(nvm3_Handle_t *h, void *adr)
{
  size_t offset = (uint8_t *)adr - (uint8_t *)h->nvmAdr;
  return offset / h->halInfo.pageSize;
}

__STATIC_INLINE bool samePage(nvm3_Handle_t *h, const void *ptrA, const void *ptrB)
{
  size_t mask = h->halInfo.pageSize - 1U;
  const uint8_t *addrA = ptrA;
  const uint8_t *addrB = ptrB;
  return ((((size_t)addrA) & ~(mask)) == (((size_t)addrB) & ~(mask)));
}

__STATIC_INLINE size_t pageIdxIncrement(nvm3_Handle_t *h, size_t idx)
{
  return (idx + 1U) % h->totalNvmPageCnt;
}

__STATIC_INLINE size_t pageIdxDecrement(nvm3_Handle_t *h, size_t idx)
{
  return (idx > 0U) ? (idx - 1U) : (h->totalNvmPageCnt - 1U);
}

static size_t getNextGoodPage(nvm3_Handle_t *h, size_t idx)
{
  nvm3_HalPtr_t pageAdr;
  nvm3_PageHdr_t pageHdr;
  nvm3_PageState_t pageState;

  /* Loop through all pages minus 1, which is the current page. */
  for (size_t i = 0U; i < (h->totalNvmPageCnt - 1U); i++) {
    idx = pageIdxIncrement(h, idx);
    pageAdr = pageAdrFromIdx(h, idx);
    nvm3_halReadWords(HAL, pageAdr, &pageHdr, NVM3_PAGE_HEADER_WSIZE);
    pageState = nvm3_pageGetState(&pageHdr);
    if (nvm3_pageStateIsGood(pageState)) {
      break;
    }
  }

  return idx;
}

static size_t getPreviousGoodPage(nvm3_Handle_t *h, size_t idx)
{
  nvm3_HalPtr_t pageAdr;
  nvm3_PageHdr_t pageHdr;
  nvm3_PageState_t state;

  /* Loop through all pages minus 1, which is the current pages. */
  for (size_t i = 0U; i < (h->totalNvmPageCnt - 1U); i++) {
    idx = pageIdxDecrement(h, idx);
    pageAdr = pageAdrFromIdx(h, idx);
    nvm3_halReadWords(HAL, pageAdr, &pageHdr, NVM3_PAGE_HEADER_WSIZE);
    state = nvm3_pageGetState(&pageHdr);
    if (nvm3_pageStateIsGood(state)) {
      break;
    }
  }

  return idx;
}

__STATIC_INLINE size_t pageFreeSize(nvm3_Handle_t *h, const void *adr)
{
  const uint8_t * mem = adr;
  return (h->halInfo.pageSize - ((size_t)mem & (PAGE_SIZE_MASK(h->halInfo.pageSize))));
}

__STATIC_INLINE size_t getPageOfs(nvm3_Handle_t *h, nvm3_HalPtr_t adr)
{
  return (size_t)adr & PAGE_SIZE_MASK(h->halInfo.pageSize);
}

static nvm3_ObjPtr_t getFirstObjAdrInNextGoodPage(nvm3_Handle_t *h, nvm3_ObjPtr_t obj)
{
  size_t pageIdx;
  nvm3_HalPtr_t pageAdr;
  nvm3_ObjPtr_t newObj;

  pageIdx = pageIdxFromAdr(h, obj);
  pageIdx = getNextGoodPage(h, pageIdx);
  pageAdr = pageAdrFromIdx(h, pageIdx);
  newObj = nvm3_pageGetFirstObj(pageAdr);

  return newObj;
}

static size_t getFreeSize(nvm3_Handle_t *h)
{
  size_t size = 0;
  nvm3_ObjPtr_t obj;
  nvm3_HalPtr_t pageAdr;
  size_t pageIdx;
  nvm3_PageHdr_t pageHdr;
  nvm3_PageState_t pageState;

  if (h->fifoNextObj == h->fifoFirstObj) {
    size = h->nvmSize - h->totalNvmPageCnt * NVM3_PAGE_HEADER_SIZE;
  } else {
    obj = h->fifoNextObj;
    while (obj != h->fifoFirstObj) {
      pageIdx = pageIdxFromAdr(h, obj);
      pageAdr = pageAdrFromIdx(h, pageIdx);
      nvm3_halReadWords(HAL, pageAdr, &pageHdr, NVM3_PAGE_HEADER_WSIZE);
      pageState = nvm3_pageGetState(&pageHdr);
      if (pageState != nvm3_PageStateGoodEip) {
        size += h->halInfo.pageSize - getPageOfs(h, obj);
      }
      obj = getFirstObjAdrInNextGoodPage(h, obj);
    }
  }

  return size;
}

// Check that the page is empty starting from the startAddr.
// Return the number of free elements from that address, 0 if the page is not empty.
static uint32_t scanPage(nvm3_Handle_t *h, void *startAdr)
{
  uint8_t *currentAdr = startAdr;
  uint32_t cnt = 0;
  uint32_t val;

  while (samePage(h, startAdr, currentAdr)) {
    nvm3_halReadWords(HAL, currentAdr, &val, 1);
    if (val != 0xFFFFFFFFU) {
      cnt = 0;
      break;
    }
    cnt = cnt + sizeof(uint32_t);
    currentAdr += sizeof(uint32_t);
  }

  // Return cnt, 0 means there is data after the start address.
  return cnt;
}

__STATIC_INLINE bool keyIsSearchKey(nvm3_ObjectKey_t key)
{
  return (key == SEARCH_KEY);
}

__STATIC_INLINE bool keyIsValid(nvm3_ObjectKey_t key)
{
  return ((key & NVM3_KEY_MASK) == key);
}

__STATIC_INLINE size_t counterMaxIncVal(nvm3_Handle_t *h)
{
  return (h->halInfo.writeSize == NVM3_HAL_WRITE_SIZE_16) ? COUNTER_MAX_INC_VAL_16 : COUNTER_MAX_INC_VAL_32;
}

__STATIC_INLINE size_t counterMaxNumberOfInc(nvm3_Handle_t *h)
{
  return (h->halInfo.writeSize == NVM3_HAL_WRITE_SIZE_16) ? COUNTER_MAX_NO_INC_16 : COUNTER_MAX_NO_INC_32;
}

__STATIC_INLINE size_t counterNumberOfIncPrBase(nvm3_Handle_t *h)
{
  return (h->halInfo.writeSize == NVM3_HAL_WRITE_SIZE_16) ? COUNTER_NO_INC_PER_NEW_MEM_LOC_16 : COUNTER_NO_INC_PER_NEW_MEM_LOC_32;
}

/* Get a location for the next object to store in memory */
static nvm3_ObjPtr_t getNextObj(nvm3_Handle_t *h, nvm3_ObjPtr_t objAdr, size_t hdrLen, size_t dataLen)
{
  nvm3_ObjPtr_t nextObj;

  /* Jump over the header and the data. */
  nextObj = (nvm3_ObjPtr_t)((size_t)objAdr + hdrLen + ((dataLen / NVM3_WORD_SIZE) * NVM3_WORD_SIZE));
  if ((dataLen % NVM3_WORD_SIZE) != 0U) {
    nextObj = (nvm3_ObjPtr_t)((size_t)nextObj + NVM3_WORD_SIZE);
  }

  /* Check if the next object is on a different page, and act accordingly. */
  if (!samePage(h, nextObj, objAdr)) {
    objAdr = getFirstObjAdrInNextGoodPage(h, objAdr);
    nextObj = objAdr;
  }

  return nextObj;
}

#ifdef NVM3_SUPPORT_ENCRYPTION
static Ecode_t nonceGet(nvm3_Handle_t *h, uint32_t *nonceVal)
{
  Ecode_t sta = ECODE_NVM3_OK;

#if defined(NVM3_USE_MBEDTLS)
  int mbedtlsSta;

  // Get 4 bytes of random data
  mbedtlsSta = mbedtls_ctr_drbg_random(&h->drbgCtx, (unsigned char *)nonceVal, 4);
  if (mbedtlsSta != 0) {
    NVM3_ERROR_ASSERT();
    sta = ECODE_NVM3_ERR_RANDOM_NUMBER;
  }
#elif defined(NVM3_USE_ZIGBEECRYPTO)
  *nonceVal = h->getRndNumber();
  (void)h;
#else
  *nonceVal = 0;
  (void)h;
#endif

  return sta;
}

static uint32_t nonceEncrypt(nvm3_Handle_t *h, uint32_t nonceVal)
{
  uint32_t nonceEnc;
  uint8_t nonceBuf[16];

  memset(nonceBuf, 0, sizeof(nonceBuf));
  memcpy(nonceBuf, &nonceVal, sizeof(nonceVal));
#if defined(NVM3_USE_MBEDTLS)
  int mbedtlsSta;

  mbedtlsSta = mbedtls_aes_setkey_enc(&h->aesCtx, h->cryptoKey, 128);
  if (mbedtlsSta != 0) {
    NVM3_ERROR_ASSERT();
    return 0;
  }
  mbedtlsSta = mbedtls_aes_crypt_ecb(&h->aesCtx, MBEDTLS_AES_ENCRYPT, nonceBuf, nvm3_ccmBuf);
  if (mbedtlsSta != 0) {
    NVM3_ERROR_ASSERT();
    return 0;
  }
  memcpy(&nonceEnc, nvm3_ccmBuf, sizeof(nonceEnc));
#elif defined(NVM3_USE_ZIGBEECRYPTO)
  nonceEnc = nonceVal;
  uint8_t oldKey[16];
  emGetKeyFromCore(oldKey);
  emLoadKeyIntoCore(h->cryptoKey);
  emCcmEncrypt(nonceBuf, NULL, 0, (uint8_t *)&nonceEnc, sizeof(nonceEnc), NULL, 0);
  emLoadKeyIntoCore(oldKey);
  (void)h;
#else
  error "No encryption has been defined"
#endif

  return nonceEnc;
}

static uint32_t counterEncrypt(nvm3_Handle_t *h, uint32_t nonceVal, uint32_t raw)
{
  uint32_t nonceEnc;
  uint32_t counterEnc;

  nonceEnc = nonceEncrypt(h, nonceVal);
  counterEnc = nonceEnc ^ raw;

  return counterEnc;
}

static uint32_t counterDecrypt(nvm3_Handle_t *h, uint32_t nonceVal, uint32_t encrypted)
{
  uint32_t nonceEnc;
  uint32_t counterDec;

  nonceEnc = nonceEncrypt(h, nonceVal);
  counterDec = nonceEnc ^ encrypted;

  return counterDec;
}
#endif

/* Write object to media, handle object type, fragmentation, and so on. */
static Ecode_t writeObj(nvm3_Handle_t *h, nvm3_Obj_t *srcObj, nvm3_Obj_t *dstObj,
                        nvm3_HalPtr_t dstAdr, bool copyObj,
                        nvm3_ObjGroup_t objGroup,
                        WriteFailure_t *wrFailure)
{
  Ecode_t sta = ECODE_NVM3_OK;
  size_t srcLen = srcObj->totalLen;
  size_t pageFreeBytes;
  size_t rdOfs = 0;
  nvm3_ObjPtr_t fragAdr;
  size_t fragLen;
  nvm3_ObjFragType_t fragTyp = fragTypeNone;
  uint32_t data;
  const uint8_t *ptrSrc = srcObj->srcPtr;
  size_t dstLen;
  nvm3_ObjHdrLarge_t objHdrLarge;
  bool srcHdrIsLarge;
  size_t srcHdrLen;
  bool dstHdrIsLarge;
  size_t dstHdrLen;
  size_t dstHdrAux = 0;
#ifdef NVM3_SUPPORT_ENCRYPTION
  uint32_t nonceVal;
  bool nonceWr = false;
#endif
  size_t offset = 0;
  uint32_t baseVal;
  bool baseWr = false;

#if NVM3_TRACE_ENABLED
  size_t tmpIdx = pageIdxFromAdr(h, dstAdr);
  void *tmpAdr = pageAdrFromIdx(h, tmpIdx);
  size_t ofs = (size_t)dstAdr - (size_t)tmpAdr;
#endif
  nvm3_tracePrint(TRACE_LEVEL_WRITE, "  writeObj: dstAdr=%p, srcObjTotLen=%zu, (dst: idx=%zu, adr=%p, ofs=0x%zx).\n", dstAdr, srcObj->totalLen, tmpIdx, tmpAdr, ofs);

#ifdef NVM3_SUPPORT_ENCRYPTION
  if ((objGroup == objGroupCounter) && (srcLen != (COUNTER_SIZE + (h->encrypted ? COUNTER_SIZE_NONCE : 0)))) {
#else
  if ((objGroup == objGroupCounter) && (srcLen != COUNTER_SIZE)) {
#endif
    nvm3_tracePrint(NVM3_TRACE_LEVEL_ERROR, "NVM3 ERROR - writeObj: src len mismatch, exp=%u, act=%zu.\n", COUNTER_SIZE, srcLen);
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_INT_SIZE_ERROR;
  }

  if (objGroup == objGroupCounter) {
    if (copyObj) {
      // Get counter value if this is a counter copy
      baseVal = readCounter(h, srcObj);
    } else {
#ifdef NVM3_SUPPORT_ENCRYPTION
      if (h->encrypted) {
        nonceVal = *((uint32_t *)(srcObj->srcPtr) + COUNTER_OFFS_NONCE_W);
        baseVal = *((uint32_t *)(srcObj->srcPtr) + COUNTER_OFFS_DATA_W);
        nvm3_tracePrint(TRACE_LEVEL_COUNTER, "      cntRd: nonce=%u, base=%u.\n", nonceVal, baseVal);
      } else {
#endif
      baseVal = *(uint32_t *)srcObj->srcPtr;
#ifdef NVM3_SUPPORT_ENCRYPTION
    }
#endif
    }
  }

  pageFreeBytes = pageFreeSize(h, dstAdr);

  srcHdrIsLarge = (srcLen > NVM3_OBJ_SMALL_MAX_SIZE) || (srcObj->isFragmented);
  srcHdrLen = srcHdrIsLarge ? NVM3_OBJ_HEADER_SIZE_LARGE : NVM3_OBJ_HEADER_SIZE_SMALL;

  dstHdrIsLarge = (srcLen > NVM3_OBJ_SMALL_MAX_SIZE);
  if (!dstHdrIsLarge) {
    if (pageFreeBytes < (srcLen + NVM3_OBJ_HEADER_SIZE_SMALL)) {
      // In this case, normally the object will be fragmented.
      // But if the first fragment is empty it is still small - starting in the next page.
      if ((srcLen > 0) && (pageFreeBytes > NVM3_OBJ_HEADER_SIZE_LARGE)) {
        dstHdrIsLarge = true;
      } else {
        dstHdrAux = NVM3_OBJ_HEADER_SIZE_LARGE - NVM3_OBJ_HEADER_SIZE_SMALL;
      }
    }
  }
  dstHdrLen = dstHdrIsLarge ? NVM3_OBJ_HEADER_SIZE_LARGE : NVM3_OBJ_HEADER_SIZE_SMALL;

  //UnityPrintf("w1: objPtr=%p, key=%2u, len=%3u, srcHdrLen=%u, dstAdr=%p.\n", srcObj->objAdr, srcObj->key, srcLen, srcHdrLen, dstAdr);

  dstObj->key = srcObj->key;
  dstObj->totalLen = srcLen;
  dstObj->objType = (uint8_t)nvm3_objGroupToType(objGroup, dstHdrIsLarge);

  nvm3_tracePrint(TRACE_LEVEL_WRITE, "    object len=%zu.\n", dstObj->totalLen);

  // Write all object fragments.
  do {
    bool pageHasRoom;

    pageFreeBytes = pageFreeSize(h, dstAdr);

    nvm3_tracePrint(TRACE_LEVEL_WRITE, "    pageFreeBytes=%zu, dstHdrLen=%zu.\n", pageFreeBytes, dstHdrLen);

    //  Make sure there is room for the header and optional data.
    pageHasRoom = (srcLen > 0U) ? (pageFreeBytes > (dstHdrLen + dstHdrAux)) : (pageFreeBytes >= dstHdrLen);
    dstHdrAux = 0;
    if (pageHasRoom) {
      pageFreeBytes -= dstHdrLen;
      fragLen = (srcLen > pageFreeBytes) ? pageFreeBytes : srcLen;
      fragAdr = dstAdr;
      dstAdr = calcAdr(dstAdr, dstHdrLen);
      nvm3_tracePrint(TRACE_LEVEL_WRITE, "    there is room, free=%zu, fragLen=%zu.\n", pageFreeBytes, fragLen);

      // Compute fragmentation type.
      if (fragLen != srcLen) {
        dstObj->isFragmented = true;
        if (srcLen == dstObj->totalLen) {
          fragTyp = fragTypeFirst;
          dstObj->objAdr = fragAdr;
        } else {
          fragTyp = fragTypeNext;
        }
      } else if (fragTyp != fragTypeNone) {
        fragTyp = fragTypeLast;
      } else {
        fragTyp = fragTypeNone;
      }

      // Set object information.
      if (dstObj->isFragmented) {
        dstObj->frag.detail[dstObj->frag.idx].adr = fragAdr;
        dstObj->frag.detail[dstObj->frag.idx].len = (uint16_t)fragLen;
        dstObj->frag.detail[dstObj->frag.idx].typ = (uint8_t)fragTyp;
        dstObj->frag.idx++;
      } else {
        dstObj->objAdr = fragAdr;
      }

      // Write payload.
      if (fragLen > 0U) {
        if (objGroup == objGroupCounter) {
          //nvm3_tracePrint(TRACE_LEVEL_WRITE, "      cntWr: adr=%p.\n", dstAdr);
          // For counter objects write only the base value and nonce if encrypted.
#ifdef NVM3_SUPPORT_ENCRYPTION
          if (h->encrypted) {
            if (!nonceWr && ((offset + fragLen) >= COUNTER_SIZE_NONCE)) {
              // Write nonce
              sta = nvm3_halWriteWords(HAL, dstAdr, &nonceVal, 1);
              nvm3_tracePrint(TRACE_LEVEL_COUNTER, "      cntWr: key=%u, fAdr=%p, bAdr=%p, nonce=%u.\n", srcObj->key, fragAdr, dstAdr, nonceVal);
              if (sta != ECODE_NVM3_OK) {
                break;
              }
              nonceWr = true;
            }
            if (!baseWr && ((offset + fragLen) >= (COUNTER_SIZE_NONCE + COUNTER_SIZE_BASE))) {
              // Write the base value
              if (offset == 0) {
                // Modify the destination address for the base value
                dstAdr = calcAdr(dstAdr, CCM_OFFS_DATA(srcLen));
              }
              if (h->encrypted) {
                baseVal = counterEncrypt(h, nonceVal, baseVal);
              }
              sta = nvm3_halWriteWords(HAL, dstAdr, &baseVal, 1);
              nvm3_tracePrint(TRACE_LEVEL_COUNTER, "      cntWr: key=%u, fAdr=%p, bAdr=%p, base=%u.\n", srcObj->key, fragAdr, dstAdr, baseVal);
              if (sta != ECODE_NVM3_OK) {
                break;
              }
              baseWr = true;
            }
          } else {
#endif
          if (!baseWr && ((offset + fragLen) >= COUNTER_SIZE_BASE)) {
            sta = nvm3_halWriteWords(HAL, dstAdr, &baseVal, 1);
            nvm3_tracePrint(TRACE_LEVEL_COUNTER, "      cntWr: key=%u, fAdr=%p, bAdr=%p, base=%u.\n", srcObj->key, fragAdr, dstAdr, baseVal);
            if (sta != ECODE_NVM3_OK) {
              break;
            }
            baseWr = true;
#ifdef NVM3_SUPPORT_ENCRYPTION
          }
#endif
          }
        } else {
          nvm3_tracePrint(TRACE_LEVEL_WRITE, "      cntWr: adr=%p, len=%zu, copy=%s.\n", dstAdr, fragLen, copyObj ? "true" : "false");
          if (!copyObj) {
            // Source is a continous memory.
            uint32_t *pSrc = (uint32_t *)ptrSrc;
            uint32_t *pDst = dstAdr;
            size_t byteCnt = fragLen;
            size_t wordCnt = byteCnt / sizeof(uint32_t);
            if (wordCnt > 0) {
              sta = nvm3_halWriteWords(HAL, pDst, pSrc, wordCnt);
              pSrc += wordCnt;
              pDst += wordCnt;
              byteCnt -= (wordCnt * sizeof(uint32_t));
            }
            if ((sta == ECODE_NVM3_OK) && (byteCnt > 0)) {
              uint32_t lastWord = 0xffffffffLU;
              memcpy(&lastWord, pSrc, byteCnt);
              sta = nvm3_halWriteWords(HAL, pDst, &lastWord, 1);
            }
            ptrSrc += fragLen;
          } else {
            // Source is an object that can be fragmented.
            dstLen = fragLen;
            for (;; ) {
              data = getObjContent(h, srcHdrLen, srcObj, rdOfs);
              rdOfs += NVM3_WORD_SIZE;
              sta = nvm3_halWriteWords(HAL, dstAdr, &data, 1);
              if ((dstLen <= 4U) || (sta != ECODE_NVM3_OK)) {
                break;
              }
              dstLen -= NVM3_WORD_SIZE;
              dstAdr = calcAdr(dstAdr, NVM3_WORD_SIZE);
            }
          }
        }
      }
      if (sta != ECODE_NVM3_OK) {
        nvm3_tracePrint(NVM3_TRACE_LEVEL_WARNING, "    write object body: ERROR in payload.\n");
        wrFailure->addrError = dstAdr;
      }

      // Write header.
      if (sta == ECODE_NVM3_OK) {
        nvm3_tracePrint(TRACE_LEVEL_WRITE, "    write object header: adr=%p, hdrLen=%zu, fragType=%u.\n", fragAdr, dstHdrLen, fragTyp);
        (void)nvm3_objHdrInit(&objHdrLarge, srcObj->key, (nvm3_ObjType_t)dstObj->objType, fragLen, dstHdrIsLarge, fragTyp);
        sta = nvm3_halWriteWords(HAL, fragAdr, &objHdrLarge, dstHdrLen / sizeof(uint32_t));
        if (sta != ECODE_NVM3_OK) {
          nvm3_tracePrint(NVM3_TRACE_LEVEL_WARNING, "    write object header: ERROR in header.\n");
          wrFailure->addrError = fragAdr;
        }
      }

      dstObj->nextObjAdr = getNextObj(h, fragAdr, dstHdrLen, fragLen);
      h->unusedNvmSize -= (dstHdrLen + lenAdjustedForWords(fragLen));

      if (sta != ECODE_NVM3_OK) {
        break;
      }

      srcLen -= fragLen;
      offset += fragLen;
    } else {
      nvm3_tracePrint(TRACE_LEVEL_WRITE, "    the page has not space for any object fragments.\n");
      h->unusedNvmSize -= pageFreeBytes;
    }

    dstAdr = getFirstObjAdrInNextGoodPage(h, dstAdr);
    //UnityPrintf("w2: dst=%p\n", dstAdr);
  } while (srcLen > 0U);

  return sta;
}

/* Write object to NVM. */
static Ecode_t fifoWriteObj(nvm3_Handle_t *h, nvm3_Obj_t *srcObj, bool copyObj,
                            nvm3_ObjGroup_t objGroup)
{
  Ecode_t sta;
  nvm3_ObjPtr_t dstAdr;
  WriteFailure_t wrFailure = { NULL };
  nvm3_ObjPtr_t nextObj;
  NVM3_OBJ_T_ALLOCATION(ObjC);

  nvm3_tracePrint(TRACE_LEVEL_LOW, "  fifoWriteObj: copy=%u, objGroup=%u.\n", copyObj, objGroup);

  if (!writeFullAllowed(h, srcObj->totalLen)) {
    nvm3_tracePrint(NVM3_TRACE_LEVEL_WARNING, "  fifoWriteObj: Storage full, objLen=%zu, unused=%zu.\n", srcObj->totalLen, h->unusedNvmSize);
    return ECODE_NVM3_ERR_STORAGE_FULL;
  }

  objBegin(pObjC);
  do {
    dstAdr = h->fifoNextObj;
    nvm3_objInit(pObjC, dstAdr);
    sta = writeObj(h, srcObj, pObjC, dstAdr, copyObj, objGroup, &wrFailure);
    if (sta == ECODE_NVM3_OK) {
      h->fifoNextObj = pObjC->nextObjAdr;
    } else {
      size_t curIdx;
      nvm3_HalPtr_t curAdr;
      size_t badIdx;
      nvm3_HalPtr_t badAdr;
      Ecode_t staCpy;

      // Handle a write error.
      // Copy any objects in front of the failing object, then rewrite the object.
      do {
        // Src
        curIdx = pageIdxFromAdr(h, dstAdr);
        curAdr = nvm3_pageGetFirstObj(pageAdrFromIdx(h, curIdx));
        // Dst
        nextObj = getFirstObjAdrInNextGoodPage(h, h->fifoNextObj);
        if (nextObj != h->fifoFirstObj) {
          h->fifoNextObj = nextObj;
          h->unusedNvmSize -= pageFreeSize(h, pObjC->nextObjAdr);
        } else {
          break;
        }
        // Length
        size_t len = (size_t)dstAdr - (size_t)curAdr;
        // Use idx and len to check if the error is on a page with previous data.
        badIdx = pageIdxFromAdr(h, wrFailure.addrError);
        if ((badIdx == curIdx) && (len > 0U)) {
          staCpy = nvm3_halWriteWords(HAL, h->fifoNextObj, curAdr, len / sizeof(uint32_t));
          h->fifoNextObj = calcAdr(h->fifoNextObj, len);
          h->unusedNvmSize -= len;
          // Simple solution.
          nvm3_cacheClear(&h->cache);
        } else {
          staCpy = ECODE_NVM3_OK;
        }
        // Mark page as invalid.
        badAdr = pageAdrFromIdx(h, badIdx);
        nvm3_pageSetBad(HAL, badAdr);

        // Adjust handle variables according to operation.
        (void)findValidPageCnt(h);
        h->unusedNvmSize = getFreeSize(h);
      } while ((staCpy != ECODE_NVM3_OK) && (writeFullAllowed(h, srcObj->totalLen)));
    }
  } while ((sta != ECODE_NVM3_OK) && (writeFullAllowed(h, srcObj->totalLen)));

  // Update cache according to operation result.
  if (sta == ECODE_NVM3_OK) {
    nvm3_cacheSet(&h->cache, srcObj->key, pObjC->objAdr, objGroup);
  }
  objEnd(pObjC);

  return sta;
}

/* Write object to NVM (wrapper function). */
static Ecode_t fifoWriteWrapper(nvm3_Handle_t *h, nvm3_ObjectKey_t key,
                                const void *srcPtr, size_t srcLen,
                                nvm3_ObjGroup_t objGroup)
{
  Ecode_t sta;
  bool wrAllowed;
  NVM3_OBJ_T_ALLOCATION(ObjB);

  nvm3_tracePrint(TRACE_LEVEL_LOW, "  fifoWriteWrapper.\n");

  // Check the size of the new object.
  if (srcLen > h->maxObjectSize) {
    return ECODE_NVM3_ERR_WRITE_DATA_SIZE;
  }

  (void)repackUntilGood(h);

  // Always allow writing of delete objects.
  wrAllowed = (objGroup == objGroupDeleted) ? true : writeHardAllowed(h, srcLen);
  if (!wrAllowed) {
    nvm3_tracePrint(NVM3_TRACE_LEVEL_ERROR, "NVM3 ERROR - fifoWriteWrapper: storage full, unusedNvmSize=%zu, srcLen=%zu.\n", h->unusedNvmSize, srcLen);
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_STORAGE_FULL;
  }

#ifdef NVM3_SUPPORT_ENCRYPTION
  if (h->encrypted) {
    unsigned char nonceBuf[CCM_NONCE_SIZE];
    uint32_t nonceVal;
    unsigned char *pNon;
    unsigned char *pDat;
    unsigned char *pTag;

    sta = nonceGet(h, &nonceVal);
    if (sta != ECODE_NVM3_OK) {
      NVM3_ERROR_ASSERT();
      return ECODE_NVM3_ERR_ENCRYPTION_ENCODE;
    }
    memset(nonceBuf, 0, sizeof(nonceBuf));
    memcpy(nonceBuf, &nonceVal, sizeof(nonceVal));
    pNon = &nvm3_ccmBuf[CCM_OFFS_NONCE(srcLen)];
    pDat = &nvm3_ccmBuf[CCM_OFFS_DATA(srcLen)];
    pTag = &nvm3_ccmBuf[CCM_OFFS_TAG(srcLen)];
    memcpy(pNon, &nonceVal, COUNTER_SIZE_NONCE);

    if (objGroup == objGroupData) {
#if defined(NVM3_USE_MBEDTLS)
      int mbedtlsSta = mbedtls_ccm_encrypt_and_tag(&h->ccmCtx, srcLen, nonceBuf, sizeof(nonceBuf), 0, 0, srcPtr, pDat, pTag, CCM_SIZE_TAG);
      if (mbedtlsSta != 0) {
        NVM3_ERROR_ASSERT();
        return ECODE_NVM3_ERR_ENCRYPTION_ENCODE;
      }
#elif defined(NVM3_USE_ZIGBEECRYPTO)
      memcpy(pDat, srcPtr, srcLen);
      // zigbee data - encrypt
      uint8_t oldKey[16];
      emGetKeyFromCore(oldKey);
      emLoadKeyIntoCore(h->cryptoKey);
      emCcmEncryptPacket(nonceBuf, nvm3_ccmBuf, 4, srcLen, 4);
      emLoadKeyIntoCore(oldKey);
      (void)pTag;
#else
      error "No encryption has been defined"
#endif
    } else if (objGroup == objGroupCounter) {
      memcpy(pDat, srcPtr, srcLen);
    }
  }
#endif

  objBegin(pObjB);
  /* Initialize the object structure. */
  nvm3_objInit(pObjB, NVM3_OBJ_PTR_INVALID);
  pObjB->key = key;
#ifdef NVM3_SUPPORT_ENCRYPTION
  if ((h->encrypted) && (objGroup == objGroupData)) {
    pObjB->totalLen = srcLen + (CCM_SIZE_NONCE + CCM_SIZE_TAG);
    pObjB->srcPtr = nvm3_ccmBuf;
  } else if ((h->encrypted) && (objGroup == objGroupCounter)) {
    pObjB->totalLen = srcLen + COUNTER_SIZE_NONCE;
    pObjB->srcPtr = nvm3_ccmBuf;
  } else {
#endif
  pObjB->totalLen = srcLen;
  pObjB->srcPtr = srcPtr;
#ifdef NVM3_SUPPORT_ENCRYPTION
}
#endif

  /* Write the object to NVM. */
  sta = fifoWriteObj(h, pObjB, COPY_OBJ_FALSE, objGroup);
  objEnd(pObjB);

  return sta;
}

/* Read object from NVM. */
static Ecode_t fifoReadObj(nvm3_Handle_t *h, void *dstPtr,
                           size_t dstLen, nvm3_ObjPtr_t obj,
                           read_operation_t read_operation)
{
  nvm3_ObjPtr_t fragAdr = obj->objAdr;
  nvm3_HalPtr_t srcAdr;
  size_t hdrLen;
  size_t fragLen;
  uint8_t *dstAdr;
  size_t dstTot;
  nvm3_ObjHdrSmall_t fraHdrSmall;

  nvm3_tracePrint(TRACE_LEVEL_LOW, "  fifoReadObj: len=%zu, operation=%s.\n", dstLen, (read_operation == read_data) ? "read" : "compare");

#ifdef NVM3_SUPPORT_ENCRYPTION
  if (h->encrypted) {
    dstAdr = nvm3_ccmBuf;
    dstTot = dstLen + (CCM_SIZE_NONCE + CCM_SIZE_TAG);
  } else {
#endif
  dstAdr = dstPtr;
  dstTot = dstLen;
#ifdef NVM3_SUPPORT_ENCRYPTION
}
#endif

  /* Ensure that the read attempt does not exceed data length. */
  if (dstTot > obj->totalLen) {
    nvm3_tracePrint(NVM3_TRACE_LEVEL_WARNING, "NVM3 ERROR - fifoReadObj: Length mismatch, exp=%zu, act=%zu.\n", dstTot, obj->totalLen);
    return ECODE_NVM3_ERR_READ_DATA_SIZE;
  }

  while (dstTot != 0U) {
    // Check the object key.
    // Mismatch should not be possible since the object was validated.

    // Read small object header
    nvm3_halReadWords(HAL, fragAdr, &fraHdrSmall, NVM3_OBJ_HEADER_SIZE_WSMALL);

    nvm3_ObjectKey_t keyAct = nvm3_objHdrGetKey(&fraHdrSmall);
    if (keyAct != obj->key) {
      nvm3_tracePrint(NVM3_TRACE_LEVEL_ERROR, "NVM3 ERROR - fifoReadObj: fragment has wrong key, exp=%u, act=%u.\n", obj->key, keyAct);
      NVM3_ERROR_ASSERT();
      return ECODE_NVM3_ERR_INT_KEY_MISMATCH;
    }

    hdrLen = nvm3_objHdrGetHdrLen(&fraHdrSmall);
    if (hdrLen == NVM3_OBJ_HEADER_SIZE_SMALL) {
      fragLen = nvm3_objHdrGetDatLen((nvm3_ObjHdrLargePtr_t)&fraHdrSmall);
    } else {
      nvm3_ObjHdrLarge_t fraHdrLarge;

      // Read large object header
      nvm3_halReadWords(HAL, fragAdr, &fraHdrLarge, NVM3_OBJ_HEADER_SIZE_WLARGE);
      fragLen = nvm3_objHdrGetDatLen(&fraHdrLarge);
    }
    nvm3_tracePrint(TRACE_LEVEL_LOW, "  fifoReadObj: hdrLen=%zu, fragLen=%zu.\n", hdrLen, fragLen);

    // Another check on the length.
    // Should not be required, the object length has already been validated.
    if (dstTot < fragLen) {
      nvm3_tracePrint(NVM3_TRACE_LEVEL_ERROR, "NVM3 ERROR - fifoReadObj: Length mismatch, exp=%zu, act=%zu.\n", dstTot, fragLen);
      NVM3_ERROR_ASSERT();
      return ECODE_NVM3_ERR_INT_SIZE_ERROR;
    }

    // Could be moved to obj; hdr size is given, fragments are given.
    // Jump over the header.
    srcAdr = calcAdr(fragAdr, hdrLen);
    // Read object data.
    size_t byteCnt = fragLen;
    size_t wordCnt = byteCnt / sizeof(uint32_t);
    if (read_operation == read_data) {
      if (wordCnt > 0) {
        nvm3_halReadWords(HAL, srcAdr, dstAdr, wordCnt);
        byteCnt -= (wordCnt * sizeof(uint32_t));
        srcAdr = (uint8_t *)srcAdr + (wordCnt * sizeof(uint32_t));
        dstAdr += (wordCnt * sizeof(uint32_t));
      }
      if (byteCnt > 0) {
        uint32_t lastWord;
        nvm3_halReadWords(HAL, srcAdr, &lastWord, 1);
        memcpy(dstAdr, &lastWord, byteCnt);
        dstAdr += byteCnt;
      }
    } else {
      uint32_t data;

      while (wordCnt) {
        nvm3_halReadWords(HAL, srcAdr, &data, 1);
        if (memcmp(dstAdr, &data, sizeof(uint32_t)) != 0) {
          return ECODE_NVM3_ERR_READ_FAILED;
        }
        wordCnt--;
        byteCnt -= (sizeof(uint32_t));
        srcAdr = (uint8_t *)srcAdr + (sizeof(uint32_t));
        dstAdr += (sizeof(uint32_t));
      }
      if (byteCnt > 0) {
        nvm3_halReadWords(HAL, srcAdr, &data, 1);
        if (memcmp(dstAdr, &data, byteCnt) != 0) {
          return ECODE_NVM3_ERR_READ_FAILED;
        }
        dstAdr += byteCnt;
      }
    }

    // Prepare for the next round.
    dstTot -= fragLen;
    fragAdr = getFirstObjAdrInNextGoodPage(h, fragAdr);
  }

#ifdef NVM3_SUPPORT_ENCRYPTION
  if (h->encrypted) {
    unsigned char nonceBuf[CCM_NONCE_SIZE];
    unsigned char *pNon;
    unsigned char *pDat;
    unsigned char *pTag;

    pNon = &nvm3_ccmBuf[CCM_OFFS_NONCE(dstLen)];
    pTag = &nvm3_ccmBuf[CCM_OFFS_TAG(dstLen)];
    pDat = &nvm3_ccmBuf[CCM_OFFS_DATA(dstLen)];
    memset(nonceBuf, 0, sizeof(nonceBuf));
    memcpy(nonceBuf, pNon, COUNTER_SIZE_NONCE);
#if defined(NVM3_USE_MBEDTLS)
    int mbedtlsSta = mbedtls_ccm_auth_decrypt(&h->ccmCtx, dstLen, nonceBuf, sizeof(nonceBuf), 0, 0, pDat, dstPtr, pTag, CCM_SIZE_TAG);
    if (mbedtlsSta != 0) {
      NVM3_ERROR_ASSERT();
      return ECODE_NVM3_ERR_ENCRYPTION_DECODE;
    }
#elif defined(NVM3_USE_ZIGBEECRYPTO)
    // zigbee data - decrypt
    uint8_t oldKey[16];
    emGetKeyFromCore(oldKey);
    emLoadKeyIntoCore(h->cryptoKey);
    bool res = emCcmDecryptPacket(nonceBuf, nvm3_ccmBuf, 4, pDat, dstLen, 4);
    emLoadKeyIntoCore(oldKey);
    if (res) {
      memcpy(dstPtr, pDat, dstLen);
    } else {
      return ECODE_NVM3_ERR_ENCRYPTION_DECODE;
    }
    (void)pTag;
#else
    error "No encryption has been defined"
#endif
  }
#endif

  return ECODE_NVM3_OK;
}

// Validate the object located at the given address.
// For fragmented objects, all fragments are validated recursively.
static bool validateObjFragments(nvm3_Handle_t *h, nvm3_ObjPtr_t fragAdr, nvm3_Obj_t *obj, bool checkAllFrag, nvm3_ObjGroup_t *pObjGroup)
{
  nvm3_HalPtr_t payloadAddr;
  size_t pageFreeBytes;
  bool fragError = false;
  uint32_t key;
  size_t hdrLen;
  bool hdrIsLarge;
  size_t fragLen;
  nvm3_ObjFragType_t fragTyp;
  nvm3_ObjHdrSmall_t objHdrSmall;
  nvm3_ObjHdrSmall_t fraHdrSmall;

  // Read small object header
  nvm3_halReadWords(HAL, obj->objAdr, &objHdrSmall, NVM3_OBJ_HEADER_SIZE_WSMALL);
  nvm3_halReadWords(HAL, fragAdr, &fraHdrSmall, NVM3_OBJ_HEADER_SIZE_WSMALL);

  // Check for erased
  if (nvm3_objHdrGetErased(&objHdrSmall)) {
    nvm3_tracePrint(TRACE_LEVEL_LOW, "      validateObjFragments: Erased.\n");
    obj->isValid  = false;
    obj->isHdrValid = false;
    obj->nextObjAdr = NVM3_OBJ_PTR_INVALID;
    return false;
  }

  key = nvm3_objHdrGetKey(&fraHdrSmall);

  // Initialize the object structure.
  obj->isValid = true;
  obj->frag.detail[obj->frag.idx].adr = fragAdr;
  obj->frag.detail[obj->frag.idx].len = 0;

  if (fragAdr == obj->objAdr) {
    // Set object information when handling the first fragment.
    obj->key = key;
    obj->objType = (uint8_t)nvm3_objHdrGetType(&fraHdrSmall);
    obj->nextObjAdr = NVM3_OBJ_PTR_INVALID;
  } else {
    // Validate the object information for the following fragments.
    if (key != obj->key) {
      obj->isValid = false;
    }
    if (nvm3_objHdrGetType(&fraHdrSmall) != (nvm3_ObjType_t)obj->objType) {
      obj->isValid = false;
    }
    if (nvm3_objHdrGetHdrIsLarge(&fraHdrSmall) != nvm3_objHdrGetHdrIsLarge(&objHdrSmall)) {
      obj->isValid = false;
    }
    if (nvm3_objHdrGetHdrLen(&fraHdrSmall) != nvm3_objHdrGetHdrLen(&objHdrSmall)) {
      obj->isValid = false;
    }
  }

  // Indicates the type of object.
  // Sanity checks on the header are done before the Berger code check.
  if ((obj->objType == (uint8_t)objTypeRes_1)
      || (obj->objType == (uint8_t)objTypeRes_2)
      || (obj->objType == (uint8_t)objTypeRes_3)) {
    obj->isValid = false;
    return false;
  }

  if (nvm3_objHdrGetHdrIsLarge(&fraHdrSmall)) {
    nvm3_ObjHdrLarge_t fraHdrLarge;
    nvm3_halReadWords(HAL, fragAdr, &fraHdrLarge, NVM3_OBJ_HEADER_SIZE_WLARGE);
    fragLen = nvm3_objHdrGetDatLen(&fraHdrLarge);
  } else {
    fragLen = nvm3_objHdrGetDatLen((nvm3_ObjHdrLargePtr_t)&fraHdrSmall);
  }

  switch ((nvm3_ObjType_t)obj->objType) {
    case objTypeCounterSmall:
      obj->frag.detail[obj->frag.idx].len = COUNTER_SIZE;
      break;
    case objTypeCounterLarge:
      // For large counter, the length is coded in the LLEN field.
      obj->frag.detail[obj->frag.idx].len = (uint16_t)fragLen;
      break;
    case objTypeDataLarge:
      obj->frag.detail[obj->frag.idx].len = (uint16_t)fragLen;
      break;
    case objTypeDeleted:
      break;
    default:
      obj->frag.detail[obj->frag.idx].len = (uint16_t)fragLen;
      break;
  }

  *pObjGroup = nvm3_objTypeToGroup((nvm3_ObjType_t)obj->objType);
  obj->frag.detail[obj->frag.idx].typ = (uint8_t)nvm3_objHdrGetFragTyp(&fraHdrSmall);

  // Helper variables.
  fragLen = obj->frag.detail[obj->frag.idx].len;
  fragTyp = (nvm3_ObjFragType_t)obj->frag.detail[obj->frag.idx].typ;

  hdrIsLarge = nvm3_objHdrGetHdrIsLarge(&fraHdrSmall);
  hdrLen = nvm3_objHdrGetHdrLen(&fraHdrSmall);

  if (hdrIsLarge) {
    nvm3_ObjHdrLarge_t fraHdrLarge;

    nvm3_halReadWords(HAL, fragAdr, &fraHdrLarge, NVM3_OBJ_HEADER_SIZE_WLARGE);
    obj->isHdrValid = nvm3_objHdrValidateLarge(&fraHdrLarge);
  } else {
    obj->isHdrValid = nvm3_objHdrValidateSmall(&fraHdrSmall);
  }

  if (!obj->isHdrValid) {
    obj->isValid = false;
  }

  // Check the length.
  if ((obj->isValid) && (fragLen > 0U)) {
    // Jump over the header.
    payloadAddr = calcAdr(fragAdr, hdrLen);

    // Make sure header and payload are on the same page.
    if (!samePage(h, payloadAddr, fragAdr)) {
      obj->isValid = false;
    }

    // An object or fragment will never cross a page boarder.
    // If so, the length field is not valid.
    pageFreeBytes = pageFreeSize(h, payloadAddr);
    if (fragLen > pageFreeBytes) {
      obj->isValid = false;
    }
  }

  /* Handle fragments:
     The following requirements must be met:
     1. All fragments are valid.
     2. Fragmented objects are contiguous in memory. */
  if (obj->isValid) {
    if (fragTyp == fragTypeNone) {
      if (obj->isFragmented) {
        // Un-fragmented object is following a fragmented object.
        fragError = true;
      }
    } else {
      obj->isFragmented = true;
      // Check that the fragments are in the correct order.
      if (!obj->frag.isFirstFragFound) {
        if (fragTyp == fragTypeFirst) {
          obj->frag.isFirstFragFound = true;
        } else {
          fragError = true;
        }
      } else {
        // Look for the last fragment
        if (fragTyp == fragTypeFirst) {
          fragError = true;
        } else {
          if (fragTyp == fragTypeLast) {
            obj->frag.isLastFragFound = true;
          }
        }
      }
    }
  }

  nvm3_tracePrint(TRACE_LEVEL_LOW, "      validateObjFragments: key=%u, len=%zu, total=%zu, adr=%p.\n", key, fragLen, obj->totalLen, fragAdr);

  /* Find the next object location. For fragmentation or
     packet error, the next object is the current object.
     Retry, starting from that new object. */
  if (obj->isValid) {
    if ((!fragError) || ((fragError) && ((fragTyp != fragTypeFirst) && (fragTyp != fragTypeNone)))) {
      obj->nextObjAdr = getNextObj(h, fragAdr, hdrLen, fragLen);
    }
  }

  nvm3_tracePrint(TRACE_LEVEL_LOW, "      validateObjFragments: fragment: is=%u, typ=%u, error=%u, cnt=%u\n", obj->isFragmented ? 1 : 0, fragTyp, fragError, obj->frag.idx);

  if (obj->isValid) {
    obj->totalLen += fragLen;
  }

  if (obj->frag.idx < nvm3_maxFragmentCount) {
    obj->frag.idx++;
  } else {
    obj->isValid = false;
  }

  // If the object is fragmented, validate the next fragment.
  if ((checkAllFrag) && (obj->isValid)) {
    if ((obj->isFragmented)
        && (!fragError)
        && (fragTyp != fragTypeLast)
        && (obj->nextObjAdr != NVM3_OBJ_PTR_INVALID)) {
      obj->isValid = validateObjFragments(h, obj->nextObjAdr, obj, true, pObjGroup);
    }
  }

  nvm3_tracePrint(TRACE_LEVEL_LOW, "      validateObjFragments: done, key=%u, adr=%p, valid=%s, group=%d\n", obj->key, fragAdr, obj->isValid ? "true" : "false", *pObjGroup);

  return obj->isValid;
}

static bool validateObj(nvm3_Handle_t *h, nvm3_Obj_t *obj, bool checkAllFrag, nvm3_ObjGroup_t *pObjGroup)
{
  bool isValid;

  nvm3_tracePrint(TRACE_LEVEL_LOW, "    validateObj: objAdr=%p, key=%u, checkAll=%u.\n", obj->objAdr, obj->key, checkAllFrag);
  obj->frag.idx = 0;
  isValid = validateObjFragments(h, obj->objAdr, obj, checkAllFrag, pObjGroup);
  if (isValid) {
    obj->isValid = obj->isFragmented ? obj->frag.isLastFragFound : true;
  }
  nvm3_tracePrint(TRACE_LEVEL_LOW, "    validateObj: key=%u, valid=%u, grp=%u\n", obj->key, obj->isValid, *pObjGroup);
  return obj->isValid;
}

/* Find the object in the NVM:
     1. First check the cache for a possible match. Even when the object is
        found in cache, it still needs to be validated to retrieve
        all the fragment locations.
     2. If the object is not in cache, start searching the NVM from
        the last page.
     3. In is a special case where the key is a special search
        key, it is used to search the NVM for the last valid object.
        position. */
static Ecode_t findObj(nvm3_Handle_t *h, nvm3_ObjectKey_t key, nvm3_Obj_t *obj, nvm3_ObjGroup_t *pObjGroup)
{
  size_t pageIdx;
  nvm3_HalPtr_t pageAdr;
  nvm3_PageHdr_t pageHdr;
  nvm3_ObjPtr_t objFirstLoc;
  nvm3_ObjPtr_t objAdr = NVM3_OBJ_PTR_INVALID;
  bool found = false;
  bool lastPage = false;
  bool checkAllFrag;
  bool isValid;
  nvm3_ObjGroup_t objGroup = objGroupUnknown;
  NVM3_OBJ_T_ALLOCATION(ObjC);

  nvm3_tracePrint(TRACE_LEVEL_LOW, "  findObj: key=%u.\n", key);

  nvm3_objInit(obj, NVM3_OBJ_PTR_INVALID);

  /* Search the cache first */
  if (keyIsValid(key)) {
    nvm3_ObjGroup_t group;
    objAdr = nvm3_cacheGet(&h->cache, key, &group);
  }

  nvm3_tracePrint(TRACE_LEVEL_LOW, "  findObj: cache adr=%p.\n", objAdr);

  /* If Object is in the cache, make sure it is valid, this
     is required to populate all fragment position. */
  if (objAdr != NVM3_OBJ_PTR_INVALID) {
    nvm3_objInit(obj, objAdr);
    isValid = validateObj(h, obj, true, &objGroup);
    if (isValid) {
      found = true;
      *pObjGroup = objGroup;
    } else {
      nvm3_tracePrint(NVM3_TRACE_LEVEL_ERROR, "NVM3 ERROR - findObj: cache hit, but the object is NOT valid, key=%u, adr=%p.\n", key, objAdr);
      NVM3_ERROR_ASSERT();
    }
  }

  nvm3_tracePrint(TRACE_LEVEL_LOW, "  findObj: found=%s.\n", found ? "true" : "false");

  // Find the object if not in the cache.
  if (keyIsSearchKey(key) || (!found && h->cache.overflow)) {
    /* Search from the FIFO last location,
       or from FIFO first page if  the next location
       is not defined yet */
    if (h->fifoNextObj == NVM3_OBJ_PTR_INVALID) {
      pageIdx = getPreviousGoodPage(h, h->fifoFirstIdx);
    } else {
      pageIdx = pageIdxFromAdr(h, h->fifoNextObj);
    }

    // The search key is used to look for any object, not the last valid.
    checkAllFrag = !keyIsSearchKey(key);

    // Search from FIFO last to first object.
    do {
      // Search until the first page is reached.
      if (pageIdx == h->fifoFirstIdx) {
        lastPage = true;
      }

      //nvm3_tracePrint(TRACE_LEVEL_LOW, "  findObj: search idx=%u.\n", pageIdx);

      // Get the page base address.
      pageAdr = pageAdrFromIdx(h, pageIdx);
      nvm3_halReadWords(HAL, pageAdr, &pageHdr, NVM3_PAGE_HEADER_WSIZE);

      // Sanity check if the page is not good.
      nvm3_PageState_t pageState = nvm3_pageGetState(&pageHdr);
      if (!nvm3_pageStateIsGood(pageState)) {
        nvm3_tracePrint(NVM3_TRACE_LEVEL_ERROR, "NVM3 ERROR - findObj: Invalid page state: bIdx=%zu, state=%d.\n", h->fifoFirstIdx, pageState);
        NVM3_ERROR_ASSERT();
      }

      objFirstLoc = nvm3_pageGetFirstObj(pageAdr);
      objAdr = objFirstLoc;
      // Search until outside this page.
      while ((objAdr != NVM3_OBJ_PTR_INVALID) && samePage(h, objAdr, objFirstLoc)) {
        objBegin(pObjC);
        nvm3_objInit(pObjC, objAdr);
        isValid = validateObj(h, pObjC, checkAllFrag, &objGroup);
        if (isValid) {
          if ((!checkAllFrag) || ((checkAllFrag) && (key == pObjC->key))) {
            found = true;
            *pObjGroup = objGroup;
            // Copy the object.
            (void)memcpy(obj, pObjC, nvm3_objHandleSize);
          }
        } else {
          // Do nothing.
        }

        // Go to the next object.
        // The validate function provides us with the next possible object location.
        objAdr = pObjC->nextObjAdr;
        objEnd(pObjC);
      }

      // Done with that page, search the previous page.
      pageIdx = getPreviousGoodPage(h, pageIdx);
    } while ((!lastPage) && (!found));

    if (found && keyIsValid(key)) {
      nvm3_cacheSet(&h->cache, key, obj->objAdr, *pObjGroup);
    }
  }

  Ecode_t sta = (found ? ECODE_NVM3_OK : ECODE_NVM3_ERR_KEY_NOT_FOUND);
  nvm3_tracePrint(TRACE_LEVEL_LOW, "  findObj: ----- key=%u, ecode=0x%x, objAdr=%p, objGroup=%u -----\n", key, sta, obj->objAdr, *pObjGroup);

  return sta;
}

/* Read object data (as a word) from the position given by the index. */
static uint32_t getObjContent(nvm3_Handle_t *h, size_t hdrLen, nvm3_Obj_t *obj, size_t ofs)
{
  nvm3_HalPtr_t adr = obj->objAdr;
  uint32_t word;
  size_t offset;

  if (!obj->isFragmented) {
    adr = calcAdr(obj->objAdr, hdrLen + ofs);
  } else {
    offset = ofs;
    for (uint8_t i = 0; i < obj->frag.idx; i++) {
      if (obj->frag.detail[i].len > offset) {
        adr = calcAdr(obj->frag.detail[i].adr, hdrLen + offset);
        break;
      } else {
        offset -= obj->frag.detail[i].len;
      }
    }
  }
  nvm3_halReadWords(HAL, adr, &word, 1);

  return word;
}

static size_t findValidPageCnt(nvm3_Handle_t *h)
{
  size_t idx;
  nvm3_HalPtr_t pageAdr;
  nvm3_PageHdr_t pageHdr;
  nvm3_PageState_t pageState;

  h->validNvmPageCnt = 0;
  for (idx = 0; idx < h->totalNvmPageCnt; idx++) {
    pageAdr = pageAdrFromIdx(h, idx);
    nvm3_halReadWords(HAL, pageAdr, &pageHdr, NVM3_PAGE_HEADER_WSIZE);
    pageState = nvm3_pageGetState(&pageHdr);
    if (nvm3_pageStateIsGood(pageState)) {
      h->validNvmPageCnt++;
    }
  }

  return h->validNvmPageCnt;
}

// Look for valid pages and use the erase counter to find the first page in the FIFO.
static void findFirstPage(nvm3_Handle_t *h)
{
  size_t idx;
  nvm3_HalPtr_t pageAdr;
  nvm3_PageHdr_t pageHdr;
  nvm3_PageState_t pageState;
  uint32_t eraseCnt;
  uint32_t eraseCntPre = NVM3_ERASE_COUNT_INVALID;
  uint32_t eraseCntMin = NVM3_ERASE_COUNT_INVALID;
  uint32_t eraseCntMax = 0;

  h->fifoFirstIdx = NVM3_PAGE_INDEX_INVALID;
  h->validNvmPageCnt = 0;

  for (idx = 0; idx < h->totalNvmPageCnt; idx++) {
    pageAdr = pageAdrFromIdx(h, idx);
    nvm3_halReadWords(HAL, pageAdr, &pageHdr, NVM3_PAGE_HEADER_WSIZE);
    pageState = nvm3_pageGetState(&pageHdr);
    eraseCnt = nvm3_pageGetEraseCnt(&pageHdr);
    if (nvm3_pageStateIsGood(pageState)) {
      h->validNvmPageCnt++;
      if (eraseCnt < eraseCntPre) {
        h->fifoFirstIdx = idx;
      }
      if (eraseCnt < eraseCntMin) {
        eraseCntMin = eraseCnt;
      }
      if (eraseCnt > eraseCntMax) {
        eraseCntMax = eraseCnt;
      }
      eraseCntPre = eraseCnt;
    }
  }

  if ((eraseCntMax - eraseCntMin) > 1U) {
    h->fifoFirstIdx = NVM3_PAGE_INDEX_INVALID;
    nvm3_tracePrint(NVM3_TRACE_LEVEL_ERROR, "NVM3 ERROR - findFirstPage: Error in erase count, min=%u, max=%u.\n", eraseCntMin, eraseCntMax);
    for (idx = 0; idx < h->totalNvmPageCnt; idx++) {
      pageAdr = pageAdrFromIdx(h, idx);
      nvm3_halReadWords(HAL, pageAdr, &pageHdr, NVM3_PAGE_HEADER_WSIZE);
      eraseCnt = nvm3_pageGetEraseCnt(&pageHdr);
      nvm3_tracePrint(NVM3_TRACE_LEVEL_ERROR, "  ERROR: idx=%zu, adr=%p, eraseCnt=%u\n", idx, pageAdr, eraseCnt);
    }
    NVM3_ERROR_ASSERT();
  }
}

static size_t findNumberOfUnusedPages(nvm3_Handle_t *h)
{
  size_t idx;
  nvm3_HalPtr_t pageAdr;
  nvm3_PageHdr_t pageHdr;
  nvm3_PageState_t pageState;
  size_t unusedSize;
  size_t unusedCnt = 0;

  for (idx = 0; idx < h->totalNvmPageCnt; idx++) {
    pageAdr = pageAdrFromIdx(h, idx);
    nvm3_halReadWords(HAL, pageAdr, &pageHdr, NVM3_PAGE_HEADER_WSIZE);
    pageState = nvm3_pageGetState(&pageHdr);
    if (nvm3_pageStateIsGood(pageState)) {
      unusedSize = scanPage(h, nvm3_pageGetFirstObj(pageAdr));
      if (unusedSize == (h->halInfo.pageSize - sizeof(nvm3_PageHdr_t))) {
        unusedCnt++;
      }
    }
  }

  return unusedCnt;
}

static void findFirstObj(nvm3_Handle_t *h)
{
  nvm3_HalPtr_t pageAdr;

  pageAdr = pageAdrFromIdx(h, h->fifoFirstIdx);
  h->fifoFirstObj = nvm3_pageGetFirstObj(pageAdr);
}

static void findNextObj(nvm3_Handle_t *h)
{
  Ecode_t sta;
  nvm3_ObjGroup_t objGroup;
  NVM3_OBJ_T_ALLOCATION(ObjB);

  objBegin(pObjB);
  // Find out where the last location is in the FIFO.
  h->fifoNextObj = NVM3_OBJ_PTR_INVALID;
  sta = findObj(h, SEARCH_KEY, pObjB, &objGroup);
  if (sta == ECODE_NVM3_OK) {
    h->fifoNextObj = pObjB->nextObjAdr;
  } else {
    // No valid objects found, the FIFO is empty.
    h->fifoNextObj = h->fifoFirstObj;
  }
  objEnd(pObjB);

  // Set the free memory pointer to the first possible object address.
  nvm3_ObjPtr_t freeObj = h->fifoNextObj;

  /* Make sure the page holding the last object is
     empty after the last object. Otherwise, that means
     the last write was aborted and the next object location
     must be moved to the next page base. */
  h->unusedNvmSize = scanPage(h, h->fifoNextObj);
  if (h->unusedNvmSize == 0U) {
    // Some garbage was found.
    h->fifoNextObj = getFirstObjAdrInNextGoodPage(h, h->fifoNextObj);
  }

  /* Check all the remaining pages. They should
     be empty. Otherwise move the next location
     pointer, and restart counting the amount of
     free memory. */
  freeObj = getFirstObjAdrInNextGoodPage(h, freeObj);
  while (freeObj != h->fifoFirstObj) {
    uint32_t unusedNvmSize = scanPage(h, freeObj);
    h->unusedNvmSize += unusedNvmSize;
    freeObj = getFirstObjAdrInNextGoodPage(h, freeObj);
    if (unusedNvmSize == 0U) {
      // A non-empty page was found.
      h->unusedNvmSize = 0;
      h->fifoNextObj = freeObj;
    }
  }
}

typedef enum {
  fifoScanAll,
  fifoScanFirst,
} FifoScanArea_t;

static void fifoScan(nvm3_Handle_t *h, FifoScanArea_t fifoScanArea, FifoScanCallback_t fifoScanCallback, void *user)
{
  nvm3_ObjPtr_t objAdr;
  nvm3_ObjGroup_t objGroup;
  bool isValid;
  bool keepGoing;
  NVM3_OBJ_T_ALLOCATION(ObjD);

  objAdr = h->fifoFirstObj;
  // Scan all objects from oldest to newest.
  while ((objAdr != h->fifoNextObj) && (objAdr != NVM3_OBJ_PTR_INVALID)) {
    // Get the current object.
    objBegin(pObjD);
    nvm3_objInit(pObjD, objAdr);
    isValid = validateObj(h, pObjD, true, &objGroup);
    if (isValid) {
      keepGoing = fifoScanCallback(h, pObjD, objGroup, user);
      if (!keepGoing) {
        objEnd(pObjD);
        return;
      }
    }
    // Get the next object.
    if (pObjD->nextObjAdr != NVM3_OBJ_PTR_INVALID) {
      objAdr = pObjD->nextObjAdr;
    } else {
      objAdr = getFirstObjAdrInNextGoodPage(h, pObjD->objAdr);
    }
    // Check scan area
    if ((fifoScanArea == fifoScanFirst) && (!samePage(h, objAdr, pObjD->objAdr))) {
      objAdr = NVM3_OBJ_PTR_INVALID;
    }
    objEnd(pObjD);
  }
}

static bool repackFirstPageCallback(nvm3_Handle_t *h, nvm3_ObjPtr_t obj, nvm3_ObjGroup_t group, void *user)
{
  nvm3_ObjGroup_t objFindGroup;
  Ecode_t *sta = user;
  NVM3_OBJ_T_ALLOCATION(ObjB);

  if (group != objGroupDeleted) {
    objBegin(pObjB);
    *sta = findObj(h, obj->key, pObjB, &objFindGroup);
    if ((*sta == ECODE_NVM3_OK) && (objFindGroup != objGroupDeleted) && (pObjB->objAdr == obj->objAdr)) {
      objEnd(pObjB);
      *sta = fifoWriteObj(h, obj, COPY_OBJ_TRUE, group);
      if (*sta != ECODE_NVM3_OK) {
        nvm3_tracePrint(NVM3_TRACE_LEVEL_WARNING, "NVM3 ERROR - repackFirstPageCallback: Write error, sta=0x%x.\n", *sta);
        NVM3_ERROR_ASSERT();  // Assert even if it is defined as a warning, used during test.
        return false;
      }
    } else {
      objEnd(pObjB);
    }
  }

  return true;
}

// Repack the FIFO first page. Copy objects if a newer object does not exist.
static Ecode_t repackFirstPage(nvm3_Handle_t *h)
{
  nvm3_HalPtr_t pageAdr;
  nvm3_PageHdr_t pageHdr;
  Ecode_t sta = ECODE_NVM3_OK;

  pageAdr = pageAdrFromIdx(h, h->fifoFirstIdx);
  nvm3_tracePrint(TRACE_LEVEL_REPACK, "    repackFirstPage: idx=%zu, adr=%p.\n", h->fifoFirstIdx, pageAdr);

  nvm3_halReadWords(HAL, pageAdr, &pageHdr, NVM3_PAGE_HEADER_WSIZE);
  nvm3_PageState_t pageState = nvm3_pageGetState(&pageHdr);
  if (pageState == nvm3_PageStateGoodEip) {
    nvm3_tracePrint(NVM3_TRACE_LEVEL_WARNING, "NVM3 WARNING - repackFirstPage: EIP was already set, no operation.\n");
    return ECODE_NVM3_OK;
  }

  if (h->fifoFirstObj == h->fifoNextObj) {
    // First page is empty
    h->fifoNextObj = getFirstObjAdrInNextGoodPage(h, h->fifoFirstObj);
    h->unusedNvmSize -= (h->halInfo.pageSize - NVM3_PAGE_HEADER_SIZE);
  } else {
    // Copy unique data
    fifoScan(h, fifoScanFirst, repackFirstPageCallback, &sta);
  }
  if (sta == ECODE_NVM3_OK) {
    // Mark page as ready to be erased.
    pageAdr = pageAdrFromIdx(h, h->fifoFirstIdx);
    (void)nvm3_pageSetEip(HAL, pageAdr);
  }

  nvm3_tracePrint(TRACE_LEVEL_REPACK, "    repackFirstPage: done status=%u, nextObj=%p.\n", sta, h->fifoNextObj);

  return sta;
}

static uint32_t findCurrentEraseCnt(nvm3_Handle_t *h, size_t idx)
{
  nvm3_HalPtr_t pageAdr;
  nvm3_PageHdr_t pageHdr;
  uint32_t eraseCnt;
  nvm3_PageState_t pageState;

  pageAdr = pageAdrFromIdx(h, idx);
  nvm3_halReadWords(HAL, pageAdr, &pageHdr, NVM3_PAGE_HEADER_WSIZE);
  pageState = nvm3_pageGetState(&pageHdr);
  if (nvm3_pageStateIsGood(pageState)) {
    eraseCnt = nvm3_pageGetEraseCnt(&pageHdr);
  } else {
    size_t validIdx;

    validIdx = getPreviousGoodPage(h, idx);
    pageAdr = pageAdrFromIdx(h, validIdx);
    nvm3_halReadWords(HAL, pageAdr, &pageHdr, NVM3_PAGE_HEADER_WSIZE);
    eraseCnt = nvm3_pageGetEraseCnt(&pageHdr);
    if ((eraseCnt != NVM3_ERASE_COUNT_INVALID) && (validIdx > idx)) {
      eraseCnt++;
    }
  }

  return eraseCnt;
}

static uint32_t findNewEraseCnt(nvm3_Handle_t *h, size_t idx, uint32_t newEraseCnt)
{
  uint32_t eraseCnt;

  eraseCnt = findCurrentEraseCnt(h, idx);
  if (eraseCnt == NVM3_ERASE_COUNT_INVALID) {
    eraseCnt = newEraseCnt;
  }
  eraseCnt++;

  return eraseCnt;
}

static Ecode_t erasePage(nvm3_Handle_t *h, size_t idx, uint32_t eraseCnt)
{
  nvm3_HalPtr_t pageAdr;
  Ecode_t sta;

  if (eraseCnt == NVM3_ERASE_COUNT_INVALID) {
    nvm3_tracePrint(NVM3_TRACE_LEVEL_ERROR, "NVM3 ERROR - erasePage: Invalid erase count.\n");
    NVM3_ERROR_ASSERT();
  }

  pageAdr = pageAdrFromIdx(h, idx);
  sta = nvm3_pageErase(HAL, pageAdr, eraseCnt, &h->halInfo, h->encrypted);
  if (sta != ECODE_NVM3_OK) {
    nvm3_tracePrint(NVM3_TRACE_LEVEL_ERROR, "NVM3 ERROR - erasePage: idx=%zu, Erase error.\n", idx);
    NVM3_ERROR_ASSERT();
  }

  (void)findValidPageCnt(h);

  //nvm3_tracePrint(TRACE_LEVEL_LOW, "  erasePage: sta=%u.\n", sta);
  return sta;
}

static bool eraseFirstPageCallback(nvm3_Handle_t *h, nvm3_ObjPtr_t objPtr, nvm3_ObjGroup_t objGroup, void *user)
{
  nvm3_ObjPtr_t objCachePtr;
  nvm3_ObjGroup_t cacheGroup;
  nvm3_ObjGroup_t objFindGroup;
  NVM3_OBJ_T_ALLOCATION(ObjB);

  (void)user;
  objCachePtr = nvm3_cacheGet(&h->cache, objPtr->key, &cacheGroup);
  if ((objGroup == objGroupDeleted) && (objCachePtr == objPtr->objAdr)) {
    Ecode_t sta;
    objBegin(pObjB);
    sta = findObj(h, objPtr->key, pObjB, &objFindGroup);
    if ((sta == ECODE_NVM3_OK) && (objFindGroup == objGroupDeleted) && (pObjB->objAdr == objPtr->objAdr)) {
      nvm3_cacheDelete(&h->cache, objPtr->key);
    } else {
      nvm3_tracePrint(NVM3_TRACE_LEVEL_ERROR, "NVM3 ERROR - eraseFirstPageCallback: Found an object that was not copied.\n");
      NVM3_ERROR_ASSERT();
    }
    objEnd(pObjB);
  }

  return true;
}

/* Erase the first page and move the FIFO first page index to the
   next valid page. */
static Ecode_t eraseFirstPage(nvm3_Handle_t *h)
{
  uint32_t eraseCnt;
  Ecode_t sta;

  nvm3_tracePrint(TRACE_LEVEL_REPACK, "    eraseFirstPage: Begin, idx=%zu.\n", h->fifoFirstIdx);

  fifoScan(h, fifoScanFirst, eraseFirstPageCallback, NULL);

  eraseCnt = findCurrentEraseCnt(h, h->fifoFirstIdx);
  eraseCnt++;
  sta = erasePage(h, h->fifoFirstIdx, eraseCnt);
  if (sta == ECODE_NVM3_OK) {
    h->unusedNvmSize += (h->halInfo.pageSize - NVM3_PAGE_HEADER_SIZE);
  }

  // Move first page index.
  h->fifoFirstObj = getFirstObjAdrInNextGoodPage(h, h->fifoFirstObj);
  h->fifoFirstIdx = pageIdxFromAdr(h, h->fifoFirstObj);

  nvm3_tracePrint(TRACE_LEVEL_REPACK, "    eraseFirstPage: End,   idx=%zu.\n", h->fifoFirstIdx);

  return sta;
}

// Erase all NVM pages.
// The purpose is to remove all objects and clean all pages from any content.
// Note1: Pages with BAD marking are preserved as is, they are not erased.
// Note2: All other pages are erased, and the page erase count is maintained.
// To maintain a sequence where the first page can be found at init, and tolerate
// that the function is reset in the sequence, the function will start at the page
// index that is next in line for re-use.
static void eraseAllPages(nvm3_Handle_t *h, uint32_t newCfgEraseCnt)
{
  uint32_t newEraseCnt;
  size_t idx;
  size_t i;
  nvm3_HalPtr_t pageAdr;
  nvm3_PageHdr_t pageHdr;
  nvm3_PageState_t pageState;
  bool firstIdxIsUnknown;

  //nvm3_tracePrint(TRACE_LEVEL_LOW, "  eraseAllPages: newCfgEraseCnt=%u\n", newCfgEraseCnt);

  firstIdxIsUnknown = (h->fifoFirstIdx == NVM3_PAGE_INDEX_INVALID);
  idx = firstIdxIsUnknown ? 0 : h->fifoFirstIdx;
  for (i = 0; i < h->totalNvmPageCnt; i++) {
    pageAdr = pageAdrFromIdx(h, idx);
    nvm3_halReadWords(HAL, pageAdr, &pageHdr, NVM3_PAGE_HEADER_WSIZE);
    pageState = nvm3_pageGetState(&pageHdr);
    // Do not erase BAD pages.
    if (pageState != nvm3_PageStateBad) {
      if (firstIdxIsUnknown) {
        newEraseCnt = newCfgEraseCnt + 1;
      } else {
        newEraseCnt = findNewEraseCnt(h, idx, newCfgEraseCnt);
      }
      (void)erasePage(h, idx, newEraseCnt);
    }
    idx++;
    idx %= h->totalNvmPageCnt;
  }

  // Leave the nvm3 in the old state.
  // The new state must be obtained by calling initialize
}

static void eraseEipAndInvalidPages(nvm3_Handle_t *h)
{
  nvm3_HalPtr_t pageAdr;
  nvm3_PageHdr_t pageHdr;
  nvm3_PageState_t pageState;
  size_t idx;
  uint32_t eraseCnt;

  for (idx = 0; idx < h->totalNvmPageCnt; idx++) {
    pageAdr = pageAdrFromIdx(h, idx);
    nvm3_halReadWords(HAL, pageAdr, &pageHdr, NVM3_PAGE_HEADER_WSIZE);
    pageState = nvm3_pageGetState(&pageHdr);
    if ((pageState == nvm3_PageStateGoodEip) || (nvm3_pageStateIsInvalid(pageState))) {
      eraseCnt = findCurrentEraseCnt(h, idx);
      if (pageState == nvm3_PageStateGoodEip) {
        eraseCnt++;
      }
      (void)erasePage(h, idx, eraseCnt);
    }
  }
}

// Repack the first page according to the page state.
static Ecode_t repackWorker(nvm3_Handle_t *h, nvm3_PageState_t *pageState)
{
  Ecode_t sta;
  nvm3_HalPtr_t pageAdr;
  nvm3_PageHdr_t pageHdr;

  if (h->minUnused > h->unusedNvmSize) {
    h->minUnused = h->unusedNvmSize;
  }

  pageAdr = pageAdrFromIdx(h, h->fifoFirstIdx);
  nvm3_halReadWords(HAL, pageAdr, &pageHdr, NVM3_PAGE_HEADER_WSIZE);
  *pageState = nvm3_pageGetState(&pageHdr);
  if (*pageState != nvm3_PageStateGoodEip) {
    //UnityPrintf("  repackWorker: repackFirstPage\n");
    sta = repackFirstPage(h);
  } else {
    //UnityPrintf("  repackWorker: eraseFirstPage\n");
    sta = eraseFirstPage(h);
    size_t freeB = getFreeSize(h);
    if (h->unusedNvmSize != freeB) {
      nvm3_tracePrint(NVM3_TRACE_LEVEL_ERROR, "NVM3 ERROR - repackWorker: Free size mismatch, a=%zu, b=%zu.\n", h->unusedNvmSize, freeB);
      NVM3_ERROR_ASSERT();
    }
  }
  nvm3_halReadWords(HAL, pageAdr, &pageHdr, NVM3_PAGE_HEADER_WSIZE);
  *pageState = nvm3_pageGetState(&pageHdr);

  if (h->minUnused > h->unusedNvmSize) {
    h->minUnused = h->unusedNvmSize;
  }

  return sta;
}

// Run repack just a single time
static Ecode_t repackOnce(nvm3_Handle_t *h)
{
  nvm3_PageState_t pageState;
  Ecode_t sta;

  nvm3_tracePrint(TRACE_LEVEL_REPACK, "  repackOnce: Begin, unusedNvmSize=%zu.\n", h->unusedNvmSize);
  sta = repackWorker(h, &pageState);
  (void)pageState;
  nvm3_tracePrint(TRACE_LEVEL_REPACK, "  repackOnce: End,   unusedNvmSize=%zu, nextObj=%p.\n", h->unusedNvmSize, h->fifoNextObj);

  return sta;
}

// Run repack until the soft threshold is reached or the FIFO has been passed through.
static Ecode_t repackUntilGood(nvm3_Handle_t *h)
{
  size_t i = 0;
  nvm3_PageState_t pageState = nvm3_PageStateGood;
  Ecode_t sta = ECODE_NVM3_OK;
#if NVM3_TRACE_ENABLED
  size_t freePre;
#endif

  nvm3_tracePrint(TRACE_LEVEL_REPACK, "  repackUntilGood: Begin, unusedNvmSize=%zu, thrSoftMinimum=%zu.\n", h->unusedNvmSize, thrSoftMinimum(h));

  while ((!softMinimumAvailable(h)) && (i < (h->validNvmPageCnt * 2U))) {
#if NVM3_TRACE_ENABLED
    freePre = h->unusedNvmSize;
#endif
    sta = repackWorker(h, &pageState);
    if (sta != ECODE_NVM3_OK) {
      break;
    }
    nvm3_tracePrint(TRACE_LEVEL_REPACK, "  repackUntilGood: Work, unusedNvmSize=%zu -> %zu.\n", freePre, h->unusedNvmSize);
    i++;
  }
  if (pageState == nvm3_PageStateGoodEip) {
    nvm3_tracePrint(TRACE_LEVEL_REPACK, "  repackUntilGood: One extra Work round is needed.\n");
    sta = repackWorker(h, &pageState);
    (void)pageState;
  }
  nvm3_tracePrint(TRACE_LEVEL_REPACK, "  repackUntilGood: End,   unusedNvmSize=%zu, nextObj=%p.\n", h->unusedNvmSize, h->fifoNextObj);

  return sta;
}

static nvm3_HalPtr_t counterIdxToAdr(nvm3_Handle_t *h, nvm3_Obj_t *obj, size_t idx, bool *high)
{
  nvm3_HalPtr_t incAddr = 0;
  size_t incOffset;
  size_t hdrLen;
  size_t incLen;

  hdrLen = NVM3_OBJ_HEADER_SIZE_COUNTER;
  incLen = (h->halInfo.writeSize == NVM3_HAL_WRITE_SIZE_16) ? sizeof(uint16_t) : sizeof(uint32_t);
  *high = false;
  if (!obj->isFragmented) {
    incOffset = sizeof(uint32_t) + idx * incLen;
#ifdef NVM3_SUPPORT_ENCRYPTION
    incOffset += h->encrypted ? COUNTER_SIZE_NONCE : 0;
#endif
    if ((incOffset & 0x3U) != 0U) {
      incOffset &= ~0x03U;
      *high = true;
    }
    incAddr = calcAdr(obj->objAdr, hdrLen + incOffset);
  } else {
    incOffset = sizeof(uint32_t) + idx * incLen;
#ifdef NVM3_SUPPORT_ENCRYPTION
    incOffset += h->encrypted ? COUNTER_SIZE_NONCE : 0;
#endif
    if ((incOffset & 0x3U) != 0U) {
      incOffset &= ~0x03U;
      *high = true;
    }
    for (uint8_t i = 0; i < obj->frag.idx; i++) {
      if (obj->frag.detail[i].len > incOffset) {
        incAddr = calcAdr(obj->frag.detail[i].adr, hdrLen + incOffset);
        break;
      } else {
        incOffset -= obj->frag.detail[i].len;
      }
    }
  }

  return incAddr;
}

static void counterIncFldGet(nvm3_Handle_t *h, nvm3_Obj_t *obj, size_t idx, uint32_t *inc)
{
  nvm3_HalPtr_t incAddr;
  bool high;

  incAddr = counterIdxToAdr(h, obj, idx, &high);
  if (h->halInfo.writeSize == NVM3_HAL_WRITE_SIZE_16) {
    uint32_t data;

    nvm3_halReadWords(HAL, incAddr, &data, 1);
    if (high) {
      data >>= 16;
    }
    *inc = data & 0xffffU;
  } else {
    nvm3_halReadWords(HAL, incAddr, inc, 1);
  }
}

static Ecode_t counterIncFldSet(nvm3_Handle_t *h, nvm3_Obj_t *obj, size_t idx, uint32_t inc)
{
  nvm3_HalPtr_t incAddr;
  bool high;
  Ecode_t sta;

  incAddr = counterIdxToAdr(h, obj, idx, &high);
  if (h->halInfo.writeSize == NVM3_HAL_WRITE_SIZE_16) {
    uint32_t data;

    nvm3_halReadWords(HAL, incAddr, &data, 1);
    if (high) {
      data = (data & 0xFFFFU) | (inc << 16U);
    } else {
      data = (data & 0xFFFF0000U) | (inc & 0xFFFFU);
    }
    sta = nvm3_halWriteWords(HAL, incAddr, &data, 1);
  } else {
    sta = nvm3_halWriteWords(HAL, incAddr, &inc, 1);
  }

  return sta;
}

static bool counterHasNewBase(nvm3_Handle_t *h, nvm3_Obj_t *obj, size_t idx)
{
  bool res = false;
  uint32_t incField0;

  if (h->halInfo.writeSize == NVM3_HAL_WRITE_SIZE_16) {
    if (idx < (counterMaxNumberOfInc(h) - 2U)) {
      counterIncFldGet(h, obj, idx, &incField0);
      if ((incField0 & 0xFFU) == 0U) {
        res = true;
      }
    }
  } else {
    if (idx < (counterMaxNumberOfInc(h) - 1U)) {
      counterIncFldGet(h, obj, idx, &incField0);
      if ((incField0 & 0xFFFFU) == 0U) {
        res = true;
      }
    }
  }

  return res;
}

static bool counterIsValidNewBase(nvm3_Handle_t *h, nvm3_Obj_t *obj, size_t idx, uint32_t *base)
{
  uint8_t BCCB;
  bool res = false;

  uint32_t incField0;
  uint32_t incField1;
  uint32_t incField2;

  if (h->halInfo.writeSize == NVM3_HAL_WRITE_SIZE_16) {
    if (idx < (counterMaxNumberOfInc(h) - 2U)) {
      counterIncFldGet(h, obj, idx, &incField0);
      counterIncFldGet(h, obj, idx + 1U, &incField1);
      counterIncFldGet(h, obj, idx + 2U, &incField2);
      BCCB = 8;
      nvm3_utilsComputeBergerCode(&BCCB, &incField1, 16);
      nvm3_utilsComputeBergerCode(&BCCB, &incField2, 16);
      if (((incField0 & 0xFFU) == 0U) && ((incField0 >> 8U) == BCCB)) {
        *base = incField1 + (incField2 << 16);
        res = true;
      }
    }
  } else {
    if (idx < (counterMaxNumberOfInc(h) - 1U)) {
      counterIncFldGet(h, obj, idx, &incField0);
      counterIncFldGet(h, obj, idx + 1U, &incField1);
      BCCB = 16;
      nvm3_utilsComputeBergerCode(&BCCB, &incField1, 32);
      if (((incField0 & 0xFFFFU) == 0U) && ((incField0 >> 16) == BCCB)) {
        *base = incField1;
        res = true;
      }
    }
  }

  return res;
}

static bool counterIsValidNewInc(nvm3_Handle_t *h, nvm3_Obj_t *obj, size_t idx, uint32_t *inc)
{
  uint8_t BCCB = 0;
  bool sta;
  uint32_t incField0;

  *inc = 0;
  counterIncFldGet(h, obj, idx, &incField0);

  if (h->halInfo.writeSize == NVM3_HAL_WRITE_SIZE_16) {
    if (incField0 == 0xFFFFU) {
      sta = false;
    } else {
      nvm3_utilsComputeBergerCode(&BCCB, &incField0, 8);
      sta = BCCB == (uint8_t)(incField0 >> 8);
      if (sta) {
        *inc = (incField0 & 0xFFU);
      }
    }
  } else {
    if (incField0 == 0xFFFFFFFFU) {
      sta = false;
    } else {
      nvm3_utilsComputeBergerCode(&BCCB, &incField0, 16);
      sta = BCCB == (uint8_t)(incField0 >> 16);
      if (sta) {
        *inc = (incField0 & 0xFFFFU);
      }
    }
  }

  return sta;
}

/* Reconstruct the counter value from the base and increments. */
static uint32_t readCounter(nvm3_Handle_t *h, nvm3_Obj_t *obj)
{
  uint32_t counterBase = 0;
  size_t idx;
  // Get counter base.
#ifdef NVM3_SUPPORT_ENCRYPTION
  uint32_t nonce = 0;
  Ecode_t sta;
  size_t len = COUNTER_SIZE + (h->encrypted ? COUNTER_SIZE_NONCE : 0);
  bool enc = h->encrypted;
  h->encrypted = false;
  sta = fifoReadObj(h, nvm3_ccmBuf, len, obj, read_data);
  if (sta != ECODE_NVM3_OK) {
    NVM3_ERROR_ASSERT();
    return 0;
  }
  h->encrypted = enc;
  counterBase = *(uint32_t *)(&nvm3_ccmBuf[h->encrypted ? COUNTER_SIZE_NONCE : 0]);
  if (h->encrypted) {
    nonce = *((uint32_t *)nvm3_ccmBuf);
    counterBase = counterDecrypt(h, nonce, counterBase);
  }
  nvm3_tracePrint(TRACE_LEVEL_COUNTER, "      cntRd: nonce=%u, base=%u.\n", nonce, counterBase);
#else
  size_t hdrLen;
  nvm3_HalPtr_t adr;

  hdrLen = NVM3_OBJ_HEADER_SIZE_COUNTER;
  adr = calcAdr(obj->objAdr, hdrLen);
  nvm3_halReadWords(HAL, adr, &counterBase, 1);
  nvm3_tracePrint(TRACE_LEVEL_COUNTER, "      cntRd: oAdr=%p, bAdr=%p, base=%u.\n", obj->objAdr, adr, counterBase);
#endif

  // Look for info in the counter increments.
  // It can be either increments or new base values.
  idx = 0;
  while (idx < counterMaxNumberOfInc(h)) {
    bool isANewBase;

    isANewBase = counterHasNewBase(h, obj, idx);
    if (isANewBase) {
      // Index is pointing to a base value
      bool isAValidNewBase;

      isAValidNewBase = counterIsValidNewBase(h, obj, idx, &counterBase);
      if (isAValidNewBase) {
        // Index is pointing to a valid new base
#ifdef NVM3_SUPPORT_ENCRYPTION
        if (h->encrypted) {
          counterBase = counterDecrypt(h, nonce, counterBase);
        }
#endif
        nvm3_tracePrint(TRACE_LEVEL_COUNTER, "      cntRd: idx=%2zu, base=%u.\n", idx, counterBase);
      } else {
        // Index is pointing to an invalid new base, just ignore it.
        nvm3_tracePrint(TRACE_LEVEL_COUNTER, "      cntRd: idx=%2zu, base=INVALID.\n", idx);
      }
      // Skip the base value increments.
      idx += counterNumberOfIncPrBase(h);
    } else {
      // Index is pointing to an increment
      bool isAValidNewInc;
      uint32_t incField;

      isAValidNewInc = counterIsValidNewInc(h, obj, idx, &incField);
      if (isAValidNewInc) {
        // Handle only valid increments
        counterBase += incField;
        nvm3_tracePrint(TRACE_LEVEL_COUNTER, "      cntRd: idx=%2zu, incr=%u=0x%04x, val=%u.\n", idx, incField, incField, counterBase);
      } else {
        nvm3_tracePrint(TRACE_LEVEL_COUNTER, "      cntRd: idx=%2zu, incr=INVALID.\n", idx);
      }
    }
    idx++;
  }

  nvm3_tracePrint(TRACE_LEVEL_COUNTER, "      cntRd: val=%u.\n", counterBase);

  return counterBase;
}

static uint8_t getCounterFreeIncrementIdx(nvm3_Handle_t *h, nvm3_Obj_t *obj)
{
  uint8_t freeIncIdx = (uint8_t)counterMaxNumberOfInc(h);
  uint32_t incField;
  size_t idx = 0;

  while (idx < counterMaxNumberOfInc(h)) {
    counterIncFldGet(h, obj, idx, &incField);
    if (h->halInfo.writeSize == NVM3_HAL_WRITE_SIZE_16) {
      if (incField == 0xFFFFU) {
        freeIncIdx = (uint8_t)idx;
        break;
      } else if ((incField & 0xFFU) == 0U) {
        idx += 2U;
      } else {
        // Do nothing.
      }
    } else {
      if (incField == 0xFFFFFFFFU) {
        freeIncIdx = (uint8_t) idx;
        break;
      } else if ((incField & 0xFFFFU) == 0U) {
        idx += 1U;
      } else {
        // Do nothing.
      }
    }
    idx++;
  }

  return freeIncIdx;
}

#ifdef NVM3_SUPPORT_ENCRYPTION
static uint32_t getCounterNonce(nvm3_Handle_t *h, nvm3_Obj_t *obj)
{
  uint32_t nonce;
  // Works only when the nonce offset is 0
  nvm3_HalPtr_t adr = calcAdr(obj->objAdr, NVM3_OBJ_HEADER_SIZE_COUNTER + COUNTER_OFFS_NONCE_W);

  nvm3_halReadWords(HAL, adr, &nonce, 1);
  return nonce;
}
#endif

static Ecode_t counterUpdateInc(nvm3_Handle_t *h, nvm3_Obj_t *obj, size_t idx, uint32_t inc)
{
  uint8_t BCCB = 0;
  uint32_t incField;
  Ecode_t sta;

  // Calculate inc value.
  if (h->halInfo.writeSize == NVM3_HAL_WRITE_SIZE_16) {
    nvm3_utilsComputeBergerCode(&BCCB, &inc, 8);
    incField = (((uint32_t)BCCB << 8) | inc);
  } else {
    nvm3_utilsComputeBergerCode(&BCCB, &inc, 16);
    incField = (((uint32_t)BCCB << 16) | inc);
  }

  // Write inc value.
  sta = counterIncFldSet(h, obj, idx, incField);

  return sta;
}

static Ecode_t counterUpdateBase(nvm3_Handle_t *h, nvm3_Obj_t *obj, uint8_t idx, uint32_t base)
{
  uint8_t BCCB;
  uint32_t incField;
  Ecode_t sta;

  // Calculate inc value = 0 + Berger code.
  if (h->halInfo.writeSize == NVM3_HAL_WRITE_SIZE_16) {
    BCCB = 8;
    nvm3_utilsComputeBergerCode(&BCCB, &base, 32);
    incField = (((uint32_t)BCCB << 8));
  } else {
    BCCB = 16;
    nvm3_utilsComputeBergerCode(&BCCB, &base, 32);
    incField = (((uint32_t)BCCB << 16));
  }

  // Write the inc value.
  nvm3_tracePrint(TRACE_LEVEL_COUNTER, "      cntUpdBase: inc=%u=0x%4x.\n", incField, incField);
  sta = counterIncFldSet(h, obj, idx, incField);
  idx++;

  // Write the base value.
  if (h->halInfo.writeSize == NVM3_HAL_WRITE_SIZE_16) {
    if (sta == ECODE_NVM3_OK) {
      nvm3_tracePrint(TRACE_LEVEL_COUNTER, "      cntUpdBase: baseLsb=%u.\n", (base & 0xFFFF));
      sta = counterIncFldSet(h, obj, idx, (base & 0xFFFFU));
      idx++;
      nvm3_tracePrint(TRACE_LEVEL_COUNTER, "      cntUpdBase: sta=0x%x.\n", sta);
    }
    if (sta == ECODE_NVM3_OK) {
      nvm3_tracePrint(TRACE_LEVEL_COUNTER, "      cntUpdBase: baseMsb=%u.\n", (base >> 16));
      sta = counterIncFldSet(h, obj, idx, (base >> 16));
      nvm3_tracePrint(TRACE_LEVEL_COUNTER, "      cntUpdBase: sta=0x%x.\n", sta);
    }
  } else {
    if (sta == ECODE_NVM3_OK) {
      nvm3_tracePrint(TRACE_LEVEL_COUNTER, "      cntUpdBase: base=%u.\n", base);
      sta = counterIncFldSet(h, obj, idx, base);
      nvm3_tracePrint(TRACE_LEVEL_COUNTER, "      cntUpdBase: sta=0x%x.\n", sta);
    }
  }

  return sta;
}

/* Update the object counter. */
static Ecode_t fifoUpdateCounter(nvm3_Handle_t *h, nvm3_Obj_t *obj,
                                 uint32_t cntNew, uint32_t cntOld)
{
  Ecode_t sta = ECODE_NVM3_OK;
  bool hasBeenUpdated = false;
  uint32_t cntDif;
  uint8_t idx;
  uint8_t incNeeded;

  /* Find out how many increment fields are needed to update the counter.
       It's fine if the new counter is smaller than the old counter, this will
       trigger the creation of a new counter base. */
  cntDif = cntNew - cntOld;

  incNeeded = 1;
  if (cntDif > counterMaxIncVal(h)) {
    // Need room for a new base value.
    incNeeded += (uint8_t)counterNumberOfIncPrBase(h);
  }

  nvm3_tracePrint(TRACE_LEVEL_COUNTER, "      cntUpd: old=%u, new=%u, dif=%u, incNeeded=%u.\n", cntOld, cntNew, cntDif, incNeeded);

  // Find out how many increments are left.
  idx = getCounterFreeIncrementIdx(h, obj);

  if ((counterMaxNumberOfInc(h) - idx) >= incNeeded) {
    // Update the existing counter.
    if (incNeeded == 1U) {
      nvm3_tracePrint(TRACE_LEVEL_COUNTER, "      cntUpd: inc - idx=%u, cntDif=%u.\n", idx, cntDif);
      sta = counterUpdateInc(h, obj, idx, cntDif);
    } else {
      nvm3_tracePrint(TRACE_LEVEL_COUNTER, "      cntUpd: base - idx=%u, cntNew=%u.\n", idx, cntNew);
#ifdef NVM3_SUPPORT_ENCRYPTION
      if (h->encrypted) {
        uint32_t nonce = getCounterNonce(h, obj);
        cntNew = counterEncrypt(h, nonce, cntNew);
      }
#endif
      sta = counterUpdateBase(h, obj, idx, cntNew);
    }
    hasBeenUpdated = (sta == ECODE_NVM3_OK);
  }

  if (!hasBeenUpdated) {
    sta = fifoWriteWrapper(h, obj->key, &cntNew, COUNTER_SIZE, objGroupCounter);
  }

  return sta;
}

static bool cacheUpdateCallback(nvm3_Handle_t *h, nvm3_ObjPtr_t objPtr, nvm3_ObjGroup_t objGroup, void *user)
{
  // By scanning the FIFO from the oldest to the newest object, information
  // from newer objects will replace information from the older. This will
  // ensure that the cache contains valid information.
  nvm3_cacheSet(&h->cache, objPtr->key, objPtr->objAdr, objGroup);
  (void)user;

  return true;
}

static void cacheUpdate(nvm3_Handle_t *h)
{
  nvm3_cacheClear(&h->cache);
  fifoScan(h, fifoScanAll, cacheUpdateCallback, NULL);
}

static Ecode_t initialize(nvm3_Handle_t *h, uint32_t newCfgEraseCnt)
{
  size_t validCnt;
  size_t unusedCnt;
  Ecode_t sta;
  nvm3_HalPtr_t pageAdr;
  nvm3_PageHdr_t pageHdr;
  uint32_t eraseCnt;

  nvm3_tracePrint(TRACE_LEVEL_INIT, "  initialize: begin\n");

  h->fifoFirstIdx = NVM3_PAGE_INDEX_INVALID;
  h->fifoFirstObj = NVM3_OBJ_PTR_INVALID;
  h->fifoNextObj = NVM3_OBJ_PTR_INVALID;
  h->validNvmPageCnt = 0;
  h->unusedNvmSize = 0;

  nvm3_cacheClear(&h->cache);

  // Search for valid pages.
  validCnt = findValidPageCnt(h);
  if (validCnt > 0U) {
    // At least one valid page is found.
    nvm3_tracePrint(TRACE_LEVEL_INIT, "  initialize: validCnt=%zu, eraseEipAndInvalidPages.\n", validCnt);
    eraseEipAndInvalidPages(h);
    validCnt = findValidPageCnt(h);
  }

  if (validCnt == 0U) {
    // No valid pages are found, format the NVM.
    nvm3_tracePrint(TRACE_LEVEL_INIT, "  initialize: validCnt=%zu, eraseAllPages.\n", validCnt);
    eraseAllPages(h, newCfgEraseCnt);
    validCnt = findValidPageCnt(h);
  }

  if (validCnt > 0U) {
    nvm3_tracePrint(TRACE_LEVEL_INIT, "  initialize: validCnt=%zu, Find all NVM content.\n", validCnt);
    findFirstPage(h);
    if (h->fifoFirstIdx != NVM3_PAGE_INDEX_INVALID) {
      pageAdr = pageAdrFromIdx(h, h->fifoFirstIdx);
      nvm3_halReadWords(HAL, pageAdr, &pageHdr, NVM3_PAGE_HEADER_WSIZE);
      eraseCnt = nvm3_pageGetEraseCnt(&pageHdr);
      nvm3_tracePrint(TRACE_LEVEL_INIT, "  initialize: -> first page=%zu, eraseCnt=%u.\n", h->fifoFirstIdx, eraseCnt);
      findFirstObj(h);
      nvm3_tracePrint(TRACE_LEVEL_INIT, "  initialize: -> first object=%p.\n", h->fifoFirstObj);
      findNextObj(h);
      nvm3_tracePrint(TRACE_LEVEL_INIT, "  initialize: -> next object=%p.\n", h->fifoNextObj);

      pageAdr = pageAdrFromIdx(h, pageIdxFromAdr(h, h->fifoNextObj));
      nvm3_tracePrint(TRACE_LEVEL_INIT, "  initialize: -> next object: first=%p, next=%p, idx=%zu.\n", nvm3_pageGetFirstObj(pageAdr), h->fifoNextObj, pageIdxFromAdr(h, h->fifoNextObj));

      size_t needCnt = pagesRepack(h);
      unusedCnt = findNumberOfUnusedPages(h);
      nvm3_tracePrint(TRACE_LEVEL_INIT, "  initialize: -> the number of pages unusedCnt=%zu, neededCnt=%zu.\n", unusedCnt, needCnt);
      if (unusedCnt < needCnt) {
        size_t pageIdx;

        nvm3_tracePrint(TRACE_LEVEL_INIT, "  initialize: ===============================\n");
        pageIdx = getPreviousGoodPage(h, h->fifoFirstIdx);
        for (size_t i = 0; i < needCnt; i++) {
          if (i >= unusedCnt) {
            pageAdr = pageAdrFromIdx(h, pageIdx);
            nvm3_halReadWords(HAL, pageAdr, &pageHdr, NVM3_PAGE_HEADER_WSIZE);
            eraseCnt = nvm3_pageGetEraseCnt(&pageHdr);
            nvm3_tracePrint(TRACE_LEVEL_INIT, "  initialize: -> erase idx=%zu, eraseCnt=%u.\n", pageIdx, eraseCnt);
            (void)nvm3_pageErase(HAL, pageAdr, eraseCnt, &h->halInfo, h->encrypted);
          }
          pageIdx = getPreviousGoodPage(h, pageIdx);
        }
        nvm3_tracePrint(TRACE_LEVEL_INIT, "  initialize: ===============================\n");
        return ECODE_NVM3_ERR_INIT_WITH_FULL_NVM;
      }
      sta = ECODE_NVM3_OK;
    } else {
      sta = ECODE_NVM3_ERR_ERASE_COUNT_ERROR;
    }
  } else {
    sta = ECODE_NVM3_ERR_NO_VALID_PAGES;
  }

  if (sta == ECODE_NVM3_OK) {
    cacheUpdate(h);
  }

#if NVM3_TRACE_ENABLED
  size_t nextIdx = pageIdxFromAdr(h, h->fifoNextObj);
#endif
  unusedCnt = findNumberOfUnusedPages(h);
  nvm3_tracePrint(TRACE_LEVEL_INIT, "  initialize: end, unusedNvmSize=%zu, unusedNvmPages=%zu, validNvmPages=%zu\n", h->unusedNvmSize, unusedCnt, h->validNvmPageCnt);
  nvm3_tracePrint(TRACE_LEVEL_INIT, "  initialize: end, firstIdx=%zu, firstAdr=%p, nextIdx=%zu, nextAdr=%p\n", h->fifoFirstIdx, h->fifoFirstObj, nextIdx, h->fifoNextObj);

  return sta;
}

static void workBegin(nvm3_Handle_t *h, nvm3_HalNvmAccessCode_t access)
{
  (void)h;
  nvm3_lockBegin();
  nvm3_halNvmAccess(HAL, access);
}

static void workEnd(nvm3_Handle_t *h)
{
  (void)h;
  nvm3_halNvmAccess(HAL, NVM3_HAL_NVM_ACCESS_NONE);
  nvm3_lockEnd();
}

//****************************************************************************
// Global functions

Ecode_t nvm3_open(nvm3_Handle_t *h, const nvm3_Init_t *i)
{
  Ecode_t sta;
  uint32_t newCfgEraseCnt = cfgEraseCnt;
  cfgEraseCnt = 0;

  if (h == NULL) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_PARAMETER;
  }
  if (h->hasBeenOpened) {
    nvm3_tracePrint(TRACE_LEVEL_INIT, "nvm3_open: Has already been opened.\n");
    if ((i->nvmAdr == h->nvmAdr)
        && (i->nvmSize == h->nvmSize)
        && (i->cachePtr == h->cache.entryPtr)
        && (i->cacheEntryCount == h->cache.entryCount)
        && (i->maxObjectSize == h->maxObjectSize)
        && (i->halHandle == h->halHandle)
#if defined(NVM3_SUPPORT_ENCRYPTION)
        && (i->cryptoKey == h->cryptoKey)
#endif
        ) {
      h->repackHeadroom = i->repackHeadroom;
      return ECODE_NVM3_OK;
    } else {
      return ECODE_NVM3_ERR_OPENED_WITH_OTHER_PARAMETERS;
    }
  }

  size_t minUnused = h->minUnused;
  (void)memset(h, 0, sizeof(nvm3_Handle_t));
  h->minUnused      = minUnused;
  h->nvmAdr         = i->nvmAdr;
  h->nvmSize        = i->nvmSize;
  h->maxObjectSize  = i->maxObjectSize;
  h->repackHeadroom = i->repackHeadroom;
  nvm3_cacheOpen(&h->cache, i->cachePtr, i->cacheEntryCount);
  h->halHandle       = i->halHandle;
  sta = nvm3_halGetInfo(i->halHandle, &h->halInfo);
  if ((sta == ECODE_NVM3_OK) && (h->halInfo.pageSize > 0)) {
    h->totalNvmPageCnt = (h->nvmSize / h->halInfo.pageSize);
  } else {
    h->totalNvmPageCnt = 0U;
  }
#ifdef NVM3_SUPPORT_ENCRYPTION
  h->encrypted = (h->halInfo.memoryMapped == 0);
  h->cryptoKey = i->cryptoKey;
#if defined(NVM3_USE_ZIGBEECRYPTO)
  h->getRndNumber = i->getRndNumber;
#endif
#else
  if (h->halInfo.memoryMapped == 0) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_ENCRYPTION_NOT_SUPPORTED;
  }
  h->encrypted = false;
#endif

  nvm3_tracePrint(TRACE_LEVEL_INIT, "nvm3_open: nvmAdr=%p, pageSize=%zu, pageCount=%zu, totalSize=0x%zx=%zu, writeSize=%u.\n",
                    i->nvmAdr, h->halInfo.pageSize, i->nvmSize / h->halInfo.pageSize, i->nvmSize, i->nvmSize,
                    (h->halInfo.writeSize == NVM3_HAL_WRITE_SIZE_32) ? 32 : 16);
  nvm3_tracePrint(TRACE_LEVEL_INIT, "nvm3_open:  maxObjSize=%zu.\n", h->maxObjectSize);
  nvm3_tracePrint(TRACE_LEVEL_INIT, "nvm3_open:   encrypted=%u.\n", h->encrypted ? 1 : 0);
  nvm3_tracePrint(TRACE_LEVEL_INIT, "nvm3_open:   repackThr=%zu.\n", thrRepack(h));
  nvm3_tracePrint(TRACE_LEVEL_INIT, "nvm3_open:     hardThr=%zu.\n", thrHard(h, h->maxObjectSize));
  nvm3_tracePrint(TRACE_LEVEL_INIT, "nvm3_open:    softMThr=%zu.\n", thrSoftMinimum(h));
  nvm3_tracePrint(TRACE_LEVEL_INIT, "nvm3_open:    softUThr=%zu.\n", thrSoftUser(h));
  nvm3_tracePrint(TRACE_LEVEL_INIT, "nvm3_open:     sizeThr=%zu.\n", thrSize(h));

  nvm3_tracePrint(TRACE_LEVEL_INIT, "nvm3_open: repackPages=%zu.\n", pagesRepack(h));

  nvm3_tracePrint(TRACE_LEVEL_INIT, "nvm3_open: fragmentCnt=%u.\n", nvm3_maxFragmentCount);
  nvm3_tracePrint(TRACE_LEVEL_INIT, "nvm3_open: sizeof(ObjFragDetail_t)=%zu, sizeof(ObjFrag_t)=%zu, sizeof(Obj_t)=%zu, nvm3_objHandleSize=%zu.\n",
                  sizeof(nvm3_ObjFragDetail_t), sizeof(nvm3_ObjFrag_t), sizeof(nvm3_Obj_t), nvm3_objHandleSize);

  nvm3_tracePrint(TRACE_LEVEL_INIT, "nvm3_open:   maxObj->n=%zu.\n", OBJ_PAGES_REQ(h->halInfo.pageSize, h->maxObjectSize));

  // Check for valid device information
  if ((h->halInfo.pageSize == 0U) || (h->totalNvmPageCnt == 0)) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_PAGE_SIZE_NOT_SUPPORTED;
  }
  // Check address for alignment.
  if (((size_t)h->nvmAdr % h->halInfo.pageSize) != 0U) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_ALIGNMENT_INVALID;
  }
  // Check size for alignment.
  if ((h->nvmSize % h->halInfo.pageSize) != 0U) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_ALIGNMENT_INVALID;
  }
  // Check minimum size, initial version.
  if (h->nvmSize < thrSize(h)) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_SIZE_TOO_SMALL;
  }
  // Check that the maximum object size is supported.
  if (h->maxObjectSize > NVM3_MAX_OBJECT_SIZE_HIGH_LIMIT) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_OBJECT_SIZE_NOT_SUPPORTED;
  }
  // Check that the minimum object size is supported.
  if (h->maxObjectSize < NVM3_MAX_OBJECT_SIZE_LOW_LIMIT) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_OBJECT_SIZE_NOT_SUPPORTED;
  }
  // Check that the page size is supported.
  if (h->halInfo.pageSize < NVM3_MIN_PAGE_SIZE) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_PAGE_SIZE_NOT_SUPPORTED;
  }

#if defined(NVM3_SUPPORT_ENCRYPTION)
  if (h->cryptoKey == NULL) {
    return ECODE_NVM3_ERR_ENCRYPTION_KEY_ERROR;
  }

#if defined(NVM3_USE_MBEDTLS)
  int mbedtlsSta;

  // Random generator init
  static const char personalStr[] = "NVM3 personal string.";
  mbedtls_entropy_init(&h->entropyCtx);
  mbedtlsSta = mbedtls_ctr_drbg_seed(&h->drbgCtx, mbedtls_entropy_func, &h->entropyCtx, (const unsigned char *)personalStr, sizeof(personalStr));
  if (mbedtlsSta != 0) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_ENCRYPTION_INIT;
  }

  // CCM init
  mbedtls_ccm_init(&h->ccmCtx);
  mbedtlsSta = mbedtls_ccm_setkey(&h->ccmCtx, MBEDTLS_CIPHER_ID_AES, h->cryptoKey, 128);
  if (mbedtlsSta != 0) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_ENCRYPTION_INIT;
  }

  // AES init
  mbedtls_aes_init(&h->aesCtx);
#endif // End NVM3_USE_MBEDTLS

#if defined(NVM3_USE_ZIGBEECRYPTO)
  if (h->getRndNumber == NULL) {
    return ECODE_NVM3_ERR_PARAMETER;
  }
#endif // End NVM3_USE_ZIGBEECRYPTO

#endif  // End NVM3_SUPPORT_ENCRYPTION

  workBegin(h, NVM3_HAL_NVM_ACCESS_NOP);
  sta = nvm3_halOpen(HAL, h->nvmAdr, h->nvmSize);
  nvm3_halNvmAccess(HAL, NVM3_HAL_NVM_ACCESS_RDWR);

  if (sta == ECODE_NVM3_OK) {
    sta = initialize(h, newCfgEraseCnt);
    if (sta == ECODE_NVM3_ERR_INIT_WITH_FULL_NVM) {
      sta = initialize(h, newCfgEraseCnt);
    }
  }
  // Check minimum size, final version.
  size_t pageCapacity = h->halInfo.pageSize - NVM3_PAGE_HEADER_SIZE;
  if ((sta == ECODE_NVM3_OK) && ((h->validNvmPageCnt * pageCapacity) < thrSize(h))) {
    NVM3_ERROR_ASSERT();
    sta = ECODE_NVM3_ERR_SIZE_TOO_SMALL;
  }
  h->hasBeenOpened = (sta == ECODE_NVM3_OK);

#ifdef NVM3_HOST_BUILD
#if 0
  // Test code - begin
  if (sta == ECODE_NVM3_OK) {
    // Do repacks if needed.
    // This is to force excessive NVM usage during repacks if power failure.
    bool repackNeeded = nvm3_repackNeeded(h);
    if (repackNeeded) {
      nvm3_tracePrint(TRACE_LEVEL_INIT, "nvm3_open: repack - begin, free=%u.\n", h->unusedNvmSize);
      sta = repackUntilGood(h);
      nvm3_tracePrint(TRACE_LEVEL_INIT, "nvm3_open: repack -   end, free=%u, status=%x.\n", h->unusedNvmSize, sta);
    }
  }
  // Test code - end
#endif
#endif

  nvm3_tracePrint(TRACE_LEVEL_INIT, "nvm3_open: firstPage=%zu, pageCnt=%zu, unusedNvmSize=%zu, sta=0x%x.\n", h->fifoFirstIdx, h->totalNvmPageCnt, h->unusedNvmSize, sta);
  workEnd(h);

  return sta;
}

Ecode_t nvm3_close(nvm3_Handle_t *h)
{
  if (h == NULL) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_PARAMETER;
  }
  if (!h->hasBeenOpened) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_NOT_OPENED;
  }

  nvm3_tracePrint(TRACE_LEVEL_INIT, "nvm3_close.\n");
  h->hasBeenOpened = false;
  nvm3_halClose(HAL);
  return ECODE_NVM3_OK;
}

Ecode_t nvm3_writeData(nvm3_Handle_t *h, nvm3_ObjectKey_t key, const void *value, size_t len)
{
  Ecode_t sta;
  nvm3_ObjGroup_t objGroup;
  bool write = true;

  if (h == NULL) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_PARAMETER;
  }
  if (!h->hasBeenOpened) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_NOT_OPENED;
  }
  if (!keyIsValid(key)) {
    return ECODE_NVM3_ERR_KEY_INVALID;
  }

  workBegin(h, NVM3_HAL_NVM_ACCESS_RDWR);
  nvm3_tracePrint(TRACE_LEVEL_INFO, "nvm3_writeData: key=%u, len=%zu.\n", key, len);

  sta = findObj(h, key, pObjA, &objGroup);
  if (sta == ECODE_NVM3_OK) {
    if ((objGroup == objGroupData) && (pObjA->totalLen == len)) {
      sta = fifoReadObj(h, (void *)value, len, pObjA, read_compare);
      write = (sta != ECODE_NVM3_OK);
    }
  }
  if (write) {
    sta = fifoWriteWrapper(h, key, value, len, objGroupData);
  }

  nvm3_tracePrint(TRACE_LEVEL_INFO, "nvm3_writeData: free=%zu, nextAdr=%p.\n", h->unusedNvmSize, h->fifoNextObj);
  workEnd(h);

  return sta;
}

Ecode_t nvm3_readData(nvm3_Handle_t *h, nvm3_ObjectKey_t key, void *value, size_t maxLen)
{
  Ecode_t sta;
  nvm3_ObjGroup_t objGroup;
  NVM3_OBJ_T_ALLOCATION(ObjA);

  if (h == NULL) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_PARAMETER;
  }
  if (!h->hasBeenOpened) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_NOT_OPENED;
  }
  if (!keyIsValid(key)) {
    return ECODE_NVM3_ERR_KEY_INVALID;
  }

  workBegin(h, NVM3_HAL_NVM_ACCESS_RD);
  nvm3_tracePrint(TRACE_LEVEL_INFO, "nvm3_readData: key=%u, len=%zu.\n", key, maxLen);

  sta = findObj(h, key, pObjA, &objGroup);
  if (sta == ECODE_NVM3_OK) {
    if (objGroup == objGroupData) {
      sta = fifoReadObj(h, value, maxLen, pObjA, read_data);
    } else if (objGroup == objGroupCounter) {
      sta = ECODE_NVM3_ERR_OBJECT_IS_NOT_DATA;
    } else {
      sta = ECODE_NVM3_ERR_KEY_NOT_FOUND;
    }
  }

  workEnd(h);

  return sta;
}

Ecode_t nvm3_writeCounter(nvm3_Handle_t *h, nvm3_ObjectKey_t key, uint32_t value)
{
  Ecode_t sta;
  uint32_t oldCntVal;
  nvm3_ObjGroup_t objGroup;
  NVM3_OBJ_T_ALLOCATION(ObjA);

  if (h == NULL) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_PARAMETER;
  }
  if (!h->hasBeenOpened) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_NOT_OPENED;
  }
  if (!keyIsValid(key)) {
    return ECODE_NVM3_ERR_KEY_INVALID;
  }

  workBegin(h, NVM3_HAL_NVM_ACCESS_RDWR);
  nvm3_tracePrint(TRACE_LEVEL_COUNTER, "nvm3_writeCounter, key=%u, value=%u.\n", key, value);

  sta = findObj(h, key, pObjA, &objGroup);
  if ((sta != ECODE_NVM3_OK)
      || ((sta == ECODE_NVM3_OK) && (objGroup == objGroupDeleted))) {
    // No counter found (or in deleted state), create new counter.
    sta = fifoWriteWrapper(h, key, &value, COUNTER_SIZE, objGroupCounter);
  } else {
    // A counter object was found.
    if (objGroup == objGroupCounter) {
      // Check old counter value.
      oldCntVal = readCounter(h, pObjA);

      // Do nothing if there is no change in counter value.
      // Otherwise, update the counter.
      if (value == oldCntVal) {
        sta = ECODE_NVM3_OK;
      } else {
        sta = fifoUpdateCounter(h, pObjA, value, oldCntVal);
      }
    } else {
      // An object already exist and it is not a counter.
      sta = ECODE_NVM3_ERR_OBJECT_IS_NOT_A_COUNTER;
    }
  }

  workEnd(h);

  return sta;
}

Ecode_t nvm3_readCounter(nvm3_Handle_t *h, nvm3_ObjectKey_t key, uint32_t *value)
{
  Ecode_t sta;
  nvm3_ObjGroup_t objGroup;
  NVM3_OBJ_T_ALLOCATION(ObjA);

  if (h == NULL) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_PARAMETER;
  }
  if (!h->hasBeenOpened) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_NOT_OPENED;
  }
  if (!keyIsValid(key)) {
    return ECODE_NVM3_ERR_KEY_INVALID;
  }

  workBegin(h, NVM3_HAL_NVM_ACCESS_RD);
  nvm3_tracePrint(TRACE_LEVEL_COUNTER, "nvm3_readCounter: key=%u.\n", key);

  sta = findObj(h, key, pObjA, &objGroup);
  if ((sta == ECODE_NVM3_OK) && (objGroup != objGroupDeleted)) {
    if (objGroup == objGroupCounter) {
      *value = readCounter(h, pObjA);
    } else {
      sta = ECODE_NVM3_ERR_OBJECT_IS_NOT_A_COUNTER;
    }
  } else {
    sta = ECODE_NVM3_ERR_KEY_NOT_FOUND;
  }

  workEnd(h);

  return sta;
}

Ecode_t nvm3_incrementCounter(nvm3_Handle_t *h, nvm3_ObjectKey_t key, uint32_t *value)
{
  Ecode_t sta;
  uint32_t cntVal;
  nvm3_ObjGroup_t objGroup;
  NVM3_OBJ_T_ALLOCATION(ObjA);

  if (h == NULL) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_PARAMETER;
  }
  if (!h->hasBeenOpened) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_NOT_OPENED;
  }
  if (!keyIsValid(key)) {
    return ECODE_NVM3_ERR_KEY_INVALID;
  }

  workBegin(h, NVM3_HAL_NVM_ACCESS_RDWR);
  nvm3_tracePrint(TRACE_LEVEL_COUNTER, "nvm3_incrementCounter: key=%u.\n", key);

  sta = findObj(h, key, pObjA, &objGroup);
  if ((sta == ECODE_NVM3_OK) && (objGroup != objGroupDeleted)) {
    // Read the old counter value.
    if (objGroup == objGroupCounter) {
      cntVal = readCounter(h, pObjA);
      sta = fifoUpdateCounter(h, pObjA, cntVal + 1U, cntVal);
      if (value != NULL) {
        *value = cntVal + 1U;
      }
    } else {
      sta = ECODE_NVM3_ERR_OBJECT_IS_NOT_A_COUNTER;
    }
  } else {
    // The key is either not found or the object is scheduled to be deleted.
    sta = ECODE_NVM3_ERR_KEY_NOT_FOUND;
  }

  workEnd(h);

  return sta;
}

Ecode_t nvm3_getObjectInfo(nvm3_Handle_t *h, nvm3_ObjectKey_t key, uint32_t *type, size_t *len)
{
  Ecode_t sta;
  *len = 0;
  nvm3_ObjGroup_t objGroup;
  NVM3_OBJ_T_ALLOCATION(ObjA);

  if (h == NULL) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_PARAMETER;
  }
  if (!h->hasBeenOpened) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_NOT_OPENED;
  }
  if (!keyIsValid(key)) {
    return ECODE_NVM3_ERR_KEY_INVALID;
  }

  workBegin(h, NVM3_HAL_NVM_ACCESS_RD);
  nvm3_tracePrint(TRACE_LEVEL_INFO, "nvm3_getObjectInfo: key=%u.\n", key);

  sta = findObj(h, key, pObjA, &objGroup);
  if ((sta == ECODE_NVM3_OK) && (objGroup != objGroupDeleted)) {
    if (objGroup == objGroupCounter) {
      *type = NVM3_OBJECTTYPE_COUNTER;
      *len = sizeof(uint32_t);
    } else {
      *type = NVM3_OBJECTTYPE_DATA;
#ifdef NVM3_SUPPORT_ENCRYPTION
      if (h->encrypted) {
        *len = pObjA->totalLen - (CCM_SIZE_NONCE + CCM_SIZE_TAG);
      } else {
#endif
      *len = pObjA->totalLen;
#ifdef NVM3_SUPPORT_ENCRYPTION
    }
#endif
    }
  } else {
    sta = ECODE_NVM3_ERR_KEY_NOT_FOUND;
  }

  workEnd(h);

  return sta;
}

typedef struct {
  nvm3_ObjectKey_t  keyMin;
  nvm3_ObjectKey_t  keyMax;
  nvm3_ObjectKey_t  *keyListPtr;
  size_t            keyListSize;
  size_t            keyListIdx;
  size_t            keyTotalCnt;
  bool              lookForDeleted;
} ScanEnum_t;

static void enumScanCacheCallback_t(nvm3_Cache_t *h, nvm3_ObjectKey_t key, nvm3_ObjGroup_t group, nvm3_ObjPtr_t obj, void *user)
{
  ScanEnum_t *scanEnum = user;

  (void)h;
  (void)obj;
  if (((bool)(group == objGroupDeleted) == scanEnum->lookForDeleted) && (key >= scanEnum->keyMin) && (key <= scanEnum->keyMax)) {
    scanEnum->keyTotalCnt++;
    if (scanEnum->keyListIdx < scanEnum->keyListSize) {
      scanEnum->keyListPtr[scanEnum->keyListIdx] = key;
      scanEnum->keyListIdx++;
    }
  }
}

static bool enumScanFifoCallback(nvm3_Handle_t *h, nvm3_ObjPtr_t obj, nvm3_ObjGroup_t group, void *user)
{
  ScanEnum_t *scanEnum = user;
  nvm3_ObjGroup_t objFindGroup;
  Ecode_t sta;
  NVM3_OBJ_T_ALLOCATION(ObjB);

  if (((bool)(group == objGroupDeleted) == scanEnum->lookForDeleted) && (obj->key >= scanEnum->keyMin) && (obj->key <= scanEnum->keyMax)) {
    objBegin(pObjB);
    sta = findObj(h, obj->key, pObjB, &objFindGroup);
    if ((sta == ECODE_NVM3_OK) && (objFindGroup != objGroupDeleted) && (pObjB->objAdr == obj->objAdr)) {
      scanEnum->keyTotalCnt++;
      if (scanEnum->keyListIdx < scanEnum->keyListSize) {
        scanEnum->keyListPtr[scanEnum->keyListIdx] = obj->key;
        scanEnum->keyListIdx++;
      }
    }
    objEnd(pObjB);
  }

  return true;
}

static size_t enumObjects(nvm3_Handle_t *h,
                          nvm3_ObjectKey_t *keyListPtr,
                          size_t keyListSize,
                          nvm3_ObjectKey_t keyMin,
                          nvm3_ObjectKey_t keyMax,
                          bool lookForDeleted)
{
  size_t count = 0;
  ScanEnum_t scanEnum;

  if (h == NULL) {
    NVM3_ERROR_ASSERT();
    return 0;
  }
  if (!h->hasBeenOpened) {
    NVM3_ERROR_ASSERT();
    return 0;
  }

  workBegin(h, NVM3_HAL_NVM_ACCESS_RD);
  nvm3_tracePrint(TRACE_LEVEL_INFO, "nvm3_enumObjects, size=%zu.\n", keyListSize);

  scanEnum.keyMin = keyMin;
  scanEnum.keyMax = keyMax;
  scanEnum.keyListPtr = keyListPtr;
  scanEnum.keyListSize = keyListSize;
  scanEnum.keyListIdx = 0;
  scanEnum.keyTotalCnt = 0;
  scanEnum.lookForDeleted = lookForDeleted;
  if (!h->cache.overflow) {
    nvm3_cacheScan(&h->cache, enumScanCacheCallback_t, &scanEnum);
  } else {
    fifoScan(h, fifoScanAll, enumScanFifoCallback, &scanEnum);
  }
  count = (keyListSize == 0) ? scanEnum.keyTotalCnt : scanEnum.keyListIdx;

  nvm3_tracePrint(TRACE_LEVEL_INFO, "nvm3_enumObjects, count=%zu.\n", count);
  workEnd(h);

  return count;
}

size_t nvm3_enumObjects(nvm3_Handle_t *h,
                        nvm3_ObjectKey_t *keyListPtr,
                        size_t keyListSize,
                        nvm3_ObjectKey_t keyMin,
                        nvm3_ObjectKey_t keyMax)
{
  return enumObjects(h, keyListPtr, keyListSize, keyMin, keyMax, false);
}

size_t nvm3_enumDeletedObjects(nvm3_Handle_t *h,
                               nvm3_ObjectKey_t *keyListPtr,
                               size_t keyListSize,
                               nvm3_ObjectKey_t keyMin,
                               nvm3_ObjectKey_t keyMax)
{
  return enumObjects(h, keyListPtr, keyListSize, keyMin, keyMax, true);
}

Ecode_t nvm3_deleteObject(nvm3_Handle_t *h, nvm3_ObjectKey_t key)
{
  Ecode_t sta;
  nvm3_ObjGroup_t objGroup;
  NVM3_OBJ_T_ALLOCATION(ObjA);

  if (h == NULL) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_PARAMETER;
  }
  if (!h->hasBeenOpened) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_NOT_OPENED;
  }
  if (!keyIsValid(key)) {
    return ECODE_NVM3_ERR_KEY_INVALID;
  }

  workBegin(h, NVM3_HAL_NVM_ACCESS_RDWR);
  nvm3_tracePrint(TRACE_LEVEL_INFO, "nvm3_deleteObject: key=%u.\n", key);

  sta = findObj(h, key, pObjA, &objGroup);
  if ((sta == ECODE_NVM3_OK) && (objGroup != objGroupDeleted)) {
    sta = fifoWriteWrapper(h, key, NULL, 0, objGroupDeleted);
  } else {
    sta = ECODE_NVM3_ERR_KEY_NOT_FOUND;
  }

  workEnd(h);

  return sta;
}

Ecode_t nvm3_eraseAll(nvm3_Handle_t *h)
{
  Ecode_t sta;

  if (h == NULL) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_PARAMETER;
  }
  if (!h->hasBeenOpened) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_NOT_OPENED;
  }

  workBegin(h, NVM3_HAL_NVM_ACCESS_RDWR);
  nvm3_tracePrint(TRACE_LEVEL_INFO, "nvm3_eraseAll.\n");

  eraseAllPages(h, 0);
  sta = initialize(h, 0);

  workEnd(h);

  return sta;
}

Ecode_t nvm3_getEraseCount(nvm3_Handle_t *h, uint32_t *eraseCnt)
{
  nvm3_HalPtr_t pageAdr;
  nvm3_PageHdr_t pageHdr;

  if (h == NULL) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_PARAMETER;
  }
  if (!h->hasBeenOpened) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_NOT_OPENED;
  }

  workBegin(h, NVM3_HAL_NVM_ACCESS_RD);

  // Read erase counter from the first page.
  pageAdr = pageAdrFromIdx(h, h->fifoFirstIdx);
  nvm3_halReadWords(HAL, pageAdr, &pageHdr, NVM3_PAGE_HEADER_WSIZE);
  *eraseCnt = nvm3_pageGetEraseCnt(&pageHdr);

  nvm3_tracePrint(TRACE_LEVEL_INFO, "nvm3_getEraseCount: cnt=%u.\n", *eraseCnt);
  workEnd(h);

  return ECODE_NVM3_OK;
}

void nvm3_setEraseCount(uint32_t eraseCnt)
{
  cfgEraseCnt = eraseCnt;
}

Ecode_t nvm3_repack(nvm3_Handle_t *h)
{
  bool repackNeeded;
  Ecode_t sta = ECODE_NVM3_OK;

  if (h == NULL) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_PARAMETER;
  }
  if (!h->hasBeenOpened) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_NOT_OPENED;
  }

  workBegin(h, NVM3_HAL_NVM_ACCESS_RDWR);
  nvm3_tracePrint(TRACE_LEVEL_INFO, "nvm3_repack: Begin, unusedNvmSize=%zu, nextObj=%p.\n", h->unusedNvmSize, h->fifoNextObj);

  repackNeeded = !softUserAvailable(h);
  if (repackNeeded) {
    repackOnce(h);
  }

  nvm3_tracePrint(TRACE_LEVEL_INFO, "nvm3_repack: End,   unusedNvmSize=%zu, nextObj=%p.\n", h->unusedNvmSize, h->fifoNextObj);
  workEnd(h);

  return sta;
}

bool nvm3_repackNeeded(nvm3_Handle_t *h)
{
  bool repackNeeded;

  if (h == NULL) {
    NVM3_ERROR_ASSERT();
    return false;
  }
  if (!h->hasBeenOpened) {
    NVM3_ERROR_ASSERT();
    return false;
  }

  workBegin(h, NVM3_HAL_NVM_ACCESS_RD);

  repackNeeded = !softUserAvailable(h);
  nvm3_tracePrint(TRACE_LEVEL_INFO, "nvm3_repackNeeded: free=%zu, result=%s.\n", h->unusedNvmSize, repackNeeded ? "true" : "false");

  workEnd(h);

  return repackNeeded;
}

Ecode_t nvm3_resize(nvm3_Handle_t *h, nvm3_HalPtr_t newAddr, size_t newSize)
{
  Ecode_t sta = ECODE_NVM3_OK;
  bool resizeInProgress = false;
  size_t  needSpaceAtLow = 0;
  size_t  needSpaceAtHigh = 0;
  nvm3_HalPtr_t eraseAddr = 0;
  size_t eraseSize = 0;
  nvm3_Init_t init;

  if (h == NULL) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_PARAMETER;
  }
  if (!h->hasBeenOpened) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_NOT_OPENED;
  }
  // Check new address for alignment.
  if (((size_t)newAddr % h->halInfo.pageSize) != 0U) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_ALIGNMENT_INVALID;
  }
  // Check new size for alignment.
  if ((newSize % h->halInfo.pageSize) != 0U) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_ALIGNMENT_INVALID;
  }
  // Check that new size is large enough.
  if (newSize < thrSize(h)) {
    NVM3_ERROR_ASSERT();
    return ECODE_NVM3_ERR_SIZE_TOO_SMALL;
  }

  nvm3_tracePrint(TRACE_LEVEL_RESIZE, "nvm3_resize: addr=%p->%p, size=%zu->%zu\n", h->nvmAdr, newAddr, h->nvmSize, newSize);

  // Step-1: Prepare NVM for resize
  workBegin(h, NVM3_HAL_NVM_ACCESS_RDWR);

  size_t addrShift = (size_t)newAddr - (size_t)h->nvmAdr;
  int    addrDiff = (int)(addrShift);
  if ((addrDiff > 0) && ((addrShift + newSize) == h->nvmSize)) {
    // Decrease size at low address
    resizeInProgress = true;
    needSpaceAtLow = addrShift;
    eraseAddr = h->nvmAdr;
    eraseSize = addrShift;
  } else if ((addrDiff < 0) && ((addrShift + newSize) == h->nvmSize)) {
    // Increase size at low address
    resizeInProgress = true;
  } else if ((addrDiff == 0) && (newSize < h->nvmSize)) {
    // Decrease size at high address
    resizeInProgress = true;
    needSpaceAtHigh = h->nvmSize - newSize;
    eraseSize = h->nvmSize - newSize;
    eraseAddr = calcAdr(h->nvmAdr, newSize);
  } else if ((addrDiff == 0) && (newSize > h->nvmSize)) {
    // Increase size at high address
    resizeInProgress = true;
  } else {
    sta = ECODE_NVM3_ERR_RESIZE_PARAMETER;
  }

  // Step-2: Move NVM content if necessary
  if (resizeInProgress) {
    // Force repacks until resize conditions are met
    for (size_t i = 0; i < (h->validNvmPageCnt * 4U); i++) {
      size_t nextToHigh = h->nvmSize - ((size_t)h->fifoNextObj - (size_t)h->nvmAdr);
      size_t lowToFirst = (size_t)h->fifoFirstObj - (size_t)h->nvmAdr;
      if ((h->fifoFirstObj > h->fifoNextObj) || (needSpaceAtLow > lowToFirst) || (needSpaceAtHigh > nextToHigh)) {
        nvm3_PageState_t pageState;

        sta = repackWorker(h, &pageState);
        nvm3_tracePrint(TRACE_LEVEL_RESIZE, "nvm3_resize: repackWorker, sta=0x%x, state=%d\n", sta, pageState);
        if (sta != ECODE_NVM3_OK) {
          break;
        }
      } else {
        resizeInProgress = false;
        break;
      }
    }
  }

  if ((sta == ECODE_NVM3_OK) && (resizeInProgress)) {
    sta = ECODE_NVM3_ERR_RESIZE_NOT_ENOUGH_SPACE;
  }

  workEnd(h);

  if (sta != ECODE_NVM3_OK) {
    return sta;
  }

  // Step-3: Close
  (void)nvm3_close(h);

  // Step-4: Erase unused pages
  sta = ECODE_NVM3_OK;
  if (eraseSize > 0) {
    nvm3_HalPtr_t pageAdr;

    sta = nvm3_halOpen(HAL, eraseAddr, eraseSize);
    if (sta == ECODE_NVM3_OK) {
      nvm3_halNvmAccess(HAL, NVM3_HAL_NVM_ACCESS_RDWR);
      for (uint32_t i = 0; i < eraseSize / h->halInfo.pageSize; i++) {
        pageAdr = (nvm3_HalPtr_t)((size_t)eraseAddr + i * h->halInfo.pageSize);
        sta = nvm3_halPageErase(HAL, pageAdr);
        nvm3_tracePrint(TRACE_LEVEL_RESIZE, "nvm3_resize: erasePage, adr=%p, len=%zu, sta=0x%x\n", pageAdr, h->halInfo.pageSize, sta);
        if (sta != ECODE_NVM3_OK) {
          break;
        }
      }
      nvm3_halNvmAccess(HAL, NVM3_HAL_NVM_ACCESS_NONE);
      nvm3_halClose(HAL);
    }
  }

  // Step-5: Re-open
  if (sta == ECODE_NVM3_OK) {
    // Open with new size
    init.nvmAdr = newAddr;
    init.nvmSize = newSize;
  } else {
    // Open with previous size
    init.nvmAdr = h->nvmAdr;
    init.nvmSize = h->nvmSize;
  }
  init.cachePtr = h->cache.entryPtr;
  init.cacheEntryCount = h->cache.entryCount;
  init.maxObjectSize = h->maxObjectSize;
  init.repackHeadroom = h->repackHeadroom;
  init.halHandle = h->halHandle;
  sta = nvm3_open(h, &init);

  nvm3_tracePrint(TRACE_LEVEL_RESIZE, "nvm3_resize: sta=0x%08x\n", sta);
  return sta;
}

/// @endcond
