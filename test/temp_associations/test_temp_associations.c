#include <unity.h>

#include "ZIP_Router.h"
#include "Bridge_temp_assoc.h"
#include "ClassicZIPNode.h"
#include "DataStore.h"
#include "conhandle.h"
#include "node_queue.h"
#include "ZW_udp_server.h"
#include "zw_frame_buffer.h"
#include "zip_router_ipv6_utils.h"
#include "RD_DataStore.h"
#include "net/uiplib.h"
#include "net/tcpip.h"

#define UIP_IP_BUF       ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP_UDP_BUF      ((struct uip_udp_hdr *)&uip_buf[uip_l2_l3_hdr_len])
#define ZIP_PKT_BUF      ((ZW_COMMAND_ZIP_PACKET*)&uip_buf[UIP_LLH_LEN + UIP_IPUDPH_LEN])
#define ZIP_PKT_BUF_SIZE (UIP_BUFSIZE -(UIP_LLH_LEN + UIP_IPUDPH_LEN))

extern nodeid_t MyNodeID;
extern const char* linux_conf_database_file;

static nodeid_t virtual_nodes[MAX_CLASSIC_TEMP_ASSOCIATIONS] = {2, 3, 4, 5};
static BOOL call_next_ctimer_callback_once = FALSE;
static ZW_SendDataAppl_Callback_t the_sd_callback;
static void* the_sd_callback_user;
static int lan_msg_count =0;
int my_queue_state = QS_IDLE;

/****************************************************************************/
/*                             MOCKS / WRAPPERS                             */
/****************************************************************************/

// Capture message most recently sent to HAN
static struct {
  uint8_t cmd_class;
  uint8_t cmd;
  nodeid_t srcnode;
  nodeid_t dstnode;
} last_HAN_message;

uint8_t __wrap_ZW_SendDataAppl(ts_param_t* p, const void *pData, uint16_t dataLength, ZW_SendDataAppl_Callback_t callback, void* user)
{
  uint8_t *c = (uint8_t*)pData;
  DBG_PRINTF("%s: dataLength=%d\n", __func__, dataLength);
  DBG_PRINTF("Sending %d->%d, class: 0x%x, cmd: 0x%x\n", p->snode, p->dnode, c[0], c[1]);

  // Save message data for unit test validation
  last_HAN_message.cmd_class = c[0];
  last_HAN_message.cmd = c[1];
  last_HAN_message.srcnode = p->snode;
  last_HAN_message.dstnode = p->dnode;
  the_sd_callback = callback;
  the_sd_callback_user = user;
  return 1;
}

// Capture message most recently sent to LAN
static struct {
  uint8_t cmd_class;
  uint8_t cmd;
  uip_ipaddr_t sipaddr;
  uip_ipaddr_t ripaddr;
  uint16_t lport;
  uint16_t rport;
} last_LAN_message;


void __wrap_ZW_SendData_UDP(zwave_connection_t *c,
                            const BYTE *dataptr,
                            u16_t datalen,
                            void (*cbFunc)(BYTE, void *user),
                            BOOL ackreq)
{
  // Called from LogicalRewriteAndSend() to send to LAN
  DBG_PRINTF("%s\n", __func__);

  // Save message data for unit test validation
  uip_ipaddr_copy(&last_LAN_message.sipaddr, &c->lipaddr);
  uip_ipaddr_copy(&last_LAN_message.ripaddr, &c->ripaddr);
  last_LAN_message.lport = c->lport;
  last_LAN_message.rport = c->rport;

  last_LAN_message.cmd_class = dataptr[0];
  last_LAN_message.cmd = dataptr[1];
  lan_msg_count++;
}

// For ack/nack testing
void __wrap_udp_send_wrap(struct uip_udp_conn* c, const void* buf, u16_t len)
{
  // Called from LogicalRewriteAndSend() to send to LAN
  DBG_PRINTF("%s\n", __func__);

  // Save message data for unit test validation
  uip_ipaddr_copy(&last_LAN_message.sipaddr, &c->sipaddr);
  uip_ipaddr_copy(&last_LAN_message.ripaddr, &c->ripaddr);
  last_LAN_message.lport = c->lport;
  last_LAN_message.rport = c->rport;

  ZW_COMMAND_ZIP_PACKET *zip_pkt = (ZW_COMMAND_ZIP_PACKET *) buf;
  last_LAN_message.cmd_class = zip_pkt->cmdClass;
  last_LAN_message.cmd = zip_pkt->cmd;
  lan_msg_count++;
}

BYTE __wrap_ZW_GetControllerCapabilities(void)
{
  DBG_PRINTF("%s\n", __func__);
  return 0;
}


void __wrap_ZW_AddNodeToNetwork(BYTE bMode, VOID_CALLBACKFUNC(completedFunc)(auto LEARN_INFO*))
{
  DBG_PRINTF("%s\n", __func__);
}


void __wrap_ZW_GetVirtualNodes(char *pNodeMask)
{
  DBG_PRINTF("%s\n", __func__);
  for (int i = 0; i < (sizeof(virtual_nodes)/sizeof(virtual_nodes[0])); i++)
  {
    BIT8_SET(virtual_nodes[i] - 1, pNodeMask);
  }
}


uint8_t __wrap_sec2_get_my_node_flags()
{
  DBG_PRINTF("%s\n", __func__);
  return 0;
}


void __wrap_SerialAPI_ApplicationSlaveNodeInformation(uint16_t dstNode, BYTE listening, APPL_NODE_TYPE nodeType, BYTE *nodeParm, BYTE parmLength )
{
  DBG_PRINTF("%s\n", __func__);
}


void __wrap_ip_assoc_unpersist_association_table(void)
{
  DBG_PRINTF("%s\n", __func__);
}

size_t __wrap_rd_datastore_unpersist_virtual_nodes(nodeid_t *nodelist, size_t max_node_count)
{
  memcpy(nodelist, virtual_nodes, sizeof(virtual_nodes));
  return MAX_CLASSIC_TEMP_ASSOCIATIONS;
}

void __wrap_ctimer_set(struct ctimer *c, clock_time_t t, void (*f)(void *), void *ptr)
{
  DBG_PRINTF("%s (invoke_cb=%d)\n", __func__, call_next_ctimer_callback_once);

  if (call_next_ctimer_callback_once)
  {
    call_next_ctimer_callback_once = FALSE;
    f(ptr);
  }
}


void __wrap_tcpip_ipv6_output(void)
{
  // Called from uip_udp_packet_send() after calling uip_process(UIP_UDP_SEND_CONN)
  DBG_PRINTF("%s: destport = %d\n", __func__, UIP_HTONS(UIP_UDP_BUF->destport));
}


int __wrap_get_queue_state(void) {
  return my_queue_state;
}


/****************************************************************************/
/*                              TEST SETUP                                  */
/****************************************************************************/

void setup_and_init_bridge_for_test(void)
{
  DBG_PRINTF("%s\n", __func__);

  uiplib_ipaddrconv("::",  &cfg.tun_prefix);
  uiplib_ipaddrconv("fd00:adba::03",  &cfg.lan_addr); // LAN address of gateway
  uiplib_ipaddrconv("fd00:bdbd::01",  &cfg.pan_prefix);
  cfg.cfg_lan_prefix_length = 64;
  uiplib_ipaddrconv("fd00:aaaa::1234", &cfg.gw_addr);
  uiplib_ipaddrconv("fd00:adba::b528:4bcb:ab53:000e", &cfg.unsolicited_dest);
  cfg.unsolicited_port = 5555;
  my_queue_state = QS_IDLE;
  MyNodeID = 1;
  lan_msg_count = 0;

  /* Make the next call to the ctimer_set() mock/wrapper call the timeout
   * function immediately (will cause temp_assoc_add_virtual_nodes() to be
   * called directly from temp_assoc_delayed_add_virtual_nodes()
   */
  call_next_ctimer_callback_once = TRUE;

  linux_conf_database_file = __FILE__ ".db";
  data_store_init();
  bridge_init();
  print_bridge_status();

  LOG_PRINTF("-------------------------------------\n");
}


/****************************************************************************/
/*                             TEST HELPERS                                 */
/****************************************************************************/


void my_ClassicZIPNode_input_cb(BYTE res, BYTE *buf, uint16_t buflen)
{
  DBG_PRINTF("%s: res:0x%02x buf:%p buflen=%d\n", __func__, res, buf, buflen);
}

static void send_cmd_from_lan_to_han_ex(const char *src_ip_addr, uint16_t src_port, nodeid_t dst_nodeid, const uint8_t *cmd, size_t cmdlen,uint8_t bStatus)
{
  DBG_PRINTF("### %s: src_port = %d dst_nodeid = %d\n", __func__, src_port, dst_nodeid);
  memset(uip_buf, 0, UIP_BUFSIZE);

  uiplib_ipaddrconv(src_ip_addr,  &UIP_IP_BUF->srcipaddr);
  UIP_UDP_BUF->srcport  = UIP_HTONS(src_port);
  UIP_IP_BUF->proto = UIP_PROTO_UDP;

  // Make IP address using cfg.pan_prefix
  ipOfNode(&UIP_IP_BUF->destipaddr, dst_nodeid);
  UIP_UDP_BUF->destport = UIP_HTONS(ZWAVE_PORT);

  ZW_COMMAND_ZIP_PACKET zip_pkt = {
    .cmdClass = COMMAND_CLASS_ZIP,
    .cmd = COMMAND_ZIP_PACKET,
    .flags0 = ZIP_PACKET_FLAGS0_ACK_REQ,
    .flags1 = ZIP_PACKET_FLAGS1_ZW_CMD_INCL,
    .seqNo = 0x00,
    .sEndpoint = 0x00,
    .dEndpoint = 0x00,
    .payload = {0}
  };

  memcpy(ZIP_PKT_BUF, &zip_pkt, sizeof(zip_pkt));
  memcpy(((uint8_t*)ZIP_PKT_BUF) + offsetof(ZW_COMMAND_ZIP_PACKET, payload), cmd, cmdlen);

  size_t udp_payload_len = offsetof(ZW_COMMAND_ZIP_PACKET, payload) + cmdlen;
  uip_len = UIP_IPUDPH_LEN + udp_payload_len;

  nodeid_t node = nodeOfIP(&(UIP_IP_BUF->destipaddr));  // should match dst_nodeid

  ClassicZIPNode_input(node, my_ClassicZIPNode_input_cb, FALSE, FALSE);

  /* Here __wrap_ZW_SendDataAppl() has been called and captured the HAN package
   * in last_HAN_message.
   */
  the_sd_callback(bStatus,the_sd_callback_user,NULL);
}


static void send_cmd_from_lan_to_han(const char *src_ip_addr, uint16_t src_port, nodeid_t dst_nodeid)
{
  uint8_t cmd[] = {COMMAND_CLASS_VERSION, VERSION_GET};
  size_t cmd_len = sizeof(cmd);

  send_cmd_from_lan_to_han_ex(src_ip_addr, src_port, dst_nodeid, cmd, cmd_len,TRANSMIT_COMPLETE_OK);
}


static void send_fw_upd_cmd_from_lan_to_han(const char *src_ip_addr, uint16_t src_port, uint8_t dst_nodeid)
{
  uint8_t cmd[] = {COMMAND_CLASS_FIRMWARE_UPDATE_MD, FIRMWARE_UPDATE_MD_GET_V3, 0x01, 0x01};
  size_t cmd_len = sizeof(cmd);

  send_cmd_from_lan_to_han_ex(src_ip_addr, src_port, dst_nodeid, cmd, cmd_len,TRANSMIT_COMPLETE_OK);
}


void send_cmd_from_han_to_lan(nodeid_t src_nodeid /* HAN node */, nodeid_t dst_nodeid /* Virtual node */)
{
  DBG_PRINTF("### %s: src_nodeid = %d dst_nodeid = %d\n", __func__, src_nodeid, dst_nodeid);

  ts_param_t p = {0};

  p.snode = src_nodeid;
  p.dnode = dst_nodeid;
  p.scheme = NO_SCHEME;

  uint8_t report[] = {COMMAND_CLASS_VERSION, VERSION_REPORT, BASIC_TYPE_ROUTING_SLAVE, 0x07, 0x0b, 0x07, 0x0b};

  memset(uip_buf, 0, UIP_BUFSIZE);

  /* Call bridge_virtual_node_commandhandler
   * Will eventually call udp_send_wrap() with a ZIP package in the uip buffer.
   */

  bridge_virtual_node_commandhandler(&p, report, sizeof(report));

  /* Here __wrap_udp_send_wrap() has been called and the ZIP package is in the
   * uip buffer. The UDP package is handled by the UIP IP layer and eventually
   * it comes back as input to ClassicZIPNode_input(). We simulate that here by
   * calling ClassicZIPNode_input() directly while uip buffer still has the
   * correct message.
   */

  //ClassicZIPNode_input(0, my_ClassicZIPNode_input_cb, FALSE, FALSE);

  /* Here __wrap_udp_send_wrap() will have been called by LogicalRewriteAndSend()
   * The LAN message has been captured in last_LAN_message.
   */
}

/****************************************************************************/
/*                           TEST RESULT VALIDATORS                         */
/****************************************************************************/


/* Check if a temporary association does (or does not) exist */
BOOL check_temp_assoc(nodeid_t virtual_nodeid, const char *expected_ip_str,
                      uint16_t expected_port, bool is_long_range)
{
  temp_association_t *a = temp_assoc_lookup_by_virtual_nodeid(virtual_nodeid);

  if (!expected_ip_str && !a)
  {
    // Not expecting any temp assoc and none was found --> success!
    return TRUE;
  }

  if (expected_ip_str && a)
  {
    uip_ip6addr_t expected_ip_addr = {0};
    uiplib_ipaddrconv(expected_ip_str,  &expected_ip_addr);

    if (uip_ipaddr_cmp(&a->resource_ip, &expected_ip_addr) &&
                       a->resource_port == UIP_HTONS(expected_port) &&
                       a->is_long_range == is_long_range)
    {
      return TRUE; // Found a temp assoc that matched the expected ip addres and port
    }
  }
  return FALSE;
}


BOOL check_last_HAN_message_ex(nodeid_t srcnode, nodeid_t dstnode, uint8_t cmdclass, uint8_t cmd)
{
  ERR_PRINTF("%d\n", dstnode);
  BOOL res = FALSE;
  if (last_HAN_message.cmd_class == cmdclass &&
      last_HAN_message.cmd == cmd &&
      last_HAN_message.srcnode == srcnode &&
      last_HAN_message.dstnode == dstnode)
  {
    res = TRUE;
  }
  // Reset for next iteration
  memset(&last_HAN_message, 0, sizeof(last_HAN_message));

  return res;
}


BOOL check_last_HAN_message(nodeid_t srcnode, nodeid_t dstnode)
{
  return check_last_HAN_message_ex(srcnode, dstnode, COMMAND_CLASS_VERSION, VERSION_GET);
}


BOOL check_last_HAN_message_fw_upd(nodeid_t srcnode, nodeid_t dstnode)
{
  return check_last_HAN_message_ex(srcnode, dstnode, COMMAND_CLASS_FIRMWARE_UPDATE_MD, FIRMWARE_UPDATE_MD_GET_V3);
}

BOOL check_last_LAN_message(const char *expected_ripaddr, uint16_t expected_rport)
{
  DBG_PRINTF("%s\n", __func__);
  uip_ipaddr_t ipaddr = {0};
  BOOL res = FALSE;

  uiplib_ipaddrconv(expected_ripaddr,  &ipaddr);

  if (uip_ipaddr_cmp(&last_LAN_message.ripaddr, &ipaddr) &&
      (expected_rport == UIP_HTONS(last_LAN_message.rport)) &&
      (COMMAND_CLASS_VERSION == last_LAN_message.cmd_class) &&
      (VERSION_REPORT == last_LAN_message.cmd))
  {
    res = TRUE;
  }

  // Reset for next iteration
  memset(&last_LAN_message, 0, sizeof(last_LAN_message));

  return res;
}


/****************************************************************************/
/*                              TEST CASES                                  */
/****************************************************************************/


#define IP1_ADDR "fd00:adba::b528:4bcb:ab53:0001"
#define IP1_PORT 4001
#define IP2_ADDR "fd00:adba::b528:4bcb:ab53:0002"
#define IP2_PORT 4002
#define IP3_ADDR "fd00:adba::b528:4bcb:ab53:0003"
#define IP3_PORT 4003
#define IP4_ADDR "fd00:adba::b528:4bcb:ab53:0004"
#define IP4_PORT 4004
#define IP5_ADDR "fd00:adba::b528:4bcb:ab53:0005"
#define IP5_PORT 4005
#define IP6_ADDR "fd00:adba::b528:4bcb:ab53:0006"
#define IP6_PORT 4006
#define IP7_ADDR "fd00:adba::b528:4bcb:ab53:0007"
#define IP7_PORT 4007
#define IP8_ADDR "fd00:adba::b528:4bcb:ab53:0008"
#define IP8_PORT 4008
#define IP9_ADDR "fd00:adba::b528:4bcb:ab53:0009"
#define IP9_PORT 4009


/*
 * Create more temp associations than the association table can hold,
 * and observe that "old" associations are re-used as expected.
 */
void test_temp_association_recycle(void)
{
  LOG_PRINTF("#####################################\n");
  LOG_PRINTF("%s\n", __func__);
  LOG_PRINTF("#####################################\n");

  setup_and_init_bridge_for_test();

  send_cmd_from_lan_to_han(IP1_ADDR, IP1_PORT, 6);
  TEST_ASSERT_TRUE( check_temp_assoc(2, IP1_ADDR, IP1_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(3, 0, 0, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4, 0, 0, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(5, 0, 0, false) );
  TEST_ASSERT_TRUE( check_last_HAN_message(2, 6) ); // Virt node 2 to HAN node 6

  send_cmd_from_lan_to_han(IP2_ADDR, IP2_PORT, 7);
  TEST_ASSERT_TRUE( check_temp_assoc(2, IP1_ADDR, IP1_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(3, IP2_ADDR, IP2_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4, 0, 0, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(5, 0, 0, false) );
  TEST_ASSERT_TRUE( check_last_HAN_message(3, 7) ); // Virt node 3 to HAN node 7

  send_cmd_from_lan_to_han(IP3_ADDR, IP3_PORT, 8);
  TEST_ASSERT_TRUE( check_temp_assoc(2, IP1_ADDR, IP1_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(3, IP2_ADDR, IP2_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4, IP3_ADDR, IP3_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(5, 0, 0, false) );
  TEST_ASSERT_TRUE( check_last_HAN_message(4, 8) );  // Virt node 4 to HAN node 8

  send_cmd_from_lan_to_han(IP4_ADDR, IP4_PORT, 9);
  TEST_ASSERT_TRUE( check_temp_assoc(2, IP1_ADDR, IP1_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(3, IP2_ADDR, IP2_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4, IP3_ADDR, IP3_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(5, IP4_ADDR, IP4_PORT, false) );
  TEST_ASSERT_TRUE( check_last_HAN_message(5, 9) ); // Virt node 5 to HAN node 9

  send_cmd_from_lan_to_han(IP5_ADDR, IP5_PORT, 10);
  TEST_ASSERT_TRUE( check_temp_assoc(2, IP5_ADDR, IP5_PORT, false) ); // Virt node 2 is recycled
  TEST_ASSERT_TRUE( check_temp_assoc(3, IP2_ADDR, IP2_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4, IP3_ADDR, IP3_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(5, IP4_ADDR, IP4_PORT, false) );
  TEST_ASSERT_TRUE( check_last_HAN_message(2, 10) ); // Virt node 2 to HAN node 10

  // Use existing temp assoc to send to new node (no change to assoc table expected)
  send_cmd_from_lan_to_han(IP3_ADDR, IP3_PORT, 12);
  TEST_ASSERT_TRUE( check_temp_assoc(2, IP5_ADDR, IP5_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(3, IP2_ADDR, IP2_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4, IP3_ADDR, IP3_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(5, IP4_ADDR, IP4_PORT, false) );
  TEST_ASSERT_TRUE( check_last_HAN_message(4, 12) ); // Virt node 4 to HAN node 12

  send_cmd_from_lan_to_han(IP6_ADDR, IP6_PORT, 16);
  TEST_ASSERT_TRUE( check_temp_assoc(2, IP5_ADDR, IP5_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(3, IP6_ADDR, IP6_PORT, false) ); // Virt node 3 is recycled
  TEST_ASSERT_TRUE( check_temp_assoc(4, IP3_ADDR, IP3_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(5, IP4_ADDR, IP4_PORT, false) );
  TEST_ASSERT_TRUE( check_last_HAN_message(3, 16) ); // Virt node 3 to HAN node 16

  send_cmd_from_lan_to_han(IP1_ADDR, IP1_PORT, 300);
  TEST_ASSERT_TRUE( check_temp_assoc(2, IP5_ADDR, IP5_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(3, IP6_ADDR, IP6_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4, IP3_ADDR, IP3_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(5, IP4_ADDR, IP4_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4002, IP1_ADDR, IP1_PORT, true) );
  TEST_ASSERT_TRUE( check_last_HAN_message(4002, 300) ); // Virt node 4002 to HAN node 300

  send_cmd_from_lan_to_han(IP2_ADDR, IP2_PORT, 400);
  TEST_ASSERT_TRUE( check_temp_assoc(2, IP5_ADDR, IP5_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(3, IP6_ADDR, IP6_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4, IP3_ADDR, IP3_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(5, IP4_ADDR, IP4_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4002, IP1_ADDR, IP1_PORT, true) );
  TEST_ASSERT_TRUE( check_temp_assoc(4003, IP2_ADDR, IP2_PORT, true) );
  TEST_ASSERT_TRUE( check_last_HAN_message(4003, 400) ); // Virt node 4003 to HAN node 400

  send_cmd_from_lan_to_han(IP7_ADDR, IP7_PORT, 500);
  TEST_ASSERT_TRUE( check_temp_assoc(2, IP5_ADDR, IP5_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(3, IP6_ADDR, IP6_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4, IP3_ADDR, IP3_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(5, IP4_ADDR, IP4_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4002, IP1_ADDR, IP1_PORT, true) );
  TEST_ASSERT_TRUE( check_temp_assoc(4003, IP2_ADDR, IP2_PORT, true) );
  TEST_ASSERT_TRUE( check_temp_assoc(4004, IP7_ADDR, IP7_PORT, true) );
  TEST_ASSERT_TRUE( check_last_HAN_message(4004, 500) ); // Virt node 4004 to HAN node 500

  send_cmd_from_lan_to_han(IP8_ADDR, IP8_PORT, 600);
  TEST_ASSERT_TRUE( check_temp_assoc(2, IP5_ADDR, IP5_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(3, IP6_ADDR, IP6_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4, IP3_ADDR, IP3_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(5, IP4_ADDR, IP4_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4002, IP1_ADDR, IP1_PORT, true) );
  TEST_ASSERT_TRUE( check_temp_assoc(4003, IP2_ADDR, IP2_PORT, true) );
  TEST_ASSERT_TRUE( check_temp_assoc(4004, IP7_ADDR, IP7_PORT, true) );
  TEST_ASSERT_TRUE( check_temp_assoc(4005, IP8_ADDR, IP8_PORT, true) );
  TEST_ASSERT_TRUE( check_last_HAN_message(4005, 600) ); // Virt node 4005 to HAN node 600

  send_cmd_from_lan_to_han(IP9_ADDR, IP9_PORT, 700);
  TEST_ASSERT_TRUE( check_temp_assoc(2, IP5_ADDR, IP5_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(3, IP6_ADDR, IP6_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4, IP3_ADDR, IP3_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(5, IP4_ADDR, IP4_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4002, IP9_ADDR, IP9_PORT, true) );
  TEST_ASSERT_TRUE( check_temp_assoc(4003, IP2_ADDR, IP2_PORT, true) );
  TEST_ASSERT_TRUE( check_temp_assoc(4004, IP7_ADDR, IP7_PORT, true) );
  TEST_ASSERT_TRUE( check_temp_assoc(4005, IP8_ADDR, IP8_PORT, true) );
  TEST_ASSERT_TRUE( check_last_HAN_message(4002, 700) ); // Virt node 4002 to HAN node 700

  send_cmd_from_lan_to_han(IP9_ADDR, IP9_PORT, 30);
  TEST_ASSERT_TRUE( check_temp_assoc(2, IP5_ADDR, IP5_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(3, IP6_ADDR, IP6_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4, IP9_ADDR, IP9_PORT, false) ); // Virt node 4 is recycled
  TEST_ASSERT_TRUE( check_temp_assoc(5, IP4_ADDR, IP4_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4002, IP9_ADDR, IP9_PORT, true) );
  TEST_ASSERT_TRUE( check_temp_assoc(4003, IP2_ADDR, IP2_PORT, true) );
  TEST_ASSERT_TRUE( check_temp_assoc(4004, IP7_ADDR, IP7_PORT, true) );
  TEST_ASSERT_TRUE( check_temp_assoc(4005, IP8_ADDR, IP8_PORT, true) );
  TEST_ASSERT_TRUE( check_last_HAN_message(4, 30) ); // Virt node 4 to HAN node 30

  send_cmd_from_han_to_lan(6, 2);
  TEST_ASSERT_TRUE( check_last_LAN_message(IP5_ADDR, IP5_PORT) );

  send_cmd_from_han_to_lan(6, 3);
  TEST_ASSERT_TRUE( check_last_LAN_message(IP6_ADDR, IP6_PORT) );

  send_cmd_from_han_to_lan(6, 4);
  TEST_ASSERT_TRUE( check_last_LAN_message(IP9_ADDR, IP9_PORT) );

  send_cmd_from_han_to_lan(6, 5);
  TEST_ASSERT_TRUE( check_last_LAN_message(IP4_ADDR, IP4_PORT) );

  send_cmd_from_han_to_lan(6, 4002);
  TEST_ASSERT_TRUE( check_last_LAN_message(IP9_ADDR, IP9_PORT) );

  send_cmd_from_han_to_lan(6, 4003);
  TEST_ASSERT_TRUE( check_last_LAN_message(IP2_ADDR, IP2_PORT) );

  send_cmd_from_han_to_lan(6, 4004);
  TEST_ASSERT_TRUE( check_last_LAN_message(IP7_ADDR, IP7_PORT) );

  send_cmd_from_han_to_lan(6, 4005);
  TEST_ASSERT_TRUE( check_last_LAN_message(IP8_ADDR, IP8_PORT) );

  temp_assoc_print_association_table();
}


/*
 * Send to multiple nodes from the same IP address/port.
 * Only a single association should be created
 */
void test_temp_association_destination_sharing(void)
{
  LOG_PRINTF("#####################################\n");
  LOG_PRINTF("%s\n", __func__);
  LOG_PRINTF("#####################################\n");

  setup_and_init_bridge_for_test();

  send_cmd_from_lan_to_han(IP1_ADDR, IP1_PORT, 6);
  TEST_ASSERT_TRUE( check_last_HAN_message(2, 6) ); // Virt node 2 to HAN node 6
  send_cmd_from_lan_to_han(IP1_ADDR, IP1_PORT, 7);
  TEST_ASSERT_TRUE( check_last_HAN_message(2, 7) ); // Virt node 2 to HAN node 7
  send_cmd_from_lan_to_han(IP1_ADDR, IP1_PORT, 8);
  TEST_ASSERT_TRUE( check_last_HAN_message(2, 8) ); // Virt node 2 to HAN node 8
  send_cmd_from_lan_to_han(IP1_ADDR, IP1_PORT, 9);
  TEST_ASSERT_TRUE( check_last_HAN_message(2, 9) ); // Virt node 2 to HAN node 9
  send_cmd_from_lan_to_han(IP1_ADDR, IP1_PORT, 10);
  TEST_ASSERT_TRUE( check_last_HAN_message(2, 10) ); // Virt node 2 to HAN node 10

  send_cmd_from_lan_to_han(IP1_ADDR, IP1_PORT, 1000);
  TEST_ASSERT_TRUE( check_last_HAN_message(4002, 1000) ); // Virt node 4002 to HAN node 1000
  send_cmd_from_lan_to_han(IP1_ADDR, IP1_PORT, 1001);
  TEST_ASSERT_TRUE( check_last_HAN_message(4002, 1001) ); // Virt node 4002 to HAN node 1001
  send_cmd_from_lan_to_han(IP1_ADDR, IP1_PORT, 1002);
  TEST_ASSERT_TRUE( check_last_HAN_message(4002, 1002) ); // Virt node 4002 to HAN node 1002
  send_cmd_from_lan_to_han(IP1_ADDR, IP1_PORT, 1003);
  TEST_ASSERT_TRUE( check_last_HAN_message(4002, 1003) ); // Virt node 4002 to HAN node 1003
  send_cmd_from_lan_to_han(IP1_ADDR, IP1_PORT, 1004);
  TEST_ASSERT_TRUE( check_last_HAN_message(4002, 1004) ); // Virt node 4002 to HAN node 1004

  TEST_ASSERT_TRUE( check_temp_assoc(2, IP1_ADDR, IP1_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(3, 0, 0, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4, 0, 0, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(5, 0, 0, false) );

  TEST_ASSERT_TRUE( check_temp_assoc(4002, IP1_ADDR, IP1_PORT, true) );
  TEST_ASSERT_TRUE( check_temp_assoc(4003, 0, 0, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4004, 0, 0, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4005, 0, 0, false) );

  send_cmd_from_lan_to_han(IP2_ADDR, IP2_PORT, 7);
  TEST_ASSERT_TRUE( check_last_HAN_message(3, 7) ); // Virt node 3 to HAN node 7
  send_cmd_from_lan_to_han(IP2_ADDR, IP2_PORT, 8);
  TEST_ASSERT_TRUE( check_last_HAN_message(3, 8) ); // Virt node 3 to HAN node 8

  send_cmd_from_lan_to_han(IP2_ADDR, IP2_PORT, 1000);
  TEST_ASSERT_TRUE( check_last_HAN_message(4003, 1000) ); // Virt node 3 to HAN node 7
  send_cmd_from_lan_to_han(IP2_ADDR, IP2_PORT, 1001);
  TEST_ASSERT_TRUE( check_last_HAN_message(4003, 1001) ); // Virt node 3 to HAN node 8

  TEST_ASSERT_TRUE( check_temp_assoc(2, IP1_ADDR, IP1_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(3, IP2_ADDR, IP2_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4, 0, 0, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(5, 0, 0, false) );

  TEST_ASSERT_TRUE( check_temp_assoc(4002, IP1_ADDR, IP1_PORT, true) );
  TEST_ASSERT_TRUE( check_temp_assoc(4003, IP2_ADDR, IP2_PORT, true) );
  TEST_ASSERT_TRUE( check_temp_assoc(4, 0, 0, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(5, 0, 0, false) );

  send_cmd_from_han_to_lan(6, 2);
  TEST_ASSERT_TRUE( check_last_LAN_message(IP1_ADDR, IP1_PORT) );

  send_cmd_from_han_to_lan(7, 2);
  TEST_ASSERT_TRUE( check_last_LAN_message(IP1_ADDR, IP1_PORT) );

  send_cmd_from_han_to_lan(6, 3);
  TEST_ASSERT_TRUE( check_last_LAN_message(IP2_ADDR, IP2_PORT) );

  send_cmd_from_han_to_lan(7, 3);
  TEST_ASSERT_TRUE( check_last_LAN_message(IP2_ADDR, IP2_PORT) );

  send_cmd_from_han_to_lan(6, 4002);
  TEST_ASSERT_TRUE( check_last_LAN_message(IP1_ADDR, IP1_PORT) );

  send_cmd_from_han_to_lan(7, 4002);
  TEST_ASSERT_TRUE( check_last_LAN_message(IP1_ADDR, IP1_PORT) );

  send_cmd_from_han_to_lan(6, 4003);
  TEST_ASSERT_TRUE( check_last_LAN_message(IP2_ADDR, IP2_PORT) );

  send_cmd_from_han_to_lan(7, 4003);
  TEST_ASSERT_TRUE( check_last_LAN_message(IP2_ADDR, IP2_PORT) );

  temp_assoc_print_association_table();
}

/*
 * Reproduce the system test leading to bug report ZGW-2517
 *
 * The test shows that the observations for ZGW-2517 are a result of the temp
 * association table can only hols four entries. Hence it's a design limitation
 * and not a bug.
 */
void test_validate_zgw2517(void)
{
  LOG_PRINTF("#####################################\n");
  LOG_PRINTF("%s\n", __func__);
  LOG_PRINTF("#####################################\n");

  const char *IP_ADDR = "fd00:adba::b528:4bcb:ab53:000e";

  setup_and_init_bridge_for_test();

  send_cmd_from_lan_to_han(IP_ADDR, 4001, 6);
  TEST_ASSERT_TRUE( check_last_HAN_message(2, 6) ); // Virt node 2 to HAN node 6

  send_cmd_from_lan_to_han(IP_ADDR, 4002, 7);
  TEST_ASSERT_TRUE( check_last_HAN_message(3, 7) ); // Virt node 3 to HAN node 7

  send_cmd_from_lan_to_han(IP_ADDR, 4003, 8);
  TEST_ASSERT_TRUE( check_last_HAN_message(4, 8) );  // Virt node 4 to HAN node 8

  send_cmd_from_lan_to_han(IP_ADDR, 4004, 9);
  TEST_ASSERT_TRUE( check_last_HAN_message(5, 9) ); // Virt node 5 to HAN node 9

  // Check the association table
  TEST_ASSERT_TRUE( check_temp_assoc(2, IP_ADDR, 4001, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(3, IP_ADDR, 4002, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4, IP_ADDR, 4003, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(5, IP_ADDR, 4004, false) );

  // --> The association table is now "full" (max four entries)

  /* Send one more message from yet another unique IP address/port. Since the
   * association table is full the oldest association (virt node 2) are recycled
   * and the message will be sent from virtual node 2 to HAN node 10.
   */
  send_cmd_from_lan_to_han(IP_ADDR, 4005, 10);
  TEST_ASSERT_TRUE( check_last_HAN_message(2, 10) ); // Virt node 2 to HAN node 10

  TEST_ASSERT_TRUE( check_temp_assoc(2, IP_ADDR, 4005, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(3, IP_ADDR, 4002, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4, IP_ADDR, 4003, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(5, IP_ADDR, 4004, false) );

  /* The original message to HAN node 6 was sent from port 4001. That
   * association was stored for virtual node 2. But since then the association
   * has been recycled while sending from port 4005 to node 10. Virtual node 2
   * is now assigned port 4005, so the reply from node 6 goes to the wrong port.
   * Nothing to do about this :-(
   */
  send_cmd_from_han_to_lan(6, 2);
  TEST_ASSERT_TRUE( check_last_LAN_message(IP_ADDR, 4005) );

  send_cmd_from_han_to_lan(7, 3);
  TEST_ASSERT_TRUE( check_last_LAN_message(IP_ADDR, 4002) );

  send_cmd_from_han_to_lan(8, 4);
  TEST_ASSERT_TRUE( check_last_LAN_message(IP_ADDR, 4003) );

  send_cmd_from_han_to_lan(9, 5);
  TEST_ASSERT_TRUE( check_last_LAN_message(IP_ADDR, 4004) );

  send_cmd_from_han_to_lan(10, 2);
  TEST_ASSERT_TRUE( check_last_LAN_message(IP_ADDR, 4005) );

  temp_assoc_print_association_table();
}


/*
 * Initiate firmware update to a node and check the temporary association
 * created by that does not get recycled until the firmware update lock timer
 * expires.
 */
void test_temp_association_fw_lock(void)
{
  LOG_PRINTF("#####################################\n");
  LOG_PRINTF("%s\n", __func__);
  LOG_PRINTF("#####################################\n");

  setup_and_init_bridge_for_test();

  send_cmd_from_lan_to_han(IP1_ADDR, IP1_PORT, 6);
  TEST_ASSERT_TRUE( check_last_HAN_message(2, 6) );

  // A firmware update command puts a lock on the virtual node 3 association
  send_fw_upd_cmd_from_lan_to_han(IP2_ADDR, IP2_PORT, 7);
  TEST_ASSERT_TRUE( check_last_HAN_message_fw_upd(3, 7) );

  send_cmd_from_lan_to_han(IP3_ADDR, IP3_PORT, 8);
  TEST_ASSERT_TRUE( check_last_HAN_message(4, 8) );

  send_cmd_from_lan_to_han(IP4_ADDR, IP4_PORT, 9);
  TEST_ASSERT_TRUE( check_last_HAN_message(5, 9) );

  TEST_ASSERT_TRUE( check_temp_assoc(2, IP1_ADDR, IP1_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(3, IP2_ADDR, IP2_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4, IP3_ADDR, IP3_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(5, IP4_ADDR, IP4_PORT, false) );

  // Recycling virtual node 2 association
  send_cmd_from_lan_to_han(IP5_ADDR, IP5_PORT, 10);
  TEST_ASSERT_TRUE( check_last_HAN_message(2, 10) );

  // Recycling virtual node 4 association (virtual node 3 is locked for firmware update)
  send_cmd_from_lan_to_han(IP6_ADDR, IP6_PORT, 11);
  TEST_ASSERT_TRUE( check_last_HAN_message(4, 11) );

  // Release firmware update lock on virtual node 3
  temp_assoc_fw_lock_release_on_timeout(temp_assoc_lookup_by_virtual_nodeid(3));

  send_cmd_from_lan_to_han(IP1_ADDR, IP1_PORT, 12);
  TEST_ASSERT_TRUE( check_last_HAN_message(3, 12) );

  temp_assoc_print_association_table();
}

/*
 * Initiate firmware update from zip client A followed by yet another firmware
 * update from zip client B. Observe that temporary associations are handled
 * according to the current limitation of only being able to lock a single
 * association for firmware update.
 */
void test_temp_association_fw_lock_dual(void)
{
  LOG_PRINTF("#####################################\n");
  LOG_PRINTF("%s\n", __func__);
  LOG_PRINTF("#####################################\n");

  setup_and_init_bridge_for_test();

  send_cmd_from_lan_to_han(IP1_ADDR, IP1_PORT, 6);
  TEST_ASSERT_TRUE( check_last_HAN_message(2, 6) );

  // A firmware update command puts a lock on the virtual node 3 association
  send_fw_upd_cmd_from_lan_to_han(IP2_ADDR, IP2_PORT, 7);
  TEST_ASSERT_TRUE( check_last_HAN_message_fw_upd(3, 7) );

  // Another firmware update command puts a lock on the virtual node 4 association
  // Since there is only one lock available globally the lock on node 3 is implicitly released
  send_fw_upd_cmd_from_lan_to_han(IP3_ADDR, IP3_PORT, 8);
  TEST_ASSERT_TRUE( check_last_HAN_message_fw_upd(4, 8) );

  send_cmd_from_lan_to_han(IP4_ADDR, IP4_PORT, 9);
  TEST_ASSERT_TRUE( check_last_HAN_message(5, 9) );

  TEST_ASSERT_TRUE( check_temp_assoc(2, IP1_ADDR, IP1_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(3, IP2_ADDR, IP2_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(4, IP3_ADDR, IP3_PORT, false) );
  TEST_ASSERT_TRUE( check_temp_assoc(5, IP4_ADDR, IP4_PORT, false) );

  // Recycling virtual node 2 association
  send_cmd_from_lan_to_han(IP5_ADDR, IP5_PORT, 10);
  TEST_ASSERT_TRUE( check_last_HAN_message(2, 10) );

  // Recycling virtual node 3 association (no longer locked)
  send_cmd_from_lan_to_han(IP6_ADDR, IP6_PORT, 11);
  TEST_ASSERT_TRUE( check_last_HAN_message(3, 11) );

  // Recycling virtual node 5 association (virtual node 4 is locked for firmware update)
  send_cmd_from_lan_to_han(IP1_ADDR, IP1_PORT, 12);
  TEST_ASSERT_TRUE( check_last_HAN_message(5, 12) );

  // Release firmware update lock on virtual node 4
  temp_assoc_fw_lock_release_on_timeout(temp_assoc_lookup_by_virtual_nodeid(4));

  send_cmd_from_lan_to_han(IP2_ADDR, IP2_PORT, 12);
  TEST_ASSERT_TRUE( check_last_HAN_message(4, 12) );

  temp_assoc_print_association_table();
}

void test_ack_nack(void) {
  const uint8_t test_frame[] = "MyTestFrame";
  setup_and_init_bridge_for_test();

  my_queue_state = QS_SENDING_FIRST;

  /*
  * When the queue is in sending first state we should get ACK or NACK on all state except for 
  * TRANSMIT_COMPLETE_NO_ACK, where we expect not to get UDP status.
  */
  send_cmd_from_lan_to_han_ex(IP1_ADDR, IP1_PORT, 6, test_frame, sizeof(test_frame),TRANSMIT_COMPLETE_FAIL);
  TEST_ASSERT_EQUAL(1,lan_msg_count);
  TEST_ASSERT_TRUE(ZIP_PKT_BUF->flags0 & ZIP_PACKET_FLAGS0_NACK_RES );

  send_cmd_from_lan_to_han_ex(IP1_ADDR, IP1_PORT, 6, test_frame, sizeof(test_frame),TRANSMIT_ROUTING_NOT_IDLE);
  TEST_ASSERT_EQUAL(2,lan_msg_count);
  TEST_ASSERT_TRUE(ZIP_PKT_BUF->flags0 & ZIP_PACKET_FLAGS0_NACK_RES );

  send_cmd_from_lan_to_han_ex(IP1_ADDR, IP1_PORT, 6, test_frame, sizeof(test_frame),TRANSMIT_COMPLETE_NO_ACK);
  TEST_ASSERT_EQUAL(2,lan_msg_count);

  send_cmd_from_lan_to_han_ex(IP1_ADDR, IP1_PORT, 6, test_frame, sizeof(test_frame),TRANSMIT_COMPLETE_OK);
  TEST_ASSERT_EQUAL(3,lan_msg_count);
  TEST_ASSERT_TRUE(ZIP_PKT_BUF->flags0 & ZIP_PACKET_FLAGS0_ACK_RES );

  my_queue_state = QS_SENDING_LONG;
  lan_msg_count  =0 ;

  /*
  * In sending long state we must alway get a status.
  */
  send_cmd_from_lan_to_han_ex(IP1_ADDR, IP1_PORT, 6, test_frame, sizeof(test_frame),TRANSMIT_COMPLETE_FAIL);
  TEST_ASSERT_EQUAL(1,lan_msg_count);
  TEST_ASSERT_TRUE(ZIP_PKT_BUF->flags0 & ZIP_PACKET_FLAGS0_NACK_RES );

  send_cmd_from_lan_to_han_ex(IP1_ADDR, IP1_PORT, 6, test_frame, sizeof(test_frame),TRANSMIT_ROUTING_NOT_IDLE);
  TEST_ASSERT_EQUAL(2,lan_msg_count);
  TEST_ASSERT_TRUE(ZIP_PKT_BUF->flags0 & ZIP_PACKET_FLAGS0_NACK_RES );

  send_cmd_from_lan_to_han_ex(IP1_ADDR, IP1_PORT, 6, test_frame, sizeof(test_frame),TRANSMIT_COMPLETE_NO_ACK);
  TEST_ASSERT_EQUAL(3,lan_msg_count);
  TEST_ASSERT_TRUE(ZIP_PKT_BUF->flags0 & ZIP_PACKET_FLAGS0_NACK_RES );

  send_cmd_from_lan_to_han_ex(IP1_ADDR, IP1_PORT, 6, test_frame, sizeof(test_frame),TRANSMIT_COMPLETE_OK);
  TEST_ASSERT_EQUAL(4,lan_msg_count);
  TEST_ASSERT_TRUE(ZIP_PKT_BUF->flags0 & ZIP_PACKET_FLAGS0_ACK_RES );

}