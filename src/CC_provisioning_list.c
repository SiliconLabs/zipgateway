/* © 2017 Silicon Laboratories Inc.
 */

/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/
#include <ZW_classcmd_ex.h>
#include <command_handler.h>
#include <stdint.h>
#include <provisioning_list.h>
#include <CC_provisioning_list.h>
#include <ZIP_Router_logging.h>
#include <ResourceDirectory.h>
#include <assert.h>
#include <zgw_str.h>
//#include <CC_NetworkManagement.h> // TODO: Include this instead of external decl below

void NetworkManagement_smart_start_init_if_pending();

/** Fill TLVs from a provisioning list entry into a buffer.
 *
 * Helper function shared between provisioning list report and provisioning list iter report.
 *
 * @param pt Pointer to a buffer where to write the tlvs.
 * @param entry The provisioning list entry to copy the tlvs from.
 * @return The updated pointer.
 */
static uint8_t* CC_provisioning_list_add_tlvs(uint8_t* pt, provisioning_list_t *entry);

/****************************************************************************/
/*                      PRIVATE TYPES and DEFINITIONS                       */
/****************************************************************************/
#define TLV_NOT_PRESENT 0xFFFF

/****************************************************************************/
/*                              EXPORTED DATA                               */
/****************************************************************************/

/****************************************************************************/
/*                              PRIVATE DATA                                */
/****************************************************************************/
static uint8_t buf[2000];

/****************************************************************************/
/*                              PRIVATE FUNCTIONS                           */
/****************************************************************************/

/**
 *  Temporary function to workaround persistent storage of provision list
 *  used to have trouble with commas. That should be solved now, and
 *  this function (along with the calling code) should be removed.
 */
static int check_comma(const uint8_t *str, int len)
{
  int i;

  for (i = 0; i < len; i++)
  {
    if (str[i] == ',')
      return 1;
  }

  return 0;
}

/****************************************************************************/
/*                              EXPORTED FUNCTIONS                          */
/****************************************************************************/

void CC_provisioning_list_init(void)
{

}

static uint8_t* CC_provisioning_list_add_tlvs(uint8_t* pt, provisioning_list_t *entry) {
   struct pvs_tlv  *tlv;
   rd_node_database_entry_t *rd_dbe;

   rd_dbe = rd_lookup_by_dsk(entry->dsk_len, entry->dsk);

   /* Return the Bootstrapping Mode (and the critical flag) as TLV */
   *pt++ = (PVS_TLV_TYPE_BOOTSTRAP_MODE << 1) | 1;
   *pt++ = 1;
   *pt++ = entry->bootmode;

   /* Return the SmartStart Inclusion Status Information (and the critical flag) as TLV */
   *pt++ = (PVS_TLV_TYPE_STATUS << 1) | 1;
   *pt++ = 1;
   *pt++ = entry->status;

   /* Return the Network Status Information as TLV */
   *pt++ = PVS_TLV_TYPE_NETWORK_INFO << 1;
   *pt++ = 4;
   if (rd_dbe == NULL) {
      *pt++ = 0; //Assigned NodeID
      *pt++ = PVS_NETSTATUS_NOT_IN_NETWORK; //Network status
      *pt++ = 0; //Assigned Z-Wave Long Range NodeID (MSB)
      *pt++ = 0; //Assigned Z-Wave Long Range NodeID (LSB)
   } else {
       if (is_lr_node(rd_dbe->nodeid)) { //Assigned NodeID
         *pt++ = 0;
       } else {
         *pt++ = rd_dbe->nodeid;
       }
      /* TODO: Should PROBE_FAIL also count as failing? */
      *pt++ = (rd_dbe->state == STATUS_FAILING) ? PVS_NETSTATUS_FAILING : PVS_NETSTATUS_INCLUDED;
       if (is_lr_node(rd_dbe->nodeid)) {
         *pt++ = rd_dbe->nodeid >> 8; //Assigned Z-Wave Long Range NodeID (MSB)
         *pt++ = rd_dbe->nodeid & 0xFF; //Assigned Z-Wave Long Range NodeID (LSB)
       } else {
         *pt++ = 0; //Assigned Z-Wave Long Range NodeID (MSB)
         *pt++ = 0; //Assigned Z-Wave Long Range NodeID (LSB)
       }
   }

   tlv = entry->tlv_list;
   while (tlv != NULL) {
      *pt++ = (tlv->type << 1) | provisioning_list_tlv_crt_flag(tlv->type);
      *pt++ = tlv->length;
      memcpy(pt, tlv->value, tlv->length);
      pt += tlv->length;
      tlv = tlv->next;
   }

   return pt;
}

int CC_provisioning_list_build_report(uint8_t* __buf,provisioning_list_t *entry,int8_t seqNo) {

   ZW_PROVISION_REPORT_FRAME_EX* r = (ZW_PROVISION_REPORT_FRAME_EX*)__buf;

   uint8_t *pt; // pointer to end of variable length report we are creating

   r->cmdClass = COMMAND_CLASS_PROVISIONING_LIST;
   r->cmd = COMMAND_PROVISION_REPORT;
   r->seqNo = seqNo;

   if(NULL == entry) {
     r->reserved_dsk_length = 0;

     return 4;
   }

   r->reserved_dsk_length = entry->dsk_len & PROVISIONING_LIST_DSK_LENGTH_MASK;
   memcpy(r->dsk, entry->dsk, entry->dsk_len);
   pt = (uint8_t*)&r->dsk + entry->dsk_len;

   pt = CC_provisioning_list_add_tlvs(pt, entry);

   return (int)(pt-__buf);
 }

command_handler_codes_t
PVL_CommandHandler(zwave_connection_t *conn,uint8_t* pData, uint16_t bDatalen)
{
  command_handler_codes_t return_value = COMMAND_HANDLED;
  if(bDatalen < 2)
  {
    return_value = COMMAND_PARSE_ERROR;
    goto cmd_handled;
  }

  switch(pData[1])
  {
    // TODO: Sequence no. management
    case COMMAND_PROVISION_SET:
    {
      ZW_PROVISION_SET_FRAME_EX *pFrame = (ZW_PROVISION_SET_FRAME_EX*)&pData[0];
      provisioning_list_t *pvl_entry;
      uint8_t dsklen = pFrame->reserved_dsk_length & PROVISIONING_LIST_DSK_LENGTH_MASK;
      uint8_t* name=0;
      uint8_t  name_len=0;
      uint8_t* location=0;
      uint8_t  location_len=0;
      uint8_t* product_id = 0;
      uint8_t  product_id_len = 0;
      uint8_t* product_type = 0;
      uint8_t  product_type_len = 0;
      uint16_t max_incl_req_interval = TLV_NOT_PRESENT;
      uint8_t* uuid16 = 0;
      provisioning_bootmode_t bootstrap_mode = TLV_NOT_PRESENT; // TODO: Name this bootstrap mode of provisioning class everywhere
      uint8_t* tlv;
      uint8_t* end = (uint8_t*)pData + bDatalen;
      int mandatory_tlv_count = 0; /*Count of mandatory TLVs encounted while parsing */
      pvs_status_t prov_status = TLV_NOT_PRESENT;
      uint16_t prov_granted_keys = TLV_NOT_PRESENT;
      uint8_t printf_output[2000]; /* Printf buffer */

      if (bDatalen < (4 + dsklen))
      {
        return_value = COMMAND_PARSE_ERROR;
        goto cmd_handled;
      }

      if (dsklen < 16)
      {
        LOG_PRINTF("Too short DSK in Whitelist Set - discarding\n");
        return_value = COMMAND_PARSE_ERROR;
        goto cmd_handled;
      }

      tlv = pFrame->dsk + dsklen;

      while(tlv < end-2) {
        uint8_t type = tlv[0];
        uint8_t len = tlv[1];

        if(end < tlv +2 + len) {
          break;
        }

        switch(type >> 1) {
        case PVS_TLV_TYPE_NAME:
          name = &tlv[2];
          name_len = len;
          break;
        case PVS_TLV_TYPE_LOCATION:
          location = &tlv[2];
          location_len = len;
          break;
        case PVS_TLV_TYPE_BOOTSTRAP_MODE:
          bootstrap_mode = tlv[2];
          //mandatory_tlv_count++;
          break;

        case PVS_TLV_TYPE_PRODUCT_TYPE:
          if (len < 4) {
            return_value = COMMAND_PARSE_ERROR;
            goto cmd_handled;
          }
          product_type = &tlv[2];
          product_type_len = len;
          break;
        case PVS_TLV_TYPE_PRODUCT_ID:
          if (len < 8) {
            return_value = COMMAND_PARSE_ERROR;
            goto cmd_handled;
          }
          product_id = &tlv[2];
          product_id_len = len;
          break;
        case PVS_TLV_TYPE_STATUS:
          if (len < 1) {
            return_value = COMMAND_PARSE_ERROR;
            goto cmd_handled;
          }
          prov_status = (pvs_status_t)tlv[2];
          break;
        case PVS_TLV_TYPE_NETWORK_INFO:
           /* MUST be ignored in set command. */
          break;
        case PVS_TLV_TYPE_ADV_JOIN:
          if (len < 1) {
            return_value = COMMAND_PARSE_ERROR;
            goto cmd_handled;
          }
          prov_granted_keys = tlv[2];
          break;
        case PVS_TLV_TYPE_MAX_INCL_REQ_INTERVAL:
          if (len < 1) {
            return_value = COMMAND_PARSE_ERROR;
            goto cmd_handled;
          }
          max_incl_req_interval = tlv[2];
          break;
        case PVS_TLV_TYPE_UUID16:
          if (len < 17) {
            return_value = COMMAND_PARSE_ERROR;
            goto cmd_handled;
          }
          uuid16 = &tlv[2];
          break;
        default:
          if(type & PROVISIONING_LIST_OPT_CRITICAL_FLAG) {
            ERR_PRINTF("Unsupported critical option\n");
            return_value = COMMAND_PARSE_ERROR;
            goto cmd_handled;
          }
          break;
        }
        tlv+=2+len;
      }
      if (mandatory_tlv_count < 0)
      {
        DBG_PRINTF("command_provision_set: Discarding due to missing mandatory TLV(s)\n");
        return_value = COMMAND_PARSE_ERROR;
        goto cmd_handled;
      }
      memcpy(printf_output, name, name_len);
      printf_output[name_len] = 0;
      DBG_PRINTF("Name %s\n",printf_output);
      memcpy(printf_output, location, location_len);
      printf_output[location_len] = 0;
      DBG_PRINTF("Location %s\n", printf_output);

      /* FIXME: not allowing comma is temporary. separating commas should be backslashed*/

      if (name_len)
      {
        if(is_valid_mdns_name((char*)name, name_len) == false)
        {
            ERR_PRINTF("Unacceptable format of name\n");
            return_value = COMMAND_PARSE_ERROR;
            goto cmd_handled;
        }

      }
      if(location_len)
      {
        if(is_valid_mdns_location((char*)location, location_len) == false)
        {
            ERR_PRINTF("Unacceptable format of location\n");
            return_value = COMMAND_PARSE_ERROR;
            goto cmd_handled;
        }
      }

      if(tlv != end) {
        WRN_PRINTF("Inconsistent TLV %p %p\n",tlv,end);
      }

      pvl_entry = provisioning_list_dev_get(dsklen, (uint8_t*)&pFrame->dsk);
      if (pvl_entry == NULL) {
         /* This is a new entry. */
         /* The provisioning list module will set status to pending on
          * a new entry, but we have to handle the boot-strap mode. */
         if (bootstrap_mode == TLV_NOT_PRESENT) {
            bootstrap_mode = PVS_BOOTMODE_SMART_START;
         }
         pvl_entry = provisioning_list_dev_add(dsklen, (uint8_t*)&pFrame->dsk, bootstrap_mode);
         if (pvl_entry == NULL) {
            /* There are no more entries */
            break;
         }
      } else {
         if (bootstrap_mode != TLV_NOT_PRESENT) {
            provisioning_list_bootmode_set(pvl_entry, bootstrap_mode);
         }
      }
      if (prov_status != TLV_NOT_PRESENT) {
         provisioning_list_status_set(pvl_entry, prov_status);
      }
      if (name) {
        provisioning_list_tlv_set(pvl_entry, PVS_TLV_TYPE_NAME, name_len, name);
      }
      if (location) {
        provisioning_list_tlv_set(pvl_entry, PVS_TLV_TYPE_LOCATION, location_len, location);
      }
      if (prov_granted_keys != TLV_NOT_PRESENT) {
        uint8_t temp_keys = prov_granted_keys & 0xFF;
        provisioning_list_tlv_set(pvl_entry, PVS_TLV_TYPE_ADV_JOIN, 1, &temp_keys);
      }
      if (product_type != 0) {
        provisioning_list_tlv_set(pvl_entry, PVS_TLV_TYPE_PRODUCT_TYPE, product_type_len, product_type);
      }
      if (product_id != 0) {
        provisioning_list_tlv_set(pvl_entry, PVS_TLV_TYPE_PRODUCT_ID, product_id_len, product_id);
      }
      if (max_incl_req_interval != TLV_NOT_PRESENT) {
        uint8_t temp_val = max_incl_req_interval & 0xFF;
        provisioning_list_tlv_set(pvl_entry, PVS_TLV_TYPE_MAX_INCL_REQ_INTERVAL, 1, &temp_val);
      }
      if (uuid16 != 0) {
        provisioning_list_tlv_set(pvl_entry, PVS_TLV_TYPE_UUID16, 17, uuid16);
      }
    }
    break;

  case COMMAND_PROVISION_DELETE:
    {
      ZW_PROVISION_DELETE_FRAME_EX *pd = (ZW_PROVISION_DELETE_FRAME_EX*)&pData[0];
      uint8_t dsklen = pd->reserved_dsk_length & PROVISIONING_LIST_DSK_LENGTH_MASK;
      if((bDatalen>=4) && (dsklen == 0)) {
        /*Delete all*/
        provisioning_list_clear();
      } else if (bDatalen < (4 + dsklen)) {
        return_value = COMMAND_PARSE_ERROR;
        goto cmd_handled;
      } else {
        if (provisioning_list_dev_remove(dsklen, pd->dsk) != PVS_SUCCESS)
        {
          // Silent error if we could not delete - do nothing
        }
      }
      return_value = COMMAND_HANDLED;
      goto cmd_handled;
    }
    break;

  case COMMAND_PROVISION_ITER_GET:
    {
      static provisioning_list_iter_t *curr_iter = NULL;
      provisioning_list_t *entry;
      uint8_t *pt; // pointer to end of variable length report we are creating
      ZW_PROVISION_ITER_GET_FRAME_EX* ig = (ZW_PROVISION_ITER_GET_FRAME_EX*) pData;
      ZW_PROVISION_ITER_REPORT_FRAME_EX* ir = (ZW_PROVISION_ITER_REPORT_FRAME_EX*)buf;
      if (ig->remaining == 0xFF) {
        if (curr_iter) {
          provisioning_list_iterator_delete(curr_iter);
        }
        curr_iter = provisioning_list_iterator_get(0);
      } else if (!curr_iter
                 || ig->remaining != curr_iter->cnt - curr_iter->next)
      {
        return_value = COMMAND_PARSE_ERROR;
        goto cmd_handled;
      }
      entry = provisioning_list_iter_get_next(curr_iter);

      ir->cmdClass = COMMAND_CLASS_PROVISIONING_LIST;
      ir->cmd = COMMAND_PROVISION_ITER_REPORT;
      ir->seqNo = ig->seqNo;
      ir->remaining = curr_iter->cnt - curr_iter->next;
      if(NULL == entry) {
        ir->reserved_dsk_length = 0;
        ZW_SendDataZIP(conn, buf, 5, 0);
        return_value = COMMAND_HANDLED;
        goto cmd_handled;
      }


      if(entry->status == PVS_STATUS_PASSIVE) {
        provisioning_list_status_set(entry,PVS_STATUS_PENDING);
      }

      ir->reserved_dsk_length = entry->dsk_len & PROVISIONING_LIST_DSK_LENGTH_MASK;
      memcpy(ir->dsk, entry->dsk, entry->dsk_len);
      pt = (uint8_t*)&ir->dsk + entry->dsk_len;

      pt = CC_provisioning_list_add_tlvs(pt, entry);

      ZW_SendDataZIP(conn, buf, pt - buf, 0);

      return_value = COMMAND_HANDLED;
      goto cmd_handled;

    }
    break;

  case COMMAND_PROVISION_GET:
    {
      int len;
      ZW_PROVISION_GET_FRAME_EX* g = (ZW_PROVISION_GET_FRAME_EX*) pData;
      provisioning_list_t *entry;
      entry = provisioning_list_dev_get(g->reserved_dsk_length, g->dsk);

      len = CC_provisioning_list_build_report(buf,entry,g->seqNo);
      ZW_SendDataZIP(conn,buf,len,0);

      return_value = COMMAND_HANDLED;
      goto cmd_handled;
    }
    break;

  default:
    return_value = COMMAND_NOT_SUPPORTED;
    goto cmd_handled;
    break;
  }

cmd_handled:
  NetworkManagement_smart_start_init_if_pending();
  return return_value;
}


REGISTER_HANDLER(
    PVL_CommandHandler,
    0,
    COMMAND_CLASS_PROVISIONING_LIST, PROVISIONING_LIST_VERSION_V1, SECURITY_SCHEME_2_ACCESS);
