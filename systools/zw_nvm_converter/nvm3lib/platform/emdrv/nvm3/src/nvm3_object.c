/***************************************************************************//**
 * @file
 * @brief NVM3 object handling functions
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

#include "nvm3_object.h"
#include "nvm3_utils.h"
#include "endian_wrap.h"
#include <string.h>

#define NVM3_OBJ_LEN_SIZE                7U          //  7 bits
#define NVM3_OBJ_LEN_MASK                ((1U << NVM3_OBJ_LEN_SIZE) - 1U)
#define NVM3_OBJ_LLEN_SIZE               26U         // 26 bits: large obj length
#define NVM3_OBJ_LLEN_MASK               ((1U << NVM3_OBJ_LLEN_SIZE) - 1U)
#define NVM3_OBJ_BCCB_SIZE               5U          //  5 bits: small obj berger code
#define NVM3_OBJ_BCCB_MASK               ((1U << NVM3_OBJ_BCCB_SIZE) - 1U)
#define NVM3_OBJ_LBCCB_SIZE              6U          //  6 bits: large obj berger code
#define NVM3_OBJ_LBCCB_MASK              ((1U << NVM3_OBJ_LBCCB_SIZE) - 1U)
#define NVM3_OBJ_F_SIZE                  2U          //  2 bits: fragment field
#define NVM3_OBJ_F_MASK                  ((1U << NVM3_OBJ_F_SIZE) - 1U)
#define NVM3_OBJ_U_SIZE                  3U          //  3 bits: unused field
#define NVM3_OBJ_U_MASK                  ((1U << NVM3_OBJ_U_SIZE) - 1U)
#define NVM3_OBJ_KEY_OFFSET              (NVM3_OBJ_LEN_SIZE)
#define NVM3_OBJ_F_OFFSET                (NVM3_OBJ_LEN_SIZE + NVM3_KEY_SIZE)
#define NVM3_OBJ_U_OFFSET                (NVM3_OBJ_LEN_SIZE + NVM3_KEY_SIZE + NVM3_OBJ_F_SIZE)
#define NVM3_OBJ_BCCB_OFFSET             (NVM3_OBJ_LEN_SIZE + NVM3_KEY_SIZE)
#define NVM3_OBJ_LBCCB_OFFSET            (NVM3_OBJ_LLEN_SIZE)

//****************************************************************************

__STATIC_INLINE uint32_t hdrSmallGetBergerCode(nvm3_ObjHdrSmall_t *oh)
{
  return ((le32toh(oh->oh1) >> NVM3_OBJ_BCCB_OFFSET) & NVM3_OBJ_BCCB_MASK);
}

__STATIC_INLINE uint32_t hdrLargeGetBergerCode(nvm3_ObjHdrLarge_t *oh)
{
  return ((le32toh(oh->oh2) >> NVM3_OBJ_LBCCB_OFFSET) & NVM3_OBJ_LBCCB_MASK);
}

__STATIC_INLINE nvm3_ObjType_t toObjType(uint32_t value)
{
  int type = (int)value;
  return (nvm3_ObjType_t)type;
}

__STATIC_INLINE nvm3_ObjFragType_t toObjFragType(uint32_t value)
{
  int type = (int)value;
  return (nvm3_ObjFragType_t)type;
}

__STATIC_INLINE nvm3_ObjType_t hdrGetType(nvm3_ObjHdrSmall_t *oh)
{
  return toObjType(le32toh(oh->oh1) & NVM3_OBJ_LEN_MASK);
}

__STATIC_INLINE bool hdrIsLarge(nvm3_ObjHdrSmall_t *oh)
{
  nvm3_ObjType_t objTyp = hdrGetType(oh);
  return ((objTyp == objTypeCounterLarge) || (objTyp == objTypeDataLarge));
}

//****************************************************************************

/*** Initialize object header */
size_t nvm3_objHdrInit(nvm3_ObjHdrLargePtr_t oh, nvm3_ObjectKey_t key, nvm3_ObjType_t objType,
                       size_t len, bool isLarge, nvm3_ObjFragType_t fragTyp)
{
  uint8_t BCCB = 0;

  oh->oh1 = (key & NVM3_KEY_MASK) << NVM3_OBJ_KEY_OFFSET;
  oh->oh2 = (len & NVM3_OBJ_LLEN_MASK);
  switch (objType) {
    case objTypeCounterLarge:
    /* Intented fall-through */
    case objTypeDataLarge:
      oh->oh1 |= (uint32_t)objType;
      oh->oh1 |= ((uint32_t)NVM3_OBJ_U_MASK << NVM3_OBJ_U_OFFSET);
      oh->oh1 |= ((uint32_t)fragTyp & NVM3_OBJ_F_MASK) << NVM3_OBJ_F_OFFSET;
      break;
    case objTypeCounterSmall:
    /* Intented fall-through*/
    case objTypeDeleted:
      oh->oh1 |= (uint32_t)objType;
      break;
    default:
      oh->oh1 |= ((len + (uint32_t)objTypeDataSmall) & NVM3_OBJ_LEN_MASK);
      break;
  }
  if (isLarge) {
    nvm3_utilsComputeBergerCode(&BCCB, &oh->oh1, 32);
    nvm3_utilsComputeBergerCode(&BCCB, &oh->oh2, NVM3_OBJ_LLEN_SIZE);
    oh->oh2 |= ((uint32_t)BCCB & NVM3_OBJ_LBCCB_MASK) << NVM3_OBJ_LBCCB_OFFSET;
  } else {
    nvm3_utilsComputeBergerCode(&BCCB, &oh->oh1, (uint8_t)(NVM3_OBJ_LEN_SIZE + NVM3_KEY_SIZE));
    oh->oh1 |= ((uint32_t)BCCB & NVM3_OBJ_BCCB_MASK) << NVM3_OBJ_BCCB_OFFSET;
  }

  oh->oh1 = htole32(oh->oh1);
  oh->oh2 = htole32(oh->oh2);

  return nvm3_objHdrLen(isLarge);
}

size_t nvm3_objHdrLen(bool isLarge)
{
  return isLarge ? NVM3_OBJ_HEADER_SIZE_LARGE : NVM3_OBJ_HEADER_SIZE_SMALL;
}

bool nvm3_objHdrValidateSmall(nvm3_ObjHdrSmallPtr_t objHdrSmall)
{
  uint8_t codReq = 0;
  uint8_t codAct;
  bool res = false;

  uint32_t oh1 = le32toh(objHdrSmall->oh1);

  nvm3_utilsComputeBergerCode(&codReq, &oh1, (uint8_t)(NVM3_OBJ_LEN_SIZE + NVM3_KEY_SIZE));
  codAct = (uint8_t)hdrSmallGetBergerCode(objHdrSmall);
  if (codReq == codAct) {
    res = true;
  }

  return res;
}

bool nvm3_objHdrValidateLarge(nvm3_ObjHdrLargePtr_t objHdrLarge)
{
  uint8_t codReq = 0;
  uint8_t codAct;
  bool res = false;

  uint32_t oh1 = le32toh(objHdrLarge->oh1);
  uint32_t oh2 = le32toh(objHdrLarge->oh2);

  nvm3_utilsComputeBergerCode(&codReq, &oh1, 32);
  nvm3_utilsComputeBergerCode(&codReq, &oh2, NVM3_OBJ_LLEN_SIZE);
  codAct = (uint8_t)hdrLargeGetBergerCode(objHdrLarge);
  if (codReq == codAct) {
    res = true;
  }

  return res;
}

bool nvm3_objHdrGetErased(nvm3_ObjHdrSmallPtr_t objHdrSmall)
{
  return objHdrSmall->oh1 == 0xFFFFFFFFU;
}

nvm3_ObjFragType_t nvm3_objHdrGetFragTyp(nvm3_ObjHdrSmallPtr_t objHdrSmall)
{
  nvm3_ObjFragType_t fragTyp = toObjFragType((le32toh(objHdrSmall->oh1) >> NVM3_OBJ_F_OFFSET) & NVM3_OBJ_F_MASK);

  return hdrIsLarge(objHdrSmall) ? fragTyp : fragTypeNone;
}

nvm3_ObjectKey_t nvm3_objHdrGetKey(nvm3_ObjHdrSmallPtr_t objHdrSmall)
{
  return (nvm3_ObjectKey_t)((le32toh(objHdrSmall->oh1) >> NVM3_OBJ_KEY_OFFSET) & NVM3_KEY_MASK);
}

bool nvm3_objHdrGetHdrIsLarge(nvm3_ObjHdrSmallPtr_t objHdrSmall)
{
  return hdrIsLarge(objHdrSmall);
}

size_t nvm3_objHdrGetHdrLen(nvm3_ObjHdrSmallPtr_t objHdrSmall)
{
  return nvm3_objHdrLen(hdrIsLarge(objHdrSmall));
}

size_t nvm3_objHdrGetDatLen(nvm3_ObjHdrLargePtr_t objHdrLarge)
{
  size_t len;

  if (hdrIsLarge((nvm3_ObjHdrSmallPtr_t)objHdrLarge)) {
    len = le32toh(objHdrLarge->oh2) & NVM3_OBJ_LLEN_MASK;
  } else {
    len = le32toh(objHdrLarge->oh1) & NVM3_OBJ_LEN_MASK;
    len = (len > (size_t)objTypeDataSmall) ? (len - (size_t)objTypeDataSmall) : 0U;
  }

  return len;
}

nvm3_ObjType_t nvm3_objHdrGetType(nvm3_ObjHdrSmallPtr_t objHdrSmall)
{
  return hdrGetType(objHdrSmall);
}

void nvm3_objInit(nvm3_ObjPtr_t obj, nvm3_ObjPtr_t objAdr)
{
  // The sizeof(nvm3_Obj_t) is dependent on the page size.
  (void)memset(obj, 0, nvm3_objHandleSize);
  obj->key = NVM3_KEY_INVALID;
  obj->objAdr = objAdr;
  obj->isValid = false;
  obj->totalLen = 0;
  obj->nextObjAdr = NVM3_OBJ_PTR_INVALID;
  obj->isFragmented = false;
  obj->frag.isFirstFragFound = false;
  obj->frag.isLastFragFound = false;
  obj->frag.idx = 0;
}

nvm3_ObjType_t nvm3_objGroupToType(nvm3_ObjGroup_t objGroup, bool hdrIsLarge)
{
  nvm3_ObjType_t objType;

  if (objGroup == objGroupDeleted) {
    objType = objTypeDeleted;
  } else if (objGroup == objGroupCounter) {
    objType = hdrIsLarge ? objTypeCounterLarge : objTypeCounterSmall;
  } else {
    objType = hdrIsLarge ? objTypeDataLarge : objTypeDataSmall;
  }

  return objType;
}

nvm3_ObjGroup_t nvm3_objTypeToGroup(nvm3_ObjType_t objType)
{
  nvm3_ObjGroup_t objGroup;

  switch (objType) {
    case objTypeCounterSmall:
      objGroup = objGroupCounter;
      break;
    case objTypeCounterLarge:
      objGroup = objGroupCounter;
      break;
    case objTypeDataLarge:
      objGroup = objGroupData;
      break;
    case objTypeRes_1:
    case objTypeRes_2:
    case objTypeRes_3:
      objGroup = objGroupUnknown;
      break;
    case objTypeDeleted:
      objGroup = objGroupDeleted;
      break;
    default:
      objGroup = objGroupData;
      break;
  }

  return objGroup;
}
