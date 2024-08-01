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
 * @file
 * The node_provisioning_list module handles the management of the SmartStart provisioning list at the Z/IP GW.
 * It provides supports for listing, updating and deleting elements in the Z/IP GW's provisioning list
 */

#include "ZW_classcmd.h"
#include "unique_seqno.h"
#include "tokenizer.h"
#include "node_provisioning_list.h"
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "libzw_log.h"


/* Private functions prototypes */
// print functions
static void print_provisioning_list();
// Memory allocation
static void init_provisioning_list_entry(s_provisioning_list_entry_t* pEntry, uint8_t dsk_length);
static void init_provisioning_list(s_provisioning_list_t* pList, uint8_t entries);
static void reset_provisioning_list(s_provisioning_list_t* pList);

/* Private variables */
static s_provisioning_list_t provisioning_list;


// Initialize an entry in the provisioning list with a given DSK length.
static void init_provisioning_list_entry(s_provisioning_list_entry_t* pEntry, uint8_t dsk_length)
{
  if(NULL != pEntry)
  {
    memset(pEntry, 0, sizeof(s_provisioning_list_entry_t));
    pEntry->dsk_len = dsk_length;
    pEntry->bootstrapping_mode = 1;
  }
  else
  {
    LOG_ERROR("Passed a NULL Provision List entry\n");
  }
}

// Initialize our local copy of the GW's provisioning list.
static void init_provisioning_list(s_provisioning_list_t* pList, uint8_t entries)
{
  if(NULL != pList)
  {
    //Avoid to do double malloc, we reset what was there in any case:
    reset_provisioning_list(pList);
    pList->number_of_entries = entries;
    pList->entry = malloc(entries*sizeof(s_provisioning_list_entry_t));
  }
  else
  {
    LOG_ERROR("Passed a NULL Provision List\n");
  }
}

// Reset our local copy of the GW's provisioning list by initializing all entries.
static void reset_provisioning_list(s_provisioning_list_t* pList)
{
  if(NULL != pList)
  {
    // Reset the list, if there was at least 1 entry
    if (0 < pList->number_of_entries)
    {
      pList->number_of_entries = 0;
      free(pList->entry);
      pList->entry = NULL;
    }
  }
  else
  {
    LOG_ERROR("Passed a NULL Provision List\n");
  }
}

/**
 *  Parse an incoming Node Provisioning Command Class command
 *  \param packet Pointer to the payload of the ZIP Packet. First
 *  byte must contain command class, followed by the command etc.
 *  \param len Length of the packet
 */
void parse_node_provisioning_list_packet(const uint8_t *packet, uint16_t len, struct zconnection *zc)
{
  if (len < 19 && packet[0] != COMMAND_CLASS_NODE_PROVISIONING)
  {
    LOG_ERROR("Received NODE_PROVISIONING_LIST_ITERATION_REPORT is discarded since length is short.\n ");
    return;
  }

  switch (packet[1])
  {
    uint16_t idx;

    case NODE_PROVISIONING_LIST_ITERATION_REPORT:
    idx = 3; //we start at index 3, looking at the remaining count.
    // Use the remaining count as an index in our provisioning list
    uint8_t current_entry = packet[idx++];
    uint8_t current_dsk_length = packet[idx++];

    if(PROVISIONING_LIST_DSK_MAX_SIZE == current_dsk_length) // Check the dsk length here, we accept only PROVISIONING_LIST_DSK_MAX_SIZE
    {
      // First time the count will be the highest number. Init anyway if the current entry seems higher than what we can handle
      if(provisioning_list.number_of_entries < current_entry+1)
      {
        init_provisioning_list(&provisioning_list, current_entry+1);
      }
      // A dsk is present, add it into the provisioning list:
      init_provisioning_list_entry(&(provisioning_list.entry[current_entry]), current_dsk_length);
      memcpy(provisioning_list.entry[current_entry].dsk, &(packet[idx]), current_dsk_length);
      
      idx+=(current_dsk_length);
    
      //Parse the TLVs that we know of
      while(idx+2 <= len) // We always increment idx by 2 as a minimum for each TLV
      {
        uint8_t tlv_length=0;

        //Look at the type fieldand increment the index to point at the length field:
        switch((packet[idx++] & 0xFE)>>1)
        {
          case E_PL_TLV_TYPE_NETWORK_STATUS:
            tlv_length = packet[idx++];
            if((2 <= tlv_length) && (idx+tlv_length-1 <= len))
            {
              provisioning_list.entry[current_entry].node_id = packet[idx];
              provisioning_list.entry[current_entry].network_status = packet[idx+1];
              if (4 <= tlv_length) {
                if (packet[idx] == 0) {
                  provisioning_list.entry[current_entry].node_id = packet[idx+2] << 8;
                  provisioning_list.entry[current_entry].node_id |= packet[idx+3];
                }
              }
            }
            else
            {
              LOG_ERROR("Network Status TLV length is less than 2. Skipping\n");
            }
            idx+=tlv_length;
          break;

          case E_PL_TLV_TYPE_BOOTSTRAPPING_MODE:
            tlv_length = packet[idx++];
            if((1 <= tlv_length) && (idx+tlv_length-1 <= len))
            {
              provisioning_list.entry[current_entry].bootstrapping_mode = packet[idx];
            }
            else
            {
              LOG_ERROR("Bootstrapping mode TLV length is 0. Skipping\n");
            }
            idx+=tlv_length;
          break;

          case E_PL_TLV_TYPE_SMARTSTART_INCLUSION_SETTING:
            tlv_length = packet[idx++];
            if((1 <= tlv_length) && (idx+tlv_length-1 <= len))
            {
              provisioning_list.entry[current_entry].inclusion_setting = packet[idx];
            }
            else
            {
              LOG_ERROR("Inclusion setting TLV length is 0. Skipping\n");
            }
            idx+=tlv_length;
          break;

          case E_PL_TLV_TYPE_NAME:
            tlv_length = packet[idx++];
            if((PROVISIONING_LIST_NAME_MAX_SIZE >= tlv_length) && (idx+tlv_length-1 <= len))
            {
              strncpy(provisioning_list.entry[current_entry].name, (char*)(&packet[idx]), tlv_length);
              provisioning_list.entry[current_entry].name[tlv_length] = '\0';
            }
            else
            {
              LOG_ERROR("Name TLV length is too large. Skipping\n");
            }
            idx+=tlv_length;
          break;

          case E_PL_TLV_TYPE_LOCATION:
            tlv_length = packet[idx++];
            if((PROVISIONING_LIST_LOCATION_MAX_SIZE >= tlv_length) && (idx+tlv_length-1 <= len))
            {
              strncpy(provisioning_list.entry[current_entry].location, (char*)(&packet[idx]), tlv_length);
              provisioning_list.entry[current_entry].location[tlv_length] = '\0';
            }
            else
            {
              LOG_ERROR("Location TLV length is too large. Skipping\n");
            }
            idx+=tlv_length;
          break;

          default: // Unknown TLV, move forward to the next TLV block.
            tlv_length = packet[idx++];
            //printf("\t Found unknonwn TLV (type: %02x, length: %d)\n",((packet[idx-1] & 0xFE)>>1),tlv_length);
            idx+=tlv_length;
          break;
        }
      }
    } // if(current_dsk_length > 0)

    // After parsing, reply if there are more nodes to discover in the list
    if(0 < current_entry)
    {
      idx = 0;
      uint8_t buf[200];

      buf[idx++] = COMMAND_CLASS_NODE_PROVISIONING;
      buf[idx++] = NODE_PROVISIONING_LIST_ITERATION_GET;
      buf[idx++] = get_unique_seq_no();
      buf[idx++] = current_entry; // Remaining counter: used to keep track of the provisioning list size.

      if(0 == zconnection_send_async(zc, buf, idx, 0))
      {
        LOG_ERROR("The transmition to the Z/IP GW could not be initiated. Please retry later.\n");
      }
    }
    else //We know of the whole provisioning list, so it can be printed
    {
      print_provisioning_list();
    }
    break; //case NODE_PROVISIONING_LIST_ITERATION_REPORT:

    default:
      LOG_WARN("Ignored received frame since it is unhandled NODE_PROVISIONING_LIST command.\n ");
      break;
  }
}

e_cmd_return_code_t cmd_pl_list(struct zconnection *zc)
{
  // Just request fresh info to the GW
  int idx = 0;
  uint8_t buf[4];

  buf[idx++] = COMMAND_CLASS_NODE_PROVISIONING;
  buf[idx++] = NODE_PROVISIONING_LIST_ITERATION_GET;
  buf[idx++] = get_unique_seq_no();
  buf[idx++] = 0xFF; // Remaining counter: used to keep track of the provisioning list size, start an iteration with 0xFF.

  if(0 == zconnection_send_async(zc, buf, idx, 0))
  {
    return E_CMD_RETURN_CODE_TRANSMIT_FAIL;
  }
  else
  {
    return E_CMD_RETURN_CODE_SUCCESS;
  }
}

e_cmd_return_code_t cmd_pl_add(struct zconnection *zc, char** tokens)
{
  uint16_t input_digits;
  char* input_fragment;
  s_provisioning_list_entry_t my_entry;

  if (1 < token_count(tokens))
  {
    init_provisioning_list_entry(&my_entry, 0); //We do not know yet the length of the DSK
    // First parameter is the DSK string. (12345-12345-12345-12345-12345-12345-12345-12345)
    input_fragment = strtok(tokens[1], "-");
    while((NULL != input_fragment) && (PROVISIONING_LIST_DSK_MAX_SIZE >= my_entry.dsk_len+2))
    {
      input_digits = strtoul(input_fragment, NULL, 10);
      my_entry.dsk[my_entry.dsk_len] = (uint8_t)(input_digits>>8);
      my_entry.dsk[my_entry.dsk_len+1] = (uint8_t)(input_digits & 0x00FF);
      my_entry.dsk_len += 2;

      if (0xFFFF < input_digits) //Verify that the input digit is not too big.
      {
        return E_CMD_RETURN_CODE_PARSE_ERROR;
      }
      input_fragment = strtok(NULL, "-");
    }
    if (PROVISIONING_LIST_DSK_MAX_SIZE != my_entry.dsk_len)
    {
      return E_CMD_RETURN_CODE_PARSE_ERROR;
    }

    // following parameters are supposed to be TLVs, name:myName location:myLocation
    uint8_t current_token = 2;
    bool name_defined = false, location_defined = false, bootmode_defined = false, smartstart_defined = false;
    while (current_token < token_count(tokens))
    {
      input_fragment = strtok(tokens[current_token], ":");
      if (NULL == input_fragment)
      {
        return E_CMD_RETURN_CODE_PARSE_ERROR;
      }
      else
      {
        if (!strcmp(input_fragment, "name"))
        {
          const char* new_name = strtok(NULL, ":");
          if (NULL != new_name)
          {
            strcpy(my_entry.name, new_name);
          }
          name_defined = true;
        }
        else if (!strcmp(input_fragment, "location"))
        {
          const char* new_location = strtok(NULL, ":");
          if (NULL != new_location)
          {
            strcpy(my_entry.location, new_location);
          }
          location_defined = true;
        }
        else if (!strcmp(input_fragment, "bootmode"))
        {
          const char* new_bootmode_string = strtok(NULL, ":");
          if (NULL != new_bootmode_string)
          {
            my_entry.bootstrapping_mode = strtol(new_bootmode_string, NULL, 16);
          }
          bootmode_defined = true;
        }
        else if (!strcmp(input_fragment, "smartstart"))
        {
          const char* new_inclusion_setting_string = strtok(NULL, ":");
          if (NULL != new_inclusion_setting_string)
          {
            my_entry.inclusion_setting = strtol(new_inclusion_setting_string, NULL, 16);
          }
          smartstart_defined = true;
        }
        else //Something unknown was passed, we bail out
        {
          return E_CMD_RETURN_CODE_PARSE_ERROR;
        }
        current_token++;
      }
    }
    
    int idx = 0;
    uint8_t buf[200];

    buf[idx++] = COMMAND_CLASS_NODE_PROVISIONING;
    buf[idx++] = NODE_PROVISIONING_SET;
    buf[idx++] = get_unique_seq_no();
    buf[idx++] = my_entry.dsk_len; // DSK Length
    for (int i = 0; i<my_entry.dsk_len ; i++)
    {
      buf[idx++] = my_entry.dsk[i]; // DSK
    }
    if(true == bootmode_defined) // Add the bootstrapping mode TLV
    { 
      buf[idx++] = ((E_PL_TLV_TYPE_BOOTSTRAPPING_MODE<<1) | 1);
      buf[idx++] = 1;
      buf[idx++] = my_entry.bootstrapping_mode;
    }
    if(true == smartstart_defined) // Add the smartstart inclusion setting TLV
    {
      buf[idx++] = ((E_PL_TLV_TYPE_SMARTSTART_INCLUSION_SETTING<<1) | 1);
      buf[idx++] = 1;
      buf[idx++] = my_entry.inclusion_setting;
    }
    // Here verify that name length do not exceed 62 char. Name and location combined must not exceed 62 characters.
    if(true == name_defined && (PROVISIONING_LIST_NAME_MAX_SIZE >= strlen(my_entry.name)) ) // Add the name TLV
    {
      buf[idx++] = ((E_PL_TLV_TYPE_NAME<<1) | 0);
      buf[idx++] = strlen(my_entry.name);
      for (int i = 0; i< strlen(my_entry.name); i++)
      {
        buf[idx++] = my_entry.name[i];
      }
    }
    // Here verify that combined name and location length do not exceed 62 char. If that's the case, only the name will be transmitted
    if(true == location_defined && (PROVISIONING_LIST_LOCATION_MAX_SIZE >= (strlen(my_entry.name) + strlen(my_entry.location))) ) // Add the location TLV
    {
      buf[idx++] = ((E_PL_TLV_TYPE_LOCATION<<1) | 0);
      buf[idx++] = strlen(my_entry.location);
      for (int i = 0; i< strlen(my_entry.location) ; i++)
      {
        buf[idx++] = my_entry.location[i];
      }
    }
    // Send the payload out
    if(0 == zconnection_send_async(zc, buf, idx, 0))
    {
      return E_CMD_RETURN_CODE_TRANSMIT_FAIL;
    }
    return E_CMD_RETURN_CODE_SUCCESS;
  }
  else
  {
    return E_CMD_RETURN_CODE_PARSE_ERROR;
  }
}


e_cmd_return_code_t cmd_pl_remove(struct zconnection *zc, char** tokens)
{
  uint16_t input_digits;
  char* dsk_fragments;
  s_provisioning_list_entry_t my_entry;

  if (token_count(tokens) > 1)
  {
    init_provisioning_list_entry(&my_entry, 0); //We do not know yet the length of the DSK
    dsk_fragments = strtok(tokens[1], "-");
    while((NULL != dsk_fragments) && (PROVISIONING_LIST_DSK_MAX_SIZE >= my_entry.dsk_len+2))
    {
      input_digits = strtol(dsk_fragments, NULL, 10);
      my_entry.dsk[my_entry.dsk_len] = (uint8_t)(input_digits>>8);
      my_entry.dsk[my_entry.dsk_len+1] = (uint8_t)(input_digits & 0x00FF);
      my_entry.dsk_len += 2;
      
      dsk_fragments = strtok(NULL, "-");
    }
    if (PROVISIONING_LIST_DSK_MAX_SIZE != my_entry.dsk_len )
    {
      return E_CMD_RETURN_CODE_PARSE_ERROR;
    }

    // Send the entry to the GW.
    int idx = 0;
    uint8_t buf[200];

    buf[idx++] = COMMAND_CLASS_NODE_PROVISIONING;
    buf[idx++] = NODE_PROVISIONING_DELETE;
    buf[idx++] = get_unique_seq_no();
    buf[idx++] = my_entry.dsk_len; // DSK Length
    for (int i = 0; i<my_entry.dsk_len ; i++)
    {
      buf[idx++] = my_entry.dsk[i]; // DSK
    }

    if(0 == zconnection_send_async(zc, buf, idx, 0))
    {
      return E_CMD_RETURN_CODE_TRANSMIT_FAIL;
    }
    return E_CMD_RETURN_CODE_SUCCESS;
  }
  else
  {
    return E_CMD_RETURN_CODE_PARSE_ERROR;
  }
}

e_cmd_return_code_t cmd_pl_reset(struct zconnection *zc)
{
  int idx = 0;
  uint8_t buf[4];

  buf[idx++] = COMMAND_CLASS_NODE_PROVISIONING;
  buf[idx++] = NODE_PROVISIONING_DELETE;
  buf[idx++] = get_unique_seq_no();
  buf[idx++] = 0; // DSK Length (delete all entries)

  if(0 == zconnection_send_async(zc, buf, idx, 0))
  {
    return E_CMD_RETURN_CODE_TRANSMIT_FAIL;
  }
   return E_CMD_RETURN_CODE_SUCCESS;
}


static void print_provisioning_list()
{
  UI_MSG("\n");
  if (provisioning_list.number_of_entries == 0)
  {
    UI_MSG("The SmartStart Node provisioning list is empty\n");
  }
  else
  {
    UI_MSG("---------------------------------------------------\n");
    UI_MSG("SmartStart Node provisioning list:\n");
    //printf("| DSK  | Network Status | Name | Location  |\n ");
    for (int i = 0; i<provisioning_list.number_of_entries; i++)
    {
      UI_MSG("---------------------------------------------------\n");
      // Print the DSK
      UI_MSG("\tDSK: ");
      for (int j=0; j<provisioning_list.entry[i].dsk_len ; j+=2)
      {
        UI_MSG("%05d", (provisioning_list.entry[i].dsk[j] << 8) + provisioning_list.entry[i].dsk[j+1]);
        if (j+2 < provisioning_list.entry[i].dsk_len)
        {
          UI_MSG("-");
        }
      }
      UI_MSG("\n");

      //  Bootstrapping mode
      UI_MSG("\tBootstrapping mode: ");
      switch(provisioning_list.entry[i].bootstrapping_mode)
      {
        case E_PL_BOOTSTRAPPINGMODE_SECURITY2:
        UI_MSG("Classic inclusion with S2\n");
        break;
        
        case E_PL_BOOTSTRAPPINGMODE_SMARTSTART:
        UI_MSG("SmartStart inclusion\n");
        break;

        case E_PL_BOOTSTRAPPINGMODE_LONG_RANGE:
        UI_MSG("Long Range SmartStart inclusion\n");
        break;

        default:
        UI_MSG("Unknown bootstrapping mode (%d)\n",provisioning_list.entry[i].bootstrapping_mode);
        break;
      }

      //  Inclusion setting
      UI_MSG("\tSmartStart inclusion setting: ");
      switch(provisioning_list.entry[i].inclusion_setting)
      {
        case E_PL_INCLUSIONSETTING_PENDING:
        UI_MSG("Pending (will be included when issuing inclusion requests)\n");
        break;
        
        case E_PL_INCLUSIONSETTING_PASSIVE:
        UI_MSG("Passive: this should not be returned by the Z/IP Gateway!\n");
        break;

        case E_PL_INCLUSIONSETTING_IGNORED:
        UI_MSG("Ignored (will not be included again)\n");
        break;

        default:
        UI_MSG("Unknown setting (%d)\n",provisioning_list.entry[i].inclusion_setting);
        break;
      }

      // Print the network status
      UI_MSG("\tNetwork status: ");
      switch(provisioning_list.entry[i].network_status)
      {
        case E_PL_NETWORKSTATUS_NOT_IN_NETWORK:
        UI_MSG("Not included\n");
        break;
        
        case E_PL_NETWORKSTATUS_INCLUDED:
        UI_MSG("Included (NodeID %d)\n",provisioning_list.entry[i].node_id);
        break;

        case E_PL_NETWORKSTATUS_FAILING:
        UI_MSG("Included, but failing (NodeID %d)\n",provisioning_list.entry[i].node_id);
        break;

        default:
        UI_MSG("Unknown network status\n");
        break;
      }

      // Print the name:
      UI_MSG("\tNode name: %s \n",provisioning_list.entry[i].name);
      
      // Print the location:
      UI_MSG("\tNode location: %s \n",provisioning_list.entry[i].location);
    }
    UI_MSG("\n");
  }

  // Free up our local copy of the provisioning list, won't need it anymore
  reset_provisioning_list(&provisioning_list);
}
