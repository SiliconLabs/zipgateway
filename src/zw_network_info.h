/* © 2019 Silicon Laboratories Inc. */

#ifndef ZW_NETWORK_INFO_H_
#define ZW_NETWORK_INFO_H_

#include <stdint.h>
#include "RD_types.h"

/**
 * @{
 * \ingroup ZIP_Router
 */


/** Node ID of this zipgateway. */
extern nodeid_t MyNodeID; 

/** Home ID of this zipgateway.
 *
 * In network byte order (big endian).
 */
extern uint32_t homeID;

typedef enum {
    SECONDARY,
    SUC,
    SLAVE
} controller_role_t;
    
extern controller_role_t controller_role;

/** @} */
#endif /* ZW_NETWORK_INFO_H_ */
