/* © 2019 Silicon Laboratories Inc.
 */

#ifndef IPV46_INTERNAL_H_
#define IPV46_INTERNAL_H_

#include "ipv46_addr.h"

/** \defgroup ipv46natint IPv4/IPv6 Internal.
 * \ingroup ip46nat
 *
 * Internal data management of the IPv4/IPv6 support component.
 * Shared with \ref ZIP_DHCP.

 * @{
 */


/**
 * Contains information about each node.
 */
typedef struct nat_table_entry {
  /** Node ID which the nat table entry points to */
  nodeid_t nodeid;
  /** The last bits of the IPv4 address if the nat entry.
   * This is combined with the IPv4 net to form the full IPv4 address.
   */
  u16_t ip_suffix;
} nat_table_entry_t;

/**
 * Maximum number of entries in the NAT table.
 */
#define MAX_NAT_ENTRIES 1000

/**
 * Actual number of entries in the NAT table.
 */
extern uint16_t nat_table_size;

/**
 * The NAT entry database.
 */
extern nat_table_entry_t nat_table[MAX_NAT_ENTRIES];

/* For DHCP */

/**
 * Get the IPv4 address of a nat table entry.
 * @param ip destination to write to
 * @param e Entry to query
 */
void ipv46nat_ipv4addr_of_entry(uip_ipv4addr_t* ip,nat_table_entry_t *e);

/** @} */
#endif
