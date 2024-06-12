/* © 2019 Silicon Laboratories Inc. */
#if !defined(NVM500_COMMON_H)
#define NVM500_COMMON_H

#include "nvm_desc.h"


typedef struct {
    const char* name;
    const nvm_desc_t* const nvm_entries;
    int nvm_entries_count;
    int app_version;
    int zw_version;
} supported_protocols_t;

extern const supported_protocols_t  supported_protocols[];
extern const int supported_protocols_count;
extern const supported_protocols_t* target_protocol;
extern uint8_t nvm_buffer[0x10000];


typedef enum {BRIDGE_6_8,STATIC_6_8,BRIDGE_6_7,STATIC_6_7,BRIDGE_6_6,STATIC_6_6} prtocols_t;

/**
 * Set the target protocol.
 * Returns true if the protocol is known, false otherwise.
 */
int set_tartget_protocol(const char* name);

/**
 * Set the target protocol identified by its ID.
 */
int set_tartget_protocol_by_id(prtocols_t protocol);


/**
 * Retrieved a descriptor with a given name. If the descriptor cannot be found,
 * null is returned.
 */
const nvm_desc_t* get_descriptor_by_name(const char* descriptor_name);


/**
 * Return a pointer into the NVM buffer, which points to
 * the descriptor which has the name @parm name.
 *
 * If the descriptor cannot be found, NULL is returned.
 */
uint8_t* get_descriptor_data_by_name(const char* descriptor_name);

#endif