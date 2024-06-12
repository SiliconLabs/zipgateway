/* © 2014 Silicon Laboratories Inc.
 */
#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

//#define SUPPORTS_ZIP_6LOWPAN
#undef UIP_FALLBACK_INTERFACE
//#define UIP_FALLBACK_INTERFACE rpl_interface
#define IP_4_6_NAT
#define ZIP_ROUTER
#define ZIP_ND6 1

#define UIP_CONFIG_SOURCE_ROUTING 0


#define UIP_CONF_ROUTER 1

#define UIP_CONF_ND6_SEND_RA 1
#define UIP_CONF_ND6_RFC4191 1
/*Dont send router anouncements.*/

#define UIP_CONF_IPV6 1
#define UIP_CONF_ICMP6 1

#ifdef SUPPORTS_ZIP_6LOWPAN
#define UIP_CONF_IPV6_RPL 1
#endif

#ifdef SUPPORTS_MDNS
#define UIP_CONF_DS6_MADDR_NBU 3
#endif

#define UIP_CONF_DS6_PREFIX_NBU 4

#undef QUEUEBUF_CONF_NUM
#define QUEUEBUF_CONF_NUM          4

#undef UIP_CONF_BUFFER_SIZE
//#define UIP_CONF_BUFFER_SIZE    488
#define UIP_CONF_BUFFER_SIZE    1544

#define PACKETBUF_CONF_SIZE UIP_CONF_BUFFER_SIZE
#define PACKETBUF_CONF_HDR_SIZE 8

//#undef UIP_CONF_RECEIVE_WINDOW
//#define UIP_CONF_RECEIVE_WINDOW  60

#undef WEBSERVER_CONF_CFS_CONNS
#define WEBSERVER_CONF_CFS_CONNS 2

//#define UIP_FALLBACK_INTERFACE backup_interface

//GCC is kind enough to tell us which byte order this is for the MIPS port.
#ifdef __MIPSEB__
  #undef UIP_CONF_BYTE_ORDER
  #define UIP_CONF_BYTE_ORDER      UIP_BIG_ENDIAN
#else
  #undef UIP_CONF_BYTE_ORDER
  #define UIP_CONF_BYTE_ORDER      UIP_LITTLE_ENDIAN
#endif


//Defines for building for Keil compatible code
#define reentrant
#define CC_NO_VA_ARGS 0
#define ZW_LOG(a,b)
#ifndef ZW_DEBUG_SEND_BYTE
#define ZW_DEBUG_SEND_BYTE(c)
#define ZW_DEBUG_SEND_NUMW(n)
#define ZW_DEBUG_SEND_NUM(n)
#define ZW_DEBUG_SEND_NL()
#endif

/*
#ifndef DEBUG
#define DEBUG
#endif

#ifdef DEBUG
#include <stdio.h>
#ifndef PRINTF
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINT6ADDR(addr) PRINTF(" %02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x ", ((u8_t *)addr)[0], ((u8_t *)addr)[1], ((u8_t *)addr)[2], ((u8_t *)addr)[3], ((u8_t *)addr)[4], ((u8_t *)addr)[5], ((u8_t *)addr)[6], ((u8_t *)addr)[7], ((u8_t *)addr)[8], ((u8_t *)addr)[9], ((u8_t *)addr)[10], ((u8_t *)addr)[11], ((u8_t *)addr)[12], ((u8_t *)addr)[13], ((u8_t *)addr)[14], ((u8_t *)addr)[15])
#define PRINTLLADDR(lladdr) PRINTF(" %02x:%02x:%02x:%02x:%02x:%02x ",lladdr->addr[0], lladdr->addr[1], lladdr->addr[2], lladdr->addr[3],lladdr->addr[4], lladdr->addr[5])
#endif
#endif
*/


#define SICSLOWPAN_CONF_COMPRESSION_IPV6        0
#define SICSLOWPAN_CONF_COMPRESSION_HC1         1
#define SICSLOWPAN_CONF_COMPRESSION_HC01        2
#define SICSLOWPAN_CONF_COMPRESSION             SICSLOWPAN_COMPRESSION_HC06
#ifndef SICSLOWPAN_CONF_FRAG
#define SICSLOWPAN_CONF_FRAG                    1
#define SICSLOWPAN_CONF_MAXAGE                  8
#endif /* SICSLOWPAN_CONF_FRAG */
#define SICSLOWPAN_CONF_CONVENTIONAL_MAC        1
#define SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS       2
#define SICSLOWPAN_CONF_MAX_MAC_TRANSMISSIONS   5


#endif /* PROJECT_CONF_H_ */
