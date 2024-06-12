/* © 2014 Silicon Laboratories Inc.
 */

#ifndef ZIP_ROUTER_H_
#define ZIP_ROUTER_H_

/**
 * \ingroup processes
 *
 * \defgroup ZIP_Router Z/IP Router Main process for zipgateway
 * This process is main process which spawns of series of sub processes
 * It is responsible for
 * handling all incoming events and perform actions from sub processes.
 * @{
 */
#include "net/uip.h"
#include "TYPES.H"
#include "ZW_SendDataAppl.h" /* ts_param_t */
#include "pkgconfig.h"

#include "router_events.h"

#include "ZIP_Router_logging.h"
#include "zip_router_config.h"
#include "ZW_types.h"
#include "RD_types.h"

/**
 * Virtual mac address pf the tunnel interface
 */
extern const uip_lladdr_t tun_lladdr;

/**
 * Callback LAN output function.
 * \param a destination link layer (MAC) address. If zero the packet must be broadcasted
 *          packet to be transmitted is in uip_buf buffer and the length of the packet is
 *          uip_len
 *
 */
typedef  u8_t(* outputfunc_t)(uip_lladdr_t *a);

/**
 * The set output function for the LAN
 * Output function for the IP packets
 */
void set_landev_outputfunc(outputfunc_t f);

/*Forward declaration*/
union _ZW_APPLICATION_TX_BUFFER_;

/** Application command handler for Z-wave packets. The packets might be the decrypted packets
 * \param p description of the send,receive params
 * \param pCmd Pointer to the command received
 * \param cmdLength Length of the command received
 */
void ApplicationCommandHandlerZIP(ts_param_t *p,
		union _ZW_APPLICATION_TX_BUFFER_ *pCmd, WORD cmdLength) CC_REENTRANT_ARG;

/** Application command handler for raw Z-wave packets. 
 * \see ApplicationCommandHandler
 */
void ApplicationCommandHandlerSerial(BYTE rxStatus, nodeid_t destNode, nodeid_t sourceNode,
    union _ZW_APPLICATION_TX_BUFFER_ *pCmd, BYTE cmdLength)CC_REENTRANT_ARG;


/** Check if the ZGW is idle.
 *
 * The zipgateway is idle if it is not reconfiguring or handling data.
 *
 * \return True if the zipgateway is idle, false otherwise.
 */
bool zgw_idle(void);

/**
 * Returns True is address is a classic zwave address.
 * \param ip IP address
 */
BYTE isClassicZWAddr(uip_ip6addr_t* ip);

/* TODO: Move ? */
#ifdef __ASIX_C51__
void Reset_Gateway(void) CC_REENTRANT_ARG;

#endif


/** @} */ // end of ZIP_Router
#endif /* ZIP_ROUTER_H_ */
