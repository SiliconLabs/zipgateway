/* © 2014 Silicon Laboratories Inc.
 */

#ifndef CC_PORTAL_H_
#define CC_PORTAL_H_

/** \ingroup CMD_handler
 * \defgroup PO_CMD_handler Portal command handler
 *
 * Use the Z/IP Portal Command Class for configuration and management
 * communication between a Z/IP portal server and a Z/IP gateway through
 * a secure connection.
 *
 * Use the Z/IP Portal command class with the Z/IP Gateway
 * command class to provide a streamlined work flow for preparing and performing
 * installation of Z/IP Gateways on consumer premises.
 *
 * The command class MUST NOT be used outside trusted environments,
 * unless via a secure connection. The command class SHOULD be further
 * limited for use only via a secure connection to an authenticated portal server.
 *
 * See also \ref Portal_handler
 * @{
 */
#include "contiki-net.h"
#include "ZW_udp_server.h"



/** @} */
#endif /* CC_PORTAL_H_ */
