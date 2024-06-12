/* © 2018 Silicon Laboratories Inc.
 */

#ifndef RF_REGION_SET_VALIDATOR_
#define RF_REGION_SET_VALIDATOR_

/*Filtering valid RF region value from a whitelist when parasing zipgateway.cfg */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include "ZIP_Router_logging.h"

/* valid RF region value used in ZW_RF_REGION_SET and ZW_RF_REGION_GET */
enum
{
  EU = 0x00,  /* Europe */
  US = 0x01,  /* US */
  ANZ = 0x02, /* Australia & New Zealand */
  HK = 0x03,  /* Hong Kong */
  ID = 0x05,  /* India */
  IL = 0x06,  /* Israel */
  RU = 0x07,  /* Russia */
  CN = 0x08,  /* China */
  US_LR = 0x09,  /* US Long Range*/
  JP = 0x20,  /* Japan */
  KR = 0x21   /* Korea */
};

uint8_t RF_REGION_CHECK(uint8_t region_value);

#endif
