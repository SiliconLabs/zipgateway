/* © 2019 Silicon Laboratories Inc. */
#ifndef _ZWAVE_CONTROLLER_NETWORK_INFO_STORAGE_H
#define _ZWAVE_CONTROLLER_NETWORK_INFO_STORAGE_H

#include <stdint.h>
#include "ZW_nodemask_api.h"

// ---------------------------------------------------------------------------
// ZWave/API/config_lib.h
// ---------------------------------------------------------------------------

#define ZW_VERSION_MAJOR 7
#define ZW_VERSION_MINOR 11
#define ZW_VERSION_PATCH 0

// ---------------------------------------------------------------------------
// ZWave/API/ZW_controller_api.h
// ---------------------------------------------------------------------------

/* Defines for ZW_GetControllerCapabilities */
#define CONTROLLER_IS_SECONDARY                 0x01
#define CONTROLLER_ON_OTHER_NETWORK             0x02
#define CONTROLLER_NODEID_SERVER_PRESENT        0x04
#define CONTROLLER_IS_REAL_PRIMARY              0x08
#define CONTROLLER_IS_SUC                       0x10
#define NO_NODES_INCUDED                        0x20

// ---------------------------------------------------------------------------
// ZWave/ZW_transport.h
// ---------------------------------------------------------------------------

/* Listening in nodeinfo capabilitity */
#define ZWAVE_NODEINFO_LISTENING_SUPPORT         0x80

/* Optional Functionality bit in nodeinfo security field */
#define ZWAVE_NODEINFO_OPTIONAL_FUNC             0x80

// ---------------------------------------------------------------------------
// ZWave/ZW_controller_network_info_storage.h
// ---------------------------------------------------------------------------

#define SUC_UPDATE_NODEPARM_MAX  20
#define SUC_MAX_UPDATES          64

typedef struct _ex_nvm_nodeinfo_
{
  uint8_t capability;       /* Network capabilities */
  uint8_t security;         /* Network security */
  uint8_t reserved;
  uint8_t generic;          /* Generic Device Type */
  uint8_t specific;         /* Specific Device Type */
} EX_NVM_NODEINFO;

typedef struct _suc_update_entry_struct_
{
  uint8_t      NodeID;
  uint8_t      changeType;
  uint8_t      nodeInfo[SUC_UPDATE_NODEPARM_MAX];
} SUC_UPDATE_ENTRY_STRUCT;

typedef struct _routecache_line_
{
  uint8_t repeaterList[MAX_REPEATERS];
  uint8_t routecacheLineConfSize;
} ROUTECACHE_LINE;


// ---------------------------------------------------------------------------
// ZWave/ZW_controller_network_info_storage.c
// ---------------------------------------------------------------------------

typedef struct
{
  EX_NVM_NODEINFO NodeInfo;
  CLASSIC_NODE_MASK_TYPE neighboursInfo;
  uint8_t ControllerSucUpdateIndex;
} SNodeInfo;

typedef struct
{
  uint8_t packedInfo;
  uint8_t generic;
  uint8_t specific;
} SLRNodeInfo;

#define FILE_ID_NODEINFO                (0x50100)
#define FILE_SIZE_NODEINFO              (sizeof(SNodeInfo))

typedef struct
{
  ROUTECACHE_LINE  routeCache;
  ROUTECACHE_LINE  routeCacheNlwrSr;
} SNodeRouteCache;


#define FILE_ID_NODEROUTECAHE           (0x50400)
#define FILE_SIZE_NODEROUTECAHE         (sizeof(SNodeRouteCache))


#define FILE_ID_ZW_VERSION              (0x50000)
#define FILE_SIZE_ZW_VERSION            (sizeof(uint32_t))

typedef struct
{
  uint8_t                          PreferredRepeater[MAX_CLASSIC_NODEMASK_LENGTH];
} SPreferredRepeaters;

#define FILE_ID_PREFERREDREPEATERS      (0x50002)
#define FILE_SIZE_PREFERREDREPEATERS    (sizeof(SPreferredRepeaters))

typedef struct
{
  SUC_UPDATE_ENTRY_STRUCT       aSucNodeList[SUC_MAX_UPDATES];
} SSucNodeList;

#define FILE_ID_SUCNODELIST             (0x50003)
#define FILE_SIZE_SUCNODELIST           (sizeof(SSucNodeList))

typedef struct
{
  uint8_t                       HomeID[HOMEID_LENGTH];
  uint8_t                       NodeID;
  uint8_t                       LastUsedNodeId;
  uint8_t                       StaticControllerNodeId;
  uint8_t                       SucLastIndex;
  uint8_t                       ControllerConfiguration;
  uint8_t                       SucAwarenessPushNeeded;
  uint8_t                       MaxNodeId;
  uint8_t                       ReservedId;
  uint8_t                       SystemState;
} SControllerInfo_NVM711;

typedef struct
{
  uint8_t                       HomeID[HOMEID_LENGTH];
  uint16_t                      NodeID;
  uint16_t                      StaticControllerNodeId;
  uint16_t                      LastUsedNodeId_LR;
  uint8_t                       LastUsedNodeId;
  uint8_t                       SucLastIndex;
  uint16_t                      MaxNodeId_LR;
  uint8_t                       MaxNodeId;
  uint8_t                       ControllerConfiguration;
  uint16_t                      ReservedId_LR;
  uint8_t                       ReservedId;
  uint8_t                       SystemState;
  uint8_t                       PrimaryLongRangeChannelId;
  uint8_t                       DcdcConfig;
} SControllerInfo_NVM715;

#define FILE_ID_CONTROLLERINFO            (0x50004)
#define FILE_SIZE_CONTROLLERINFO_NVM711   (sizeof(SControllerInfo_NVM711))
#define FILE_SIZE_CONTROLLERINFO_NVM715   (sizeof(SControllerInfo_NVM715))

#define FILE_ID_NODE_STORAGE_EXIST        (0x50005)
#define FILE_SIZE_NODE_STORAGE_EXIST      (sizeof(CLASSIC_NODE_MASK_TYPE))

#define FILE_ID_APP_ROUTE_LOCK_FLAG       (0x50006)
#define FILE_SIZE_APP_ROUTE_LOCK_FLAG     (sizeof(CLASSIC_NODE_MASK_TYPE))

#define FILE_ID_ROUTE_SLAVE_SUC_FLAG      (0x50007)
#define FILE_SIZE_ROUTE_SLAVE_SUC_FLAG    (sizeof(CLASSIC_NODE_MASK_TYPE))

#define FILE_ID_SUC_PENDING_UPDATE_FLAG   (0x50008)
#define FILE_SIZE_SUC_PENDING_UPDATE_FLAG (sizeof(CLASSIC_NODE_MASK_TYPE))

#define FILE_ID_BRIDGE_NODE_FLAG          (0x50009)
#define FILE_SIZE_BRIDGE_NODE_FLAG        (sizeof(CLASSIC_NODE_MASK_TYPE))

#define FILE_ID_PENDING_DISCOVERY_FLAG    (0x5000A)
#define FILE_SIZE_PENDING_DISCOVERY_FLAG  (sizeof(CLASSIC_NODE_MASK_TYPE))

#define FILE_ID_NODE_ROUTECACHE_EXIST     (0x5000B)
#define FILE_SIZE_NODE_ROUTECACHE_EXIST   (sizeof(CLASSIC_NODE_MASK_TYPE))

#define FILE_ID_LRANGE_NODE_EXIST         (0x5000C)
#define FILE_SIZE_LRANGE_NODE_EXIST       (sizeof(LR_NODE_MASK_TYPE))

/*
Protocol S2 not used for controllers (gateway links to libs2 instead)
CtrlStorageInit() will create empty copies of all S2 files
Here we hardcode sizes to 0 to avoid pulling in S2 struct definition
*/

#define FILE_ID_S2_KEYS                   (0x50010)
#define FILE_SIZE_S2_KEYS                 0 // (sizeof(Ss2_keys))

#define FILE_ID_S2_KEYCLASSES_ASSIGNED    (0x50011)
#define FILE_SIZE_S2_KEYCLASSES_ASSIGNED  0 // (sizeof(Ss2_keyclassesAssigned))

#define FILE_ID_S2_MPAN                   (0x50012)
#define FILE_SIZE_S2_MPAN                 0 // (sizeof(mpan_file))

#define FILE_ID_S2_SPAN                   (0x50013)
#define FILE_SIZE_S2_SPAN                 0 // (sizeof(span_file))

/* In version 1 of the nvm format (initial nvm format was version 0), the route caches and nodeinfos are grouped in four/eight elements pr file.*/
#define NODEINFOS_PER_FILE                 4
#define FILE_ID_NODEINFO_V1                (0x50200)
#define FILE_SIZE_NODEINFO_V1              (sizeof(SNodeInfo) * NODEINFOS_PER_FILE)

#define NODEROUTECACHES_PER_FILE           8
#define NUMBER_OF_NODEROUTECACHE_V1_FILES  29  // supporting 232 nodes
#define FILE_ID_NODEROUTECAHE_V1           (0x51400)
#define FILE_SIZE_NODEROUTECAHE_V1         (sizeof(SNodeRouteCache) * NODEROUTECACHES_PER_FILE)

/* In version 2 of the nvm format (initial nvm format was version 0), lr node info is added.*/
#define LR_NODEINFOS_PER_FILE              50
#define FILE_ID_NODEINFO_LR                (0x50800)
#define FILE_SIZE_NODEINFO_LR              (sizeof(SLRNodeInfo) * LR_NODEINFOS_PER_FILE)

#define FILE_ID_LR_TX_POWER_V2             (0x50014)
#define LR_TX_POWER_PER_FILE_V2            64

#define FILE_SIZE_LR_TX_POWER              32

/* In version 3 the file format for LR_TX_POWER changed. */
#define FILE_ID_LR_TX_POWER_V3             (0x52000)
#define LR_TX_POWER_PER_FILE_V3            32

#endif // _ZWAVE_CONTROLLER_NETWORK_INFO_STORAGE_H
