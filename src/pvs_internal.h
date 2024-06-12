/* © 2014 Silicon Laboratories Inc.
 */

#ifndef _PVS_INTERNAL_H
#define _PVS_INTERNAL_H

#include "provisioning_list.h"

/**
 * \ingroup pvslist
 * \defgroup pvslist_int Provisioning List Internals
 * @{
 */

/*@dependent@*//*@null@*/struct pvs_tlv * pvs_tlv_get(/*@null@*/struct pvs_tlv *tlv, uint8_t type);

/** Release all tlvs */
void pvs_tlv_clear(/*@null@*/struct pvs_tlv *tlv);

/**
 * @}
 */

#endif
