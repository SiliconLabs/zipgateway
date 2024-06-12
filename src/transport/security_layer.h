/* © 2014 Silicon Laboratories Inc.
 */

#ifndef SEC0_LEARN_H_
#define SEC0_LEARN_H_

#include "ZW_SendDataAppl.h"
/** \ingroup Security_Scheme
 *
 * @{
 */

/**
 * Indicate that the secure inclusion or
 * learn mode has completed.
 *
 * @param status < 0 for failure 0 for non-secure >0 for secure .
 */
typedef void (*sec_learn_complete_t)(int status);

/**
 * Initialize the state machine one-time.
 */
void security_init();

/** Set the supported security classes.
 * \param classes pointer to a byte array of command classes which are supported secure.
 * \param n_classes length of the byte array
 */
void security_set_supported_classes(u8_t* classes, u8_t n_classes);
/**
 * Reset security states.
 */
void security_set_default();
/**
 * Begin the learn mode.
 * On completion, the callback function is called with scheme mask negotiated for
 * this node. An empty scheme mask means that the secure negotiation has failed.
 * \param __cb callback function.
 */
void security_learn_begin(sec_learn_complete_t __cb);

/**
 * Begin the add node mode.
 * On completion, the callback function is called with scheme mask negotiated for
 * the new node. An empty scheme mask means that the secure negotiation has failed.
 * Call this only for nodes which support security.
 *
 * \param node Node to negotiate with
 * \param txOptions Transmit options
 * \param controller TRUE if the node is a controller
 * \param __cb callback function.
 */
u8_t security_add_begin(u8_t node, u8_t txOptions, BOOL controller, sec_learn_complete_t __cb);

/**
 * Input handler for the security state machine.
 * \param p Reception parameters
 * \param pCmd Payload from the received frame
 * \param cmdLength Number of command bytes including the command
 */

void security_CommandHandler(ts_param_t* p,
    const ZW_APPLICATION_TX_BUFFER *pCmd,
    BYTE cmdLength);



/** Query if an S0 key is granted.
This function checks if an S0 key has been written to the keystore.
\returns TRUE if S0 key has been granted, FALSE otherwise.

*/
int8_t is_sec0_key_granted ();


/**
 * State machine polling.
 */
void secure_poll();

/**
 * \return TRUE if we are in learnmode or add node
 */
uint8_t secure_learn_active();



/**
 * Abort the current inclusion.
 */
void sec0_abort_inclusion();

/**
 * @}
 */
#endif /* SEC0_LEARN_H_ */
