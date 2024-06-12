
#ifndef SECURE_LEARN_H_
#define SECURE_LEARN_H_

#include "sc_types.h"

#ifdef __cplusplus
extern "C" { 
#endif 

/*! \file Header of the state machine 'secure_learn'.
*/

//! enumeration of all states 
typedef enum {
	Secure_learn_main_region_LearnMode,
	Secure_learn_main_region_LearnMode_r1_Start,
	Secure_learn_main_region_LearnMode_r1_Scheme_report,
	Secure_learn_main_region_LearnMode_r1_Fail,
	Secure_learn_main_region_LearnMode_r1_NewKey,
	Secure_learn_main_region_LearnMode_r1_Inherited,
	Secure_learn_main_region_LearnMode_r1_InsecureNet,
	Secure_learn_main_region_LearnMode_r1_end,
	Secure_learn_main_region_Idle,
	Secure_learn_main_region_InclusionMode,
	Secure_learn_main_region_InclusionMode_r1_SchemeRequest,
	Secure_learn_main_region_InclusionMode_r1_SendKey,
	Secure_learn_main_region_InclusionMode_r1_SendInterit,
	Secure_learn_main_region_InclusionMode_r1_Fail,
	Secure_learn_main_region_InclusionMode_r1_end,
	Secure_learn_main_region_InclusionMode_r1_waitVerify,
	Secure_learn_main_region_Complete,
	Secure_learn_main_region_SendReport,
	Secure_learn_main_region__final_,
	Secure_learn_last_state
} Secure_learnStates;

//! Type definition of the data structure for the Secure_learnIface interface scope.
typedef struct {
	sc_integer supported_schemes;
	sc_integer txOptions;
	sc_boolean isController;
	sc_integer node;
	sc_integer snode;
	sc_integer scheme;
	sc_integer net_scheme;
	sc_boolean learnRequest_raised;
	sc_boolean inclusionRequest_raised;
	sc_integer inclusionRequest_value;
	sc_boolean commandsSupportedRequest_raised;
	sc_boolean tx_done_raised;
	sc_boolean tx_fail_raised;
	sc_boolean scheme_get_raised;
	sc_integer scheme_get_value;
	sc_boolean scheme_inherit_raised;
	sc_integer scheme_inherit_value;
	sc_boolean key_set_raised;
	sc_integer key_set_value;
	sc_boolean scheme_report_raised;
	sc_integer scheme_report_value;
	sc_boolean key_verify_raised;
	sc_integer key_verify_value;
} Secure_learnIface;

//! Type definition of the data structure for the Secure_learnIfaceL interface scope.
typedef struct {
} Secure_learnIfaceL;

//! Type definition of the data structure for the Secure_learnIfaceI interface scope.
typedef struct {
} Secure_learnIfaceI;

//! Type definition of the data structure for the Secure_learnTimeEvents interface scope.
typedef struct {
	sc_boolean secure_learn_main_region_LearnMode_r1_Start_tev0_raised;
	sc_boolean secure_learn_main_region_LearnMode_r1_Scheme_report_tev0_raised;
	sc_boolean secure_learn_main_region_LearnMode_r1_NewKey_tev0_raised;
	sc_boolean secure_learn_main_region_InclusionMode_r1_SchemeRequest_tev0_raised;
	sc_boolean secure_learn_main_region_InclusionMode_r1_SendKey_tev0_raised;
	sc_boolean secure_learn_main_region_InclusionMode_r1_SendInterit_tev0_raised;
	sc_boolean secure_learn_main_region_InclusionMode_r1_waitVerify_tev0_raised;
} Secure_learnTimeEvents;


//! the maximum number of orthogonal states defines the dimension of the state configuration vector.
#define SECURE_LEARN_MAX_ORTHOGONAL_STATES 1

/*! Type definition of the data structure for the Secure_learn state machine.
This data structure has to be allocated by the client code. */
typedef struct {
	Secure_learnStates stateConfVector[SECURE_LEARN_MAX_ORTHOGONAL_STATES];
	sc_ushort stateConfVectorPosition; 
	
	Secure_learnIface iface;
	Secure_learnIfaceL ifaceL;
	Secure_learnIfaceI ifaceI;
	Secure_learnTimeEvents timeEvents;
} Secure_learn;

/*! Initializes the Secure_learn state machine data structures. Must be called before first usage.*/
extern void secure_learn_init(Secure_learn* handle);

/*! Activates the state machine */
extern void secure_learn_enter(Secure_learn* handle);

/*! Deactivates the state machine */
extern void secure_learn_exit(Secure_learn* handle);

/*! Performs a 'run to completion' step. */
extern void secure_learn_runCycle(Secure_learn* handle);

/*! Raises a time event. */
extern void secure_learn_raiseTimeEvent(Secure_learn* handle, sc_eventid evid);

/*! Gets the value of the variable 'supported_schemes' that is defined in the default interface scope. */ 
extern sc_integer secure_learnIface_get_supported_schemes(Secure_learn* handle);
/*! Gets the value of the variable 'txOptions' that is defined in the default interface scope. */ 
extern sc_integer secure_learnIface_get_txOptions(Secure_learn* handle);
/*! Sets the value of the variable 'txOptions' that is defined in the default interface scope. */ 
extern void secure_learnIface_set_txOptions(Secure_learn* handle, sc_integer value);
/*! Gets the value of the variable 'isController' that is defined in the default interface scope. */ 
extern sc_boolean secure_learnIface_get_isController(Secure_learn* handle);
/*! Sets the value of the variable 'isController' that is defined in the default interface scope. */ 
extern void secure_learnIface_set_isController(Secure_learn* handle, sc_boolean value);
/*! Gets the value of the variable 'node' that is defined in the default interface scope. */ 
extern sc_integer secure_learnIface_get_node(Secure_learn* handle);
/*! Sets the value of the variable 'node' that is defined in the default interface scope. */ 
extern void secure_learnIface_set_node(Secure_learn* handle, sc_integer value);
/*! Gets the value of the variable 'snode' that is defined in the default interface scope. */ 
extern sc_integer secure_learnIface_get_snode(Secure_learn* handle);
/*! Sets the value of the variable 'snode' that is defined in the default interface scope. */ 
extern void secure_learnIface_set_snode(Secure_learn* handle, sc_integer value);
/*! Gets the value of the variable 'scheme' that is defined in the default interface scope. */ 
extern sc_integer secure_learnIface_get_scheme(Secure_learn* handle);
/*! Sets the value of the variable 'scheme' that is defined in the default interface scope. */ 
extern void secure_learnIface_set_scheme(Secure_learn* handle, sc_integer value);
/*! Gets the value of the variable 'net_scheme' that is defined in the default interface scope. */ 
extern sc_integer secure_learnIface_get_net_scheme(Secure_learn* handle);
/*! Sets the value of the variable 'net_scheme' that is defined in the default interface scope. */ 
extern void secure_learnIface_set_net_scheme(Secure_learn* handle, sc_integer value);
/*! Raises the in event 'learnRequest' that is defined in the default interface scope. */ 
extern void secure_learnIface_raise_learnRequest(Secure_learn* handle);

/*! Raises the in event 'inclusionRequest' that is defined in the default interface scope. */ 
extern void secure_learnIface_raise_inclusionRequest(Secure_learn* handle, sc_integer value);

/*! Raises the in event 'commandsSupportedRequest' that is defined in the default interface scope. */ 
extern void secure_learnIface_raise_commandsSupportedRequest(Secure_learn* handle);

/*! Raises the in event 'tx_done' that is defined in the default interface scope. */ 
extern void secure_learnIface_raise_tx_done(Secure_learn* handle);

/*! Raises the in event 'tx_fail' that is defined in the default interface scope. */ 
extern void secure_learnIface_raise_tx_fail(Secure_learn* handle);

/*! Raises the in event 'scheme_get' that is defined in the default interface scope. */ 
extern void secure_learnIface_raise_scheme_get(Secure_learn* handle, sc_integer value);

/*! Raises the in event 'scheme_inherit' that is defined in the default interface scope. */ 
extern void secure_learnIface_raise_scheme_inherit(Secure_learn* handle, sc_integer value);

/*! Raises the in event 'key_set' that is defined in the default interface scope. */ 
extern void secure_learnIface_raise_key_set(Secure_learn* handle, sc_integer value);

/*! Raises the in event 'scheme_report' that is defined in the default interface scope. */ 
extern void secure_learnIface_raise_scheme_report(Secure_learn* handle, sc_integer value);

/*! Raises the in event 'key_verify' that is defined in the default interface scope. */ 
extern void secure_learnIface_raise_key_verify(Secure_learn* handle, sc_integer value);





/*! Checks if the specified state is active. */
extern sc_boolean secure_learn_isActive(Secure_learn* handle, Secure_learnStates state);

#ifdef __cplusplus
}
#endif 

#endif /* SECURE_LEARN_H_ */
