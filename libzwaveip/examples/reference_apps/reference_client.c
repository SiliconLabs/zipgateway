/*
 * Copyright 2020 Silicon Laboratories Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <signal.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <libgen.h>
#include <pthread.h>

#include "libzwaveip.h"
#include "tokenizer.h"
#include "ZW_classcmd.h"
#include "network_management.h"
#include "node_provisioning_list.h"
#include "command_return_codes.h"
#include "xml/parse_xml.h"
#include "zresource.h"

#include "readline/readline.h"
#include "zw_cmd_tool.h"
#include "command_completion.h"

#include "util.h"
#include "hexchar.h"
#include "libzw_log.h"
#include "pkgconfig.h"

#define BINARY_COMMAND_BUFFER_SIZE 2000
#define MAX_ADDRESS_SIZE 100

#define SECURITY_0_NETWORK_KEY_BIT 0x80
#define SECURITY_2_ACCESS_CLASS_KEY 0x04
#define SECURITY_2_AUTHENTICATED_CLASS_KEY 0x02
#define SECURITY_2_UNAUTHENTICATED_CLASS_KEY 0x01

// Mode values for COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION, NODE_ADD
#define REMOVE_NODE_ANY  0x01
#define REMOVE_NODE_STOP 0x05

// Mode values for COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION, NODE_ADD
#define ADD_NODE_ANY    0x01
#define ADD_NODE_STOP   0x05
#define NODE_ADD_ANY_S2 0x07

// Mode values for COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC, LEARN_MODE_SET
#define ZW_SET_LEARN_MODE_DISABLE 0x00
#define ZW_SET_LEARN_MODE_CLASSIC 0x01
#define ZW_SET_LEARN_MODE_NWI     0x02

#ifndef DEFAULT_ZWAVE_CMD_CLASS_XML
#define DEFAULT_ZWAVE_CMD_CLASS_XML "/usr/local/share/zwave/ZWave_custom_cmd_classes.xml"
#endif
#define DEFAULT_LOG_FILE_LOCATION "/tmp/libzw_reference_client.log"

static const uint8_t default_dtls_psk[] = {0x12, 0x34, 0x56, 0x78, 0x90, 0x12, 0x34, 0x56,
                                           0x78, 0x90, 0x12, 0x34, 0x56, 0x78, 0x90, 0xAA};


/* Global app configuration, set from command line */
struct app_config {
  bool    use_dtls;                 //!< Connect to Z/IP gateway using DTLS or not (i.e. UDP)
  uint8_t psk[MAX_PSK_LEN];         //!< DTLS pre-shared key
  uint8_t psk_len;                  //!< Length of psk
  char server_ip[INET6_ADDRSTRLEN]; //!< IPv4 or IPv6 address of Z/IP gateway
  char xml_file_path[PATH_MAX];     //!< Path of XML file with Z-Wave cmd class definitions
  log_severity_t ui_message_level;  //!<logging severity level causing messages to be displayed to the user
  char log_file_path[PATH_MAX];     //!<path for the log file location
  log_severity_t log_filter_level;  //!< logging severity filter level
} cfg;

static struct {
  uint8_t requested_keys;
  uint8_t csa_inclusion_requested;
} inclusion_context;

/** Stores the relation between IP address and zconnection */
typedef struct {
  char addr[MAX_ADDRESS_SIZE]; //!< IP address of node client is connected to
  zconnection_t *con;          //!< Connection info
} client_session_t;

#define MAX_CLIENT_SESSIONS 4
static client_session_t client_sessions[MAX_CLIENT_SESSIONS];

static client_session_t * get_client_session(const char *addr) {
  for (int i = 0; i < MAX_CLIENT_SESSIONS; i++) {
    if (client_sessions[i].con && strcmp(client_sessions[i].addr, addr) == 0) {
      return &client_sessions[i];
    }
  }
  return NULL;
}

static int stop_all_client_sessions(bool stop_busy_clients) {
  int stopped_count = 0;
  LOG_INFO("Shutting down all client connections...");
  for (int i = 0; i < MAX_CLIENT_SESSIONS; i++) {
    if (client_sessions[i].con) {
      if (stop_busy_clients == false && zconnection_is_busy(client_sessions[i].con)) {
        LOG_TRACE("Client connection to %s busy, skipping", client_sessions[i].addr);
        continue;
      }

      LOG_TRACE("Shutting down client connection to %s", client_sessions[i].addr);
      zclient_stop(client_sessions[i].con);
      client_sessions[i].con = NULL;
      memset(&client_sessions[i], 0, sizeof(client_sessions[i]));
      stopped_count++;
    }
  }
  return stopped_count;
}

static client_session_t * add_client_session(const char *addr) {
  static int next_idx_to_check = 0;

  /* Locate next non-busy slot in the session list (start the search after the
   * last item added in order to free up the oldest sessions - might not be the
   * last ones used though)
  */
  for (int n = 0; n < MAX_CLIENT_SESSIONS; n++) {
    int idx = (next_idx_to_check + n) % MAX_CLIENT_SESSIONS;
    if (client_sessions[idx].con == NULL || !zconnection_is_busy(client_sessions[idx].con))
    {
      if (client_sessions[idx].con) {
        LOG_DEBUG("Need to reuse idle client session %d connected to %s. Stopping client now.", idx, client_sessions[idx].addr);
        zclient_stop(client_sessions[idx].con);
      }
      LOG_TRACE("Registering new client session %d to %s", idx, addr);
      client_sessions[idx].con = NULL;  // The caller must fill this later
      strncpy(client_sessions[idx].addr, addr, MAX_ADDRESS_SIZE);
      client_sessions[idx].addr[MAX_ADDRESS_SIZE - 1] = '\0';
      next_idx_to_check = (idx + 1) % MAX_CLIENT_SESSIONS;
      return &client_sessions[idx];
    }
  } 
  return NULL; // All sessions busy
}

uint8_t get_unique_seq_no(void) {
  static uint8_t uniq_seqno = 0;
  return uniq_seqno++;
}

void print_hex_string(const uint8_t *data, unsigned int datalen) {
  unsigned int i;

  for (i = 0; i < datalen; i++) {
    UI_MSG("%2.2X", data[i]);
    if ((i & 0xf) == 0xf) {
      UI_MSG("\n");
    }
  }
}

/* Print a DSK in decimal format (99999-99999-99999-...) */
static void print_dsk(const uint8_t *dsk, uint8_t len) {
  for (int i = 0; i + 1 < len; i += 2) {
    UI_MSG("%s%05d", (i > 0) ? "-" : "", ((uint16_t)dsk[i] << 8) + dsk[i + 1]);
  }
  UI_MSG("\n");
}

static struct zip_service * get_gateway_service(void) {
  return zresource_find_service_by_ip_str(cfg.server_ip);
}

void net_mgmt_command_handler(union evt_handler_struct evt) {
  switch (evt.dsk_report.type) {
    case APPROVE_REQUESTED_KEYS: {
      inclusion_context.requested_keys = evt.requested_keys.requested_keys;
      inclusion_context.csa_inclusion_requested =
          evt.requested_keys.csa_requested;

      UI_MSG("The joining node requests these keys:\n\n");
      if (evt.requested_keys.requested_keys & SECURITY_2_ACCESS_CLASS_KEY) {
        UI_MSG(" * Security 2 Access/High Security key\n");
      }
      if (evt.requested_keys.requested_keys &
          SECURITY_2_AUTHENTICATED_CLASS_KEY) {
        UI_MSG(" * Security 2 Authenticated/Normal key\n");
      }
      if (evt.requested_keys.requested_keys &
          SECURITY_2_UNAUTHENTICATED_CLASS_KEY) {
        UI_MSG(" * Security 2 Unauthenticated/Ad-hoc key\n");
      }
      if (evt.requested_keys.requested_keys & SECURITY_0_NETWORK_KEY_BIT) {
        UI_MSG(" * Security S0 key\n");
      }
      UI_MSG("\n");
      if (evt.requested_keys.csa_requested) {
        UI_MSG("and client side authentication (CSA)\n");
      }
      UI_MSG("Enter \'grantkeys\' to accept or \'abortkeys\' to cancel.\n");
      UI_MSG("Usage: grantkeys [argument_1] [argument_2]\n"
         "argument_1: a byte that describes which keys to grant \n"
         "argument_2: a byte to allow CSA request\n"
         "Example:\n"
         "    type \'grantkeys\' without arguments to accept the keys/CSA request\n"
         "    type \'grantkeys 87 00\' to grant all keys and reject CSA if it is requested\n"
         "    type \'grantkeys 87 01\' to grant all keys and accept CSA\n");
         } break;
    case APPROVE_DSK: {
      UI_MSG("The joining node is reporting this device specific key:\n");
      print_dsk(evt.dsk_report.dsk, 16);
      UI_MSG(
          "\nPlease approve by typing \'acceptdsk 12345\' where 12345 is the "
          "first part of the DSK.\n12345 may be omitted if the device does not "
          "require the Access or Authenticated keys.\n");

    } break;
    default:
      break;
  }
}

void transmit_done(struct zconnection *zc, transmission_status_code_t status) {
  switch (status) {
    case TRANSMIT_OK:
      LOG_INFO("Transmit OK");
      rl_forced_update_display();
      break;
    case TRANSMIT_NOT_OK:
      LOG_ERROR("Transmit failed");
      rl_forced_update_display();
      break;
    case TRANSMIT_WAIT:
      LOG_INFO("Transmit put in gateways mailbox. Expected delay = %d seconds",
               zconnection_get_expected_delay(zc));
      rl_forced_update_display();
      break;
    case TRANSMIT_TIMEOUT:
      LOG_ERROR("Transmit attempt timed out");
      rl_forced_update_display();
      break;
  }
}

static void transmit_done_pan(struct zconnection *zc,
                              transmission_status_code_t status) {
  transmit_done(zc, status);
}

void application_command_handler(struct zconnection *connection,
                                 const uint8_t *data, uint16_t datalen) {
  int i;
  int len;
  char cmd_classes[400][MAX_LEN_CMD_CLASS_NAME];
  switch (data[0]) {
    case COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION:
      LOG_TRACE("COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION");
      parse_network_mgmt_inclusion_packet(data, datalen);
      break;

    case COMMAND_CLASS_NODE_PROVISIONING:
      LOG_TRACE("COMMAND_CLASS_NODE_PROVISIONING");
      parse_node_provisioning_list_packet(data, datalen, connection);
      break;

    default:
      LOG_TRACE("Calling decode()");
      memset(cmd_classes, 0, sizeof(cmd_classes));
      /* decode() clobbers data - but we are not using it afterwards, hence the
       * typecast */
      decode((uint8_t *)data, datalen, cmd_classes, &len);
      UI_MSG("\n");
      for (i = 0; i < len; i++) {
        UI_MSG("%s\n", cmd_classes[i]);
      }
      UI_MSG("\n");
      break;
  }
  rl_forced_update_display();
}

struct zconnection *zclient_start_addr(const char *remote_addr)
{
  LOG_DEBUG("");
  uint16_t remote_port = ZGW_UDP_PORT;

  if (cfg.use_dtls) {
    remote_port = ZGW_DTLS_PORT;
    if (cfg.psk_len == 0) {
      LOG_INFO("DTLS PSK not configured - using default.");
      memcpy(cfg.psk, default_dtls_psk, sizeof(default_dtls_psk));
      cfg.psk_len = sizeof(default_dtls_psk);
    }
  }

  struct zconnection *zc = zclient_start(remote_addr,
                                         remote_port,
                                         cfg.use_dtls,
                                         cfg.psk,
                                         cfg.psk_len,
                                         application_command_handler);

  if (zc == 0) {
    LOG_ERROR("Error connecting to %s on port %d", remote_addr, remote_port);
  }
  return zc;
}

/**
 * This command takes a text-command entered and tab-completed by the user
 *(possibly in JSON format?)
 * Then converts that text command to a complete, binary ZIP Command.
 *
 * \returns the output binary length or 0 on error/unrecognized text command
 */
int text_command_to_binary(const char *input_text_cmd,
                           uint8_t *output_binary_cmd, unsigned int max_len) {
  char **tokens;
  int retval = 0;

  if (!strcmp(input_text_cmd, "")) return 0;

  tokens = tokenize(input_text_cmd);
  if (!strcmp(tokens[0], "COMMAND_CLASS_NETWORK_MANAGEMENT")) {
    if (!strcmp(tokens[1], "COMMAND_NODE_ADD")) {
      int idx = 0;
      output_binary_cmd[idx++] = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
      output_binary_cmd[idx++] = NODE_ADD;
      output_binary_cmd[idx++] = get_unique_seq_no();
      output_binary_cmd[idx++] = 0;
      output_binary_cmd[idx++] = 0x07; /* ADD_NODE_S2 */
      output_binary_cmd[idx++] = 0;    /* Normal power, no NWI */
      retval = idx;
      goto cleanup;
    }
  }

cleanup:
  free_tokenlist((char **)tokens);
  return retval;
}

static void cmd_learn_mode_help(void)
{
  UI_MSG("Usage: learnmode [option]\n"
         "Options:\n"
         "  (empty): Classic inclusion\n"
         "  nwi    : Network wide inclusion\n"
         "  stop   : Disable learnmode\n"
         "Example:\n"
         "  learnmode nwi\n");
}

void cmd_learn_mode(struct zconnection *zc, const char *input) {
  uint8_t mode = ZW_SET_LEARN_MODE_CLASSIC;
  bool send_command = true;

  char **tokens;

  tokens = tokenize(input);

  if (token_count(tokens) >= 2) {
    if (!strcmp(tokens[1], "nwi")) {
      mode = ZW_SET_LEARN_MODE_NWI;
    } else if (!strcmp(tokens[1], "stop")) {
      mode = ZW_SET_LEARN_MODE_DISABLE;
    } else {
      if (strcmp(tokens[1], "help") != 0) {
        UI_MSG_ERR("Invalid learnmode option \"%s\"\n", tokens[1]);
      }
      cmd_learn_mode_help();
      send_command = false;
    }
  }

  free_tokenlist(tokens);

  if (send_command) {
    int idx = 0;
    uint8_t buf[20];

    buf[idx++] = COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC;
    buf[idx++] = LEARN_MODE_SET;
    buf[idx++] = get_unique_seq_no();
    buf[idx++] = 0;
    buf[idx++] = mode;

    zconnection_send_async(zc, buf, idx, 0);
  }
}

void cmd_grant_keys(const char *input)
{
  uintmax_t keys;
  uintmax_t csa_accepted = 0;
  char **tokens;
  bool parsed_ok = true;
  bool key_is_accepted = true;

  tokens = tokenize(input);

  if (token_count(tokens) == 1) {
    /* accept joining node requested keys/csa unchanged */
    keys = inclusion_context.requested_keys;
    csa_accepted = inclusion_context.csa_inclusion_requested;
  } else if (token_count(tokens) == 3) {
    keys = strtoumax(tokens[1], NULL, 16);
    if (keys == UINTMAX_MAX && errno == ERANGE) {
      parsed_ok = false;
    } else if (token_count(tokens) > 2) {
      csa_accepted = strtoumax(tokens[2], NULL, 16);
      if (csa_accepted == UINTMAX_MAX && errno == ERANGE) {
        parsed_ok = false;
      }
    }
  } else {
    parsed_ok = false;
  }

  free_tokenlist(tokens);
  
  if (parsed_ok) {
    net_mgmt_grant_keys((uint8_t)keys, (uint8_t)csa_accepted, key_is_accepted);
  } else {
    UI_MSG_ERR(
        "Syntax error: The second argument (i.e., CSA request acceptance/rejection) is missing\n"
        "Example:\n"
        "    type \'grantkeys 87 01\' to grant all keys and accept CSA\n");
    UI_MSG_ERR(
        "    Or type \'grantkeys\' without arguments to accept the keys/CSA requested\n"
        "    by joining node.\n");
  }
}

void cmd_abort_keys(void)
{
  uint8_t keys = 0;
  uint8_t csa_accepted = 0;
  bool key_is_accepted = false;

  /*User rejected the requested keys */
  net_mgmt_grant_keys(keys, csa_accepted, key_is_accepted);
}

void cmd_accept_dsk(const char *input)
{
  uintmax_t input_dsk;
  char **tokens = tokenize(input);

  if (token_count(tokens) > 1) {
    input_dsk = strtoumax(tokens[1], NULL, 10);
    if ((input_dsk == UINTMAX_MAX && errno == ERANGE) ||
        (input_dsk > 65535)) {
      UI_MSG(
          "Syntax error.\nUse \'acceptdsk 65535\' accept DSK and input first "
          "part\n");
    } else {
      input_dsk = htons(input_dsk);
      net_mgmt_set_input_dsk((uint8_t *)&input_dsk, 2);
    }
  } else {
    net_mgmt_set_input_dsk(NULL, 2);
  }
  free_tokenlist(tokens);
}

static void cmd_add_node_help(void)
{
  UI_MSG("Usage: addnode [option]\n"
         "Options:\n"
         "  (empty): Start add any node (including S0 and S2 bootstrapping)\n"
         "  stop   : Stop add any node\n"
         "Example:\n"
         "  addnode\n");
}

void cmd_add_node(struct zconnection *zc, const char *input)
{
  uint8_t mode = NODE_ADD_ANY_S2;
  bool send_command = true;

  struct zip_service *gw_s = get_gateway_service();
  if (gw_s) {
    if (!zresource_is_cc_supported(gw_s, COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION)) {
      LOG_ERROR("The Gateway does not support COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION");
      return;
    }
  }

  char **tokens = tokenize(input);

  if (token_count(tokens) >= 2) {
    if (!strcmp(tokens[1], "stop")) {
      mode = ADD_NODE_STOP;
    } else {
      if (strcmp(tokens[1], "help") != 0) {
        LOG_ERROR("Unknown addnode option: \"%s\"", tokens[1]);
      }
      cmd_add_node_help();
      send_command = false;;
    }
  }

  free_tokenlist(tokens);

  if (send_command) {
    int idx = 0;
    uint8_t buf[20];

    buf[idx++] = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
    buf[idx++] = NODE_ADD;
    buf[idx++] = get_unique_seq_no();
    buf[idx++] = 0;    // Reserved
    buf[idx++] = mode;
    buf[idx++] = 0;    // TX options 0: Normal power, no NWI

    if (mode == NODE_ADD_ANY_S2) {
      net_mgmt_learn_mode_start();
    }
    /* NB: Even if mode == ADD_NODE_STOP don't call net_mgmt_abort_inclusion()
     *     here. It will reset the libzwaveip inclusion state machine before the
     *     response to the ADD_NODE_STOP command has been received and
     *     processed. The end result is the same in both cases (the state
     *     machine returns to idle state), but the user will not get to see the
     *     decoded response message if net_mgmt_abort_inclusion() is called.
     */

    zconnection_send_async(zc, buf, idx, 0);
  }
}

static void cmd_remove_node_help(void)
{
  UI_MSG("Usage: removenode [option]\n"
         "Options:\n"
         "  (empty): Start remove any node\n"
         "  stop   : Stop remove node\n"
         "Example:\n"
         "  removenode\n");
}

void cmd_remove_node(struct zconnection *zc, const char *input)
{
  uint8_t mode = REMOVE_NODE_ANY;
  bool send_command = true;

  struct zip_service *gw_s = get_gateway_service();
  if (gw_s) {
    if (!zresource_is_cc_supported(gw_s, COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION)) {
      LOG_ERROR("The Gateway does not support COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION");
      return;
    }
  }

  char **tokens = tokenize(input);

  if (token_count(tokens) >= 2) {
    if (!strcmp(tokens[1], "stop")) {
      mode = REMOVE_NODE_STOP;
    } else {
      if (strcmp(tokens[1], "help") != 0) {
        LOG_ERROR("Unknown addnode option: \"%s\"", tokens[1]);
      }
      cmd_remove_node_help();
      send_command = false;
    }
  }

  free_tokenlist(tokens);

  if (send_command) {
    int idx = 0;
    uint8_t buf[20];

    buf[idx++] = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
    buf[idx++] = NODE_REMOVE;
    buf[idx++] = get_unique_seq_no();
    buf[idx++] = 0; // Reserved
    buf[idx++] = mode;

    zconnection_send_async(zc, buf, idx, 0);
  }
}

void cmd_set_default(struct zconnection *zc) {
  int idx = 0;
  uint8_t buf[200];

  buf[idx++] = COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC;
  buf[idx++] = DEFAULT_SET;
  buf[idx++] = get_unique_seq_no();

  zconnection_send_async(zc, buf, idx, 0);
}

typedef enum {
  CMD_SEND_OK =          0,
  CMD_SEND_ERR_ARG =    -1,
  CMD_SEND_ERR_CONN =   -2,
  CMD_SEND_ERR_BUSY =   -3
} cmd_send_status;


static cmd_send_status send_to_address(const char *dest_address, const uint8_t *data, uint16_t len) {
  // Do we already have a client connected to the address?
  client_session_t *s = get_client_session(dest_address);

  if (s) {
    if (zconnection_is_busy(s->con)) {
      LOG_ERROR("Connection on %s is busy, cannot send new command right now.", dest_address);
      return CMD_SEND_ERR_BUSY;
    }
  } else {
    LOG_DEBUG("Starting client for address: %s", dest_address);
    s = add_client_session(dest_address);
    if (s) {
      s->con = zclient_start_addr(dest_address);
      if (!s->con) {
        LOG_ERROR("Failed to connect to PAN node on address %s", dest_address);
        return CMD_SEND_ERR_CONN;
      }
    } else {
      LOG_ERROR("Max number of client sessions in use and all are busy. Unable to start new session for address: %s", dest_address);
      return CMD_SEND_ERR_BUSY;
    }
  }

  zconnection_set_transmit_done_func(s->con, transmit_done_pan);

  if (zconnection_send_async(s->con, data, len, 0)) {
    LOG_TRACE("CMD_SEND_OK");
    return CMD_SEND_OK;
  } else {
    LOG_TRACE("CMD_SEND_ERR_CONN");
    return CMD_SEND_ERR_CONN;
  }
}

/**
 * \param dest_address address of PAN node to receive command.
 * \param[in] input_tokens A tokenlist of input commands. Freed by caller. First
 * token is Command Class.
 */
static cmd_send_status _cmd_send(const char *dest_address, char **input_tokens) {
  unsigned char binary_command[BINARY_COMMAND_BUFFER_SIZE];
  unsigned int binary_command_len;
  unsigned char *p;
  const struct zw_command *p_cmd;
  const struct zw_command_class *p_class;

  if (token_count(input_tokens) < 2) {
    LOG_ERROR("Too few arguments.\n");
    return CMD_SEND_ERR_ARG;
  }

  /* Compose binary command from symbolic names using XML encoder */
  p_class = zw_cmd_tool_get_cmd_class(NULL,
                                      zw_cmd_tool_class_name_to_num(input_tokens[0]),
                                      0);
  p_cmd = zw_cmd_tool_get_cmd_by_name(p_class, input_tokens[1]);

  if (!p_class || !p_cmd) {
    LOG_ERROR("The command class name or command name not found.\n");
    return CMD_SEND_ERR_ARG;
  }

  memset(binary_command, 0, BINARY_COMMAND_BUFFER_SIZE);
  p = binary_command;
  *p++ = p_class->cmd_class_number;
  *p++ = p_cmd->cmd_number;
  binary_command_len = 2;
  if (token_count(input_tokens) > 2) {
    int additional_binary_len =
        asciihex_to_bin(input_tokens[2], p, BINARY_COMMAND_BUFFER_SIZE);
    if (additional_binary_len < 0) {
      LOG_ERROR("Syntax error in argument 3.");
      return CMD_SEND_ERR_ARG;
    }
    binary_command_len += additional_binary_len;
  }
  if (token_count(input_tokens) > 3) {
    LOG_WARN("Warning: Only 3 arguments are supported, all others are ignored.");
  }

  if (0 == binary_command_len) {
    LOG_ERROR("Zero-length command not sent.");
    return CMD_SEND_ERR_ARG;
  }

  return send_to_address(dest_address, binary_command, binary_command_len);
}

static cmd_send_status _cmd_hexsend(const char *dest_address, const char *input) {
  unsigned char binary_command[BINARY_COMMAND_BUFFER_SIZE];
  unsigned int binary_command_len;

  binary_command_len =
      asciihex_to_bin(input, binary_command, BINARY_COMMAND_BUFFER_SIZE);

  if (0 == binary_command_len) {
    LOG_ERROR("Zero-length command not sent.");
    return CMD_SEND_ERR_ARG;
  }

  return send_to_address(dest_address, binary_command, binary_command_len);
}

static void cmd_send(const char *input) {

  char **tokens = tokenize(input);
  if (token_count(tokens) >= 4) {
    char *friendly_service_name = tokens[1];
    char addr_str[INET6_ADDRSTRLEN] = {0};

    /* Strip beginning and closing quotes - Service names include spaces and are
     * always quoted with double-quotes
     */
    if (friendly_service_name[0] == '\"') {
      friendly_service_name++;                             /* strip opening quote */
      friendly_service_name[strlen(friendly_service_name) - 1] = 0; /* strip closing quote */
    }

    struct zip_service *s = find_service_by_friendly_name(friendly_service_name);

    if (s) {
      if (zresource_get_ip_str(s, addr_str, sizeof(addr_str))) {
        _cmd_send(addr_str, &tokens[2]);
      } else {
        LOG_ERROR("Invalid destination address for service: %s.", friendly_service_name);
      }
    } else {
      LOG_ERROR("Unknown service \"%s\"", friendly_service_name);
    }
  } else {
    LOG_ERROR("Syntax error.\nUse \'send \"Service Name\" COMMAND_CLASS_BASIC "
           "BASIC_GET\' to send a Basic Get");
  }

  free_tokenlist(tokens);
}

static void cmd_hexsend(const char *input) {

  char **tokens = tokenize(input);
  if (token_count(tokens) >= 3) {
    _cmd_hexsend(tokens[1], tokens[2]);
  } else {
    LOG_ERROR("Syntax error.\nUse \'hexsend fd00:bbbb::4 2002\' to send a Basic Get "
           "to node 4.");
  }
  free_tokenlist(tokens);
}

static void cmd_identify(const char *input) {
  char **tokens = tokenize(input);
  if (token_count(tokens) == 2) {
    /*
     * The command to blink the identify indicator 5 times:
     * 
     * ** NB: This is a COMMAND_CLASS_INDICATOR V3 command **
     * 
     * 00 Indicator 0 value (ignore)
     * 03 Object count
     * 500305 Identify (0x50) cycle length (0x03) = 5 * 0.1s (0x05)
     * 500405 Identify (0x50) num cycles (0x04) = 5 (0x05)
     * 500501 Identify (0x50) duty cycle (0x05) = 1 * 0.1s (0x01)
     */
    char cmd_buf[200] = {0};
    snprintf(cmd_buf,
             sizeof(cmd_buf),
             "send %s COMMAND_CLASS_INDICATOR INDICATOR_SET 0003500305500405500501",
             tokens[1]);
    cmd_send(cmd_buf);
  } else {
    LOG_ERROR("Syntax error.\nUse \'identify \"Service Name\"'");
  }
  free_tokenlist(tokens);
}

static void cmd_lifeline(const char *input) {
  char **tokens = tokenize(input);
  if (token_count(tokens) == 3) {
    unsigned int nodeid = 0;
    if (sscanf(tokens[2], "%u", &nodeid) == 1 && (nodeid < 232)) {
      char cmd_buf[200] = {0};
      if (nodeid > 0) {
        /*
         * The command to set the lifeline association to a specific node:
         * 
         * 01 Group (Lifeline = 1)
         * 00 MARKER
         * XX Node id
         * 00 Endpoint 
         */
        snprintf(cmd_buf,
                sizeof(cmd_buf),
                "send %s COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION MULTI_CHANNEL_ASSOCIATION_SET 0100%02X00",
                tokens[1],
                nodeid);
      } else {
        /*
         * The command to remove all lifeline associations:
         * 
         * 01 Group (Lifeline = 1)
         * 00 MARKER
         */
        snprintf(cmd_buf,
                sizeof(cmd_buf),
                "send %s COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION MULTI_CHANNEL_ASSOCIATION_REMOVE 0100",
                tokens[1]);
      }
      cmd_send(cmd_buf);

    } else {
      LOG_ERROR("Node id \"%s\"not a valid (decimal) numer or out of range", tokens[2]);
    }
  } else {
    LOG_ERROR("Syntax error.\nUse \'lifeline \"Service Name\" nodeid'");
  }
  free_tokenlist(tokens);
}

/* Max number of nodes in a Z-wave system */
#define ZW_LR_MAX_NODE_ID     4000
#define ZW_LR_MIN_NODE_ID     256
#define ZW_CLASSIC_MAX_NODES  232
#define is_classic_node(nodeid) (((nodeid) > 0) && ((nodeid) <= ZW_CLASSIC_MAX_NODES))

#define is_lr_node(nodeid) (((nodeid) >= ZW_LR_MIN_NODE_ID) && ((nodeid) <= ZW_LR_MAX_NODE_ID))

static void cmd_nodeinfo(const char *input) {
  char **tokens = tokenize(input);
  LOG_ERROR("token_count(tokens):%d \n", token_count(tokens));
  if (token_count(tokens) == 3) {
    // TODO: Check that the service name does not refer to an endpoint (does not support lifeline)
    unsigned int nodeid = 0;
    if (sscanf(tokens[2], "%u", &nodeid) == 1 && (is_classic_node(nodeid) || is_lr_node (nodeid))) {
     char cmd_buf[200] = {0};
      /*
       * The NODE_INFO_CACHED_GET payload:
       * 
       * XX Seq num
       * 00 max Age (0 = force update)
       * XXXX Node id
       */
      if (is_lr_node(nodeid)) {
        snprintf(cmd_buf,
                sizeof(cmd_buf),
                "send %s COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY NODE_INFO_CACHED_GET %02X00FF%04X",
                tokens[1],
                get_unique_seq_no(),
                nodeid);
      } else {
        snprintf(cmd_buf,
                sizeof(cmd_buf),
                "send %s COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY NODE_INFO_CACHED_GET %02X00%02X0000",
                tokens[1],
                get_unique_seq_no(),
                nodeid);
      }
      //printf("cmd: %s\n", cmd_buf);

      cmd_send(cmd_buf);
    } else {
      LOG_ERROR("Node id \"%s\"not a valid (decimal) number or out of range", tokens[2]);
    }
  } else {
    LOG_ERROR("Syntax error.\nUse \'nodeinfo \"Service Name\" nodeid'");
  }
  free_tokenlist(tokens);
}

static void print_zresource_nif(const struct zip_service *s, const char *line_prefix) {
  const struct zw_command_class *cmd_class = NULL;
  const char *prefix = "";

  if (line_prefix) {
    prefix = line_prefix;
  }

  const char *mode_str = "";

  switch (s->comm_mode) {
    case MODE_PROBING: mode_str = "MODE_NONLISTENING"; break;
    case MODE_NONLISTENING: mode_str = "MODE_NONLISTENING"; break;
    case MODE_ALWAYSLISTENING: mode_str = "MODE_ALWAYSLISTENING"; break;
    case MODE_FREQUENTLYLISTENING: mode_str = "MODE_FREQUENTLYLISTENING"; break;
    case MODE_MAILBOX: mode_str = "MODE_MAILBOX"; break;
    default: mode_str = "(unknown)"; break;
  }

  UI_MSG("%sMode: 0x%02X (%s)\n", prefix, s->comm_mode, mode_str);

  UI_MSG("%sMode flags: 0x%02X ", prefix, s->oper_flags);

  if (s->oper_flags == 0) {
    UI_MSG("(none)");
  } else {
    if (s->oper_flags & MODE_FLAGS_DELETED) UI_MSG("DELETED ");
    if (s->oper_flags & MODE_FLAGS_FAILED)  UI_MSG("FAILED ");
    if (s->oper_flags & MODE_FLAGS_LOWBAT)  UI_MSG("LOWBAT");
  }

  UI_MSG("\n%sNIF:\n", prefix);

  if (s->infolen >= 2) {
    const char *generic_class_name = "unknown";
    const char *specific_class_name = "unknown";

    zw_cmd_tool_get_device_class_by_id(s->info[0], // Generic class id
                                       s->info[1], // Specific class id
                                       &generic_class_name,
                                       &specific_class_name);

    UI_MSG("%s  0x%02X (%s)\n", prefix, s->info[0], generic_class_name);
    UI_MSG("%s  0x%02X (%s)\n", prefix, s->info[1], specific_class_name);
  }

  UI_MSG("%sSupported CCs (*:secure):\n", prefix);
  for (int i = 0; i < s->cc_info_count; i++) {
    cc_ver_info_t *ci = &s->cc_info[i];
    cmd_class = zw_cmd_tool_get_cmd_class(NULL, ci->cc, ci->version);
    UI_MSG("%s  0x%02X (%s%s",
           prefix,
           ci->cc,
           (ci->type == SECURE_CC) ? "*" : "",
           (cmd_class) ? cmd_class->name : "unknown");
    if (ci->version != 0) {
      UI_MSG(" V%d", ci->version);
    }
    UI_MSG(")\n");
  }
}

static void cmd_list_service(const char *input) {

  struct zip_service *s = NULL;
  struct zip_service *s_prev = NULL;
  int idx = 0;
  char gw_homeid[20] = {0};
  int show_only_my_services = 1;
  int print_nif = 0;

  char **tokens = tokenize(input);
  for (int i = token_count(tokens) - 1; i > 0; i--) {
    /* Secret/undocumented "-a" option:
    _* List all services received with mDNS. Default is to filter the
     * output so we only show services with the same homeID as the
     * gateway we are connected to.
     */
    if (strcmp(tokens[i], "-a") == 0) {
      show_only_my_services = 0;
    }
    if (strcmp(tokens[i], "-n") == 0) {
      print_nif = 1;
    }
  }
  free_tokenlist(tokens);

  if (show_only_my_services) {
    /* NB: Sadly, this will NOT work if you have connected to the gateway using
     *     its IPv6 address. The zresource module only knows services by their
     *     IPv4 address received from mDNS. Since the IPv6 address of the
     *     gateway and its IPv4 address (obtained with DHCP) are not related in
     *     any way we will not find a match here and all known services will be
     *     shown below.
     *
     *     For the filtering to work you must specify the gateways IPv4 address
     *     when running reference_client.
     */
    struct zip_service *gw_s = zresource_find_service_by_ip_str(cfg.server_ip);
    if (gw_s) {
      zresource_get_homeid(gw_s, gw_homeid, sizeof(gw_homeid));
    }
  }

  UI_MSG("List of discovered Z/IP services:\n");

  do {
    s = zresource_list_matching_services("", gw_homeid, idx++);
    if (s) {
      char ipbuf[100] = {0};
      if (s_prev && strncmp(s_prev->host_name, s->host_name, 10)) {
        UI_MSG("\n");
      }
      if (!zresource_get_ip_str(s, ipbuf, sizeof(ipbuf))) {
        strcpy(ipbuf, "(invalid)");
      }
      UI_MSG("\"%s\" IP:%s\n", s->friendly_name, ipbuf);
      if (print_nif) {
        print_zresource_nif(s, "  ");
      }
      s_prev = s;
    }
  } while(s);
}

static void cmd_pl_add_help(void)
{
  UI_MSG("Usage: pl_add <dsk> name:<name> location:<location> bootmode:<mode> smartstart:<ss>\n\n"
         "Parameters:\n"
         "  dsk: The DSK must be decimal digits separated by hypens -, leading zeros\n"
         "       may be removed.\n"
         "  key:value: Currently spaces are not supported in name and location values.\n" 
         "       The Z/IP Gateway does not allow to remove Name or Location value once\n"
         "       they have been set. If you want to revert to an empty Name or Location\n"
         "       string, you will have to delete the provisioning list entry with pl_remove\n"
         "       and add it again.\n"
         "       bootmode and smartstart values are hexadecimal values.\n"
         "       Other key:value pairs than the ones shown will be ignored. Note that Z/IP GW\n"
         "       may not support all specified key:value pairs.\n"
         "Example:\n"
         "  pl_add 08214-51265-26742-32133-59070-25641-60224-03557 "
         "name:MyName location:MyLocation bootmode:1 smartstart:0\n");
}

static void cmd_pl_remove_help(void)
{
  UI_MSG("Usage: pl_remove <dsk>\n"
         "Parameters:\n"
         "  dsk: The DSK must be decimal digits separated by hypens -, leading zeros\n"
         "       may be removed.\n"
         "Example:\n"
         "  pl_remove 08214-51265-26742-32133-59070-25641-60224-03557\n");
}

/**
 * Display help for all available user commands or for specific Z-Wave
 * command class and command.
 *
 * If requesting help for a Z-Wave command class use this syntax:
 * help <COMMAND_CLASS_xxxx> <COMMAND_xxxx>
 */
static void cmd_help(FILE* f, const char *input)
{
  char **tokens;
  tokens = tokenize(input);

  token_count(tokens);

  if (token_count(tokens) == 1) { // Plain and simple "help" was typed
    UI_MSG("Usage: help [addnode|removenode|learnmode|acceptdsk|grantkeys|setdefault|list|nodeinfo|hexsend|send|pl_list|pl_add|pl_remove|pl_reset|identify|lifeline|bye|exit|quit|]\n");
    UI_MSG("Usage: help <tab>: for autocompleting Z-Wave command class names\n");
    UI_MSG("\n");
    UI_MSG("Classic Network management:\n");
    UI_MSG("\t learnmode: Start learn mode at the Z/IP GW\n");
    UI_MSG("\t addnode: Add a new node in the Z/IP GW's network\n");
    UI_MSG("\t removenode: Remove a node from the Z/IP GW's network\n");
    UI_MSG("\t identify \"Service name\": Blink the identify indicator of the device\n");
    UI_MSG("\t lifeline \"Service name\" nodeid: Set the lifeline association to the specified nodeid\n");
    UI_MSG("\t          (if nodeid is zero all lifeline associations will be removed)\n");
    UI_MSG("\t list: List the nodes present in the network (use -n option to print NIF for each node\n");
    UI_MSG("\t nodeinfo \"Service name\" nodeid: Get fresh node info for the specified nodeid\n");
    UI_MSG("\t setdefault: Reset the Z/IP Gateway to default.\n");
    UI_MSG("\n");
    UI_MSG("SmartStart Node Provisioning list management (pl_): \n");
    UI_MSG("\t pl_list: Display the nodes in the provisioning list\n");
    UI_MSG("\t pl_add dsk [tlv:value] [tlv:value] ... Add/update a node in the provisioning list\n");
    UI_MSG("\t pl_remove dsk: Remove a node from the provisioning list \n");
    UI_MSG("\t pl_reset: Flushes the entire provisioning list at the Z/IP GW\n");
    UI_MSG("\n");
    UI_MSG("Sending frames to nodes: \n");
    UI_MSG("\t hexsend: Send data to a node specifying the payload as hexadecimal argument\n");
    UI_MSG("\t send \"Service name\" COMMAND_CLASS_NAME COMMAND hexpayload: Send a command to the node\n");
    UI_MSG("\n");
    UI_MSG("Exit the program: bye|exit|quit \n");
    UI_MSG("\n");
  } else if (token_count(tokens) == 2) {
    if (!strcmp(tokens[1], "learnmode")) {
      cmd_learn_mode_help();
    } else if (!strcmp(tokens[1], "addnode")) {
      cmd_add_node_help();
    } else if (!strcmp(tokens[1], "removenode")) {
      cmd_remove_node_help();
    } else if (!strcmp(tokens[1], "pl_add")) {
      cmd_pl_add_help();
    } else if (!strcmp(tokens[1], "pl_remove")) {
      cmd_pl_remove_help();
    } else {
      UI_MSG_ERR("ERROR: No help available for help topic: %s\n", tokens[1]);
    }
  } else if (token_count(tokens) >= 3) {
    const char *cmdclass = tokens[1];
    const char *cmd      = tokens[2];

    const struct zw_command_class *zw_class;
    const struct zw_command *zw_cmd;
    const struct zw_parameter *const *zw_param;

    zw_class = zw_cmd_tool_get_cmd_class(NULL, zw_cmd_tool_class_name_to_num(cmdclass), 0);
    zw_cmd = zw_cmd_tool_get_cmd_by_name(zw_class, cmd);

    if (zw_class && zw_cmd) {
      for (zw_param = zw_cmd->params; *zw_param; zw_param++) {
        zw_cmd_tool_print_parameter(f, *zw_param, 2);
      }
      fprintf(f, "\n");
    } else {
      UI_MSG_ERR("The command class name or command name not found.\n");
    }
  }
}

/**
 * Prints an error message when a packet could not be sent with zconnection_send_async()
 */
static void print_zconnection_send_error(void)
{
  LOG_ERROR("Transmition to the Z/IP GW could not be initiated. Please retry later.\n");
}

bool process_commandline_command(const char *input, struct zconnection *zc) {
  char cmd[100] = {0}; // Hold the longest command name - 100 is plenty big

  if (0 == input || 0 == strlen(input)) {
    return true;
  }

  LOG_DEBUG("input = \"%s\"", input);

  size_t cmdlen = strcspn(input, " ");
  if (cmdlen >= sizeof(cmd)) {
    cmdlen = sizeof(cmd) - 1;
  }
  strncpy(cmd, input, cmdlen);

  if (!strcmp(cmd, "help")) {
    cmd_help(stdout, input);
  } else if (!strcmp(cmd, "quit")
             || !strcmp(cmd, "exit")
             || !strcmp(cmd, "bye")) {
    return false;
  } else if (!strcmp(cmd, "learnmode")) {
    cmd_learn_mode(zc, input);
  } else if (!strcmp(cmd, "grantkeys")) {
    cmd_grant_keys(input);
  } else if (!strcmp(cmd, "abortkeys")) {
    cmd_abort_keys();
  }else if (!strcmp(cmd, "acceptdsk")) {
    cmd_accept_dsk(input);
  } else if (!strcmp(cmd, "send")) {
    cmd_send(input);
  } else if (!strcmp(cmd, "hexsend")) {
    cmd_hexsend(input);
  } else if (!strcmp(cmd, "addnode")) {
    cmd_add_node(zc, input);
  } else if (!strcmp(cmd, "removenode")) {
    cmd_remove_node(zc, input);
  } else if (!strcmp(cmd, "setdefault")) {
    cmd_set_default(zc);
  } else if (!strcmp(cmd, "list")) {
    cmd_list_service(input);
  } else if (!strcmp(cmd, "identify")) {
    cmd_identify(input);
  } else if (!strcmp(cmd, "lifeline")) {
    cmd_lifeline(input);
  } else if (!strcmp(cmd, "nodeinfo")) {
    cmd_nodeinfo(input);
  } else if (!strcmp(cmd, "pl_list")) {
    e_cmd_return_code_t my_return_code = cmd_pl_list(zc);
    switch(my_return_code)
    {
      case E_CMD_RETURN_CODE_SUCCESS:
        LOG_INFO("Requested a fresh copy of the SmartStart Node provisioning list to the Z/IP Gateway.");
        break;
      case E_CMD_RETURN_CODE_TRANSMIT_FAIL:
        print_zconnection_send_error();
        break;
      default: // No other return codes expected from cmd_pl_list. But need to satisfy the compiler 
        break;
    }
  } else if (!strcmp(cmd, "pl_add")) {
    char **tokens;
    tokens = tokenize(input);
    e_cmd_return_code_t my_return_code = cmd_pl_add(zc,tokens);
    switch(my_return_code)
    {
      case E_CMD_RETURN_CODE_PARSE_ERROR:
        UI_MSG_ERR("Syntax error.\n");
        cmd_pl_add_help();
        break;
      case E_CMD_RETURN_CODE_TRANSMIT_FAIL:
        print_zconnection_send_error();
        break;
      default: // No other return codes expected from cmd_pl_add. But need to satisfy the compiler 
        break;
    }
    free_tokenlist(tokens);
  } else if (!strcmp(cmd, "pl_remove")) {
    char **tokens;
    tokens = tokenize(input);
    e_cmd_return_code_t my_return_code = cmd_pl_remove(zc,tokens);
    switch(my_return_code)
    {
      case E_CMD_RETURN_CODE_PARSE_ERROR:
        UI_MSG_ERR("Syntax error.\n");
        cmd_pl_remove_help();
        break;
      case E_CMD_RETURN_CODE_TRANSMIT_FAIL:
        print_zconnection_send_error();
        break;
      default: // No other return codes expected from cmd_pl_remove. But need to satisfy the compiler 
        break;
    }
    free_tokenlist(tokens);
  } else if (!strcmp(cmd, "pl_reset")) {
    e_cmd_return_code_t my_return_code = cmd_pl_reset(zc);
    switch(my_return_code)
    {
      case E_CMD_RETURN_CODE_SUCCESS:
        LOG_INFO("Z/IP Gateway node provisioning list has been reset.");
        break;
      case E_CMD_RETURN_CODE_TRANSMIT_FAIL:
        print_zconnection_send_error();
        break;
      default: // No other return codes expected from cmd_pl_reset. But need to satisfy the compiler 
        break;
    }
  } else {
    LOG_ERROR("Unknown command: %s", cmd);
  }
  return true;
}

void print_usage(char *argv0)
{
  UI_MSG("\nUsage: %s [-p pskkey] [-n] [-x zwave_xml_file] [-g logging file path] [-u UI message severity level] [-f logging severity filter level] -s ip_address\n\n", basename(argv0));
  UI_MSG("  -p Provide the DTLS pre-shared key as a hex string.\n"
         "     Default value: ");
  for (int i = 0; i < sizeof(default_dtls_psk); i++) {
    UI_MSG("%02X", default_dtls_psk[i]);
  }
  UI_MSG("\n     If the -n option is also used the key will not be used.\n\n");
  UI_MSG("  -n Use a non-secure UDP connection to the gateway. Default is a DTLS connection.\n\n");
  UI_MSG("  -g Provide the logging file path (i.e., absoulte name path, e.g., ~/test_log.log)\n");
  UI_MSG("  -u Specify logging severity level that causes messages to be displayed yo UI.\n");
  UI_MSG("     The logging severities are: 0...7 0 = all, 1 = trace(low level OpenSSL tracing), 2 = trace, 3 = debug\n");
  UI_MSG("     4 = info, 5 = warning, 6 = error, 7 fatal\n");
  UI_MSG("  -f Specify logging severity filter level. The logging severities are: 0...7 0 = all,\n");
  UI_MSG("     1 = trace(low level OpenSSL tracing), 2 = trace, 3 = debug, 4 = info, 5 = warning, 6 = error, 7 fatal\n");
  UI_MSG("  -x Provide XML file containing command class definitions. Used to decode\n"
         "     received messages. Default is to search for the following two files:\n");
  UI_MSG("      - %s\n", DEFAULT_ZWAVE_CMD_CLASS_XML);
  UI_MSG("      - %s\n\n", find_xml_file(argv0));
  UI_MSG("  -s Specify the IP address of the Z/IP Gateway. Can be IPv4 or IPv6.\n\n");
  UI_MSG("Examples:\n");
  UI_MSG("  reference_client -s fd00:aaaa::3\n");
  UI_MSG("  reference_client -s 10.168.23.10 -p 123456789012345678901234567890AA\n");
  UI_MSG("  reference_client -s 10.211.55.18 -p 123456789012345678901234567890AA -g ~/test_log.log -f 2 -u 4\n");
}

void parse_server_ip(struct app_config *cfg, char *optarg)
{
  if ((strlen(optarg) + 1) >= sizeof(cfg->server_ip)) {
    UI_MSG_ERR("IP address string too long. Please use correct address.");
  } else {
    strncpy(cfg->server_ip, optarg, sizeof(cfg->server_ip));
  }
}

void parse_xml_filename(struct app_config *cfg, char *optarg)
{
  struct stat _stat;
  if (!stat(optarg, &_stat)) {
    strcpy(cfg->xml_file_path, optarg);
  }
}

void parse_ui_message_severity_level(struct app_config *cfg, char *optarg)
{
  int val;
  const char *s = optarg;
  val = *s - '0';
  if(val < 0 || val > 7){
    UI_MSG_ERR("UI logging severity level is wrong, the level must be between 0-7. INFO logging level is used as default\n");
  } else {
    cfg->ui_message_level = (log_severity_t)val;
  }
}

void parse_log_file_location(struct app_config *cfg, char *optarg)
{
  const char *c= optarg;
  static FILE *file;
  file = fopen(c,"a");
  if(file){
    fclose(file);
    strcpy(cfg->log_file_path, c);
  }else {
    char errbuf[1024];
    strerror_r(errno, errbuf, sizeof(errbuf));
    UI_MSG_ERR("cann't open or create log file path \"%s\": %s\n", c, errbuf);
    UI_MSG("The default '%s'log file location will be used\n", DEFAULT_LOG_FILE_LOCATION);
  }
}

void parse_logging_filter_level(struct app_config *cfg, char *optarg){
  int val;
  const char *s = optarg;
  val = *s - '0';
    if(val < 0 || val > 8){
    UI_MSG_ERR("The logging severity filter level is wrong, the level must be between 0-7. TRACE logging level is used as default\n");
  } else {
    cfg->log_filter_level = (log_severity_t)val;
  }
}

static void parse_prog_args(int prog_argc, char **prog_argv)
{
  int opt;

  cfg.use_dtls = true; // Default value
 
  while ((opt = getopt(prog_argc, prog_argv, "p:s:x:n:u:g:f:")) != -1) {
    switch (opt) {
      case 'p':
        cfg.psk_len = hexstr2bin(optarg, cfg.psk, sizeof(cfg.psk));
        break;
      case 's':
        parse_server_ip(&cfg, optarg);
        break;
      case 'x':
        parse_xml_filename(&cfg, optarg);
        break;
      case 'n': // Use non-secure connection
        cfg.use_dtls = false;
        break;
      case 'u': // control logging severity level causing messages to be displayed to the user
        parse_ui_message_severity_level(&cfg, optarg);
        break;
      case 'g':
        parse_log_file_location(&cfg, optarg);
        break;
      case 'f': //set the logging severity filter level
        parse_logging_filter_level(&cfg, optarg);
        break;
      default: /* '?' */
        print_usage(prog_argv[0]);
        exit(EXIT_FAILURE);
    }
  }
}

int main(int argc, char **argv)
{
  char *input;
  const char *shell_prompt = "(ZIP) ";
  const char *xml_filename;
  struct zconnection *gw_zc = NULL;

#if WITH_MDNS
  pthread_t mdns_thread;
  pthread_create(&mdns_thread, 0, &zresource_mdns_thread_func, 0);
#endif
  UI_MSG("%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
  memset(&cfg, 0, sizeof(cfg));
  parse_prog_args(argc, argv);

  // initialize the log file path
  if(!*cfg.log_file_path) {
    // user did not specified the log file location - now we use the default location
    libzw_log_init(DEFAULT_LOG_FILE_LOCATION);
  }else{
    libzw_log_init(cfg.log_file_path);
  }
  //setting the logging severity filter level
  if(!cfg.log_filter_level){
    libzw_log_set_filter_level(LOGLVL_TRACE);
  }else{
    libzw_log_set_filter_level(cfg.log_filter_level);
  }
  //configuring the UI message severity level that will be displayed to the user
  if(!cfg.ui_message_level){
    libzw_log_set_ui_message_level(LOGLVL_INFO);
  }else{
    libzw_log_set_ui_message_level(cfg.ui_message_level);
  }
  LOG_DEBUG("%s %s", PACKAGE_NAME, PACKAGE_VERSION);
  LOG_DEBUG("=============== Program Start ===============");

  // Configure readline to auto-complete paths when the tab key is hit.
  rl_bind_key('\t', rl_complete);

  if (!strcmp(cfg.server_ip, "")) {
    print_usage(argv[0]);
    return -1;
  }

  if (!*cfg.xml_file_path) {
    // no user specified file - look for the default file in /etc/zipgateway.d
    parse_xml_filename(&cfg, DEFAULT_ZWAVE_CMD_CLASS_XML);
  }
  if (*cfg.xml_file_path) {
    // use the user specified file, or the default if it was found in the expected location
    xml_filename = cfg.xml_file_path;
  } else {
    // fallback to looking in the same directory where the binary is located
    xml_filename = find_xml_file(argv[0]);
  }
  if (!initialize_xml(xml_filename)) {
    LOG_ERROR("Could not load Command Class definitions.");
    return -1;
  }
  initialize_completer(cfg.server_ip);

  gw_zc = zclient_start_addr(cfg.server_ip);
  if (!gw_zc) {
    return -1;
  }
  zconnection_set_transmit_done_func(gw_zc, transmit_done);
  memset(&inclusion_context, 0, sizeof(inclusion_context));
  net_mgmt_init(gw_zc);

  for (;;) {
    // Display prompt and read input (NB: input must be freed after use)...
    input = readline(shell_prompt);

    // Check for EOF.
    if (!input) {
      stop_completer();
      break;
    }

    // Add input to history.
    add_history(input);

    // Do stuff...

    bool keep_running = process_commandline_command(input, gw_zc);

    // Free input.
    free(input);

    // The user wants us to exit
    if (!keep_running) {
      stop_completer();
      break;
    }
  }

  stop_all_client_sessions(true);
  zclient_stop(gw_zc);

#if WITH_MDNS
  pthread_kill(mdns_thread, SIGTERM);
#endif

  return 0;
}
