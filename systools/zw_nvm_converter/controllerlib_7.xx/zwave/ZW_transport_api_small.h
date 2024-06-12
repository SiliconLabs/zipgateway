/* © 2019 Silicon Laboratories Inc. */
#ifndef _ZW_TRANSPORT_API_SMALL_H
#define _ZW_TRANSPORT_API_SMALL_H

// ---------------------------------------------------------------------------
// ZWave/API/ZW_transport_api.h
// ---------------------------------------------------------------------------

/* Max number of nodes in a Z-wave system */
#define ZW_LR_MAX_NODE_ID     1279  // LR bitmask has only 128 bytes in NVM
#define ZW_LR_MIN_NODE_ID     256
#define ZW_CLASSIC_MAX_NODES  232
#define ZW_LR_MAX_NODES       ((ZW_LR_MAX_NODE_ID - ZW_LR_MIN_NODE_ID) + 1)

#define HOMEID_LENGTH      4
#define MAX_REPEATERS      4

#endif // _ZW_TRANSPORT_API_SMALL_H
