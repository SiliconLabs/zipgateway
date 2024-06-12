/***************************************************************************//**
 * @file
 * @brief NVM3 page handling functions
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
#include "nvm3_page.h"
#include "nvm3_object.h"
#include "nvm3_utils.h"
#include "nvm3_trace.h"
#include "endian_wrap.h"

/** @cond DO_NOT_INCLUDE_WITH_DOXYGEN */

//============================================================================

#define H1_MAGIC_SHIFT          16          // The magic shift
#define H1_MAGIC_V1             0xB29AU     // The magic marker
#define H1_VERSION_V1           0x01U       // The first version of NVM3
#define H4_BAD_MASK             0xffff0000U // The BAD mask
#define H4_BAD_SHIFT            16          // The BAD shift
#define H4_BAD_GOOD             0xFFFFU     // The page is good
#define H4_BAD_NOTGOOD          0x0000U     // The page is not good
#define H4_EIP_MASK             0xffffU     // The EIP mask
#define H4_EIP_SET              0xA5A5U     // The page is scheduled for erase
#define H4_EIP_CLEAR            0xFFFFU     // The page is not sceduled for erase
#define H5_DEVICE_FAMILY_MASK   0x07FFU
#define H5_WRITESIZE_SHIFT      11
#define H5_WRITESIZE_MASK       0x0800U
#define H5_MEMORYMAPPED_SHIFT   12
#define H5_MEMORYMAPPED_MASK    0x1000U
#define H5_PAGESIZE_SHIFT       13
#define H5_PAGESIZE_MASK        0xE000U
#define H5_DEVINFO_MASK         (H5_DEVICE_FAMILY_MASK | H5_WRITESIZE_MASK | H5_MEMORYMAPPED_MASK | H5_PAGESIZE_MASK)

//============================================================================

typedef enum {
  eraseCntNormal,
  eraseCntInverted
} EraseCnt_t;

//============================================================================

__STATIC_INLINE bool pageHdrErased(nvm3_PageHdr_t *pageHdr)
{
  return (pageHdr->data[0] == 0xFFFFFFFFU);
}

__STATIC_INLINE bool pageHdrMagicAndVersion(nvm3_PageHdr_t *pageHdr)
{
  uint32_t h1 = le32toh(pageHdr->data[0]);
  uint16_t magic, version;

  magic = (uint16_t)(h1 >> H1_MAGIC_SHIFT);
  version = (uint16_t)h1;
  return (magic == H1_MAGIC_V1) && (version == H1_VERSION_V1);
}

__STATIC_INLINE bool pageHdrIsPageBad(nvm3_PageHdr_t *pageHdr)
{
  uint32_t h4 = le32toh(pageHdr->data[3]);
  uint32_t badPage = (h4 & H4_BAD_MASK) >> H4_BAD_SHIFT;

  return badPage != H4_BAD_GOOD;
}

__STATIC_INLINE bool pageHdrIsEraseInProgress(nvm3_PageHdr_t *pageHdr)
{
  uint32_t h4 = le32toh(pageHdr->data[3]);
  uint16_t EIP = h4 & H4_EIP_MASK;

  return EIP != H4_EIP_CLEAR;
}

__STATIC_INLINE uint32_t pageHdrGetCounterBcod(uint32_t h)
{
  return (h >> NVM3_PAGE_COUNTER_SIZE) & NVM3_PAGE_BCCB_MASK;
}

__STATIC_INLINE uint32_t pageHdrGetCounterVal(uint32_t h, EraseCnt_t level)
{
  uint32_t cnt = (level == eraseCntInverted) ? ~h : h;
  return cnt & NVM3_PAGE_COUNTER_MASK;
}

__STATIC_INLINE uint32_t pageHdrCounterMake(uint32_t cnt, EraseCnt_t level)
{
  uint8_t  BCCB;

  if (level == eraseCntInverted) {
    cnt = ~cnt;
  }
  cnt &= NVM3_PAGE_COUNTER_MASK;
  BCCB = 0;
  nvm3_utilsComputeBergerCode(&BCCB, &cnt, NVM3_PAGE_COUNTER_SIZE);
  return ((uint32_t)BCCB << NVM3_PAGE_COUNTER_SIZE) | cnt;
}

// The page size field has a value from 0 to 7 describing page sizes from 512
// to 65536 bytes. Note: The page size must be a power of 2.
static uint16_t pageSizeToField(size_t pageSize)
{
  uint16_t field = 0U;

  pageSize /= 512U;
  for (;; ) {
    pageSize /= 2U;
    if (pageSize == 0U) {
      break;
    }
    field++;
  }

  return field;
}

//============================================================================

Ecode_t nvm3_pageHeaderWrite(const nvm3_HalHandle_t *hal, nvm3_HalPtr_t pageAdr, uint32_t eraseCnt, nvm3_HalInfo_t *halInfo, bool encrypted)
{
  nvm3_PageHdr_t pageHdr;
  uint32_t devInfo;
  uint32_t formatInfo;
  size_t ofs;
  nvm3_HalPtr_t adr;
  Ecode_t staWrite;
  Ecode_t sta = ECODE_NVM3_OK;

  // Create header
  devInfo = ((pageSizeToField(halInfo->pageSize) << H5_PAGESIZE_SHIFT) & H5_PAGESIZE_MASK)
            + (((halInfo->memoryMapped) << H5_MEMORYMAPPED_SHIFT) & H5_MEMORYMAPPED_MASK)
            + (((halInfo->writeSize) << H5_WRITESIZE_SHIFT) & H5_WRITESIZE_MASK)
            + ((halInfo->deviceFamily) & H5_DEVICE_FAMILY_MASK);
  formatInfo = (encrypted ? 0 : 1)
               + 0xFFFEU;
  pageHdr.data[0] = htole32((H1_MAGIC_V1 << H1_MAGIC_SHIFT) | H1_VERSION_V1);
  pageHdr.data[1] = htole32(pageHdrCounterMake(eraseCnt, eraseCntNormal));
  pageHdr.data[2] = htole32(pageHdrCounterMake(eraseCnt, eraseCntInverted));
  pageHdr.data[3] = htole32(0xffffffffU);
  pageHdr.data[4] = htole32((formatInfo << 16) | devInfo);
  //printf("-> hdrWr: adr=%p, cnt=%u, 1=0x%08x, 2=0x%08x\n", pageAdr, eraseCnt, le32toh(pageHdr.data[1]), le32toh(pageHdr.data[2]));

  // Write header in the following order: H5 -> H3 -> H2 -> H1
  // Write H5
  ofs = 4 * sizeof(uint32_t);
  adr = (nvm3_HalPtr_t)((size_t)pageAdr + ofs);
  staWrite = nvm3_halWriteWords(hal, adr, &(pageHdr.data[4]), 1);
  if (staWrite != ECODE_NVM3_OK) {
    sta = staWrite;
  }

  // Write H4
  // Do not write 0xffffffff

  // Write H3
  ofs = 2 * sizeof(uint32_t);
  adr = (nvm3_HalPtr_t)((size_t)pageAdr + ofs);
  staWrite = nvm3_halWriteWords(hal, adr, &(pageHdr.data[2]), 1);
  if (staWrite != ECODE_NVM3_OK) {
    sta = staWrite;
  }

  // Write H2
  ofs = 1 * sizeof(uint32_t);
  adr = (nvm3_HalPtr_t)((size_t)pageAdr + ofs);
  staWrite = nvm3_halWriteWords(hal, adr, &(pageHdr.data[1]), 1);
  if (staWrite != ECODE_NVM3_OK) {
    sta = staWrite;
  }

  // Write H1
  ofs = 0;
  adr = (nvm3_HalPtr_t)((size_t)pageAdr + ofs);
  staWrite = nvm3_halWriteWords(hal, adr, &(pageHdr.data[0]), 1);
  if (staWrite != ECODE_NVM3_OK) {
    sta = staWrite;
  }

  return sta;
}

/*** Mark page as bad page */
void nvm3_pageSetBad(const nvm3_HalHandle_t *hal, nvm3_HalPtr_t pageAdr)
{
  uint32_t  h4Rd;
  uint32_t  h4Wr;
  nvm3_HalPtr_t adr;

  nvm3_tracePrint(NVM3_TRACE_LEVEL_WARNING, "nvm3_pageSetBad, pageAdr=0x%p.\n", pageAdr);

  adr = (nvm3_HalPtr_t)((size_t)pageAdr + NVM3_PAGE_H4_OFFSET);
  nvm3_halReadWords(hal, adr, &h4Rd, 1);
  h4Rd = le32toh(h4Rd);

  h4Wr = h4Rd & ~H4_BAD_MASK;
  h4Wr |= (H4_BAD_NOTGOOD << H4_BAD_SHIFT);

  h4Wr = htole32(h4Wr);
  (void)nvm3_halWriteWords(hal, adr, &h4Wr, 1);

  // There is no recovery from a write error at this point.
}

Ecode_t nvm3_pageSetEip(const nvm3_HalHandle_t *hal, nvm3_HalPtr_t pageAdr)
{
  uint32_t  h4Rd;
  uint32_t  h4Wr;
  nvm3_HalPtr_t adr;
  Ecode_t sta = ECODE_NVM3_OK;

  nvm3_tracePrint(NVM3_TRACE_LEVEL_LOW, "  nvm3_pageSetEip, pageAdr=0x%p.\n", pageAdr);

  adr = (nvm3_HalPtr_t)((size_t)pageAdr + NVM3_PAGE_H4_OFFSET);
  nvm3_halReadWords(hal, adr, &h4Rd, 1);
  h4Rd = le32toh(h4Rd);

  if ((h4Rd & H4_EIP_MASK) == H4_EIP_CLEAR) {
    h4Wr = (h4Rd & ~H4_EIP_MASK);
    h4Wr |= H4_EIP_SET;

    h4Wr = htole32(h4Wr);
    sta = nvm3_halWriteWords(hal, adr, &h4Wr, 1);
  }

  return sta;
}

uint32_t nvm3_pageGetEraseCnt(nvm3_PageHdr_t *pageHdr)
{
  uint8_t BCCB;

  BCCB = 0;
  uint32_t h2 = le32toh(pageHdr->data[1]);
  nvm3_utilsComputeBergerCode(&BCCB, &h2, NVM3_PAGE_COUNTER_SIZE);
  if (BCCB == pageHdrGetCounterBcod(h2)) {
    return pageHdrGetCounterVal(h2, eraseCntNormal);
  }

  BCCB = 0;
  uint32_t h3 = le32toh(pageHdr->data[2]);
  nvm3_utilsComputeBergerCode(&BCCB, &h3, NVM3_PAGE_COUNTER_SIZE);
  if (BCCB == pageHdrGetCounterBcod(h3)) {
    return pageHdrGetCounterVal(h3, eraseCntInverted);
  }

  //printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> ERROR: getEraseCount failed!\n");
  return NVM3_ERASE_COUNT_INVALID;
}

nvm3_PageState_t nvm3_pageGetState(nvm3_PageHdr_t *pageHdr)
{
  nvm3_PageState_t pageState;
  uint32_t eraseCnt = NVM3_ERASE_COUNT_INVALID;
  bool pageHdrValid;

  pageHdrValid = pageHdrMagicAndVersion(pageHdr);

  if (pageHdrValid) {
    eraseCnt = nvm3_pageGetEraseCnt(pageHdr);
  }

  if (eraseCnt != NVM3_ERASE_COUNT_INVALID) {
    if (pageHdrIsPageBad(pageHdr)) {
      pageState = nvm3_PageStateBad;
    } else if (pageHdrIsEraseInProgress(pageHdr)) {
      pageState = nvm3_PageStateGoodEip;
    } else {
      pageState = nvm3_PageStateGood;
    }
  } else {
    if (pageHdrErased(pageHdr)) {
      pageState = nvm3_PageStateInvalidErased;
    } else {
      pageState = nvm3_PageStateInvalidUnknown;
    }
  }

  return pageState;
}

bool nvm3_pageStateIsGood(nvm3_PageState_t pageState)
{
  return ((pageState == nvm3_PageStateGood) || (pageState == nvm3_PageStateGoodEip));
}

bool nvm3_pageStateIsInvalid(nvm3_PageState_t pageState)
{
  return ((pageState == nvm3_PageStateInvalidErased) || (pageState == nvm3_PageStateInvalidUnknown));
}

nvm3_ObjPtr_t nvm3_pageGetFirstObj(nvm3_HalPtr_t pageAdr)
{
  return (nvm3_ObjPtr_t)((size_t)pageAdr + NVM3_PAGE_HEADER_SIZE);
}

Ecode_t nvm3_pageErase(const nvm3_HalHandle_t *hal, nvm3_HalPtr_t pageAdr, uint32_t eraseCnt, nvm3_HalInfo_t *halInfo, bool encrypted)
{
  Ecode_t sta;

  nvm3_tracePrint(NVM3_TRACE_LEVEL_LOW, "  nvm3_pageErase: adr=0x%p, eraseCnt=%u.\n", pageAdr, eraseCnt);

  // Erase
  sta = nvm3_halPageErase(hal, pageAdr);
  if (sta == ECODE_NVM3_OK) {
    // Create new page header
    sta = nvm3_pageHeaderWrite(hal, pageAdr, eraseCnt, halInfo, encrypted);
    if (sta != ECODE_NVM3_OK) {
      nvm3_tracePrint(NVM3_TRACE_LEVEL_WARNING, "  erasePage: adr=0x%p, Write hdr ERROR, mark page as BAD.\n", pageAdr);
      nvm3_pageSetBad(hal, pageAdr);
    }
  } else {
    // Erasure failed, mark page as BAD
    nvm3_tracePrint(NVM3_TRACE_LEVEL_WARNING, "  erasePage: adr=0x%p, Erase ERROR, page is marked as BAD.\n", pageAdr);
    nvm3_pageSetBad(hal, pageAdr);
  }

  //nvm3_tracePrint("  erasePage: sta=%u.\n", sta);
  return sta;
}

/** @endcond */
