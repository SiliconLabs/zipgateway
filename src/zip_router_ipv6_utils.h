/* © 2019 Silicon Laboratories Inc. */

#ifndef ZIP_ROUTER_IP_UTILS_H_
#define ZIP_ROUTER_IP_UTILS_H_

#include <stdint.h>
#include "net/uip.h"
#include "RD_types.h"

/**
 * Get the ipaddress of a given node
 * \param dst output
 * \param nodeID ID of the node
 */
void ipOfNode(uip_ip6addr_t* dst, nodeid_t nodeID);

/**
 * Return the node is corresponding to an ip address
 * \param ip IP address
 * \return ID of the node 
 */
nodeid_t nodeOfIP(const uip_ip6addr_t *ip);

void macOfNode(uip_lladdr_t* dst, nodeid_t nodeID);

void refresh_ipv6_addresses();



#endif
