/* © 2018 Silicon Laboratories Inc.
 */

#include "RF_Region_Set_Validator.h"

uint8_t RF_REGION_CHECK(uint8_t region_value){
  uint8_t RFregion;
  /*Filtering the RF Region with valid values*/
  switch(region_value)
  {
    case EU:
          RFregion = region_value;
          DBG_PRINTF("RF Region EU = %d \n",region_value );
          break;
    case US:
          RFregion = region_value;
          DBG_PRINTF("RF Region US = %d \n",region_value );
          break;
    case ANZ:
          RFregion = region_value;
          DBG_PRINTF("RF Region Australia/ New Zealand = %d \n",region_value );
          break;
    case HK:
          RFregion = region_value;
          DBG_PRINTF("RF Region Hong Kong = %d \n",region_value );
          break;
    case ID:
          RFregion = region_value;
          DBG_PRINTF("RF Region India = %d \n",region_value );
          break;
    case IL:
          RFregion = region_value;
          DBG_PRINTF("RF Region Israel = %d \n",region_value );
          break;
    case RU:
          RFregion = region_value;
          DBG_PRINTF("RF Region Russia = %d \n",region_value );
          break;
    case CN:
          RFregion = region_value;
          DBG_PRINTF("RF Region China = %d \n",region_value );
          break;
    case US_LR:
          RFregion = region_value;
          DBG_PRINTF("RF Region US_LR = %d \n",region_value );
          break;
    case JP:
         RFregion = region_value;
         DBG_PRINTF("RF Region Japan = %d \n",region_value );
         break;
    case KR:
         RFregion = region_value;
         DBG_PRINTF("RF Region Korea = %d \n",region_value );
         break;
    default:
        RFregion = 0xFE;
        DBG_PRINTF("The RF Region value is not valid: %d \n", region_value);
  }
  return RFregion;
}
