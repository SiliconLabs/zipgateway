/*
 * Copyright 2020 Silicon Laboratories Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @defgroup smartstart SmartStart provisioning list management
 * @ingroup zwaveip
 *
 * The node_provisioning_list module handles the management of the SmartStart provisioning list at the Z/IP GW.
 * It provides supports for listing, updating and deleting elements in the Z/IP GW's provisioning list
 *
 * @{
 */

#ifndef _NODE_PROVISIONING_LIST_H_
#define _NODE_PROVISIONING_LIST_H_

#include "zconnection.h"
#include "command_return_codes.h"

/** Maximum size of the provisioning list */
#define PROVISIONING_LIST_MAX_ENTRIES 232

/** Max size of TLVs in the provisioning list  */
#define PROVISIONING_LIST_DSK_MAX_SIZE 16
#define PROVISIONING_LIST_NAME_MAX_SIZE 62
#define PROVISIONING_LIST_LOCATION_MAX_SIZE 62

/**
 * @brief Z-Wave Network status for an item in the provisioning list.
 */
typedef enum
{
   E_PL_NETWORKSTATUS_NOT_IN_NETWORK  = 0x00,   /**< Not included */
   E_PL_NETWORKSTATUS_INCLUDED        = 0x01,   /**< Included and functioning */
   E_PL_NETWORKSTATUS_FAILING         = 0x02    /**< Included but not responding anymore */
} e_pl_networkstatus_t;

/**
 * @brief Bootstrapping mode for an item in the provisioning list,
 * which is used by the Z/IP Gateway to decide how the node will be included
 * in the network.
 */
typedef enum
{
   E_PL_BOOTSTRAPPINGMODE_SECURITY2   = 0x00,   /**< Use regular inclusion with S2 bootstrapping */
   E_PL_BOOTSTRAPPINGMODE_SMARTSTART  = 0x01,    /**< Use SmartStart inclusion */
   E_PL_BOOTSTRAPPINGMODE_LONG_RANGE = 0x02    /**< Use Long Range SmartStart inclusion */
} e_pl_bootstrappingmode_t;

/**
 * @brief Inclusion setting for an item in the provisioning list.
 * The Z/IP Gateway will use this to value if a node needs to be
 * added to the list but not included immediately in the network.
 * This setting has an effect only if the bootstrapping mode is set to E_PL_BOOTSTRAPPINGMODE_SMARTSTART
 */
typedef enum
{
   E_PL_INCLUSIONSETTING_PENDING  = 0x00,    /**< Listens to inclusion requests and will include the node when it issues one */
   E_PL_INCLUSIONSETTING_PASSIVE  = 0x01,    /**< Temporarily stop listening to inclusion requests, until the provisioning list is updated/read again */
   E_PL_INCLUSIONSETTING_IGNORED  = 0x02     /**< Do not include the entry until this value is changed */
} e_pl_inclusionsetting_t;

/**
 * @brief Represent data kept for each Provisioning list entry
 */
typedef struct provisioning_list_entry
{
    uint8_t dsk[PROVISIONING_LIST_DSK_MAX_SIZE];
    uint8_t dsk_len;
    // Here we add the TLVs that we do not ignore.
    e_pl_networkstatus_t network_status;                    /**< Current network status */
    e_pl_bootstrappingmode_t bootstrapping_mode;            /**< if it is to be included with SmartStart or classic S2 */
    e_pl_inclusionsetting_t inclusion_setting;              /**< if it is to be included or ignored */
    uint16_t node_id;                                        /**< Current Z-Wave NodeID, if included */
    char name[PROVISIONING_LIST_NAME_MAX_SIZE+1];           /**< node name, will be used at inclusion */
    char location[PROVISIONING_LIST_LOCATION_MAX_SIZE+1];   /**< Node location */
} s_provisioning_list_entry_t;


/**
 * @brief Provisioning list, containing a number of provisioning list entries
 */
typedef struct provisioning_list
{
    uint8_t number_of_entries;
    s_provisioning_list_entry_t *entry;
} s_provisioning_list_t;


/**
 * @brief Valid types for TLVs in the node provisioning list entries
 */
typedef enum
{
   E_PL_TLV_TYPE_PRODUCT_TYPE                   = 0x00,  /**< Product type. This is ignored by the client */
   E_PL_TLV_TYPE_PRODUCT_ID                     = 0x01,  /**< Product ID. This is ignored by the client */
   E_PL_TLV_TYPE_MAX_INCLUSION_REQUEST_INTERVAL = 0x02,  /**< Max Inclusion request interval. This is ignored by the client */
   E_PL_TLV_TYPE_UUID16                         = 0x03,  /**< UUID for the node. This is ignored by the client */
   E_PL_TLV_TYPE_NAME                           = 0x32,  /**< Name to assign to the node when including it */
   E_PL_TLV_TYPE_LOCATION                       = 0x33,  /**< Location to assign to the node when including it */
   E_PL_TLV_TYPE_SMARTSTART_INCLUSION_SETTING   = 0x34,  /**< SmartStart inclusion setting */
   E_PL_TLV_TYPE_ADVANCED_JOINING               = 0x35,  /**< Advanced joining (which keys to grant). This is ignored by the client */
   E_PL_TLV_TYPE_BOOTSTRAPPING_MODE             = 0x36,  /**< Bootstrapping mode for the node */
   E_PL_TLV_TYPE_NETWORK_STATUS                 = 0x37   /**< Z-Wave Network Status for the entry */
} e_pl_tlv_type_t;


/**
 * @brief Handle incoming commands from the Node Provisioning List Command Class.
 *
 * It currently only handles NODE_PROVISIONING_LIST_ITERATION_REPORT
 * because it is the only one that can be requested by the client.
 *
 * @param packet Node Provisioning List packet
 * @param len Length of the packet
 * @param zc A handle to the connection object
 */
void parse_node_provisioning_list_packet(const uint8_t *packet, uint16_t len, struct zconnection *zc);

/**
 * @brief Display the provisioning list at the Z/IP Gateway.
 *
 * This method issues a NODE_PROVISIONING_LIST_ITERATION_GET command and waits for the Z/IP GW to reply with
 * NODE_PROVISIONING_LIST_ITERATION_REPORT that will be parsed by parse_node_provisioning_list_packet(). When
 * all node provisioning entries have been transferred, the list will be printed out.
 *
 * @param zc A handle to the connection object
 * @return e_cmd_return_code_t Command status codes indicating the commands execution outcome
 */
e_cmd_return_code_t cmd_pl_list(struct zconnection *zc);

/**
 * @brief Add/update an entry to the Z/IP Gateway Node Provisioning List.
 *
 * It parses the tokens given to the "pl_add" command and pushes the provisioning list data
 * (add/update entry) to the Z/IP Gateway using a NODE_PROVISIONING_LIST_SET Command.
 * If the DSK was not present in the list, a new entry will be added.
 * If the DSK was present in the list, the specified TLVs will be modified.

 * @param zc A handle to the connection object
 * @param tokens Tokens given to the "pl_add" command
 * @return e_cmd_return_code_t Command status codes indicating the commands execution outcome
 */
e_cmd_return_code_t cmd_pl_add(struct zconnection *zc, char** tokens);

/**
 * @brief Remove an entry from the Z/IP Gateway Node Provisioning List.
 *
 * It parses the tokens given to the "pl_remove" command and issues a
 * NODE_PROVISIONING_LIST_DELETE Command for the requested DSK to the Z/IP Gateway.
 *
 * @param zc A handle to the connection object
 * @param tokens Tokens given to the "pl_add" command
 * @return e_cmd_return_code_t Command status codes indicating the commands execution outcome
 */
e_cmd_return_code_t cmd_pl_remove(struct zconnection *zc, char** tokens);

/**
 * @brief Erase all entries from the Z/IP Gateways node provisioning list.
 *
 * A NODE_PROVISIONING_LIST_DELETE Command with a 0 length DSK field is sent to the Z/IP Gateway.
 *
 * @param zc A handle to the connection object
 * @return e_cmd_return_code_t Command status codes indicating the commands execution outcome
 */
e_cmd_return_code_t cmd_pl_reset(struct zconnection *zc);

#endif /* _NODE_PROVISIONING_LIST_H_ */

/**
 * @}
 */

