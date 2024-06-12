/**
  © 2019 Silicon Laboratories Inc. 
*/


#include "CC_Wakeup.h"
#include "zw_network_info.h"
#include "zip_router_ipv6_utils.h" /* nodeOfIP */
#include "ZIP_Router_logging.h"
#include "zip_router_config.h"
#include "zw_frame_buffer.h"
#include "ResourceDirectory.h"
#include "command_handler.h"
#include "NodeCache.h"
#include "Mailbox.h"
#include "CC_NetworkManagement_queue.h"

static command_handler_codes_t
WakeUpHandlerInternal(zwave_connection_t *c, uint8_t *frame, uint16_t length, uint8_t was_just_probed);

static void wakeup_node_was_probed_callback(rd_ep_database_entry_t* ep, void* user) {
  zw_frame_ip_buffer_element_t* fb = (zw_frame_ip_buffer_element_t*)user;

  if(fb) {
    WakeUpHandlerInternal(&fb->conn, fb->frame_data,fb->frame_len,1);
    zw_frame_ip_buffer_free(fb);
  }
}

static command_handler_codes_t
WakeUpHandlerInternal(zwave_connection_t *c, uint8_t *frame, uint16_t length, uint8_t was_just_probed)
{
    ZW_APPLICATION_TX_BUFFER *pCmd = (ZW_APPLICATION_TX_BUFFER *)frame;
    uint32_t new_node_flags =0xFFFFFFFF;
    nodeid_t snode = nodeOfIP(&c->ripaddr);
    if((c->scheme == SECURITY_SCHEME_UDP) || (snode == 0)) return CLASS_NOT_SUPPORTED;
    security_scheme_t snode_scheme = highest_scheme(GetCacheEntryFlag(snode));
    nodeid_t suc_node = ZW_GetSUCNodeID(); /* Ask the protocol */

    switch (pCmd->ZW_WakeUpNotificationFrame.cmd)
    {
    case WAKE_UP_NOTIFICATION:
        /* If we are not SIS, ignore broadcast WUN. */
        if ((c->rx_flags & RECEIVE_STATUS_TYPE_BROAD)
            && (MyNodeID != suc_node)) {
           return COMMAND_HANDLED;
        }

        if(!was_just_probed) 
        {
          /*
          * CC:0084.01.07.12.002
          * Upon receiving this command via broadcast, a receiving node SHOULD
          * configure a relevant Wake Up destination issuing a Wake Up Interval Set Command to the node that
          * issued this command via broadcast.
          * 
          * We only do this if we are SIS
          */
          if((c->rx_flags & RECEIVE_STATUS_TYPE_BROAD) 
              && (MyNodeID == suc_node) 
              && (isNodeSecure(snode)==0)
            ) {
            new_node_flags = RD_NODE_FLAG_JUST_ADDED;
          }

          /* 
          * This node wakes up with incomplete information in RD and never
          * has chance to be probed, we should probe it.
          */
          if ((rd_get_node_state(snode) == STATUS_PROBE_FAIL)
              && (rd_get_node_probe_flags(snode) == RD_NODE_PROBE_NEVER_STARTED))
          {
            new_node_flags = 0; 
          }

          if(new_node_flags != 0xFFFFFFFF) {
            zw_frame_ip_buffer_element_t* fb = zw_frame_ip_buffer_create(c,frame, length);
            
            if (fb && rd_register_node_probe_notifier(snode, fb, wakeup_node_was_probed_callback)) {
              //Trigger a full reprobe, which will setup the wakeup interval.
               rd_register_new_node(snode, new_node_flags);
              return COMMAND_HANDLED;
            } else {
              ERR_PRINTF("Node probe not triggred since we are lacking buffer space.\n");
              if (fb) {
                 zw_frame_ip_buffer_free(fb);
              }
            }
          }
        }

        
        if(cfg.mb_conf_mode != DISABLE_MAILBOX)
        {
            /* S2 nodes MUST send WUN on highest scheme (a.k.a level), and we filter them out otherwise. We don't filter S0 nodes
            * because they are allowed to use either scheme.
            */
            if ((snode_scheme == SECURITY_SCHEME_0) || (c->scheme == snode_scheme))
            {
                if(c->rx_flags & RECEIVE_STATUS_TYPE_BROAD) {
                  mb_wakeup_event(snode, true);
                } else if(!NetworkManagement_queue_wakeup_event(snode) ) {
                  mb_wakeup_event(snode, false);
                }
            }
            else
            {
                WRN_PRINTF("Wake up notification from node:%d is received on lower security level. Ignoring\n", snode);
            }
            return COMMAND_HANDLED;
        }
        else
        {
            /*
                * ZGW-2320:
                * Workaround for stopping the verify deliver timer in the target node, due to the 
                * use of virtual nodes.
                * This code should be removed when we are no longer using virtual nodes.
            */
            switch (c->scheme)
            {
            case SECURITY_SCHEME_2_UNAUTHENTICATED:
            case SECURITY_SCHEME_2_AUTHENTICATED:
            case SECURITY_SCHEME_2_ACCESS:
            {
                const uint8_t nop_frame = {COMMAND_CLASS_NO_OPERATION};
                ZW_SendDataZIP( c,&nop_frame,sizeof(nop_frame),0 );
                break;
            }
            default:
                break;
            }
        }
        break;
    default:
        break;    
    }

    return COMMAND_NOT_SUPPORTED;
}


command_handler_codes_t
WakeUpHandler(zwave_connection_t *c, uint8_t *frame, uint16_t length)
{
  return WakeUpHandlerInternal(c,frame,length,0);
}


