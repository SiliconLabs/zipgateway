/* © 2018 Silicon Laboratories Inc.  */

#include "RD_probe_cc_version.h"
#include "RD_types.h"
#include "ZW_SendRequest.h"
#include "ResourceDirectory.h"
#include "NodeCache.h"
#include "ZIP_Router_logging.h"
#include "zw_network_info.h"
#include "RD_DataStore.h"
#include "ZW_classcmd_ex.h"
#include <stdbool.h>
#include <stdlib.h>

extern cc_version_pair_t controlled_cc_v[];
static ZW_VERSION_COMMAND_CLASS_GET_FRAME v;
void pcv_fsm_post_event(rd_ep_database_entry_t *ep, pcv_event_t ev);

#ifndef TEST_PROBE_CC_VERSION
static
#endif
int cc_version_callback(BYTE txStatus, BYTE rxStatus,
    ZW_APPLICATION_TX_BUFFER *pCmd, WORD cmdLength, void* user)
{
  rd_ep_database_entry_t *ep = (rd_ep_database_entry_t*) user;
  rd_node_database_entry_t* n = ep ? ep->node : NULL; // get node if ep is not NULL
  if (!n) {
    ERR_PRINTF("ep or ep->node is NULL!!");
    return 0;
  }

  if (!pCmd && txStatus == TRANSMIT_COMPLETE_OK) {
    if (pCmd->ZW_VersionCommandClassReportFrame.requestedCommandClass == v.requestedCommandClass) {
      if (pCmd->ZW_VersionCommandClassReportFrame.commandClassVersion >= 0x01) {
        rd_node_cc_version_set(n, v.requestedCommandClass, pCmd->ZW_VersionCommandClassReportFrame.commandClassVersion);
        n->pcvs->probe_cc_idx++;
        n->pcvs->state = PCV_LAST_REPORT;
        pcv_fsm_post_event(ep, PCV_EV_VERSION_CC_REPORT_RECV);
        return 0;
      } else {
        ERR_PRINTF("Unknown version of Version command class.\n");
        n->probe_flags = RD_NODE_FLAG_PROBE_FAILED;
      }
    } else {
      WRN_PRINTF("Version report(%02x) is not the requested command class: %02x.\n",
          pCmd->ZW_VersionCommandClassReportFrame.requestedCommandClass, v.requestedCommandClass);
      n->probe_flags = RD_NODE_FLAG_PROBE_FAILED;
    }
  } else {
    WRN_PRINTF("cc_version_callback failed.\n");
    n->probe_flags = RD_NODE_FLAG_PROBE_FAILED;
  }
  n->pcvs->probe_cc_idx++;
  n->pcvs->state = PCV_LAST_REPORT;
  pcv_fsm_post_event(ep, PCV_EV_VERSION_CC_CALLBACK_FAIL);
  return 0;
}

#ifndef TEST_PROBE_CC_VERSION
static
#endif
int version_capabilities_callback(BYTE txStatus, BYTE rxStatus,
    ZW_APPLICATION_TX_BUFFER *pCmd, WORD cmdLength, void* user)
{
  rd_ep_database_entry_t *ep = (rd_ep_database_entry_t*) user;
  rd_node_database_entry_t* n = ep ? ep->node : NULL; // get node if ep is not NULL
  if (!n) {
    ERR_PRINTF("ep or ep->node is NULL!!");
    return 0;
  }
  if (!pCmd && txStatus == TRANSMIT_COMPLETE_OK) {
    n->node_version_cap_and_zwave_sw = ((ZW_VERSION_CAPABILITIES_REPORT_V3_FRAME*)pCmd)->properties;
    n->pcvs->state = PCV_SEND_VERSION_ZWS_GET;
    pcv_fsm_post_event(ep, PCV_EV_VERSION_CAP_REPORT_RECV);
  } else {
    ERR_PRINTF("version_capabilities_callback failed.\n");
    n->probe_flags = RD_NODE_FLAG_PROBE_FAILED;
    n->pcvs->state = PCV_VERSION_PROBE_DONE;
    pcv_fsm_post_event(ep, PCV_EV_VERSION_CAP_CALLBACK_FAIL);
  }
  return 0;
}

#ifndef TEST_PROBE_CC_VERSION
static
#endif
int version_zwave_software_callback(BYTE txStatus, BYTE rxStatus,
    ZW_APPLICATION_TX_BUFFER *pCmd, WORD cmdLength, void* user)
{
  rd_ep_database_entry_t *ep = (rd_ep_database_entry_t*) user;
  rd_node_database_entry_t *n = ep ? ep->node : NULL; // get node if ep is not NULL
  if (!n) {
    ERR_PRINTF("ep or ep->node is NULL!!");
    return 0;
  }

  if (txStatus == TRANSMIT_COMPLETE_OK) {
    /* TODO Do we have the need to save ZWS report? */
    //memcpy(n->node_version_cap_and_zwave_sw, &(pCmd->ZW_VersionCapabilitiesReportV3Frame.sdkVersion1), sizeof(ZW_VersionCapabilitiesReportV3Frame) - 2);
    n->node_is_zws_probed = 1;
    n->pcvs->state = PCV_VERSION_PROBE_DONE;
    pcv_fsm_post_event(ep, PCV_EV_VERSION_ZWS_REPORT_RECV);
  } else {
    ERR_PRINTF("Version Z-Wave software report transmit failed.\n");
    n->pcvs->state = PCV_VERSION_PROBE_DONE;
    pcv_fsm_post_event(ep, PCV_EV_VERSION_ZWS_CALLBACK_FAIL);
  }
  return 0;
}

void rd_ep_probe_cc_version(rd_ep_database_entry_t *ep, _pcvs_callback callback)
{
  rd_node_database_entry_t *n = ep ? ep->node : NULL;
  if (!n) {
    ERR_PRINTF("ep or ep->node is NULL!!");
    return;
  }

  if (n->nodeid == MyNodeID) {
    /* We don't probe ourself for version */
    callback(ep, 0);
  } else {
    LOG_PRINTF("Probing version of command class for node %d ep=%d.\n", n->nodeid, ep->endpoint_id);
    n->probe_flags = RD_NODE_FLAG_PROBE_STARTED;
    if(!n->pcvs) {
      n->pcvs = malloc(sizeof(ProbeCCVersionState_t));
    }
    n->pcvs->callback = callback;
    n->pcvs->state = PCV_IDLE;
    pcv_fsm_post_event(ep, PCV_EV_INITIAL);
  }
}

void pcv_fsm_post_event(rd_ep_database_entry_t *ep, pcv_event_t ev)
{
  /* Check all the controlled CC and see if we've probed. If not, try to re-probe. */
  rd_node_database_entry_t *n = ep ? ep->node : NULL;
  if (!n) {
    ERR_PRINTF("ep or ep->node is NULL!!");
    return;
  }
  ts_param_t p;
  ProbeCCVersionState_t *pcvs = n->pcvs;

  switch(pcvs->state) {
    case PCV_IDLE:
      if(ev == PCV_EV_INITIAL) {
        pcvs->probe_cc_idx = 0;
        pcvs->state = PCV_SEND_VERSION_CC_GET;
        pcv_fsm_post_event(ep, PCV_EV_START);
      }
      break;
    case PCV_SEND_VERSION_CC_GET:
      //DBG_PRINTF("Probing version of 0x%02x command class for node %d.\n", controlled_cc_v[pcvs->probe_cc_idx].command_class, n->nodeid);
      if(ev == PCV_EV_START || ev == PCV_EV_NOT_LAST) {
        /* Only probe for those CC controlled by GW and supported by this endpoint. */
        if(rd_ep_class_support(ep, controlled_cc_v[pcvs->probe_cc_idx].command_class) & SUPPORTED) {
          /* Only probe if the version is unknowni, i.e. 0) */
          if(rd_node_cc_version_get(n, controlled_cc_v[pcvs->probe_cc_idx].command_class) == 0) {
            v.cmdClass = COMMAND_CLASS_VERSION;
            v.cmd = VERSION_COMMAND_CLASS_GET;
            v.requestedCommandClass = controlled_cc_v[pcvs->probe_cc_idx].command_class;
            ts_set_std(&p, n->nodeid);
            if(!ZW_SendRequest(&p, (BYTE*) &v, sizeof(v), VERSION_COMMAND_CLASS_REPORT, 3*20, ep, cc_version_callback)) {
              ERR_PRINTF("Transmit probing version of 0x%02x command class for node %d failed.\n", controlled_cc_v[pcvs->probe_cc_idx].version, n->nodeid);
              n->probe_flags = RD_NODE_FLAG_PROBE_FAILED;
            }
          } else {
            //DBG_PRINTF("Version of 0x%02x command class had been probed. Move to the next one.\n", controlled_cc_v[pcvs->probe_cc_idx].command_class);
            pcvs->probe_cc_idx++;
            pcvs->state = PCV_LAST_REPORT;
            pcv_fsm_post_event(ep, PCV_EV_CC_PROBED);
          }
        } else {
          //DBG_PRINTF("Skip probing version for 0x%02x command class since it's not supportedi by the node %d.\n", controlled_cc_v[pcvs->probe_cc_idx].command_class, n->nodeid);
          pcvs->probe_cc_idx++;
          pcvs->state = PCV_LAST_REPORT;
          pcv_fsm_post_event(ep, PCV_EV_CC_NOT_SUPPORT);
        }
      }
      break;
    case PCV_LAST_REPORT:
      /* Move onto the next state if we finish all the required version probing. */
      if(ev == PCV_EV_VERSION_CC_REPORT_RECV
          || ev == PCV_EV_CC_PROBED
          || ev == PCV_EV_CC_NOT_SUPPORT
          || ev == PCV_EV_VERSION_CC_CALLBACK_FAIL) {
        if(controlled_cc_v[pcvs->probe_cc_idx].command_class != 0xffff) {
          pcvs->state = PCV_SEND_VERSION_CC_GET;
          pcv_fsm_post_event(ep, PCV_EV_NOT_LAST);
        } else {
          pcvs->state = PCV_CHECK_IF_V3;
          pcv_fsm_post_event(ep, PCV_EV_VERSION_CC_DONE);
        }
      }
      break;
    case PCV_CHECK_IF_V3:
      /* If the version CC is V3, probe version_cap_get and ZWS */
      if(ev == PCV_EV_VERSION_CC_DONE) {
        if(rd_node_cc_version_get(n, COMMAND_CLASS_VERSION) == VERSION_VERSION_V3) {
          pcvs->state = PCV_SEND_VERSION_CAP_GET;
          pcv_fsm_post_event(ep, PCV_EV_IS_V3);
        } else {
          pcvs->state = PCV_VERSION_PROBE_DONE;
          pcv_fsm_post_event(ep, PCV_EV_NOT_V3);
        }
      }
      break;
    case PCV_SEND_VERSION_CAP_GET:
      if(ev == PCV_EV_IS_V3) {
        if(n->node_version_cap_and_zwave_sw == 0) {
          DBG_PRINTF("Node %d supports Version V3. Probing version capabilities.\n", n->nodeid);
          static ZW_VERSION_CAPABILITIES_GET_V3_FRAME v_cap_get;
          v_cap_get.cmdClass = COMMAND_CLASS_VERSION;
          v_cap_get.cmd = VERSION_CAPABILITIES_GET;
          ts_set_std(&p, n->nodeid);
          if(!ZW_SendRequest(&p, (BYTE*) &v_cap_get, sizeof(v_cap_get), VERSION_CAPABILITIES_REPORT, 3*20, ep, version_capabilities_callback)) {
            ERR_PRINTF("Version capabilities probe failed.\n");
            n->probe_flags = RD_NODE_FLAG_PROBE_FAILED;
          }
        } else {
          pcvs->state = PCV_SEND_VERSION_ZWS_GET;
          pcv_fsm_post_event(ep, PCV_EV_CAP_PROBED);
        }
      }
      break;
    case PCV_SEND_VERSION_ZWS_GET:
      /* Probe for ZWS only if it's supported. */
      if(ev == PCV_EV_VERSION_CAP_REPORT_RECV || ev == PCV_EV_CAP_PROBED) {
        if((n->node_version_cap_and_zwave_sw & VERSION_CAPABILITIES_REPORT_V)
            && (n->node_version_cap_and_zwave_sw & VERSION_CAPABILITIES_REPORT_CC)
            && (n->node_version_cap_and_zwave_sw & VERSION_CAPABILITIES_REPORT_ZWS)) {
          if(n->node_is_zws_probed == 0) {
            DBG_PRINTF("Probing version zwave software for node %d.\n", n->nodeid);
            static ZW_VERSION_ZWAVE_SOFTWARE_GET_V3_FRAME v_zws_get;
            v_zws_get.cmdClass = COMMAND_CLASS_VERSION;
            v_zws_get.cmd = VERSION_ZWAVE_SOFTWARE_GET;
            ts_set_std(&p, n->nodeid);
            if(!ZW_SendRequest(&p, (BYTE*) &v_zws_get, sizeof(v_zws_get), VERSION_ZWAVE_SOFTWARE_REPORT, 3*20, ep, version_zwave_software_callback)) {
              ERR_PRINTF("Version zwave software probe failed.\n");
              n->probe_flags = RD_NODE_FLAG_PROBE_FAILED;
            }
          } else {
            pcvs->state = PCV_VERSION_PROBE_DONE;
            pcv_fsm_post_event(ep, PCV_EV_ZWS_PROBED);
          }
        } else {
          pcvs->state = PCV_VERSION_PROBE_DONE;
          pcv_fsm_post_event(ep, PCV_EV_ZWS_NOT_SUPPORT);
        }
      }
      break;
    case PCV_VERSION_PROBE_DONE:
      if(ev == PCV_EV_ZWS_NOT_SUPPORT
          || ev == PCV_EV_VERSION_ZWS_REPORT_RECV
          || ev == PCV_EV_NOT_V3
          || ev == PCV_EV_VERSION_ZWS_CALLBACK_FAIL
          || ev == PCV_EV_VERSION_CAP_CALLBACK_FAIL
          || ev == PCV_EV_ZWS_PROBED) {
        LOG_PRINTF("Probing CC version done for node %d ep=%d.\n", n->nodeid, ep->endpoint_id);
        /* Mark it as DONE if none of the probing failed. */
        if(n->probe_flags == RD_NODE_FLAG_PROBE_FAILED) {
          pcvs->callback(ep, 1);
        } else {
          n->probe_flags = RD_NODE_FLAG_PROBE_HAS_COMPLETED;
          pcvs->callback(ep, 0);
        }
      }
      break;
    default:
      break;
  }
  return;
}
