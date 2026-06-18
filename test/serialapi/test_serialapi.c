/* © 2023 Silicon Laboratories Inc.  */
#include <Serialapi.h>
#include <ZW_SerialAPI.h>
#include <ZW_classcmd_ex.h>
#include <ZIP_Router_logging.h>
#include <pthread.h>
#include <pty.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <unity.h>

#define MAX_S2_ROW_CALLBACKS 5

#define CHECKSUM 0xFF
#define TEST_ZW_VERSION                                                        \
  0x7A, 0x65, 0x65, 0x77, 0x61, 0x76, 0x65, 0x72, 0x6F, 0x63, 0x6B, 0x73
#define SUPPORTED_CMDS                                                         \
  0xFF, 0x05, 0xFF, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0xFF, 0x0a, 0x0b,      \
      0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0xFF,  \
      0x18, 0x19, 0x1a, 0x1b, 0x1c
#define SUPPORTED_API_CMDS                                                     \
  0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,      \
      0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0xFF,  \
      0x18, 0x19, 0x1a, 0x1b, 0x1c
#define BITMASK                                                                \
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,      \
      0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,  \
      0x18, 0x19, 0x1a, 0x1b, 0x1c
#define CAPS                                                                   \
  0xA0, 0xA1, 0xB0, 0xB1, 0xC0, 0xC1, 0xD0, 0xD1, SUPPORTED_CMDS,              \
      SUPPORTED_API_CMDS, BITMASK, 0xE0, 0xE1

#define TEST_LR_ENABLED(event_loop)                                            \
  TEST_ASSERT_FALSE(pthread_create(&event_loop, NULL, Device_ToggleLR, NULL)); \
  TEST_ASSERT_TRUE(SerialAPI_EnableLR());                                      \
  TEST_ASSERT_FALSE(pthread_join(event_loop, NULL));

#define TEST_LR_DISABLED(event_loop)                                           \
  TEST_ASSERT_FALSE(pthread_create(&event_loop, NULL, Device_ToggleLR, NULL)); \
  TEST_ASSERT_TRUE(SerialAPI_DisableLR());                                     \
  TEST_ASSERT_FALSE(pthread_join(event_loop, NULL));

typedef struct {
  uint8_t data[0xFF];
  size_t size;
} payload_t;

typedef struct {
  payload_t rx;
  payload_t tx;
  bool is_timeout;
} session_t;

static int end_device;
static bool is_cb_called;
static bool is_device_lost_called;
static uint8_t device_lost_data[32];
static uint16_t device_lost_data_len;

static int s2_row_callback_count;
static uint8_t s2_row_callback_entries[MAX_S2_ROW_CALLBACKS][4];

static void TestApplicationCommandHandler(BYTE rxStatus, uint16_t destNode,
                                          uint16_t sourceNode,
                                          ZW_APPLICATION_TX_BUFFER *pCmd,
                                          BYTE cmdLength) {
  DBG_PRINTF("TestApplicationCommandHandler\n");
  is_cb_called = TRUE;
}

static void TestApplicationControllerUpdate(BYTE bStatus, uint16_t bNodeID,
                                            BYTE *pCmd, BYTE bLen, BYTE *foo) {
  DBG_PRINTF("TestApplicationControllerUpdate\n");
  is_cb_called = TRUE;
}

static void TestSerialAPIStarted(BYTE *pData, BYTE pLen) {
  DBG_PRINTF("TestSerialAPIStarted\n");
  is_cb_called = TRUE;
}

static void TestOnDeviceLost(const uint8_t *payload, uint16_t payload_len) {
  is_device_lost_called = TRUE;
  device_lost_data_len = payload_len;
  if (payload != NULL && payload_len > 0 && payload_len <= sizeof(device_lost_data))
    memcpy(device_lost_data, payload, payload_len);
}

static void TestOnNcpS2CountSync(uint16_t node_id, uint8_t s2_count,
                                 uint8_t last_seq) {
  if (s2_row_callback_count < MAX_S2_ROW_CALLBACKS) {
    uint8_t *e = s2_row_callback_entries[s2_row_callback_count++];
    e[0] = (uint8_t)(node_id >> 8);
    e[1] = (uint8_t)(node_id & 0xFF);
    e[2] = s2_count;
    e[3] = last_seq;
  }
}

static const struct SerialAPI_Callbacks callbacks = {
    TestApplicationCommandHandler,
    0,
    TestApplicationControllerUpdate,
    0,
    0,
    0,
    0,
    TestApplicationCommandHandler,
    TestSerialAPIStarted,
    TestOnNcpS2CountSync,
    TestOnDeviceLost
};

void apply_checksum(payload_t *payload) {
  uint8_t bChecksum = 0xFF;

  if (CHECKSUM != payload->data[payload->size - 1]) {
    abort();
  }

  for (int i = 0; i < payload->size - 2; i++) {
    bChecksum ^= payload->data[i + 1];
  }

  payload->data[payload->size - 1] = bChecksum;
}

uint8_t Device_ReceiveFrame(session_t *session) {
  uint8_t rx_ack = ACK;
  const uint8_t tx_ack = ACK;

  if (session->rx.size !=
      read(end_device, &session->rx.data, session->rx.size)) {
    abort();
  }

  write(end_device, &tx_ack, 1);

  if (session->tx.size == 0) {
    return TRUE;
  }

  apply_checksum(&session->tx);
  write(end_device, &session->tx.data, session->tx.size);

  if (1 != read(end_device, &rx_ack, 1) || ACK != rx_ack) {
    DBG_PRINTF("Expected ACK but received %02x", rx_ack);
    abort();
  }

  return TRUE;
}

uint8_t Device_SendFrame(session_t *session) {
  apply_checksum(&session->tx);
  write(end_device, &session->tx.data, session->tx.size);

  {
    fd_set rfds;
    struct timeval ack_timeout = {.tv_usec = 500};

    FD_ZERO(&rfds);
    FD_SET(end_device, &rfds);

    int data_ready =
        select(end_device + 1, &rfds, NULL, NULL, &ack_timeout);
    if (-1 == data_ready) {
      abort();
    } else if (0 == data_ready) {
      session->is_timeout = TRUE;
      DBG_PRINTF("Device_SendFrame timed out\n");
    } else {
      if (1 != read(end_device, &session->rx.data, 1) ||
          ACK != session->rx.data[0]) {
        DBG_PRINTF("Expected ACK but received %02x", session->rx.data[0]);
        abort();
      }

      DBG_PRINTF("Device_SendFrame succeeded\n");
    }

    FD_CLR(end_device, &rfds);
  }

  return TRUE;
}

static void *Device_Init(void *ptr) /* NOSONAR: pthread start routine signature */
{
  (void)ptr;
  uint8_t rx_ack = ACK;
  const uint8_t tx_ack = ACK;
  session_t sessions[4] = {
      {.tx = {.data = {SOF, 0x64, RESPONSE, FUNC_ID_SERIAL_API_GET_CAPABILITIES,
                       CAPS, CHECKSUM},
              .size = 0x66},
       .rx = {.size = 0x05}},
      {.tx = {.data = {SOF, 0x20, RESPONSE, FUNC_ID_SERIALAPI_SETUP,
                       SUPPORTED_API_CMDS, CHECKSUM},
              .size = 0x22},
       .rx = {.size = 0x06}},
      {.tx = {.data = {SOF, 0x0f, RESPONSE, FUNC_ID_ZW_GET_VERSION,
                       TEST_ZW_VERSION, CHECKSUM},
              .size = 0x11},
       .rx = {.size = 0x05}},
      {.tx = {.data = {SOF, 0x3, RESPONSE, FUNC_ID_SERIAL_API_GET_INIT_DATA,
                       CHECKSUM},
              .size = 0x05},
       .rx = {.size = 0x05}}};

  if (1 != read(end_device, &rx_ack, 1) || ACK != rx_ack) {
    DBG_PRINTF("Expected initial ACK but received %02x", rx_ack);
    abort();
  }

  for (int i = 0; i < 4; i++) {
    Device_ReceiveFrame(&sessions[i]);
  }

  DBG_PRINTF("Device_Init succeeded\n");
  return NULL;
}

static void *Device_ToggleLR(void *ptr) /* NOSONAR: pthread start routine signature */
{
  (void)ptr;
  session_t sessions[2] = {
      {.tx = {.data = {SOF, 0x4, RESPONSE, FUNC_ID_SERIALAPI_SETUP, 0x01,
                       CHECKSUM},
              .size = 0x06},
       .rx = {.size = 0x07}},
      {.rx = {.size = 0x06}}};

  for (int i = 0; i < 2; i++) {
    Device_ReceiveFrame(&sessions[i]);
  }
  return NULL;
}

static void *Device_StartRoutine(void *ptr) /* NOSONAR: pthread start routine signature */
{
  Device_SendFrame((session_t *)ptr);
  return NULL;
}

void setUp(void) {
  int controller;
  pthread_t device_loop;
  is_cb_called = FALSE;
  is_device_lost_called = FALSE;
  device_lost_data_len = 0;
  s2_row_callback_count = 0;

  if (openpty(&end_device, &controller, NULL, NULL, NULL)) {
    abort();
  }

  char *controller_path = ttyname(controller);
  DBG_PRINTF("%s\n", controller_path);

  if (pthread_create(&device_loop, NULL, Device_Init, NULL)) {
    abort();
  }

  if (!SerialAPI_Init(controller_path, &callbacks)) {
    abort();
  }

  if (pthread_join(device_loop, NULL)) {
    abort();
  }

  /* After SerialAPI_Init + Device_Init, clear callback capture: init can leave queued
   * work or dispatch that sets is_cb_called; tests that expect FALSE must start clean. */
  is_cb_called = FALSE;
  is_device_lost_called = FALSE;
  device_lost_data_len = 0;
  s2_row_callback_count = 0;
}

void tearDown(void) {
  is_cb_called = FALSE;
  SerialAPI_Destroy();

  if (close(end_device)) {
    abort();
  }
}

void test_accepts_happy_frame() {
  pthread_t device_loop;
  session_t session = {
      .tx = {.data = {SOF, 0x06, REQUEST, FUNC_ID_APPLICATION_COMMAND_HANDLER,
                      0x00, 0x00, 0x00, CHECKSUM},
             .size = 0x08},
      .rx = {.data = {0x00}, .size = 0x01}};

  TEST_ASSERT_FALSE(session.is_timeout);
  TEST_ASSERT_FALSE(
      pthread_create(&device_loop, NULL, Device_StartRoutine, &session));
  TEST_ASSERT(SerialAPI_Poll());
  TEST_ASSERT_FALSE(SerialAPI_Poll());
  TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
  TEST_ASSERT_FALSE(session.is_timeout);
  TEST_ASSERT_EQUAL(ACK, session.rx.data[0]);
  TEST_ASSERT(is_cb_called);
}

void test_drops_runt_frame() {
  pthread_t device_loop;
  session_t session = {
      .tx = {.data = {SOF, 0x03, REQUEST, FUNC_ID_APPLICATION_COMMAND_HANDLER,
                      CHECKSUM},
             .size = 0x05},
      .rx = {.data = {0x00}, .size = 0x01}};

  TEST_ASSERT_FALSE(session.is_timeout);
  TEST_ASSERT_FALSE(
      pthread_create(&device_loop, NULL, Device_StartRoutine, &session));
  TEST_ASSERT(SerialAPI_Poll());
  TEST_ASSERT_FALSE(SerialAPI_Poll());
  TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
  TEST_ASSERT_FALSE(session.is_timeout);
  TEST_ASSERT_EQUAL(ACK, session.rx.data[0]);
  TEST_ASSERT_FALSE(is_cb_called);
}

void test_drops_nif_too_short_frame() {
  pthread_t device_loop;
  session_t session = {
      .tx = {.data = {SOF, 0x04, REQUEST,
                      FUNC_ID_ZW_APPLICATION_CONTROLLER_UPDATE,
                      UPDATE_STATE_NODE_INFO_FOREIGN_HOMEID_RECEIVED, CHECKSUM},
             .size = 0x06},
      .rx = {.data = {0x00}, .size = 0x01}};

  TEST_ASSERT_FALSE(session.is_timeout);
  TEST_ASSERT_FALSE(
      pthread_create(&device_loop, NULL, Device_StartRoutine, &session));
  TEST_ASSERT(SerialAPI_Poll());
  TEST_ASSERT_FALSE(SerialAPI_Poll());
  TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
  TEST_ASSERT_FALSE(session.is_timeout);
  TEST_ASSERT_EQUAL(ACK, session.rx.data[0]);
  TEST_ASSERT_FALSE(is_cb_called);
}

void test_drops_nif_too_long_frame() {
  pthread_t device_loop;
  session_t session = {
      .tx = {.data = {SOF, 0x0c, REQUEST,
                      FUNC_ID_ZW_APPLICATION_CONTROLLER_UPDATE,
                      UPDATE_STATE_NODE_INFO_FOREIGN_HOMEID_RECEIVED, 0x00,
                      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, CHECKSUM},
             .size = 0x0e},
      .rx = {.data = {0x00}, .size = 0x01}};

  TEST_ASSERT_FALSE(session.is_timeout);
  TEST_ASSERT_FALSE(
      pthread_create(&device_loop, NULL, Device_StartRoutine, &session));
  TEST_ASSERT(SerialAPI_Poll());
  TEST_ASSERT_FALSE(SerialAPI_Poll());
  TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
  TEST_ASSERT_FALSE(session.is_timeout);
  TEST_ASSERT_EQUAL(ACK, session.rx.data[0]);
  TEST_ASSERT_FALSE(is_cb_called);
}

void test_detects_bof_application_update_frame() {
  pthread_t device_loop;
  session_t session = {
      .tx = {.data = {SOF, 0x06, REQUEST,
                      FUNC_ID_ZW_APPLICATION_CONTROLLER_UPDATE,
                      UPDATE_STATE_NEW_ID_ASSIGNED, 0x00, 0xee, CHECKSUM},
             .size = 0x08},
      .rx = {.data = {0x00}, .size = 0x01}};

  TEST_ASSERT_FALSE(session.is_timeout);
  TEST_ASSERT_FALSE(
      pthread_create(&device_loop, NULL, Device_StartRoutine, &session));
  TEST_ASSERT(SerialAPI_Poll());
  TEST_ASSERT_FALSE(SerialAPI_Poll());
  TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
  TEST_ASSERT_FALSE(session.is_timeout);
  TEST_ASSERT_EQUAL(ACK, session.rx.data[0]);
  TEST_ASSERT_FALSE(is_cb_called);
}

void test_drops_insufficient_application_command_handler_friends_frame() {
  pthread_t device_loop;

  session_t sessions[2] = {
      {.tx = {.data = {SOF, 0x05, REQUEST, FUNC_ID_APPLICATION_COMMAND_HANDLER,
                       0x00, 0x00, CHECKSUM},
              .size = 0x07},
       .rx = {.data = {0x00}, .size = 0x01}},
      {.tx = {.data = {SOF, 0x05, REQUEST,
                       FUNC_ID_APPLICATION_COMMAND_HANDLER_BRIDGE, 0x00, 0x00,
                       CHECKSUM},
              .size = 0x07},
       .rx = {.data = {0x00}, .size = 0x01}}};

  for (int i = 0; i < 2; i++) {
    session_t *session = &sessions[i];
    TEST_ASSERT_FALSE(session->is_timeout);
    TEST_ASSERT_FALSE(
        pthread_create(&device_loop, NULL, Device_StartRoutine, session));
    TEST_ASSERT(SerialAPI_Poll());
    TEST_ASSERT_FALSE(SerialAPI_Poll());
    TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
    TEST_ASSERT_FALSE(session->is_timeout);
    TEST_ASSERT_EQUAL(ACK, session->rx.data[0]);
    TEST_ASSERT_FALSE(is_cb_called);
  }
}

void test_detects_bof_application_command_handler_friends_frame() {
  pthread_t device_loop;

  session_t sessions[3] = {
      {.tx = {.data = {SOF, 0x06, REQUEST, FUNC_ID_APPLICATION_COMMAND_HANDLER,
                       0x00, 0x00, 0xee, CHECKSUM},
              .size = 0x08},
       .rx = {.data = {0x00}, .size = 0x01}},
      {.tx = {.data = {SOF, 0x06, REQUEST,
                       FUNC_ID_PROMISCUOUS_APPLICATION_COMMAND_HANDLER, 0x00,
                       0x00, 0xee, CHECKSUM},
              .size = 0x08},
       .rx = {.data = {0x00}, .size = 0x01}},
      {.tx = {.data = {SOF, 0x07, REQUEST,
                       FUNC_ID_APPLICATION_COMMAND_HANDLER_BRIDGE, 0x00, 0x00,
                       0x00, 0xee, CHECKSUM},
              .size = 0x09},
       .rx = {.data = {0x00}, .size = 0x01}}};

  for (int i = 0; i < 3; i++) {
    session_t *session = &sessions[i];
    TEST_ASSERT_FALSE(session->is_timeout);
    TEST_ASSERT_FALSE(
        pthread_create(&device_loop, NULL, Device_StartRoutine, session));
    TEST_ASSERT(SerialAPI_Poll());
    TEST_ASSERT_FALSE(SerialAPI_Poll());
    TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
    TEST_ASSERT_FALSE(session->is_timeout);
    TEST_ASSERT_EQUAL(ACK, session->rx.data[0]);
    TEST_ASSERT_FALSE(is_cb_called);
  }
}

void test_drops_serialapi_started_too_short_frame() {
  pthread_t device_loop;
  session_t session = {
      .tx = {.data = {SOF, 0x05, REQUEST, FUNC_ID_SERIALAPI_STARTED, 0x00, 0x00,
                      CHECKSUM},
             .size = 0x07},
      .rx = {.data = {0x00}, .size = 0x01}};

  TEST_ASSERT_FALSE(session.is_timeout);
  TEST_ASSERT_FALSE(
      pthread_create(&device_loop, NULL, Device_StartRoutine, &session));
  TEST_ASSERT(SerialAPI_Poll());
  TEST_ASSERT_FALSE(SerialAPI_Poll());
  TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
  TEST_ASSERT_FALSE(session.is_timeout);
  TEST_ASSERT_EQUAL(ACK, session.rx.data[0]);
  TEST_ASSERT_FALSE(is_cb_called);
}

void test_drops_serialapi_started_too_long_frame() {
  pthread_t device_loop;
  session_t session = {
      .tx = {.data = {SOF, 0x06, REQUEST, FUNC_ID_SERIALAPI_STARTED, 0x00, 0x00,
                      0xb4, CHECKSUM},
             .size = 0x08},
      .rx = {.data = {0x00}, .size = 0x01}};

  TEST_ASSERT_FALSE(session.is_timeout);
  TEST_ASSERT_FALSE(
      pthread_create(&device_loop, NULL, Device_StartRoutine, &session));
  TEST_ASSERT(SerialAPI_Poll());
  TEST_ASSERT_FALSE(SerialAPI_Poll());
  TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
  TEST_ASSERT_FALSE(session.is_timeout);
  TEST_ASSERT_EQUAL(ACK, session.rx.data[0]);
  TEST_ASSERT_FALSE(is_cb_called);
}

void test_detects_bof_serialapi_started_frame() {
  pthread_t device_loop;
  session_t session = {
      .tx = {.data = {SOF, 0x06, REQUEST, FUNC_ID_SERIALAPI_STARTED, 0x00, 0x00,
                      0xb5, CHECKSUM},
             .size = 0x08},
      .rx = {.data = {0x00}, .size = 0x01}};

  TEST_ASSERT_FALSE(session.is_timeout);
  TEST_ASSERT_FALSE(
      pthread_create(&device_loop, NULL, Device_StartRoutine, &session));
  TEST_ASSERT(SerialAPI_Poll());
  TEST_ASSERT_FALSE(SerialAPI_Poll());
  TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
  TEST_ASSERT_FALSE(session.is_timeout);
  TEST_ASSERT_EQUAL(ACK, session.rx.data[0]);
  TEST_ASSERT_FALSE(is_cb_called);
}

void test_drops_insufficient_zw_add_node_to_network_frame() {
  void cbFuncZWAddNodeToNetwork(LEARN_INFO * ptr) { is_cb_called = true; }

  void *Device_AddNodeToNetwork(void *ptr) /* NOSONAR: pthread start routine signature */
  {
    (void)ptr;
    session_t session = {.rx = {.size = 0x07}};
    Device_ReceiveFrame(&session);
    return NULL;
  }

  pthread_t device_loop;

  {
    TEST_ASSERT_FALSE(
        pthread_create(&device_loop, NULL, Device_AddNodeToNetwork, NULL));
    ZW_AddNodeToNetwork(TRUE, cbFuncZWAddNodeToNetwork);
    TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
  }

  session_t session = {
      .tx = {.data = {SOF, 0x05, REQUEST, FUNC_ID_ZW_ADD_NODE_TO_NETWORK, 0x00,
                      ADD_NODE_STATUS_ADDING_END_NODE, CHECKSUM},
             .size = 0x07},
      .rx = {.data = {0x00}, .size = 0x01}};

  TEST_ASSERT_FALSE(session.is_timeout);
  TEST_ASSERT_FALSE(
      pthread_create(&device_loop, NULL, Device_StartRoutine, &session));
  TEST_ASSERT(SerialAPI_Poll());
  TEST_ASSERT_FALSE(SerialAPI_Poll());
  TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
  TEST_ASSERT_FALSE(session.is_timeout);
  TEST_ASSERT_EQUAL(ACK, session.rx.data[0]);
  TEST_ASSERT_FALSE(is_cb_called);
}

void test_drops_insufficient_zw_set_learn_mode_frame() {
  void cbFuncZWSetLearnMode(LEARN_INFO * ptr) { is_cb_called = true; }

  void *Device_SetLearnMode(void *ptr) /* NOSONAR: pthread start routine signature */
  {
    (void)ptr;
    session_t session = {.rx = {.size = 0x07}};
    Device_ReceiveFrame(&session);
    return NULL;
  }

  pthread_t device_loop;

  {
    TEST_ASSERT_FALSE(
        pthread_create(&device_loop, NULL, Device_SetLearnMode, NULL));
    ZW_SetLearnMode(TRUE, cbFuncZWSetLearnMode);
    TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
  }

  session_t session = {
      .tx = {.data = {SOF, 0x04, REQUEST, FUNC_ID_ZW_SET_LEARN_MODE, 0x00,
                      CHECKSUM},
             .size = 0x06},
      .rx = {.data = {0x00}, .size = 0x01}};

  TEST_ASSERT_FALSE(session.is_timeout);
  TEST_ASSERT_FALSE(
      pthread_create(&device_loop, NULL, Device_StartRoutine, &session));
  TEST_ASSERT(SerialAPI_Poll());
  TEST_ASSERT_FALSE(SerialAPI_Poll());
  TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
  TEST_ASSERT_FALSE(session.is_timeout);
  TEST_ASSERT_EQUAL(ACK, session.rx.data[0]);
  TEST_ASSERT_FALSE(is_cb_called);
}

void test_drops_insufficient_application_update_frame() {
  pthread_t device_loop;

  TEST_LR_ENABLED(device_loop);

  {
    session_t session = {
        .tx = {.data = {SOF, 0x04, REQUEST,
                        FUNC_ID_ZW_APPLICATION_CONTROLLER_UPDATE,
                        UPDATE_STATE_NEW_ID_ASSIGNED, CHECKSUM},
               .size = 0x06},
        .rx = {.data = {0x00}, .size = 0x01}};

    TEST_ASSERT_FALSE(session.is_timeout);
    TEST_ASSERT_FALSE(
        pthread_create(&device_loop, NULL, Device_StartRoutine, &session));
    TEST_ASSERT(SerialAPI_Poll());
    TEST_ASSERT_FALSE(SerialAPI_Poll());
    TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
    TEST_ASSERT_FALSE(session.is_timeout);
    TEST_ASSERT_EQUAL(ACK, session.rx.data[0]);
    TEST_ASSERT_FALSE(is_cb_called);
  }

  TEST_LR_DISABLED(device_loop);
}

/* Host hibernation Serial API tests */

static void *Device_HostHibClear(void *ptr) /* NOSONAR: pthread start routine signature */
{
  (void)ptr;
  session_t session = {
      .tx = {.data = {SOF, 0x05, RESPONSE, FUNC_ID_SERIAL_API_HOST_HIBERNATION,
                      HOST_HIBERNATION_SUBCMD_IMPORTANT_DEVICES_CLEAR, 0x01, CHECKSUM},
             .size = 0x07}, /* SOF + len(5) + type + cmd + data(2) + checksum */
      .rx = {.size = 0x06}};
  Device_ReceiveFrame(&session);
  return NULL;
}

static void *Device_HostHibCapabilities(void *ptr) /* NOSONAR: pthread start routine signature */
{
  (void)ptr;
  session_t session = {
      .tx = {.data = {SOF, 0x06, RESPONSE, FUNC_ID_SERIAL_API_HOST_HIBERNATION,
                      HOST_HIBERNATION_SUBCMD_MODULE_CAPABILITIES, 0x20, 0x80, CHECKSUM},
             .size = 0x08}, /* SOF + len(6) + type + cmd + data(3) + checksum */
      .rx = {.size = 0x06}};
  Device_ReceiveFrame(&session);
  return NULL;
}

static void *Device_HostHibNotifyState(void *ptr) /* NOSONAR: pthread start routine signature */
{
  (void)ptr;
  session_t session = {
      .tx = {.data = {SOF, 0x06, RESPONSE, FUNC_ID_SERIAL_API_HOST_HIBERNATION,
                      HOST_HIBERNATION_SUBCMD_NOTIFY_HOST_STATE, 0x01, 0x00, CHECKSUM},
             .size = 0x08},
      .rx = {.size = 0x08}};
  Device_ReceiveFrame(&session);
  return NULL;
}

static void *Device_HostHibWakeupReport(void *ptr) /* NOSONAR: pthread start routine signature */
{
  (void)ptr;
  session_t session = {.rx = {.size = 0x06}, .tx = {.size = 0}};
  Device_ReceiveFrame(&session);
  return NULL;
}

/* NCP rejects clear: command status 0x00 */
static void *Device_HostHibClearReject(void *ptr) /* NOSONAR: pthread start routine signature */
{
  (void)ptr;
  session_t session = {
      .tx = {.data = {SOF, 0x05, RESPONSE, FUNC_ID_SERIAL_API_HOST_HIBERNATION,
                      HOST_HIBERNATION_SUBCMD_IMPORTANT_DEVICES_CLEAR, 0x00, CHECKSUM},
             .size = 0x07},
      .rx = {.size = 0x06}};
  Device_ReceiveFrame(&session);
  return NULL;
}

/* Command status 0x01 — unknown node ID */
static void *Device_HostHibImportantListUnknownNodeId(void *ptr) /* NOSONAR: pthread start routine signature */
{
  (void)ptr;
  session_t list_session = {
      .tx = {.data = {SOF, 0x06, RESPONSE, FUNC_ID_SERIAL_API_HOST_HIBERNATION,
                      HOST_HIBERNATION_SUBCMD_IMPORTANT_DEVICES_LIST, 0x05, 0x01, CHECKSUM},
             .size = 0x08},
      .rx = {.size = 29}};
  Device_ReceiveFrame(&list_session);
  return NULL;
}

/* Response SubCmd byte wrong (not 0x03) */
static void *Device_HostHibCapabilitiesBadSubcmd(void *ptr) /* NOSONAR: pthread start routine signature */
{
  (void)ptr;
  session_t session = {
      .tx = {.data = {SOF, 0x06, RESPONSE, FUNC_ID_SERIAL_API_HOST_HIBERNATION,
                      0xFF, 0x20, 0x80, CHECKSUM},
             .size = 0x08},
      .rx = {.size = 0x06}};
  Device_ReceiveFrame(&session);
  return NULL;
}

/* First S2 response has wrong SubCmd (not 0x06) */
static void *Device_HostHibS2WrongSubcmd(void *ptr) /* NOSONAR: pthread start routine signature */
{
  (void)ptr;
  session_t session = {
      .tx = {.data = {SOF, 0x05, RESPONSE, FUNC_ID_SERIAL_API_HOST_HIBERNATION,
                      0xFF, 0x00, CHECKSUM},
             .size = 0x07},
      .rx = {.size = 0x08}};
  Device_ReceiveFrame(&session);
  return NULL;
}

static void *Device_HostHibImportantList(void *ptr) /* NOSONAR: pthread start routine signature */
{
  (void)ptr;
  /* acknowledged_frame_with_response: SubCmd | Total Node Count | Command Status */
  session_t list_session = {
      .tx = {.data = {SOF, 0x06, RESPONSE, FUNC_ID_SERIAL_API_HOST_HIBERNATION,
                      HOST_HIBERNATION_SUBCMD_IMPORTANT_DEVICES_LIST, 0x05, 0x00, CHECKSUM},
             .size = 0x08}, /* SubCmd | TotalNodeCount(5) | CommandStatus(0x00=success); Length=6 => 8 bytes total */
      .rx = {.size = 29}}; /* Request: payload 4+4*5=24, frame Length=26 => SOF+1+26+1=29 bytes */
  Device_ReceiveFrame(&list_session);
  return NULL;
}

static void *Device_HostHibS2MessageCount(void *ptr) /* NOSONAR: pthread start routine signature */
{
  (void)ptr;
  session_t session = {
      .tx = {.data = {SOF, 0x1A, RESPONSE, FUNC_ID_SERIAL_API_HOST_HIBERNATION,
                      HOST_HIBERNATION_SUBCMD_REQUEST_S2_MSG_COUNT_LIST, 0x05,
                      0x00, 0x05, 0x02, 0x03,
                      0x00, 0x0C, 0x01, 0x02,
                      0x00, 0x03, 0x00, 0x01,
                      0x00, 0x07, 0x04, 0x05,
                      0x00, 0x0F, 0x03, 0x04,
                      0x00, CHECKSUM},
             .size = 0x1C}, /* len=26: subcmd+length(5)+5*4+more; total 28 bytes */
      .rx = {.size = 0x08}}; /* Request: subcmd(1)+node_id(2)=3 bytes payload */
  Device_ReceiveFrame(&session);
  return NULL;
}

static void *Device_HostHibWakeNotifyDeviceLost(void *ptr) /* NOSONAR: pthread start routine signature */
{
  session_t session = {
      .tx = {.data = {SOF, 0x19, REQUEST, FUNC_ID_SERIAL_API_HOST_HIBERNATION,
                      HOST_HIBERNATION_SUBCMD_DEVICE_LOST_REPORT, 0x05,
                      0x00, 0x05, 0x00, 0x1E,
                      0x00, 0x0C, 0x00, 0x78,
                      0x00, 0x03, 0x00, 0x1E,
                      0x00, 0x07, 0x00, 0x5A,
                      0x00, 0x0F, 0x00, 0x2D,
                      CHECKSUM},
             .size = 0x1B}, /* len=25: subcmd+count+5*4+checksum; total 27 bytes */
      .rx = {.data = {0x00}, .size = 0x01}};
  Device_SendFrame(&session);
  return NULL;
}

void test_host_hib_send_important_devices_clear(void)
{
  pthread_t device_loop;
  TEST_ASSERT_FALSE(pthread_create(&device_loop, NULL, Device_HostHibClear, NULL));
  /* mock returns command status 0x01 (success) */
  TEST_ASSERT_EQUAL_INT(SERIALAPI_HOST_HIBERNATION_CLEAR_STATUS_SUCCESS_MIN,
                        SerialAPI_SendImportantDevicesClear());
  TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
}

void test_host_hib_get_module_capabilities(void)
{
  pthread_t device_loop;
  BYTE max_devices = 0;
  BYTE max_frame_length = 0;
  TEST_ASSERT_FALSE(pthread_create(&device_loop, NULL, Device_HostHibCapabilities, NULL));
  TEST_ASSERT_EQUAL_INT(
      SERIALAPI_HOST_HIBERNATION_OK,
      SerialAPI_GetModuleCapabilities(&max_devices, &max_frame_length));
  TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
  TEST_ASSERT_EQUAL(0x20, max_devices);
  TEST_ASSERT_EQUAL(0x80, max_frame_length);
}

void test_host_hib_notify_host_state(void)
{
  pthread_t device_loop;
  uint8_t ncp_severity = 0xFF;
  TEST_ASSERT_FALSE(pthread_create(&device_loop, NULL, Device_HostHibNotifyState, NULL));
  TEST_ASSERT_EQUAL_INT(SERIALAPI_HOST_HIBERNATION_OK,
                        SerialAPI_NotifyHostState(0x00, 0x00, &ncp_severity));
  TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
  TEST_ASSERT_EQUAL_HEX8(0x00, ncp_severity);
}

void test_host_hib_request_wakeup_report(void)
{
  pthread_t device_loop;
  TEST_ASSERT_FALSE(pthread_create(&device_loop, NULL, Device_HostHibWakeupReport, NULL));
  TEST_ASSERT_EQUAL_INT(SERIALAPI_HOST_HIBERNATION_OK, SerialAPI_RequestWakeupReport());
  TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
}

void test_host_hib_send_important_device_list(void)
{
  gw_important_node_entry_t list[5] = {
      {.node_id = 5, .keep_alive_win_min = 60},
      {.node_id = 12, .keep_alive_win_min = 120},
      {.node_id = 3, .keep_alive_win_min = 30},
      {.node_id = 7, .keep_alive_win_min = 90},
      {.node_id = 15, .keep_alive_win_min = 45},
  };

  pthread_t device_loop;
  TEST_ASSERT_FALSE(pthread_create(&device_loop, NULL, Device_HostHibImportantList, NULL));
  TEST_ASSERT_EQUAL_INT(SERIALAPI_HOST_HIBERNATION_OK,
                        SerialAPI_SendImportantDeviceList(list, 5, 40));
  TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
}

void test_host_hib_request_s2_message_count_list(void)
{
  static const uint8_t expected[MAX_S2_ROW_CALLBACKS][4] = {
      {0x00, 0x05, 0x02, 0x03}, /* node_id=5, s2_count=2, last_seq=3 */
      {0x00, 0x0C, 0x01, 0x02}, /* node_id=12, s2_count=1, last_seq=2 */
      {0x00, 0x03, 0x00, 0x01}, /* node_id=3, s2_count=0, last_seq=1 */
      {0x00, 0x07, 0x04, 0x05}, /* node_id=7, s2_count=4, last_seq=5 */
      {0x00, 0x0F, 0x03, 0x04}, /* node_id=15, s2_count=3, last_seq=4 */
  };

  pthread_t device_loop;
  TEST_ASSERT_FALSE(pthread_create(&device_loop, NULL, Device_HostHibS2MessageCount, NULL));
  TEST_ASSERT_EQUAL_INT(SERIALAPI_HOST_HIBERNATION_OK, SerialAPI_RequestS2MessageCountList(0));
  TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
  TEST_ASSERT_EQUAL(MAX_S2_ROW_CALLBACKS, s2_row_callback_count);
  for (int i = 0; i < MAX_S2_ROW_CALLBACKS; i++) {
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected[i], s2_row_callback_entries[i], 4);
  }
}

void test_host_hib_on_wake_notify_device_lost(void)
{
  static const uint8_t expected[20] = {
      0x00, 0x05, 0x00, 0x1E, /* node_id=5, last_seen=30 */
      0x00, 0x0C, 0x00, 0x78, /* node_id=12, last_seen=120 */
      0x00, 0x03, 0x00, 0x1E, /* node_id=3, last_seen=30 */
      0x00, 0x07, 0x00, 0x5A, /* node_id=7, last_seen=90 */
      0x00, 0x0F, 0x00, 0x2D, /* node_id=15, last_seen=45 */
  };

  pthread_t device_loop;
  bool got_frame = false;
  TEST_ASSERT_FALSE(
      pthread_create(&device_loop, NULL, Device_HostHibWakeNotifyDeviceLost, NULL));
  /* Poll until frame received (SerialCheck has 1ms timeout; device may not have written yet) */
  for (int i = 0; i < 100; i++) {
    if (SerialAPI_Poll()) {
      got_frame = true;
      break;
    }
    struct timespec delay = {.tv_sec = 0, .tv_nsec = 1000 * 1000L};
    (void)nanosleep(&delay, NULL);
  }
  TEST_ASSERT_TRUE(got_frame);
  (void)SerialAPI_Poll();  /* Second call: dispatch queued frame to OnDeviceLost */
  TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
  TEST_ASSERT_TRUE(is_device_lost_called);
  TEST_ASSERT_EQUAL(20, device_lost_data_len);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, device_lost_data, 20);
}

/* ---------- Host hibernation negative / validation tests ---------- */

void test_host_hib_send_important_device_list_invalid_args(void)
{
  gw_important_node_entry_t one = {.node_id = 1, .keep_alive_win_min = 1};

  TEST_ASSERT_EQUAL_INT(SERIALAPI_HOST_HIBERNATION_ERR_INVALID_ARG,
                        SerialAPI_SendImportantDeviceList(NULL, 1, 40));
  TEST_ASSERT_EQUAL_INT(SERIALAPI_HOST_HIBERNATION_ERR_INVALID_ARG,
                        SerialAPI_SendImportantDeviceList(&one, 0, 40));
  TEST_ASSERT_EQUAL_INT(SERIALAPI_HOST_HIBERNATION_ERR_INVALID_ARG,
                        SerialAPI_SendImportantDeviceList(&one, 1, 7));
}

void test_host_hib_notify_host_state_invalid_args(void)
{
  TEST_ASSERT_EQUAL_INT(SERIALAPI_HOST_HIBERNATION_ERR_INVALID_ARG,
                        SerialAPI_NotifyHostState(0xFF, 0x00, NULL));
  TEST_ASSERT_EQUAL_INT(SERIALAPI_HOST_HIBERNATION_ERR_INVALID_ARG,
                        SerialAPI_NotifyHostState(0x02, 0x00, NULL));
}

void test_host_hib_get_module_capabilities_null_args(void)
{
  uint8_t a = 0;
  TEST_ASSERT_EQUAL_INT(SERIALAPI_HOST_HIBERNATION_ERR_INVALID_ARG,
                        SerialAPI_GetModuleCapabilities(NULL, &a));
  TEST_ASSERT_EQUAL_INT(SERIALAPI_HOST_HIBERNATION_ERR_INVALID_ARG,
                        SerialAPI_GetModuleCapabilities(&a, NULL));
  TEST_ASSERT_EQUAL_INT(SERIALAPI_HOST_HIBERNATION_ERR_INVALID_ARG,
                        SerialAPI_GetModuleCapabilities(NULL, NULL));
}

void test_host_hib_clear_ncp_rejects_with_status_zero(void)
{
  pthread_t device_loop;
  TEST_ASSERT_FALSE(pthread_create(&device_loop, NULL, Device_HostHibClearReject, NULL));
  int r = SerialAPI_SendImportantDevicesClear();
  TEST_ASSERT_EQUAL_INT(SERIALAPI_HOST_HIBERNATION_CLEAR_STATUS_NOT_ACCEPTED_OR_ERROR, r);
  TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
}

void test_host_hib_send_important_device_list_ncp_unknown_nodeid(void)
{
  gw_important_node_entry_t list[5] = {
      {.node_id = 5, .keep_alive_win_min = 60},
      {.node_id = 12, .keep_alive_win_min = 120},
      {.node_id = 3, .keep_alive_win_min = 30},
      {.node_id = 7, .keep_alive_win_min = 90},
      {.node_id = 15, .keep_alive_win_min = 45},
  };

  pthread_t device_loop;
  TEST_ASSERT_FALSE(
      pthread_create(&device_loop, NULL, Device_HostHibImportantListUnknownNodeId, NULL));
  TEST_ASSERT_EQUAL_INT(
      SERIALAPI_HOST_HIBERNATION_LIST_STATUS_UNKNOWN_NODEID,
      SerialAPI_SendImportantDeviceList(list, 5, 40));
  TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
}

void test_host_hib_get_module_capabilities_bad_response_subcmd(void)
{
  pthread_t device_loop;
  uint8_t max_d = 0;
  uint8_t max_f = 0;
  TEST_ASSERT_FALSE(
      pthread_create(&device_loop, NULL, Device_HostHibCapabilitiesBadSubcmd, NULL));
  TEST_ASSERT_EQUAL_INT(
      SERIALAPI_HOST_HIBERNATION_ERR_BAD_RESPONSE,
      SerialAPI_GetModuleCapabilities(&max_d, &max_f));
  TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
}

void test_host_hib_request_s2_message_count_list_bad_response_subcmd(void)
{
  pthread_t device_loop;
  TEST_ASSERT_FALSE(pthread_create(&device_loop, NULL, Device_HostHibS2WrongSubcmd, NULL));
  TEST_ASSERT_EQUAL_INT(
      SERIALAPI_HOST_HIBERNATION_ERR_BAD_RESPONSE,
      SerialAPI_RequestS2MessageCountList(0));
  TEST_ASSERT_FALSE(pthread_join(device_loop, NULL));
}
