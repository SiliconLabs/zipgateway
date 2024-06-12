/* © 2014 Silicon Laboratories Inc.
 */
#include <uip.h>
#include <contiki.h>
#include <list.h>
#include <memb.h>
#ifndef ZIP_NATIVE
#include <stdio.h>
#endif
#include "ClassicZIPNode.h"
#include <uip-debug.h>
#include "ZW_controller_bridge_api.h"
#include "ZW_zip_classcmd.h"
#include "Bridge.h"
#include "ZW_udp_server.h"
#include "ResourceDirectory.h"
#include "DataStore.h"
#include "security_layer.h"
#include "sys/ctimer.h"
#include "DTLS_server.h"
#include "ZIP_Router_logging.h"
#include "router_events.h"
#include "zgw_nodemask.h"

#include "Bridge_temp_assoc.h"
#include "Bridge_ip_assoc.h"

// Global variable shared with multiple modules
bridge_state_t bridge_state;

/** Bitmask reflecting the virtual nodes in the controller */
uint8_t virtual_nodes_mask[MAX_CLASSIC_NODEMASK_LENGTH];

/* Documented in Bridge.h */
BOOL is_assoc_create_in_progress(void)
{
  /* We are only checking the state of IP association creation here. The
   * creation of temporary associations does not involve any asynchronously
   * processing so it simply runs to completion before any other events will be
   * handled.
   */

  return (is_ip_assoc_create_in_progress()) ? TRUE : FALSE;
}

/* Inspect a Z-Wave frame payload and extract the destination multichannel endpoint.
 * Return 0 if the frame is not multichannel encapsultaed.
 * Return 0xFF if the frame bit-addresses several endpoints.
 *
 * Known bugs:
 * Returns 0 if fail if there are other encapsulations outside the multichannel
 * (e.g. multi command class).
 *
 * TODO: Unpack other encapsulations while searching for multichannel.
 */
uint8_t
get_multichannel_dest_endpoint(unsigned char *p)
{
  ZW_MULTI_CHANNEL_CMD_ENCAP_V2_FRAME *pCmd = (void *)p;
  if (pCmd->cmdClass == COMMAND_CLASS_MULTI_CHANNEL_V2 && pCmd->cmd == MULTI_CHANNEL_CMD_ENCAP_V2)
  {
    if (pCmd->properties2 & MULTI_CHANNEL_CMD_ENCAP_PROPERTIES2_BIT_ADDRESS_BIT_MASK_V2)
    {
      return 0xFF;
    }
    return pCmd->properties2 & MULTI_CHANNEL_CMD_ENCAP_PROPERTIES2_DESTINATION_END_POINT_MASK_V2;
  }
  return 0;
}

BOOL bridge_virtual_node_commandhandler(ts_param_t *p, BYTE *__pCmd, BYTE cmdLength)
{
  ZW_APPLICATION_TX_BUFFER *pCmd = (ZW_APPLICATION_TX_BUFFER *)__pCmd;

  switch (pCmd->ZW_Common.cmdClass)
  {
  default:
    CreateLogicalUDP(p, __pCmd, cmdLength);
    break;
  }

  return TRUE;
}

/* Documented in Bridge.h */
void copy_virtual_nodes_mask_from_controller()
{
  memset(virtual_nodes_mask, 0, sizeof(virtual_nodes_mask));
  ZW_GetVirtualNodes(virtual_nodes_mask);

  /* TODO: Delete all remaining virtual nodes that are not used in association_table */
}

/* Documented in Bridge.h */
BOOL is_virtual_node(nodeid_t nid)
{
  /**
   * LR virtual nodes are always 4002 - 4005 and won't be shown up in virtual_node_list
   * so we have to do special handling here. As the plan is to abandon the virtual nodes
   * feature, this hack for the moment is acceptable.
   */
  if ((is_classic_node(nid) && BIT8_TST(nid - 1, virtual_nodes_mask)) ||
      ((nid <= 4005) && (nid >= 4002))) {
    return true;
  }
  return false;
}

void bridge_init()
{
  uint8_t cap;

  const APPL_NODE_TYPE virtual_node_type =
      {GENERIC_TYPE_REPEATER_SLAVE, SPECIFIC_TYPE_VIRTUAL_NODE};
  const uint8_t virtual_cmd_classes[] =
      {COMMAND_CLASS_SECURITY, COMMAND_CLASS_SECURITY_2};
  const uint8_t virtual_nop_classes[] =
      {COMMAND_CLASS_NO_OPERATION};

  ip_assoc_init();
  temp_assoc_init();

  bridge_state = booting;

  if (is_sec0_key_granted() > 0)
  {
    SerialAPI_ApplicationSlaveNodeInformation(0, 1, virtual_node_type, (uint8_t *)virtual_cmd_classes,
                                              sizeof(virtual_cmd_classes));
  }
  else
  {
    SerialAPI_ApplicationSlaveNodeInformation(0, 1, virtual_node_type, (uint8_t *)virtual_nop_classes,
                                              sizeof(virtual_nop_classes));
  }

  ip_assoc_unpersist_association_table();
  temp_assoc_unpersist_virtual_nodeids();

  copy_virtual_nodes_mask_from_controller();

  cap = ZW_GetControllerCapabilities();

  if ((cap & CONTROLLER_NODEID_SERVER_PRESENT) || (cap & CONTROLLER_IS_SECONDARY) == 0)
  {
    // Doing add node stop before virtual node creation
    // to avoid race condition after setdefault.
    // Otherwise we would enable Smart Start lean mode before creating
    // virtual nodes, and that would disrupt the virtual node creation.
    ZW_AddNodeToNetwork(ADD_NODE_STOP, NULL);

    /* Start adding the virtual nodes preallocated for temporary associations */
    temp_assoc_delayed_add_virtual_nodes();
  }
  else
  {
    ERR_PRINTF("Gateway is included into a NON SIS network\n");
    ERR_PRINTF("there will be limited functionality available\n");
    bridge_state = initfail;
    process_post(&zip_process, ZIP_EVENT_BRIDGE_INITIALIZED, 0);
  }
}

/**
 * Clear and persist the (empty) association tables
 */
static void
bridge_clear_association_tables(void)
{
  ip_assoc_init();
  ip_assoc_persist_association_table();

  temp_assoc_init();
  temp_assoc_persist_virtual_nodeids();
}

void resume_bridge_init(void)
{
  temp_assoc_resume_init();
}

void bridge_reset()
{
  bridge_clear_association_tables();

  bridge_state = booting;
}

bool bridge_idle(void) {
   return ((bridge_state == initialized)
           && (is_assoc_create_in_progress() == FALSE));
}


/* Documented in Bridge.h */
void
print_association_list_line(uint8_t line_no,
                            uint8_t resource_endpoint,
                            uint16_t resource_port,
                            uip_ip6addr_t *resource_ip,
                            nodeid_t virtual_id,
                            uint8_t virtual_endpoint,
                            nodeid_t han_nodeid,
                            uint8_t han_endpoint,
                            const char *type_str)
{
  LOG_PRINTF("#%2d: [%-5s] Virtual: %d.%d - HAN: %d.%d\n",
             line_no,
             type_str,
             virtual_id,
             virtual_endpoint,
             han_nodeid,
             han_endpoint
             );

  LOG_PRINTF("             LAN endpoint: %d port: %d IP: ",
             resource_endpoint,
             resource_port);

  uip_debug_ipaddr_print(resource_ip);

}

/* Documented in Bridge.h */


void print_bridge_associations(void)
{
  ip_assoc_print_association_table();
  temp_assoc_print_association_table();
}

void print_bridge_status(void)
{
  LOG_PRINTF("bridge_state: %u\n", bridge_state);

  print_bridge_associations();

  LOG_PRINTF("\n");

  temp_assoc_print_virtual_node_ids();

  copy_virtual_nodes_mask_from_controller();

  {
    // Each bitmask byte is printed with two hex characters and a space (len=3)
    char nodemask_str[(sizeof(virtual_nodes_mask) * 3) + 1] = {0};

    int pos = 0;
    for (int i = 0; i < sizeof(virtual_nodes_mask); ++i)
    {
      // Print MSB first (to the left) and LSB last (to the right)
      pos += sprintf(nodemask_str + pos, "%02x ", virtual_nodes_mask[sizeof(virtual_nodes_mask) - i - 1]);
    }

    LOG_PRINTF("\nvirtual_nodes_mask (hex): %s\n", nodemask_str);
  }
}
