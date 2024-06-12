/* © 2014 Silicon Laboratories Inc.
 */

#ifndef IPV46_IF_HANDLER_H_
#define IPV46_IF_HANDLER_H_

#include <stdint.h>
#include "TYPES.H"

/** \defgroup ip46natdriver IPv4 to IPv6 NAT-component I/O functions.
 * \ingroup ip46nat
 *
 * This submodule is used from Contiki, almost directly on the LAN
 * interface.
 *
 * It maps incoming IPv4 packets to internal (IPv4-mapped) IPv6
 * packets and outgoing (IPv4-mapped) IPv6 packets to IPv4.
 *
 * It operates directly on Contiki's uip_buf.
 *
 * @{
 */


/**
 * Input of the NAT interface driver.  If the destination of the IP address is a NAT address,
 * this will translate the IPv4 packet in  uip_buf to a IPv6 packet,
 * If the address is not a NAT'ed address, this function will not change the uip_buf.
 *
 * If the destination address is the the uip_hostaddr and the destination
 * UDP port is the Z-Wave port, translation will be performed. Otherwise, it will not.
 */
void ipv46nat_interface_input();

/**
 * Translate the the packet in uip_buf from a IPv6 packet to a IPv4 packet if the packet
 * in uip_buf is a mapped IPv4 packet.
 *
 * This functions updates uip_buf and uip_len with the translated IPv4 packet.
  */
uint8_t ipv46nat_interface_output();

/** @} */

#endif
