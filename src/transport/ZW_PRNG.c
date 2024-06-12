/* © 2014 Silicon Laboratories Inc.
 */

/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/


#define NO_MEM_FUNCTIONS
#include<string.h>


#include "Serialapi.h"
#include "ZW_PRNG.h"
#include "ZIP_Router_logging.h"
#include "random.h"

void GetRNGData(BYTE *pRNDData, BYTE noRNDDataBytes)REENTRANT;
static void PRNGUpdate(void)REENTRANT;

/**
 * Pseudo-random number generator seed - is only reset on reset, */
/* on wakeup by either WUT or BEAM the previous seed is maintained
 * via usage of the NON_ZERO_START block */
static BYTE prngState[16];  /* PRNG state */

/* Use SerialAPI */
#define ZW_AES_ECB(key, inputDat, outputDat) SerialAPI_AES128_Encrypt(inputDat, outputDat, key);

/**
 * XOR 16 bytes
 */
static void xor16(BYTE* c, BYTE* a, BYTE* b)
{
  int i;
  for (i = 0; i < 16; i++)
  {
    c[i] = (BYTE) (a[i] ^ b[i]);
  }
}

/*================================   AESRaw   ===============================
 **    AES Raw
 **
 **    Side effects :
 **
 **--------------------------------------------------------------------------*/
/*
 Declaration: void AESRaw(BYTE *pKey, BYTE *pSrc, BYTE *pDest)
 Called: When individual 128-bit blocks of data have to be encrypted
 Arguments: pKey Pointer to key (input; fixed size 16 bytes)
 pSrc Pointer to source data (input; fixed size 16 bytes)
 pDest Pointer to destination buffer (output; fixed size
 16 bytes)
 Return value: None
 Global vars: None affected
 Task: Encrypts 16 bytes of data at pSrc, using Raw AES and the key at pKey. The
 16-byte result is written to pDest.*/
void AESRaw(BYTE *pKey, BYTE *pSrc, BYTE *pDest)
{
  memcpy(pDest, pSrc, 16);
  ZW_AES_ECB(pKey, pSrc, pDest);
}


void InitPRNG(void)
{
  /* Reset PRNG State */
  memset(prngState, 0, 16);
  /* Update PRNG State */
  PRNGUpdate();
}

/*===============================   GetRNGData   =============================
 **    GetRNGData
 **
 **    Side effects :
 **
 **--------------------------------------------------------------------------*/
void GetRNGData(BYTE *pRNDData, BYTE noRNDDataBytes)
{
  BYTE i,j;
  BYTE rnd[8];
  /* It is no longer needed to call SetRFReceiveMode() before and after GetRandomWord. */

  j=0;
  for(i=0; i <noRNDDataBytes; i++) {
    if(j==0) {
      if (!dev_urandom(sizeof(rnd), rnd)) {
          ERR_PRINTF("Failed to seed random number generator.\n");
      }
    }
    pRNDData[i] = rnd[j];
    j = (j+1) & 0x7;
  }
}

/**
 Incorporate new data from hardware RNG into the PRNG State
 Called, When fresh input from hardware RNG is needed
 */
static void PRNGUpdate(void) REENTRANT
{

  BYTE k[16], h[16], ltemp[16], btemp[16],j;

  /* H = 0xA5 (repeated x16) */
  memset(h, 0xA5, sizeof(h));
  /* The two iterations of the hardware generator */
  for (j = 0; j <2; j++)
  {
    /* Random data to K */
    GetRNGData(k, 16);
    /* ltemp = AES(K, H) */
    AESRaw(k, h, ltemp);
    /* H = AES(K, H) ^ H */
    xor16(h,ltemp,h);
  }
  /* Update inner state */
  /* S = S ^ H */
  xor16(prngState,prngState,h);

  /* ltemp = 0x36 (repeated x16) */
  memset(ltemp, 0x36, 16);
  /* S = AES(S, ltemp) */
  AESRaw(prngState, ltemp, btemp);
  memcpy(prngState, btemp, 16);
}

void PRNGOutput(BYTE *pDest) REENTRANT
{
  BYTE ltemp[16], btemp[16];

  /* Generate output */
  /* ltemp = 0x5C (repeated x16) */
  memset((BYTE *) ltemp, 0x5C/*0xA5*/, 16);
  /* ltemp = AES(PRNGState, ltemp) */
  AESRaw(prngState, ltemp, btemp);
  /* pDest[0..7] = ltemp[0..7] */
  memcpy(pDest, btemp, 8);
  /* Generate next internal state */
  /* ltemp = 0x36 (repeated x16) */
  memset((BYTE *) ltemp, 0x36, 16);
  /* PRNGState = AES(PRNGState, ltemp) */
  AESRaw(prngState, ltemp, btemp);
  memcpy(prngState, btemp, 16);
}
