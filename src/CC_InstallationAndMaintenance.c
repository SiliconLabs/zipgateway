/* © 2014 Silicon Laboratories Inc.
 */


/**
 * SDS12954
 *
 * Z/IP Gateway Installation and Maintenance Framework
 *
 */


#include "sys/clock.h"
#include "CC_InstallationAndMaintenance.h"
#include "Serialapi.h"
#include "ZW_classcmd_ex.h"
#include "ResourceDirectory.h"
#include "command_handler.h"
#include "ZIP_Router_logging.h"
#include "zgw_nodemask.h"
#include "ZW_transport_api.h"

#define ZWAVE_NODEINFO_BAUD_9600                0x08
#define ZWAVE_NODEINFO_BAUD_40000               0x10
#define LR_NOT_SUPPORTED 128

/* Speed extension in nodeinfo reserved field */
#define ZWAVE_NODEINFO_BAUD_100K                 0x01
//#define ZWAVE_NODEINFO_BAUD_200K                 0x02 /* Tentative */


static clock_time_t transmit_start_time;

struct node_ima ima_database[ZW_MAX_NODES];

u16_t tApp =0;

/**
 * RSSI – The Received Signal Strength Indication for the transmitted frame. If frame is routed,
 * NOTE that this will only contain the RSSI between the Z/IP GW and the first hop
 */
u8_t ima_last_snr;

/**
 * Transmission Time (TT) – Time from SendData() return to callback is received, in ms
 */
u16_t ima_last_tt;

/**
 * Indicates if the route was changed on the last transmission
 */
u8_t ima_was_route_changed;

void get_last_tx_stats(u16_t* time, u8_t* rc, u8_t* rssi ) {
  *time = ima_last_tt;
  *rc = ima_was_route_changed;
  *rssi = ima_last_snr;
}


const struct node_ima* ima_get_entry(nodeid_t node) {
  // Return NULL for invalid node requests.
  return (node > 0 && node <= ZW_MAX_NODES) ? &ima_database[node-1] : NULL;
}

/**
 * Called when just before senddata is issued
 */
void ima_send_data_start(nodeid_t node) {
  transmit_start_time = clock_time();
}


/*
 * Called when send_data is completed
 */
void ima_send_data_done(nodeid_t node,u8_t status, TX_STATUS_TYPE* tx_info) {
  struct node_ima* ima;
  struct route lwr;

  ASSERT(node>0);

  ima = &ima_database[node-1];

  ima->tc++;

  if(status == TRANSMIT_COMPLETE_NO_ACK) {
    ima_was_route_changed = FALSE;
    ima->pec++;
    return;
  }

  if(tx_info) {
    ima_last_tt = tx_info->wTransmitTicks; 
    lwr.repeaters[0] = tx_info->pLastUsedRoute[LAST_USED_ROUTE_REPEATER_0_INDEX];
    lwr.repeaters[1] = tx_info->pLastUsedRoute[LAST_USED_ROUTE_REPEATER_1_INDEX];
    lwr.repeaters[2] = tx_info->pLastUsedRoute[LAST_USED_ROUTE_REPEATER_2_INDEX];
    lwr.repeaters[3] = tx_info->pLastUsedRoute[LAST_USED_ROUTE_REPEATER_3_INDEX];
    lwr.speed = tx_info->pLastUsedRoute[LAST_USED_ROUTE_CONF_INDEX];

    ima_was_route_changed = (ima->lwr.repeaters[0] != lwr.repeaters[0]) ||
      (ima->lwr.repeaters[1] != lwr.repeaters[1]) ||
      (ima->lwr.repeaters[2] != lwr.repeaters[2]) ||
      (ima->lwr.repeaters[3] != lwr.repeaters[3]) ||
      (ima->lwr.speed != lwr.speed);
    if(ima_was_route_changed) {
      ima->lwr = lwr;
      DBG_PRINTF("Route change\n");
    }

  } else { //We dont have IMA info in the callback, so do it the hard way
    if(ZW_GetLastWorkingRoute(node,(BYTE*)&lwr) && ( 
    (ima->lwr.repeaters[0] != lwr.repeaters[0]) ||
    (ima->lwr.repeaters[1] != lwr.repeaters[1]) ||
    (ima->lwr.repeaters[2] != lwr.repeaters[2]) ||
    (ima->lwr.repeaters[3] != lwr.repeaters[3]) ||
    (ima->lwr.speed != lwr.speed) ))
    {
      ima->lwr = lwr;
      DBG_PRINTF("Route change\n");
      ima_was_route_changed =TRUE;
    } else if( (ima_last_tt  > (ima->last_tt  + 150))
               /* If node was failing, MODE_FLAGS_FAILED has not been cleared at this point */
               && (rd_node_mode_value_get(node) == MODE_ALWAYSLISTENING) ) {
      /* To be able to detect if route has gone from route A to B and back to A, we to a timer based calculation.
      * We only do timer based route changes if the node is allways listening node.*/
      DBG_PRINTF("Route change based on timer\n");
      ima_was_route_changed = TRUE;
    } else {
      ima_was_route_changed = FALSE;
    }

    ima_last_tt = clock_time() - transmit_start_time;
    if(ima_last_tt > tApp) {
      ima_last_tt = ima_last_tt - tApp;
    }
    ima_last_snr = 0; /*TODO*/
  }



  /*Time statistics */
  ima->mts += ima_last_tt;
  ima->mts2 += ima_last_tt*ima_last_tt;

  if(ima_was_route_changed) {
    ima->rc++;
  }

  ima->last_tt = ima_last_tt;
}

void ima_reset() {
  nodeid_t n;
  struct route lwr;
  memset(ima_database,0, sizeof(ima_database));

  for(n=1; n<= ZW_MAX_NODES; n++) {
    if(rd_node_exists(n)) {
      if(ZW_GetLastWorkingRoute(n, (BYTE*)&lwr ) ) {
        memcpy(&ima_database[n-1].lwr,&lwr,5);
      }
    }
  }
}


static command_handler_codes_t ima_CommandHandler(zwave_connection_t *c, BYTE* pData, WORD bDatalen) {
  nodeid_t i;
  BYTE routeType;

  switch(pData[1]) {
    case PRIORITY_ROUTE_SET:
      if(bDatalen >=8) {
      ZW_SetLastWorkingRoute(pData[2],&pData[3]);
      }
      break;
    case PRIORITY_ROUTE_GET:

      pData[1] = PRIORITY_ROUTE_REPORT;
//      pData[2] = pData[2] nodeID;
      routeType = ZW_GetPriorityRoute(pData[2],&pData[4]);
      pData[3] = routeType;
      ZW_SendDataZIP(c,pData,6+3,0);
      break;
    case EXTENDED_STATISTICS_GET:
    case STATISTICS_GET:
    {
      u8_t *p = pData;

      nodeid_t nodeid;

      *p++ = COMMAND_CLASS_NETWORK_MANAGEMENT_INSTALLATION_MAINTENANCE;
      if (pData[1] == EXTENDED_STATISTICS_GET) {
        nodeid = (pData[2] << 8) | pData[3];
        *p++ = EXTENDED_STATISTICS_REPORT;
        *p++ = pData[2]; //Extended NodeID (MSB)
        *p++ = pData[3]; //Extended NodeID (LSB)
      } else {
        nodeid = pData[2];
        *p++ = STATISTICS_REPORT;
        *p++ = pData[2];
      }
      const struct node_ima* ima = ima_get_entry(nodeid);
      if (NULL == ima) {
        // Invalid request, drop it.
        break;
      }
      *p++ = TRANSMISSION_COUNT;
      *p++ = 1;
      *p++ = ima->tc;

      *p++ = PACKET_ERROR_COUNT;
      *p++ = 1;
      *p++ = ima->pec;

      *p++ = ROUTE_CHANGES;
      *p++ = 1;
      *p++ = ima->rc;

      *p++ = NEIGHBORS;

      {
        NODEINFO ni;
        u8_t nb[ZW_MAX_NODES / 8];
        u8_t *length;

        *p=0;
        length = p;
        p++;

        ZW_GetRoutingInfo_old(nodeid, nb, FALSE, FALSE);

        for (i = 0; i < ZW_MAX_NODES; i++)
        {
          if (rd_node_exists(i + 1) && BIT8_TST(i, nb))
          {
            *length += 2;
            ZW_GetNodeProtocolInfo(i + 1, &ni);

            *p++ = i + 1; //NodeID

#if 0
            if (ni.reserved & ZWAVE_NODEINFO_BAUD_200K)
            {
              *p |= IMA_NODE_SPEED_200;
            } else 
#endif
            *p = 0;
            if (ni.reserved & ZWAVE_NODEINFO_BAUD_100K)
            {
              /* ni.reserved & ZWAVE_NODEINFO_BAUD_100K reports 100K capability
               * in addition to whatever ni.capability reports below */
              *p |= IMA_NODE_SPEED_100;
            }
            if (ni.capability & ZWAVE_NODEINFO_BAUD_40000)
            {
              /* ni.capability sets either ZWAVE_NODEINFO_BAUD_40000 or ZWAVE_NODEINFO_BAUD_96
               * depending on the maximum below-100K speed of the node. It will not set both.
               * We know that all 40K nodes also support 9.6K, so we set that bit also */
              *p |= IMA_NODE_SPEED_40 | IMA_NODE_SPEED_96;
            }
            else if (ni.capability & ZWAVE_NODEINFO_BAUD_9600)
            {
              *p |= IMA_NODE_SPEED_96;
            }

            if ((ni.capability & NODEINFO_ROUTING_SUPPORT) && (ni.capability & NODEINFO_LISTENING_SUPPORT) ) {
              *p |= IMA_NODE_REPEATER; //Is repeater
            }
            p++;
          }
        }
      }

      *p++ = TANSMISSION_TIME_SUM;
      *p++ = 4;
      *p++ = (ima->mts>>24) & 0xFF;
      *p++ = (ima->mts>>16) & 0xFF;
      *p++ = (ima->mts>> 8) & 0xFF;
      *p++ = (ima->mts>> 0) & 0xFF;

      *p++ = TANSMISSION_TIME_SUM2;
      *p++ = 4;
      *p++ = (ima->mts2>>24) & 0xFF;
      *p++ = (ima->mts2>>16) & 0xFF;
      *p++ = (ima->mts2>> 8) & 0xFF;
      *p++ = (ima->mts2>> 0) & 0xFF;

      ZW_SendDataZIP(c,pData,(u8_t)(p - pData),0);
    }
    break;
    case RSSI_GET:
    {
      uint8_t rssi_buffer[20];
      uint8_t *r = rssi_buffer;
      uint8_t num_channels; /* Number of channels used by ZW chip */
      uint8_t channel;

      r[0] = COMMAND_CLASS_NETWORK_MANAGEMENT_INSTALLATION_MAINTENANCE;
      r[1] = RSSI_REPORT;
      ZW_GetBackgroundRSSI(&r[2], &num_channels);
      channel = GetLongRangeChannel();
      if (num_channels == 2)
      {
        r[4] = RSSI_NOT_AVAILABLE;   /* Spec SDS13784-4 requires us to insert the 3rd channel even if unavailable
                          it is a shame, since the ZIP Client could otherwise detect if this is a 2CH
                          or 3CH system. */
      }

      if ((r[5] == RSSI_NOT_AVAILABLE) || (LR_NOT_SUPPORTED == channel)) {
        r[5] = RSSI_NOT_AVAILABLE;
        r[6] = RSSI_NOT_AVAILABLE;
      } else {
        if (channel == 1) {
           r[6] = RSSI_NOT_AVAILABLE; // set second channel RSSI_NOT_AVAILABLE 
        } else if (channel == 2) {
           r[6] = r[5];
           r[5] = RSSI_NOT_AVAILABLE; // set first channel RSSI_NOT_AVAILABLE 
        }
      }

      ZW_SendDataZIP(c, rssi_buffer, 7, 0);
    }
    break;


    case STATISTICS_CLEAR:
      ima_reset();
      break;
    case ZWAVE_LR_CHANNEL_CONFIGURATION_SET:
      if (ZW_RFRegionGet() == RF_US_LR) {
        DBG_PRINTF("Setting Long Range channel to: %d\n", pData[2]);
        SetLongRangeChannel(pData[2]);
      } else {
        ERR_PRINTF("Z-Wave module is not in Long Range mode. "
                    "SetLongRangeChannel will not work.\n");
      }
      break;
    case ZWAVE_LR_CHANNEL_CONFIGURATION_GET:
    {
      uint8_t report_frame[3] = {0};
      report_frame[0] = COMMAND_CLASS_NETWORK_MANAGEMENT_INSTALLATION_MAINTENANCE;
      report_frame[1] = ZWAVE_LR_CHANNEL_CONFIGURATION_REPORT;
      report_frame[2] = GetLongRangeChannel();
      if (LR_NOT_SUPPORTED == report_frame[2]) {
        report_frame[2] = 1; /* Just fake the report if LR is not supported */
      }
      DBG_PRINTF("Long Range channel is: %d\n", report_frame[2]);
      ZW_SendDataZIP(c, report_frame, sizeof(report_frame), 0);
    }
    break;
    default:
      return COMMAND_NOT_SUPPORTED;
      break;
  }
  return COMMAND_HANDLED;
}

void ima_init() {
  clock_time_t t0;
  //Benchmark the SerialAPI for a short frame;
  t0 = clock_time();
  ZW_Type_Library();
  tApp = clock_time()- t0;
  DBG_PRINTF("Resetting IMA\n");
  ima_reset();
}



REGISTER_HANDLER(
    ima_CommandHandler,
    ima_init,
    COMMAND_CLASS_NETWORK_MANAGEMENT_INSTALLATION_MAINTENANCE, NETWORK_MANAGEMENT_INSTALLATION_MAINTENANCE_VERSION_4, SECURITY_SCHEME_0);
