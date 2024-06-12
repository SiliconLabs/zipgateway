/* © 2017 Silicon Laboratories Inc.
 */
/*
 * S2_wrap.h
 *
 *  Created on: Aug 25, 2015
 *      Author: aes
 */

#ifndef SRC_TRANSPORT_S2_WRAP_H_
#define SRC_TRANSPORT_S2_WRAP_H_
#include "ZW_SendDataAppl.h"
#include "ZW_classcmd.h"
typedef void (*sec2_inclusion_cb_t)(int);

/*Initialize the s2 layer*/
void sec2_init();


uint8_t sec2_send_data(ts_param_t* p,uint8_t* data, uint16_t len,ZW_SendDataAppl_Callback_t callback,void* user);

uint8_t sec2_send_multicast(ts_param_t *p, const uint8_t *data, uint8_t data_len, BOOL send_sc_followups, ZW_SendDataAppl_Callback_t callback, void *user);

void sec2_abort_multicast(void);

void /*RET Nothing                  */
security2_CommandHandler(ts_param_t* p,
const ZW_APPLICATION_TX_BUFFER *pCmd, /* IN Payload from the received frame, the union */
uint16_t cmdLength); /* IN Number of command bytes including the command */

void sec2_start_learn_mode(nodeid_t node_id, sec2_inclusion_cb_t cb);

/** Tell libs2 that the gateway has been included (ie, to start the TB1 timer).
 */
void sec2_inclusion_neighbor_discovery_complete();

void sec2_start_add_node(nodeid_t node_id, sec2_inclusion_cb_t cb );


/**
 * Create a new Elliptic curve key for learn mode
 */
void sec2_create_new_static_ecdh_key();



/**
 * Create a new Elliptic curve key for add node mode
 */
void sec2_create_new_dynamic_ecdh_key();


/**
 * Create new network keys
 */
void sec2_create_new_network_keys();


/**
 * Get the node cache security flags for this node
 */
uint8_t sec2_get_my_node_flags();


/*
 * grant keys durring inclusion
 */
void sec2_key_grant(uint8_t accept, uint8_t keys,uint8_t csa);
void sec2_dsk_accept(uint8_t accept, uint8_t* dsk, uint8_t len);

/**
 * Convert gateway security flags to keystore security flags
 */
uint8_t sec2_gw_node_flags2keystore_flags(uint8_t gw_flags);


/**
 * Abort the current secure add /learn
 */
void sec2_abort_join();

/*
 * Refresh the S2 homeID used for authentication with the currently used homeID in the ZIP_Router
 * module. Reads the global homeID variable to discover the updated homeID.
 *
 * This function allows sec2 joining to be initialized/started already when entering learn mode.
 * With this function, we can subsequently update the homeID information when it becomes available
 * and handle the race condition where the Kex Get arrives before we know the homeID. See ZGW-813.
 * */
void sec2_refresh_homeid(void);

/**
 * Refresh the inclusion peer used during s2 inclusion.
 * * This function allows sec2 joining to be initialized/started already when entering learn mode.
 * With this function, we can subsequently update the information when it becomes available
 * and handle the race condition where the Kex Get arrives before we know the homeID. See ZGW-813.
 */
void sec2_set_inclusion_peer(uint8_t remote_nodeID, uint8_t local_nodeID);

/**
 * Generate S2 and S0 network keys if they are missing.
 */
void keystore_network_generate_key_if_missing();

/**
 * Persist S2 SPAN Table to DataStore
 */
void sec2_persist_span_table();

/**
 * Unpersist S2 SPAN Table from DataStore
 */
void sec2_unpersist_span_table();
/**
 * Reset all the SPANs from SPAN table with node as destination node id
 * @param node: Destination node id to match to reset the SPANs
 */
void sec2_reset_span(nodeid_t node);
#endif /* SRC_TRANSPORT_S2_WRAP_H_ */
