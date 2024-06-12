#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "Serialapi.h"
#include "sdk_versioning.h" 

/**
 * Translate the 6.x 500-series based version numbers to SDK version numbers.
 *
 * The 500-series only uses major and minor version numbers. The revision is always 0.
 *
 * Refer to Chapter 7 of INS13954 for the latest version mappings. This is useful for updating this function.
 * Internally in SiLabs, this document can be found at https://highstage.silabs.com/ts/ts/search.aspx?t=doc&name==INS13954
 */
static void Translate6xVersionNumbers(
    uint8_t *p_major,
    uint8_t *p_minor,
    uint8_t *p_rev)
{
  if (*p_major > 6) {
    /* SDK 7 and up do not need translation */
    return;
  }

  switch (*p_major * 100 + *p_minor)
  {
    case 412:
    case 414:
      *p_major = 6; *p_minor = 60; *p_rev = 0; break;
    case 433:
      *p_major = 6; *p_minor = 61; *p_rev = 0; break;
    case 462:
      *p_major = 6; *p_minor = 61; *p_rev = 1; break;
    case 428:
      *p_major = 6; *p_minor = 70; *p_rev = 0; break;
    case 445:
      *p_major = 6; *p_minor = 70; *p_rev = 1; break;
    case 460:
      *p_major = 6; *p_minor = 71; *p_rev = 0; break;
    case 461:
      *p_major = 6; *p_minor = 71; *p_rev = 1; break;
    case 502:
      *p_major = 6; *p_minor = 71; *p_rev = 2; break;
    case 503:
      *p_major = 6; *p_minor = 71; *p_rev = 3; break;
    case 601:
      *p_major = 6; *p_minor = 81; *p_rev = 0; break;
    case 602:
      *p_major = 6; *p_minor = 81; *p_rev = 1; break;
    case 603:
      *p_major = 6; *p_minor = 81; *p_rev = 2; break;
    case 604:
      *p_major = 6; *p_minor = 81; *p_rev = 3; break;
    case 605:
      *p_major = 6; *p_minor = 81; *p_rev = 4; break;
    case 606:
      *p_major = 6; *p_minor = 81; *p_rev = 5; break;
    case 607:
      *p_major = 6; *p_minor = 81; *p_rev = 6; break;
    case 608:
      *p_major = 6; *p_minor = 82; *p_rev = 0; break;
    case 609:
      *p_major = 6; *p_minor = 82; *p_rev = 1; break;
    case 610:
      *p_major = 6; *p_minor = 84; *p_rev = 0; break;
    default:
      /* Ideally the above table should be updated with every new release.
      * But in case this does not always happen we make a simple generic
      * fallback mapping unknown 6.xx protocol version to a future sdk version
      * 6.89.99.
      * This should work until the NVM layout changes. Since 6.8x is in
      * maintenance mode, we expect that to never happen.
      * If NVM layout does change, the table above - and the nvm converter -
      * needs to be updated.
      */
      if (6 == *p_major)
      {
        /* Identify unknown 6.x protocol versions as future release 6.89.99 */
        *p_major = 6;
        *p_minor = 89;
        *p_rev = 99;
      }
    break;
  }
}

const char * GenerateNvmIdFromSdkVersion(uint8_t major, uint8_t minor, uint8_t rev,
                                                uint8_t library_type, uint8_t chip_type)
{
  static char nvm_id_buf[20] = {};

  nvm_id_buf[0] = '\0';

  if (major)
  {
    /* The SDK version is assumed as M.mm.pp, i.e. first digit is major
     * version number (see GenerateSdkVersionFromProtocolVersion())
     */
    if((major == 7) && (minor == 11) && (rev == 0)) {
      return "bridge7.11";
    } else if (major == 7 && minor < 15)
    {
      /* For now we don't differentiate between the versions following
       * version 6, we simply assume they all use the nvm3 file system
       * driver.
       */
      return "bridge7.12";
    } else if (major == 7 && minor == 15) {
      return "bridge7.15";
    } else if (major == 7 && minor == 16){
      return "bridge7.16";
    } else if (major == 7 && minor == 17){
      if (8 == chip_type){
        return "bridge_800s_7.17";
      } else {
        return "bridge_700s_7.17";
      }
    } else if (major >= 7 && minor >= 18){
      if (8 == chip_type){
        return "bridge_800s_7.18";
      } else {
        return "bridge_700s_7.18";
      }
    }

    else
    {
      const char *library_type_str = "";

      switch (library_type)
      {
       case ZW_LIB_CONTROLLER_BRIDGE:
          library_type_str = "bridge";
          break;
       case ZW_LIB_CONTROLLER_STATIC:
          library_type_str = "static";
          break;
       case ZW_LIB_CONTROLLER:
          library_type_str = "portable";
          break;
        default:
          library_type_str = "ERROR";
          break;
      }

      Translate6xVersionNumbers(&major, &minor, &rev);

      /* As the NVM layout for SDK 6.8x was only changed after 6.81.00, we handle 6.81.00 as a 6.7 controller. */
      if((major == 6) && (minor == 81) && (rev == 0)) {
        snprintf(nvm_id_buf, sizeof(nvm_id_buf), "%s6.7", library_type_str);
      } else {
        /* Only use the first three chars of the sdk version,
        * i.e. 6.81.03 becomes 6.8. End result e.g. "bridge6.8"
        */
        snprintf(nvm_id_buf, sizeof(nvm_id_buf), "%s%0d.%0d", library_type_str, major, minor / 10);
      }
    }
  }
  return nvm_id_buf; // Static buffer
}


