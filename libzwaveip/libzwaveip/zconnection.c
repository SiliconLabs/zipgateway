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

#include "zconnection-internal.h"
#include "libzw_log.h"
#include "ZW_classcmd.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define ZIP_PACKET_FLAGS0_ACK_REQ  0x80
#define ZIP_PACKET_FLAGS0_ACK_RES  0x40
#define ZIP_PACKET_FLAGS0_NACK_RES 0x20
#define ZIP_PACKET_FLAGS0_WAIT_RES 0x10
#define ZIP_PACKET_FLAGS0_NACK_QF  0x08
#define ZIP_PACKET_FLAGS0_NACK_OE  0x04
#define ZIP_PACKET_FLAGS1_HDR_EXT_INCL 0x80
#define ZIP_PACKET_FLAGS1_ZW_CMD_INCL 0x40
#define ZIP_PACKET_FLAGS1_MORE_INFORMATION 0x20
#define ZIP_PACKET_FLAGS1_SECURE_ORIGIN 0x10
#define ZIP_OPTION_EXPECTED_DELAY 1
#define ZIP_OPTION_MAINTENANCE_GET 2
#define ZIP_OPTION_MAINTENANCE_REPORT 3
#define ENCAPSULATION_FORMAT_INFO 4

#define ZIP_HDR_EXT_OPTION_TYPE_MASK     0x7f
#define ZIP_HDR_EXT_OPTION_CRITICAL_MASK 0x80

#define IMA_OPTION_RC 0
#define IMA_OPTION_TT 1
#define IMA_OPTION_LWR 2

#define COMMAND_CLASS_ZIP 0x23
#define COMMAND_ZIP_PACKET 0x02
#define COMMAND_ZIP_KEEP_ALIVE 0x03

// Transmissions will timeout if an ack is not received before this many milliseconds
#define ZIP_ACK_TIMEOUT_MS 400

typedef struct zip_hdr {
  uint8_t cmdClass;
  uint8_t cmd;
  uint8_t flags0;
  uint8_t flags1;
  uint8_t seq;
  uint8_t send;
  uint8_t dend;
} __attribute__((packed)) zip_hdr_t;

/** Size of header extension options header */
#define OPT_HDR_SIZE 2


/**
 * @brief Parse header extension option ENCAPSULATION_FORMAT_INFO
 * 
 * @param zc zconnection where packet was received from
 * @param option Pointer to extended header option
 * @param option_data_len Length of extended header option
 */
static void parse_ext_hdr_opt_encap_info(zconnection_t *zc,
                                         const uint8_t *option,
                                         uint8_t option_data_len)
{
  if (option_data_len == 2) {
    const uint8_t *encap = option + OPT_HDR_SIZE;
    zc->encapsulation1 = encap[0];
    zc->encapsulation2 = encap[1];
  } else {
    assert(false);
  }
}

/**
 * @brief Parse header extension option ZIP_OPTION_EXPECTED_DELAY
 * 
 * @param zc zconnection where packet was received from
 * @param option Pointer to extended header option
 * @param option_data_len Length of extended header option
 */
static void parse_ext_hdr_opt_expected_delay(zconnection_t *zc,
                                             const uint8_t *option,
                                             uint8_t option_data_len)
{
  if (option_data_len == 3) {
    const uint8_t *delay = option + OPT_HDR_SIZE;
    // Expected delay in seconds (used with NAck+Waiting)
    zc->expected_delay = (delay[0] << 16) + (delay[1] << 8) + delay[2];
  } else {
    assert(false);
  }
}

/**
 * @brief Parse header extension option ZIP_OPTION_MAINTENANCE_REPORT
 * 
 * @param zc zconnection where packet was received from
 * @param option Pointer to extended header option
 * @param option_data_len Length of extended header option
 */
static void parse_ext_hdr_opt_ima_report(zconnection_t *zc,
                                         const uint8_t *option,
                                         uint8_t option_data_len)
{
  const uint8_t *ima_ptr = 0;
  uint8_t        ima_type = 0;
  uint8_t        ima_len = 0;
  const uint8_t *ima_val = 0;


  for (ima_ptr = option + OPT_HDR_SIZE;
       ima_ptr < (option + OPT_HDR_SIZE + option_data_len);
       ima_ptr += 2 + ima_len) // 2: account for IMA type & length fields
  {
    // Extract TLV
    ima_type = *ima_ptr;
    ima_len  = *(ima_ptr + 1);
    ima_val  = ima_ptr + 2;

    switch (ima_type) {
      case IMA_OPTION_RC:
        if (ima_len >= 1) {
          zc->ima.route_changed = ima_val[0];
        }
        break;

      case IMA_OPTION_TT:
        if (ima_len >= 2) {
          zc->ima.tramission_time = (ima_val[0] << 8) + ima_val[1];
        }
        break;

      case IMA_OPTION_LWR:
        if (ima_len >= 5) {
          // Repeater 1-4
          memcpy(zc->ima.last_working_route, ima_val, 4);
          zc->ima.speed = ima_val[4];
        }

      default:
        break;
    }
  }
}

/**
 * @brief Parse all header extentions in Z/IP packet
 * 
 * @param zc zconnection where packet was received from
 * @param zip_packet_data Pointer to Z/IP packet
 * @param zip_packet_len Length of Z/IP packet
 * @return Total length of header extensions. -1 if parse error.
 */
static int parse_header_extensions(zconnection_t *zc,
                                   const uint8_t *zip_packet_data,
                                   uint16_t zip_packet_len)
{
  const zip_hdr_t *zip_hdr = (const zip_hdr_t*) zip_packet_data;
  size_t zip_hdr_len = sizeof(*zip_hdr);

  if (zip_packet_len < zip_hdr_len) {
    return 0;
  }

  if (!(zip_hdr->flags1 & ZIP_PACKET_FLAGS1_HDR_EXT_INCL)) {
    return 0;
  }

  const uint8_t *zip_hdr_ext = zip_packet_data + zip_hdr_len;

  // Length of (single byte header length) and all options
  uint8_t zip_hdr_ext_len = *zip_hdr_ext;

  if (zip_hdr_ext_len < 1 || (zip_hdr_len + zip_hdr_ext_len) > zip_packet_len) {
    return -1;
  }

  const uint8_t *opt = NULL;
  uint8_t  opt_type = 0;
  uint8_t  opt_critical = 0;
  uint8_t  opt_data_len = 0;

  bool critical_option_ignored = false;

  /* Loop over all header extension options */
  for (opt = zip_hdr_ext + 1; // First option follows total length field
       opt < (zip_hdr_ext + zip_hdr_ext_len);
       opt += OPT_HDR_SIZE + opt_data_len)
  {
    opt_type     = opt[0] & ZIP_HDR_EXT_OPTION_TYPE_MASK;
    opt_critical = opt[0] & ZIP_HDR_EXT_OPTION_CRITICAL_MASK;
    opt_data_len = opt[1];

    switch (opt_type) {
      case ENCAPSULATION_FORMAT_INFO:
        parse_ext_hdr_opt_encap_info(zc, opt, opt_data_len);
        break;

      case ZIP_OPTION_EXPECTED_DELAY:
        parse_ext_hdr_opt_expected_delay(zc, opt, opt_data_len);
        break;

      case ZIP_OPTION_MAINTENANCE_GET:
        break;

      case ZIP_OPTION_MAINTENANCE_REPORT:
        parse_ext_hdr_opt_ima_report(zc, opt, opt_data_len);
        break;

      default:
        if (opt_critical) {
          critical_option_ignored = true;
          fprintf(stderr, "Unsupported critical header extension option %02X. Ignoring package.\n", opt_type);
        }
    }
  }
  if (critical_option_ignored) {
    return -1;
  } else {
    return zip_hdr_ext_len;
  }
}


void zconnection_recv_raw(zconnection_t *zc,
                          const uint8_t *data,
                          uint16_t datalen)
{
  const zip_hdr_t *hdr = (const zip_hdr_t*) data;
  size_t hdr_len = sizeof(*hdr);

  // Just to ensure packing and alignment is not broken
  STATIC_ASSERT((sizeof(zip_hdr_t) == 7), STATIC_ASSERT_FAILED_zip_hdr_len_check); 

  if (datalen < hdr_len) {
    return;
  }

  if (hdr->cmdClass != COMMAND_CLASS_ZIP || hdr->cmd != COMMAND_ZIP_PACKET) {
    return;
  }

  LOG_TRACE("Got ZIP packet: flags0:0x%02x, flags1:0x%02x, seq:%02d, send:0x%02x, dend:0x%02x",
            hdr->flags0,
            hdr->flags1,
            hdr->seq,
            hdr->send,
            hdr->dend);

  pthread_mutex_lock(&zc->mutex);

  if (hdr->flags0 & ZIP_PACKET_FLAGS0_ACK_REQ) {
    struct zip_hdr ack_pkt = {0};
    ack_pkt.cmdClass = COMMAND_CLASS_ZIP;
    ack_pkt.cmd = COMMAND_ZIP_PACKET;
    ack_pkt.flags0 = ZIP_PACKET_FLAGS0_ACK_RES;
    ack_pkt.flags1 = 0;
    ack_pkt.send = hdr->dend;
    ack_pkt.dend = hdr->send;
    ack_pkt.seq = hdr->seq;
    LOG_TRACE("Sending ACK_RES");
    /* Next one will usually invoke send_dtls() or send_udp() */
    zc->send(zc, (uint8_t*)&ack_pkt, sizeof(ack_pkt));
  }

  zc->encapsulation1 = 0;
  zc->encapsulation2 = 0;
  zc->expected_delay = 0;

  int hdr_ext_len = parse_header_extensions(zc, data, datalen);
  LOG_TRACE("hdr_ext_len = %d", hdr_ext_len);

  if (hdr_ext_len >= 0) {
    if (zc->state == STATE_TRANSMISSION_IN_PROGRESS && hdr->seq == zc->seq) {

      if (hdr->flags0 & ZIP_PACKET_FLAGS0_ACK_RES) {
        LOG_TRACE("Got ACK_RES. New state = STATE_IDLE");
        zc->state = STATE_IDLE;
        if (zc->transmit_done) {
          zc->transmit_done(zc, TRANSMIT_OK);
          pthread_cond_signal(&zc->send_done_cond);
        }
      } else if (hdr->flags0 & ZIP_PACKET_FLAGS0_NACK_RES) {
        if (hdr->flags0 & ZIP_PACKET_FLAGS0_WAIT_RES) {
          LOG_TRACE("Got NACK+WAIT RES");
          /* Got NAck+Waiting Response so we extend the timeout to 90 seconds
           * (the gateway MUST send NAck+Waiting updates every 60 seconds, but
           * we should not time out until after 90 seconds) */
          zc->ms_until_timeout = 90000;

          if (zc->expected_delay == 0) {
            zc->expected_delay = 90; // Default to 90 seconds if not specified
          }

          LOG_TRACE("ms_until_timeout = %d ms, zc->expected_delay = %d",
                    zc->ms_until_timeout,
                    zc->expected_delay);

          LOG_TRACE("TRANSMIT_WAIT (state == STATE_TRANSMISSION_IN_PROGRESS)");
          if (zc->transmit_done) {
            zc->transmit_done(zc, TRANSMIT_WAIT);
          }
        } else {
          /* Any other NAck response (Queue/mailbox Full and Option Error)
           * essentially means that the package was not delivered */
          LOG_TRACE("Got NACK_RES. New state = STATE_IDLE");
          zc->state = STATE_IDLE;
          if (zc->transmit_done) {
            zc->transmit_done(zc, TRANSMIT_NOT_OK);
            pthread_cond_signal(&zc->send_done_cond);
          }
        }
      }
    } else {
      LOG_TRACE("Dropping package. zc->state = %d, hdr->seq = %d, zc->seq = %d", zc->state, hdr->seq, zc->seq);
    }
  } else {
    /* Error while checking for header extensions */
  }

  pthread_mutex_unlock(&zc->mutex);

  if (hdr->flags1 & ZIP_PACKET_FLAGS1_ZW_CMD_INCL) {
    size_t total_hdr_len = hdr_len + hdr_ext_len; 
    if (datalen > total_hdr_len) {
      LOG_TRACE("Processing ZW Command");
      zc->recv(zc, data + total_hdr_len, datalen - total_hdr_len);
    }
  }
}

uint8_t zconnection_send_async(struct zconnection* connection,
                               const uint8_t* data, uint16_t datalen,
                               int response) {
  uint8_t buf[512];
  int offset = 0;
  struct zip_hdr* hdr = (struct zip_hdr*)buf;

  pthread_mutex_lock(&connection->mutex);

  if (connection->state == STATE_TRANSMISSION_IN_PROGRESS) {
    pthread_mutex_unlock(&connection->mutex);
    return 0;
  }

  LOG_TRACE("connection->state = %d", connection->state);

  connection->seq++;
  connection->state = STATE_TRANSMISSION_IN_PROGRESS;
  connection->ms_until_timeout = ZIP_ACK_TIMEOUT_MS;
  LOG_TRACE("New state = STATE_TRANSMISSION_IN_PROGRESS, ms_until_timeout = %d",
            connection->ms_until_timeout);

  LOG_ERROR("data: 0x%02X datalen: %d\n", data[0], datalen);
  if (data[0] != COMMAND_CLASS_ZIP_ND) { // Do not ZIP encapsulate ZIP ND
    hdr->cmdClass = COMMAND_CLASS_ZIP;
    hdr->cmd = COMMAND_ZIP_PACKET;
    hdr->seq = connection->seq;
    hdr->flags0 = ZIP_PACKET_FLAGS0_ACK_REQ;
    hdr->flags1 = ZIP_PACKET_FLAGS1_ZW_CMD_INCL | ZIP_PACKET_FLAGS1_HDR_EXT_INCL |
                  ZIP_PACKET_FLAGS1_SECURE_ORIGIN;
    hdr->dend = connection->remote_endpoint;
    hdr->send = connection->local_endpoint;
    offset = sizeof(struct zip_hdr);

    buf[offset++] = 3;
    buf[offset++] = ZIP_OPTION_MAINTENANCE_GET;
    buf[offset++] = 0;

    if (response) {
      buf[sizeof(struct zip_hdr)] += 4;
      buf[offset++] = 0x80 | ENCAPSULATION_FORMAT_INFO;
      buf[offset++] = 2;
      buf[offset++] = connection->encapsulation1;
      buf[offset++] = connection->encapsulation2;
    }
    if ((offset + datalen) > sizeof(buf)) {
      pthread_mutex_unlock(&connection->mutex);
      return 0;
    }
  }
  
  memcpy(&buf[offset], data, datalen);

  pthread_cond_init(&connection->send_done_cond, 0);
  pthread_mutex_init(&connection->send_done_mutex, 0);
  connection->send(connection, buf, offset + datalen);

  pthread_mutex_unlock(&connection->mutex);
  return 1;
}

/**
 * Wait for the current transmission to complete
 */
void zconnection_wait_for_transmission(struct zconnection* connection) {

  pthread_mutex_lock(&connection->mutex);
  if (connection->state == STATE_TRANSMISSION_IN_PROGRESS) {
    pthread_mutex_unlock(&connection->mutex);
    LOG_TRACE("Waiting for send_done_cond...");

    pthread_cond_wait(&connection->send_done_cond,
                      &connection->send_done_mutex);
    LOG_TRACE("Waiting for send_done_cond -> DONE");
  } else {
    pthread_mutex_unlock(&connection->mutex);
  }
}

void zconnection_send_keepalive(struct zconnection* connection) {
  uint8_t keepalive[] = {COMMAND_CLASS_ZIP, COMMAND_ZIP_KEEP_ALIVE, ZIP_PACKET_FLAGS0_ACK_REQ};
  connection->send(connection, keepalive, sizeof(keepalive));
}

void zconnection_timer_tick(struct zconnection* connection, uint16_t tick_interval_ms) {

  if (connection->state == STATE_TRANSMISSION_IN_PROGRESS) {
    connection->ms_until_timeout -= tick_interval_ms;

    if (connection->ms_until_timeout <= 0) {
      LOG_TRACE("New state = STATE_IDLE");
      connection->state = STATE_IDLE;
      if (connection->transmit_done) {
        LOG_TRACE("Calling transmit_done(TRANSMIT_TIMEOUT)");
        connection->transmit_done(connection, TRANSMIT_TIMEOUT);
        pthread_cond_signal(&connection->send_done_cond);
      }
    }
  }
}

bool zconnection_is_busy(const struct zconnection* connection) {
  return (connection->state != STATE_IDLE) ? true : false;
}

const struct ima_data* zconnection_get_ima_data(
    const struct zconnection* connection) {
  return &connection->ima;
}

uint16_t zconnection_get_expected_delay(const struct zconnection* connection) {
  return connection->expected_delay;
}

void zconnection_set_transmit_done_func(struct zconnection* connection,
                                        transmit_done_func_t func) {
  connection->transmit_done = func;
}

void zconncetion_set_endpoint(struct zconnection* connection,
                              uint8_t endpoint) {
  connection->remote_endpoint = endpoint;
}

void zconnection_get_remote_addr(struct zconnection *connection, struct sockaddr_storage *remote_addr) {
  connection_handler_info_t *ch = connection->handler_info;
  memcpy(remote_addr, &ch->remote_addr, sizeof(struct sockaddr_storage));
}

void zconnection_set_user_context(struct zconnection *connection, void *context) {
	connection->user_context = context;
}

void *zconnection_get_user_context(struct zconnection *connection) {
	return connection->user_context;
}

