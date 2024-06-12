/***************************************************************************//**
 * @file
 * @brief NVM3 object cache
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

#ifndef NVM3_CACHE_H
#define NVM3_CACHE_H

#include "nvm3_config.h"
#include "nvm3.h"
#include "nvm3_object.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 *******************************   TYPES   *************************************
 ******************************************************************************/

typedef void (*nvm3_CacheScanCallback_t)(nvm3_Cache_t *h, nvm3_ObjectKey_t key, nvm3_ObjGroup_t group, nvm3_ObjPtr_t obj, void *user);

/*******************************************************************************
 ***************************   PROTOTYPES   ************************************
 ******************************************************************************/

void nvm3_cacheOpen(nvm3_Cache_t *h, nvm3_CacheEntry_t *ptr, size_t count);
void nvm3_cacheClear(nvm3_Cache_t *h);
void nvm3_cacheDelete(nvm3_Cache_t *h, nvm3_ObjectKey_t key);
nvm3_ObjPtr_t nvm3_cacheGet(nvm3_Cache_t *h, nvm3_ObjectKey_t key, nvm3_ObjGroup_t *group);
void nvm3_cacheSet(nvm3_Cache_t *h, nvm3_ObjectKey_t key, nvm3_ObjPtr_t obj, nvm3_ObjGroup_t group);

void nvm3_cacheScan(nvm3_Cache_t *h, nvm3_CacheScanCallback_t cacheScanCallback, void *user);

#ifdef __cplusplus
}
#endif

#endif /* NVM3_CACHE_H */
