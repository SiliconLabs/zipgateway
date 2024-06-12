
#ifndef SECURE_LEARNREQUIRED_H_
#define SECURE_LEARNREQUIRED_H_

#include "sc_types.h"
#include "Secure_learn.h"

#ifdef __cplusplus
extern "C" {
#endif 

/*! \file This header defines prototypes for all functions that are required by the state machine implementation.

This is a state machine uses time events which require access to a timing service. Thus the function prototypes:
	- secure_learn_setTimer and
	- secure_learn_unsetTimer
are defined.

This state machine makes use of operations declared in the state machines interface or internal scopes. Thus the function prototypes:
	- secure_learnIface_complete
	- secure_learnIface_send_commands_supported
	- secure_learnIfaceL_send_scheme_report
	- secure_learnIfaceL_set_inclusion_key
	- secure_learnIfaceL_send_key_verify
	- secure_learnIfaceL_new_keys
	- secure_learnIfaceL_save_state
	- secure_learnIfaceI_send_scheme_get
	- secure_learnIfaceI_send_key
	- secure_learnIfaceI_send_scheme_inherit
	- secure_learnIfaceI_restore_key
	- secure_learnIfaceI_register_scheme
are defined.

These functions will be called during a 'run to completion step' (runCycle) of the statechart. 
There are some constraints that have to be considered for the implementation of these functions:
	- never call the statechart API functions from within these functions.
	- make sure that the execution time is as short as possible.
 
*/
extern void secure_learnIface_complete(const sc_integer status);
extern void secure_learnIface_send_commands_supported();

extern void secure_learnIfaceL_send_scheme_report(const sc_integer node, const sc_integer txOptions);
extern void secure_learnIfaceL_set_inclusion_key();
extern void secure_learnIfaceL_send_key_verify(const sc_integer node, const sc_integer txOptions);
extern void secure_learnIfaceL_new_keys();
extern void secure_learnIfaceL_save_state();

extern void secure_learnIfaceI_send_scheme_get(const sc_integer node, const sc_integer txOptions);
extern void secure_learnIfaceI_send_key(const sc_integer node, const sc_integer txOptions);
extern void secure_learnIfaceI_send_scheme_inherit(const sc_integer node, const sc_integer txOptions);
extern void secure_learnIfaceI_restore_key();
extern void secure_learnIfaceI_register_scheme(const sc_integer node, const sc_integer scheme);


//
// This is a timed state machine that requires timer services
// 

//! This function has to set up timers for the time events that are required by the state machine.
/*! 
	This function will be called for each time event that is relevant for a state when a state will be entered.
	\param evid An unique identifier of the event.
	\time_ms The time in milli seconds
	\periodic Indicates the the time event must be raised periodically until the timer is unset 
*/
extern void secure_learn_setTimer(Secure_learn* handle, const sc_eventid evid, const sc_integer time_ms, const sc_boolean periodic);

//! This function has to unset timers for the time events that are required by the state machine.
/*! 
	This function will be called for each time event taht is relevant for a state when a state will be left.
	\param evid An unique identifier of the event.
*/
extern void secure_learn_unsetTimer(Secure_learn* handle, const sc_eventid evid);

#ifdef __cplusplus
}
#endif 

#endif /* SECURE_LEARNREQUIRED_H_ */
