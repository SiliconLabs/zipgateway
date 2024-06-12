/* © 2014 Silicon Laboratories Inc.
 */

#ifndef IPV4_INTERFACE_H_
#define IPV4_INTERFACE_H_

/** \ingroup processes
 * \defgroup ZIP_ipv4 Z/IP IPV4 stack process
 * @{
 */

/** Initialize IPv4.
 *
 * Set outputfunc to ipv4_send, set uip_ethaddr, start
 * tcpip_ipv4_process.
 * Should only be done once.
*/
void ipv4_interface_init();


/** @} */
#endif /* IPV4_INTERFACE_H_ */
