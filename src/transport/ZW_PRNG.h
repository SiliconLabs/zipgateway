/* © 2014 Silicon Laboratories Inc.
 */
#ifndef _ZW_SECURITY_AES_MODULE_H_
#define _ZW_SECURITY_AES_MODULE_H_






/*=============================   PRNGInit   =================================
**    PRNGInit
**
**    Side effects :
**
**--------------------------------------------------------------------------*/
extern void PRNGInit(void);
extern void PRNGOutput(BYTE *pDest)REENTRANT;


/*==============================   InitSecurity   ============================
**    Initialization of the Security module, can be called in ApplicationInitSW
**
**    This is an application function example
**
**--------------------------------------------------------------------------*/
extern void InitPRNG();

extern void GetRNGData(BYTE *pRNDData, BYTE noRNDDataBytes)REENTRANT;

/**
 * }@
 */

#endif
