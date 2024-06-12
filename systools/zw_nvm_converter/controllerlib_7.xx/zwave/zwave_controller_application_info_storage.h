/* © 2019 Silicon Laboratories Inc. */
#ifndef _ZWAVE_CONTROLLER_APPLICATION_INFO_STORAGE_H
#define _ZWAVE_CONTROLLER_APPLICATION_INFO_STORAGE_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// ZWave/API/ZW_radio_api.h
// ---------------------------------------------------------------------------

// typedef enum
// {
//   REGION_EU = 0,        /**< Radio is located in Region EU. 2 Channel region. **/
//   REGION_US,            /**< Radio is located in Region US. 2 Channel region. **/
//   REGION_ANZ,           /**< Radio is located in Region Australia/New Zealand. 2 Channel region. **/
//   REGION_HK,            /**< Radio is located in Region Hong Kong. 2 Channel region. **/
//   REGION_IN = 5,            /**< Radio is located in Region India. 2 Channel region. **/
//   REGION_IL,            /**< Radio is located in Region Israel. 2 Channel region. **/
//   REGION_RU,            /**< Radio is located in Region Russia. 2 Channel region. **/
//   REGION_CN,            /**< Radio is located in Region China. 2 Channel region. **/
//   REGION_2CH_NUM = (REGION_CN - REGION_EU) + 1, /**< Number of 2 channel region values. For checking if value is out of range. **/
//   REGION_JP = 32,       /**< Radio is located in Region Japan. 3 Channel region. **/
//   REGION_KR,            /**< Radio is located in Region Korea. 3 Channel region. **/
//   REGION_3CH_NUM = (REGION_KR - REGION_JP) + 1, /**< Number of 3 channel region values. For checking if value is out of range. **/
//   REGION_UNDEFINED = 0xFE,
//   REGION_DEFAULT = 0xFF	/**< Radio is located in Library Default Region EU. 2 Channel region. **/
// } ZW_Region_t;


// ---------------------------------------------------------------------------
// ZWave/API/ZW_basis_api.h
// ---------------------------------------------------------------------------

#define APPLICATION_NODEINFO_NOT_LISTENING            0x00
#define APPLICATION_NODEINFO_LISTENING                0x01
#define APPLICATION_NODEINFO_OPTIONAL_FUNC            0x02 // Only added here. Not present in ZW_basis_api.h
#define APPLICATION_FREQ_LISTENING_MODE_1000ms        0x10
#define APPLICATION_FREQ_LISTENING_MODE_250ms         0x20

// ---------------------------------------------------------------------------
// ZAF/ApplicationUtilities/ZW_TransportSecProtocol.h
// ---------------------------------------------------------------------------

#define APPL_NODEPARM_MAX       35

// ---------------------------------------------------------------------------
// ZAF/ApplicationUtilities/ZAF_file_ids.h
// ---------------------------------------------------------------------------

#define ZAF_FILE_ID_BASE             (0x51000)
#define ZAF_FILE_ID_APP_VERSION      (ZAF_FILE_ID_BASE + 0)
#define ZAF_FILE_SIZE_APP_VERSION    (sizeof(uint32_t))

// ---------------------------------------------------------------------------
// Apps/SerialAPI/config_app.h
// ---------------------------------------------------------------------------

#define APP_VERSION 7
#define APP_REVISION 00
#define APP_PATCH 00

// ---------------------------------------------------------------------------
// Apps/SerialAPI/serialapi_file.c
// ---------------------------------------------------------------------------

typedef struct
{
  uint8_t listening;  
  uint8_t generic;  
  uint8_t specific;
} SApplicationSettings;

#define FILE_ID_APPLICATIONSETTINGS        102
#define FILE_SIZE_APPLICATIONSETTINGS      (sizeof(SApplicationSettings))

typedef struct
{
  uint8_t UnSecureIncludedCCLen;
  uint8_t UnSecureIncludedCC[APPL_NODEPARM_MAX];  
  uint8_t SecureIncludedUnSecureCCLen;
  uint8_t SecureIncludedUnSecureCC[APPL_NODEPARM_MAX];
  uint8_t SecureIncludedSecureCCLen;
  uint8_t SecureIncludedSecureCC[APPL_NODEPARM_MAX];
} SApplicationCmdClassInfo;

#define FILE_ID_APPLICATIONCMDINFO         103
#define FILE_SIZE_APPLICATIONCMDINFO       (sizeof(SApplicationCmdClassInfo))

typedef struct
{
  /* Size of the ZW_Region_t enum is 8 bits when compiled for Gecko.
   * To ensure rfRegion is 8 bits on any platform we force the type
   * to uint8_t here.
   */
  uint8_t /* ZW_Region_t */ rfRegion;
  int8_t      iTxPower;
  int8_t      ipower0dbmMeasured;
} SApplicationConfiguration_prior_7_15_3;

typedef struct
{
  /* Size of the ZW_Region_t enum is 8 bits when compiled for Gecko.
   * To ensure rfRegion is 8 bits on any platform we force the type
   * to uint8_t here.
   */
  uint8_t /* ZW_Region_t */ rfRegion;
  int8_t      iTxPower;
  int8_t      ipower0dbmMeasured;
  uint8_t     enablePTI;
  int16_t     maxTxPower;
} SApplicationConfiguration_prior_7_18_1;

//When compiled for Linux this struct can get padding bytes that are not there
//when compiled for Gecko.
#pragma pack(1)
typedef struct
{
  /* Size of the ZW_Region_t enum is 8 bits when compiled for Gecko.
   * To ensure rfRegion is 8 bits on any platform we force the type
   * to uint8_t here.
   */
  uint8_t /* ZW_Region_t */ rfRegion;
  int16_t     iTxPower;
  int16_t     ipower0dbmMeasured;
  uint8_t     enablePTI;
  int16_t     maxTxPower;
} SApplicationConfiguration;

#define FILE_ID_APPLICATIONCONFIGURATION   104
#define FILE_SIZE_APPLICATIONCONFIGURATION_prior_7_15_3 (sizeof(SApplicationConfiguration_prior_7_15_3))
#define FILE_SIZE_APPLICATIONCONFIGURATION_prior_7_18_1 (sizeof(SApplicationConfiguration_prior_7_18_1))
#define FILE_SIZE_APPLICATIONCONFIGURATION (sizeof(SApplicationConfiguration))

#define APPL_DATA_FILE_SIZE            512
typedef struct
{
  uint8_t extNvm[APPL_DATA_FILE_SIZE];
} SApplicationData;

#define FILE_ID_APPLICATIONDATA            200
#define FILE_SIZE_APPLICATIONDATA          (sizeof(SApplicationData))

#endif // _ZWAVE_CONTROLLER_APPLICATION_INFO_STORAGE_H
