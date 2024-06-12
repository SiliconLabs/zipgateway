/* © 2018 Silicon Laboratories Inc.
 */

/** \defgroup test_gw_basics Basic gateway components that most tests need.
 * @{
 */

#include <stdint.h>

/* dummy for testing, should return constant */
/* Used by DBG_PRINTF and friends.  Should go in a separate file that stubs contiki/core/sys/clock.h */
unsigned long clock_time(void) {
    return 0;
}


/* For now... We probaby want to move these to a separate helper file when we want to use them in cases. */
/**
 * \ingroup test_gw_basics
 */ 
uint8_t MyNodeID = 1;
/**
 * \ingroup test_gw_basics
 */ 
uint32_t homeID = 0x7357F0F0;

/* @}
 */
