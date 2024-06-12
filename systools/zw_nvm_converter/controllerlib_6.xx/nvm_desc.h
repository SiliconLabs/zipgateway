/* © 2019 Silicon Laboratories Inc. */
#if !defined(NVM_DESC_H)
#define NVM_DESC_H

#include<stdint.h>

typedef struct 
{
    const char* nvm_var;
    const char* nvm_type;
    uint16_t nvm_offset;
    int nvm_array_size;
} nvm_desc_t;

#define ZW_CONTROLLER

#define MAX_NODEMASK_LENGTH 29
#define ZW_MAX_NODES 232
#define HOMEID_LENGTH 4

#define SUC_CONTROLLER_LIST_SIZE             232
#define SUC_MAX_UPDATES                      64

#define MAX_REPEATERS 4
#define SUC_UPDATE_NODEPARM_MAX  20   /* max. number of command classes in update list */

/* NVM is 16KB, 32KB or even more (you decide the size of your SPI EEPROM or FLASH chip) */
/* Use only a reasonable amount of it for host application */
#define NVM_SERIALAPI_HOST_SIZE 2048

#define POWERLEVEL_CHANNELS   3
#define APPL_NODEPARM_MAX       35
#define RTC_TIMER_SIZE 16 
#define TOTAL_RTC_TIMER_MAX  (8+2) /* max number of active RTC timers */

#define CONFIGURATION_VALID_0               0x54
#define CONFIGURATION_VALID_1               0xA5
#define ROUTECACHE_VALID                    0x4A
#define MAGIC_VALUE       0x42


#pragma pack(push)
#pragma pack()
#include "ZW_nvm_descriptor.h"

typedef struct  __attribute__((packed, aligned(1))) _ex_nvm_nodeinfo_
{
  BYTE      capability;             /* Network capabilities */
  BYTE      security;               /* Network security */
  BYTE      reserved;
//  NODE_TYPE nodeType;               /* Basic, Generic and Specific Device Type */
  BYTE generic;                     /* Generic Device Type */
  BYTE specific;                    /* Specific Device Type */
} EX_NVM_NODEINFO;

typedef struct __attribute__((packed, aligned(1))) _routecache_line_
{
  BYTE repeaterList[MAX_REPEATERS]; /* List of repeaters */
  BYTE routecacheLineConfSize;
} ROUTECACHE_LINE ;

typedef struct  __attribute__((packed, aligned(1))) _suc_update_entry_struct_
{
  BYTE      NodeID;
  BYTE      changeType;
  BYTE      nodeInfo[SUC_UPDATE_NODEPARM_MAX]; /* Device status */
} SUC_UPDATE_ENTRY_STRUCT;

#pragma pack(pop)

#endif // NVM_DESC_H
