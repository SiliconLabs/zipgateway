/* © 2025 – CC_GatewayKeepAlive unit tests */

#include "CC_GatewayKeepAlive.h"
#include "Serialapi.h"
#include "zip_router_config.h"
#include "ZW_classcmd.h"
#include "ZW_classcmd_ex.h"
#include <unity.h>
#include <string.h>

/* Mock capture for SerialAPI calls */
static int g_serialapi_send_clear_ret = (int)SERIALAPI_HOST_HIBERNATION_CLEAR_STATUS_SUCCESS_MIN;
static int serialapi_send_important_devices_clear_called;
static int serialapi_send_important_device_list_called;
static const void *serialapi_send_list_arg;
static uint16_t serialapi_send_count_arg;
static int serialapi_notify_host_state_called;
static uint8_t serialapi_notify_state_arg;
static uint8_t serialapi_notify_severity_arg;
static int serialapi_request_s2_called;
static uint16_t serialapi_request_s2_node_arg;
static int serialapi_request_wakeup_report_called;

/* Mock capture for ClassicZIPNode_SendUnsolicited */
static int classic_zip_send_unsolicited_called;
static uint8_t classic_zip_send_buf[270];
static uint8_t classic_zip_send_len;

/*
 * gw_keepalive_init() calls SerialAPI_GetModuleCapabilities(); without --wrap the real
 * implementation blocks on serial I/O and the test binary hangs under ctest.
 */
int __wrap_SerialAPI_GetModuleCapabilities(uint8_t *max_devices, uint8_t *max_frame_length)
{
  if (max_devices != NULL)
    *max_devices = (uint8_t)GW_IMPORTANT_NODE_MAX_ENTRIES;
  if (max_frame_length != NULL)
    *max_frame_length = GW_IMPORTANT_LIST_FRAME_BYTES;
  return SERIALAPI_HOST_HIBERNATION_OK;
}

int __wrap_SerialAPI_SendImportantDevicesClear(void)
{
  serialapi_send_important_devices_clear_called++;
  return g_serialapi_send_clear_ret;
}

int __wrap_SerialAPI_SendImportantDeviceList(const void *entries, uint16_t count, uint8_t max_frame_length)
{
  (void)max_frame_length;
  serialapi_send_important_device_list_called++;
  serialapi_send_list_arg = entries;
  serialapi_send_count_arg = count;
  return SERIALAPI_HOST_HIBERNATION_OK;
}

int __wrap_SerialAPI_NotifyHostState(uint8_t host_state, uint8_t severity_level,
                                     uint8_t *ncp_severity_level)
{
  serialapi_notify_host_state_called++;
  serialapi_notify_state_arg = host_state;
  serialapi_notify_severity_arg = severity_level;
  if (ncp_severity_level != NULL)
    *ncp_severity_level = severity_level;
  return SERIALAPI_HOST_HIBERNATION_OK;
}

int __wrap_SerialAPI_RequestS2MessageCountList(uint16_t node_id)
{
  serialapi_request_s2_called++;
  serialapi_request_s2_node_arg = node_id;
  return SERIALAPI_HOST_HIBERNATION_OK;
}

int __wrap_SerialAPI_RequestWakeupReport(void)
{
  serialapi_request_wakeup_report_called++;
  return SERIALAPI_HOST_HIBERNATION_OK;
}

void __wrap_ClassicZIPNode_SendUnsolicited(const void *conn, const void *buf, uint8_t len,
                                           const void *dest, uint16_t port, int flag)
{
  (void)conn;
  (void)dest;
  (void)port;
  (void)flag;
  classic_zip_send_unsolicited_called++;
  classic_zip_send_len = len;
  memcpy(classic_zip_send_buf, buf, len);
}

static void reset_mocks(void)
{
  g_serialapi_send_clear_ret = (int)SERIALAPI_HOST_HIBERNATION_CLEAR_STATUS_SUCCESS_MIN;
  serialapi_send_important_devices_clear_called = 0;
  serialapi_send_important_device_list_called = 0;
  serialapi_send_list_arg = NULL;
  serialapi_send_count_arg = 0;
  serialapi_notify_host_state_called = 0;
  serialapi_notify_state_arg = 0;
  serialapi_notify_severity_arg = 0;
  serialapi_request_s2_called = 0;
  serialapi_request_s2_node_arg = 0;
  serialapi_request_wakeup_report_called = 0;
  classic_zip_send_unsolicited_called = 0;
  classic_zip_send_len = 0;
}

void setUp(void)
{
  reset_mocks();
  memset(&cfg, 0, sizeof(cfg));
  gw_keepalive_init();
}

void test_gw_keepalive_init(void)
{
  uint16_t count;
  gw_keepalive_init();
  TEST_ASSERT_EQUAL(GW_APP_STATE_AWAKE, gw_keepalive_get_app_state());
  TEST_ASSERT_NULL(gw_keepalive_get_node_list(&count));
  TEST_ASSERT_EQUAL(0, count);
}

void test_gw_keepalive_handle_important_node_set_empty(void)
{
  uint8_t payload[] = {0x00, 0x00}; /* count=0 */
  int ret = gw_keepalive_handle_important_node_set(payload, sizeof(payload));
  TEST_ASSERT_EQUAL(GW_KEEPALIVE_OK, ret);
  TEST_ASSERT_EQUAL(1, serialapi_send_important_devices_clear_called);
  TEST_ASSERT_EQUAL(0, serialapi_send_important_device_list_called);
}

void test_gw_keepalive_handle_important_node_set_empty_clear_not_accepted(void)
{
  g_serialapi_send_clear_ret = (int)SERIALAPI_HOST_HIBERNATION_CLEAR_STATUS_NOT_ACCEPTED_OR_ERROR; /* wire 0x00 */
  uint8_t payload[] = {0x00, 0x00};
  int ret = gw_keepalive_handle_important_node_set(payload, sizeof(payload));

  TEST_ASSERT_EQUAL(GW_KEEPALIVE_ERR_CLEAR_NOT_ACCEPTED, ret);
  TEST_ASSERT_EQUAL(1, serialapi_send_important_devices_clear_called);
}

void test_gw_keepalive_handle_important_node_set_one_entry(void)
{
  /* count=1, node_id=5, keep_alive=60 (LSB first) */
  uint8_t payload[] = {0x01, 0x00, 0x05, 0x00, 0x3C, 0x00};
  int ret = gw_keepalive_handle_important_node_set(payload, sizeof(payload));
  TEST_ASSERT_EQUAL(GW_KEEPALIVE_OK, ret);
  TEST_ASSERT_EQUAL(0, serialapi_send_important_devices_clear_called);
  TEST_ASSERT_EQUAL(1, serialapi_send_important_device_list_called);
  TEST_ASSERT_NOT_NULL(serialapi_send_list_arg);
  TEST_ASSERT_EQUAL(1, serialapi_send_count_arg);

  uint16_t count;
  const gw_important_node_entry_t *list = gw_keepalive_get_node_list(&count);
  TEST_ASSERT_NOT_NULL(list);
  TEST_ASSERT_EQUAL(1, count);
  TEST_ASSERT_EQUAL(5, list[0].node_id);
  TEST_ASSERT_EQUAL(60, list[0].keep_alive_win_min);
}

void test_gw_keepalive_handle_important_node_set_five_entries(void)
{
  uint8_t payload[] = {
      0x05, 0x00,                           /* count=5 */
      0x05, 0x00, 0x3C, 0x00,              /* node_id=5, keep_alive=60 */
      0x0C, 0x00, 0x78, 0x00,              /* node_id=12, keep_alive=120 */
      0x03, 0x00, 0x1E, 0x00,              /* node_id=3, keep_alive=30 */
      0x07, 0x00, 0x5A, 0x00,              /* node_id=7, keep_alive=90 */
      0x0F, 0x00, 0x2D, 0x00,              /* node_id=15, keep_alive=45 */
  };
  static const uint16_t expected_node[] = {5, 12, 3, 7, 15};
  static const uint16_t expected_keep[] = {60, 120, 30, 90, 45};

  int ret = gw_keepalive_handle_important_node_set(payload, sizeof(payload));
  TEST_ASSERT_EQUAL(GW_KEEPALIVE_OK, ret);
  TEST_ASSERT_EQUAL(1, serialapi_send_important_device_list_called);
  TEST_ASSERT_EQUAL(5, serialapi_send_count_arg);

  uint16_t count;
  const gw_important_node_entry_t *list = gw_keepalive_get_node_list(&count);
  TEST_ASSERT_NOT_NULL(list);
  TEST_ASSERT_EQUAL(5, count);
  for (int i = 0; i < 5; i++) {
    TEST_ASSERT_EQUAL(expected_node[i], list[i].node_id);
    TEST_ASSERT_EQUAL(expected_keep[i], list[i].keep_alive_win_min);
  }
}

void test_gw_keepalive_handle_important_node_set_invalid_len(void)
{
  uint8_t payload[] = {0x01}; /* too short */
  int ret = gw_keepalive_handle_important_node_set(payload, sizeof(payload));
  TEST_ASSERT_EQUAL(GW_KEEPALIVE_ERR_INVALID_PAYLOAD, ret);
}

void test_gw_keepalive_handle_important_node_set_invalid_count(void)
{
  uint8_t payload[] = {0xE9, 0x00}; /* count=233 > GW_IMPORTANT_NODE_MAX_ENTRIES (232) */
  int ret = gw_keepalive_handle_important_node_set(payload, sizeof(payload));
  TEST_ASSERT_EQUAL(GW_KEEPALIVE_ERR_INVALID_PAYLOAD, ret);
}

void test_gw_keepalive_handle_app_state_set_asleep(void)
{
  int ret = gw_keepalive_handle_app_state_set(GW_APP_STATE_ASLEEP, 0x03);
  TEST_ASSERT_EQUAL(GW_KEEPALIVE_OK, ret);
  TEST_ASSERT_EQUAL(GW_APP_STATE_ASLEEP, gw_keepalive_get_app_state());
  TEST_ASSERT_EQUAL(1, serialapi_notify_host_state_called);
  TEST_ASSERT_EQUAL(HOST_HIBERNATION_STATE_GOING_TO_SLEEP, serialapi_notify_state_arg);
  TEST_ASSERT_EQUAL(0x03, serialapi_notify_severity_arg);
  TEST_ASSERT_EQUAL(0, serialapi_request_s2_called); /* S2 list only on wake; reconcile uses NCP counts directly */
  TEST_ASSERT_EQUAL(0, serialapi_send_important_devices_clear_called);
}

void test_gw_keepalive_handle_app_state_set_awake(void)
{
  int ret = gw_keepalive_handle_app_state_set(GW_APP_STATE_AWAKE, 0x00);
  TEST_ASSERT_EQUAL(GW_KEEPALIVE_OK, ret);
  TEST_ASSERT_EQUAL(GW_APP_STATE_AWAKE, gw_keepalive_get_app_state());
  TEST_ASSERT_EQUAL(1, serialapi_send_important_devices_clear_called);
  TEST_ASSERT_EQUAL(1, serialapi_notify_host_state_called);
  TEST_ASSERT_EQUAL(0x00, serialapi_notify_severity_arg);
  TEST_ASSERT_EQUAL(1, serialapi_request_s2_called);
  TEST_ASSERT_EQUAL(0, serialapi_request_s2_node_arg);
  TEST_ASSERT_EQUAL(1, serialapi_request_wakeup_report_called);
  TEST_ASSERT_EQUAL(0, classic_zip_send_unsolicited_called);
}

void test_gw_keepalive_handle_app_state_set_awake_wake_notify_with_dest(void)
{
  /* unsolicited_dest + lan_addr set; AWAKE path still only calls SerialAPI (no wake notify here). */
  cfg.unsolicited_dest.u8[0] = 0xFE;
  cfg.unsolicited_dest.u8[1] = 0x80;
  cfg.lan_addr.u8[15] = 0x01;

  int ret = gw_keepalive_handle_app_state_set(GW_APP_STATE_AWAKE, 0x00);
  TEST_ASSERT_EQUAL(GW_KEEPALIVE_OK, ret);
  TEST_ASSERT_EQUAL(GW_APP_STATE_AWAKE, gw_keepalive_get_app_state());
  TEST_ASSERT_EQUAL(1, serialapi_send_important_devices_clear_called);
  TEST_ASSERT_EQUAL(1, serialapi_notify_host_state_called);
  TEST_ASSERT_EQUAL(1, serialapi_request_s2_called);
  TEST_ASSERT_EQUAL(1, serialapi_request_wakeup_report_called);
  TEST_ASSERT_EQUAL(0, classic_zip_send_unsolicited_called);
}

void test_gw_keepalive_handle_app_state_set_invalid(void)
{
  int ret = gw_keepalive_handle_app_state_set(0xFF, 0x00);
  TEST_ASSERT_EQUAL(GW_KEEPALIVE_ERR_INVALID_APP_STATE, ret);
}

void test_gw_keepalive_send_wake_notify_unspecified_dest(void)
{
  memset(&cfg, 0, sizeof(cfg));
  gw_keepalive_send_wake_notify(GW_WAKE_REASON_DEVICE_LOST, NULL, 0);
  TEST_ASSERT_EQUAL(0, classic_zip_send_unsolicited_called);
}

void test_gw_keepalive_send_wake_notify_with_dest(void)
{
  memset(&cfg, 0, sizeof(cfg));
  cfg.unsolicited_dest.u8[0] = 0xFE;
  cfg.unsolicited_dest.u8[1] = 0x80;
  cfg.lan_addr.u8[15] = 0x01;
  gw_keepalive_send_wake_notify(GW_WAKE_REASON_URGENT_NODE, (const uint8_t[]){0x00, 0x05, 0x02, 0x03}, 4);
  TEST_ASSERT_EQUAL(1, classic_zip_send_unsolicited_called);
  TEST_ASSERT_EQUAL(COMMAND_CLASS_ZIP_GATEWAY, classic_zip_send_buf[0]);
  TEST_ASSERT_EQUAL(GATEWAY_WAKE_NOTIFY_REPORT, classic_zip_send_buf[1]);
  TEST_ASSERT_EQUAL(GW_WAKE_REASON_URGENT_NODE, classic_zip_send_buf[2]);
  TEST_ASSERT_EQUAL(0, classic_zip_send_buf[3]);
  TEST_ASSERT_EQUAL(4, classic_zip_send_buf[4]);
  TEST_ASSERT_EQUAL(0, classic_zip_send_buf[5]);
  TEST_ASSERT_EQUAL(0x00, classic_zip_send_buf[6]);
  TEST_ASSERT_EQUAL(0x05, classic_zip_send_buf[7]);
  TEST_ASSERT_EQUAL(0x02, classic_zip_send_buf[8]);
  TEST_ASSERT_EQUAL(0x03, classic_zip_send_buf[9]);
}
