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
#include <string.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <errno.h>

#include "libzwaveip.h"
#include "parse_xml.h"
#include "util.h"
#include "libzw_log.h"
#include "pkgconfig.h"

#ifndef DEFAULT_ZWAVE_CMD_CLASS_XML
#define DEFAULT_ZWAVE_CMD_CLASS_XML "/usr/local/share/zwave/ZWave_custom_cmd_classes.xml"
#endif

#define DEFAULT_LISTEN_PORT ZGW_DTLS_PORT
#define DEFAULT_LOG_FILE_LOCATION "/tmp/libzw_reference_listener.log"

static const uint8_t default_dtls_psk[] = {0x12, 0x34, 0x56, 0x78, 0x90, 0x12, 0x34, 0x56,
                                           0x78, 0x90, 0x12, 0x34, 0x56, 0x78, 0x90, 0xAA};

struct app_config {
  bool    use_dtls;                 //!< Use DTLS or not (i.e. UDP)
  uint8_t psk[MAX_PSK_LEN];         //!< DTLS pre-shared key
  uint8_t psk_len;                  //!< Length of psk
  char listen_ip[INET6_ADDRSTRLEN]; //!< IPv4 or IPv6 address TO LISTEN ON
  uint16_t listen_port;             //!< Port to listen on
  char xml_file_path[PATH_MAX];     //!< Path of XML file with Z-Wave cmd class definitions
  log_severity_t ui_message_level;  //!<logging severity level causing messages to be displayed to the user
  char log_file_path[PATH_MAX];     //!<path for the log file location
  log_severity_t log_filter_level;  //!< logging severity filter level
} cfg;

void application_command_handler(struct zconnection *connection,
                                 const uint8_t *data, uint16_t datalen) {
  int i;
  int len;
  char cmd_classes[400][MAX_LEN_CMD_CLASS_NAME];
  char hexbuf[1024] = {0};

  int idx = 0;
  for (i = 0; i < datalen; i++) {
    idx += snprintf(hexbuf + idx, sizeof(hexbuf) - idx, "%02X ", data[i]);
    if ((i & 0xf) == 0xf) {
      idx += snprintf(hexbuf + idx, sizeof(hexbuf) - idx, "\n");
    }
  }
  UI_MSG("\n-----------------------------------------\nReceived data:\n%s\n\n", hexbuf);

  memset(cmd_classes, 0, sizeof(cmd_classes));
  /* decode() clobbers data - but we are not using it afterwards, hence the
   * typecast */
  decode((uint8_t *)data, datalen, cmd_classes, &len);

  for (i = 0; i < len; i++) {
    UI_MSG("%s\n", cmd_classes[i]);
  }
}

static void print_usage(char *argv0) {
  UI_MSG("\nUsage: %s [-p pskkey] [-n] [-x zwave_xml_file] [-g logging file path] [-u UI message severity level] [-f logging severity filter level] -l ip_address -o port\n\n", basename(argv0));
  UI_MSG("  -p Provide the DTLS pre-shared key as a hex string.\n"
         "     Default value: ");
  for (int i = 0; i < sizeof(default_dtls_psk); i++) {
    UI_MSG("%02X", default_dtls_psk[i]);
  }
  UI_MSG("\n     If the -n option is also used the key will not be used.\n\n");
  UI_MSG("  -n Use a non-secure UDP connection to the gateway. Default is a DTLS connection.\n\n");
  UI_MSG("  -x Provide XML file containing command class definitions. Used to decode\n"
         "     received messages. Default is to search for the following two files:\n");
  UI_MSG("      - %s\n", DEFAULT_ZWAVE_CMD_CLASS_XML);
  UI_MSG("      - %s\n\n", find_xml_file(argv0));
  UI_MSG("  -g Provide the logging file path (i.e., absoulte name path, e.g., /home/parallels/Desktop/listerner_log.log)\n");
  UI_MSG("  -u Specify logging severity level that causes messages to be displayed yo UI.\n");
  UI_MSG("     The logging severities are: 0...7 0 = all, 1 = trace(low level OpenSSL tracing), 2 = trace, 3 = debug\n");
  UI_MSG("     4 = info, 5 = warning, 6 = error, 7 fatal\n");
  UI_MSG("  -f Specify logging severity filter level. The logging severities are: 0...7 0 = all,\n");
  UI_MSG("     1 = trace(low level OpenSSL tracing), 2 = trace, 3 = debug, 4 = info, 5 = warning, 6 = error, 7 fatal\n");
  UI_MSG("  -l Specify the IP address of the interface to listen on.\n"
         "     Can be IPv4 or IPv6.\n\n");
  UI_MSG("  -o Specify the port to listen for incoming connections on.\n"
         "     Default value: %u\n", DEFAULT_LISTEN_PORT);
  UI_MSG("Examples:\n");
  UI_MSG("  reference_listener -l fd00:aaaa::3 -o 54000\n");
  UI_MSG("  reference_listener -l 10.168.23.10 -p 123456789012345678901234567890AA\n");
}

void parse_listen_ip(struct app_config *cfg, char *optarg) {
  if ((strlen(optarg) + 1) >= sizeof(cfg->listen_ip)) {
    UI_MSG_ERR("IP address string too long. Please use correct address.");
  } else {
    strncpy(cfg->listen_ip, optarg, sizeof(cfg->listen_ip));
  }
}

void parse_listen_port(struct app_config *cfg, char *optarg) {
  LOG_DEBUG("Listen port: %s", optarg);
  cfg->listen_port = atoi(optarg);
}

void parse_xml_filename(struct app_config *cfg, char *optarg) {
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
  if(val < 0 || val > 8){
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

static void parse_prog_args(int prog_argc, char **prog_argv) {
  int opt;

  cfg.use_dtls = true; // Default value
  
  while ((opt = getopt(prog_argc, prog_argv, "p:l:o:x:nu:g:f:")) != -1) {
    switch (opt) {
      case 'p':
        cfg.psk_len = hexstr2bin(optarg, cfg.psk, sizeof(cfg.psk));
        break;
      case 'l':
        parse_listen_ip(&cfg, optarg);
        break;
      case 'x':
        parse_xml_filename(&cfg, optarg);
        break;
      case 'o':
        parse_listen_port(&cfg, optarg); 
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
        exit(-1);
    }
  }
}

int main(int argc, char **argv) {
  const char *xml_filename;
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
  if(cfg.log_filter_level){
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

  if (strlen(cfg.listen_ip) > 0) {
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
      return EXIT_FAILURE;
    }

    if (cfg.use_dtls) {
      if (cfg.psk_len == 0) {
        memcpy(cfg.psk, default_dtls_psk, sizeof(default_dtls_psk));
        cfg.psk_len = sizeof(default_dtls_psk);
        LOG_INFO("DTLS PSK not configured - using default.");
      }
    }
    
    if (!cfg.listen_port) {
      cfg.listen_port = DEFAULT_LISTEN_PORT;
    }

    zserver_start(cfg.listen_ip, cfg.listen_port, cfg.use_dtls, cfg.psk, cfg.psk_len,
                  application_command_handler);
  } else {
    LOG_ERROR("IP address not specified or too long.");
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
