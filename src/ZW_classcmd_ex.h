/* © 2014 Silicon Laboratories Inc.
 */

#ifndef ZW_CLASSCMD_EX_H_
#define ZW_CLASSCMD_EX_H_
#include <TYPES.H>
#include "ZW_classcmd.h"
#include "contiki-conf.h"
#include "sys/cc.h"
#include <provisioning_list_types.h>

#define ZW_FRAME(name, params) \
  typedef struct _ZW_##name##_FRAME_ { \
    BYTE      cmdClass; \
    BYTE      cmd;      \
    params              \
  } CC_ALIGN_PACK ZW_##name##_FRAME; \


#define ZW_NMFRAME_EX(name, params) \
  typedef struct _ZW_##name##_FRAME_EX_ { \
    BYTE      cmdClass; \
    BYTE      cmd;      \
    BYTE      seqNo;    \
    params              \
  } CC_ALIGN_PACK ZW_##name##_FRAME_EX; \

#define ZW_NMFRAME(name, params) \
  typedef struct _ZW_##name##_FRAME_ { \
    BYTE      cmdClass; \
    BYTE      cmd;      \
    BYTE      seqNo;    \
    params              \
  } CC_ALIGN_PACK ZW_##name##_FRAME; \


#define DEFAULT_SET_DONE    ADD_NODE_STATUS_DONE
#define DEFAULT_SET_BUSY    ADD_NODE_STATUS_FAILED



#define RETURN_ROUTE_ASSIGN                                              0x0D
#define RETURN_ROUTE_ASSIGN_COMPLETE                                     0x0E
#define RETURN_ROUTE_DELETE                                              0x0F
#define RETURN_ROUTE_DELETE_COMPLETE                                     0x10

#define COMMAND_CLASS_ZIP_ND                                             0x58
#define ZWAVE_LR_CHANNEL_CONFIGURATION_SET                               0x0A
#define ZWAVE_LR_CHANNEL_CONFIGURATION_GET                               0x0D
#define ZWAVE_LR_CHANNEL_CONFIGURATION_REPORT                            0x0E

/*Last working route report */
#define ROUTE_TYPE_STATIC 0
#define ROUTE_TYPE_DYNAMIC 1

#define ZIP_VERSION_V5 5

#if 0
ZW_NMFRAME(RETURN_ROUTE_ASSIGN,
    BYTE  sourceNode;
    BYTE  destinationNode; )

ZW_NMFRAME(RETURN_ROUTE_ASSIGN_COMPLETE,
  BYTE  status;  )

ZW_NMFRAME(RETURN_ROUTE_DELETE,
  BYTE  nodeID;  )

ZW_NMFRAME(RETURN_ROUTE_DELETE_COMPLETE,
  BYTE  status;  )
#endif 


#define START_FAILED_NODE_REPLACE 0x1 //ADD_NODE_ANY CODE
#define START_FAILED_NODE_REPLACE_S2 ADD_NODE_ANY_S2 //ADD_NODE_ANY2 CODE

#define STOP_FAILED_NODE_REPLACE  0x5 //ADD_NODE_STOP CODE



/************************************************************/
/* Firmware Update v5 */
/************************************************************/
#define FIRMWARE_MD_GET_V5 FIRMWARE_MD_GET_V3
#define FIRMWARE_MD_REPORT_V5 FIRMWARE_MD_REPORT_V3
#define COMMAND_CLASS_FIRMWARE_UPDATE_MD_V5 COMMAND_CLASS_FIRMWARE_UPDATE_MD_V3
   
typedef struct _ZW_FIRMWARE_UPDATE_ACTIVATION_STATUS_REPORT_V5_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      manufacturerId1;              /* MSB */
    BYTE      manufacturerId2;              /* LSB */
    BYTE      firmwareId1;                  /* MSB */
    BYTE      firmwareId2;                  /* LSB */
    BYTE      checksum1;                    /* MSB */
    BYTE      checksum2;                    /* LSB */
    BYTE      firmwareTarget;               /**/
    BYTE      firmwareUpdateStatus;         /**/
    BYTE      hardwareVersion;              /**/
} ZW_FIRMWARE_UPDATE_ACTIVATION_STATUS_REPORT_V5_FRAME;

typedef struct _ZW_FIRMWARE_MD_REPORT_1ID_V5_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      manufacturerId1;              /* MSB */
    BYTE      manufacturerId2;              /* LSB */
    BYTE      firmware0Id1;                 /* MSB */
    BYTE      firmware0Id2;                 /* LSB */
    BYTE      firmware0Checksum1;           /* MSB */
    BYTE      firmware0Checksum2;           /* LSB */
    BYTE      firmwareUpgradable;           /**/
    BYTE      numberOfFirmwareTargets;      /**/
    BYTE      maxFragmentSize1;             /* MSB */
    BYTE      maxFragmentSize2;             /* LSB */
    BYTE      firmwareId11;                  /* MSB */
    BYTE      firmwareId12;                  /* LSB */
    BYTE      hardwareVersion;              /**/
} ZW_FIRMWARE_MD_REPORT_1ID_V5_FRAME;

typedef struct _ZW_GATEWAY_PEER_REPORT_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      peerProfile;                  /* PeerProfile  */
    BYTE      actualPeers;                  /* PeerProfile  */
    BYTE      ipv6Address1;                 /* MSB */
  BYTE      ipv6Address2;
  BYTE      ipv6Address3;
  BYTE      ipv6Address4;
  BYTE      ipv6Address5;
  BYTE      ipv6Address6;
  BYTE      ipv6Address7;
  BYTE      ipv6Address8;
  BYTE      ipv6Address9;
  BYTE      ipv6Address10;
  BYTE      ipv6Address11;
  BYTE      ipv6Address12;
  BYTE      ipv6Address13;
  BYTE      ipv6Address14;
  BYTE      ipv6Address15;
  BYTE      ipv6Address16;                /* LSB */
  BYTE    port1;            /*Port MSB*/
  BYTE    port2;            /*Port LSB */
  BYTE      peerLength;                   /* Peer Length */
  BYTE      peerName[1];                  /* Peer Name */
} ZW_GATEWAY_PEER_REPORT_FRAME;

typedef struct _ZW_GATEWAY_PEER_LOCK_SET_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      showEnable;                  /* Show/Enable/Disable Bitmask  */
} ZW_GATEWAY_PEER_LOCK_SET_FRAME;

typedef struct _ZW_GATEWAY_UNSOLICITED_DESTINATION_SET_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE    unsolicit_ipv6_addr[16];      /* Unsolicited IPv6 address */
    BYTE      unsolicitPort1;               /* Unsolicited Port Number MSB */
    BYTE      unsolicitPort2;               /* Unsolicited Port Number LSB */

} ZW_GATEWAY_UNSOLICITED_DESTINATION_SET_FRAME;

typedef struct _ZW_GATEWAY_UNSOLICITED_DESTINATION_REPORT_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE    unsolicit_ipv6_addr[16];      /* Unsolicited IPv6 address */
    BYTE      unsolicitPort1;               /* Unsolicited Port Number MSB */
    BYTE      unsolicitPort2;               /* Unsolicited Port Number LSB */

} ZW_GATEWAY_UNSOLICITED_DESTINATION_REPORT_FRAME;

typedef struct _ZW_GATEWAY_APP_NODE_INFO_SET_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE    payload[1];             /* Variable Payload */

} ZW_GATEWAY_APP_NODE_INFO_SET_FRAME;

typedef struct _ZW_GATEWAY_APP_NODE_INFO_GET_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */

} ZW_GATEWAY_APP_NODE_INFO_GET_FRAME;

typedef struct _ZW_GATEWAY_APP_NODE_INFO_REPORT_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE    payload[1];             /* Variable Payload */

} ZW_GATEWAY_APP_NODE_INFO_REPORT_FRAME;


typedef struct _ZW_GATEWAY_CONFIGURATION_SET_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      payload[1];                   /*  Start of Confiuration Parameters */
}ZW_GATEWAY_CONFIGURATION_SET;

typedef struct _ZW_GATEWAY_CONFIGURATION_STATUS_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      status;                       /* configuration status status*/
}ZW_GATEWAY_CONFIGURATION_STATUS;

typedef struct _ZW_GATEWAY_CONFIGURATION_GET_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
}ZW_GATEWAY_CONFIGURATION_GET;

typedef struct _ZW_GATEWAY_CONFIGURATION_REPORT_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      payload[1];                   /*  Start of Confiuration Parameters */
}ZW_GATEWAY_CONFIGURATION_REPORT;


typedef struct _ZW_GATEWAY_PEER_SET_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      peerProfile;                 /* PeerProfile  */
    BYTE      ipv6Address1;                 /* MSB */
        BYTE      ipv6Address2;
        BYTE      ipv6Address3;
        BYTE      ipv6Address4;
        BYTE      ipv6Address5;
        BYTE      ipv6Address6;
        BYTE      ipv6Address7;
        BYTE      ipv6Address8;
        BYTE      ipv6Address9;
        BYTE      ipv6Address10;
        BYTE      ipv6Address11;
        BYTE      ipv6Address12;
        BYTE      ipv6Address13;
        BYTE      ipv6Address14;
        BYTE      ipv6Address15;
        BYTE      ipv6Address16;                /* LSB */
        BYTE      port1;                                                /*Port MSB*/
        BYTE      port2;                                                /*Port LSB */
        BYTE      peerNameLength;                  /* Peer Length */
        BYTE      peerName[1];                 /* Peer Name */
} ZW_GATEWAY_PEER_SET_FRAME;


#define ZWAVEPLUS_INFO_VERSION_V2      0x02

#define ZWAVEPLUS_VERSION				0x01
#define ZWAVEPLUS_VERSION_V2			0x02

/*FROM SDS12083*/
#define ICON_TYPE_UNASSIGNED                                0x0000 // MUST NOT be used by any product
#define ICON_TYPE_GENERIC_GATEWAY 0x0500

typedef union{
  ZW_COMMON_FRAME                                       ZW_Common;
  ZW_GATEWAY_MODE_SET_FRAME  ZW_GatewayModeSet;
  ZW_GATEWAY_PEER_SET_FRAME ZW_GatewayPeerSet;
  ZW_GATEWAY_PEER_LOCK_SET_FRAME ZW_GatewayLockSet;
  ZW_GATEWAY_UNSOLICITED_DESTINATION_SET_FRAME ZW_GatewayUnsolicitDstSet;
  ZW_GATEWAY_PEER_GET_FRAME ZW_GatewayPeerGet;

  BYTE bPadding[META_DATA_MAX_DATA_SIZE];
} ZW_APPLICATION_TX_BUFFER_EX;

#define GATEWAY_UNREGISTER                                                                 0x05


#define NODE_LIST_REPORT_LATEST_LIST 0
#define NODE_LIST_REPORT_NO_GUARANTEE 1



/* MAILBOX CC */

#define MAILBOX_VERSION_V2 0x02
#define MAILBOX_LAST 0x4


typedef enum {
  DISABLE_MAILBOX=0,
  ENABLE_MAILBOX_SERVICE=1,
  ENABLE_MAILBOX_PROXY_FORWARDING=2,
} mailbox_configuration_mode_t;


typedef enum {
  MAILBOX_SERVICE_SUPPORTED=0x8,
  MAILBOX_PROXY_SUPPORTED=0x10,
} mailbox_configuration_supported_mode_t;

ZW_FRAME(MAILBOX_QUEUE,
    BYTE param1;
    BYTE handle;
    BYTE mailbox_entry[1];
    );


ZW_FRAME(MAILBOX_NODE_FAILING,
    BYTE node_ip[16];;
    );


typedef enum {
  MAILBOX_PUSH,
  MAILBOX_POP,
  MAILBOX_WAITING,
  MAILBOX_PING,
  MAILBOX_ACK,
  MAILBOX_NAK,
  MAILBOX_QUEUE_FULL,
} mailbox_queue_mode_t;
#define MAILBOX_POP_LAST_BIT 0x8
#define FIRMWARE_UPDATE_MD_PREPARE_GET                                                0x0A
#define FIRMWARE_UPDATE_MD_PREPARE_REPORT                                             0x0B

/**
 * Ask the receiver to prepare the target firmware for retrieval
 */
/************************************************************/
/* Firmware Update Md Prepare Get V5 command class structs */
/************************************************************/
typedef struct _ZW_FIRMWARE_UPDATE_MD_PREPARE_GET_V5_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      manufacturerId1;              /* MSB */
    BYTE      manufacturerId2;              /* LSB */
    BYTE      firmwareId1;                  /* MSB */
    BYTE      firmwareId2;                  /* LSB */
    BYTE      firmwareTarget;               /**/
    BYTE      fragmentSize1;                /* MSB */
    BYTE      fragmentSize2;                /* LSB */
    BYTE      hardwareVersion;              /**/
} ZW_FIRMWARE_UPDATE_MD_PREPARE_GET_V5_FRAME;

/**
 * Ask the receiver to prepare the target firmware for retrieval
 */
/************************************************************/
/* Firmware Update Md Prepare Report V5 command class structs */
/************************************************************/
typedef struct _ZW_FIRMWARE_UPDATE_MD_PREPARE_REPORT_V5_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      status;                       /**/
    BYTE      firmwareChecksum1;            /* MSB */
    BYTE      firmwareChecksum2;            /* LSB */
} ZW_FIRMWARE_UPDATE_MD_PREPARE_REPORT_V5_FRAME;

#define COMMAND_CLASS_NETWORK_MANAGEMENT_INSTALLATION_MAINTENANCE 0x67


typedef enum {
  ROUTE_CHANGES=0,
  TRANSMISSION_COUNT=1,
  NEIGHBORS=2,
  PACKET_ERROR_COUNT=3,
  TANSMISSION_TIME_SUM=4,
  TANSMISSION_TIME_SUM2=5,
} statistics_tlv;

#define  IMA_NODE_SPEED_96 1
#define  IMA_NODE_SPEED_40 2
#define  IMA_NODE_SPEED_100 4

#define IMA_NODE_REPEATER 0x80

/* ZIP Packet Header Extension TLV types (also called options in spec) */
#define ZIP_PACKET_EXT_EXPECTED_DELAY   1
#define INSTALLATION_MAINTENANCE_GET    2
#define INSTALLATION_MAINTENANCE_REPORT 3
#define ENCAPSULATION_FORMAT_INFO       4
#define ZWAVE_MULTICAST_ADDRESSING      5
#define EXT_ZIP_PACKET_HEADER_LENGTH    6
#define MULTICAST_DESTINATION_ADDRESS   7
#define MULTICAST_ACK                   8

#define ZIP_EXT_HDR_OPTION_CRITICAL_FLAG 0x80
#define ZIP_EXT_HDR_OPTION_TYPE_MASK     0x7f

typedef enum {
  EFI_SEC_LEVEL_NONE =0x0,
  EFI_SEC_S0 = 0x80,
  EFI_SEC_S2_UNAUTHENTICATED = 0x01,
  EFI_SEC_S2_AUTHENTICATED = 0x02,
  EFI_SEC_S2_ACCESS = 0x4,
} efi_security_level;

#define EFI_FLAG_CRC16    1
#define EFI_FLAG_MULTICMD 2

#define COMMAND_ZIP_KEEP_ALIVE 0x3
#define ZIP_KEEP_ALIVE_ACK_REQUEST  0x80
#define ZIP_KEEP_ALIVE_ACK_RESPONSE 0x40


/***************** Network management CC v 2  ******************/
#define NETWORK_MANAGEMENT_BASIC_VERSION_V2 2
#define NETWORK_MANAGEMENT_PROXY_VERSION_V2 2
#define NETWORK_MANAGEMENT_PROXY_VERSION_V3 3
#define NETWORK_MANAGEMENT_PROXY_VERSION_V4 4
#define NETWORK_MANAGEMENT_INCLUSION_VERSION_V2 2
#define NETWORK_MANAGEMENT_INCLUSION_VERSION_V3 3
#define NETWORK_MANAGEMENT_INCLUSION_VERSION_V4 4

#define NODE_ADD_KEYS_REPORT 0x11
#define NODE_ADD_KEYS_SET    0x12
#define NODE_ADD_DSK_REPORT  0x13
#define NODE_ADD_DSK_SET     0x14


/***************** Network management CC v 3  ******************/
#define COMMAND_SMART_START_JOIN_STARTED_REPORT  0x15 /* FIXME: Get official cmd no. assigned */
#define COMMAND_SMART_START_ADV_JOIN_MODE_SET    0x16 /* FIXME: Get official cmd no. assigned */
#define COMMAND_SMART_START_ADV_JOIN_MODE_GET    0x17 /* FIXME: Get official cmd no. assigned */
#define COMMAND_SMART_START_ADV_JOIN_MODE_REPORT 0x18 /* FIXME: Get official cmd no. assigned */
#define COMMAND_INCLUDED_NIF_REPORT              0x19 /* FIXME: Get official cmd no. assigned */

#define STATUS_DSK_ADD_STARTED                       1
#define STATUS_INCLUSION_REQUEST_ALREADY_INCLUDED    2


/**************** Network Management IMA ************************/
#define RSSI_GET                                                                    0x07
#define RSSI_REPORT                                                                 0x08
#define COMMAND_S2_RESYNCHRONIZATION_EVENT                                          0x09

/****************** Provisioning List CC v1 ******************/
#define COMMAND_CLASS_PROVISIONING_LIST  0x78
#define PROVISIONING_LIST_VERSION_V1 1

#define COMMAND_PROVISION_SET         0x01
#define COMMAND_PROVISION_DELETE      0x02
#define COMMAND_PROVISION_ITER_GET    0x03
#define COMMAND_PROVISION_ITER_REPORT 0x04
#define COMMAND_PROVISION_GET         0x05
#define COMMAND_PROVISION_REPORT      0x06

#define PROVISIONING_LIST_OPT_CRITICAL_FLAG       0x01
#define PROVISIONING_LIST_OPT_TYPE_MASK           0xFE

#define PROVISIONING_LIST_DSK_LENGTH_MASK (0x1F)

#define PROVISIONING_LIST_STATUS_ADD_STARTED 1

ZW_NMFRAME_EX(INCLUSION_REQUEST,
);

ZW_NMFRAME_EX(PROVISION_SET,
    BYTE  reserved_dsk_length;
    BYTE  dsk[32];
//  BYTE  tlv_meta_data[0];
);

ZW_NMFRAME_EX(PROVISION_DELETE,
    BYTE  reserved_dsk_length;
    BYTE  dsk[32];
);

ZW_NMFRAME_EX(PROVISION_ITER_GET,
    BYTE  remaining;
);

ZW_NMFRAME_EX(PROVISION_ITER_REPORT,
    BYTE  remaining;
    BYTE  reserved_dsk_length;
    BYTE  dsk[32];
//  BYTE  tlv_meta_data[0];
);

ZW_NMFRAME_EX(PROVISION_GET,
    BYTE  reserved_dsk_length;
    BYTE  dsk[32];
);

ZW_NMFRAME_EX(PROVISION_REPORT,
    BYTE  reserved_dsk_length;
    BYTE  dsk[32];
//  BYTE  tlv_meta_data[0];
);


/* from NM basic */
#define DSK_GET              0x8
#define DSK_RAPORT           0x9

#define NODE_ADD_KEYS_SET_EX_ACCEPT_BIT 0x01
#define NODE_ADD_KEYS_SET_EX_CSA_BIT 0x02

#define NODE_ADD_DSK_SET_EX_ACCEPT_BIT  0x80
#define NODE_ADD_DSK_REPORT_DSK_LEN_MASK 0x0F
#define NODE_ADD_DSK_SET_DSK_LEN_MASK 0x0F
#define INCLUSION_REQUEST 0x10

#define DSK_GET_ADD_MODE_BIT 1

ZW_NMFRAME_EX(NODE_ADD_KEYS_REPORT,
    uint8_t request_csa;
    uint8_t requested_keys;
    );

ZW_NMFRAME_EX(NODE_ADD_KEYS_SET,
    uint8_t reserved_accept;
    uint8_t granted_keys;
    );


ZW_NMFRAME_EX(NODE_ADD_DSK_REPORT,
    uint8_t reserved_dsk_len;
    uint8_t dsk[16];
    );

ZW_NMFRAME_EX(NODE_ADD_DSK_SET,
    uint8_t accet_reserved_dsk_len;
    uint8_t dsk[2];
    );

ZW_NMFRAME_EX(DSK_GET,
    BYTE add_mode;

    );

ZW_NMFRAME_EX(DSK_RAPORT,
    BYTE add_mode;
    uint8_t dsk[16];
    );


ZW_NMFRAME_EX(LEARN_MODE_SET_STATUS,
    BYTE      status;                       /**/
    BYTE      reserved;                     /**/
    BYTE      newNodeId;
    BYTE      granted_keys;
    BYTE      kexFailType;
    BYTE      dsk[16];
    );

ZW_NMFRAME_EX(FAILED_NODE_REPLACE_STATUS,
    BYTE      status;                       /**/
    BYTE      nodeId;                       /**/
    BYTE      grantedKeys;
    BYTE      kexFailType;
);

ZW_NMFRAME_EX(SMART_START_JOIN_STARTED_REPORT,
    uint8_t reserved_dsk_len;
    uint8_t dsk[16];
    );

ZW_NMFRAME_EX(INCLUDED_NIF_REPORT,
    uint8_t  reserved_len;
    uint8_t  dsk[16];
    );

#define FIRMWARE_UPDATE_MD_VERSION_V5 5


#define COMMAND_CLASS_INCLUSION_CONTROLLER 0x74
#define INCLUSION_CONTROLLER_INITIATE 0x1
#define INCLUSION_CONTROLLER_COMPLETE 0x2

#define STEP_ID_PROXY_INCLUSION 0x1
#define STEP_ID_S0_INCLUSION 0x2
#define STEP_ID_PROXY_REPLACE 0x3

/* Step "result codes" */
#define STEP_OK                 0x1
#define STEP_USER_REJECTED      0x2
#define STEP_FAILED             0x3
#define STEP_NOT_SUPPORTED      0x4

ZW_FRAME(INCLUSION_CONTROLLER_INITIATE,
  BYTE node_id;
  BYTE step_id;
);

ZW_FRAME(INCLUSION_CONTROLLER_COMPLETE,
    BYTE step_id;
    BYTE status;
);

#define NM_MULTI_CHANNEL_END_POINT_GET             0x05
#define NM_MULTI_CHANNEL_END_POINT_REPORT          0x06
#define NM_MULTI_CHANNEL_CAPABILITY_GET            0x07
#define NM_MULTI_CHANNEL_CAPABILITY_REPORT         0x08
#define NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_GET    0x09
#define NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_REPORT 0x0A

ZW_NMFRAME(NM_MULTI_CHANNEL_END_POINT_GET,
    BYTE      nodeID;                       /**/
);

ZW_NMFRAME(NM_MULTI_CHANNEL_END_POINT_REPORT,
    BYTE      nodeID;                       /**/
    BYTE      reserved;                     /**/
    BYTE      individualEndPointCount;      /**/
    BYTE      aggregatedEndPointCount;      /**/
);

ZW_NMFRAME(NM_MULTI_CHANNEL_END_POINT_GET_V4,
    BYTE      nodeID;                       /**/
    uint8_t   extendedNodeidMSB;
    uint8_t   extendedNodeidLSB;
);

ZW_NMFRAME(NM_MULTI_CHANNEL_END_POINT_REPORT_V4,
    BYTE      nodeID;                       /**/
    BYTE      reserved;                     /**/
    BYTE      individualEndPointCount;      /**/
    BYTE      aggregatedEndPointCount;      /**/
    uint8_t   extendedNodeidMSB;
    uint8_t   extendedNodeidLSB;
);


ZW_NMFRAME(NM_MULTI_CHANNEL_CAPABILITY_GET,
    BYTE      nodeID;                       /**/
    BYTE      endpoint;                     /**/
);

ZW_NMFRAME(NM_MULTI_CHANNEL_CAPABILITY_REPORT,
    BYTE      nodeID;                       /**/
    BYTE      commandClassLength;           /**/
    BYTE      endpoint;                     /**/
    BYTE      genericDeviceClass;           /**/
    BYTE      specificDeviceClass;          /**/
    BYTE      commandClass1;                /**/
);

ZW_NMFRAME(NM_MULTI_CHANNEL_CAPABILITY_GET_V4,
    BYTE      nodeID;                       /**/
    BYTE      endpoint;                     /**/
    uint8_t   extendedNodeidMSB;
    uint8_t   extendedNodeidLSB;
);

ZW_NMFRAME(NM_MULTI_CHANNEL_CAPABILITY_REPORT_V4,
    BYTE      nodeID;                       /**/
    BYTE      commandClassLength;           /**/
    BYTE      endpoint;                     /**/
    BYTE      genericDeviceClass;           /**/
    BYTE      specificDeviceClass;          /**/
    BYTE      commandClass1;                /**/
    uint8_t   extendedNodeidMSB;
    uint8_t   extendedNodeidLSB;
);


ZW_NMFRAME(NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_GET,
    BYTE      nodeID;                       /**/
    BYTE      aggregatedEndpoint;           /**/
);

ZW_NMFRAME(NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_REPORT,
    BYTE      nodeID;                       /**/
    BYTE      aggregatedEndpoint;           /**/
    BYTE      memberCount;
    BYTE      memberEndpoint1;
);

ZW_NMFRAME(NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_GET_V4,
    BYTE      nodeID;                       /**/
    BYTE      aggregatedEndpoint;           /**/
    uint8_t   extendedNodeidMSB;
    uint8_t   extendedNodeidLSB;
);

ZW_NMFRAME(NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_REPORT_V4,
    BYTE      nodeID;                       /**/
    BYTE      aggregatedEndpoint;           /**/
    BYTE      memberCount;
    BYTE      memberEndpoint1;
    uint8_t   extendedNodeidMSB;
    uint8_t   extendedNodeidLSB;
);
/* NETWORK_MANAGEMENT_INSTALLATION_MANTENANCE */
#define PRIORITY_ROUTE_SET LAST_WORKING_ROUTE_SET
#define PRIORITY_ROUTE_GET LAST_WORKING_ROUTE_GET
#define PRIORITY_ROUTE_REPORT LAST_WORKING_ROUTE_REPORT

#define UPDATE_STATE_INCLUDED_NODE_INFO_RECEIVED  0x86


/* Command class Indicator V3
 *
 * NB: All defines below are fetched from svn ZW_classcmd.h
 *     All should be deleted when ZW_classcmd.h in zipgateway
 *     repository is updated
 */

// TODO: Must add indicator structures to ZW_FRAME_COLLECTION_MACRO1

#define COMMAND_CLASS_INDICATOR_V3                                                       0x87

/* Indicator command class commands */
#define INDICATOR_VERSION_V3                                                             0x03
#define INDICATOR_GET_V3                                                                 0x02
#define INDICATOR_REPORT_V3                                                              0x03
#define INDICATOR_SET_V3                                                                 0x01
#define INDICATOR_SUPPORTED_GET_V3                                                       0x04
#define INDICATOR_SUPPORTED_REPORT_V3                                                    0x05


/* Values used for Indicator Get command */
#define INDICATOR_GET_NA_V3                                                              0x00
#define INDICATOR_GET_NODE_IDENTIFY_V3                                                   0x50

/* Values used for Indicator Report command */
#define INDICATOR_REPORT_PROPERTIES1_INDICATOR_OBJECT_COUNT_MASK_V3                      0x1F
#define INDICATOR_REPORT_PROPERTIES1_RESERVED_MASK_V3                                    0xE0
#define INDICATOR_REPORT_PROPERTIES1_RESERVED_SHIFT_V3                                   0x05

#define INDICATOR_REPORT_NA_V3                                                           0x00
#define INDICATOR_REPORT_NODE_IDENTIFY_V3                                                0x50

#define INDICATOR_REPORT_MULTILEVEL_V3                                                   0x01
#define INDICATOR_REPORT_BINARY_V3                                                       0x02
#define INDICATOR_REPORT_ON_OFF_PERIOD_V3                                                0x03
#define INDICATOR_REPORT_ON_OFF_CYCLES_V3                                                0x04
#define INDICATOR_REPORT_ON_TIME_V3                                                      0x05
#define INDICATOR_REPORT_LOW_POWER_V3                                                    0x10


/* Values used for Indicator Set command */
#define INDICATOR_SET_PROPERTIES1_INDICATOR_OBJECT_COUNT_MASK_V3                         0x1F
#define INDICATOR_SET_PROPERTIES1_RESERVED_MASK_V3                                       0xE0
#define INDICATOR_SET_PROPERTIES1_RESERVED_SHIFT_V3                                      0x05

#define INDICATOR_SET_NA_V3                                                              0x00
#define INDICATOR_SET_NODE_IDENTIFY_V3                                                   0x50

#define INDICATOR_SET_MULTILEVEL_V3                                                      0x01
#define INDICATOR_SET_BINARY_V3                                                          0x02
#define INDICATOR_SET_ON_OFF_PERIOD_V3                                                   0x03
#define INDICATOR_SET_ON_OFF_CYCLES_V3                                                   0x04
#define INDICATOR_SET_ON_TIME_V3                                                         0x05
#define INDICATOR_SET_LOW_POWER_V3                                                       0x10

/* Values used for Indicator Supported Get command */
#define INDICATOR_SUPPORTED_GET_NA_V3                                                    0x00
#define INDICATOR_SUPPORTED_GET_NODE_IDENTIFY_V3                                         0x50

/* Values used for Indicator Supported Report command */
#define INDICATOR_SUPPORTED_REPORT_NA_V3                                                 0x00
#define INDICATOR_SUPPORTED_REPORT_NODE_IDENTIFY_V3                                      0x50

#define INDICATOR_SUPPORTED_REPORT_PROPERTIES1_PROPERTY_SUPPORTED_BIT_MASK_LENGTH_MASK_V3 0x1F
#define INDICATOR_SUPPORTED_REPORT_PROPERTIES1_RESERVED_MASK_V3                          0xE0
#define INDICATOR_SUPPORTED_REPORT_PROPERTIES1_RESERVED_SHIFT_V3                         0x05

/************************************************************/
/* Indicator Get V3 command class structs */
/************************************************************/
typedef struct _ZW_INDICATOR_GET_V3_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      indicatorId;                  /**/
} ZW_INDICATOR_GET_V3_FRAME;

/************************************************************/
/* Indicator Report V3 variant group structs */
/************************************************************/
typedef struct _VG_INDICATOR_REPORT_V3_VG_
{
    BYTE      indicatorId;                  /**/
    BYTE      propertyId;                   /**/
    BYTE      value;                        /**/
} VG_INDICATOR_REPORT_V3_VG;

/************************************************************/
/* Indicator Report 1byte V3 command class structs */
/************************************************************/
typedef struct _ZW_INDICATOR_REPORT_1BYTE_V3_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      indicator0Value;              /**/
    BYTE      properties1;                  /* masked byte */
    VG_INDICATOR_REPORT_V3_VG variantgroup1;                /**/
} ZW_INDICATOR_REPORT_1BYTE_V3_FRAME;

/************************************************************/
/* Indicator Report 2byte V3 command class structs */
/************************************************************/
typedef struct _ZW_INDICATOR_REPORT_2BYTE_V3_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      indicator0Value;              /**/
    BYTE      properties1;                  /* masked byte */
    VG_INDICATOR_REPORT_V3_VG variantgroup1;                /**/
    VG_INDICATOR_REPORT_V3_VG variantgroup2;                /**/
} ZW_INDICATOR_REPORT_2BYTE_V3_FRAME;

/************************************************************/
/* Indicator Report 3byte V3 command class structs */
/************************************************************/
typedef struct _ZW_INDICATOR_REPORT_3BYTE_V3_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      indicator0Value;              /**/
    BYTE      properties1;                  /* masked byte */
    VG_INDICATOR_REPORT_V3_VG variantgroup1;                /**/
    VG_INDICATOR_REPORT_V3_VG variantgroup2;                /**/
    VG_INDICATOR_REPORT_V3_VG variantgroup3;                /**/
} ZW_INDICATOR_REPORT_3BYTE_V3_FRAME;

/************************************************************/
/* Indicator Report 4byte V3 command class structs */
/************************************************************/
typedef struct _ZW_INDICATOR_REPORT_4BYTE_V3_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      indicator0Value;              /**/
    BYTE      properties1;                  /* masked byte */
    VG_INDICATOR_REPORT_V3_VG variantgroup1;                /**/
    VG_INDICATOR_REPORT_V3_VG variantgroup2;                /**/
    VG_INDICATOR_REPORT_V3_VG variantgroup3;                /**/
    VG_INDICATOR_REPORT_V3_VG variantgroup4;                /**/
} ZW_INDICATOR_REPORT_4BYTE_V3_FRAME;

/************************************************************/
/* Indicator Set V3 variant group structs */
/************************************************************/
typedef struct _VG_INDICATOR_SET_V3_VG_
{
    BYTE      indicatorId;                  /**/
    BYTE      propertyId;                   /**/
    BYTE      value;                        /**/
} VG_INDICATOR_SET_V3_VG;

/************************************************************/
/* Indicator Set 1byte V3 command class structs */
/************************************************************/
typedef struct _ZW_INDICATOR_SET_1BYTE_V3_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      indicator0Value;              /**/
    BYTE      properties1;                  /* masked byte */
    VG_INDICATOR_SET_V3_VG variantgroup1;                /**/
} ZW_INDICATOR_SET_1BYTE_V3_FRAME;

/************************************************************/
/* Indicator Set 2byte V3 command class structs */
/************************************************************/
typedef struct _ZW_INDICATOR_SET_2BYTE_V3_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      indicator0Value;              /**/
    BYTE      properties1;                  /* masked byte */
    VG_INDICATOR_SET_V3_VG variantgroup1;                /**/
    VG_INDICATOR_SET_V3_VG variantgroup2;                /**/
} ZW_INDICATOR_SET_2BYTE_V3_FRAME;

/************************************************************/
/* Indicator Set 3byte V3 command class structs */
/************************************************************/
typedef struct _ZW_INDICATOR_SET_3BYTE_V3_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      indicator0Value;              /**/
    BYTE      properties1;                  /* masked byte */
    VG_INDICATOR_SET_V3_VG variantgroup1;                /**/
    VG_INDICATOR_SET_V3_VG variantgroup2;                /**/
    VG_INDICATOR_SET_V3_VG variantgroup3;                /**/
} ZW_INDICATOR_SET_3BYTE_V3_FRAME;

/************************************************************/
/* Indicator Set 4byte V3 command class structs */
/************************************************************/
typedef struct _ZW_INDICATOR_SET_4BYTE_V3_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      indicator0Value;              /**/
    BYTE      properties1;                  /* masked byte */
    VG_INDICATOR_SET_V3_VG variantgroup1;                /**/
    VG_INDICATOR_SET_V3_VG variantgroup2;                /**/
    VG_INDICATOR_SET_V3_VG variantgroup3;                /**/
    VG_INDICATOR_SET_V3_VG variantgroup4;                /**/
} ZW_INDICATOR_SET_4BYTE_V3_FRAME;

/************************************************************/
/* Indicator Supported Get V3 command class structs */
/************************************************************/
typedef struct _ZW_INDICATOR_SUPPORTED_GET_V3_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      indicatorId;                  /**/
} ZW_INDICATOR_SUPPORTED_GET_V3_FRAME;

/************************************************************/
/* Indicator Supported Report 1byte V3 command class structs */
/************************************************************/
typedef struct _ZW_INDICATOR_SUPPORTED_REPORT_1BYTE_V3_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      indicatorId;                  /**/
    BYTE      nextIndicatorId;              /**/
    BYTE      properties1;                  /* masked byte */
    BYTE      propertySupportedBitMask1;
} ZW_INDICATOR_SUPPORTED_REPORT_1BYTE_V3_FRAME;

/************************************************************/
/* Indicator Supported Report 2byte V3 command class structs */
/************************************************************/
typedef struct _ZW_INDICATOR_SUPPORTED_REPORT_2BYTE_V3_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      indicatorId;                  /**/
    BYTE      nextIndicatorId;              /**/
    BYTE      properties1;                  /* masked byte */
    BYTE      propertySupportedBitMask1;    /* MSB */
    BYTE      propertySupportedBitMask2;    /* LSB */
} ZW_INDICATOR_SUPPORTED_REPORT_2BYTE_V3_FRAME;

/************************************************************/
/* Indicator Supported Report 3byte V3 command class structs */
/************************************************************/
typedef struct _ZW_INDICATOR_SUPPORTED_REPORT_3BYTE_V3_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      indicatorId;                  /**/
    BYTE      nextIndicatorId;              /**/
    BYTE      properties1;                  /* masked byte */
    BYTE      propertySupportedBitMask1;    /* MSB */
    BYTE      propertySupportedBitMask2;
    BYTE      propertySupportedBitMask3;    /* LSB */
} ZW_INDICATOR_SUPPORTED_REPORT_3BYTE_V3_FRAME;

/************************************************************/
/* Indicator Supported Report 4byte V3 command class structs */
/************************************************************/
typedef struct _ZW_INDICATOR_SUPPORTED_REPORT_4BYTE_V3_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      indicatorId;                  /**/
    BYTE      nextIndicatorId;              /**/
    BYTE      properties1;                  /* masked byte */
    BYTE      propertySupportedBitMask1;    /* MSB */
    BYTE      propertySupportedBitMask2;
    BYTE      propertySupportedBitMask3;
    BYTE      propertySupportedBitMask4;    /* LSB */
} ZW_INDICATOR_SUPPORTED_REPORT_4BYTE_V3_FRAME;

/************************************************************/
/* Version command class V3 command class structs */
/************************************************************/

#define VERSION_VERSION_V3                                      0x03
#define VERSION_CAPABILITIES_GET                                0x15
#define VERSION_CAPABILITIES_REPORT                             0x16
#define VERSION_ZWAVE_SOFTWARE_GET                              0x17
#define VERSION_ZWAVE_SOFTWARE_REPORT                           0x18

#define VERSION_CAPABILITIES_REPORT_V                           0x01
#define VERSION_CAPABILITIES_REPORT_CC                          0x02
#define VERSION_CAPABILITIES_REPORT_ZWS                         0x04



/************************************************************/
/* Version Capabilities command class structs */
/************************************************************/
typedef struct _ZW_VERSION_CAPABILITIES_GET_V3_FRAME_
{
    BYTE cmdClass;                          /* The command class */
    BYTE cmd;                               /* The command */
} ZW_VERSION_CAPABILITIES_GET_V3_FRAME;

typedef struct _ZW_VERSION_CAPABILITIES_REPORT_V3_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      properties;                  /* masked byte */
} ZW_VERSION_CAPABILITIES_REPORT_V3_FRAME;

/************************************************************/
/* Version Z-Wave Software command class structs */
/************************************************************/
typedef struct _ZW_VERSION_ZWAVE_SOFTWARE_GET_V3_FRAME_
{
    BYTE cmdClass;                          /* The command class */
    BYTE cmd;                               /* The command */
} ZW_VERSION_ZWAVE_SOFTWARE_GET_V3_FRAME;

typedef struct _ZW_VERSION_ZWAVE_SOFTWARE_REPORT_V3_FRAME_

{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      sDKversion1;
    BYTE      sDKversion2;
    BYTE      sDKversion3;
    BYTE      applicationFrameworkAPIVersion1;  // (MSB)
    BYTE      applicationFrameworkAPIVersion2;
    BYTE      applicationFrameworkAPIVersion3;  // (LSB)
    BYTE      applicationFrameworkBuildNumber1;  // (MSB)
    BYTE      applicationFrameworkBuildNumber2;  // (LSB)
    BYTE      hostInterfaceVersion1;  // (MSB)
    BYTE      hostInterfaceVersion2; 
    BYTE      hostInterfaceVersion3;  // (LSB)
    BYTE      hostInterfaceBuildNumber1; // (MSB)
    BYTE      hostInterfaceBuildNumber2; // (LSB)
    BYTE      zWaveProtocolVersion1;  // (MSB)
    BYTE      zWaveProtocolVersion2; 
    BYTE      zWaveProtocolVersion3;  // (LSB)
    BYTE      zWaveProtocolBuildNumber1;  // (MSB)
    BYTE      zWaveProtocolBuildNumber2;  // (LSB)
    BYTE      applicationVersion1; //(MSB)
    BYTE      applicationVersion2;
    BYTE      applicationVersion; // (LSB)
    BYTE      applicationBuildNumber1; //(MSB)
    BYTE      applicationBuildNumber2; //(LSB)
} ZW_VERSION_ZWAVE_SOFTWARE_REPORT_V3_FRAME;

#define NETWORK_MANAGEMENT_INSTALLATION_MAINTENANCE_VERSION_2                            0x02
#define NETWORK_MANAGEMENT_INSTALLATION_MAINTENANCE_VERSION_3                            0x03
#define NETWORK_MANAGEMENT_INSTALLATION_MAINTENANCE_VERSION_4                            0x04

/*************************************************************************/
/* Network Management Proxy Version 3 Failed node list report */
/*************************************************************************/
#define FAILED_NODE_LIST_GET    0x0B
#define FAILED_NODE_LIST_REPORT 0x0C

typedef struct _ZW_FAILED_NODE_LIST_REPORT_1BYTE_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      seqNo;                        /**/
    BYTE      failedNodeListData1;
} ZW_FAILED_NODE_LIST_REPORT_1BYTE_FRAME;

typedef struct _ZW_FAILED_NODE_LIST_REPORT_1BYTE_V4_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      seqNo;                        /**/
    BYTE      failedNodeListData1[29];
    uint8_t   extendedNodeidMSB;            /**/
    uint8_t   extendedNodeidLSB;            /**/
    uint8_t   extendedNodeListData1;
} ZW_FAILED_NODE_LIST_REPORT_1BYTE_V4_FRAME;


/*************************************************************************/
/* Network Management Inclusion Command Class, version 4 */ 
/*************************************************************************/
/************************************************************/
/* Node Remove Status V4 command class structs */
/************************************************************/
typedef struct _ZW_NODE_REMOVE_STATUS_V4_FRAME_
{
    uint8_t   cmdClass;                     /* The command class */
    uint8_t   cmd;                          /* The command */
    uint8_t   seqNo;                        /**/
    uint8_t   status;                       /**/
    uint8_t   nodeid;                       /**/
    uint8_t   extendedNodeidMSB;            /**/
    uint8_t   extendedNodeidLSB;            /**/
} ZW_NODE_REMOVE_STATUS_V4_FRAME;

/************************************************************/
/* Failed Node Remove Status V4 command class structs */
/************************************************************/
typedef struct _ZW_FAILED_NODE_REMOVE_STATUS_V4_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      seqNo;                        /**/
    BYTE      status;                       /**/
    BYTE      nodeId;                       /**/
    BYTE      extendedNodeIdMSB;            /**/
    BYTE      extendedNodeIdLSB;            /**/
} ZW_FAILED_NODE_REMOVE_STATUS_V4_FRAME;

/************************************************************/
/* Failed Node Remove V4 command class structs */
/************************************************************/
typedef struct _ZW_FAILED_NODE_REMOVE_V4_FRAME_
{
    uint8_t   cmdClass;                     /* The command class */
    uint8_t   cmd;                          /* The command */
    uint8_t   seqNo;                        /**/
    uint8_t   nodeId;                       /**/
    uint8_t   extendedNodeIdMSB;            /**/
    uint8_t   extendedNodeIdLSB;            /**/
} ZW_FAILED_NODE_REMOVE_V4_FRAME;

/************************************************************/
/* Node Info Cached Get command class structs */
/************************************************************/
typedef struct _ZW_NODE_INFO_CACHED_GET_V4_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      seqNo;                        /**/
    BYTE      properties1;                  /* masked byte */
    BYTE      nodeId;                       /**/
    BYTE      extendedNodeIdMSB;
    BYTE      extendedNodeIdLSB;
} ZW_NODE_INFO_CACHED_GET_V4_FRAME;


/************************************************************/
/* Node List Report 1byte command class structs */          
/************************************************************/
typedef struct _ZW_NODE_LIST_REPORT_1BYTE_V4_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      seqNo;                        /**/
    BYTE      status;                       /**/
    BYTE      nodeListControllerId;         /**/
    BYTE      nodeListData1;                
    uint8_t   extendedNodeidMSB;            /**/
    uint8_t   extendedNodeidLSB;            /**/
    uint8_t   extendedNodeListData1;
} ZW_NODE_LIST_REPORT_1BYTE_V4_FRAME;

/************************************************************/
/* Zip Node Advertisement command class structs */          
/************************************************************/
typedef struct _ZW_ZIP_NODE_ADVERTISEMENT_V4_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      properties1;                  /* masked byte */
    BYTE      nodeID;                       /**/
    BYTE      ipv6Address1;                 /* MSB */
    BYTE      ipv6Address2;                 
    BYTE      ipv6Address3;                 
    BYTE      ipv6Address4;                 
    BYTE      ipv6Address5;                 
    BYTE      ipv6Address6;                 
    BYTE      ipv6Address7;                 
    BYTE      ipv6Address8;                 
    BYTE      ipv6Address9;                 
    BYTE      ipv6Address10;                
    BYTE      ipv6Address11;                
    BYTE      ipv6Address12;                
    BYTE      ipv6Address13;                
    BYTE      ipv6Address14;                
    BYTE      ipv6Address15;                
    BYTE      ipv6Address16;                /* LSB */
    BYTE      homeId1;                      /* MSB */
    BYTE      homeId2;                      
    BYTE      homeId3;                      
    BYTE      homeId4;                      /* LSB */
    uint8_t   extendedNodeidMSB;            /**/
    uint8_t   extendedNodeidLSB;            /**/
} ZW_ZIP_NODE_ADVERTISEMENT_V4_FRAME;

/************************************************************/
/* Zip Inv Node Solicitation command class structs */       
/************************************************************/
typedef struct _ZW_ZIP_INV_NODE_SOLICITATION_V4_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      properties1;                  /* masked byte */
    BYTE      nodeID;                       /**/
    uint8_t   extendedNodeidMSB;            /**/
    uint8_t   extendedNodeidLSB;            /**/
} ZW_ZIP_INV_NODE_SOLICITATION_V4_FRAME;

#define EXTENDED_NODE_ADD_STATUS                                                       0x16
/************************************************************/
/* Extended Node Add Status 1byte command class structs */
/************************************************************/
typedef struct _ZW_EXTENDED_NODE_ADD_STATUS_1BYTE_FRAME_
{
    BYTE      cmdClass;                     /* The command class */
    BYTE      cmd;                          /* The command */
    BYTE      seqNo;                        /**/
    BYTE      status;                       /**/
    BYTE      newNodeIdMSB;                    /**/
    BYTE      newNodeIdLSB;                    /**/
    BYTE      nodeInfoLength;               /**/
    BYTE      properties1;                  /* masked byte */
    BYTE      properties2;                  /* masked byte */
    BYTE      basicDeviceClass;             /**/
    BYTE      genericDeviceClass;           /**/
    BYTE      specificDeviceClass;          /**/
    BYTE      commandClass1;
} ZW_EXTENDED_NODE_ADD_STATUS_1BYTE_FRAME;

#define EXTENDED_STATISTICS_GET         0x0B
#define EXTENDED_STATISTICS_REPORT      0X0C
#define COMMAND_CLASS_NO_OPERATION_LR   0x04

#endif /* ZW_CLASSCMD_EX_H_ */
