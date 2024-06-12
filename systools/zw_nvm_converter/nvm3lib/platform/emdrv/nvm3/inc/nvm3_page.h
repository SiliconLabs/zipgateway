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

#ifndef NVM3_PAGE_H
#define NVM3_PAGE_H

#include "nvm3_hal.h"
#include "nvm3_object.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @cond DO_NOT_INCLUDE_WITH_DOXYGEN

#define NVM3_PAGE_COUNTER_SIZE          27U         // 27 bits
#define NVM3_PAGE_COUNTER_MASK          ((1U << NVM3_PAGE_COUNTER_SIZE) - 1U)
#define NVM3_PAGE_BCCB_SIZE             5U          //  5 bits
#define NVM3_PAGE_BCCB_MASK             ((1U << NVM3_PAGE_BCCB_SIZE) - 1U)

#define NVM3_PAGE_HEADER_WORDS          5           // The number of words in the page header
#define NVM3_PAGE_HEADER_SIZE           (sizeof(nvm3_PageHdr_t))
#define NVM3_PAGE_HEADER_WSIZE          (sizeof(nvm3_PageHdr_t) / sizeof(uint32_t))
#define NVM3_PAGE_H1_OFFSET             (0 * sizeof(uint32_t))
#define NVM3_PAGE_H2_OFFSET             (1 * sizeof(uint32_t))
#define NVM3_PAGE_H3_OFFSET             (2 * sizeof(uint32_t))
#define NVM3_PAGE_H4_OFFSET             (3 * sizeof(uint32_t))
#define NVM3_PAGE_H5_OFFSET             (4 * sizeof(uint32_t))

#define NVM3_ERASE_COUNT_INVALID        0xFFFFFFFFU
#define NVM3_PAGE_INDEX_INVALID         0xFFFFU

typedef struct nvm3_PageHdr {
  uint32_t    data[NVM3_PAGE_HEADER_WORDS];
} nvm3_PageHdr_t;

/// @endcond

typedef enum {
  nvm3_PageStateGood,
  nvm3_PageStateGoodEip,
  nvm3_PageStateBad,
  nvm3_PageStateInvalidErased,
  nvm3_PageStateInvalidUnknown,
} nvm3_PageState_t;

Ecode_t nvm3_pageHeaderWrite(const nvm3_HalHandle_t *hal, nvm3_HalPtr_t pageAdr, uint32_t eraseCnt, nvm3_HalInfo_t *halInfo, bool encrypted);
void nvm3_pageSetBad(const nvm3_HalHandle_t *hal, nvm3_HalPtr_t pageAdr);
Ecode_t nvm3_pageSetEip(const nvm3_HalHandle_t *hal, nvm3_HalPtr_t pageAdr);
uint32_t nvm3_pageGetEraseCnt(nvm3_PageHdr_t *pageHdr);
nvm3_PageState_t nvm3_pageGetState(nvm3_PageHdr_t *pageHdr);
bool nvm3_pageStateIsGood(nvm3_PageState_t pageState);
bool nvm3_pageStateIsInvalid(nvm3_PageState_t pageState);
nvm3_ObjPtr_t nvm3_pageGetFirstObj(nvm3_HalPtr_t pageAdr);
Ecode_t nvm3_pageErase(const nvm3_HalHandle_t *hal, nvm3_HalPtr_t pageAdr, uint32_t eraseCnt, nvm3_HalInfo_t *halInfo, bool encrypted);

#ifdef __cplusplus
}
#endif

#endif /* NVM3_PAGE_H */
