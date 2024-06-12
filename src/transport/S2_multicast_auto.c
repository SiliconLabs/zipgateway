/* © 2019 Silicon Laboratories Inc. */

#include "ZIP_Router_logging.h"
#include "ZW_typedefs.h"
#include "ZW_classcmd.h"

#include "S2_wrap.h"
#include "sys/cc.h"
#include "NodeCache.h"
#include "zgw_nodemask.h"

static const security_scheme_t scheme_state_map[] = { NO_SCHEME,
    SECURITY_SCHEME_2_UNAUTHENTICATED, SECURITY_SCHEME_2_AUTHENTICATED,
    SECURITY_SCHEME_2_ACCESS, SECURITY_SCHEME_2_UNAUTHENTICATED,
    SECURITY_SCHEME_2_AUTHENTICATED, SECURITY_SCHEME_2_ACCESS, };

//Maps into auto_mc_state.sub_mask
static const int node_mask_map[] = { 0, 1, 2, 3, 1, 2, 3 };

struct {
  //NOTE MC_AUTO_IDLE must be the last element
  enum {
    MC_AUTO_NO_SCHEME,
    MC_AUTO_C0,
    MC_AUTO_C1,
    MC_AUTO_C2,
    MC_AUTO_C0_FU,
    MC_AUTO_C1_FU,
    MC_AUTO_C2_FU,
    MC_AUTO_IDLE
  } state, termination_state;

  ts_param_t ts;
  const uint8_t *data;
  uint8_t data_len;
  nodemask_t sub_mask[4];

  ZW_SendDataAppl_Callback_t s2_send_callback_auto;
  void * user;
  uint8_t abort; //Should we abort the current transmission
} auto_mc_state;

static const nodemask_t empty_node_list = { 0 };

static void s2_send_callback_auto(BYTE txStatus, void* user,
    TX_STATUS_TYPE *txStatEx) {

  //Find next state to use
  if (auto_mc_state.state == MC_AUTO_IDLE) {
    auto_mc_state.state = MC_AUTO_NO_SCHEME;
  } else if ((auto_mc_state.state == auto_mc_state.termination_state)
      || (auto_mc_state.abort)) {
    //we are done, this was the last pass
    auto_mc_state.state = MC_AUTO_IDLE;
    auto_mc_state.s2_send_callback_auto(txStatus, auto_mc_state.user, txStatEx);
    return;
  } else {
    auto_mc_state.state++; //Next sublist
  }

  /* FIXME-MCAST: If we want to do like singlecast, we should find
     highest scheme here, not just check the flags of the node.
     Alternately, we could simplify singlecast. */
  auto_mc_state.ts.scheme = scheme_state_map[auto_mc_state.state];
  nodemask_copy(auto_mc_state.ts.node_list,
      auto_mc_state.sub_mask[node_mask_map[auto_mc_state.state]]);

  int send_sc_followups = auto_mc_state.state > MC_AUTO_C2;

  //If there is noting to send skip this step
  if (nodemask_equal(auto_mc_state.ts.node_list,
      empty_node_list) == 0) {
    s2_send_callback_auto(txStatus, user, txStatEx);
    return;
  }

  //We have something to send:
  if (auto_mc_state.ts.scheme == NO_SCHEME) {
    //TODO
    WRN_PRINTF(
        "I'm not sending the non-secure multicast .... Implement me....");
    s2_send_callback_auto(txStatus, user, txStatEx);
  } else {
    if (!sec2_send_multicast(&auto_mc_state.ts, auto_mc_state.data,
        auto_mc_state.data_len, send_sc_followups, s2_send_callback_auto,
        user)) {
      ERR_PRINTF(
          "sec2_send_multicast return false... not supposed to happen\n");
      s2_send_callback_auto(txStatus, user, txStatEx);
    }
  }
}

uint8_t sec2_send_multicast_auto_split(ts_param_t *p, const uint8_t *data,
    uint8_t data_len, BOOL send_sc_followups,
    ZW_SendDataAppl_Callback_t callback, void *user) {

  if (auto_mc_state.state != MC_AUTO_IDLE) {
    return 0;
  }

  auto_mc_state.abort = 0;
  auto_mc_state.s2_send_callback_auto = callback;
  auto_mc_state.user = user;
  auto_mc_state.ts = *p;
  auto_mc_state.termination_state =
      send_sc_followups ? MC_AUTO_C2_FU : MC_AUTO_C2;

  for (int i = 0; i < 4; i++) {
    nodemask_clear(auto_mc_state.sub_mask[i]);
  }

  for (int n = 1; n <= ZW_MAX_NODES; n++) {

    if (nodemask_test_node(n, p->node_list)) {
      uint8_t node_scheme_mask = GetCacheEntryFlag(n);
      if (node_scheme_mask & NODE_FLAG_SECURITY2_ACCESS) {
        nodemask_add_node(n, auto_mc_state.sub_mask[MC_AUTO_C2]);
      } else if (node_scheme_mask & NODE_FLAG_SECURITY2_AUTHENTICATED) {
        nodemask_add_node(n, auto_mc_state.sub_mask[MC_AUTO_C1]);
      } else if (node_scheme_mask & NODE_FLAG_SECURITY2_UNAUTHENTICATED) {
        nodemask_add_node(n, auto_mc_state.sub_mask[MC_AUTO_C0]);
      } else if (0 == (node_scheme_mask & NODE_FLAGS_SECURITY)) {
	/* FIXME-MCAST: we should probably handle secure separately
	   from non-secure here, to allow for status fail to be
	   returned or for singlecast handling of the secure nodes, as
	   proposed during spec devel.  First version could be to
	   block multicast commands that contain secure nodes when
	   doing the tlv parsing, but that is probably not where we
	   want to end up. */
        nodemask_add_node(n, auto_mc_state.sub_mask[MC_AUTO_NO_SCHEME]);
      }
    }
  }
  s2_send_callback_auto(0, user, 0);
  return 1;
}

void sec2_send_multicast_auto_split_abort() {
  /* FIXME-MCAST: It might be enough to set .state= .termination_state*/
  auto_mc_state.abort = 1;
}

void sec2_send_multicast_auto_init() {
  /* FIXME-MCAST: Find out where to call this function.  Probably in
     ZW_SendDataAppl_init(). */

  auto_mc_state.state = MC_AUTO_IDLE;
}
