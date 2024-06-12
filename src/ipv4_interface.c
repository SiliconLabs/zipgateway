/* © 2014 Silicon Laboratories Inc.
 */
#include "contiki.h"
#include "contiki-net-ipv4.h"

#define UIP_CONF_IPV6
#include "contiki-net.h"
#include "dhcpc2.h"
#include "string.h"

PROCESS_NAME(tcpip_ipv4_process);

extern u8_t do_send(void);
extern void tcpip_ipv4_set_outputfunc(u8_t (*f)(void));

extern u16_t uip_len;

u8_t ipv4_send(void)
{
  //printf("ipv4_send: called. %i\n", uip_ipv4_len);
  uip_arp_out();
  uip_len = uip_ipv4_len;

  do_send();

  return 0;
}


void ipv4_interface_init() {
  uip_ipv4_len= 0;
  tcpip_ipv4_set_outputfunc(ipv4_send);

  memcpy(&uip_ethaddr,&uip_lladdr,6);

  process_exit(&tcpip_ipv4_process);
  process_start(&tcpip_ipv4_process,0);

  // TODO: This is redundant.  DHCP will be restarted later, anyway.
  process_exit(&dhcp_client_process);
  process_start(&dhcp_client_process,0);

}


