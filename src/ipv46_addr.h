/* © 2019 Silicon Laboratories Inc. */

#ifndef IPV46_ADDR_H_
#define IPV46_ADDR_H_
#include "RD_types.h"

/** \defgroup ip4zwmap IPv4 to Z-Wave address translator
 * \ingroup ip46nat
 *
 * This submodule maps between IPv4 addresses and Z-Wave node IDs.
 *
 * The submodule is independent of IPv6 support.
 * @{
 */

/**
 * Generic definition of an IPv4 address.
 *
 * This must be identical to the Contiki IPv4 address type.
 */
typedef union ip4addr {
  u8_t  u8[4];			/* Initializer, must come first!!! */
  u16_t u16[2];
} uip_ipv4addr_t;

/**
 * Get the IPv4 address of a node.
 *
 * @param ip destination to write the IP to
 * @param node ID of the node to query for
 * @return TRUE if address was found
 */
u8_t ipv46nat_ipv4addr_of_node(uip_ipv4addr_t* ip, nodeid_t node);

/**
 * Get the node id behind the IPv4 address.
 *
 * @param ip IPv4 address
 * @return nodeid
 */
nodeid_t ipv46nat_get_nat_addr(uip_ipv4addr_t *ip);

/** @} */
#endif
