/* © 2014 Silicon Laboratories Inc.
 */

#ifndef NODECACHE_H_
#define NODECACHE_H_

#include"TYPES.H"
#include "RD_types.h"
#include"sys/clock.h"
#include"sys/cc.h"

#ifdef __ASIX_C51__
#undef data
#define data _data
#endif
/**
 * Place holder for a node Info.
 * After this structure comes a list of classes.
 */

struct NodeInfo {
    BYTE      basicDeviceClass;             /**/
    BYTE      genericDeviceClass;           /**/
    BYTE      specificDeviceClass;          /**/
//    BYTE      classes[0];
};

#define NIF_MAX_SIZE 64

#define NODE_FLAG_SECURITY0      0x01
#define NODE_FLAG_KNOWN_BAD     0x02

#define NODE_FLAG_INFO_ONLY     0x08 /* Only probe the node info */
#define NODE_FLAG_SECURITY2_UNAUTHENTICATED 0x10
#define NODE_FLAG_SECURITY2_AUTHENTICATED   0x20
#define NODE_FLAG_SECURITY2_ACCESS          0x40

#define NODE_FLAGS_SECURITY2 (\
    NODE_FLAG_SECURITY2_UNAUTHENTICATED| \
    NODE_FLAG_SECURITY2_AUTHENTICATED| \
    NODE_FLAG_SECURITY2_ACCESS)

#define NODE_FLAGS_SECURITY (\
    NODE_FLAG_SECURITY0 | \
    NODE_FLAG_KNOWN_BAD| \
    NODE_FLAGS_SECURITY2)

#define  isNodeBad(n) ((GetCacheEntryFlag(n) & NODE_FLAG_KNOWN_BAD) !=0)
#define  isNodeSecure(n) ( (GetCacheEntryFlag(n) & (NODE_FLAG_SECURITY0 | NODE_FLAG_KNOWN_BAD)) == NODE_FLAG_SECURITY0)
int isNodeController(nodeid_t nodeid);

/**
 * Retrieve Cache entry flag
 */
BYTE GetCacheEntryFlag(nodeid_t nodeid) CC_REENTRANT_ARG;

/**
 * Checks if a given node supports a command class non secure.
 * @param nodeid ID of the node to check
 * @param class Class code to check for. This may be an extended class.
 * @retval TRUE The node supports this class
 * @retval FALSE The node does not support this class.
 */
int SupportsCmdClass(nodeid_t nodeid, WORD class);

/**
 * Checks if a given node supports a command class securely.
 * \param nodeid ID of the node to check
 * \param class Class code to check for. This may be an extended class.
 * \retval TRUE The node supports this class
 * \retval FALSE The node does not support this class.
 */
int SupportsCmdClassSecure(nodeid_t nodeid, WORD class)CC_REENTRANT_ARG;

/**
 * Set node attribute flags
 */
BYTE SetCacheEntryFlagMasked(nodeid_t nodeid,BYTE value, BYTE mask)CC_REENTRANT_ARG;

/**
 * Used to indicate that node info was received or timed out from protocol side.
 */
void NodeInfoRequestDone(BYTE status);

#endif /* NODECACHE_H_ */
