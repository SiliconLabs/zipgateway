/* © 2017 Silicon Laboratories Inc. */

#ifndef CC_POWERLEVEL_H_
#define CC_POWERLEVEL_H_

#include <stdbool.h>

/**
 * \ingroup CMD_handler
 * \defgroup pwlvl_CMD_handler Power Level Command Handler
 *
 * The Power Level command class supports setting and getting the
 * power level used for RF transmission and RF link test of network
 * nodes.
 *
 * \note This function should only be used in an install/test link
 * situation and the power level should always be set back to normal
 * Power when the testing is done.
 *
 * @{
 */

/**
 * Check that there is no Powerlevel test in progress.
 *
 * The test can be a power level test on a network node or a temporary
 * test setting of the power level of the Z/IP Gateway's bridge
 * controller.
 *
 * \return True if a power level test is in progress, false otherwise.
 */
bool PowerLevel_idle(void);

/** @} */
#endif /* CC_POWERLEVEL_H_ */
