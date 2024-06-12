/* © 2018 Silicon Laboratories Inc.  */

#ifndef PROBE_CC_VERSION_H
#define PROBE_CC_VERSION_H

#include "RD_internal.h"
/**
 * \defgroup cc_version_probe Command Class Version Probing State Machines
 * \ingroup ZIP_Resource
 *
 * \brief Overview of the state machines used by the version probing
 *
 * @{
 */


/** Trigger the version probing for one node and store it in its node entry(n->node_cc_versions)
 *
 * \param ep The endpoint to be probed
 * \param callback The callback when probing done
 */
void rd_ep_probe_cc_version(rd_ep_database_entry_t *ep, _pcvs_callback callback);

/**
 * @}
 */

#endif
