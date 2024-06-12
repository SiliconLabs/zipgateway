/* © 2019 Silicon Laboratories Inc. */

#include <stdint.h>
#include "contiki-net-ipv4.h"
#include "ZIP_Router_logging.h"
#include "zw_network_info.h"
#include "ipv46_nat.h"
#include "ipv46_internal.h"

#include "sys/stimer.h"

/* TYPES.H must be included before ZW_classcmd.h */
#include "TYPES.H"
#include "ZW_classcmd.h"

#include "ipv46_if_handler.h"
#include "RD_types.h"

#define uip_buf uip_aligned_buf.u8

extern uip_ipv4addr_t uip_ipv4_net_broadcast_addr;

/* This file uses contiki IPv4, but also needs to know about the ipv6 address type. */
#if !UIP_CONF_IPV6
typedef union uip_ip6addr_t {
  u8_t  u8[16];     /* Initializer, must come first!!! */
  u16_t u16[8];
} uip_ip6addr_t ;
#endif

/** * \brief Unicast address structure */
typedef struct uip_ds6_addr {
  uint8_t isused;
  uip_ip6addr_t ipaddr;
  uint8_t state;
  uint8_t type;
  uint8_t isinfinite;
  struct stimer vlifetime;
  struct timer dadtimer;
  uint8_t dadnscount;
} uip_ds6_addr_t;

/** \brief A multicast address */
typedef struct uip_ds6_maddr {
  uint8_t isused;
  uip_ip6addr_t ipaddr;
} uip_ds6_maddr_t;

typedef struct ip6_hdr {
  /* IPV6 header */
  u8_t vtc;
  u8_t tcflow;
  u16_t flow;
  u16_t len;
  u8_t proto, ttl;
  uip_ip6addr_t srcipaddr, destipaddr;
} ip6_hdr_t; //40 bytes

/**
 * \ingroup ip46natdriver
 */
typedef struct ip4_hdr {
  /* IPV4 header */
  u8_t vhl, tos; u16_t len;
  u16_t ipid, ipoffset;
  u8_t ttl, proto; u16_t ipchksum;
  uip_ip4addr_t srcipaddr, destipaddr;
}  __attribute__ ((packed)) ip4_hdr_t ; //20 bytes


extern u16_t uip_len;
extern uip_buf_t uip_aligned_buf;
extern u16_t uip_udpchksum(void);
extern u16_t uip_ipchksum(void);
extern u16_t chksum(u16_t sum, const u8_t *data, u16_t len);

extern void ipOfNode(uip_ip6addr_t* dst, nodeid_t nodeID);
extern nodeid_t nodeOfIP(uip_ip6addr_t* ip);

extern uip_ds6_addr_t *uip_ds6_addr_lookup(uip_ip6addr_t * ipaddr);
extern uip_ds6_maddr_t *uip_ds6_maddr_lookup(uip_ip6addr_t * ipaddr);

#define uip_ds6_is_my_addr(addr)  (uip_ds6_addr_lookup(addr) != NULL)
#define uip_ds6_is_my_maddr(addr) (uip_ds6_maddr_lookup(addr) != NULL)

#define PRINTF(a,...)

/*
 * Return true if this is an IPv4 mapped IPv6 address
 */
uint8_t is_4to6_addr(const uip_ip6addr_t* ip);

static void ip4to6_addr(uip_ip6addr_t* dst,uip_ip4addr_t* src) {
  dst->u16[0] = 0;
  dst->u16[1] = 0;
  dst->u16[2] = 0;
  dst->u16[3] = 0;
  dst->u16[4] = 0;
  dst->u16[5] = 0xFFFF;
  dst->u16[6] = src->u16[0];
  dst->u16[7] = src->u16[1];
}

uint8_t is_4to6_addr(const uip_ip6addr_t* ip) {
  return(
    ip->u16[0] == 0 &&
    ip->u16[1] == 0 &&
    ip->u16[2] == 0 &&
    ip->u16[3] == 0 &&
    ip->u16[4] == 0 &&
    ip->u16[5] == 0xFFFF);
}

/**
 * Do the actual ipv4 to ipv6 translation
 */
static void do_46_translation(nodeid_t node) {
  ip6_hdr_t* ip6h,__ip6h;
  ip4_hdr_t* ip4h = (ip4_hdr_t*) &(uip_buf[14]);
  struct uip_eth_hdr* ethh =(struct uip_eth_hdr*) uip_buf;
  struct uip_udp_hdr *udph;
  struct uip_icmp_hdr *icmph;

  uint16_t len,i;
  u8_t *p,*q;

  ip6h = &__ip6h;


  /* For IPv6, the IP length field does not include the IPv6 IP header
     length. */
  len = UIP_HTONS(ip4h->len) - ((ip4h->vhl & 0xF) <<2);
  ip6h->len = UIP_HTONS(len);
  ip6h->ttl = ip4h->ttl;
  ip6h->proto = ip4h->proto;
  ip6h->vtc = 0x60;
  ip6h->tcflow = 0x00;
  ip6h->flow = 0x00;

  if(node == 0xFF) {

    memset(ip6h->destipaddr.u8,0,16);
    ip6h->destipaddr.u8[0] = 0xFF;
    ip6h->destipaddr.u8[1] = 0x02;
    ip6h->destipaddr.u8[15] = 0x02;
  } else {
    ipOfNode(&ip6h->destipaddr,node);
  }
  ip4to6_addr(&ip6h->srcipaddr, &ip4h->srcipaddr);

  /*End of the ipv4 package */
  p = (u8_t*)ip4h + UIP_HTONS(ip4h->len);

  /*As the ipv6 header is longer than the ipv4 header we do a backwards copy*/
  q = &uip_buf[14+sizeof(ip6_hdr_t) +len];
  if(q > uip_buf + UIP_BUFSIZE) {
    return;
  }

  uip_len += sizeof(ip6_hdr_t) - sizeof(ip4_hdr_t);
  ethh->type = UIP_HTONS(UIP_ETHTYPE_IPV6);

  PRINTF("Performing translation len 4 to 6 %u\n",len);

  p--;
  q--;
  for(i = len; i>0; i--) {
    *q-- = *p--;
  }

  /* Overwrite the ipv4 header with the new ipv6 header */
  memcpy(ip4h, ip6h, sizeof(ip6_hdr_t));
  ip6h = (ip6_hdr_t*)ip4h;

  /* Correct udp checksum, and translate icmp echo */
  switch(ip6h->proto) {
  case UIP_PROTO_UDP:
    udph = (struct uip_udp_hdr*)( (u8_t*) ip6h + sizeof(ip6_hdr_t));
    udph->udpchksum = 0;

    udph->udpchksum = ~(uip_udpchksum());
    if(udph->udpchksum == 0) {
      udph->udpchksum = 0xffff;
    }
    PRINTF("Translated udp\n");
    break;
  case UIP_PROTO_ICMP:
    icmph = (struct uip_icmp_hdr*) ((u8_t*) ip6h + sizeof(ip6_hdr_t));
    ip6h->proto = UIP_PROTO_ICMP6;

    /*See http://tools.ietf.org/html/rfc2765 3.3.  Translating ICMPv4 Headers into ICMPv6 Headers*/
    if(icmph->type == 8)
      icmph->type = 128;
    else if (icmph->type == 0)
      icmph->type = 129;
    else {
      PRINTF("Strange ICMP type %d\n", icmph->type);
      goto drop;
    }
    PRINTF("Translated icmp\n");
    break;
  default:
    PRINTF("protocol error 0x%bx\n",ip6h->proto);
    break;
  }
  return;
drop:
  uip_len = 0;
  PRINTF("We should drop this package\n");
}

/**
 * Do the actual ipv6 to ipv4 translation
 */
static void do_64_translation() {
  ip6_hdr_t* ip6h = (ip6_hdr_t*) &(uip_buf[14]);
  ip4_hdr_t* ip4h;
  u8_t __ip4h[20];
  struct uip_eth_hdr* ethh =(struct uip_eth_hdr*) uip_buf;
  struct uip_udp_hdr *udph;
  struct uip_icmp_hdr *icmph;

  uint16_t len,i;
  u8_t *p,*q;

  ip4h = (ip4_hdr_t*)&__ip4h;

  uip_len -= sizeof(ip6_hdr_t) - sizeof(ip4_hdr_t);
  ethh->type = UIP_HTONS(UIP_ETHTYPE_IP);

  len = UIP_HTONS(ip6h->len);
  PRINTF("Performing translation len 6 to 4 len =  %u\n",len);

  ip4h->vhl =  4 << 4 | 5; // Verison 4 , 5*4 =20 bytes header length
  ip4h->tos = 0;
  ip4h->len = UIP_HTONS(len + 20);
  ip4h->ipid = 0;
  ip4h->ipoffset = 0;
  ip4h->ttl = ip6h->ttl;
  ip4h->proto = ip6h->proto;

  memcpy(ip4h->destipaddr.u8,ip6h->destipaddr.u8+12,4);
  memcpy(ip4h->srcipaddr.u8,ip6h->srcipaddr.u8+12,4);

  p = (u8_t*)ip6h + 20;
  /*As the ipv6 header is longer than the ipv4 header we do a forwards copy*/
  q = (u8_t*)ip6h + sizeof(ip6_hdr_t);

  for(i = len; i>0; i--) {
    *p++ = *q++;
  }

  /* Overwrite the ipv6 header with the new ipv4 header */
  memcpy(ip6h, ip4h, sizeof(ip4_hdr_t));
  ip4h = (ip4_hdr_t*)ip6h;

  /* Correct udp checksum, and translate icmp echo */
  switch(ip4h->proto) {
  case UIP_PROTO_UDP:
    udph = (struct uip_udp_hdr*)( (u8_t*) ip4h + 20);
    udph->udpchksum = 0x0; //Disable checksum
    PRINTF("Translated udp\n");
    break;
  case UIP_PROTO_ICMP6:
    icmph = (struct uip_icmp_hdr*) ((u8_t*) ip4h + 20);
    ip4h->proto = UIP_PROTO_ICMP;

    /*See http://tools.ietf.org/html/rfc2765 3.3.  Translating ICMPv4 Headers into ICMPv6 Headers*/
    if(icmph->type == 128)
      icmph->type = 8;
    else if (icmph->type == 129)
      icmph->type = 0;
    else {
      PRINTF("Strange ICMP type %u\n", icmph->type);
      goto drop;
    }
#if !CHECKSUM_OFFLOAD
    icmph->icmpchksum = 0x0;
    icmph->icmpchksum = ~UIP_HTONS(chksum(0, (u8_t*)icmph, len));
#endif

    PRINTF("Translated icmp\n");
    break;
  default:
    PRINTF("protocol error 0x%x\n",ip6h->proto);
    break;
  }
#if !CHECKSUM_OFFLOAD
  ip4h->ipchksum = 0;
  ip4h->ipchksum = ~(uip_ipv4_ipchksum());
#endif

  return;
drop:
  uip_len = 0;
  PRINTF("We should drop this package\n");
}

/**
 * Input of the nat interface driver. This will translate the ip4 package in
 * uip_buf to a ipv6 package. If the destination of the ip address is a NAT address.
 * If the address is not a NAT'ed address this function will not change the uip_buf.
 *
 * If the destination if the destination address is the the uip_hostaddr and the destination
 * UDP port is the Z-Wave port, translation will be performed. Otherwise it will not.
 */
void ipv46nat_interface_input() {
  ip4_hdr_t* ip4h = (ip4_hdr_t*) &(uip_buf[14]);
  struct uip_udp_hdr *udph;
  nodeid_t node;
  uint8_t *zipcmd;


#if 0
  printf("IPv4 packet received from %d.%d.%d.%d to %d.%d.%d.%d  \r\n",
		  ip4h->srcipaddr.u8[0], ip4h->srcipaddr.u8[1], ip4h->srcipaddr.u8[2], ip4h->srcipaddr.u8[3],

		  ip4h->destipaddr.u8[0], ip4h->destipaddr.u8[1], ip4h->destipaddr.u8[2], ip4h->destipaddr.u8[3]
  );

  udph = (struct uip_udp_hdr *)&uip_buf[14 + ((ip4h->vhl & 0xF) <<2)];
  printf("Src port: %u  Dest Port = %u\r\n", udph->srcport, udph->destport);
#endif

  //Sasidhar: If it is TCP packet..by pass NAT function.
  if(ip4h->proto == UIP_PROTO_TCP)
  {
	  return;
  }
  else if(ip4h->proto == UIP_PROTO_UDP)
  {
	  udph = (struct uip_udp_hdr *)&uip_buf[14 + ((ip4h->vhl & 0xF) <<2)];
	  //It is DNS packet or  NTP client by pass NAT function.
    switch (UIP_HTONS(udph->srcport) )
	  {
      case 53: //DNS
      case 123: //NTP
      case 67: //BOOTP
      case 68: //BOOTP
		  return;
	  }

    if ((uip_ipaddr_cmp(&ip4h->destipaddr, &uip_broadcast_addr)
        || uip_ipaddr_cmp(&ip4h->destipaddr, &uip_ipv4_net_broadcast_addr)))
    {
      if (udph->destport == UIP_HTONS(4123))
      {
      zipcmd = (uint8_t*)udph + 8 + 7;
        if (zipcmd[0] == COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY
            && zipcmd[1] == NODE_INFO_CACHED_GET)
        {
        /*TODO add delay*/
        /*do_46_translation(MyNodeID);*/
        do_46_translation(0xFF);
        }
      }
        return;
      }
    if ((ip4h->destipaddr.u8[0] & 224) == 224)
    {
      /*If mDNS*/
      if (udph->destport == UIP_HTONS(5353))
      {
        do_46_translation(0xFF);
    }
      return;
  }
  }

  //Do the 4to6 NAT
  /*
   * TODO Maybe we should divide this up in just a SIIT (rfc2765 stage) and then a NAT stage.
   * */
  node = ipv46nat_get_nat_addr((uip_ipv4addr_t*)&ip4h->destipaddr);
  if(node) {
    do_46_translation(node);
  }
}

uint8_t ipv46nat_interface_output() {
  uip_ip4addr_t addr;
  ip6_hdr_t* ip6h = (ip6_hdr_t*) &(uip_buf[14]);
  uint16_t i;
  nodeid_t node;

  addr = uip_hostaddr;
  addr.u16[1] &= uip_netmask.u16[1];

  /*Check if we should translate this package */
  if (is_4to6_addr((uip_ip6addr_t*)&ip6h->destipaddr)) {
    /*Translate source address to NATed address*/
    if(uip_ds6_is_my_addr(&ip6h->srcipaddr) ||
       uip_ds6_is_my_maddr(&ip6h->srcipaddr)) {
      node=MyNodeID;
    } else {
      node = nodeOfIP(&ip6h->srcipaddr);
    }


    for(i = 0; i < nat_table_size; i++) {
      if( (nat_table[i].nodeid) == node ) {
        addr.u16[1] |= nat_table[i].ip_suffix;
        break;
      }
    }

    if(i != nat_table_size) {
      ip4to6_addr(&ip6h->srcipaddr,&addr);

      /*Convert to ipv4 package*/
      do_64_translation();
      if(uip_len==0) {
        return 0;
      }
      uip_ipv4_len = uip_len - UIP_LLH_LEN;

      /*arp_out checks is we should send to a local host or to default gw*/
      uip_arp_out();
      uip_len = uip_ipv4_len;
    } else {
      WRN_PRINTF(" The source of this is package is not a Z-Wave node dropping\n\n");
      uip_len = 0;
    }
  }
  return 0;
}
