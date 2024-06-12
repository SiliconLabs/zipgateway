/* Copyright 2019  Silicon Laboratories Inc. */

#include <list.h>
#include <memb.h>
#include <uip-debug.h>

#include "ZIP_Router_logging.h" /* DBG_PRINTF */
#include "zip_router_ipv6_utils.h" /* nodeOfIP */
#include "router_events.h"
#include "Bridge_ip_assoc.h"
#include "ClassicZIPNode.h"
#include "DTLS_server.h"
#include "ZW_controller_bridge_api.h"
#include "DataStore.h"
#include "ResourceDirectory.h"
#include "NodeCache.h" /* SupportsCmdClass */

#include "node_queue.h"

/*
 * ****** STATIC FUNCTION PROTOTYPES ******
 */

static void handle_ip_association_set(zwave_connection_t* c,
                                      const u8_t* payload,
                                      const u8_t len,
                                      BOOL was_dtls);
static void handle_ip_association_get(zwave_connection_t* c,
                                      const u8_t* payload,
                                      const u8_t len);
static void handle_ip_association_remove(zwave_connection_t* c,
                                         const u8_t* payload,
                                         const u8_t len);
static bridge_return_status_t ip_assoc_create(ip_assoc_type_t type,
                                              ZW_COMMAND_IP_ASSOCIATION_SET *ip_assoc_set_cmd,
                                              ip_assoc_cb_func_t create_completed_cb,
                                              uint8_t was_dtls);
static ip_association_t * ip_assoc_create_local(ZW_COMMAND_IP_ASSOCIATION_SET *cmd);
static void ip_assoc_virtual_add_callback(uint8_t bStatus,
                                          nodeid_t orgID,
                                          nodeid_t newID);
static void ip_assoc_create_permanent_callback(ip_association_t *a);
static void ip_assoc_create_proxy_callback(ip_association_t *a);
static void ip_assoc_call_create_complete_cb(ip_association_t *a);
static void ip_assoc_add_to_association_table(ip_association_t *new_a);
static ip_association_t * ip_assoc_find_existing(ip_assoc_type_t type,
                                                 uint8_t was_dtls);
static void ip_assoc_setup_and_save(ip_association_t *new_assoc,
                                    const ZW_COMMAND_IP_ASSOCIATION_SET *ip_assoc_set_cmd);
static void ip_assoc_remove(ip_association_t *a);
static void ip_assoc_remove_callback(uint8_t bStatus,
                                     void* user,
                                     TX_STATUS_TYPE *t);
static void ip_assoc_remove_and_persist(ip_association_t *a);
static void ip_assoc_find_and_mark_for_removal(nodeid_t han_nodeid,
                                               uint8_t han_endpoint,
                                               uint8_t grouping_id,
                                               const uip_ip6addr_t *resource_ip,
                                               uint8_t resource_endpoint);
static void ip_assoc_remove_all_marked_for_removal(void);
static ip_association_t * ip_assoc_find_first_marked_for_removal(void);
static void ip_assoc_continue_with_next_marked_for_removal(uint8_t bStatus,
                                                           nodeid_t orgID,
                                                           nodeid_t newID);
static BOOL ip_assoc_is_any_marked_for_removal(void);
static void ip_assoc_abort(ip_association_t *a, uint8_t bStatus);
static uint8_t ip_assoc_find_by_grouping(nodeid_t han_nodeid,
                                         uint8_t han_endpoint,
                                         uint8_t grouping_id,
                                         uint8_t index,
                                         ip_association_t **result);
static void assign_return_route_callback(unsigned char bStatus);
static void send_ip_association_cmd(ip_association_t *a,
                                    nodeid_t dest_nodeid,
                                    uint16_t len);
static void send_ip_association_cmd_callback(unsigned char bStatus,
                                             void* user,
                                             TX_STATUS_TYPE *t);
static uint8_t ip_assoc_setup_multi_channel_assoc_set(uint8_t* p,
                                                      nodeid_t assoc_dest_node,
                                                      ZW_COMMAND_IP_ASSOCIATION_SET *cmd);
static int ip_assoc_setup_classic_assoc_set_cmd(uint8_t* p,
                                                ZW_COMMAND_IP_ASSOCIATION_SET *ip_assoc_cmd);
static uint8_t ip_assoc_setup_classic_assoc_remove_cmd(uint8_t *p,
                                                       ip_association_t *ipa);
static uint8_t ip_assoc_setup_multi_channel_assoc_remove_cmd(uint8_t *p,
                                                             ip_association_t *ipa);
static void ip_assoc_setup_and_send_assoc_remove_cmd(ip_association_t *ipa);
static void report_send_complete_callback_wrapper(uint8_t bStatus,
                                                  void* user,
                                                  TX_STATUS_TYPE *t);
static const char * ip_assoc_type2str(ip_assoc_type_t type);

/*
 * ****** STATIC VARIABLES ******
 */

LIST(ip_association_table);
MEMB(ip_association_pool, ip_association_t, MAX_IP_ASSOCIATIONS);

/* ZW_AssignReturnRoute() does not support passing a user parameter to the
 * provided callback function. The following global variable is used instead. */
static ip_association_t *ip_association_for_assign_return_route_cb;

static uint16_t number_of_ip_assocs_marked_for_removal;

/** 
 * Callback function to call when IP association creation is over. Also acts
 * as access control to the bridge module via is_assoc_create_in_progress(),
 * since no new association create calls are allowed when the callback pointer
 * is non-null
 */
static ip_assoc_cb_func_t ip_assoc_create_complete_cb;

/**
 * Used for passing IP Association Set across asynchronous call to callback func
 * Set .cmd to NULL when not in use.
 */
static struct
{
  ZW_COMMAND_IP_ASSOCIATION_SET *cmd;
  ip_assoc_type_t type;
  uint8_t was_dtls;
} ip_assoc_virtual_add_callback_state;


/*
 * ****** EXPORTED FUNCTIONS ******
 */

void ip_assoc_init(void)
{
  list_init(ip_association_table);
  memb_init(&ip_association_pool);

  ip_assoc_virtual_add_callback_state.cmd = NULL;

  ip_assoc_create_complete_cb = NULL;
}

/* Documented in header file */
void ip_assoc_persist_association_table(void)
{
  rd_data_store_persist_associations(ip_association_table);
}

/* Documented in header file */
void ip_assoc_unpersist_association_table(void)
{
  ASSERT(*ip_association_table == NULL); /* Make sure we dont leak memory */

  rd_datastore_unpersist_association(ip_association_table, &ip_association_pool);
}

BOOL handle_ip_association(zwave_connection_t* c,
                              const u8_t* payload,
                              u8_t len,
                              BOOL was_dtls)
{
  /* Reset uip_len since periodic timers expect it to be zero
   * TODO: This should perhaps happen earlier when we copy into backup_buf
   * and backup_len
   * I'm doing it only for IP Assoc to test the waters first */
  uip_len = 0;

  if((get_queue_state() == QS_SENDING_FIRST) && (payload[1] != ASSOCIATION_GET))
  {
    /* Slow operation, must go in the long queue.
     * When rejected, it will return from the long queue later*/
    ClassicZIPNode_CallSendCompleted_cb(TRANSMIT_COMPLETE_NO_ACK, NULL, NULL);
    return TRUE;
  }

  /* TODO: Check if node/endpoint suppoerts association cc */
  switch (payload[1])
  {
    case ASSOCIATION_SET:
      handle_ip_association_set(c,payload,len,was_dtls);
      return TRUE;
      break;

    case ASSOCIATION_REMOVE:
      handle_ip_association_remove(c,payload,len);
      return TRUE;
      break;

    case ASSOCIATION_GET:
      handle_ip_association_get(c,payload,len);
      return TRUE;
      break;

    default:
      /* Unexpected cmd */
      report_send_completed(TRANSMIT_COMPLETE_OK);
      return TRUE;
      break;
  }
  return FALSE;
}

void ip_assoc_remove_by_nodeid(nodeid_t node_id)
{
  // DBG_PRINTF("------ip_assoc_remove_by_nodeid %d\n", node_id);
  ip_assoc_find_and_mark_for_removal(node_id, 0, 0, 0, 0);

  if (ip_assoc_is_any_marked_for_removal()) 
  {
    // DBG_PRINTF("------ ip_assoc_is_any_marked_for_removal() == TRUE\n");
    ip_assoc_remove_all_marked_for_removal();
  }
  else {
    // DBG_PRINTF("------ ip_assoc_is_any_marked_for_removal() == FALSE\n");
    /* Trigger the resource directory update */
    process_post(&zip_process, ZIP_EVENT_NM_VIRT_NODE_REMOVE_DONE, 0);
  }
}

void ip_assoc_print_association_table(void)
{
  LOG_PRINTF("Number of IP associations: %d\n", list_length(ip_association_table));

  uint8_t line_no = 1;
  for (ip_association_t *ia = list_head(ip_association_table); ia != NULL; ia = list_item_next(ia), line_no++)
  {
    print_association_list_line(
        line_no,
        ia->resource_endpoint,
        uip_ntohs(ia->resource_port),
        &ia->resource_ip,
        ia->virtual_id,
        ia->virtual_endpoint,
        ia->han_nodeid,
        ia->han_endpoint,
        ip_assoc_type2str(ia->type));
  }
}

BOOL is_ip_assoc_create_in_progress(void)
{
  return (ip_assoc_create_complete_cb != NULL) ? TRUE : FALSE;
}

/*
 * ****** STATIC FUNCTIONS ******
 */

/**
 * Handle an incoming IP Association Set packet.
 *
 * Ground rule: You MUST call report_send_completed() to clean up after passing
 * through here.
 */
static void
handle_ip_association_set(zwave_connection_t* c,
                          const u8_t* payload,
                          const u8_t len,
                          BOOL was_dtls)
{
  uip_ip6addr_t *assoc_dest_ip;
  u8_t assoc_dest_ep;
  int i = 0;
  nodeid_t dest_nodeid;
  rd_ep_database_entry_t *ep;

  ZW_COMMAND_IP_ASSOCIATION_SET *ip_assoc_set_cmd = (ZW_COMMAND_IP_ASSOCIATION_SET*)payload;

  assoc_dest_ip = &ip_assoc_set_cmd->resourceIP;
  assoc_dest_ep = ip_assoc_set_cmd->endpoint;
  dest_nodeid = nodeOfIP(&c->lipaddr);
  /* r->lendpoint contains the destination endpoint of the ZIP Packet which
   * is basically ASN endpoint
   */
  ep = rd_get_ep(dest_nodeid, c->lendpoint);
  if (!ep) {
    ASSERT(0);
    goto abort;
  }

 
  if (is_local_address(assoc_dest_ip))
  {
    /* Send either a multi-channel association set or an association set */
    /* ASN = Association Source Node
     * ADN = Association Destination Node
     *
     * There are 5 sub-cases here:
     *  Case 1: Single ASN, Single ADN -> Classic Assoc Set
     *  Case 2: Single ASN no multi support, Multi ADN -> Classic Assoc Set
     *          (ZIP Roter must proxy and provide Multichannel encap)
     *  Case 3: Multi ASN, Single ADN -> Multi-channel encapsulated Classic Assoc Set
     *  Case 4: Multi ASN, Multi ADN -> Multi-channel encapsulated Multi Channel Assoc Set
     *  Case 5: Like case 2, but ASN supports Multi Channel Assoc Set
     *          command class -> Multi Channel Assoc Set
     */

    if (c->lendpoint == 0 && assoc_dest_ep == 0) {
      /* if the ASN endpoint and ADN endpoint are both zero (case 1).
       * We expect to sen Classic Assoc Set and we must check if the destination
       * node supports COMMAND_CLASS_ASSOCIATION_V2
       */
      if (!(rd_ep_class_support(ep, COMMAND_CLASS_ASSOCIATION_V2) & SUPPORTED)) {
        ASSERT(0);
        goto abort;
      }
    } else if (c->lendpoint == 0 && assoc_dest_ep != 0) {
      /* if the ASN endpoint is zero and ADN endpoint is NOT zero. (case 5)
       * We expect to send Multi channel Assoc Set and we must check if the
       * destination node supports COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V2
       */
      if(!(rd_ep_class_support(ep, COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V2) & SUPPORTED)) {
        ASSERT(0);
        goto abort;
      }
    } else if (c->lendpoint != 0  && assoc_dest_ep == 0) {
      /* if the ASN endpoint is NOT zero and ADN endpoint is zero. (case 3)
       * We expect to send Multi channel cmd encp classic Assoc Set and we must
       * check if the destination node supports 
       * COMMAND_CLASS_MULTI_CHANNEL_V2 and if the endpoint supports
       * COMMAND_CLASS_ASSOCIATION_V2
       */
      if((!SupportsCmdClass(dest_nodeid, COMMAND_CLASS_MULTI_CHANNEL_V2) &&
          !SupportsCmdClassSecure(dest_nodeid, COMMAND_CLASS_MULTI_CHANNEL_V2)) ||
         !(rd_ep_class_support(ep, COMMAND_CLASS_ASSOCIATION_V2) & SUPPORTED)) {
        ASSERT(0);
        goto abort;
      }
    } else {
      /* if the ASN endpoint is NOT zero and ADN endpoint NOT is zero. (case 4)
       * We expect to send Multi channel cmd encp multi channel Assoc Set and 
       * we must check if the destination node supports 
       * COMMAND_CLASS_MULTI_CHANNEL_V2  and the endpoint
       * COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V2 
       */
      if((!SupportsCmdClass(dest_nodeid, COMMAND_CLASS_MULTI_CHANNEL_V2) &&
          !SupportsCmdClassSecure(dest_nodeid, COMMAND_CLASS_MULTI_CHANNEL_V2)) ||
         !(rd_ep_class_support(ep, COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V2) & SUPPORTED)) {
        ASSERT(0);
        goto abort;
      }
    }

    if ((c->lendpoint == 0 && assoc_dest_ep != 0)
        && !SupportsCmdClass(dest_nodeid,
            COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V2)
        && !SupportsCmdClassSecure(dest_nodeid,
            COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V2)
       )
    {
      /* Case 2 */
      /* we must proxy this connection */
      if (ip_assoc_create(PROXY_IP_ASSOC,
                          ip_assoc_set_cmd,
                          ip_assoc_create_proxy_callback,
                          was_dtls) != BRIDGE_OK)
      {
        send_zip_ack(TRANSMIT_COMPLETE_ERROR);
        ASSERT(0);
        report_send_completed(TRANSMIT_COMPLETE_ERROR);
      }
      return;
    }

    ip_association_t *a = ip_assoc_create_local(ip_assoc_set_cmd);

    if (!a)
    {
      send_zip_ack(TRANSMIT_COMPLETE_ERROR);
      ASSERT(0);
      report_send_completed(TRANSMIT_COMPLETE_ERROR);
      return;
    }
    if (c->lendpoint == 0 && assoc_dest_ep == 0)
    {
      /* Case 1 */
      i += ip_assoc_setup_classic_assoc_set_cmd(ClassicZIPNode_getTXBuf(),
                                                ip_assoc_set_cmd);

      send_ip_association_cmd(a, a->han_nodeid, i);
      return;
    }
    else if (c->lendpoint == 0 && assoc_dest_ep != 0)
    {
      /* Case 5, send Multi Channel assoc set, no encap */
      i = ip_assoc_setup_multi_channel_assoc_set(ClassicZIPNode_getTXBuf(),
                                                 nodeOfIP(assoc_dest_ip),
                                                 ip_assoc_set_cmd);

      send_ip_association_cmd(a, a->han_nodeid, i);
      return;
    }
    else if (c->lendpoint != 0  && assoc_dest_ep == 0)
    {
      ZW_MULTI_CHANNEL_CMD_ENCAP_V2_FRAME *b =
          (ZW_MULTI_CHANNEL_CMD_ENCAP_V2_FRAME*) ClassicZIPNode_getTXBuf();
      /* Case 3 */
      b->cmdClass = COMMAND_CLASS_MULTI_CHANNEL_V2;
      b->cmd = MULTI_CHANNEL_CMD_ENCAP_V2;
      b->properties1 = 0;
      b->properties2 = c->lendpoint;
      i = 4;
      i += ip_assoc_setup_classic_assoc_set_cmd(ClassicZIPNode_getTXBuf() + i,
                                                ip_assoc_set_cmd);

      send_ip_association_cmd(a, a->han_nodeid, i);
      return;
    }
    else
    {
      /* Case 4*/
      ZW_MULTI_CHANNEL_CMD_ENCAP_V2_FRAME *b =
          (ZW_MULTI_CHANNEL_CMD_ENCAP_V2_FRAME*) ClassicZIPNode_getTXBuf();
      b->cmdClass = COMMAND_CLASS_MULTI_CHANNEL_V2;
      b->cmd = MULTI_CHANNEL_CMD_ENCAP_V2;
      b->properties1 = 0;
      b->properties2 = c->lendpoint;
      i = 4;
      /* TODO: Proxy if ASN doesn't support COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION. */
      i += ip_assoc_setup_multi_channel_assoc_set(ClassicZIPNode_getTXBuf() + i,
                                                  nodeOfIP(assoc_dest_ip),
                                                  ip_assoc_set_cmd);

      send_ip_association_cmd(a, a->han_nodeid, i);
      return;
    }
  }
  else /* if (is_local_address(assoc_dest_ip)) */
  {
    /* creating an IP Association that goes out of the PAN to a ZIP client; 
     * and NOT to another node in the PAN. These are called Permanant associations
     */

    if (c->lendpoint == 0) {
      if (!(rd_ep_class_support(ep, COMMAND_CLASS_ASSOCIATION_V2) & SUPPORTED)) {
        ASSERT(0);
        goto abort;
      }
    } else {
      if(!(rd_ep_class_support(ep, COMMAND_CLASS_MULTI_CHANNEL_V2) & SUPPORTED) &&
         !(rd_ep_class_support(ep, COMMAND_CLASS_ASSOCIATION_V2) & SUPPORTED)) {
        ASSERT(0);
        goto abort;
      }
    }

    /* TODO Just keeping this code here. Incase if we ever implement this. */
    /* In theory we should make this final check too, but the code currently will never use endpoints
     * on the virtual node. Because the virtual_endpoint field of the ip_association_t struct is never set nonzero
     * It would be a cool optimization, though. And if we ever add it, we need to check for support .
     */
#if 0
    if (are_we_going_to_use_endpoints_on_the_virtual_node() == TRUE) {
      if(!SupportsCmdClass(dest_nodeid, COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V2) &&
        !SupportsCmdClassSecure(dest_nodeid, COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V2)) {
        ASSERT(0);
        goto abort;
      }
    }
#endif
    if (ip_assoc_create(PERMANENT_IP_ASSOC,
                        ip_assoc_set_cmd,
                        ip_assoc_create_permanent_callback,
                        was_dtls) != BRIDGE_OK)
    {
        ASSERT(0);
        goto abort;
    }
  }
  return;
abort:
  send_zip_ack(TRANSMIT_COMPLETE_ERROR);
  report_send_completed(TRANSMIT_COMPLETE_ERROR);
  return;
}

static void
handle_ip_association_get(zwave_connection_t* c, const u8_t* payload, const u8_t len)
{
  ip_association_t *a;
  ZW_IP_ASSOCIATION_GET_FRAME *req =(ZW_IP_ASSOCIATION_GET_FRAME *) payload;
  ZW_IP_ASSOCIATION_REPORT_1BYTE_FRAME f;
  u8_t nodes_in_grouping;

  send_zip_ack(TRANSMIT_COMPLETE_OK);
  if(len < sizeof(ZW_IP_ASSOCIATION_GET_FRAME)) {
    report_send_completed(TRANSMIT_COMPLETE_OK);
  }

  nodes_in_grouping = ip_assoc_find_by_grouping(nodeOfIP(&c->lipaddr), c->lendpoint, req->groupingIdentifier, req->index, &a);

  memset(&f,0,sizeof(f));
  f.cmdClass = COMMAND_CLASS_IP_ASSOCIATION;
  f.cmd = ASSOCIATION_REPORT;
  f.groupingIdentifier = req->groupingIdentifier;
  f.index = req->index;
  f.actualNodes = nodes_in_grouping;
  if (a)
  {

    uip_ipaddr_copy((uip_ip6addr_t* )&f.associationDestinationIpv6Address1, &a->resource_ip);
    f.associationDestEndpoint = a->resource_endpoint;
    f.resourceName1 = 0; /* TODO: Support Symbolic naming*/
  }
  else
  {
    /* empty grouping or index out of bounds - report error */
  }

  ZW_SendDataZIP(c,
                 &f,
                 sizeof(ZW_IP_ASSOCIATION_REPORT_1BYTE_FRAME) - 2,
                 report_send_complete_callback_wrapper);
}

/**
 * Lookup the IP association requested by an IP Association Get command
 *
 * Get the IP association at the specified index position in the list of
 * elements matching han_nodeid, han_endpoint and grouping_id. Additionally
 * return the total count of IP associations in the list of matching elements.
 *
 * @param han_nodeid   HAN node id
 * @param han_endpoint HAN end point
 * @param grouping_id  Grouping identifier 
 * @param index        Index (one-based) into association table
 * @param result       Pointer to IP association (NULL if not found)
 *
 * @return Number of associations in grouping
 */
static uint8_t
ip_assoc_find_by_grouping(nodeid_t han_nodeid,
                          uint8_t han_endpoint,
                          uint8_t grouping_id,
                          uint8_t index,
                          ip_association_t **result)
{
  uint8_t nodes_in_grouping = 0;
  *result = NULL;

  for (ip_association_t *a = list_head(ip_association_table); a != NULL; a = list_item_next(a))
  {
    if ((a->han_nodeid == han_nodeid) && (a->han_endpoint == han_endpoint) && (a->grouping == grouping_id))
    {
      nodes_in_grouping++;
      if (--index == 0)
      {
        *result = a;
      }
    }
  }
  return nodes_in_grouping;
}

/**
 * Handle an incoming IP Association Remove packet.
 *
 * Note: We dont remove the return routes, because we cannot
 * remove them individually. Instead we just rely on explorer
 * routing to sort this out.
 */
static void
handle_ip_association_remove(zwave_connection_t* c, const u8_t* payload, const u8_t len)
{
  ZW_COMMAND_IP_ASSOCIATION_REMOVE *cmd = (ZW_COMMAND_IP_ASSOCIATION_REMOVE*)payload;
  uip_ip6addr_t zero_ip;
  /* TODO: Check lenght of frame */

  memset(&zero_ip, 0, sizeof(uip_ip6addr_t));
  ASSERT(cmd->cmd == IP_ASSOCIATION_REMOVE);
  ASSERT(cmd->cmd_class == COMMAND_CLASS_IP_ASSOCIATION);
  if (ip_assoc_is_any_marked_for_removal()) { /*Just locking further IP association removal till existing ones are finished */
    ERR_PRINTF("Still in the process of removing IP associations in response to previous IP_ASSOCIATION_REMOVE command.\n");
    send_zip_ack(TRANSMIT_COMPLETE_ERROR);
    report_send_completed(TRANSMIT_COMPLETE_ERROR);
    return;
  }
  ip_assoc_find_and_mark_for_removal(nodeOfIP(&c->lipaddr),
                                     c->lendpoint,
                                     cmd->grouping,
                                     &cmd->ip_addr,
                                     cmd->endpoint);
  if (ip_assoc_is_any_marked_for_removal()) {
      ip_assoc_remove_all_marked_for_removal();
      return;
  } else {
      LOG_PRINTF("Found no IP associations to remove.\n");
  }
  /* The Remove command was delivered successfully, but could not be processed due to invalid
   * contents. We still return _OK because we do not signal application layer errors in the ZIP
   * header. We treat it as strictly a transport layer mechanism. And delivery was successful. */
  send_zip_ack(TRANSMIT_COMPLETE_OK);
  report_send_completed(TRANSMIT_COMPLETE_OK); /* _OK because we dont want to see this frame again */
  return;
}

/**
 * Create an IP bridge association and corresponding virtual node
 * 
 * @param type Type of IP association to create.
 *             Must be PERMANENT_IP_ASSOC or PROXY_IP_ASSOC.
 *             Use ip_assoc_create_local() to create a local IP association.
 * @param ip_assoc_set_cmd    The IP association set command.
 * @param create_completed_cb Function to call when the ip association has
 *                            been created (or a failure detected).
 * @param was_dtls            Was the request from the ZIP client received via
 *                            DTLS?
 *
 * @return BRIDGE_OK or BRIDGE_FAIL
 */
static bridge_return_status_t
ip_assoc_create(ip_assoc_type_t type,
                ZW_COMMAND_IP_ASSOCIATION_SET *ip_assoc_set_cmd,
                ip_assoc_cb_func_t create_completed_cb,
                uint8_t was_dtls)
{
  ip_association_t *a = NULL;

  if (ip_assoc_set_cmd == NULL)
  {
    /* ip_assoc_set_cmd must be nonnull */
    ASSERT(0);
    return BRIDGE_FAIL;
  }

  if (is_assoc_create_in_progress())
  {
    /* It's an error to get here while a bridge session is already ongoing */
    ASSERT(0);
    return BRIDGE_FAIL;
  }

  ip_assoc_create_complete_cb = create_completed_cb;

  DBG_PRINTF("ip_assoc_create of %s type\n", ip_assoc_type2str(type));

  /* Note: It is not obvious from the find_existing_assoc() call parameters,
   *       but the function is also using the current contents of
   *       BACKUP_UIP_IP_BUF to lookup existing associations.
   *       These fields are used:
   *         --> destipaddr, srcipaddr, srcport, sEndpoint
   */
  a = ip_assoc_find_existing(type, was_dtls);
  if (a)
  {
    a->han_endpoint = BACKUP_ZIP_PKT_BUF->dEndpoint;
    ip_assoc_call_create_complete_cb(a);
  }
  else if (type == PERMANENT_IP_ASSOC || type == PROXY_IP_ASSOC)
  {
    DBG_PRINTF("creating a new virtual node\n");
    /* TODO: Add a guard timer in case this callback is lost */
    BYTE retVal;
    ASSERT(ip_assoc_virtual_add_callback_state.cmd == NULL);
    ip_assoc_virtual_add_callback_state.cmd = ip_assoc_set_cmd;
    ip_assoc_virtual_add_callback_state.type = type;
    ip_assoc_virtual_add_callback_state.was_dtls = was_dtls;
    retVal = ZW_SetSlaveLearnMode(0, VIRTUAL_SLAVE_LEARN_MODE_ADD, ip_assoc_virtual_add_callback);
    if (!retVal)
    {
      ip_assoc_virtual_add_callback_state.cmd = NULL;
      DBG_PRINTF("ZW_SetSlaveLearnMode() returned false\n");
      ip_assoc_call_create_complete_cb(NULL);
      return BRIDGE_FAIL;
    }
  }
  else
  {
    /* LOCAL_IP_ASSOC is unsupported here. Call ip_assoc_create_local() instead */
    ASSERT(type != LOCAL_IP_ASSOC);

    ERR_PRINTF("Unknown or unsupported IP association type.\n");
    return BRIDGE_FAIL;
  }

  return BRIDGE_OK;
}

/**
 * Create IP association of local type (between to HAN nodes)
 *
 * To use as backup for replacing failed node, and for storing association
 * source/destination during async setup of return routes.
 *
 * Must be called synchronously while the IP Association Set command is still in
 * uip_buf.
 *
 * @param cmd IP association set command
 * @return    New local IP association. 
 * @return    NULL if unable to allocate a new IP allocation element (pool empty)
 */
static ip_association_t *
ip_assoc_create_local(ZW_COMMAND_IP_ASSOCIATION_SET *cmd)
{
  ip_association_t *new_assoc = (ip_association_t *)memb_alloc(&ip_association_pool);
  if (!new_assoc)
  {
    DBG_PRINTF("failed to alloc new struct ip_association_t\n");
    return NULL;
  }
  ASSERT(is_local_address(&cmd->resourceIP));
  ASSERT(is_local_address(&BACKUP_UIP_IP_BUF->destipaddr));
  new_assoc->type = LOCAL_IP_ASSOC;
  new_assoc->resource_ip = cmd->resourceIP;
  new_assoc->resource_port = UIP_HTONS(DTLS_PORT);
  new_assoc->resource_endpoint = cmd->endpoint;
  new_assoc->grouping = cmd->groupingIdentifier;
  new_assoc->han_nodeid = nodeOfIP(&BACKUP_UIP_IP_BUF->destipaddr);
  new_assoc->han_endpoint = BACKUP_ZIP_PKT_BUF->dEndpoint;

  ip_assoc_add_to_association_table(new_assoc);
  ip_assoc_persist_association_table();

  return new_assoc;
}

/**
 * We have created a new virtual node. Proceed to create a permanent or
 * proxy IP association on it.
 */
static void
ip_assoc_virtual_add_callback(uint8_t bStatus, nodeid_t orgID, nodeid_t newID)
{
  ip_association_t *new_assoc = NULL;
  ASSERT(NULL != ip_assoc_virtual_add_callback_state.cmd);
  ZW_COMMAND_IP_ASSOCIATION_SET *set_cmd = ip_assoc_virtual_add_callback_state.cmd;
  ip_assoc_virtual_add_callback_state.cmd = NULL;

  DBG_PRINTF("ip_assoc_virtual_add_callback bStatus=%d\n", bStatus);
  switch (bStatus)
  {
  case ASSIGN_NODEID_DONE:
    if (set_cmd == NULL)
    {
      ASSERT(0);
      return;
    }

    DBG_PRINTF("Successfully preallocated a virtual node %d for permanent association\n", newID);
    BIT8_SET(newID - 1, virtual_nodes_mask);
    /* We have a virtual node, process an IP Association Set from txQueue
     * Note: This does not have to be the same IP Assoc Set that triggered virtual node
     * creation. */
    new_assoc = (ip_association_t *)memb_alloc(&ip_association_pool);
    if (new_assoc)
    {
      memset(new_assoc, 0, sizeof(ip_association_t));
      new_assoc->type = ip_assoc_virtual_add_callback_state.type;
      new_assoc->was_dtls = ip_assoc_virtual_add_callback_state.was_dtls;
      new_assoc->virtual_id = newID;
      ip_assoc_setup_and_save(new_assoc, set_cmd);
    }
    else
    {
      ip_assoc_call_create_complete_cb(NULL);
    }
    break;

  default:
  case ASSIGN_COMPLETE:
    /* It looks like the SerialAPI target emits this callback when virtual add fails
     * with no free nodeids. */
    /* Signal to txQueue module that we have permanently failed
     * to process this IP Association Set packet */
    ip_assoc_call_create_complete_cb(NULL);
    break;
  }
}

static void
ip_assoc_create_permanent_callback(ip_association_t *a)
{
  /* create association from .dest_endpoint to .assoc_endpoint to
   * and send ZIP_ACK to .src_endpoint.
   * All endpoints in struct IP_Association
   */
  int i = 0;
  if (!a)
  {
    /* malloc or create virtual failed, abort */
    DBG_PRINTF("out of memory");
    send_zip_ack(TRANSMIT_COMPLETE_ERROR);
    ip_assoc_abort(NULL, TRANSMIT_COMPLETE_ERROR);
    return;
  }

  if (a->han_endpoint != 0)
  {
    ZW_MULTI_CHANNEL_CMD_ENCAP_V2_FRAME *b =
        (ZW_MULTI_CHANNEL_CMD_ENCAP_V2_FRAME*) ClassicZIPNode_getTXBuf();
    b->cmdClass = COMMAND_CLASS_MULTI_CHANNEL_V2;
    b->cmd = MULTI_CHANNEL_CMD_ENCAP_V2;
    b->properties1 = 0;
    b->properties2 = a->han_endpoint;
    i = 4;
  }

  if (a->virtual_endpoint != 0)
  {
    uint8_t *p;
    /* Use Multi Channel Association Set */
    p = ClassicZIPNode_getTXBuf() + i;
    *(p++) = COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V2;
    *(p++) = MULTI_CHANNEL_ASSOCIATION_SET_V2;
    *(p++) = a->grouping;
    *(p++) = a->virtual_id;
    *(p++) = MULTI_CHANNEL_ASSOCIATION_SET_MARKER_V2;
    *(p++) = a->virtual_id;
    *(p++) = a->virtual_endpoint;
    i += 7;
  }
  else
  {
    /* Use normal Association Set */
    ZW_ASSOCIATION_SET_1BYTE_V2_FRAME *assoc_frame = (void*) (ClassicZIPNode_getTXBuf() + i);
    assoc_frame->cmdClass = COMMAND_CLASS_ASSOCIATION;
    assoc_frame->cmd = ASSOCIATION_SET_V2;
    assoc_frame->groupingIdentifier = a->grouping;
    assoc_frame->nodeId1 = a->virtual_id;
    i += 4;
  }
  /* Send the frame. When callback comes back, setup return routes.
   * When that callback comes, send off ZIP_ACK. */
  send_ip_association_cmd(a, a->han_nodeid, i);
}

static void
ip_assoc_create_proxy_callback(ip_association_t *a)
{
  ASSERT(a);
  if (a)
  {
    uint16_t i = 0;
    uint8_t *p;

    ASSERT(a->grouping);
    p = ClassicZIPNode_getTXBuf();
    p[i++] = COMMAND_CLASS_ASSOCIATION_V2;
    p[i++] = ASSOCIATION_SET_V2;
    p[i++] = a->grouping;
    p[i++] = a->virtual_id;
    send_ip_association_cmd(a, a->han_nodeid, i);
  }
  else
  {
    DBG_PRINTF("out of memory");
    send_zip_ack(TRANSMIT_COMPLETE_ERROR);
    ip_assoc_abort(a, TRANSMIT_COMPLETE_ERROR);
  }
}

/**
 * Call registered callback function to report an IP association has been created
 * 
 * @param a The new IP association
 */
static void ip_assoc_call_create_complete_cb(ip_association_t *a)
{
  ip_assoc_cb_func_t callback_func = ip_assoc_create_complete_cb;
  ip_assoc_create_complete_cb = NULL;
  if (callback_func)
  {
    callback_func(a);
  }
  process_post(&zip_process, ZIP_EVENT_COMPONENT_DONE, 0);
}

/**
 * Add IP association to the IP association table
 *
 * If the specified IP association new_a is already in the association table it
 * will be returned to the association pool. I.e. don't reference the pointer
 * after this call.
 *
 * @param new_a IP association to add
 */
static void
ip_assoc_add_to_association_table(ip_association_t *new_a)
{
  /* Check for duplicates before adding. No need to release virtual nodeids,
   * only local associations (which dont use virtual nodes) are stopped here.
   * Permanent associations already have their own virtual id at this point, so
   * they would not be flagged as duplicates.
   */

  // Check if it's already in the ip_association table
  for (ip_association_t *a = list_head(ip_association_table); a != NULL; a = list_item_next(a))
  {
    // Compare the structs starting from element "virtual_id" (i.e. skipping the next pointer)
    if (0 == memcmp(&new_a->virtual_id,
                    &a->virtual_id,
                    sizeof(ip_association_t) - offsetof(ip_association_t, virtual_id)))
    {
      // New association is already in the table. Return item to pool.
      memb_free(&ip_association_pool, new_a);
      return;
    }
  }
  // New association was not found in the table. Add it.
  list_add(ip_association_table, new_a);
}

/**
 * Lookup an IP association of a specific type using properties of the Z/IP
 * package currently in the uIP buffer.
 *
 * \note In addition to the function parameters the lookup is also using the
 * content of the most recently received Z/IP package (currently in the uIP
 * backup buffer).
 *
 * @param type      IP association type
 * @param was_dtls  DTLS flag on the association
 * @return          The matching IP association
 * @return          NULL if not found.
 */
static ip_association_t *
ip_assoc_find_existing(ip_assoc_type_t type, uint8_t was_dtls)
{
  ip_association_t *a = NULL;
  nodeid_t destination_node = nodeOfIP(&(BACKUP_UIP_IP_BUF->destipaddr));

  DBG_PRINTF("ip_assoc_find_existing\n");

  for (a = list_head(ip_association_table); a != NULL; a = list_item_next(a))
  {
    if (
        a->type == type &&
        destination_node == a->han_nodeid &&
        uip_ipaddr_cmp(&a->resource_ip, &(BACKUP_UIP_IP_BUF->srcipaddr)) &&
        a->resource_port == BACKUP_UIP_UDP_BUF->srcport &&
        a->resource_endpoint == BACKUP_ZIP_PKT_BUF->sEndpoint &&
        a->was_dtls == was_dtls)
    {
      DBG_PRINTF("ip_assoc_find_existing() found: %p\n", a);
      return a;
    }
  }
  DBG_PRINTF("ip_assoc_find_existing() NOT found\n");
  return NULL;
}

/**
 * Finish the setup of an IP association
 *
 * Will set the association properties from the provided IP association SET
 * command and from the Z/IP package currently in the uIP backup buffer.
 *
 * Register the association in the IP association table and persist the table.
 *
 * Preconditions:
 *  - new_assoc->type has been set.
 *  - (for permanent assoc) a virtual node has been allocated and the node id is
 *    stored in new_assoc->virtual_id
 *
 * @param new_assoc        IP association struct to setup.
 * @param ip_assoc_set_cmd IP association SET command 
 */
static void
ip_assoc_setup_and_save(ip_association_t *new_assoc,
                        const ZW_COMMAND_IP_ASSOCIATION_SET *ip_assoc_set_cmd)
{
  new_assoc->han_nodeid = nodeOfIP(&(BACKUP_UIP_IP_BUF->destipaddr));
  ASSERT(new_assoc->han_nodeid != 0);
  new_assoc->han_endpoint = BACKUP_ZIP_PKT_BUF->dEndpoint;

  /* The following check is really not needed (this function is only called for
   * PERMANENT_IP_ASSOC and PROXY_IP_ASSOC)
   */
  if (new_assoc->type == PERMANENT_IP_ASSOC || new_assoc->type == PROXY_IP_ASSOC)
  {
    if (zip_payload_len < IP_ASSOC_SET_FIXED_SIZE)
    {
      WRN_PRINTF("IP Association message invalid: too short\n");
      memb_free(&ip_association_pool, new_assoc);
      ip_assoc_call_create_complete_cb(NULL);
      return;
    }

    new_assoc->grouping = ip_assoc_set_cmd->groupingIdentifier;
    uip_ipaddr_copy(&(new_assoc->resource_ip), (uip_ipaddr_t *)&(ip_assoc_set_cmd->resourceIP));
    new_assoc->resource_endpoint = ip_assoc_set_cmd->endpoint;

#ifdef DISABLE_DTLS
    new_assoc->resource_port = UIP_HTONS(ZWAVE_PORT);
#else
    /* resource_port controls if forwardings via this association are
     * DTLS encrypted. Use DTLS_PORT for resources on the LAN and
     * ZWAVE_PORT for resources in the PAN. */
    if (new_assoc->type == PROXY_IP_ASSOC)
    {
      new_assoc->resource_port = UIP_HTONS(ZWAVE_PORT);
    }
    else
    {
      new_assoc->resource_port = UIP_HTONS(DTLS_PORT);
    }
#endif
    rd_register_new_node(new_assoc->virtual_id, 0x00);
  }

  ip_assoc_add_to_association_table(new_assoc);
  ip_assoc_persist_association_table();

  ip_assoc_call_create_complete_cb(new_assoc);
}

/**
 * Query if any IP associations are marked for removal
 *
 * \note Uses the value of number_of_ip_assocs_marked_for_removal which contains
 * the number of IP associations marked by removal by the most recent invocation
 * of ip_assoc_find_and_mark_for_removal, which is not guaranteed to be the
 * actual number of entries currently marked for removal in the IP association
 * table. /see ip_assoc_find_and_mark_for_removal() for details.
 *
 * @return TRUE is any IP associations are marked for removal. FALSE otherwise.
 */
static BOOL
ip_assoc_is_any_marked_for_removal(void)
{
  return (number_of_ip_assocs_marked_for_removal != 0) ? TRUE : FALSE;
}

/**
 * Search the IP associations table from the top and return the first element marked for removal
 * 
 * @return Pointer to IP association marked for removal
 * @return NULL if not IP associations are marked for removal
 */
static ip_association_t *
ip_assoc_find_first_marked_for_removal(void)
{
  for (ip_association_t *a = list_head(ip_association_table); a != NULL; a = list_item_next(a))
  {
    if (a->mark_removal == 1)
    {
      DBG_PRINTF("ip_assoc_find_first_marked_for_removal found a->resource_endpoint:%d\n ip:",
                 a->resource_endpoint);
      uip_debug_ipaddr_print(&a->resource_ip);
      return a;
    }
  }
  return NULL;
}

/**
 * Start processing of removing nodes which are marked for removal
 * 
 * NB: It is not obvious that this function is actually removing all marked ip
 * associations. But except from the first invocation to start the bulk-removal
 * of associations this function will be called iteratively by
 * ip_assoc_continue_with_next_marked_for_removal() as long as there are still
 * ip associations marked for removal (the latter function is invoked by the
 * callback mechanism) */
static void
ip_assoc_remove_all_marked_for_removal(void)
{
  ip_association_t *ipa = ip_assoc_find_first_marked_for_removal();

  if (ipa != NULL) {
    #if 0
    DBG_PRINTF("--------- Removed ip association ipa->resource_endpoint:%d\n ip:", ipa->resource_endpoint);
    uip_debug_ipaddr_print(&ipa->resource_ip);
    DBG_PRINTF("Virt-%d:%d \t HAN-%d:%d \t %c\n",
        ipa->virtual_id, ipa->virtual_endpoint,
        ipa->han_nodeid, ipa->han_endpoint ,
        assoc_types[ipa->type]);
    #endif

    ip_assoc_setup_and_send_assoc_remove_cmd(ipa);
  } else {
    ASSERT(0); //This should not happen
    send_zip_ack(TRANSMIT_COMPLETE_OK);
    ip_assoc_abort(NULL, TRANSMIT_COMPLETE_OK);
  }
}

/**
 * Proceed to delete the next IP association marked for removal
 * 
 * Called whenever an association (and it's virtual node) has been removed.
 *
 * \note It's also being used as a callback from ZW_SetSlaveLearnMode(). The
 * parameters are defined to match the signature of the ZW_SetSlaveLearnMode
 * call back function. None of them are used by this function though.
 *
 * @param bStatus Not used
 * @param orgID   Not used
 * @param newID   Not used
 */
static void
ip_assoc_continue_with_next_marked_for_removal(uint8_t bStatus, nodeid_t orgID, nodeid_t newID)
{
  // Assumptions: The ip association (a) has been removed from the ip_association_table
  // One association (or virtual node) was deleted. Are there any more to remove?
  if (number_of_ip_assocs_marked_for_removal > 0)
  {
    number_of_ip_assocs_marked_for_removal--;
  }

  if (ip_assoc_is_any_marked_for_removal())
  {
    ip_assoc_remove_all_marked_for_removal();
  }
  else
  {
    send_zip_ack(TRANSMIT_COMPLETE_OK);
    report_send_completed(TRANSMIT_COMPLETE_OK);
    process_post(&zip_process, ZIP_EVENT_NM_VIRT_NODE_REMOVE_DONE, 0);
  }
}

/**
 * Remove an IP association
 *
 * Note that the memory for the IP association struct is deallocated. Don't use
 * after this function returns
 */
static void
ip_assoc_remove(ip_association_t *a)
{
  if (!a)
  {
    return;
  }

  rd_remove_node(a->virtual_id);
  list_remove(ip_association_table, a);
  if (a->type == PERMANENT_IP_ASSOC)
  {
    DBG_PRINTF("Removing a permanent virtual node: %d\n", a->virtual_id);
    BYTE retVal = ZW_SetSlaveLearnMode(a->virtual_id,
                                       VIRTUAL_SLAVE_LEARN_MODE_REMOVE,
                                       ip_assoc_continue_with_next_marked_for_removal);
    if (!retVal)
    {
      DBG_PRINTF("ZW_SetSlaveLearnMode() returned false\n");
      ip_assoc_continue_with_next_marked_for_removal(FALSE, 0, 0);
    }
  }
  else
  {
    ip_assoc_continue_with_next_marked_for_removal(TRUE, 0, 0);
  }
  /* TODO: Use refcount to allow using the same ip_association_t struct for several sessions */
  memb_free(&ip_association_pool, a);
}

static void
ip_assoc_remove_and_persist(ip_association_t *a)
{
  ip_assoc_remove(a);
  ip_assoc_persist_association_table();
}

/**
 * Mark matching IP associations in the in-memory association table for removal
 *
 * Use in response to one of the bulk remove IP association commands or when
 * removing a node with multiple associations.
 *
 * \note The global variable number_of_ip_assocs_marked_for_removal will be
 * updated with the number of IP associations marked for removal by the most
 * recent invocation of this function. I.e. if called twice back-to-back with
 * the same parameters then number_of_ip_assocs_marked_for_removal will always
 * end up being zero (but the correct elements in the IP association table will
 * still be marked for removal)
 *
 * @param han_nodeid   HAN node id
 * @param han_endpoint HAN endpoint
 * @param grouping_id  Grouping identifier
 * @param resource_ip  Resource IP address
 * @param resource_endpoint (not used)
 */
static void
ip_assoc_find_and_mark_for_removal(nodeid_t han_nodeid,
                                   uint8_t han_endpoint,
                                   uint8_t grouping_id,
                                   const uip_ip6addr_t *resource_ip,
                                   uint8_t resource_endpoint) /* NB: resource_endpoint not used */
{
  number_of_ip_assocs_marked_for_removal = 0;
  uip_ip6addr_t zero_ip = {};

  for (ip_association_t *a = list_head(ip_association_table); a != NULL; a = list_item_next(a))
  {
    if (
         a->han_nodeid == han_nodeid && 
         ((han_endpoint == 0) || (a->han_endpoint == han_endpoint)) && 
         ((grouping_id == 0) || (a->grouping == grouping_id)) && 
         ((resource_ip == 0) || uip_ipaddr_cmp(resource_ip, &zero_ip) || uip_ipaddr_cmp(resource_ip, &a->resource_ip))
       )
    {
      a->mark_removal = 1;
      number_of_ip_assocs_marked_for_removal++;
      DBG_PRINTF("IP association marked for removal. HAN node id: %d, resource endpoint: %d, resource ip:",
                 a->han_nodeid,
                 a->resource_endpoint);
      uip_debug_ipaddr_print(&a->resource_ip);
    }
  }
}

static void
ip_assoc_remove_callback(uint8_t bStatus, void* user, TX_STATUS_TYPE *t)
{
  ip_association_t *a = (ip_association_t *) user;

  /* We dont call ZW_DeleteReturnRoutes because that
   * would flush all return routes, and require us
   * to set the others up again.
   * Instead we rely on explorer searches to find routes. */
  if ((bStatus == TRANSMIT_COMPLETE_OK) || (bStatus == TRANSMIT_COMPLETE_NO_ACK))
  {
    ip_assoc_remove_and_persist(a);
  }
  else
  {
    send_zip_ack(TRANSMIT_COMPLETE_FAIL); /* Indicate delivery failure to source */
    report_send_completed(TRANSMIT_COMPLETE_OK); /* _OK because we dont want to see this frame again */
  }
}

static void
ip_assoc_abort(ip_association_t *a, uint8_t bStatus)
{
  ip_assoc_remove(a);
  report_send_completed(bStatus);
}

static void
report_send_complete_callback_wrapper(uint8_t bStatus, void* user, TX_STATUS_TYPE *t)
{
  report_send_completed(bStatus);
}

/* Documented in header file */
ip_association_t *
ip_assoc_lookup_by_virtual_node(nodeid_t virtnode)
{
  for (ip_association_t *a = list_head(ip_association_table); a != NULL; a = list_item_next(a))
  {
    if (a->virtual_id == virtnode)
    {
      return a;
    }
  }
  return NULL;
}

static void
assign_return_route_callback(unsigned char bStatus)
{
  ip_association_t *a = ip_association_for_assign_return_route_cb;
  ip_association_for_assign_return_route_cb = NULL;

  send_zip_ack(bStatus);
  if (bStatus != TRANSMIT_COMPLETE_OK)
  {
    ip_assoc_abort(a, TRANSMIT_COMPLETE_FAIL);
  }
  else
  {
    report_send_completed(TRANSMIT_COMPLETE_OK);
  }
}

static void
send_ip_association_cmd_callback(unsigned char bStatus, void* user, TX_STATUS_TYPE *t)
{
  ip_association_t *a = (ip_association_t *) user;

  ASSERT(a != NULL);
  if ((a != NULL) && (bStatus == TRANSMIT_COMPLETE_OK))
  {
    // Make the ip_association available for assign_return_route_callback()
    ip_association_for_assign_return_route_cb = a;

    /* Setup return routes, then send back ZIP_ACK*/
    /* If this is a local assoc, setup return routes directly btw. assoc
     * source and dest, otherwise setup return routes to virtual node */
    if (!ZW_AssignReturnRoute(a->han_nodeid,
        a->type == LOCAL_IP_ASSOC ? nodeOfIP(&a->resource_ip) : a->virtual_id,
        assign_return_route_callback))
    {
      ip_association_for_assign_return_route_cb = NULL;
      send_zip_ack(TRANSMIT_COMPLETE_FAIL);
      ip_assoc_abort(a, TRANSMIT_COMPLETE_FAIL);
    }
  }
  else
  {
    send_zip_ack(TRANSMIT_COMPLETE_FAIL);
    ip_assoc_abort(a, TRANSMIT_COMPLETE_FAIL);
  }
}

static void
send_ip_association_cmd(ip_association_t *a, nodeid_t dest_nodeid, uint16_t len)
{
  /* Send the association frame in classic_txBuf on the radio.
   * When callback comes back, setup return routes.
   * When that callback comes, send off ZIP_ACK. */

  ts_param_t p;
  ts_set_std(&p, dest_nodeid);

  uint8_t *classicTxBuf = ClassicZIPNode_getTXBuf();

  p.scheme = AUTO_SCHEME;

  if (!ClassicZIPNode_SendDataAppl(&p,
                                   classicTxBuf,
                                   len,
                                   send_ip_association_cmd_callback,
                                   a))
  {
    send_zip_ack(TRANSMIT_COMPLETE_ERROR);
    ASSERT(0);

    ip_assoc_abort(a, TRANSMIT_COMPLETE_ERROR);
    /* drop packet */
  }
}

/**
 * Set up a classic Association Set in buffer p
 * 
 * @param p Pointer to tx buffer
 * @param ip_assoc_cmd IP Association command
 * @return number of bytes written
 */
static int
ip_assoc_setup_classic_assoc_set_cmd(uint8_t* p, ZW_COMMAND_IP_ASSOCIATION_SET *ip_assoc_cmd)
{
  /* FIXME: Is *p and ip_assoc_cmd pointing to the same thing?
   * If so, remove one of them and derive from the other */

  ZW_MULTI_CHANNEL_ASSOCIATION_SET_1BYTE_V2_FRAME* b =
      (ZW_MULTI_CHANNEL_ASSOCIATION_SET_1BYTE_V2_FRAME*) p;
  b->cmdClass = COMMAND_CLASS_ASSOCIATION_V2;
  b->cmd = ASSOCIATION_SET_V2;
  b->groupingIdentifier = ip_assoc_cmd->groupingIdentifier;
  b->nodeId1 = nodeOfIP(&ip_assoc_cmd->resourceIP);
  return 4;
}

/**
 * Generate a classic Association Remove command in tx buffer
 * 
 * @param p   Pointer to tx buffer
 * @param ipa IP association to remove
 * @return    Size of generated message
 */
static uint8_t
ip_assoc_setup_classic_assoc_remove_cmd(uint8_t *p, ip_association_t *ipa)
{
  ZW_MULTI_CHANNEL_ASSOCIATION_SET_1BYTE_V2_FRAME* b =
      (ZW_MULTI_CHANNEL_ASSOCIATION_SET_1BYTE_V2_FRAME*) p;
  ASSERT(ipa->type == PERMANENT_IP_ASSOC || ipa->type == LOCAL_IP_ASSOC);
  b->cmdClass = COMMAND_CLASS_ASSOCIATION_V2;
  b->cmd = ASSOCIATION_REMOVE_V2;
  b->groupingIdentifier = ipa->grouping;
  if (ipa->type == LOCAL_IP_ASSOC)
  {
    b->nodeId1 = nodeOfIP(&ipa->resource_ip);
  }
  else
  {
    b->nodeId1 = ipa->virtual_id;
  }
  return 4;
}

/**
 * Generate a Multi-Channel Association Remove command in tx buffer
 * 
 * @param p   Pointer to tx buffer
 * @param ipa IP association to remove
 * @return    Size of generated message
 */
static uint8_t
ip_assoc_setup_multi_channel_assoc_remove_cmd(uint8_t *p, ip_association_t *ipa)
{
  *(p++) = COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V2;
  *(p++) = MULTI_CHANNEL_ASSOCIATION_REMOVE_V2;
  *(p++) = ipa->grouping;
  *(p++) = MULTI_CHANNEL_ASSOCIATION_REMOVE_MARKER_V2;
  *(p++) = ipa->type == LOCAL_IP_ASSOC ? nodeOfIP(&ipa->resource_ip) :ipa->virtual_id ;
  *(p++) = ipa->type == LOCAL_IP_ASSOC ? ipa->resource_endpoint : ipa->virtual_endpoint;
  return 6;
}

static void
ip_assoc_setup_and_send_assoc_remove_cmd(ip_association_t *ipa)
{
  uint8_t i = 0;
  ts_param_t p;

  ASSERT(ipa);

  ts_set_std(&p, ipa->han_nodeid);
  p.scheme = zw.scheme;
  p.dendpoint = ipa->han_endpoint;
  p.sendpoint = ipa->resource_endpoint;

  ASSERT(ipa->type == LOCAL_IP_ASSOC || ipa->type == PERMANENT_IP_ASSOC);

  if (!rd_node_exists(ipa->han_nodeid)) {
    /* Somewhere down the call hierarchy ip_assoc_remove_callback() will
     * activate ip_assoc_continue_with_next_marked_for_removal() */
    ip_assoc_remove_callback(TRANSMIT_COMPLETE_OK, ipa, NULL);
    return;
  }

  if (SupportsCmdClass(ipa->han_nodeid,
      COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V2)
      || SupportsCmdClassSecure(ipa->han_nodeid,
          COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V2)
     )
  {
    /* Case 5 */
    i = ip_assoc_setup_multi_channel_assoc_remove_cmd(ClassicZIPNode_getTXBuf(), ipa);
  }
  else
  {
    /* Case 2 */
    i = ip_assoc_setup_classic_assoc_remove_cmd(ClassicZIPNode_getTXBuf(), ipa);
  }

  /* Send Association Remove command to Association Source Node */
  if (!ClassicZIPNode_SendDataAppl(&p,
                                   ClassicZIPNode_getTXBuf(),
                                   i,
                                   // NB: ip_assoc_remove_callback() will activate 
                                   // ip_assoc_continue_with_next_marked_for_removal() 
                                   ip_assoc_remove_callback,
                                   ipa))
  {
    send_zip_ack(TRANSMIT_COMPLETE_ERROR);
    ASSERT(0);
    ip_assoc_abort(ipa, TRANSMIT_COMPLETE_ERROR);
    /* drop packet */
  }
}

static uint8_t
ip_assoc_setup_multi_channel_assoc_set(uint8_t* p, nodeid_t assoc_dest_node, ZW_COMMAND_IP_ASSOCIATION_SET *cmd)
{
  *(p++) = COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V2;
  *(p++) = MULTI_CHANNEL_ASSOCIATION_SET_V2;
  *(p++) = cmd->groupingIdentifier;
  *(p++) = assoc_dest_node;
  *(p++) = MULTI_CHANNEL_ASSOCIATION_SET_MARKER_V2;
  *(p++) = assoc_dest_node;
  *(p++) = cmd->endpoint;
  return 7;
}

/**
 * Get IP association type as a string (for debugging)
 * 
 * @param type IP association type
 * @return     Type name (or error message)
 */
static const char *
ip_assoc_type2str(ip_assoc_type_t type)
{
  switch (type)
  {
  case PERMANENT_IP_ASSOC:
    return "perm";
    break;

  case PROXY_IP_ASSOC:
    return "proxy";
    break;

  case LOCAL_IP_ASSOC:
    return "local";
    break;
  }
  return "ERROR unknown assoc type";
}

/* Helper functions to inspect ip association table from debugger */
ip_association_t *
ip_assoc_head()
{
  return (ip_association_t *)list_head(ip_association_table);
}

ip_association_t *
ip_assoc_next(ip_association_t *a)
{
  return (ip_association_t *)list_item_next(a);
}
