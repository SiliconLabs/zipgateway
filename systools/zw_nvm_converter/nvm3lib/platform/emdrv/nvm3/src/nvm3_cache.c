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

#include "nvm3_cache.h"
#include "nvm3_trace.h"

#if defined(__ICCARM__)
#define SPEED_OPT _Pragma("optimize=speed")
#elif defined(__GNUC__) && defined(__CORTEX_M)
#define SPEED_OPT _Pragma("GCC optimize(\"O3\")")
#else
#define SPEED_OPT
#endif

#define TRACE_LEVEL                 NVM3_TRACE_LEVEL_LOW

//****************************************************************************

// isValid is implemented as a macro as this significantly improves
// speed when using compiler settings that do not inline these functions.
#define isValid(h, idx) (h->entryPtr[idx].key != NVM3_KEY_INVALID)

static inline nvm3_ObjectKey_t entryGetKey(nvm3_Cache_t *h, size_t idx)
{
  uint32_t tmp = (uint32_t)h->entryPtr[idx].key;
  return (nvm3_ObjectKey_t)(tmp & NVM3_KEY_MASK);
}

static inline nvm3_ObjGroup_t entryGetGroup(nvm3_Cache_t *h, size_t idx)
{
  uint32_t tmp = (uint32_t)h->entryPtr[idx].key;
  return (nvm3_ObjGroup_t)(tmp >> NVM3_KEY_SIZE);
}

static inline nvm3_ObjPtr_t entryGetPtr(nvm3_Cache_t *h, size_t idx)
{
  return h->entryPtr[idx].ptr;
}

static inline void entrySetKey(nvm3_Cache_t *h, size_t idx, nvm3_ObjectKey_t key)
{
  uint32_t tmp = (uint32_t)h->entryPtr[idx].key;
  tmp &= ~NVM3_KEY_MASK;
  tmp |= key;
  h->entryPtr[idx].key = (nvm3_ObjectKey_t)tmp;
}

static inline void entrySetGroup(nvm3_Cache_t *h, size_t idx, nvm3_ObjGroup_t group)
{
  uint32_t tmp = (uint32_t)h->entryPtr[idx].key;
  tmp &= NVM3_KEY_MASK;
  tmp |= (group << NVM3_KEY_SIZE);
  h->entryPtr[idx].key = (nvm3_ObjectKey_t)tmp;
}

static inline void entrySetPtr(nvm3_Cache_t *h, size_t idx, nvm3_ObjPtr_t obj)
{
  h->entryPtr[idx].ptr = obj;
}

static inline void setInvalid(nvm3_Cache_t *h, size_t idx)
{
  h->entryPtr[idx].key = NVM3_KEY_INVALID;
  h->entryPtr[idx].ptr = NVM3_OBJ_PTR_INVALID;
}

//****************************************************************************

void nvm3_cacheOpen(nvm3_Cache_t *h, nvm3_CacheEntry_t *ptr, size_t count)
{
  h->entryPtr = ptr;
  h->entryCount = count;
  nvm3_cacheClear(h);
}

void nvm3_cacheClear(nvm3_Cache_t *h)
{
  nvm3_tracePrint(TRACE_LEVEL, "      nvm3_cacheClear.\n");

  for (size_t idx = 0; idx < h->entryCount; idx++) {
    setInvalid(h, idx);
  }
  h->overflow = false;
}

void nvm3_cacheDelete(nvm3_Cache_t *h, nvm3_ObjectKey_t key)
{
  bool found = false;

  for (size_t idx = 0; idx < h->entryCount; idx++) {
    if (isValid(h, idx)) {
      if (entryGetKey(h, idx) == key) {
        setInvalid(h, idx);
        found = true;
        break;
      }
    }
  }

  nvm3_tracePrint(TRACE_LEVEL, "      nvm3_cacheDelete, key=%u, found=%d.\n", key, found ? 1 : 0);
  (void)found;
}

nvm3_ObjPtr_t nvm3_cacheGet(nvm3_Cache_t *h, nvm3_ObjectKey_t key, nvm3_ObjGroup_t *group)
{
  nvm3_ObjPtr_t obj = NVM3_OBJ_PTR_INVALID;
#if NVM3_TRACE_PORT
  int tmp = -1;
#endif

  for (size_t idx = 0; idx < h->entryCount; idx++) {
    if (isValid(h, idx)) {
      if (entryGetKey(h, idx) == key) {
        *group = entryGetGroup(h, idx);
        obj = entryGetPtr(h, idx);
#if NVM3_TRACE_PORT
        tmp = (int)idx;
#endif
        break;
      }
    }
  }

  nvm3_tracePrint(TRACE_LEVEL, "      nvm3_cacheGet, key=%u, ==> grp=%d, obj=%p, idx=%d.\n", key, (obj != NVM3_OBJ_PTR_INVALID) ? *group : -1, obj, tmp);

  return obj;
}

SPEED_OPT
void nvm3_cacheSet(nvm3_Cache_t *h, nvm3_ObjectKey_t key, nvm3_ObjPtr_t obj, nvm3_ObjGroup_t group)
{
  bool bSet = false;

  // Update existing entry
  for (size_t idx = 0; idx < h->entryCount; idx++) {
    if (isValid(h, idx)) {
      if (entryGetKey(h, idx) == key) {
        entrySetGroup(h, idx, group);
        entrySetPtr(h, idx, obj);
        nvm3_tracePrint(TRACE_LEVEL, "      nvm3_cacheSet(1), key=%u, grp=%u, obj=%p, idx=%zu.\n", key, group, obj, idx);
        return;
      }
    }
  }

  // Add new Entry
  for (size_t idx = 0; idx < h->entryCount; idx++) {
    if (!isValid(h, idx)) {
      entrySetKey(h, idx, key);
      entrySetGroup(h, idx, group);
      entrySetPtr(h, idx, obj);
      bSet = true;
      nvm3_tracePrint(TRACE_LEVEL, "      nvm3_cacheSet(2), key=%u, grp=%u, obj=%p, idx=%zu.\n", key, group, obj, idx);
      break;
    }
  }

  // Full, prioritize data over deleted objects, force an overwrite if possible
  if ((!bSet) && (group != objGroupDeleted)) {
    for (size_t idx = 0; idx < h->entryCount; idx++) {
      nvm3_ObjGroup_t cacheGroup = entryGetGroup(h, idx);
      if (cacheGroup == objGroupDeleted) {
        entrySetKey(h, idx, key);
        entrySetGroup(h, idx, group);
        entrySetPtr(h, idx, obj);
        bSet = true;
        nvm3_tracePrint(TRACE_LEVEL, "      nvm3_cacheSet(3), cache overflow for key=%u, grp=%u, obj=%p, inserted at idx=%zu.\n", key, group, obj, idx);
        break;
      }
    }
  }

  if (!bSet) {
    h->overflow = true;
    nvm3_tracePrint(TRACE_LEVEL, "      nvm3_cacheSet(4), cache overflow for key=%u, grp=%u, obj=%p.\n", key, group, obj);
  }
}

void nvm3_cacheScan(nvm3_Cache_t *h, nvm3_CacheScanCallback_t cacheScanCallback, void *user)
{
  for (size_t idx = 0; idx < h->entryCount; idx++) {
    if (isValid(h, idx)) {
      // Found an object.
      nvm3_ObjectKey_t key = entryGetKey(h, idx);
      nvm3_ObjGroup_t group = entryGetGroup(h, idx);
      nvm3_ObjPtr_t obj = entryGetPtr(h, idx);
      cacheScanCallback(h, key, group, obj, user);
    }
  }
}
