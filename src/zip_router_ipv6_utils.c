/* © 2019 Silicon Laboratories Inc. */

#include "contiki-net.h"
#include "uip-debug.h"
#include "zip_router_config.h"
#include "zw_network_info.h"
#include "ZIP_Router_logging.h"
#include "ipv46_addr.h"
#include "zip_router_ipv6_utils.h"

/* Z-Wave/IPv6 address utilities. */

/*
 * Get the ipv6 address of a given node
 */
void ipOfNode(uip_ip6addr_t* dst, nodeid_t nodeID) {
  if (nodeID == MyNodeID) {
    uip_ipaddr_copy(dst,&cfg.lan_addr);
  } else {
    uip_ipaddr_copy(dst,&cfg.pan_prefix);
    dst->u16[7] = UIP_HTONS(nodeID);
  }
}

/**
 * Get the mac address of a node. For #MyNodeID this returns
 * the device mac address which should be globally unique.
 * For the Z-Wave nodes we use locally administered MAC addresses.
 *
 * The locally administered address MUST be created in a way so
 * multiple Z/IP gateways can coincide in the same network. If the
 * gateway are member of the same Z-Wave network(HomeID) they will use the
 * same MAC range.
 */
void macOfNode(uip_lladdr_t* dst, nodeid_t nodeID) {
  if(nodeID == MyNodeID) {
    memcpy(dst,&uip_lladdr, sizeof(uip_lladdr_t));
  } else {
    dst->addr[0] = ((homeID>>24) & 0xff);
    dst->addr[1] = ((homeID>>16) & 0xff);
    dst->addr[2] = (homeID>>8) & 0xFF;
    dst->addr[3] = (homeID & 0xFF);
    dst->addr[4] = (nodeID>>8) & 0xFF;
    dst->addr[5] = nodeID & 0xFF;
  }
}

/**
 * Return the node is corresponding to an ip address, returns 0 if the
 * ip address is not in the pan
 */
nodeid_t nodeOfIP(const uip_ip6addr_t* ip)
{
  if (uip_ipaddr_prefixcmp(ip,&cfg.pan_prefix,64))
  {
     return UIP_HTONS(ip->u16[7]);
  }

  if (uip_ds6_is_my_addr((uip_ip6addr_t*)ip))
  {
    return MyNodeID;
  }

  else if (uip_is_4to6_addr(ip))
  {
    return ipv46nat_get_nat_addr((uip_ipv4addr_t*) &ip->u8[12]);
  }
  return 0;
}

/* ******* IP/LAN and zip_router_config ******** */

/**
 * Reread all IPv6 addresses from cfg structure
 */
void refresh_ipv6_addresses() CC_REENTRANT_ARG
{
  uip_ipaddr_t ipaddr;
  const uip_ipaddr_t ipv4prefix =  { .u16 = {0,0,0,0,0,0xFFFF,0,0} };

  /*Clear all old addresses*/
  uip_ds6_init();

  uip_ipaddr_copy(&ipaddr,&(cfg.pan_prefix));
  ipaddr.u16[7] = UIP_HTONS(MyNodeID);
  uip_ds6_addr_add(&ipaddr, 0, ADDR_MANUAL);

  /*Add Lan prefix */
  uip_ds6_prefix_add(&cfg.lan_addr, cfg.lan_prefix_length, 1,
      UIP_ND6_RA_FLAG_ONLINK | UIP_ND6_RA_FLAG_AUTONOMOUS, 2 * 60 * 60,
      2 * 60 * 60);

  /* Add lan IP address */
  uip_ds6_addr_add(&cfg.lan_addr, 0, ADDR_MANUAL);

  /* Add default route */
  if (!uip_is_addr_unspecified(&cfg.gw_addr))
  {
        uip_ds6_defrt_add(&cfg.gw_addr, 0);
    }

  /*Add pan prefix */
  uip_ds6_prefix_add(&(cfg.pan_prefix), 64, 0, 0, 0, 0);

  if (!uip_is_addr_unspecified(&(cfg.tun_prefix)))
  {
    /*Add tun prefix */
    uip_ds6_prefix_add(&(cfg.tun_prefix), cfg.tun_prefix_length, 1, 0, 0, 0);
  }

  /*Add IPv4 prefix*/
  uip_ds6_prefix_add((uip_ipaddr_t*)&ipv4prefix,128-32,0,0,0,0);

  LOG_PRINTF("Tunnel prefix       ");
  uip_debug_ipaddr_print(&cfg.tun_prefix);
  LOG_PRINTF("Lan address         ");
  uip_debug_ipaddr_print(&cfg.lan_addr);
  LOG_PRINTF("Han address         ");
  uip_debug_ipaddr_print(&cfg.pan_prefix);
  LOG_PRINTF("Gateway address     ");
  uip_debug_ipaddr_print(&cfg.gw_addr);
  LOG_PRINTF("Unsolicited address ");
  uip_debug_ipaddr_print(&cfg.unsolicited_dest);
}

