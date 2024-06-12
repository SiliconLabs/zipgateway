/* © 2014 Silicon Laboratories Inc.
 */

#ifndef GATEWAY_CC_H_
#define GATEWAY_CC_H_

/** \ingroup CMD_handler
 * \defgroup GW_CMD_handler Gateway command handler(Remote Configuration)
 *
 *
 * In addition to local configuration, it is possible to push Z/IP Gateway
 * configuration remotely. Users can specify the following:
 *
 * - Stand-alone or Portal – if the Z/IP Gateway should connect to a Portal
 *   through a Remote Access connection.
 * - Setting the Peer – address of the Portal
 * - Lock / Unlock configuration
 * - Configuration of LAN and Z-Wave IPv6 and IPv4 prefixes and addresses
 *
 * @{
 */
#include "TYPES.H"
#include "zip_router_config.h"
#include "ZW_udp_server.h"

/**
 *
 * Read previously stored CC(pushed by COMMAND_APPLICATION_NODE_INFO_SET) from NVM and add them to NIF/global structures.
 */
void appNodeInfo_CC_Add(void) CC_REENTRANT_ARG;


/* TODO: Do it in a different way. Use another code from the Resource directory*/
BYTE IsCCInNodeInfoSetList(BYTE cmdclass,BOOL secure) CC_REENTRANT_ARG;

/** @} */
#endif /* GATEWAY_CC_H_ */
