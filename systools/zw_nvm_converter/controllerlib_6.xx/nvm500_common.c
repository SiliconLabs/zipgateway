/* © 2019 Silicon Laboratories Inc. */
#include "nvm500_common.h"

#include "bridge_6_8.h"
#include "static_6_8.h"
#include "bridge_6_7.h"
#include "static_6_7.h"
#include "bridge_6_6.h"
#include "static_6_6.h"

#include<string.h>

/**
 * 
 * Database of 500 serris protocol descriptions
 * 
 */
const supported_protocols_t  supported_protocols[] = {
    {"bridge_6_8", bridge_6_8,sizeof(bridge_6_8) / sizeof(nvm_desc_t),bridge_6_8_app_version,bridge_6_8_zw_version},
    {"static_6_8", static_6_8,sizeof(static_6_8) / sizeof(nvm_desc_t),static_6_8_app_version,static_6_8_zw_version},
    {"bridge_6_7", bridge_6_7,sizeof(bridge_6_7) / sizeof(nvm_desc_t),bridge_6_7_app_version,bridge_6_7_zw_version},
    {"static_6_7", static_6_7,sizeof(static_6_7) / sizeof(nvm_desc_t),static_6_7_app_version,static_6_7_zw_version},
    {"bridge_6_6", bridge_6_6,sizeof(bridge_6_6) / sizeof(nvm_desc_t),bridge_6_6_app_version,bridge_6_6_zw_version},
    {"static_6_6", static_6_6,sizeof(static_6_6) / sizeof(nvm_desc_t),static_6_6_app_version,static_6_6_zw_version},

};




const int supported_protocols_count = sizeof(supported_protocols) / sizeof(supported_protocols_t);
const supported_protocols_t* target_protocol=&supported_protocols[0];
uint8_t nvm_buffer[0x10000];

uint16_t _APP_VERSION_;
uint16_t _ZW_VERSION_;
int set_tartget_protocol(const char* name) {
    target_protocol=0;
    for(int i=0; i < supported_protocols_count; i++) {
        if(strcmp(name, supported_protocols[i].name) == 0) {
            target_protocol = &supported_protocols[i];
            _APP_VERSION_ = supported_protocols[i].app_version;
            _ZW_VERSION_ = supported_protocols[i].zw_version;
            return 1;
        }
    }
    return 0;
}
#include<stdio.h>
int set_tartget_protocol_by_id(prtocols_t protocol){
    target_protocol = &supported_protocols[protocol];
    _APP_VERSION_ = supported_protocols[protocol].app_version;
    _ZW_VERSION_ = supported_protocols[protocol].zw_version;
    return 0;
}


const nvm_desc_t* get_descriptor_by_name(const char* name) {
    for(int i=0; i < target_protocol->nvm_entries_count; i++) {
        if(strcmp(name,target_protocol->nvm_entries [i].nvm_var )==0) {
            return &target_protocol->nvm_entries[i];
        }
    }
    return NULL;
}

uint8_t* get_descriptor_data_by_name(const char* name) {
    const nvm_desc_t* d = get_descriptor_by_name(name);
    if(d) {
        return &nvm_buffer[d->nvm_offset];
    } else {
        return 0;
    }
}
