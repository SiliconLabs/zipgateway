/* © 2014 Silicon Laboratories Inc.
 */

#include "string.h"
#include "contiki-net-ipv4.h"
#include "ZIP_Router_logging.h"
#include "zw_network_info.h"
#include "ipv46_internal.h"
#include "ipv46_nat.h"

#include "ZW_typedefs.h"
#include "ZW_classcmd.h"
#include "RD_types.h"
uint16_t  nat_table_size =0;
nat_table_entry_t nat_table[MAX_NAT_ENTRIES];

/* This file uses contiki IPv4, but also needs to know about the ipv6 address type. */
#if !UIP_CONF_IPV6
typedef union uip_ip6addr_t {
  u8_t  u8[16];     /* Initializer, must come first!!! */
  u16_t u16[8];
} uip_ip6addr_t ;
#endif

void dhcp_check_for_new_sessions();
void dhcp_release( nat_table_entry_t *s );

void ipv46nat_ipv4addr_of_entry(uip_ipv4addr_t* ip,nat_table_entry_t *e);

#define PRINTF(a,...)

/**
 * @return The node id behind the ipv4 address
 * Return NULL if this is not one of our ipaddresses
 */
nodeid_t ipv46nat_get_nat_addr(uip_ipv4addr_t *ip) {
  uint16_t i;

  if(uip_ipaddr_maskcmp(ip, &uip_hostaddr, &uip_netmask)) {
    for(i = 0; i < nat_table_size; i++) {
      if( (nat_table[i].ip_suffix) == (ip->u16[1] & (~uip_netmask.u16[1]))   ) {
        return nat_table[i].nodeid;
      }
    }
  }
  return 0;
}

/**
 * Add NAT table entry. Returns 0 is the add entry fails
 */
u8_t ipv46nat_add_entry(nodeid_t node) {
  uint16_t i;
  if(nat_table_size >= MAX_NAT_ENTRIES) {
    return 0;
  }

  for(i = 0; i < nat_table_size; i++) {
    if( (nat_table[i].nodeid) == node ) {
      PRINTF("Entry %d already exists\n",node);
      return nat_table_size;
    }
  }

  nat_table[nat_table_size].nodeid = node;
  nat_table[nat_table_size].ip_suffix = (node==MyNodeID) ? (uip_hostaddr.u16[1] & ~uip_netmask.u16[1]): 0;

  PRINTF("Nat entry added node = %d ip = %u\n",
      nat_table[nat_table_size].nodeid, (unsigned)UIP_HTONS(nat_table[nat_table_size].ip_suffix));

  nat_table_size++;
  dhcp_check_for_new_sessions();
  return nat_table_size;
}


/**
 * Remove a nat table entry, return 1 if the entry was removed.
 */
u8_t ipv46nat_del_entry(nodeid_t node) {
  uint16_t i,removing;

  if(node == MyNodeID) {
    return 0;
  }
  removing = 0;
  for(i=0; i < nat_table_size;i++) {
    if(removing) {
      nat_table[i-1] = nat_table[i];
    } else if(nat_table[i].nodeid == node) {
      DBG_PRINTF("Releasing DHCP entry for node %d\n",node);
      dhcp_release(&nat_table[i]);
      removing++;
    }
  }
  nat_table_size-=removing;
  return removing;
}

/**
 * Remove all nat table entries except the gw.
 */
void ipv46nat_del_all_nodes() {
  uint16_t i;
  uint16_t my_index = 0;

  for (i=0; i < nat_table_size;i++) {
    if (nat_table[i].nodeid == MyNodeID) {
      my_index = i;
    } else {
      DBG_PRINTF("Releasing DHCP entry for node %d\n",
                 nat_table[i].nodeid);
      dhcp_release(&nat_table[i]);
    }
  }
  nat_table[0] = nat_table[my_index];
  nat_table_size = 1;
}

void ipv46nat_init() {
  nat_table_size = 0;
}

u8_t ipv46nat_ipv4addr_of_node(uip_ipv4addr_t* ip,nodeid_t node) {
  uint16_t i;

  for(i=0; i < nat_table_size;i++) {
    PRINTF("%d of %d %d\n",i,nat_table[i].nodeid, node);
    if(nat_table[i].nodeid == node && nat_table[i].ip_suffix) {
      ipv46nat_ipv4addr_of_entry( ip,&nat_table[i]);
      return 1;
    }
  }
  return 0;
}

void ipv46nat_ipv4addr_of_entry(uip_ipv4addr_t* ip,nat_table_entry_t *e) {
  memcpy(ip,uip_hostaddr.u8, sizeof(uip_ipv4addr_t));

  ip->u16[1] &= uip_netmask.u16[1];
  ip->u16[1] |= e->ip_suffix;
  return;
}


u8_t ipv46nat_all_nodes_has_ip() {
  uint16_t i;

  for(i=0; i < nat_table_size;i++) {
    if(nat_table[i].ip_suffix == 0) {
      return 0;
    }
  }
  return 1;
}

void ipv46nat_rename_node(nodeid_t old_id, nodeid_t new_id) {
  uint16_t i;

  /* TODO-reset: The gateway should always have index 0, no matter
   * what MyNodeID is, so renaming the gateway itself should be simple
   * and it should never be necessary to delete it. */
  for(i=0; i < nat_table_size;i++) {
      if(nat_table[i].nodeid == old_id) {
        nat_table[i].nodeid = new_id;
      }
    }
}
