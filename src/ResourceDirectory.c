/* © 2014 Silicon Laboratories Inc.  */

#include "zw_network_info.h" /* MyNodeID */
#include "ResourceDirectory.h"
#include "RD_DataStore.h"
#include "mDNSService.h"
#include "ZIP_Router_logging.h"
#include "zip_router_config.h"
#include "router_events.h" /* zip_process */
//#include "Serialapi.h"
#include "NodeCache.h"
#include "Mailbox.h"
#include "Bridge.h" /* is_virtual_node */
#include "S2.h"
#include "ZW_SendRequest.h"
#include "CommandAnalyzer.h" /* for rd_check_security_for_unsolicited_dest() */
#include "ZW_SendDataAppl.h"
#include "CC_Gateway.h" /* IsCCInNodeInfoSetList for rd_check_security_for_unsolicited_dest() */
#include "CC_NetworkManagement.h"
#include "security_layer.h"
#include <ctype.h>
#include <stdlib.h>
#include <strings.h>
#include <stdio.h> /* sprintf */
#include <stdbool.h>

#include "ipv46_nat.h"

#include "ZW_ZIPApplication.h"

#include "RD_probe_cc_version.h"

#include "zgw_str.h"

//This is 2000ms
#define REQUEST_TIMEOUT 200

// This is define from ZW_transport.h in the protocol.
#define ZWAVE_NODEINFO_CONTROLLER_NODE           0x02

typedef char rd_name_t[64];
int dont_set_cache_flush_bit = 0;
struct ctimer rd_node_probe_update_timer;
struct ctimer ep_probe_update_timer;
extern int tie_braking_won;
extern int conflict_when_not_in_qs;
/* RD probe lock. If this lock is set then the probe machine is locked. */
static uint8_t probe_lock = 0;
uint8_t denied_conflict_probes = 0;
static uint8_t identical_endpoints = 0;
/* This global, when set to 1, indicates that the ZIP GW has entered an existing network
 * and has assigned itself the SUC/SIS role. The ZGW must proceed to inform all existing
 * nodes about the presence of itself as SUC/SIS and will then clear this flag. */
uint8_t suc_changed = 0;

/* forward */
void
rd_node_probe_update(rd_node_database_entry_t* data);
void
rd_ep_probe_update(rd_ep_database_entry_t* ep);

/** Regularly check if mailbox nodes are failing */
static void rd_check_for_dead_nodes_worker(void* v);

/** Check if endpoint supports a command class non-securely.
 *
 * \param ep Valid pointer to an endpoint.
 * \param class The command class to check for.
 * \return True is class is supported by ep, otherwise false.
 */
static int rd_ep_supports_cmd_class_nonsec(rd_ep_database_entry_t* ep, uint16_t class);

static int
SupportsCmdClassFlags(nodeid_t nodeid, WORD class);

static struct ctimer dead_node_timer;
static struct ctimer nif_request_timer;
static struct ctimer find_report_timer;


typedef struct node_probe_done_notifier
{
   /** Callback used to report interview result to a requester. \see
    * \ref send_ep_name_reply() for \a ZIP_NAMING_NAME_GET and \a
    * ZIP_NAMING_LOCATION_GET and \ref
    * wakeup_node_was_probed_callback() for \a WUN. */
  void (*callback)(rd_ep_database_entry_t* ep, void* user);
  void* user;
  nodeid_t node_id;
} node_probe_done_notifier_t;

#define NUM_PROBES 1

/** Storage for callbacks to clients that want notification when a
 * node probe is completed.
 *
 * Note that the callback will be triggered when the probing state
 * machine reaches a final state, ie, both when the probe completes
 * and when it fails.
 *
 * There is only a limited amount of slots, so the client has to
 * handle failure.
 */
static node_probe_done_notifier_t node_probe_notifier[NUM_PROBES];

/** Initialize/reset the array of notifiers.
 *
 * Clear all entries.
 */
static void rd_reset_probe_completed_notifier(void);

/** Call the notifiers if nodeid matches and there are any.
 * Clear the nodeid.
 */
static void rd_trigger_probe_completed_notifier(rd_node_database_entry_t *node);


/**
 * Increase the number at the end of the string. Add a 1 if there is no number
 * return the new length of the string
 *
 * s, as string buffer which must be large enough to contain the new string
 * ilen the inital length of s
 * maxlen the maximum lenth of the new string.
 */
static u8_t
add_or_increment_trailing_digit(char* s, u8_t ilen, u8_t maxlen)
{
  u8_t k, d;

  k = ilen - 1;
  while (isdigit(s[k]) && (k >=0) )
    k--;

  if (s[k] == '-')
  {
    d = atoi(&(s[k + 1])) + 1;
  }
  else
  {
    d = 1;
    k = ilen;
  }

  k += snprintf(&(s[k]), maxlen - k, "-%i", d);
  s[k] = 0;
  return k;
}


/* If node state is STATUS_FAILING or STATUS_DONE, check if \p failing
 * matches state.  If not, change state, set failed flag, update mdns, 
 * persist the new state, and send a failed node list to client.
 *
 * Do nothing if node state is something else. */
void rd_set_failing(rd_node_database_entry_t* n, uint8_t failing) {
  rd_ep_database_entry_t* ep;
  rd_node_state_t s;
  s = failing ? STATUS_FAILING : STATUS_DONE;

  if (n->state != s
      && ((n->state == STATUS_FAILING) || (n->state == STATUS_DONE)))
  {
    DBG_PRINTF("Node %d is now %s\n", n->nodeid, failing ? "failing" : "ok");

    n->state = s;
    n->mode &= ~MODE_FLAGS_FAILED;
    n->mode |= (n->state == STATUS_FAILING) ? MODE_FLAGS_FAILED : 0;

    /*Persisting the Failing Node state*/
    rd_data_store_update(n);

    for (ep = list_head(n->endpoints); ep != NULL; ep = list_item_next(ep))
    {
      mdns_endpoint_notify(ep, 0);
    }

    NetworkManagement_SendFailedNodeList_To_Unsolicited();

    /* Force node re-discovery */
    /*n->mode |= MODE_FLAGS_DELETED;
     n->mode &= ~MODE_FLAGS_DELETED;
     for (ep = list_head(n->endpoints); ep != NULL; ep = list_item_next(ep)) {
     mdns_endpoint_notify(ep);
     }*/
  }
}

static void
rd_check_for_dead_nodes_worker(void* v)
{
  static const uint8_t nop = 0;
  nodeid_t i;
  rd_node_database_entry_t *n;
  nodeid_t start_with_node = v - (void*) 0;

  uint32_t limit = 0;

  for (i = start_with_node; i <= ZW_MAX_NODES; i++)
  {
    n = rd_node_get_raw(i);
    if (!n)
      continue;
    if (n->nodeid == MyNodeID)
      continue;

    if ((RD_NODE_MODE_VALUE_GET(n) == MODE_MAILBOX) ||
        (RD_NODE_MODE_VALUE_GET(n) == MODE_FIRMWARE_UPGRADE)) {
    /* Devices with a wakeup interval of 0 need special treatment.
     * They only wake up on external events, not on a regular schedule.
     * Since we have no expected wakeup time for these devices, they are never
     * marked failing for missing a wakeup. */
       if (n->wakeUp_interval > 0) {
          limit = n->lastAwake + 3 * n->wakeUp_interval; //3 times wakeup interval
       }
       //  ERR_PRINTF("Node %i wakeup interval %i\n", n->nodeid, n->wakeUp_interval);
       /* if the wakeup interval is 0 (nodes never failing) or sleeping nodes which
                 are already marked failed*/
       if (((n->wakeUp_interval == 0) || (n->mode & MODE_FLAGS_FAILED)))
       {
         //DBG_PRINTF("Node (%d)is already marked failed or has wakeup interval 0. Just purging mailbox messages for it\n", n->nodeid);
         mb_purge_messages(n->nodeid);
       }
       else if (limit < clock_seconds())
       {
           if(mb_enabled()) {
               DBG_PRINTF("mailbox node %i is now failing, because it has not reported in\n",n->nodeid);
               /*Notify the mailbox*/
               /* Following function ends up purging messages for the mailbox. Only messages older 
                  than 10 mins are purged there. As rd_check_for_dead_nodes_worker() is called every 
                  minute if condition above mb_purge_messages() ends up periodically purging messages 
                  which are not purged here */
               mb_failing_notify(n->nodeid);

               rd_set_failing(n, TRUE);
           }
        }
    }
  }
  /*Re schedule probe in 1 minute */
  ctimer_set(&dead_node_timer, 60 * 1000, rd_check_for_dead_nodes_worker, (void*) 1);
}

clock_time_t rd_calculate_s2_inclusion()
{
 clock_time_t timeout = 0;
 nodeid_t i = 0;
 rd_node_database_entry_t *n;

 for (i = 1; i <= ZW_CLASSIC_MAX_NODES; i++)
 {
   n = rd_node_get_raw(i);

   if (!n)
     continue;

   if ((n->mode & 0xff) == MODE_FREQUENTLYLISTENING)
     timeout += 3517;

   if ((n->mode & 0xff) == MODE_ALWAYSLISTENING)
     timeout += 217;
  }
  DBG_PRINTF("S2 Inclusion timeout (based on the network nodes) calculated is: %lu\n", timeout);
  return timeout;

}

/*
The timeouts are described in Appl Guide(INS13954):

4.4.1.3.3 AddNodeTimeout: An application MUST implement a timeout for waiting for the protocol 
library to complete inclusion (add). The timeout MUST be calculated according to the formulas 
presented in sections 4.4.1.3.3.1 and 4.4.1.3.3.2.

4.4.1.3.3.1 New slave: AddNodeTimeout.NewSlave = 76000ms + LISTENINGNODES*217ms + FLIRSNODES*3517ms 
where LISTENINGNODES is the number of listening nodes in the network, and FLIRSNODES is the number
of nodes in the network that are reached via beaming.

4.4.1.3.3.2 New controller: AddNodeTimeout.NewController = 76000ms + LISTENINGNODES*217ms + 
FLIRSNODES*3517ms + NETWORKNODES*732ms, where LISTENINGNODES is the number of listening nodes
in the network, and FLIRSNODES is the number of nodes in the network that are reached via beaming.
NETWORKNODES is the total number of nodes in the network, i.e. NONLISTENINGNODES + LISTENINGNODES
+ FLIRSNODES.
*/
clock_time_t rd_calculate_inclusion_timeout(BOOL is_controller)
{
  clock_time_t timeout = 76000;
  nodeid_t i = 0;
  rd_node_database_entry_t *n;

  for (i = 1; i <= ZW_MAX_NODES; i++)
  {
    n = rd_node_get_raw(i);

    if (!n)
      continue;

    if (is_virtual_node(n->nodeid))
      continue;

    if (is_controller) {
      timeout += 732;
    }

    if ((n->mode & 0xff) == MODE_FREQUENTLYLISTENING)
      timeout += 3517;

    if ((n->mode & 0xff) == MODE_ALWAYSLISTENING)
      timeout += 217;
   }
   DBG_PRINTF("Inclusion timeout (based on the network nodes) calculated is: %lu\n", timeout);
   return timeout;
}

int rd_register_node_probe_notifier(nodeid_t node_id, void* user,
                                    void (*callback)(rd_ep_database_entry_t* ep, void* user)) {
  int ii;

  /* If the node does not exist yet, just assume that it will be created later. */
  for (ii = 0; ii < NUM_PROBES; ii++) {
     if (node_probe_notifier[ii].node_id == 0) {
        node_probe_notifier[ii].node_id = node_id;
        node_probe_notifier[ii].user = user;
        node_probe_notifier[ii].callback = callback;
        return 1;
     }
  }
  DBG_PRINTF("Failed to find a slot for the probe notifier\n");
  return 0;
}

static uint8_t
rd_add_endpoint(rd_node_database_entry_t* n, BYTE epid)
{
  rd_ep_database_entry_t* ep;

  ep = (rd_ep_database_entry_t*) rd_data_mem_alloc(
      sizeof(rd_ep_database_entry_t));
  if (!ep)
  {
    ERR_PRINTF("Out of memory\n");
    return 0;
  }

  memset(ep, 0, sizeof(rd_ep_database_entry_t));

  ep->endpoint_id = epid;
  ep->state = EP_STATE_PROBE_INFO;
  ep->node = n;

  /*A name of null means the default name should be used.*/
  ep->endpoint_name_len = 0;
  ep->endpoint_name = 0;
  list_add(n->endpoints, ep);
  n->nEndpoints++;

  return 1;
}

static void rd_wu_cc_version_callback(BYTE txStatus, BYTE rxStatus,
    ZW_APPLICATION_TX_BUFFER *pCmd, WORD cmdLength, void* user)
{

    rd_node_database_entry_t* e = (rd_node_database_entry_t*) user;
    if (txStatus == TRANSMIT_COMPLETE_OK) {
        if(pCmd->ZW_VersionCommandClassReportFrame.requestedCommandClass == COMMAND_CLASS_WAKE_UP) {
            if (pCmd->ZW_VersionCommandClassReportFrame.commandClassVersion >= 0x02) {
                e->state = STATUS_GET_WU_CAP;
                DBG_PRINTF("WAKEUP CC V2 or above\n");
            } else if (pCmd->ZW_VersionCommandClassReportFrame.commandClassVersion == 0x01) {
                e->state = STATUS_SET_WAKE_UP_INTERVAL;
                DBG_PRINTF("WAKEUP CC V1\n");
                if (e->node_properties_flags & RD_NODE_FLAG_PORTABLE) {
                  e->wakeUp_interval = 0;
                }
            } else {
                ERR_PRINTF("Unknown version of Wakeup command class\n");
                e->state = STATUS_PROBE_FAIL;
            }
        } else {
            ERR_PRINTF("version report is not of Wakeup command class\n");
            e->state = STATUS_PROBE_FAIL;
        }
    } else {
        ERR_PRINTF("Not TRANSMIT_COMPLETE_OK\n");
        e->state = STATUS_PROBE_FAIL;
    }
    rd_node_probe_update(e);
}

static int rd_cap_wake_up_callback(BYTE txStatus, BYTE rxStatus,
    ZW_APPLICATION_TX_BUFFER *pCmd, WORD cmdLength, void* user)
{
  uint32_t min_interval = 0;
  uint32_t max_interval = 0;
  uint32_t step = 0;

  rd_node_database_entry_t* e = (rd_node_database_entry_t*) user;
  if (txStatus == TRANSMIT_COMPLETE_OK)
  {
    min_interval = pCmd->ZW_WakeUpIntervalCapabilitiesReportV2Frame.minimumWakeUpIntervalSeconds1 << 16 |
                   pCmd->ZW_WakeUpIntervalCapabilitiesReportV2Frame.minimumWakeUpIntervalSeconds2 << 8 |
                   pCmd->ZW_WakeUpIntervalCapabilitiesReportV2Frame.minimumWakeUpIntervalSeconds3;

    max_interval = pCmd->ZW_WakeUpIntervalCapabilitiesReportV2Frame.maximumWakeUpIntervalSeconds1 << 16 |
                   pCmd->ZW_WakeUpIntervalCapabilitiesReportV2Frame.maximumWakeUpIntervalSeconds2 << 8 |
                   pCmd->ZW_WakeUpIntervalCapabilitiesReportV2Frame.maximumWakeUpIntervalSeconds3;

    step = pCmd->ZW_WakeUpIntervalCapabilitiesReportV2Frame.wakeUpIntervalStepSeconds1 << 16 |
                   pCmd->ZW_WakeUpIntervalCapabilitiesReportV2Frame.wakeUpIntervalStepSeconds2 << 8 |
                   pCmd->ZW_WakeUpIntervalCapabilitiesReportV2Frame.wakeUpIntervalStepSeconds3;

    DBG_PRINTF("min: %d max: %d\n step:%d", min_interval, max_interval, step);
    if ((min_interval < DEFAULT_WAKE_UP_INTERVAL)  && (DEFAULT_WAKE_UP_INTERVAL < max_interval)) {
        e->wakeUp_interval = (DEFAULT_WAKE_UP_INTERVAL - (DEFAULT_WAKE_UP_INTERVAL % step ));
        if (e->wakeUp_interval < min_interval) {
            e->wakeUp_interval = (DEFAULT_WAKE_UP_INTERVAL + (DEFAULT_WAKE_UP_INTERVAL % step ));
        }
    } else if (min_interval > DEFAULT_WAKE_UP_INTERVAL) {
        e->wakeUp_interval = min_interval;
    } else if (max_interval < DEFAULT_WAKE_UP_INTERVAL) {
        e->wakeUp_interval = max_interval;
    }
    DBG_PRINTF("Wakeup Interval set to %d\n", e->wakeUp_interval);
    e->state = STATUS_SET_WAKE_UP_INTERVAL;
  }
  else
  {
    e->state = STATUS_ASSIGN_RETURN_ROUTE;
  }
  rd_node_probe_update(e);
  return 0;
}

static int 
rd_probe_vendor_callback(BYTE txStatus, BYTE rxStatus,
    ZW_APPLICATION_TX_BUFFER *pCmd, WORD cmdLength, void* user)
{
  rd_node_database_entry_t* e = (rd_node_database_entry_t*) user;

  if (txStatus == TRANSMIT_COMPLETE_OK)
  {
    e->manufacturerID = pCmd->ZW_ManufacturerSpecificReportFrame.manufacturerId1
        << 8 | pCmd->ZW_ManufacturerSpecificReportFrame.manufacturerId2;
    e->productID = pCmd->ZW_ManufacturerSpecificReportFrame.productId1 << 8
        | pCmd->ZW_ManufacturerSpecificReportFrame.productId2;
    e->productType = pCmd->ZW_ManufacturerSpecificReportFrame.productTypeId1
        << 8 | pCmd->ZW_ManufacturerSpecificReportFrame.productTypeId2;
  }
  else
  {
    ERR_PRINTF("rd_probe_vendor_callback: manufacturer report received fail\n");
  }
  /* Move to the next state in any case since the failure of manufacturer report
   * is independent of multi channel probing */
  e->state = STATUS_ENUMERATE_ENDPOINTS;
  rd_node_probe_update(e);
  return 0;
}

void find_report_timed_out(void *user)
{
  rd_node_database_entry_t* n = (rd_node_database_entry_t*) user;
  n->state = STATUS_CHECK_WU_CC_VERSION;
  rd_node_probe_update(n);
}

static int 
rd_ep_find_callback(BYTE txStatus, BYTE rxStatus,
    ZW_APPLICATION_TX_BUFFER *pCmd, WORD cmdLength, void* user)
{
  rd_node_database_entry_t* n = (rd_node_database_entry_t*) user;
  uint8_t *cmd = (uint8_t *)pCmd;
  int endpoint_offset =
    offsetof(ZW_MULTI_CHANNEL_END_POINT_FIND_REPORT_1BYTE_V4_FRAME,
             variantgroup1);
  int n_end_points = (cmdLength - endpoint_offset);
  int i = 0;
  int epid;

  ctimer_stop(&find_report_timer);
  if (txStatus == TRANSMIT_COMPLETE_OK
      && n->state == STATUS_FIND_ENDPOINTS)
  {
    if ((uint8_t)cmd[endpoint_offset] == 0) {
        n_end_points = 0;
    }

    DBG_PRINTF("Node id: %d has: %d endpoints in this find report\n", n->nodeid,
               n_end_points);

    for (i = 0; i < n_end_points; i++) {
      epid = ((uint8_t)cmd[i + endpoint_offset]) & 0x7f;
      if (!rd_add_endpoint(n, epid)) {
        n->state = STATUS_PROBE_FAIL;
        rd_node_probe_update(n);
        return 0;
      }
    }
    for ( i = 0 ; i < (n->nAggEndpoints); i++) {
        if (!rd_add_endpoint(n, epid+ i +1)) {
        n->state = STATUS_PROBE_FAIL;
        rd_node_probe_update(n);
        return 0;
      }
 
    }
    
    if (pCmd->ZW_MultiChannelEndPointFindReport1byteV4Frame.reportsToFollow !=
        0) {
      ctimer_set(&find_report_timer, 100, find_report_timed_out, n);
      return 1; // Tell ZW_SendRequest to wait for more reports 
    }
  } else {
    ERR_PRINTF("rd_ep_find_callback: multichannel endpoint find report"
               "receive failed or timedout\n");
  }
  n->state = STATUS_CHECK_WU_CC_VERSION;
  rd_node_probe_update(n);
  return 0;
}

static int 
rd_ep_get_callback(BYTE txStatus, BYTE rxStatus, ZW_APPLICATION_TX_BUFFER *pCmd,
    WORD cmdLength, void* user)
{
  int n_end_points;
  int n_aggregated_endpoints;
  int epid;
  rd_node_database_entry_t* n = (rd_node_database_entry_t*) user;
  rd_ep_database_entry_t* ep;

  if (txStatus == TRANSMIT_COMPLETE_OK
      && n->state == STATUS_ENUMERATE_ENDPOINTS)
  {
    n_end_points = pCmd->ZW_MultiChannelEndPointReportV4Frame.properties2
        & 0x7f;

    identical_endpoints = pCmd->ZW_MultiChannelEndPointReportV4Frame.properties1 & 0x40;
    if(cmdLength >=5) {
      n_aggregated_endpoints = pCmd->ZW_MultiChannelEndPointReportV4Frame.properties3 & 0x7f;
      n_end_points+=n_aggregated_endpoints;
    } else {
      n_aggregated_endpoints = 0;
    }

    /*If the old endpoint count does not match free up all endpoints old endpoints except ep0 and create some new ones */
    if (n->nEndpoints != (n_end_points + 1))
    {
      rd_ep_database_entry_t*  ep0 =list_head(n->endpoints);

      while(ep0->list) {
        ep = ep0->list;
        ep0->list = ep->list;
        rd_store_mem_free_ep(ep);
      }

      n->nAggEndpoints = n_aggregated_endpoints;
      n->nEndpoints = 1; //Endpoint 0 is still there
    }

    DBG_PRINTF("Node id: %d has: %d regular endpoints and :%d aggregated "
               "endpoints in endpoint report frame \n", n->nodeid,
               n_end_points, n->nAggEndpoints);
  }
  else
  {
    ERR_PRINTF("rd_ep_get_callback: multichannel endpoint report received fail\n");
  }
  n->state = STATUS_FIND_ENDPOINTS;
  rd_node_probe_update(n);
  return 0;
}

static int 
rd_probe_wakeup_callback(BYTE txStatus, BYTE rxStatus,
    ZW_APPLICATION_TX_BUFFER *pCmd, WORD cmdLength, void* user)
{
  rd_node_database_entry_t* e = (rd_node_database_entry_t*) user;

  if (txStatus == TRANSMIT_COMPLETE_OK
      && e->state == STATUS_PROBE_WAKE_UP_INTERVAL)
  {
    e->wakeUp_interval = pCmd->ZW_WakeUpIntervalReportFrame.seconds1 << 16
        | pCmd->ZW_WakeUpIntervalReportFrame.seconds2 << 8
        | pCmd->ZW_WakeUpIntervalReportFrame.seconds3 << 0;

    if (pCmd->ZW_WakeUpIntervalReportFrame.nodeid != MyNodeID)
    {
      WRN_PRINTF("WakeUP notifier NOT set to me!\n");
    }
  }
  else
  {
    ERR_PRINTF("rd_probe_wakeup_callback: wake up interval report received fail\n");
  }
  e->state = STATUS_PROBE_ENDPOINTS;
  rd_node_probe_update(e);
  return 0;
}


/*
 * https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetNaive
 * Counting bits set, Brian Kernighan's way
 */
static inline BYTE
bit8_count(BYTE n)
{
  unsigned int c; // c accumulates the total bits set in v
  for (c = 0; n; c++)
    n &= n - 1; // clear the least sigficant bit set
  return c;
}

static BYTE
count_array_bits(BYTE* a, BYTE length)
{
  BYTE c = 0;
  BYTE i;
  for (i = 0; i < length; i++)
    c += bit8_count(a[i]);
  return c;
}


static int 
rd_ep_aggregated_members_get_callback(BYTE txStatus, BYTE rxStatus,
    ZW_APPLICATION_TX_BUFFER *pCmd, WORD cmdLength, void* user)
{
  rd_ep_database_entry_t* ep = (rd_ep_database_entry_t*) user;
  ZW_MULTI_CHANNEL_AGGREGATED_MEMBERS_REPORT_4BYTE_V4_FRAME* f = (ZW_MULTI_CHANNEL_AGGREGATED_MEMBERS_REPORT_4BYTE_V4_FRAME*) pCmd;

  int i,j,n;
  if( ep->state != EP_STATE_PROBE_AGGREGATED_ENDPOINTS) {
    return 0;
  }

  if(txStatus == TRANSMIT_COMPLETE_OK) {
    n = count_array_bits(&f->aggregatedMembersBitMask1,f->numberOfBitMasks);
    if(ep->endpoint_agg) {
      rd_data_mem_free(ep->endpoint_agg);
    }
    ep->endpoint_agg = rd_data_mem_alloc(n);
    ep->endpoint_aggr_len = 0;

    /*Calculate the index of the bits set in the mask*/
    n=0;
    for(i=0; i < f->numberOfBitMasks; i++) {
      uint8_t c = (&f->aggregatedMembersBitMask1)[i];

      j=0;
      while(c) {
        if(c & 1) {
          ep->endpoint_agg[ep->endpoint_aggr_len] = n + j+1;
          ep->endpoint_aggr_len++;
        }
        c = c >> 1;
        j++;
      }
      n=n+8;
    }
  }

  DBG_PRINTF("This is aggregated enpdoint. Not doing any more probing on it\n");
  ep->state = EP_STATE_PROBE_DONE;
  rd_ep_probe_update(ep);
  return 0;
}

static int 
rd_ep_capability_get_callback(BYTE txStatus, BYTE rxStatus,
    ZW_APPLICATION_TX_BUFFER *pCmd, WORD cmdLength, void* user)
{
  rd_ep_database_entry_t* ep = (rd_ep_database_entry_t*) user;
  //DBG_PRINTF(" %i\n",txStatus);
  if (txStatus == TRANSMIT_COMPLETE_OK
      && ep->endpoint_id
          == (pCmd->ZW_MultiChannelCapabilityReport1byteV4Frame.properties1
              & 0x7F))
  {
    if (ep->endpoint_info)
      rd_data_mem_free(ep->endpoint_info);
    ep->endpoint_info_len = 0;

    ep->endpoint_info = rd_data_mem_alloc(cmdLength - 3);
    WRN_PRINTF("Storing %i bytes epid = %i\n", cmdLength - 3, ep->endpoint_id);
    if (ep->endpoint_info)
    {
      memcpy(ep->endpoint_info,
          &(pCmd->ZW_MultiChannelCapabilityReport1byteV4Frame.genericDeviceClass),
          cmdLength - 3);
      ep->endpoint_info_len = cmdLength - 3;
      ep->state = EP_STATE_PROBE_SEC2_C2_INFO;
    }
    else
    {
      ep->state = EP_STATE_PROBE_FAIL;
    }
  }
  else
  {
    ep->state = EP_STATE_PROBE_FAIL;
  }
  rd_ep_probe_update(ep);
  return 0;
}

static int rd_ep_zwave_plus_info_callback(BYTE txStatus, BYTE rxStatus,
    ZW_APPLICATION_TX_BUFFER *pCmd, WORD cmdLength, void* user)
{
  rd_ep_database_entry_t* ep = (rd_ep_database_entry_t*) user;

  if (txStatus == TRANSMIT_COMPLETE_OK)
  {
    if(cmdLength>= sizeof(ZW_ZWAVEPLUS_INFO_REPORT_V2_FRAME)) {
      ep->installer_iconID = pCmd->ZW_ZwaveplusInfoReportV2Frame.installerIconType1 << 8
          | pCmd->ZW_ZwaveplusInfoReportV2Frame.installerIconType2;

      ep->user_iconID = pCmd->ZW_ZwaveplusInfoReportV2Frame.userIconType1 << 8
          | pCmd->ZW_ZwaveplusInfoReportV2Frame.userIconType2;

      if (pCmd->ZW_ZwaveplusInfoReportV2Frame.roleType == ROLE_TYPE_SLAVE_PORTABLE ) {
        ep->node->node_properties_flags |= RD_NODE_FLAG_PORTABLE;
      }
    }
  }
  else
  {
    ERR_PRINTF("rd_ep_zwave_plus_info_callback: zwave plus info report received fail\n");
  }
  ep->state = EP_STATE_MDNS_PROBE;
  rd_ep_probe_update( ep);
  return 0;
}

static void rd_ep_probe_cc_version_callback(void *user, uint8_t status_code) {
  rd_ep_database_entry_t *ep = (rd_ep_database_entry_t*)user;
  if(status_code != 0)
    WRN_PRINTF("Version probing is not completedly done.\n");
  ep->state = EP_STATE_PROBE_ZWAVE_PLUS;
  rd_ep_probe_update(ep);
}

static int
rd_ep_secure_commands_get_callback(BYTE txStatus, BYTE rxStatus,
    ZW_APPLICATION_TX_BUFFER *pCmd, WORD cmdLength, void* user)
{
  uint8_t *p;
  rd_ep_database_entry_t* ep = (rd_ep_database_entry_t*) user;
  uint8_t header_len;
  uint8_t flag;

  if(ep->state == EP_STATE_PROBE_SEC0_INFO) {
    header_len = 3;
  } else {
    header_len = 2;
  }

  switch(ep->state)
  {
  case EP_STATE_PROBE_SEC2_C2_INFO:
    flag = NODE_FLAG_SECURITY2_ACCESS;
    break;
  case EP_STATE_PROBE_SEC2_C1_INFO:
    flag = NODE_FLAG_SECURITY2_AUTHENTICATED;
    break;
  case EP_STATE_PROBE_SEC2_C0_INFO:
    flag = NODE_FLAG_SECURITY2_UNAUTHENTICATED;
    break;
  case EP_STATE_PROBE_SEC0_INFO:
    flag = NODE_FLAG_SECURITY0;
    break;
  default:
    flag = 0;
    ASSERT(0);
    break;
  }

  DBG_PRINTF("Security flags 0x%02x\n", ep->node->security_flags);
  /* If ep is the real node and it was added by someone else and it
   * has not been probed before, we allow probe fail to down-grade the
   * security flags. */
  if ((ep->endpoint_id == 0)
      && !(ep->node->node_properties_flags & RD_NODE_FLAG_ADDED_BY_ME)
      && (ep->node->node_properties_flags & RD_NODE_FLAG_JUST_ADDED)) {
     if (ep->node->security_flags & flag) {
        DBG_PRINTF("Clearing flag 0x%02x\n", flag);
     }
     ep->node->security_flags &= ~flag;
  }

  //LOG_PRINTF("%s",__FUNCTION__);
  if (txStatus == TRANSMIT_COMPLETE_OK)
  {
    if(cmdLength >header_len )
    {
      /*Reallocate the node info to hold the security nif*/
      p = rd_data_mem_alloc(ep->endpoint_info_len + 2 + cmdLength - header_len);
      if (p)
      {
        if (ep->endpoint_info)
        {
          memcpy(p, ep->endpoint_info, ep->endpoint_info_len);
          rd_data_mem_free(ep->endpoint_info);
        }

        ep->endpoint_info = p;
        p += ep->endpoint_info_len;

        /* Insert security command class mark */
        *p++ = (COMMAND_CLASS_SECURITY_SCHEME0_MARK >> 8);
        *p++ = (COMMAND_CLASS_SECURITY_SCHEME0_MARK >> 0) & 0xFF;

        memcpy(p, &((BYTE*)pCmd)[header_len],cmdLength- header_len);
        ep->endpoint_info_len += 2 + cmdLength - header_len;
      }
    }

    if (ep->endpoint_id == 0) {
        DBG_PRINTF("Setting flag 0x%02x\n", flag);
        ep->node->security_flags |= flag;
    }
  }
  if (ep->endpoint_id > 0 || (ep->endpoint_id == 0 && ep->state == EP_STATE_PROBE_SEC0_INFO)) {
    /* Endpoints are only queried about one security class while root device
     * will run through all security probing.
     *
     * GW should try to probe versions after we done the security probing.
     * EP_STATE_PROBE_SEC0_INFO is used as an ending state to indicate the root device
     * has finished security probing and state should be moved to PROBE_VERSION.
     */
    ep->state = EP_STATE_PROBE_VERSION;
  } else {
    /* The node itself is dragged through all the states. */
    ep->state++;
  }
  rd_ep_probe_update(ep);
  return 0;
}

/*Used when a get node info is pending */
static rd_ep_database_entry_t* nif_request_ep = 0;

/** The node currently being probed (pan side or mdns).
 *
 * Used to determine if the probe machine is busy.  I.e., it should
 * not be cleared before all nodes are in one of the terminal states.
 *
 * Used by assign return route, node_info_request_timeout(),
 * rd_endpoint_name_probe_done() to determine which node the callback
 * concerns.  I.e., a node must not be removed (rd_remove_node())
 * while these operations are in progress.
 *
 */
static rd_node_database_entry_t* current_probe_entry = 0;

/*IN  Node id of the node that send node info */
/*IN  Pointer to Application Node information */
/*IN  Node info length                        */
void rd_nif_request_notify(uint8_t bStatus, nodeid_t bNodeID, uint8_t* nif, uint8_t nif_len)
{
  rd_ep_database_entry_t* ep = nif_request_ep;
  if (ep && ep->state == EP_STATE_PROBE_INFO)
  {
    nif_request_ep = 0;
    ctimer_stop(&nif_request_timer);

    ASSERT(ep->node);
    if (bStatus && ep->node->nodeid == bNodeID)
    {
      ep->node->nodeType = nif[0];
      if (ep->endpoint_info)
        rd_data_mem_free(ep->endpoint_info);
      ep->endpoint_info = rd_data_mem_alloc(nif_len+1);
      if (ep->endpoint_info)
      {
        memcpy(ep->endpoint_info, nif + 1, nif_len - 1);

        ep->endpoint_info[nif_len-1] = COMMAND_CLASS_ZIP_NAMING;
        ep->endpoint_info[nif_len  ] = COMMAND_CLASS_ZIP;

        ep->endpoint_info_len = nif_len+1;

        /* If node is just added and GW is inclusion controller, we
         * are still trying to determine security classes. */
        if ((ep->endpoint_id == 0)
            && !(ep->node->node_properties_flags & RD_NODE_FLAG_ADDED_BY_ME)
            && (ep->node->node_properties_flags & RD_NODE_FLAG_JUST_ADDED)) {
           /* Downgrade S2 flags if node does not support it. */
           if (!rd_ep_supports_cmd_class_nonsec(ep, COMMAND_CLASS_SECURITY_2)) {
              ep->node->security_flags &= ~NODE_FLAGS_SECURITY2;
           }
           /* Downgrade S0 if node does not support it. */
           if (!rd_ep_supports_cmd_class_nonsec(ep, COMMAND_CLASS_SECURITY)) {
              ep->node->security_flags &= ~NODE_FLAG_SECURITY0;
           }
        }
        if(ep->endpoint_id == 0) {
          /* The version knowledge GW has is related to which set of command class
           * the node supports. We need to reset the version here together with
           * endpoint_info.
           */
          rd_node_cc_versions_set_default(ep->node);
          ep->node->node_version_cap_and_zwave_sw = 0;
          ep->node->probe_flags = RD_NODE_PROBE_NEVER_STARTED;
          ep->node->node_is_zws_probed = 0;
        }

        ep->state++;
      }
      else
      {
        ep->state = EP_STATE_PROBE_FAIL;
      }
    }
    else
    {
      ep->state = EP_STATE_PROBE_FAIL;
    }
    rd_ep_probe_update(ep);
  }
}

void
AssignReturnRouteCallback(uint8_t status)
{
  if (current_probe_entry)
  {
    if (status != TRANSMIT_COMPLETE_OK)
    {
      ERR_PRINTF("AssignReturnRouteCallback: assign return route fail\n");
    }
    current_probe_entry->state = STATUS_PROBE_WAKE_UP_INTERVAL;
    rd_node_probe_update(current_probe_entry);
  }
  else
  {
    ASSERT(0);
  }
}

static void
node_info_request_timeout(void* d)
{
  if(!current_probe_entry) return;

  rd_ep_database_entry_t* ep = (rd_ep_database_entry_t*) d;

  ep->state = EP_STATE_PROBE_FAIL;
  nif_request_ep = 0;
  rd_ep_probe_update(ep);
}

static void
rd_endpoint_name_probe_done(int bStatus, void* ctx)
{
  char buf[64];
  int timer_value = 0;
  rd_ep_database_entry_t *ep = (rd_ep_database_entry_t*) ctx;
  u8_t k;
  ASSERT(ep);
  if(!current_probe_entry) return;
  if(!ep) return;

  if (ep->state == EP_STATE_MDNS_PROBE_IN_PROGRESS)
  {
    if (bStatus)
    {
      ep->state = EP_STATE_PROBE_DONE;
    }
    else
    {
      /* If this is the default node name, We need to allocate memory for this name*/
      if (ep->endpoint_name_len == 0)
      {
        ep->endpoint_name_len = rd_get_ep_name(ep, buf, sizeof(buf));
      } else {
        /*Locate trailing digits in the name */
        memcpy(buf, ep->endpoint_name, ep->endpoint_name_len);
        rd_data_mem_free(ep->endpoint_name);
      }
      k = add_or_increment_trailing_digit(buf, ep->endpoint_name_len,
          sizeof(buf));
      ep->endpoint_name = rd_data_mem_alloc(k);
      ep->endpoint_name_len = k;
      memcpy(ep->endpoint_name, buf, k);
      timer_value = 250 + (750 * (denied_conflict_probes > 10));
    }

    if (timer_value) {
        ep->state = EP_STATE_MDNS_PROBE;
        ctimer_set(&ep_probe_update_timer, timer_value, (void (*)(void *)) rd_ep_probe_update, ep);
        timer_value = 0;
        denied_conflict_probes++;
    } else {
        rd_ep_probe_update(ep);
    }
  }
}

static int should_probe_level(rd_ep_database_entry_t* ep, uint8_t level)
{
   /* If this gateway added the node and If the root node does not support this security class. Do not probe it. */
   /* Inclusion controllers should still probe this because they do not know which keys are granted */
   if ((ep->endpoint_id == 0) &&                                          //root endpoint
      (0==(ep->node->security_flags & level)) &&                         //security level granted
      (ep->node->node_properties_flags & RD_NODE_FLAG_ADDED_BY_ME)) {  //and node added by me))
     DBG_PRINTF("This gateway (nodeid: %d) added this node (nodeid: %d) with %02x security keys greanted but is not "
                "granted this level security key (%02x). This gateway wont probe S2 at this level\n",
                MyNodeID, ep->node->nodeid, ep->node->security_flags, level);
     return FALSE;
   }
   return TRUE;
}
 
void
rd_ep_probe_update(rd_ep_database_entry_t* ep)
{
  const uint8_t secure_commands_supported_get[] = {COMMAND_CLASS_SECURITY,SECURITY_COMMANDS_SUPPORTED_GET};
  const uint8_t secure_commands_supported_get2[] = {COMMAND_CLASS_SECURITY_2,SECURITY_2_COMMANDS_SUPPORTED_GET};
  ts_param_t p;

  ASSERT(current_probe_entry);
  if(!current_probe_entry) return;

  static ZW_MULTI_CHANNEL_CAPABILITY_GET_V2_FRAME cap_get_frame =
    { COMMAND_CLASS_MULTI_CHANNEL_V2, MULTI_CHANNEL_CAPABILITY_GET_V2, 0 };

  if(ep->node == 0) {
    return;
  }
  DBG_PRINTF("EP probe nd=%i (flags 0x%02x) ep =%d state=%s\n",
             ep->node->nodeid, ep->node->security_flags,
             ep->endpoint_id, ep_state_name(ep->state));

  if(is_virtual_node(ep->node->nodeid)) {
    rd_remove_node(ep->node->nodeid);
    ASSERT(0);
    return;
  }

  ts_set_std(&p, ep->node->nodeid);
  p.dendpoint = ep->endpoint_id;

  switch (ep->state)
  {
    case EP_STATE_PROBE_AGGREGATED_ENDPOINTS:
      ts_set_std(&p, ep->node->nodeid);
      ZW_MULTI_CHANNEL_AGGREGATED_MEMBERS_GET_V4_FRAME f;
      f.cmdClass = COMMAND_CLASS_MULTI_CHANNEL_V4;
      f.cmd = MULTI_CHANNEL_AGGREGATED_MEMBERS_GET_V4;
      f.properties1 = ep->endpoint_id;

      if (!ZW_SendRequest(&p, (BYTE*) &f, sizeof(ZW_MULTI_CHANNEL_AGGREGATED_MEMBERS_GET_V4_FRAME),
          MULTI_CHANNEL_AGGREGATED_MEMBERS_REPORT_V4, REQUEST_TIMEOUT, ep,
          rd_ep_aggregated_members_get_callback))
      {
        goto fail_state;
      }
      break;
    case EP_STATE_PROBE_INFO:
      /*The Device Capabilities of Endpoint 1 must respond the normal NIF*/
      if (ep->endpoint_id > 0)
      {
        // Check if this endpoint is aggregated, we need to locate its offset in
        // the list. Ie:
        // nEndpoints = 4,  nAggr = 1
        // root idx = 1
        // ep1  idx = 2
        // ep4  idx = 3
        // ep5  idx = 4 (aggr)
        int ep_idx = 0;
        for(rd_ep_database_entry_t* s = list_head(ep->node->endpoints); s; s = list_item_next(s)) {
          ep_idx++;
          if(s == ep) break;
        }

        ASSERT(ep_idx <= ep->node->nEndpoints );
        // If the endpoint index is in the aggregated range we need to do and
        // AGGREGATED_MEMBERS_GET otherwis we go to security probing.
        int last_regular_ep_idx = ep->node->nEndpoints - ep->node->nAggEndpoints;
        if(ep_idx > last_regular_ep_idx) {
          DBG_PRINTF("Probing aggregated endpoint: %d", ep_idx);
          ep->state = EP_STATE_PROBE_AGGREGATED_ENDPOINTS;
          rd_ep_probe_update(ep);
          return;
        } else {
          cap_get_frame.properties1 = ep->endpoint_id & 0x7F;

          ts_param_t p;
          ts_set_std(&p, ep->node->nodeid);
          if (!ZW_SendRequest(&p, (BYTE*) &cap_get_frame, sizeof(cap_get_frame),
              MULTI_CHANNEL_CAPABILITY_REPORT_V2, REQUEST_TIMEOUT, ep,
              rd_ep_capability_get_callback))
          {
            goto fail_state;
          }
        }
      }
      else
      {
        /*ERR_PRINTF("Request node info %d\n",ep->node->nodeid);*/

        /*The routers own NIF */
        if (ep->node->nodeid == MyNodeID)
        {
          if (ep->endpoint_info)
            rd_data_mem_free(ep->endpoint_info);
          ep->endpoint_info = rd_data_mem_alloc(
              IPNIFLen - 4 + 2 + IPnSecureClasses);
          ep->user_iconID = ICON_TYPE_GENERIC_GATEWAY;
          ep->installer_iconID = ICON_TYPE_GENERIC_GATEWAY;
          if (ep->endpoint_info)
          {

            memcpy(ep->endpoint_info, IPNIF + 4, IPNIFLen - 4);
            ep->endpoint_info_len = IPNIFLen - 4;
            /*Add the command class mark -- No matter if we are secure or not */
            ep->endpoint_info[ep->endpoint_info_len] =
                (COMMAND_CLASS_SECURITY_SCHEME0_MARK >> 8) & 0xFF;
            ep->endpoint_info[ep->endpoint_info_len + 1] =
                (COMMAND_CLASS_SECURITY_SCHEME0_MARK >> 0) & 0xFF;
            ep->endpoint_info_len += 2;

            memcpy(ep->endpoint_info + ep->endpoint_info_len, IPSecureClasses,
                IPnSecureClasses);

            ep->endpoint_info_len += IPnSecureClasses;
            ep->state =  EP_STATE_MDNS_PROBE;
            rd_ep_probe_update(ep);
            return;
          }
          else
          {
            ERR_PRINTF("Nodeinfo fail(no memory)\n");
            goto fail_state;
          }
        }
        else
        { /* ep->node->nodeid == MyNodeID */
          //ASSERT(nif_request_ep == 0);
          /*This might just be a session resume */
          if (nif_request_ep)
          {
            return;
          }

          nif_request_ep = ep;
          /*Mask out all security flags as we will find them durring the probe */
          /* TODO : we set all the NODE_FLAGS_SECURITY in network management flags but then mask it out again here ? */
          /* Only increase security */
          /* ep->node->security_flags &= ~NODE_FLAGS_SECURITY; */

          if (ZW_RequestNodeInfo(ep->node->nodeid, 0))
          {
            ctimer_set(&nif_request_timer, 65000, node_info_request_timeout, ep);
          }
          else
          {
            nif_request_ep = 0;
            ERR_PRINTF("Nodeinfo fail\n");
            goto fail_state;
            return;
          }
        }
      }
      break;
    case EP_STATE_PROBE_SEC2_C2_INFO:
      /*Dont probe security at all */
      if (ep->node->security_flags & NODE_FLAG_KNOWN_BAD)
      {
        ep->state = EP_STATE_PROBE_VERSION;
        rd_ep_probe_update(ep);
        return;
      }

      /* Don't probe more S2 */
      if (!SupportsCmdClass(ep->node->nodeid, COMMAND_CLASS_SECURITY_2))
      {
        DBG_PRINTF("Node (%d) does not support COMMAND_CLASS_SECURITY_2, will not probe S2 for it", ep->node->nodeid);
        ep->state = EP_STATE_PROBE_SEC0_INFO - 1;
        goto next_state;
      }

      if (!should_probe_level(ep, NODE_FLAG_SECURITY2_ACCESS)) {
        goto next_state;
      }

      /*If this is not the root NIF just use the auto scheme */
      if( ep->endpoint_id >0 )
      {
        p.scheme = AUTO_SCHEME;
        if (((GetCacheEntryFlag(ep->node->nodeid) & NODE_FLAGS_SECURITY2))
            && ((GetCacheEntryFlag(MyNodeID) & NODE_FLAGS_SECURITY2))) {
          /* S2 is supported and working on both node and GW.
           * Therefore ep must be S2. */
          if (!ZW_SendRequest(&p, secure_commands_supported_get2, sizeof(secure_commands_supported_get2),
                  SECURITY_2_COMMANDS_SUPPORTED_REPORT, 20, ep, rd_ep_secure_commands_get_callback))
          {
            goto fail_state;
          }
        } else {
          /* Node is not included with S2 or GW is not running S2. */
          /* If the ep or the GW do not support S0, either, we let the
           * PROBE_SEC0 state sort it out. */
          ep->state = EP_STATE_PROBE_SEC0_INFO - 1;
          goto next_state;
        }
      } else {
        /* Root device */
        if(GetCacheEntryFlag(MyNodeID) & NODE_FLAG_SECURITY2_ACCESS)
        {
          p.scheme = SECURITY_SCHEME_2_ACCESS;
          if (!ZW_SendRequest(&p, secure_commands_supported_get2, sizeof(secure_commands_supported_get2),
          SECURITY_2_COMMANDS_SUPPORTED_REPORT, 20, ep, rd_ep_secure_commands_get_callback))
          {
            goto fail_state;
          }
        }
        else
        {
          goto next_state;
        }
      }
      break;

    case EP_STATE_PROBE_SEC2_C1_INFO:
      if (!should_probe_level(ep, NODE_FLAG_SECURITY2_AUTHENTICATED)) {
        goto next_state;
      }


      /*If this is not the root NIF and the root does not support this class, move on*/
      if((ep->endpoint_id >0) && (0==(GetCacheEntryFlag(ep->node->nodeid) & NODE_FLAG_SECURITY2_AUTHENTICATED))) {
        goto next_state;
      }

      if (GetCacheEntryFlag(MyNodeID) & NODE_FLAG_SECURITY2_AUTHENTICATED)
      {
        p.scheme = SECURITY_SCHEME_2_AUTHENTICATED;

        if (!ZW_SendRequest(&p, secure_commands_supported_get2, sizeof(secure_commands_supported_get2),
        SECURITY_2_COMMANDS_SUPPORTED_REPORT, 20, ep, rd_ep_secure_commands_get_callback))
        {
          goto fail_state;
        }
      }
      else
      {
        goto next_state;
      }

      break;
    case EP_STATE_PROBE_SEC2_C0_INFO:

      if (!should_probe_level(ep, NODE_FLAG_SECURITY2_UNAUTHENTICATED)) {
          goto next_state;
      }

      /*If this is not the root NIF and the root does not support this class, move on*/
      if((ep->endpoint_id >0) && (0==(GetCacheEntryFlag(ep->node->nodeid) & NODE_FLAG_SECURITY2_UNAUTHENTICATED))) {
        goto next_state;
      }

      if (GetCacheEntryFlag(MyNodeID) & NODE_FLAG_SECURITY2_UNAUTHENTICATED)
      {
        p.scheme = SECURITY_SCHEME_2_UNAUTHENTICATED;

        if (!ZW_SendRequest(&p, secure_commands_supported_get2, sizeof(secure_commands_supported_get2),
        SECURITY_2_COMMANDS_SUPPORTED_REPORT, 20, ep, rd_ep_secure_commands_get_callback))
        {
          goto fail_state;
        }
      }
      else
      {
        goto next_state;
      }

      break;
    case EP_STATE_PROBE_SEC0_INFO:
      /* Do not probe S0 if it is not supported by the node. */
      /* is_lr_node() is workaround for LR nodes incorrectly advertizing S0 in the NIF */
      if (!SupportsCmdClass(ep->node->nodeid, COMMAND_CLASS_SECURITY) || is_lr_node(ep->node->nodeid))
      {
        DBG_PRINTF("Node (%d) does not support COMMAND_CLASS_SECURITY, will not probe S0 for it", ep->node->nodeid);
        ep->state = EP_STATE_PROBE_VERSION;
        rd_ep_probe_update(ep);
        return;
     }

      /* Do not probe S0 on a real endpoint if the root device is not
       * using it.  */
      if ((ep->endpoint_id > 0) &&
          !(GetCacheEntryFlag(ep->node->nodeid) & NODE_FLAG_SECURITY0))
      {
        ep->state = EP_STATE_PROBE_VERSION;
        rd_ep_probe_update(ep);
        return;
      }

      /* Only probe S0 if the GW is using it. */
      if ((GetCacheEntryFlag(MyNodeID) & NODE_FLAG_SECURITY0))
      {
        p.scheme = SECURITY_SCHEME_0;

        if (!ZW_SendRequest(&p, secure_commands_supported_get, sizeof(secure_commands_supported_get),
        SECURITY_COMMANDS_SUPPORTED_REPORT, 3*20, ep, rd_ep_secure_commands_get_callback))
        {
          goto fail_state;
        }
      }
      else
      {
        ep->state = EP_STATE_PROBE_VERSION;
        rd_ep_probe_update(ep);
        return;
      }

      break;
    case EP_STATE_PROBE_VERSION:
      rd_ep_probe_cc_version(ep, rd_ep_probe_cc_version_callback);
      return;
      break;
    case EP_STATE_PROBE_ZWAVE_PLUS:
      ep->installer_iconID = 0;
      ep->user_iconID = 0;
      if ((rd_ep_class_support(ep, COMMAND_CLASS_ZWAVEPLUS_INFO) & SUPPORTED))
      {
        const uint8_t zwave_plus_info_get[] ={ COMMAND_CLASS_ZWAVEPLUS_INFO, ZWAVEPLUS_INFO_GET};
        p.scheme = AUTO_SCHEME;
        if (!ZW_SendRequest(&p, zwave_plus_info_get,
            sizeof(zwave_plus_info_get), ZWAVEPLUS_INFO_REPORT,
            REQUEST_TIMEOUT, ep, rd_ep_zwave_plus_info_callback))
        {
          goto fail_state;
        }
      } else {
        goto next_state;
      }
      break;
    case EP_STATE_MDNS_PROBE:
      ep->state = EP_STATE_MDNS_PROBE_IN_PROGRESS;
      if (!mdns_endpoint_name_probe(ep, rd_endpoint_name_probe_done, ep))
      {
        goto next_state;
      }
      break;
    case EP_STATE_MDNS_PROBE_IN_PROGRESS:
      break;
    case EP_STATE_PROBE_FAIL:
    case EP_STATE_PROBE_DONE:
      rd_node_probe_update(ep->node);
      break;
  }
  return;

  next_state: ep->state++;
  rd_ep_probe_update(ep);
  return;

  fail_state: ep->state = EP_STATE_PROBE_FAIL;
  rd_ep_probe_update(ep);
  return;
}

void
rd_set_wu_interval_callback(BYTE txStatus, void* user, TX_STATUS_TYPE *t)
{
  rd_node_database_entry_t* n = (rd_node_database_entry_t*) user;

  if (txStatus != TRANSMIT_COMPLETE_OK)
  {
    WRN_PRINTF("rd_set_wu_interval_callback: set wake up interval fail\n");
  }
  n->state = STATUS_ASSIGN_RETURN_ROUTE;
  rd_node_probe_update(n);
}

/**
 * Read the protocol info from NVM
 */
static void
update_protocol_info(rd_node_database_entry_t* n)
{
  NODEINFO ni;

  ZW_GetNodeProtocolInfo(n->nodeid, &ni);
  n->nodeType = ni.nodeType.basic;

  if (ni.capability & NODEINFO_LISTENING_SUPPORT)
  {
    n->mode = MODE_ALWAYSLISTENING;
    //n->wakeUp_interval = 70*60; //70 Minutes, the node will be probed once each 70 minutes.
  }
  else if (ni.security
      & (NODEINFO_ZWAVE_SENSOR_MODE_WAKEUP_1000
          | NODEINFO_ZWAVE_SENSOR_MODE_WAKEUP_250))
  {
    n->mode = MODE_FREQUENTLYLISTENING;
  }
  else
  {
    n->mode = MODE_NONLISTENING;
  }
}

void send_suc_id(uint8_t);
struct ctimer send_suc_id_timer;
void send_suc_id_cb(BYTE txStatus, TX_STATUS_TYPE *t)
{
    DBG_PRINTF("send_suc_id_cb\n");
    ctimer_stop(&send_suc_id_timer);
    send_suc_id(txStatus);
}

void send_suc_id_slave_cb(BYTE STATUS)
{
    DBG_PRINTF("send_suc_id_slave_cb\n");
    /* Stop the timer or GW ends up false calling send_suc_id() in timeout */
    ctimer_stop(&send_suc_id_timer);
    send_suc_id(STATUS);
}

void send_suc_id_timeout(void *use)
{
    DBG_PRINTF("Timed out waiting for callback in ZW_SendSUCID or "
               "ZW_AssignSUCReturnRoute\n");
    send_suc_id(TRANSMIT_COMPLETE_FAIL);
}

void send_suc_id(uint8_t status)
{
    rd_node_database_entry_t* nd;
    static nodeid_t current_send_suc_id_dest = 0;

    if (status!= TRANSMIT_COMPLETE_OK) {
      ERR_PRINTF("ERROR: ZW_SendSUCID or ZW_AssignSUCReturnRoute to node id: %d failed\n", current_send_suc_id_dest);
    }
    while (current_send_suc_id_dest <= ZW_MAX_NODES)
    {
       current_send_suc_id_dest++;
       if (current_send_suc_id_dest == MyNodeID)
         continue;

       nd = rd_node_get_raw(current_send_suc_id_dest);
       if (!nd)
         continue;

       if (is_virtual_node(nd->nodeid))
         continue;

       DBG_PRINTF("Sending SUI ID to node id: %d\n", nd->nodeid); 
       /* To cover the case of missing callback from either of following calls
        * ZW_SendSUCID() or ZW_AssignSUCReturnRoute()
        */
       ctimer_set(&send_suc_id_timer, 1 * CLOCK_SECOND, send_suc_id_timeout, 0);
       if(isNodeController(nd->nodeid)) {
           uint8_t txOption = TRANSMIT_OPTION_ACK | TRANSMIT_OPTION_AUTO_ROUTE;
           if(!ZW_SendSUCID(nd->nodeid, txOption, send_suc_id_cb)) {
               ERR_PRINTF("Call to ZW_SendSUCID failed\n");
               send_suc_id_cb(TRANSMIT_COMPLETE_FAIL, 0);
           }
           goto exit;
       } else {
           ZW_AssignSUCReturnRoute(nd->nodeid, send_suc_id_slave_cb);
           goto exit;
       }
    }
    /* Post the event only when we are done with all nodes */
    process_post(&zip_process, ZIP_EVENT_ALL_NODES_PROBED, 0);
exit:
    return;
}

void rd_probe_resume()
{
  nodeid_t i;
  rd_node_database_entry_t* nd;

  if (current_probe_entry)
  {
     DBG_PRINTF("Resume probe of %u\n", current_probe_entry->nodeid);
    rd_node_probe_update(current_probe_entry);
    return;
  }

  for (i = 1; i <= ZW_MAX_NODES; i++)
  {
    nd = rd_node_get_raw(i);
    if (nd
        && (nd->state != STATUS_DONE && nd->state != STATUS_PROBE_FAIL
            && nd->state != STATUS_FAILING))
    {
      current_probe_entry = nd;
      rd_node_probe_update(nd);
      break;
    }
  }

  if ((i == (ZW_MAX_NODES+1)) && (probe_lock==0))
  {
    rd_node_database_entry_t* nd;

    if (suc_changed) {
      suc_changed = 0;
      DBG_PRINTF("Suc changed, Sending new SUC Id to network \n");
      send_suc_id(TRANSMIT_COMPLETE_OK);
    } else {
      process_post(&zip_process, ZIP_EVENT_ALL_NODES_PROBED, 0);
    }
  }
}

static void
rd_node_name_probe_done(int bStatus, void* ctx)
{
  char buf[255];
  rd_node_database_entry_t *n = (rd_node_database_entry_t*) ctx;
  u8_t k;
  ASSERT(n);
  int timer_value = 0;

  DBG_PRINTF("rd_node_name_probe_done status: %d\n", bStatus);
  if (n->state == STATUS_MDNS_PROBE)
  {
    if (bStatus)
    {
      n->state= STATUS_MDNS_EP_PROBE;
    }
    else
    {
      if (tie_braking_won == 1) { /* Need to probe with same name */
        DBG_PRINTF("rd_node_name_probe_done tie_braking_won\n");
        timer_value = 250;
        tie_braking_won = 0;
        goto exit;
      }
      if (tie_braking_won == 2) {
        DBG_PRINTF("rd_node_name_probe_done tie_braking_won = 2\n");
        timer_value = 1000;
        tie_braking_won = 0;
        goto exit;
      }

      /* If this is the default node name, We need to allocate memory for this name*/
      if (n->nodeNameLen == 0)
      {
        n->nodeNameLen = snprintf(buf, sizeof(buf), "zw%08X%04X-1", uip_htonl(homeID),
            n->nodeid);
        n->nodename = rd_data_mem_alloc(n->nodeNameLen);
        memcpy(n->nodename, buf, n->nodeNameLen);
      }
      else
      { /**/
        /*Locate trailing digits in the name */
        memcpy(buf, n->nodename, n->nodeNameLen);
        buf[n->nodeNameLen] = 0;
        rd_data_mem_free(n->nodename);
        k = add_or_increment_trailing_digit(buf, n->nodeNameLen, sizeof(buf));
        n->nodeNameLen = k;
        n->nodename = rd_data_mem_alloc(k);
        memcpy(n->nodename, buf, k);
      }
      timer_value = 250 + (750 * (denied_conflict_probes > 10));
      DBG_PRINTF("Delaying rd_node_probe_update by %d ms\n", timer_value);
    }
exit:
    if (timer_value) {
     ctimer_set(&rd_node_probe_update_timer, timer_value, (void (*)(void *)) rd_node_probe_update, n);
     timer_value = 0;
     denied_conflict_probes++;
    } else {
     rd_node_probe_update(n);
    }
  }
}

/**
 *  Lock/Unlock the node probe machine. When the node probe lock is enabled, all probing will stop.
 *  Probing is resumed when the lock is disabled. The probe lock is used during a add node process or during learn mode.
 */
void rd_probe_lock(uint8_t enable)
{
  probe_lock = enable;
  DBG_PRINTF("Probe machine is %slocked\n", (enable) ? "" : "un");

  if (!probe_lock)
  {
    rd_probe_resume();
  }
}

/**
 * Unlock the probe machine but do not resume probe engine.
 *
 * If probe was locked during NM add node, but the node should not be
 * probed because it is a self-destructing smart start node, this
 * function resets the probe lock.
 *
 * When removal of the node succeeds, \ref current_probe_entry will be
 * reset when the node is deleted.  We also clear \ref
 * current_probe_entry here so that this function can be used in the
 * "removal failed" scenarios.
 */
void rd_probe_cancel(void) {
   probe_lock = FALSE;
   current_probe_entry = NULL;
}

u8_t
rd_probe_in_progress()
{
  return (current_probe_entry != 0);
}

u8_t rd_node_in_probe(nodeid_t node)
{
  rd_node_database_entry_t* nd;
  nd = rd_node_get_raw(node);
  if (nd
      && (nd->state != STATUS_DONE && nd->state != STATUS_PROBE_FAIL
          && nd->state != STATUS_FAILING))
  {
    return 1;
  }
  return 0;
 
}

/* identical bit guarantees that ALL endpoints are identical. So it is safe to 
 * copy endpoint 1 to all other endpoints
*/
void copy_endpoints(rd_ep_database_entry_t *src_ep, rd_node_database_entry_t *n)
{
  rd_ep_database_entry_t *dest_ep;
  for (dest_ep = list_head(n->endpoints); dest_ep != NULL; dest_ep = list_item_next(dest_ep)) {
    if (dest_ep->endpoint_id > 1) { //only copy to endpoint id 2 and up
      dest_ep->endpoint_location = NULL;

      dest_ep->endpoint_name= NULL;

      dest_ep->endpoint_info = rd_data_mem_alloc(src_ep->endpoint_info_len);
      memcpy(dest_ep->endpoint_info, src_ep->endpoint_info, src_ep->endpoint_info_len);

      dest_ep->endpoint_agg = rd_data_mem_alloc(src_ep->endpoint_aggr_len); 
      memcpy(dest_ep->endpoint_agg, src_ep->endpoint_agg, src_ep->endpoint_aggr_len);

       /** Length of #endpoint_info. */
      dest_ep->endpoint_info_len = src_ep->endpoint_info_len;
       /** Length of #endpoint_name. */
      dest_ep->endpoint_name_len = 0;
       /** Length of #endpoint_location. */
      dest_ep->endpoint_loc_len = 0;
       /** Length of aggregations */
      dest_ep->endpoint_aggr_len = src_ep->endpoint_aggr_len;
      dest_ep->state = src_ep->state;
       /** Z-Wave plus icon ID. */
      dest_ep->installer_iconID = src_ep->installer_iconID;
       /** Z-Wave plus icon ID. */
      dest_ep->user_iconID = src_ep->user_iconID;
    }
  }
}
 
void
rd_node_probe_update(rd_node_database_entry_t* n)
{
  rd_ep_database_entry_t* ep;
  ts_param_t p;

  static const ZW_MANUFACTURER_SPECIFIC_GET_FRAME man_spec_get =
    { COMMAND_CLASS_MANUFACTURER_SPECIFIC, MANUFACTURER_SPECIFIC_GET };
  static const ZW_MULTI_CHANNEL_END_POINT_GET_V4_FRAME multi_ep_get =
    { COMMAND_CLASS_MULTI_CHANNEL_V4, MULTI_CHANNEL_END_POINT_GET_V4 };

  /* Query for all endpoints of node by setting the fields as follow in
   * MULTI_CHANNEL_END_POINT_FIND frame
   *  Generic Device class = 0xff;
   *  Specific Device class = 0xff;
   */
  static const ZW_MULTI_CHANNEL_END_POINT_FIND_V4_FRAME multi_ep_find =
    { COMMAND_CLASS_MULTI_CHANNEL_V4, MULTI_CHANNEL_END_POINT_FIND_V4,
      0xFF, 0xFF};
  static const ZW_WAKE_UP_INTERVAL_GET_FRAME wakeup_get =
    { COMMAND_CLASS_WAKE_UP, WAKE_UP_INTERVAL_GET };

  if (probe_lock)
  {
    DBG_PRINTF("probe machine is locked\n");
    return;
  }

  if(bridge_state == booting) {
    DBG_PRINTF("Waiting for bridge\n");
    return;
  }

  if (!(process_is_running(&mDNS_server_process))) {
    DBG_PRINTF("Waiting for mDNS server process\n");
    return;
  }

  if(n->nodeid==0) {
    ASSERT(0);
    return;
  }

  if(is_virtual_node(n->nodeid)) {
    rd_remove_node(n->nodeid);
    ASSERT(0);
    return;
  }
  DBG_PRINTF("rd_node_probe_update state %s node =%d\n", rd_node_probe_state_name(n->state), n->nodeid);
  switch (n->state)
  {
    /*case STATUS_ADDING:
     update_protocol_info(n); //We do this here because the security layer needs to know if this is node is a controller
     break;*/
    case STATUS_CREATED:
      if (current_probe_entry && current_probe_entry != n)
      {
        DBG_PRINTF("another probe is in progress waiting\n");
        return;
      }
      current_probe_entry = n;
      n->probe_flags = RD_NODE_FLAG_PROBE_STARTED;
      goto next_state;
      break;
    case STATUS_PROBE_NODE_INFO:
    {
      rd_ep_database_entry_t* ep = list_head(n->endpoints);

      if (!ep)
      { // Abort the probe the node might have been removed
         DBG_PRINTF("Abort probe\n");
        current_probe_entry = NULL;
        rd_probe_resume();
        return;
      }

      ASSERT(ep->node == n);

      if (ep->state == EP_STATE_PROBE_FAIL)
      {
        DBG_PRINTF("Endpoint probe fail\n");
        n->state = STATUS_PROBE_FAIL;
        rd_node_probe_update(n);
        return;
      }
      else if (ep->state != EP_STATE_PROBE_DONE)
      {
        rd_ep_probe_update(ep);
      }
      else
      {
        if (n->security_flags & NODE_FLAG_INFO_ONLY)
        {
          n->state = STATUS_DONE;
          rd_node_probe_update(n);
        }
        else
        {
          goto next_state;
        }
      }

    }
      break;
    case STATUS_PROBE_PRODUCT_ID:

      if(n->nodeid == MyNodeID) {
        n->productID = cfg.product_id;
        n->manufacturerID = cfg.manufacturer_id;
        n->productType = cfg.product_type;

        goto next_state;
      }



      if ((SupportsCmdClassFlags(n->nodeid, COMMAND_CLASS_MANUFACTURER_SPECIFIC) & SUPPORTED)
          == 0)
      {
        goto next_state;
      }
      ts_set_std(&p, n->nodeid);
      /**
       * Workaround for Schlage door locks with faulty security implementation.
       */
      p.scheme = SupportsCmdClassSecure(n->nodeid, COMMAND_CLASS_MANUFACTURER_SPECIFIC) ? AUTO_SCHEME : NO_SCHEME;
      /* Find manufacturer info */
      if (!ZW_SendRequest(&p, (BYTE*) &man_spec_get, sizeof(man_spec_get),
          MANUFACTURER_SPECIFIC_REPORT, REQUEST_TIMEOUT, n, rd_probe_vendor_callback))
      {
        goto fail_state;
      }

      break;
    case STATUS_ENUMERATE_ENDPOINTS:
      if (n->nodeid == MyNodeID
          || ((SupportsCmdClassFlags(n->nodeid, COMMAND_CLASS_MULTI_CHANNEL_V4) & SUPPORTED)
              == 0))
      {
        goto next_state;
      }

      ts_set_std(&p, n->nodeid);
      /* Query the number of endpoints */
      if (!ZW_SendRequest(&p, (BYTE*) &multi_ep_get, sizeof(multi_ep_get),
          MULTI_CHANNEL_END_POINT_REPORT_V4, 3*20, n, rd_ep_get_callback))
        goto fail_state;
      break;
    case STATUS_FIND_ENDPOINTS:
      if (n->nodeid == MyNodeID
          || ((SupportsCmdClassFlags(n->nodeid, COMMAND_CLASS_MULTI_CHANNEL_V4) & SUPPORTED)
              == 0))
      {
        goto next_state;
      }

      ts_set_std(&p, n->nodeid);

      /* Query the endpoint number of endpoints */
      if (!ZW_SendRequest(&p, (BYTE*) &multi_ep_find, sizeof(multi_ep_find),
          MULTI_CHANNEL_END_POINT_FIND_REPORT_V4, 3*20, n, rd_ep_find_callback))
        goto fail_state;
      break;

    case STATUS_PROBE_ENDPOINTS:
    {
      for (ep = list_head(n->endpoints); ep != NULL; ep = list_item_next(ep))
      {
        /*if(ep->endpoint_id==1) continue;*/

        if (ep->state == EP_STATE_PROBE_FAIL)
        {
          n->state = STATUS_PROBE_FAIL;
          rd_node_probe_update(n);
          return;
        }
        else if (ep->state != EP_STATE_PROBE_DONE)
        {
          rd_ep_probe_update(ep);
          return;
        }
        if (identical_endpoints && (ep->node->nodeid != MyNodeID) && (ep->endpoint_id > 0)) {
            DBG_PRINTF("Endpoints are identical. Not probing more endpoints\n");
            copy_endpoints(ep, n); // We still need to fill ep structure for all endpoints
            identical_endpoints = 0;
            goto next_state;
        }
      }
      if (ep == NULL)
      {
        goto next_state;
      }
    }
      break;
    case STATUS_CHECK_WU_CC_VERSION:
      if ( (SupportsCmdClassFlags(n->nodeid, COMMAND_CLASS_WAKE_UP) & SUPPORTED) == 0 || !mb_enabled() )
      {
        n->state = STATUS_PROBE_ENDPOINTS;
        rd_node_probe_update(n);
        return;
      }

      /*This is a mailbox node */
      n->mode = MODE_MAILBOX;
      uint8_t suc_node = ZW_GetSUCNodeID();
      uint8_t wu_version = 0;
#if 0
     // Disable the check of inclusion controller support for ZGW-3279 cert issue
    // Please reneable this when the spec is changed as mentioned in ZGW-3281
      /*If the node was added by me then set the wakeup interval*/
      /* Three scenarios:
        1. If I am SIS 
                I set WUI and WAKE UP Destination to myself
        2. If I am inclusion controller(not SIS) and SIS supports COMMAND_CLASS_INCLUSION_CONTROLLER,
                I WONT set WUI and WAKE UP Destination
        3. If I inclusion controller (not SIS) and SIS does not support COMMAND_CLASS_INCLUSION_CONTROLLER
                I set WUI and WAKE UP Destination to SIS
      */
      if ((n->node_properties_flags & RD_NODE_FLAG_JUST_ADDED) &&
          (suc_node == MyNodeID || !SupportsCmdClass(suc_node, COMMAND_CLASS_INCLUSION_CONTROLLER)))
#endif
      if ((n->node_properties_flags & RD_NODE_FLAG_JUST_ADDED) &&
          (suc_node == MyNodeID))
      {
        wu_version = rd_node_cc_version_get(n, COMMAND_CLASS_WAKE_UP);
        if (wu_version >= 0x02) {
          n->state = STATUS_GET_WU_CAP;
          DBG_PRINTF("WAKEUP CC V2 or above\n");
          rd_node_probe_update(n);
        } else if(wu_version == 0x01) {
          n->state = STATUS_SET_WAKE_UP_INTERVAL;
          DBG_PRINTF("WAKEUP CC V1\n");
          if (n->node_properties_flags & RD_NODE_FLAG_PORTABLE) {
            n->wakeUp_interval = 0;
          }
          rd_node_probe_update(n);
        } else {
          goto fail_state;
        }
      }
      else
      {
        n->state = STATUS_PROBE_WAKE_UP_INTERVAL;
        rd_node_probe_update(n);
        return;
      }
      break;

    case STATUS_GET_WU_CAP:
          DBG_PRINTF("");
          static ZW_WAKE_UP_INTERVAL_CAPABILITIES_GET_V2_FRAME cf;
          cf.cmdClass = COMMAND_CLASS_WAKE_UP_V2;
          cf.cmd = WAKE_UP_INTERVAL_CAPABILITIES_GET_V2;

          ts_set_std(&p, n->nodeid);

          if (!ZW_SendRequest(&p, (BYTE*) &cf, sizeof(cf),
             WAKE_UP_INTERVAL_CAPABILITIES_REPORT_V2, 3*20, n, rd_cap_wake_up_callback))
          {
            goto fail_state;
          }
       break;
    case STATUS_SET_WAKE_UP_INTERVAL:
          DBG_PRINTF("");
          static ZW_WAKE_UP_INTERVAL_SET_V2_FRAME f;

          f.cmdClass = COMMAND_CLASS_WAKE_UP_V2;
          f.cmd = WAKE_UP_INTERVAL_SET_V2;

          f.seconds1 = (n->wakeUp_interval >> 16) & 0xFF;
          f.seconds2 = (n->wakeUp_interval >> 8) & 0xFF;
          f.seconds3 = (n->wakeUp_interval >> 0) & 0xFF;
          // SUC in long range is always gateway, whose node ID is always 1, so 1 byte node ID is good here
          f.nodeid = ZW_GetSUCNodeID();

          ts_set_std(&p, n->nodeid);
          if (!ZW_SendDataAppl(&p, &f, sizeof(ZW_WAKE_UP_INTERVAL_SET_V2_FRAME),
              rd_set_wu_interval_callback, n))
          {
            goto fail_state;
          }
      break;
    case STATUS_ASSIGN_RETURN_ROUTE:
      if (!ZW_AssignReturnRoute(n->nodeid, MyNodeID, AssignReturnRouteCallback))
      {
        goto fail_state;
      }
      break;
    case STATUS_PROBE_WAKE_UP_INTERVAL:
      ts_set_std(&p, n->nodeid);

      if (!ZW_SendRequest(&p, (BYTE*) &wakeup_get, sizeof(wakeup_get),
          WAKE_UP_INTERVAL_REPORT, REQUEST_TIMEOUT, n, rd_probe_wakeup_callback))
      {
        goto fail_state;
      }
      break;
    case STATUS_MDNS_PROBE:
      if (!mdns_node_name_probe(n, rd_node_name_probe_done, n))
      {
        goto next_state;
      }
      break;
    case STATUS_MDNS_EP_PROBE:
      /* This is used in initial probing. */
      for (ep = list_head(n->endpoints); ep != NULL; ep = list_item_next(ep))
      {
        if (ep->state == EP_STATE_MDNS_PROBE)
        {
          rd_ep_probe_update(ep);
          return;
        }
      }
      goto next_state;
      break;
    case STATUS_DONE:
      LOG_PRINTF("Probe of node %d is done\n", n->nodeid);
      for (ep = list_head(n->endpoints); ep != NULL; ep = list_item_next(ep))
      {
        LOG_PRINTF("Info len %i\n", ep->endpoint_info_len);
      }
      n->lastUpdate = clock_seconds();
      n->lastAwake = clock_seconds();
      n->node_properties_flags &= ~RD_NODE_FLAG_JUST_ADDED;
      n->probe_flags = RD_NODE_FLAG_PROBE_HAS_COMPLETED;
      goto probe_complete;
      break;
    case STATUS_PROBE_FAIL:
      ERR_PRINTF("Probe of node %d failed\n", n->nodeid);
      goto probe_complete;
      break;
    default:
      break;
  }
  return;

  next_state: n->state++;
  rd_node_probe_update(n);
  return;
  fail_state: n->state = STATUS_PROBE_FAIL;
  rd_node_probe_update(n);
  return;

  probe_complete:
  /* Store all node data in persistent memory */
  rd_data_store_nvm_free(n);
  rd_data_store_nvm_write(n);
  current_probe_entry = NULL;

  /*Send out notification for all endpoints */
  for (ep = list_head(n->endpoints); ep != NULL; ep = list_item_next(ep))
  {
    mdns_endpoint_notify(ep, 0);
  }
  /* If a callback is registered for this node, trigger the callback
   * when the node has reached a final state. */
  rd_trigger_probe_completed_notifier(n);

  /* Trigger probe done event */
  process_post(&zip_process, ZIP_EVENT_NODE_PROBED, (void*)n);

  rd_probe_resume();
}

static void rd_reset_probe_completed_notifier(void) {
   memset(&node_probe_notifier, 0, sizeof(node_probe_notifier));
}

static void rd_trigger_probe_completed_notifier(rd_node_database_entry_t *node) {
   uint8_t ii;
   for (ii = 0; ii < NUM_PROBES; ii++) {
      if (node_probe_notifier[ii].node_id == node->nodeid) {
         node_probe_notifier[ii].node_id = 0;
         if (node_probe_notifier[ii].callback) {
            node_probe_notifier[ii].callback(list_head(node->endpoints),
                                             node_probe_notifier[ii].user);
         }
      }
   }
}

void rd_register_new_node(nodeid_t node, uint8_t node_properties_flags)
{
  rd_node_database_entry_t *n;
  rd_ep_database_entry_t * ep;
  //char name[16];

  DBG_PRINTF(" nodeid=%d 0x%02x\n", node, node_properties_flags);
  if (is_virtual_node(node))
  {
    return;
  }

  ASSERT(nodemask_nodeid_is_valid(node));

  n = rd_node_get_raw(node);

  ipv46nat_add_entry(node);

  if (n)
  {
    if (n->state == STATUS_FAILING || n->state == STATUS_PROBE_FAIL
        || n->state == STATUS_DONE)
    {
      DBG_PRINTF("re-probing node %i old state %s\n",n->nodeid, rd_node_probe_state_name(n->state));
      ASSERT(n->nodeid == node);
      n->state = STATUS_CREATED;

      for (ep = list_head(n->endpoints); ep != NULL; ep = list_item_next(ep))
      {
        ep->state = EP_STATE_PROBE_INFO;
      }

      return rd_node_probe_update(n);
    }
    else
    {
       WRN_PRINTF("Node probe is already in progress on node %u.\n", n->nodeid);
      return;
    }
  }

  n = rd_node_entry_alloc(node);

  if (!n)
  {
    ERR_PRINTF("Unable to register new node Out of mem!\n");
    return;
  }

  update_protocol_info(n); //Get protocol info new because this is always sync

  n->node_properties_flags |= node_properties_flags;

  /* Do not probe non-listening node since it'll most certainly fail.
   * Set it to FAIL state and probe it when this node wakes up.
   */
  if (rd_get_node_mode(n->nodeid) == MODE_NONLISTENING
      && !(n->node_properties_flags & RD_NODE_FLAG_JUST_ADDED))
  {
    for (ep = list_head(n->endpoints); ep != NULL; ep = list_item_next(ep))
    {
      ep->state = EP_STATE_PROBE_INFO;
    }
    n->state = STATUS_PROBE_FAIL;
  }

  /*Endpoint 0 always exists*/
  if (!rd_add_endpoint(n, 0))
  {
    n->state = STATUS_PROBE_FAIL;
    rd_node_probe_update(n);
  } else {
    /* Since status is CREATED, this node will be set as
     * current_probe_entry if there is not already a current.  This
     * ensures that if the probe machine is currently locked, it will
     * resume probing as soon as it is unlocked and eventually get to
     * this node.  */
    rd_node_probe_update(n);
  }
}

void rd_exit()
{
  rd_node_database_entry_t *n;
  rd_ep_database_entry_t *ep;
  nodeid_t i;

  ctimer_stop(&dead_node_timer);
  ctimer_stop(&nif_request_timer);

  for (i = 1; i <= ZW_MAX_NODES; i++)
  {
    n = rd_node_get_raw(i);
    if (n)
    {
      n->mode |= MODE_FLAGS_DELETED;
      for (ep = list_head(n->endpoints); ep != NULL; ep = list_item_next(ep))
      {
        mdns_endpoint_notify(ep, 0);
      }
    }
  }
  mdns_exit();
}

void
rd_remove_node(nodeid_t node)
{
  rd_node_database_entry_t *n;
  rd_ep_database_entry_t *ep;

  ZW_Abort_SendRequest(node);
  if (node == 0)
    return;
  n = rd_node_get_raw(node);
  if (n == 0)
    return;

  /*
   * Abort probe if we have one in progress.
   */
  if(current_probe_entry == n) {
    current_probe_entry = 0;
  }

  DBG_PRINTF("Removing node %i %p\n", node, n);

  if ((n->mode & 0xff) == MODE_MAILBOX )
  {
    /*Clear the mailbox for potential entries*/
    mb_failing_notify(n->nodeid);
  }

  n->mode |= MODE_FLAGS_DELETED;
  if (n->nodeid == 0) {
    return;
  }
  for (ep = list_head(n->endpoints); ep != NULL; ep = list_item_next(ep))
  {
    //DBG_PRINTF("Removing endpoint %s %p\n", ep->endpoint_name,ep);
    mdns_endpoint_notify(ep, 1);
    mdns_remove_from_answers(ep);
  }

  /* Clear node from data storage */
  rd_node_entry_free(node);

  ipv46nat_del_entry(node);
}

int rd_init(uint8_t lock)
{
  nodeid_t i;
  nodemask_t nodelist = {0};
  uint16_t lr_nodelist_len = 0;
  uint8_t ver, capabilities, len, chip_type, chip_version;
  rd_node_database_entry_t* nd;
  DWORD old_homeID;

  data_store_init();

  old_homeID = homeID;

  rd_reset_probe_completed_notifier();

  /* The returned homeID is in network byte order (big endian) */
  MemoryGetID((BYTE*) &homeID, &MyNodeID);
  LOG_PRINTF("HomeID is %08x node id %d\n",uip_htonl(homeID),MyNodeID);
  ipv46nat_init();
  /* Make sure the virtual node mask is up to date */
  copy_virtual_nodes_mask_from_controller();

  nif_request_ep = 0;
  if (rd_probe_in_progress()) {
     ERR_PRINTF("RD re-initialized while probing node %u\n", current_probe_entry->nodeid);
  }
  current_probe_entry = 0;
  probe_lock = lock;

  SerialAPI_GetInitData(&ver, &capabilities, &len, nodelist, &chip_type,
      &chip_version);
  SerialAPI_GetLRNodeList(&lr_nodelist_len, NODEMASK_GET_LR(nodelist));
  /*Always update the entry for this node, since this is without cost, and network role
   * might have changed. */
  rd_register_new_node(MyNodeID, 0x00);
  DBG_PRINTF("Requesting probe of gw, node id %u\n", MyNodeID);

  /* i is a nodeid */
  for (i = 1; i <= ZW_LR_MAX_NODE_ID_IN_NVM; i++)
  {
    if (nodemask_test_node(i, nodelist))
    {
      if (i == MyNodeID)
        continue;

      DBG_PRINTF("Network has node %i\n", i);
      nd = rd_node_entry_import(i);

      if (nd == 0)
      {
        rd_register_new_node(i, 0x00);
      }
      else
      {
        rd_node_database_entry_t* n = rd_node_get_raw(i);
        rd_ep_database_entry_t *ep;

        /*Create nat entry for node */
        ipv46nat_add_entry(i);

        if (n->state == STATUS_CREATED)
        {
          for (ep = list_head(n->endpoints); ep != NULL; ep = list_item_next(ep))
          {
            ep->state = EP_STATE_PROBE_INFO;
          }
          if ((rd_get_node_mode(n->nodeid) & 0xff) == MODE_NONLISTENING
              || (rd_get_node_mode(n->nodeid) & 0xff) == MODE_MAILBOX)
          {
            DBG_PRINTF("Node %d is sleeping. Probe it later.\n", n->nodeid);
            n->state = STATUS_PROBE_FAIL;
            /* We don't know the WUI, so make sure the node does not
             * become failing before we have a chance to interview
             * it. */
            n->wakeUp_interval = 0;
            n->probe_flags = RD_NODE_PROBE_NEVER_STARTED;
          }
        }
        else if (n->state == STATUS_DONE)
        {
          /* Here we just fake that the nodes has been alive recently.
           * This is to prevent that the node becomes failing without
           * reason*/
          n->lastAwake = clock_seconds();
          /* Schedule all names to be re-probed. */
          n->state = STATUS_MDNS_PROBE;
          for (ep = list_head(n->endpoints); ep != NULL;
              ep = list_item_next(ep))
          {
            ep->state = EP_STATE_MDNS_PROBE;
          }
        }
        /* Mark wakeup nodes without fixed interval as recently
         * alive. This prevents frames queued to them from being
         * dropped too early after a gateway restart. */
        if (((n->mode & 0xff) == MODE_MAILBOX) && (n->wakeUp_interval == 0))
        {
          n->lastAwake = clock_seconds();
        }
      }
    }
    else
    {
       /* TODO-reset: When called in this place, this does not remove
        * the node from the nat_table, since nat_table_size has just
        * been set to 0 *in ipv46_nat_init()).  So DHCP Release is not
        * sent. */
      rd_remove_node(i);
    }
  }

  ctimer_set(&dead_node_timer, 60 * 1000, rd_check_for_dead_nodes_worker,
      (void*) 1);
  rd_probe_resume();
  DBG_PRINTF("Resource directory init done\n");
  return (old_homeID != homeID);
}

void
rd_full_network_discovery()
{
  nodemask_t nodelist = {0};
  uint8_t ver, capabilities, len, chip_type, chip_data;
  nodeid_t i;
  uint16_t lr_nodelist_len = 0;

  DBG_PRINTF("Re-synchronizing nodes\n");

  SerialAPI_GetInitData(&ver, &capabilities, &len, nodelist, &chip_type,
      &chip_data);
  SerialAPI_GetLRNodeList(&lr_nodelist_len, NODEMASK_GET_LR(nodelist));

  for (i = 1; i <= ZW_LR_MAX_NODE_ID_IN_NVM; i++)
  {
    if (nodemask_test_node(i, nodelist))
    {
      rd_register_new_node(i, 0x00);
    }
    else
    {
      rd_remove_node(i);
    }
  }
}

u8_t
rd_probe_new_nodes()
{
  nodemask_t nodelist = {0};
  uint8_t ver, capabilities, len, chip_type, chip_data;
  nodeid_t i, k;
  uint16_t lr_nodelist_len = 0;

  DBG_PRINTF("Re-synchronizing nodes in rd_probe_new_nodes\n");

  SerialAPI_GetInitData(&ver, &capabilities, &len, nodelist, &chip_type,
      &chip_data);
  SerialAPI_GetLRNodeList(&lr_nodelist_len, NODEMASK_GET_LR(nodelist));
  k = 0;
  for (i = 1; i <= ZW_LR_MAX_NODE_ID_IN_NVM; i++)
  {
    if (nodemask_test_node(i, nodelist) && !rd_node_exists(i) && !is_virtual_node(i))
    {
      rd_register_new_node(i, 0x00);
      k++;
    }
  }
  return k;
}
/******************************************* Iterators ***************************************/

rd_ep_database_entry_t*
rd_ep_iterator_group_begin(rd_group_entry_t* ge)
{
  return 0;
}

rd_ep_database_entry_t*
rd_group_ep_iterator_next(rd_group_entry_t* ge, rd_ep_database_entry_t* ep)
{
  return 0;
}

/**
 *
 * Supported  non Sec
 * Controlled non Sec
 * Supported Sec
 * Controlled Sec
 *
 */
int
rd_ep_class_support(rd_ep_database_entry_t* ep, uint16_t cls)
{
  int i;
  int bSecureClass;
  int bControlled;
  u8_t result;
  u16_t c;
  if (!ep->endpoint_info)
    return 0;

  if(ep->node->mode & MODE_FLAGS_DELETED) {
    return 0;
  }

  bSecureClass = 0;
  bControlled = 0;
  result = 0;

  for (i = 2; i < ep->endpoint_info_len; i++)
  {
    c = ep->endpoint_info[i];
    /*
     * F0 mark:
     * For interoperability considerations of some thermostat and controller
     * products. Check ZW Application CC spec, 4.101.1 together with table 134.
     */
    if ((c & 0xF0) == 0xF0 && (i < ep->endpoint_info_len - 1))
    {
      i++;
      c = ((c & 0xFF) << 8) | ep->endpoint_info[i];
    }

    if (c == COMMAND_CLASS_SECURITY_SCHEME0_MARK)
    {
      bSecureClass = 1;
      bControlled = 0;
    }
    else if (c == COMMAND_CLASS_MARK)
    {
      bControlled = 1;
    }
    else if (c == cls)
    {
      result |= 1 << ((bSecureClass << 1) | bControlled);
    }
  }
  //DBG_PRINTF("Result %x\n", result);
  return result;
}

void
rd_update_ep_name_and_location(rd_ep_database_entry_t* ep, char* name,
    u8_t name_size, char* location, u8_t location_size)
{
  char* new_name;
  char* new_location;

  if (ep->node->state != STATUS_DONE)
  {
    WRN_PRINTF("Not setting the node name, because probe is not done\n");
    return;
  }

  if (is_valid_mdns_name(name, name_size) == false)
  {
    ERR_PRINTF("Invalid name %s\n", name);
    return;
  }

  if (location_size > 0 && (is_valid_mdns_location(location, location_size) == false))
  {
    ERR_PRINTF("Invalid location %s\n", location);
    return;
  }

  if (((name_size + location_size) > 63) || name_size < 1)
  {
    ERR_PRINTF("Resource names must be between 1 and  63 bytes.\n");
    return;
  }

  if (rd_lookup_by_ep_name(name, location))
  {
    WRN_PRINTF("Already have an endpoint with this name\n");
    return;
  }

  new_name = rd_data_mem_alloc(name_size);
  memcpy(new_name, name, name_size);

  if (location_size > 0)
  {
    new_location = rd_data_mem_alloc(location_size);
    memcpy(new_location, location, location_size);
  }
  else
  {
    new_location = 0;
  }

  /*Send a mDNS goodbye for this endpoint right away */
  ep->node->mode |= MODE_FLAGS_DELETED;
  mdns_endpoint_notify(ep, 1);
  ep->node->mode &= ~MODE_FLAGS_DELETED;

  if (ep->endpoint_name_len)
  {
    rd_data_mem_free(ep->endpoint_name);
  }
  if (ep->endpoint_loc_len)
  {
    rd_data_mem_free(ep->endpoint_location);
  }

  ep->endpoint_name_len = name_size;
  ep->endpoint_name = new_name;

  ep->endpoint_location = new_location;
  ep->endpoint_loc_len = location_size;

  ep->state = EP_STATE_MDNS_PROBE;
  ep->node->state = STATUS_MDNS_EP_PROBE;
  DBG_PRINTF("Setting cache-flush bit as its new name\n");
  dont_set_cache_flush_bit = 1;
  if (current_probe_entry == NULL) {
     current_probe_entry=ep->node;
     rd_node_probe_update(ep->node);
  }
  /* If there is a current probe entry, the probe machine will
   * eventually get around to this node, as well. */
}

void
rd_update_ep_name(rd_ep_database_entry_t* ep, char* name, u8_t size)
{
  rd_update_ep_name_and_location(ep, name, size, ep->endpoint_location,
      ep->endpoint_loc_len);
}

void
rd_update_ep_location(rd_ep_database_entry_t* ep, char* name, u8_t size)
{

  rd_update_ep_name_and_location(ep, ep->endpoint_name, ep->endpoint_name_len,
      name, size);
}

/*
 * Lookup an endpoint from its service name
 * \return NULL if no endpoint is found otherwise return the ep sructure.
 */
rd_ep_database_entry_t*
rd_lookup_by_ep_name(const char* name, const char* location)
{
  rd_ep_database_entry_t* ep;
  uint8_t j;
  char buf[64];

  for (ep = rd_ep_first(0); ep; ep = rd_ep_next(0, ep))
  {
    j = rd_get_ep_name(ep, buf, sizeof(buf));
    if (mdns_str_cmp(buf, name, j) == 0)
    {
      if (location)
      {
        if (mdns_str_cmp(ep->endpoint_location, location, ep->endpoint_loc_len))
        {
          return ep;
        }
      }
      else
      {
        return ep;
      }
    }
  }
  return 0;
}

rd_group_entry_t*
rd_lookup_group_by_name(const char* name)
{
  return 0;
}

/**
 * \ingroup node_db
 * MUST be called when a node entry is no longer needed.
 */
void
rd_free_node_dbe(rd_node_database_entry_t* n)
{
  if (n)
  {
    ASSERT(n->refCnt>0);
    n->refCnt--;
  }
}

rd_ep_database_entry_t*
rd_get_ep(nodeid_t nodeid, uint8_t epid)
{
  rd_node_database_entry_t* n = rd_get_node_dbe(nodeid);
  rd_ep_database_entry_t* ep = NULL;

  if (n == NULL)
  {
    return NULL;
  }
  for (ep = list_head(n->endpoints); ep != NULL; ep = list_item_next(ep))
  {
    if (ep->endpoint_id == epid)
    {
      break;
    }
  }
  rd_free_node_dbe(n);
  return ep;
}

bool sleeping_node_is_in_firmware_upgrade(nodeid_t nodeid)
{
  rd_node_database_entry_t *n = rd_get_node_dbe(nodeid);
  if (n && (n->mode == MODE_FIRMWARE_UPGRADE))
  {
      return true;
  }
  return false;
}

rd_node_mode_t
rd_get_node_mode(nodeid_t nodeid)
{
  /* FIXME: Choose a better default value in case lookup fails */
  rd_node_mode_t mode = MODE_FLAGS_DELETED;
  rd_node_database_entry_t *n = rd_get_node_dbe(nodeid);
  //ASSERT(n);
  if (n)
  {
    mode = n->mode;
    rd_free_node_dbe(n);
  }
  return mode;
}

void rd_mark_node_deleted(nodeid_t nodeid) {
  /* FIXME: Choose a better default value in case lookup fails */
  rd_node_database_entry_t *n = rd_get_node_dbe(nodeid);
  //ASSERT(n);
  if (n)
  {
    n->mode |= MODE_FLAGS_DELETED;
    rd_free_node_dbe(n);
  }
}

rd_node_state_t
rd_get_node_state(nodeid_t nodeid)
{
  /* FIXME: Choose a better default value in case lookup fails */
  rd_node_state_t state = STATUS_PROBE_FAIL;
  rd_node_database_entry_t *n = rd_get_node_dbe(nodeid);
  if (n)
  {
    state = n->state;
    rd_free_node_dbe(n);
  }
  return state;
}

uint16_t rd_get_node_probe_flags(nodeid_t nodeid)
{
  uint16_t probe_flags = RD_NODE_PROBE_NEVER_STARTED;
  rd_node_database_entry_t *n = rd_get_node_dbe(nodeid);
  if (n)
  {
    probe_flags = n->probe_flags;
    rd_free_node_dbe(n);
  }
  return probe_flags;

}

/***************Reimplementaion of nodecache *****************************/
/*
 * Returns a bit field of Secure and non-secure support for given node
 */
static int
SupportsCmdClassFlags(nodeid_t nodeid, WORD class)
{
  rd_node_database_entry_t* n;
  int rc;

  rc = 0;
  n = rd_get_node_dbe(nodeid);
  if (n)
  {
    if (list_head(n->endpoints))
    {

      rc = rd_ep_class_support(
          (rd_ep_database_entry_t*) list_head(n->endpoints), class);
    }
    rd_free_node_dbe(n);
  }

  return rc;
}

int
SupportsCmdClass(nodeid_t nodeid, WORD class)
{
  return ((SupportsCmdClassFlags(nodeid, class) & SUPPORTED_NON_SEC) > 0);
}

int
SupportsCmdClassSecure(nodeid_t nodeid, WORD class)
{
  return ((SupportsCmdClassFlags(nodeid, class) & SUPPORTED_SEC) > 0);
}

static int rd_ep_supports_cmd_class_nonsec(rd_ep_database_entry_t* ep, uint16_t class)
{
   return ((rd_ep_class_support(ep, class) & SUPPORTED_NON_SEC) > 0);
}

/**
 * Set node attribute flags
 */BYTE
SetCacheEntryFlagMasked(nodeid_t nodeid, BYTE value, BYTE mask)
{
  rd_node_database_entry_t* n;

  n = rd_get_node_dbe(nodeid);
  if (n)
  {
    n->security_flags = (n->security_flags & (~mask)) | value;
    rd_data_store_update(n);

    rd_free_node_dbe(n);
  }
  else
  {
    ERR_PRINTF("Attempt to set flag of non existing node = %i\n", nodeid);
  }
  return 1;
}

/**
 * Retrieve Cache entry flag
 */BYTE
GetCacheEntryFlag(nodeid_t nodeid)
{
  rd_node_database_entry_t* n;
  uint8_t rc;

  if(is_virtual_node(nodeid)) {
    n = rd_get_node_dbe(MyNodeID);
  } else {
    n = rd_get_node_dbe(nodeid);
    if (n) {
      if (nodeid != n->nodeid) {
        ERR_PRINTF("Attempt to get security flag from a node entry with "
                   "inconsistent node ID inside, nodeid: %d, n->nodeid: %d\n",
                   nodeid, n->nodeid);
        assert(0);
      }
    }
  }

  if (!n)
  {
    ERR_PRINTF("GetCacheEntryFlag: on non existing node=%i\n", nodeid);
    return 0;
  }
  rc = n->security_flags;
  rd_free_node_dbe(n);
  return rc;
}

int
isNodeController(nodeid_t nodeid)
{
  rd_node_database_entry_t* n;
  uint8_t rc = 0;

  n = rd_get_node_dbe(nodeid);
  if (n)
  {
    DBG_PRINTF("isNodeController(%d) type =%d\n", nodeid, n->nodeType);
    rc = (n->nodeType == BASIC_TYPE_CONTROLLER)
        || (n->nodeType == BASIC_TYPE_STATIC_CONTROLLER);
    rd_free_node_dbe(n);
  }
  else
  {
    ERR_PRINTF("isNodeController: on non existing node\n");
  }
  return rc;
}

/* Set node failing if it is in state STATUS_DONE and it is not
 * MODE_MAILBOX.  */
void rd_node_is_unreachable(nodeid_t node) {
   rd_node_database_entry_t *n;
   n = rd_get_node_dbe(node);
   if (n) {
      /* Mailbox nodes are managed by dead_nodes_worker */
      if ((RD_NODE_MODE_VALUE_GET(n) != MODE_MAILBOX) &&
          (RD_NODE_MODE_VALUE_GET(n) != MODE_FIRMWARE_UPGRADE)) {
         rd_set_failing(n, TRUE);
      }
      rd_free_node_dbe(n);
   }
}

/* Clear node failing if node is in STATUS_FAILING */
void rd_node_is_alive(nodeid_t node) {
  rd_node_database_entry_t *n;
  n = rd_get_node_dbe(node);
  if (n)
  {
    n->lastAwake = clock_seconds();
    rd_set_failing(n, FALSE);
    rd_free_node_dbe(n);
  }
}

/**
    Business logic of this function:

    - if it is a REPORT the node must use its highest supported scheme
    - if it is a GET/SET of a command listed in the secure nif of the GW, it must be the net_scheme
    - if it is a GET/SET of a command listed in the non-secure nif of the GW, it should be the highest scheme
      supported by the node. Legacy non-S2 nodes are allowed to send on lower-schemes for backwards compatibility.
    Exceptions:
     - BASIC C.C. is always supported
     - For supervision its the rule of the embedded command that applies.
*/
int rd_check_security_for_unsolicited_dest( nodeid_t rnode,  security_scheme_t scheme, void *pData, u16_t bDatalen )
{
  ZW_APPLICATION_TX_BUFFER* pCmd = (ZW_APPLICATION_TX_BUFFER*)pData;
  BYTE *c = (BYTE *)pData;
  DBG_PRINTF("Check security for rnode:%u scheme %s class %02x command %02x\n", rnode, network_scheme_name(scheme), pCmd->ZW_Common.cmdClass, pCmd->ZW_Common.cmd);

  if(rnode==0) return FALSE;

  uint8_t node_scheme_mask = GetCacheEntryFlag(rnode);
  security_scheme_t highest_node_scheme = highest_scheme(node_scheme_mask);
  DBG_PRINTF("highest_node_scheme. %s, node_scheme_mask: 0x%02x\n", network_scheme_name(highest_node_scheme), node_scheme_mask);
  if (scheme == USE_CRC16) { // CRC_16 is treated as non-secure
      scheme = NO_SCHEME;
  }

  /* Is this a supporting command ie a REPORT */
  if(CommandAnalyzerIsSupporting(pCmd->ZW_Common.cmdClass, pCmd->ZW_Common.cmd)) {

    DBG_PRINTF("class %02x command %02x\n", pCmd->ZW_Common.cmdClass, pCmd->ZW_Common.cmd);
    /* if the sending node is a S2 node check that the frame was received on the highest common security level => accept */
    //Check if the sending node supports S2, ie it has one of the S2 keys
    if(scheme_compare(highest_node_scheme,SECURITY_SCHEME_2_UNAUTHENTICATED)) {
      DBG_PRINTF("Node supports S2");
      //Check that the frame sent was sent using the highest scheme
      if( scheme_compare(scheme, highest_node_scheme)) {
        DBG_PRINTF("frame sent with highest scheme\n");
        return TRUE;
      }

    } else {
      DBG_PRINTF("Node does NOT support S2");
      //if the sending node is a non-secure or S0 node => accept
      return TRUE;
    }

  } else {
    //This is a controlling command SET/GET

    if((pCmd->ZW_Common.cmdClass == COMMAND_CLASS_VERSION) && (pCmd->ZW_Common.cmd == VERSION_COMMAND_CLASS_GET)) {
        if (IsCCInNodeInfoSetList(c[2],TRUE)) {
           if(scheme == net_scheme) {
             return TRUE;
           }
        }
    }

   /* Some non-secure and quirky S0 legacy sensor devices send BASIC SETs non-securely. We need to let these kind of BASIC SETs reach the unsol destination.
    * But we also need to make sure that supervision encapsulated BASIC Set are dropped and replied with NO_SUPPORT */
    /* So we allow non-secure BASIC CC if its send from non-secure/S0 node which does not support Supervision */
    /* Trusting unsecure/CRC16 messages from an S0 node is a security hole, so the unsolicited destination should not trust these messages unless there is a good reason. */
    /* E.g. if this device has already been identified as a quirky device. */
    if((pCmd->ZW_Common.cmdClass == COMMAND_CLASS_BASIC ) &&
        !(SupportsCmdClass(rnode, COMMAND_CLASS_SUPERVISION)) &&
        ((highest_node_scheme == NO_SCHEME) ||
         (highest_node_scheme == SECURITY_SCHEME_0))) {
      DBG_PRINTF("Received BASIC CC from S0 or non-secure node which does not"
                 " support Supervision.\n");
      return TRUE;
    }

    //Is this set in the secure (TRUE) part of the NIF, or BASIC which must always be supported
    if(  (pCmd->ZW_Common.cmdClass == COMMAND_CLASS_BASIC ) || IsCCInNodeInfoSetList(pCmd->ZW_Common.cmdClass,TRUE)) {
      if(scheme == net_scheme) {
        return TRUE;
      }
    }
    /* SUPERVISION should be handled by the unsolicited destination. Supervision sent on 
     * different security level than highest common are also forwarded to unsol
     * destination. */
    /* Frame encapsulate in supervision CC is passed through this function 
     * (rd_check_security_for_unsolicited_dest()) twice. 
     * First with the payload frame by SupervisionCommandHandler then with the 
     * supervison frame by the normal command handler.*/
    if(pCmd->ZW_Common.cmdClass == COMMAND_CLASS_SUPERVISION) {
       return TRUE;
    }
   
    /* Is CC in non-secure (FALSE) NIF, then it should be handled by 
     * Unsolicited Destination. */
    if (IsCCInNodeInfoSetList(pCmd->ZW_Common.cmdClass,FALSE)) {
      /* if the sending node is a S2 node check that the frame was received on the highest common security level => accept */
      //Check if the sending node supports S2, ie it has one of the S2 keys
      if(scheme_compare(highest_node_scheme,SECURITY_SCHEME_2_UNAUTHENTICATED)) {
        if (scheme == highest_node_scheme) {
          return TRUE;
        }
      } else {
        //if the sending node is a non-secure or S0 node => accept
        return TRUE;
      }
    }
  }

  return FALSE;
}

bool rd_check_nif_security_controller_flag( nodeid_t node ) {
  NODEINFO ni;
  ZW_GetNodeProtocolInfo(node,&ni);
  return (ni.security & ZWAVE_NODEINFO_CONTROLLER_NODE) >0;
}

