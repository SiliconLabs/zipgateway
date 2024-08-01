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

#ifndef ZCONNECITON_INTERNAL_H_
#define ZCONNECITON_INTERNAL_H_

#include "zconnection.h"
#include <stdbool.h>
#ifdef WIN32
#include <Ws2tcpip.h>
#else
#include <netinet/in.h>
#endif
#include <openssl/ssl.h>

#define MAXPSK 64

/**
 * Compile time assert macro for C99
 * 
 * Usage: STATIC_ASSERT(bMustBeTrue, STATIC_ASSERT_FAILED_my_error_message);
 * 
 * Can be used both inside and outside methods. Error msg must be unique within scope.
 * BMustBeTrue must be a 'constant integral expression' or be derived from only
 * 'constant integral expressions'
 * Does not generate any real code or variables.
 * Will generate compile error if bMustBeTrue cannot be evaluated at compile time.
 */
#define STATIC_ASSERT(bMustBeTrue, UniqueErrorMessage) \
  enum { UniqueErrorMessage = 1 / (bMustBeTrue) }

// Forward declaration
typedef struct connection_handler_info connection_handler_info_t;

#if WIN32
  typedef DWORD thread_id_t;
#else
  typedef pthread_t thread_id_t;
#endif


/**
 * Object holding the state between a Z/IP client and a Z/IP service.
 */
struct zconnection {
  enum {
    STATE_IDLE,
    STATE_TRANSMISSION_IN_PROGRESS,
  } state;
  
  uint8_t seq;

  uint8_t local_endpoint;  /// Local endpoint of the frame being sent or
                           /// received
  uint8_t remote_endpoint;  /// Remote endpoint of the frame being sent or
                            /// received
  uint8_t encapsulation1;  /// Encapsulation format of the frame being sent or
                           /// received
  uint8_t encapsulation2;  /// Encapsulation format of the frame being sent or
                           /// received

  uint32_t expected_delay; /// Expected delay (in seconds) of the frame which
                           /// has just been sent.

  transfer_func_t send;
  transfer_func_t recv;
  transmit_done_func_t transmit_done;
  connection_handler_info_t *handler_info;
  void* user_context;
  int ms_until_timeout;
  pthread_mutex_t mutex;
  pthread_cond_t send_done_cond;
  pthread_mutex_t send_done_mutex;

  struct ima_data ima;
};

struct connection_handler_info {
  BIO_ADDR *local_addr;
  BIO_ADDR *remote_addr;
  SSL *ssl;       // The SSL object to use in case of DTLS
  BIO *dgram_bio; // The datagram BIO to use in case of non-DTLS (i.e. when ssl == NULL)

  bool is_server;
  bool is_running;
  struct zconnection connection;

  /* Send control messages to the connection handler thread by writing to
   * control_msg_pipe_fd. Currently the only supported control is to write some
   * insignificant data simply to unblock the thread while it is waiting for
   * network messages. Used to have the thread inspect the is_running flag.
   */
  int control_msg_pipe_fd; 
  uint8_t psk[MAXPSK];
  int psk_len;

  thread_id_t tid; // Thread id of connection_handler_thread

#ifdef WIN32
  HANDLE threadhandle;
#else
  // TODO: We need something like this also for Windows
  pthread_cond_t handshake_cond;
  pthread_mutex_t handshake_mutex;
#endif
};


/**
 * Entry point for all packages into the client module
 */
void zconnection_recv_raw(zconnection_t *connection, const uint8_t* data,
                          uint16_t datalen);

/**
 * Should be called every 100ms
 */
void zconnection_timer_tick(zconnection_t *connection, uint16_t tick_interval_ms);

/**
 * Send keepalive to remote
 */
void zconnection_send_keepalive(zconnection_t *connection);

#endif /* ZCONNECITON_INTERNAL_H_ */
