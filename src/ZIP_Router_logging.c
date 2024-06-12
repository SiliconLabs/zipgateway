#include <stdio.h>
#include <stdint.h>
#include <ZW_transport_api.h>
#include <Serialapi.h>
#include "ZIP_Router_logging.h"
#include "RD_internal.h"
#include "CC_NetworkManagement.h"
#include "s2_event.h"

#define STR_CASE(x) \
  case x:           \
    return #x;

char print_frame_buf[1024] = {0};
const char *lf = "frame too long";
const char *e = "can not print frame";

const char *print_frame(const char *cmd, unsigned int len)
{
    if (!cmd || !len) {
      return e;
    }
    if (len > (sizeof(print_frame_buf)/4)) { 
      return lf; 
    }; 

    int idx = 0;
    for(int i = 0; i < len; i++) {
        idx += snprintf(print_frame_buf + idx, 4, "%02X ", (uint8_t) cmd[i]);
    }
    return print_frame_buf;
}

const char *transmit_status_name(int state)
{
  switch (state) {
    case TRANSMIT_COMPLETE_OK          : return "TRANSMIT_COMPLETE_OK";
    case TRANSMIT_COMPLETE_NO_ACK        : return "TRANSMIT_COMPLETE_NO_ACK";
    case TRANSMIT_COMPLETE_FAIL          : return "TRANSMIT_COMPLETE_FAIL";
    case TRANSMIT_ROUTING_NOT_IDLE       : return "TRANSMIT_ROUTING_NOT_IDLE";
    case TRANSMIT_COMPLETE_REQUEUE_QUEUED: return "TRANSMIT_COMPLETE_REQUEUE_QUEUED";
    case TRANSMIT_COMPLETE_REQUEUE       : return "TRANSMIT_COMPLETE_REQUEUE";
    case TRANSMIT_COMPLETE_ERROR         : return "TRANSMIT_COMPLETE_ERROR";
    default:
      return "Unknown";
  }
}

void print_key(uint8_t *buf)
{
  WRN_PRINTF(
      "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n",
      buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8],
      buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
}

void
print_hex(uint8_t* buf, int len)
{
  int i;
  for (i = 0; i < len; i++)
  {
    printf("%02X", buf[i]);
  }
  printf("\n");
}

const char *ep_state_name(int state)
{
  static char str[25];
  switch (state)
  {
  case EP_STATE_PROBE_INFO              : return "EP_STATE_PROBE_INFO";
  case EP_STATE_PROBE_SEC2_C2_INFO      : return "EP_STATE_PROBE_SEC2_C2_INFO";
  case EP_STATE_PROBE_SEC2_C1_INFO      : return "EP_STATE_PROBE_SEC2_C1_INFO";
  case EP_STATE_PROBE_SEC2_C0_INFO      : return "EP_STATE_PROBE_SEC2_C0_INFO";
  case EP_STATE_PROBE_SEC0_INFO         : return "EP_STATE_PROBE_SEC0_INFO";
  case EP_STATE_PROBE_VERSION           : return "EP_STATE_PROBE_VERSION";
  case EP_STATE_PROBE_ZWAVE_PLUS        : return "EP_STATE_PROBE_ZWAVE_PLUS";
  case EP_STATE_MDNS_PROBE              : return "EP_STATE_MDNS_PROBE";
  case EP_STATE_MDNS_PROBE_IN_PROGRESS  : return "EP_STATE_MDNS_PROBE_IN_PROGRESS";
  case EP_STATE_PROBE_DONE              : return "EP_STATE_PROBE_DONE";
  case EP_STATE_PROBE_FAIL              : return "EP_STATE_PROBE_FAIL";
  default:
    sprintf(str, "%d", state);
    return str;
 }
};
const char* rd_node_probe_state_name(int state)
{
  static char str[25];

  switch(state)
  {
  case STATUS_CREATED               :return "STATUS_CREATED";
  case STATUS_PROBE_NODE_INFO       :return "STATUS_PROBE_NODE_INFO";
  case STATUS_PROBE_PRODUCT_ID      :return "STATUS_PROBE_PRODUCT_ID";
  case STATUS_ENUMERATE_ENDPOINTS   :return "STATUS_ENUMERATE_ENDPOINTS";
  case STATUS_FIND_ENDPOINTS        :return "STATUS_FIND_ENDPOINTS";
  case STATUS_CHECK_WU_CC_VERSION   :return "STATUS_CHECK_WU_CC_VERSION";
  case STATUS_GET_WU_CAP            :return "STATUS_GET_WU_CAP";            
  case STATUS_SET_WAKE_UP_INTERVAL  :return "STATUS_SET_WAKE_UP_INTERVAL";
  case STATUS_ASSIGN_RETURN_ROUTE   :return "STATUS_ASSIGN_RETURN_ROUTE";
  case STATUS_PROBE_WAKE_UP_INTERVAL:return "STATUS_PROBE_WAKE_UP_INTERVAL";
  case STATUS_PROBE_ENDPOINTS       :return "STATUS_PROBE_ENDPOINTS";
  case STATUS_MDNS_PROBE            :return "STATUS_MDNS_PROBE";
  case STATUS_MDNS_EP_PROBE         :return "STATUS_MDNS_EP_PROBE";
  case STATUS_DONE                  :return "STATUS_DONE";
  case STATUS_PROBE_FAIL            :return "STATUS_PROBE_FAIL";
  case STATUS_FAILING               :return "STATUS_FAILING";
  default:
    sprintf(str, "%d", state);
    return str;
  }
};
const char* nm_state_name(int state)
{
  static char str[25];
  switch(state)
  {
  case NM_IDLE                             : return "NM_IDLE";
  case NM_WAITING_FOR_ADD                  : return "NM_WAITING_FOR_ADD";
  case NM_NODE_FOUND                       : return "NM_NODE_FOUND";
  case NM_WAIT_FOR_PROTOCOL                : return "NM_WAIT_FOR_PROTOCOL";
  case NM_NETWORK_UPDATE                   : return "NM_NETWORK_UPDATE";
  case NM_WAITING_FOR_PROBE                : return "NM_WAITING_FOR_PROBE";
  case NM_SET_DEFAULT                      : return "NM_SET_DEFAULT";
  case NM_LEARN_MODE                       : return "NM_LEARN_MODE";
  case NM_LEARN_MODE_STARTED               : return "NM_LEARN_MODE_STARTED";
  case NM_WAIT_FOR_SECURE_ADD              : return "NM_WAIT_FOR_SECURE_ADD";
  case NM_SENDING_NODE_INFO                : return "NM_SENDING_NODE_INFO";
  case NM_WAITING_FOR_NODE_REMOVAL         : return "NM_WAITING_FOR_NODE_REMOVAL";
  case NM_WAITING_FOR_FAIL_NODE_REMOVAL    : return "NM_WAITING_FOR_FAIL_NODE_REMOVAL";
  case NM_WAITING_FOR_NODE_NEIGH_UPDATE    : return "NM_WAITING_FOR_NODE_NEIGH_UPDATE";
  case NM_WAITING_FOR_RETURN_ROUTE_ASSIGN  : return "NM_WAITING_FOR_RETURN_ROUTE_ASSIGN";
  case NM_WAITING_FOR_RETURN_ROUTE_DELETE  : return "NM_WAITING_FOR_RETURN_ROUTE_DELETE";

  case NM_WAIT_FOR_PROBE_AFTER_ADD         : return "NM_WAIT_FOR_PROBE_AFTER_ADD";
  case NM_WAIT_FOR_SECURE_LEARN            : return "NM_WAIT_FOR_SECURE_LEARN";
  case NM_WAIT_FOR_MDNS                    : return "NM_WAIT_FOR_MDNS";
  case NM_WAIT_FOR_PROBE_BY_SIS            : return "NM_WAIT_FOR_PROBE_BY_SIS";
  case NM_WAIT_FOR_OUR_PROBE               : return "NM_WAIT_FOR_OUR_PROBE";
  case NM_WAIT_DHCP                        : return "NM_WAIT_DHCP";
  case NM_REMOVING_ASSOCIATIONS            : return "NM_REMOVING_ASSOCIATIONS";

  case NM_REPLACE_FAILED_REQ               : return "NM_REPLACE_FAILED_REQ";
  case NM_PREPARE_SUC_INCLISION            : return "NM_PREPARE_SUC_INCLISION";
  case NM_WIAT_FOR_SUC_INCLUSION           : return "NM_WIAT_FOR_SUC_INCLUSION";
  case NM_PROXY_INCLUSION_WAIT_NIF         : return "NM_PROXY_INCLUSION_WAIT_NIF";
  case NM_WAIT_FOR_SELF_DESTRUCT           : return "NM_WAIT_FOR_SELF_DESTRUCT";
  case NM_WAIT_FOR_SELF_DESTRUCT_RETRY     : return "NM_WAIT_FOR_SELF_DESTRUCT_RETRY";
  case NM_WAIT_FOR_TX_TO_SELF_DESTRUCT     : return "NM_WAIT_FOR_TX_TO_SELF_DESTRUCT";
  case NM_WAIT_FOR_TX_TO_SELF_DESTRUCT_RETRY: return "NM_WAIT_FOR_TX_TO_SELF_DESTRUCT_RETRY";
  case NM_WAIT_FOR_SELF_DESTRUCT_REMOVAL   : return "NM_WAIT_FOR_SELF_DESTRUCT_REMOVAL";
  case NM_WAIT_FOR_SELF_DESTRUCT_REMOVAL_RETRY   : return "NM_WAIT_FOR_SELF_DESTRUCT_REMOVAL_RETRY";
  case NM_WAIT_FOR_NEIGHBOR_UPDATE_AFTER_SECURE_ADD: return "NM_WAIT_FOR_NEIGHBOR_UPDATE_AFTER_SECURE_ADD";
  default:
    sprintf(str, "%d", state);
    return str;
  }
}

const char * nm_event_name(int ev)
{
  static char str[25];
  switch (ev) 
  {
  case NM_EV_ADD_LEARN_READY            :       return  "NM_EV_ADD_LEARN_READY";
  case NM_EV_ADD_NODE_FOUND             :       return  "NM_EV_ADD_NODE_FOUND";
  case NM_EV_ADD_CONTROLLER             :       return  "NM_EV_ADD_CONTROOLER";
  case NM_EV_ADD_PROTOCOL_DONE          :       return  "NM_EV_ADD_PROTOCOL_DONE";
  case NM_EV_ADD_END_NODE                  :       return  "NM_EV_ADD_END_NODE";
  case NM_EV_ADD_FAILED                 :       return  "NM_EV_ADD_FAILED";
  case NM_EV_ADD_NOT_PRIMARY            :       return  "NM_EV_ADD_NOT_PRIMARY";
  case NM_EV_ADD_NODE_STATUS_DONE       :       return  "NM_EV_ADD_NODE_STATUS_DONE";

  case NM_EV_NODE_ADD                   :       return  "NM_EV_NODE_ADD";
  case NM_NODE_ADD_STOP                 :       return  "NM_NODE_ADD_STOP";
  case NM_EV_TIMEOUT                    :       return  "NM_EV_TIMEOUT";
  case NM_EV_SECURITY_DONE              :       return  "NM_EV_SECURITY_DONE";
  case NM_EV_ADD_SECURITY_REQ_KEYS      :       return  "NM_EV_ADD_SECURITY_REQ_KEYS";
  case NM_EV_ADD_SECURITY_KEY_CHALLENGE :       return  "NM_EV_ADD_SECURITY_KEY_CHALLANGE";
  case NM_EV_NODE_PROBE_DONE            :       return  "NM_EV_NODE_PROBE_DONE";
  case NM_EV_DHCP_DONE                  :       return  "NM_EV_DHCP_DONE";
  case NM_EV_NODE_ADD_S2                :       return  "NM_EV_NODE_ADD_S2";

  case NM_EV_ADD_SECURITY_KEYS_SET      :       return  "NM_EV_ADD_SECURITY_KEYS_SET";
  case NM_EV_ADD_SECURITY_DSK_SET       :       return  "NM_EV_ADD_SECURITY_DSK_SET";

  case NM_EV_REPLACE_FAILED_START       :       return  "NM_EV_REPLACE_FAILED_START";

  case NM_EV_REPLACE_FAILED_STOP        :       return  "NM_EV_REPLACE_FAILED_STOP";
  case NM_EV_REPLACE_FAILED_DONE        :       return  "NM_EV_REPLACE_FAILED_DONE";
  case NM_EV_REPLACE_FAILED_FAIL        :       return  "NM_EV_REPLACE_FAILED_FAIL";
  case NM_EV_REPLACE_FAILED_START_S2    :       return  "NM_EV_REPLACE_FAILED_START_S2";
  case NM_EV_MDNS_EXIT                  :       return  "NM_EV_MDNS_EXIT";
  case NM_EV_LEARN_SET                  :       return  "NM_EV_LEARN_SET";
  case NM_EV_REQUEST_NODE_LIST          :       return  "NM_EV_REQUEST_NODE_LIST";
  case NM_EV_REQUEST_FAILED_NODE_LIST   :       return  "NM_EV_REQUEST_FAILED_NODE_LIST";
  case NM_EV_PROXY_COMPLETE             :       return  "NM_EV_PROXY_COMPLETE";
  case NM_EV_START_PROXY_INCLUSION      :       return  "NM_EV_START_PROXY_INCLUSION";
  case NM_EV_START_PROXY_REPLACE        :       return  "NM_EV_START_PROXY_REPLACE";
  case NM_EV_NODE_INFO                  :       return  "NM_EV_NODE_INFO";
  case NM_EV_FRAME_RECEIVED             :       return  "NM_EV_FRAME_RECEIVED";
  case NM_EV_ALL_PROBED                 :       return  "NM_EV_ALL_PROBED";
  case NM_EV_NODE_ADD_SMART_START       :       return  "NM_EV_NODE_ADD_SMART_START";
  case NM_EV_TX_DONE_SELF_DESTRUCT      :       return  "NM_EV_TX_DONE_SELF_DESTRUCT";
  case NM_EV_REMOVE_FAILED_OK           :       return  "NM_EV_REMOVE_FAILED_OK";
  case NM_EV_REMOVE_FAILED_FAIL         :       return "NM_EV_REMOVE_FAILED_FAIL";
  case NM_EV_ADD_NODE_STATUS_SFLND_DONE :       return "NM_EV_ADD_NODE_STATUS_SFLND_DONE";
  case NM_EV_NEIGHBOR_UPDATE_AFTER_SECURE_ADD_DONE: return "NM_EV_NEIGHBOR_UPDATE_AFTER_SECURE_ADD_DONE";

  default:
    sprintf(str, "%d", ev);
    return str;
  }
}

const char *s2_inclusion_event_name(int state)
{
  switch (state) {
    STR_CASE(S2_NODE_INCLUSION_INITIATED_EVENT)
    STR_CASE(S2_NODE_INCLUSION_KEX_REPORT_EVENT)
    STR_CASE(S2_NODE_INCLUSION_PUBLIC_KEY_CHALLENGE_EVENT)
    STR_CASE(S2_NODE_INCLUSION_COMPLETE_EVENT)
    STR_CASE(S2_NODE_JOINING_COMPLETE_EVENT)
    STR_CASE(S2_NODE_INCLUSION_FAILED_EVENT)
    STR_CASE(S2_JOINING_COMPLETE_NEVER_STARTED_EVENT)
   default:
      LOG_PRINTF("Unknown state: %d", state);
      return "Unknown";
  }
}



