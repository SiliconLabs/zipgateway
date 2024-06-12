/* © 2023 Silicon Laboratories Inc.  */
#include <Serialapi.h>
#include <ZIP_Router_logging.h>
#include <pthread.h>
#include <pty.h>
#include <stdlib.h>
#include <unistd.h>
#include <unity.h>

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

static const struct SerialAPI_Callbacks callbacks = {
    TestApplicationCommandHandler,
    0,
    TestApplicationControllerUpdate,
    0,
    0,
    0,
    0,
    TestApplicationCommandHandler,
    TestSerialAPIStarted};

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
    struct timeval tv = {.tv_usec = 100};

    FD_ZERO(&rfds);
    FD_SET(end_device, &rfds);

    int data_ready = select(end_device + 1, &rfds, NULL, NULL, &tv);
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

static void *Device_Init(void *ptr) {
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
}

static void *Device_ToggleLR(void *ptr) {
  session_t sessions[2] = {
      {.tx = {.data = {SOF, 0x4, RESPONSE, FUNC_ID_SERIALAPI_SETUP, 0x01,
                       CHECKSUM},
              .size = 0x06},
       .rx = {.size = 0x07}},
      {.rx = {.size = 0x06}}};

  for (int i = 0; i < 2; i++) {
    Device_ReceiveFrame(&sessions[i]);
  }
}

static void *Device_StartRoutine(void *ptr) {
  Device_SendFrame((session_t *)ptr);
}

void setUp(void) {
  int controller;
  pthread_t device_loop;
  is_cb_called = FALSE;

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

  void *Device_AddNodeToNetwork(void *ptr) {
    session_t session = {.rx = {.size = 0x07}};
    Device_ReceiveFrame(&session);
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

  void *Device_SetLearnMode(void *ptr) {
    session_t session = {.rx = {.size = 0x07}};
    Device_ReceiveFrame(&session);
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
