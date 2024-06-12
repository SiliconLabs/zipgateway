/* © 2019 Silicon Laboratories Inc. */
#include "nvm500_export.h"
#include "nvm500_common.h"
#include "user_message.h"
#include "base64.h"

#include<json.h>
#include<string.h>
#include<stdio.h>
#include<time.h>
#include<stdio.h>

#define BIT_IN_MASK(m,b) (m[  b >>3] & (1<<(b&7)))

static json_object* serialize_descriptor_element(int element,const nvm_desc_t* d ) {
    uint8_t* base = &nvm_buffer[ d->nvm_offset ];

    if( strcmp("BYTE",d->nvm_type)==0 ) {
        return json_object_new_int( base[0+element] & 0xff );
    } else if((strcmp("WORD",d->nvm_type)==0) || (strcmp("t_NvmModuleSize",d->nvm_type)==0) ) {
        return json_object_new_int( ((base[2*element+0] << 8) | base[2*element+1]) & 0xffff );
    } else if(strcmp("DWORD",d->nvm_type)==0) {
        return json_object_new_int( 
                (base[4*element+0]  << 24) | (base[4*element+1] <<16) |
                (base[4*element+2]  << 8)  |  base[4*element+3] 
            );
    } else if(strcmp("NODE_MASK_TYPE",d->nvm_type)==0) {
        json_object* nodelist = json_object_new_array();
        for(int i=0; i < 232; i++) {
            if( base[element*29 + (i/8) ] & (1<<(i&7) )) {
                json_object_array_add( nodelist , json_object_new_int(i+1) );
            } 
        }
        return nodelist;
    } else if(strcmp("EX_NVM_NODEINFO",d->nvm_type)==0) {
        EX_NVM_NODEINFO* nif =(EX_NVM_NODEINFO*) (base + sizeof(EX_NVM_NODEINFO)*element); 
        json_object* jnif = json_object_new_object();
        json_object_object_add( jnif,"capability", json_object_new_int(nif->capability));
        json_object_object_add( jnif,"security", json_object_new_int( nif->security ));
        json_object_object_add( jnif,"reserved", json_object_new_int( nif->reserved ));
        json_object_object_add( jnif,"generic", json_object_new_int( nif->generic));
        json_object_object_add( jnif,"specific", json_object_new_int( nif->specific));
        return jnif;
    } else if(strcmp("ROUTECACHE_LINE",d->nvm_type)==0) {
        ROUTECACHE_LINE* rc_line =(ROUTECACHE_LINE*) (base + sizeof(ROUTECACHE_LINE)*element); 
        json_object* jrc_line = json_object_new_object();
        json_object_object_add( jrc_line,"routecacheLineConf", json_object_new_int(rc_line->routecacheLineConfSize));
        json_object* jrepeaters = json_object_new_array();
        for(int i=0; i < MAX_REPEATERS; i++) {
            json_object_array_add( jrepeaters, json_object_new_int( rc_line->repeaterList[i] ));
        }
        json_object_object_add( jrc_line,"repeaters", jrepeaters);
        return jrc_line;
    } else if(strcmp("SUC_UPDATE_ENTRY_STRUCT",d->nvm_type)==0) {
        SUC_UPDATE_ENTRY_STRUCT* sue =(SUC_UPDATE_ENTRY_STRUCT*) (base + sizeof(SUC_UPDATE_ENTRY_STRUCT)*element); 
        json_object* jsue = json_object_new_object();
        json_object_object_add( jsue,"nodeId", json_object_new_int(sue->NodeID));
        json_object_object_add( jsue,"changeType", json_object_new_int(sue->changeType));
        json_object* jni = json_object_new_array();
        for(int i=0; i < SUC_UPDATE_NODEPARM_MAX; i++) {
            if (sue->nodeInfo[i] != 0)
            {
                json_object_array_add( jni, json_object_new_int( sue->nodeInfo[i] ) );
            }
        }
        json_object_object_add( jsue,"nodeInfo", jni);

        return jsue;
    } else if(strcmp("t_nvmModuleDescriptor",d->nvm_type)==0) {
        //t_nvmModuleDescriptor* nmd =(t_nvmModuleDescriptor*) (base + sizeof(t_nvmModuleDescriptor)*element); 
        uint8_t* k = (uint8_t*)(base + sizeof(t_nvmModuleDescriptor)*element); 
        json_object* jnmd = json_object_new_object();
        int wNvmModuleSize = (k[0]) << 8 | (k[1]);
        int bNvmModuleType = k[2];  
        int wNvmModuleVersion = (k[3]) << 8 | (k[4]);
        json_object_object_add( jnmd,"wNvmModuleSize", json_object_new_int(wNvmModuleSize));
        json_object_object_add( jnmd,"bNvmModuleType", json_object_new_int(bNvmModuleType));
        json_object_object_add( jnmd,"wNvmModuleVersion", json_object_new_int(wNvmModuleVersion));
        return jnmd;
    } else {
        return NULL;
    }
}


static json_object* serialize_descriptor(const nvm_desc_t* d ) {
    if(d->nvm_array_size>1) {
        json_object* array = json_object_new_array();

        for(int i=0; i <d->nvm_array_size; i++) {
            json_object_array_add( array, serialize_descriptor_element(i,d) );
        }
        return array;
    } else {
        return serialize_descriptor_element(0,d);
    }
}



static void add_nvm_object_simple(json_object* parent, const char* key, const char* nvm_name) {
    const nvm_desc_t* d;
    d = get_descriptor_by_name(nvm_name);
    if(d==NULL) return;
    json_object* child = serialize_descriptor(d);
    json_object_object_add( parent,key,child);
}

static void add_nvm_object_base64(json_object* parent, const char* key, const char* nvm_name, bool skip_trailing_nulls) {
    const nvm_desc_t* d;
    d = get_descriptor_by_name(nvm_name);
    if(d==NULL) return;

    int n = d->nvm_array_size;
    if (skip_trailing_nulls)
    {
        while ((n > 0) && (0 == nvm_buffer[d->nvm_offset + n - 1]))
        {
            --n;
        }
    }

    if (n > 0)
    {
        char *base64_str = base64_encode(&nvm_buffer[d->nvm_offset],
                                         n,
                                         0);
        if (base64_str)
        {
            json_object* child = json_object_new_string(base64_str);
            json_object_object_add(parent, key, child);
            free(base64_str);
        }
    }
}

static void add_nvm_object_homeid(json_object* parent, const char* key, const char* nvm_name) {
    char homeIDstr[11];
    const nvm_desc_t* d;
    d = get_descriptor_by_name(nvm_name);
    snprintf(homeIDstr,sizeof(homeIDstr),"0x%08X", 
        nvm_buffer[d->nvm_offset]<<24 |nvm_buffer[d->nvm_offset+1]<<16 |
        nvm_buffer[d->nvm_offset+2]<<8 |nvm_buffer[d->nvm_offset+3]<<0) ;
    
    json_object* child = json_object_new_string(homeIDstr);
    json_object_object_add( parent,key,child);

}

static void add_nvm_object_cmdclass_list(json_object* parent, const char* key) {
    const char homeIDstr[11];
    const nvm_desc_t* d;
    d = get_descriptor_by_name("EEOFFSET_CMDCLASS_LEN_far");
    uint8_t cmdclass_len = nvm_buffer[d->nvm_offset];

    d = get_descriptor_by_name("EEOFFSET_CMDCLASS_far");
    json_object* array = json_object_new_array();

    for(int i=0; i <cmdclass_len; i++) {
        json_object_array_add( array, serialize_descriptor_element(i,d) );
    }
    json_object_object_add( parent,key,array);
}


static void add_nvm_object_node_table(json_object* parent, const char* key) {
    const nvm_desc_t* nt_d;
    const nvm_desc_t* rt_d;
    const nvm_desc_t* lwr_d;
    const nvm_desc_t* nlwr_d;

    uint8_t *pending_updates=get_descriptor_data_by_name("EX_NVM_PENDING_UPDATE_far");
    uint8_t *pending_discovery=get_descriptor_data_by_name("NVM_PENDING_DISCOVERY_far");
    uint8_t *virtual=get_descriptor_data_by_name("EX_NVM_BRIDGE_NODEPOOL_START_far");
    uint8_t *sucUpdateIndex=get_descriptor_data_by_name("EX_NVM_SUC_CONTROLLER_LIST_START_far");
    uint8_t* app_lock_mask=get_descriptor_data_by_name("EX_NVM_ROUTECACHE_APP_LOCK_far");
    uint8_t* route_slave_mask=get_descriptor_data_by_name("EX_NVM_SUC_ROUTING_SLAVE_LIST_START_far");

    rt_d = get_descriptor_by_name("EX_NVM_ROUTING_TABLE_START_far");
    lwr_d = get_descriptor_by_name("EX_NVM_ROUTECACHE_START_far");
    nlwr_d = get_descriptor_by_name("EX_NVM_ROUTECACHE_NLWR_SR_START_far");
    nt_d = get_descriptor_by_name("EX_NVM_NODE_TABLE_START_far");

    EX_NVM_NODEINFO* ni = (EX_NVM_NODEINFO*)&nvm_buffer[nt_d->nvm_offset];
    
    json_object* array = json_object_new_array();
    for(int n = 0; n < ZW_MAX_NODES-1; n++) {
        if(ni[n].generic) {
            json_object* e = json_object_new_object();
            json_object_object_add(e, "nodeId",json_object_new_int(n+1));
            if(virtual) {
                json_object_object_add(e, "virtualNode", json_object_new_boolean( BIT_IN_MASK(virtual,n)) );
            }
            json_object_object_add(e, "pendingUpdate", json_object_new_boolean( BIT_IN_MASK(pending_updates,n)) );
            json_object_object_add(e, "pendingDiscovery", json_object_new_boolean( BIT_IN_MASK(pending_discovery,n)) );
            json_object_object_add(e, "routeSlaveSuc",json_object_new_boolean( BIT_IN_MASK(route_slave_mask, n)) );
            json_object_object_add(e, "controllerSucUpdateIndex",json_object_new_int(sucUpdateIndex[n]));
            json_object_object_add(e, "neighbours",serialize_descriptor_element(n,rt_d));
            json_object_object_add(e, "nodeInfo",serialize_descriptor_element(n,nt_d));

            json_object* rc = json_object_new_object();
            json_object_object_add(rc, "applock",json_object_new_boolean( BIT_IN_MASK(app_lock_mask, n)) );
            json_object_object_add(rc, "LWR",serialize_descriptor_element(n,lwr_d));
            json_object_object_add(rc, "NLWR",serialize_descriptor_element(n,nlwr_d));
            json_object_object_add(e, "routeCache",rc);

            json_object_array_add( array,e);
        }
    }
    json_object_object_add( parent,key,array);
}


static void add_nvm_object_node_suc_state(json_object* parent, const char* key) {
    const nvm_desc_t* snl_d;

    snl_d = get_descriptor_by_name("EX_NVM_SUC_NODE_LIST_START_far");

    json_object* suc_sucstate = json_object_new_object();
    add_nvm_object_simple(suc_sucstate,"lastIndex","EX_NVM_SUC_LAST_INDEX_START_far");

    json_object* array = json_object_new_array();
    json_object_object_add( suc_sucstate,"updateNodeList",array);

    SUC_UPDATE_ENTRY_STRUCT* sue = (SUC_UPDATE_ENTRY_STRUCT*)&nvm_buffer[snl_d->nvm_offset];
    
    for(int n = 0; n < snl_d->nvm_array_size; n++) {
        json_object_array_add( array, serialize_descriptor_element(n,snl_d) );
    }
    json_object_object_add( parent,key,suc_sucstate);

}

bool nvmlib6xx_is_nvm_valid(const uint8_t *nvm_image, size_t nvm_image_size) {
    bool is_valid;
    if(nvm_image_size > sizeof(nvm_buffer)) {
        user_message( MSG_ERROR, "NVM image exceeds buffer size\n");
        return false;
    }
    memcpy(nvm_buffer,nvm_image,nvm_image_size);

    uint8_t *eeoffset_magic = get_descriptor_data_by_name("EEOFFSET_MAGIC_far");
    uint8_t *configuration_valid_0 = get_descriptor_data_by_name("NVM_CONFIGURATION_VALID_far");
    uint8_t *configuration_valid_1 = get_descriptor_data_by_name("NVM_CONFIGURATION_REALLYVALID_far");
    uint8_t *routecache_valid = get_descriptor_data_by_name("EX_NVM_ROUTECACHE_MAGIC_far");


    is_valid = (
        (*eeoffset_magic == MAGIC_VALUE) && 
        (*configuration_valid_0 == CONFIGURATION_VALID_0) &&
        (*configuration_valid_1 == CONFIGURATION_VALID_1) &&
        (*routecache_valid ==ROUTECACHE_VALID)
    );

    if(!is_valid) {
        user_message( MSG_ERROR, "NVM image is not valid\n");
    }
    return is_valid;
}

bool nvmlib6xx_nvm_to_json(const uint8_t *nvm_image, size_t nvm_image_size, json_object **jo_out)
{
    char zw_version[32];
    char app_version[32];
    snprintf(zw_version,sizeof(zw_version),  "%02d.%02d.00",(_ZW_VERSION_ >> 8) &0xff,(_ZW_VERSION_ >> 0) & 0xff );
    snprintf(app_version,sizeof(app_version),"%02d.%02d.00",(_APP_VERSION_ >> 8) &0xff,(_APP_VERSION_ >> 0) & 0xff );

    json_object* nvm_jso = json_object_new_object();
    time_t current_time = time(NULL);

    if(nvm_image_size > sizeof(nvm_buffer)) {
        user_message( MSG_ERROR, "NVM image exceeds buffer size\n");
        return false;
    }
    memcpy(nvm_buffer,nvm_image,nvm_image_size);


    json_object* nmv_backup_info_o = json_object_new_object();
    json_object_object_add(nvm_jso, "backupInfo",nmv_backup_info_o);
    json_object_object_add( nmv_backup_info_o ,"backupFormatVersion", json_object_new_int(1) );
    json_object_object_add( nmv_backup_info_o ,"sourceProtocolVersion", json_object_new_string(  zw_version ) ); 
    json_object_object_add( nmv_backup_info_o ,"sourceAppVersion", json_object_new_string(  app_version ) ); 

    json_object_object_add( nmv_backup_info_o ,"date", json_object_new_string( ctime( &current_time) ) );

    json_object* nmv_zwcontroller_o = json_object_new_object();
    json_object_object_add(nvm_jso, "zwController",nmv_zwcontroller_o);
    add_nvm_object_simple(nmv_zwcontroller_o,"nodeId","NVM_NODEID_far");
    add_nvm_object_homeid(nmv_zwcontroller_o,"ownHomeId","EX_NVM_HOME_ID_far");
    add_nvm_object_homeid(nmv_zwcontroller_o,"learnedHomeId","NVM_HOMEID_far");
    add_nvm_object_simple(nmv_zwcontroller_o,"lastUsedNodeId","EX_NVM_LAST_USED_NODE_ID_START_far");
    add_nvm_object_simple(nmv_zwcontroller_o,"staticControllerNodeId","EX_NVM_STATIC_CONTROLLER_NODE_ID_START_far");
    add_nvm_object_simple(nmv_zwcontroller_o,"controllerConfiguration","EX_NVM_CONTROLLER_CONFIGURATION_far");
    add_nvm_object_simple(nmv_zwcontroller_o,"systemState","NVM_SYSTEM_STATE");
    add_nvm_object_cmdclass_list(nmv_zwcontroller_o,"cmdClassList");

    add_nvm_object_node_table(nmv_zwcontroller_o,"nodeTable");
    add_nvm_object_node_suc_state(nmv_zwcontroller_o,"sucState");
    add_nvm_object_base64(nmv_zwcontroller_o,"applicationData","EEOFFSET_HOST_OFFSET_START_far", true);

    json_object* app_info_500 = json_object_new_object();
    json_object_object_add(nvm_jso, "appConfig",app_info_500);

    add_nvm_object_simple(app_info_500,"watchdogStarted","EEOFFSET_WATCHDOG_STARTED_far");
    add_nvm_object_simple(app_info_500,"powerLevelNormal","EEOFFSET_POWERLEVEL_NORMAL_far");
    add_nvm_object_simple(app_info_500,"powerLevelLow","EEOFFSET_POWERLEVEL_LOW_far");
    add_nvm_object_simple(app_info_500,"powerMode","EEOFFSET_MODULE_POWER_MODE_far");
    add_nvm_object_simple(app_info_500,"powerModeExtintEnable","EEOFFSET_MODULE_POWER_MODE_EXTINT_ENABLE_far");
    add_nvm_object_simple(app_info_500,"powerModeWutTimeout","EEOFFSET_MODULE_POWER_MODE_WUT_TIMEOUT_far");

    *jo_out = nvm_jso;

    return true;
}
