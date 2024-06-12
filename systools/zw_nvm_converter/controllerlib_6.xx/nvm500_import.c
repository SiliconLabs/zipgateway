/* © 2019 Silicon Laboratories Inc. */

#include "nvm500_import.h"
#include "nvm500_common.h"
#include "base64.h"
#include <json.h>
#include<string.h>
#include<stdio.h>
#include<time.h>
#include"json_helpers.h"
#include<assert.h>
#include<stdbool.h>
#include<stdint.h>

/**
 * This is the default product ID and product TYPE
 * Which is used for the serial api bridge in the SDK.
 * If a costumor has changed theese the values below 
 * must be updated.
 * 
 * The values below originates from 
 * PRODUCT_ID_SerialAPIPlus from the ZW_product_id_enum.h file in the embedded SDK 
 */
#define  APP_MANUFACTURER_ID 0x0000
#define  APP_PRODUCT_TYPE_ID 0x0008
#define  APP_PRODUCT_ID 0x0003
#define  APP_FIRMWARE_ID ( ( APP_PRODUCT_ID<<8 ) | APP_PRODUCT_TYPE_ID) 

typedef uint8_t nodemask_t[29];

int deserialize_descriptor_element(int element,const nvm_desc_t* d,json_object* obj ) {
    json_object* key;
    uint8_t* base = &nvm_buffer[ d->nvm_offset ];
    
    if(!obj) return 0;

    if( strcmp("BYTE",d->nvm_type)==0 ) {
        int  v = json_object_get_int(obj);
        base[0+element*1] = (v>>0) & 0xff;
    } else if((strcmp("WORD",d->nvm_type)==0) || (strcmp("t_NvmModuleSize",d->nvm_type)==0) ) {
        int  v = json_object_get_int(obj);
        base[0+element*2] = (v>>8) & 0xff;
        base[1+element*2] = (v>>0) & 0xff;
    } else if(strcmp("DWORD",d->nvm_type)==0) {
        int  v = json_object_get_int(obj);
        base[0+element*4] = (v>>24) & 0xff;
        base[1+element*4] = (v>>16) & 0xff;
        base[2+element*4] = (v>>8) & 0xff;
        base[3+element*4] = (v>>0) & 0xff;
    } else if(strcmp("NODE_MASK_TYPE",d->nvm_type)==0) {
        memset( base + element*29, 0,29);
        for(int i=0; i < json_object_array_length(obj) ; i++) {
            int n = json_object_get_int( json_object_array_get_idx( obj, i) )-1;
            base[element*29 + (n/8) ] |= 1<<(n&7);
        }
    } else if(strcmp("EX_NVM_NODEINFO",d->nvm_type)==0) {
        EX_NVM_NODEINFO* nif =(EX_NVM_NODEINFO*) (base + sizeof(EX_NVM_NODEINFO)*element); 

        if(!json_object_object_get_ex( obj,"capability",&key )) return -1;
        nif->capability = json_object_get_int(key);
        if(!json_object_object_get_ex( obj,"security",&key )) return -1;
        nif->security = json_object_get_int(key);
        if(!json_object_object_get_ex( obj,"reserved",&key )) return -1;
        nif->reserved = json_object_get_int(key);
        if(!json_object_object_get_ex( obj,"generic",&key )) return -1;
        nif->generic = json_object_get_int(key);
        if(!json_object_object_get_ex( obj,"specific",&key )) return -1;
        nif->specific = json_object_get_int(key);
    } else if(strcmp("ROUTECACHE_LINE",d->nvm_type)==0) {
        ROUTECACHE_LINE* rc_line =(ROUTECACHE_LINE*) (base + sizeof(ROUTECACHE_LINE)*element); 
        
        if(!json_object_object_get_ex( obj,"routecacheLineConf",&key )) return -1;
        rc_line->routecacheLineConfSize = json_object_get_int(key);
       
        if(!json_object_object_get_ex( obj,"repeaters",&key )) return -1;
       
        for(int i=0; (i < json_object_array_length(key)) && (i < MAX_REPEATERS) ; i++) {
            rc_line->repeaterList[i] = json_object_get_int(json_object_array_get_idx( key, i));;
        }
    } else if(strcmp("SUC_UPDATE_ENTRY_STRUCT",d->nvm_type)==0) {
        SUC_UPDATE_ENTRY_STRUCT* sue =(SUC_UPDATE_ENTRY_STRUCT*) (base + sizeof(SUC_UPDATE_ENTRY_STRUCT)*element); 
        if(!json_object_object_get_ex( obj,"nodeId",&key )) return -1;

        sue->NodeID = json_object_get_int(key);
        if(!json_object_object_get_ex( obj,"changeType",&key )) return -1;
        sue->changeType = json_object_get_int(key);
        if(!json_object_object_get_ex( obj,"nodeInfo",&key )) return -1;

        for(int i=0; (i < json_object_array_length(key)) && (i < SUC_UPDATE_NODEPARM_MAX) ; i++) {
            sue->nodeInfo[i] = json_object_get_int(json_object_array_get_idx( key, i));;
        }
    } else if(strcmp("t_nvmModuleDescriptor",d->nvm_type)==0) {
        //t_nvmModuleDescriptor* nmd =(t_nvmModuleDescriptor*) (base + sizeof(t_nvmModuleDescriptor)*element); 
        uint8_t* k = (uint8_t*) (base + sizeof(t_nvmModuleDescriptor)*element); 

        if(!json_object_object_get_ex( obj,"wNvmModuleSize",&key )) return -1;
        *k++ = (json_object_get_int(key)>>8);
        *k++ = (json_object_get_int(key)>>0);
        //nmd->wNvmModuleSize = htons(json_object_get_int(key));
        if(!json_object_object_get_ex( obj,"bNvmModuleType",&key )) return -1;
        *k++ = (json_object_get_int(key)>>0);
        //nmd->bNvmModuleType = json_object_get_int(key);
        if(!json_object_object_get_ex( obj,"wNvmModuleVersion",&key )) return -1;
        //nmd->wNvmModuleVersion = htons(json_object_get_int(key));
        *k++ = (json_object_get_int(key)>>8);
        *k++ = (json_object_get_int(key)>>0);

    } else {
        return 1;
    }
    return 0;
}

int deserialize_named_element_element(const char* name,int element,json_object* obj) {
    const nvm_desc_t* d = get_descriptor_by_name(name);
    return deserialize_descriptor_element(element,d,obj);
}


int deserialize_descriptor(const nvm_desc_t* d,json_object* obj ) {
    if(d->nvm_array_size>1) {
        for(int i=0; (i < d->nvm_array_size) && (i < json_object_array_length(obj) ) ; i++) {
            if(deserialize_descriptor_element(i, d, json_object_array_get_idx(obj,i) )) {
                return 1;
            }
        }
        return 0;
    } else {
        return deserialize_descriptor_element(0,d,obj);
    }
}

void nvm_fill(const char* key,uint16_t val) {
    const nvm_desc_t* d = get_descriptor_by_name(key);
    int elem_size=0;
    if(!d) return ;
    if(strcmp("BYTE",d->nvm_type)==0) elem_size=1;
    if(strcmp("EX_NVM_NODEINFO",d->nvm_type)==0) elem_size=sizeof(EX_NVM_NODEINFO);
    if(strcmp("NODE_MASK_TYPE",d->nvm_type)==0) elem_size=MAX_NODEMASK_LENGTH;
    if(strcmp("SUC_UPDATE_ENTRY_STRUCT",d->nvm_type)==0) elem_size=sizeof(SUC_UPDATE_ENTRY_STRUCT);
    if(strcmp("ROUTECACHE_LINE",d->nvm_type)==0) elem_size=sizeof(ROUTECACHE_LINE);

    assert(elem_size>0);

    for(int i=0; i < d->nvm_array_size*elem_size; i++) {
        nvm_buffer[d->nvm_offset+ i] = val;
    }
}
void nvm_write_word(const char* key, uint16_t val) {
    uint8_t *p = get_descriptor_data_by_name(key);
    if(!p) return;
    p[0] =  (val >> 8) & 0xff;;
    p[1] =  (val >> 0) & 0xff;;
}

void nvm_write_dword(const char* key, uint32_t val) {
    uint8_t *p = get_descriptor_data_by_name(key);
    if(!p) return;
    p[0] =  (val >> 24) & 0xff;;
    p[1] =  (val >> 16) & 0xff;;
    p[2] =  (val >> 8) & 0xff;;
    p[3] =  (val >> 0) & 0xff;;
}

void nvm_write_byte(const char* key, uint8_t val) {
    uint8_t *p = get_descriptor_data_by_name(key);
    *p =  (val >> 0) & 0xff;;
}

void nvm_write_nodemask(const char* key, const nodemask_t mask) {
    uint8_t *p = get_descriptor_data_by_name(key);
    if(!p) return;
    memcpy(p,mask,sizeof(nodemask_t));
}


void nvm_write_overall_nvm_descriptor(const char* key,const t_nvmDescriptor* p ) {
    uint8_t *np = get_descriptor_data_by_name(key);
    if(!np) return;

    np[0 ] =  (p->manufacturerID >> 8) & 0xff;;
    np[1 ] =  (p->manufacturerID >> 0) & 0xff;;
    np[2 ] =  (p->firmwareID >> 8) & 0xff;;
    np[3 ] =  (p->firmwareID >> 0) & 0xff;;
    np[4 ] =  (p->productTypeID >> 8) & 0xff;;
    np[5 ] =  (p->productTypeID >> 0) & 0xff;;
    np[6 ] =  (p->productID >> 8) & 0xff;;
    np[7 ] =  (p->productID >> 0) & 0xff;;
    np[8 ] =  (p->applicationVersion >> 8) & 0xff;;
    np[9 ] =  (p->applicationVersion >> 0) & 0xff;;
    np[10] =  (p->zwaveProtocolVersion >> 8) & 0xff;;
    np[11] =  (p->zwaveProtocolVersion >> 0) & 0xff;;
}


void nvm_write_nvm_descriptor(const char* key,const t_nvmModuleDescriptor* p ) {
    uint8_t *np = get_descriptor_data_by_name(key);
    if(!np) return;

    np[0 ] =  (p->wNvmModuleSize >> 8) & 0xff;;
    np[1 ] =  (p->wNvmModuleSize >> 0) & 0xff;;
    np[2 ] =  (p->bNvmModuleType >> 0) & 0xff;;
    np[3 ] =  (p->wNvmModuleVersion >> 0) & 0xff;;
    np[4 ] =  (p->wNvmModuleVersion >> 8) & 0xff;;
}


void nvm_write_json_base64_string(const char* nvm_key, json_object* parent, const char* json_key) {
    const nvm_desc_t* d = get_descriptor_by_name(nvm_key);
    json_object* value;

    if (d && json_get_object_error_check(parent, json_key, &value, json_type_string, JSON_OPTIONAL))
    {
        const char *base64_str = json_object_get_string(value);
        if (base64_str)
        {
            size_t decoded_len = 0;
            uint8_t *decoded_data = base64_decode(base64_str, strlen(base64_str), &decoded_len);
            if (decoded_data)
            {
                size_t num_bytes_to_copy = decoded_len;
                if (num_bytes_to_copy > d->nvm_array_size)
                {
                    // Don't overflow the dest buffer
                    num_bytes_to_copy = d->nvm_array_size;
                }
                memcpy(&nvm_buffer[d->nvm_offset], decoded_data, num_bytes_to_copy);
                free(decoded_data);
            }
        }
    }
}


void nvm_write_json_object(const char* nvm_key, json_object* parent,const char* json_key,json_type __type) {
    const nvm_desc_t* d = get_descriptor_by_name(nvm_key);
    json_object* value;
   
    if(d && json_get_object_error_check(parent,json_key,&value,__type,JSON_OPTIONAL) )
    {
        deserialize_descriptor(d,value);
    } else {
        //assert(0);
    }
}


size_t nvmlib6xx_json_to_nvm(json_object *jo, uint8_t **nvm_buf_ptr, size_t *nvm_size)
{
    json_object* jso = jo;
    json_register_root(jso);

    memset(nvm_buffer,0,sizeof(nvm_buffer));

    int nvmZWlibrarySize = get_descriptor_by_name("nvmApplicationSize")->nvm_offset-get_descriptor_by_name("nvmZWlibrarySize")->nvm_offset;
    nvm_write_word("nvmTotalEnd",get_descriptor_by_name("nvmModuleSizeEndMarker")->nvm_offset+1 );
    nvm_write_word("nvmZWlibrarySize",nvmZWlibrarySize );
    nvm_write_dword("NVM_INTERNAL_RESERVED_1_far",0x46524545); //ACSII "FREE"
    nvm_fill("NVM_INTERNAL_RESERVED_2_far",0);
    nvm_fill("NVM_INTERNAL_RESERVED_3_far",0);
    nvm_write_byte("NVM_CONFIGURATION_VALID_far",CONFIGURATION_VALID_0);
    nvm_write_byte("NVM_CONFIGURATION_REALLYVALID_far",CONFIGURATION_VALID_1);
    nvm_write_byte("EX_NVM_ROUTECACHE_MAGIC_far",ROUTECACHE_VALID);
    nvm_fill("NVM_PREFERRED_REPEATERS_far",0);
    nvm_fill("NVM_PENDING_DISCOVERY_far",0); //We are filling this because its longer than a node mask length.
    nvm_fill("NVM_RTC_TIMERS_far",0);
    nvm_fill("EX_NVM_ROUTECACHE_START_far",0);
    nvm_fill("EX_NVM_NODE_TABLE_START_far",0);
    nvm_fill("EX_NVM_ROUTING_TABLE_START_far",0);
    nvm_fill("EX_NVM_SUC_NODE_LIST_START_far",0);
    nvm_fill("EX_NVM_ROUTECACHE_NLWR_SR_START_far",0);

    t_nvmModuleDescriptor nvmZWlibraryDescriptor = {
        nvmZWlibrarySize,
        NVM_MODULE_TYPE_ZW_LIBRARY,
        _ZW_VERSION_
    };
    nvm_write_nvm_descriptor("nvmZWlibraryDescriptor",&nvmZWlibraryDescriptor);

    int nvmApplicationSize = get_descriptor_by_name("nvmHostApplicationSize")->nvm_offset-get_descriptor_by_name("nvmApplicationSize")->nvm_offset;
    nvm_write_word("nvmApplicationSize",nvmApplicationSize);
    nvm_write_byte("EEOFFSET_MAGIC_far",MAGIC_VALUE);
    t_nvmModuleDescriptor nvmApplicationDescriptor = {
        nvmApplicationSize,
        NVM_MODULE_TYPE_APPLICATION,
        _APP_VERSION_
    };
    nvm_write_nvm_descriptor("nvmApplicationDescriptor",&nvmApplicationDescriptor);

    int nvmHostApplicationSize = get_descriptor_by_name("nvmDescriptorSize")->nvm_offset-get_descriptor_by_name("nvmHostApplicationSize")->nvm_offset;
    nvm_write_word("nvmHostApplicationSize",nvmHostApplicationSize);

    t_nvmModuleDescriptor nvmHostApplicationDescriptor = {
        nvmHostApplicationSize,
        NVM_MODULE_TYPE_HOST_APPLICATION,
        _APP_VERSION_
    };
    nvm_write_nvm_descriptor("nvmHostApplicationDescriptor",&nvmHostApplicationDescriptor);

    int nvmDescriptorSize = get_descriptor_by_name("nvmModuleSizeEndMarker")->nvm_offset-get_descriptor_by_name("nvmDescriptorSize")->nvm_offset;
    nvm_write_word("nvmDescriptorSize",nvmDescriptorSize);

    t_nvmModuleDescriptor nvmDescriptorDescriptor = {
        nvmDescriptorSize,
        NVM_MODULE_TYPE_NVM_DESCRIPTOR,
        _APP_VERSION_
    };
    nvm_write_nvm_descriptor("nvmDescriptorDescriptor",&nvmDescriptorDescriptor);

    t_nvmDescriptor nvm_descriptor = {
        APP_MANUFACTURER_ID,                  /* WORD manufacturerID;             */
        APP_FIRMWARE_ID,                      /* WORD firmwareID;                 */
        APP_PRODUCT_TYPE_ID,                  /* WORD productTypeID;              */
        APP_PRODUCT_ID,                       /* WORD productID;                  */
        (WORD)_APP_VERSION_,                 /* WORD applicationVersion;         */
        (WORD)_ZW_VERSION_,                  /* WORD zwaveProtocolVersion;       */    
    };
    nvm_write_overall_nvm_descriptor("nvmDescriptor",&nvm_descriptor);

    json_object* jzwController;
    if(!json_object_object_get_ex( jso,"zwController",&jzwController )) {
        return 1;
    }

    nvm_write_json_object("NVM_NODEID_far",jzwController,"nodeId",json_type_int);
    
    nvm_write_dword("EX_NVM_HOME_ID_far",json_get_home_id(jzwController,"ownHomeId",0,0,JSON_REQUIRED));
    nvm_write_dword("NVM_HOMEID_far",json_get_home_id(jzwController,"learnedHomeId",0,0,JSON_OPTIONAL));

    nvm_write_json_object("EX_NVM_LAST_USED_NODE_ID_START_far",jzwController,"lastUsedNodeId",json_type_int);
    nvm_write_json_object("EX_NVM_CONTROLLER_CONFIGURATION_far",jzwController,"controllerConfiguration",json_type_int);
    nvm_write_json_object("EX_NVM_MAX_NODE_ID_far",jzwController,"lastUsedNodeId",json_type_int);
    nvm_write_json_object("NVM_SYSTEM_STATE",jzwController,"systemState",json_type_int);
    nvm_write_json_object("EX_NVM_STATIC_CONTROLLER_NODE_ID_START_far",jzwController,"staticControllerNodeId",json_type_int);


    /* Node table */
    json_object* jnodeTable;
    int maxNodeID=0;
    nodemask_t virtual_nodes;
    nodemask_t repeaters_nodes;
    nodemask_t sensor_nodes;
    nodemask_t pending_update_nodes;
    nodemask_t pending_discovery_nodes;
    nodemask_t route_slave_suc_nodes;
    nodemask_t applock;

    uint8_t *sucUpdateIndex=get_descriptor_data_by_name("EX_NVM_SUC_CONTROLLER_LIST_START_far");
    
    memset(virtual_nodes,0,sizeof(virtual_nodes));
    memset(sensor_nodes,0,sizeof(sensor_nodes));
    memset(pending_update_nodes,0,sizeof(pending_update_nodes));
    memset(pending_discovery_nodes,0,sizeof(pending_discovery_nodes));
    memset(route_slave_suc_nodes,0,sizeof(route_slave_suc_nodes));
    memset(sucUpdateIndex,254,ZW_MAX_NODES);
    memset(applock,0,sizeof(applock));

    /* Process the node table */
    if(json_object_object_get_ex( jzwController,"nodeTable",&jnodeTable )) {
        for(int i=0; i < json_object_array_length( jnodeTable); i++) {
            json_object* jnodeEntry = json_object_array_get_idx(jnodeTable,i);

            int nodeID =json_get_int(jnodeEntry,"nodeId",0,JSON_REQUIRED);
            int nodeIndex = nodeID -1;
            assert(nodeID>0);            
            if(nodeID > maxNodeID) maxNodeID = nodeID;

            json_object* jnif = json_get_object(jnodeEntry,"nodeInfo",0,JSON_REQUIRED);
            deserialize_named_element_element("EX_NVM_ROUTING_TABLE_START_far",nodeIndex,json_get_object(jnodeEntry,"neighbours",0,JSON_REQUIRED) );
            deserialize_named_element_element("EX_NVM_NODE_TABLE_START_far",nodeIndex,jnif );
            sucUpdateIndex[nodeIndex] = json_get_int(jnodeEntry,"controllerSucUpdateIndex",254,JSON_OPTIONAL);
            
            if(json_get_bool(jnodeEntry,"virtualNode",0,JSON_OPTIONAL))       virtual_nodes[ (nodeIndex/8) ] |= 1 << (nodeIndex&7);
            if(json_get_bool(jnodeEntry,"pendingUpdate",0,JSON_OPTIONAL))     pending_update_nodes[ (nodeIndex/8) ] |= 1 << (nodeIndex&7);
            if(json_get_bool(jnodeEntry,"pendingDiscovery",0,JSON_OPTIONAL))  pending_discovery_nodes[ (nodeIndex/8) ] |= 1 << (nodeIndex&7);
            if(json_get_bool(jnodeEntry,"routeSlaveSuc",0,JSON_OPTIONAL))     route_slave_suc_nodes[ (nodeIndex/8) ] |= 1 << (nodeIndex&7);
            if((json_get_int(jnif,"capability",0,JSON_OPTIONAL) & 0x80) == 0)  sensor_nodes[ (nodeIndex/8) ] |= 1 << (nodeIndex&7);

            json_object* jrouteCache;
            if(json_object_object_get_ex( jnodeEntry,"routeCache",&jrouteCache )) {
                if(json_get_bool(jrouteCache,"applock",0,JSON_OPTIONAL)) applock[ (nodeIndex/8) ] |= 1 << (nodeIndex&7);
                deserialize_named_element_element("EX_NVM_ROUTECACHE_START_far",nodeIndex,json_get_object(jrouteCache,"LWR",0,JSON_OPTIONAL) );
                deserialize_named_element_element("EX_NVM_ROUTECACHE_NLWR_SR_START_far",nodeIndex,json_get_object(jrouteCache,"NLWR",0,JSON_OPTIONAL) );
            }
        }
    } 
    
    nvm_write_nodemask("EX_NVM_BRIDGE_NODEPOOL_START_far",virtual_nodes);
    nvm_write_nodemask("EX_NVM_ZENSOR_TABLE_START_far",sensor_nodes);
    nvm_write_nodemask("NVM_PENDING_DISCOVERY_far",pending_discovery_nodes);
    nvm_write_nodemask("EX_NVM_PENDING_UPDATE_far",pending_update_nodes);
    nvm_write_nodemask("EX_NVM_SUC_ROUTING_SLAVE_LIST_START_far",route_slave_suc_nodes);
    nvm_write_nodemask("EX_NVM_ROUTECACHE_APP_LOCK_far",applock);
    nvm_write_byte("EX_NVM_MAX_NODE_ID_far",maxNodeID);

    /* SUC state */
    json_object* jsucState;
    if(json_object_object_get_ex( jzwController,"sucState",&jsucState )) {
        json_object* updateNodeList;

        nvm_write_json_object("EX_NVM_SUC_LAST_INDEX_START_far",jsucState,"lastIndex",json_type_int);

        if(json_object_object_get_ex( jsucState,"updateNodeList",&updateNodeList )) {
            for(int i=0; i < json_object_array_length( updateNodeList); i++) {

                json_object* jupdateEntry = json_object_array_get_idx(updateNodeList,i);
                deserialize_named_element_element("EX_NVM_SUC_NODE_LIST_START_far",i,jupdateEntry );
            }
        }
    }

    nvm_write_byte("EX_NVM_SUC_ACTIVE_START_far",0);
    nvm_fill("NVM_SECURITY0_KEY_far",0);
    nvm_fill("EEOFFSET_CMDCLASS_far",0);
    const nvm_desc_t* cmd_class_dest = get_descriptor_by_name("EEOFFSET_CMDCLASS_far");
    int cmd_class_len = json_get_bytearray(jzwController,"cmdClassList",&nvm_buffer[cmd_class_dest->nvm_offset],cmd_class_dest->nvm_array_size,false);
    nvm_write_byte("EEOFFSET_CMDCLASS_LEN_far",cmd_class_len);
    nvm_write_json_base64_string("EEOFFSET_HOST_OFFSET_START_far",jzwController,"applicationData");

    json_object* jappInfo500;
    if(json_object_object_get_ex( jzwController,"appConfig",&jappInfo500 )) {
        nvm_write_json_object("EEOFFSET_WATCHDOG_STARTED_far",jappInfo500,"watchdogStarted",json_type_int);
        nvm_write_json_object("EEOFFSET_POWERLEVEL_NORMAL_far",jappInfo500,"powerLevelNormal",json_type_null);
        nvm_write_json_object("EEOFFSET_POWERLEVEL_LOW_far",jappInfo500,"powerLevelLow",json_type_null);

        nvm_write_json_object("EEOFFSET_MODULE_POWER_MODE_far",jappInfo500,"powerMode",json_type_null);    
        nvm_write_json_object("EEOFFSET_MODULE_POWER_MODE_EXTINT_ENABLE_far",jappInfo500,"powerModeExtintEnable",json_type_int);
        nvm_write_json_object("EEOFFSET_MODULE_POWER_MODE_WUT_TIMEOUT_far",jappInfo500,"powerModeWutTimeout",json_type_null);
    }

    json_object_put(jso);

    if(json_parse_error_detected()) {
        return 0;
    } else
    {
        *nvm_size = get_descriptor_by_name("nvmModuleSizeEndMarker")->nvm_offset;
        *nvm_buf_ptr = (uint8_t*) malloc(*nvm_size);
        memcpy(*nvm_buf_ptr, nvm_buffer,*nvm_size);
        return *nvm_size;
    }         
}
    

