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

/*
 * Copyright (C) 2009 - 2012 Robin Seggelmann, seggelmann@fh-muenster.de,
 *                           Michael Tuexen, tuexen@fh-muenster.de
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef WIN32
#include <winsock2.h>
#include <Ws2tcpip.h>
#define in_port_t u_short
#define ssize_t int
#else
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <assert.h>
#endif

#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#include "libzwaveip.h"
#include "zconnection-internal.h"
#include "libzw_log.h"
#include "ssldbg_utils.h"

#define BUFFER_SIZE (1 << 16)
#define COOKIE_SECRET_LENGTH 16

#ifndef INVALID_SOCKET
  #define INVALID_SOCKET (-1)
#endif

typedef enum {
  READ_RESULT_GOT_DATA,
  READ_RESULT_NO_DATA,
  READ_RESULT_RETRY,
  READ_RESULT_FATAL_ERROR
} read_result_t;

static bool cookie_initialized = false;
static int user_data_index = -1;
static unsigned char cookie_secret[COOKIE_SECRET_LENGTH];
static pthread_mutex_t ssl_init_lock = PTHREAD_MUTEX_INITIALIZER;

/** Cipher list to use for SSL_CTX_set_cipher_list() with DTLS client and
 * server.
 *
 * If we simply specify "PSK" the client will announce support for 49 PSK
 * ciphers in the client hello and the client/server will likely agree on an
 * ECDHE PSK cipher e.g. TLS_ECDHE_PSK_WITH_AES256_CBC_SHA384 (0xc038). It works
 * well between libzwaveip and Z/IP gateway. BUT it causes problems for
 * Wireshark which cannot decode the DTLS payload. Haven't fully analyzed it,
 * but it seems to be related to Wireshark limitations with ECDHE. To get around
 * that problem we specifically select a single cipher that works with Z/IP
 * gateway and Wireshark. Fell free to select something stronger, if Wireshark
 * support is not in play.
 */
static const char *dtls_cipher_list = "PSK-AES256-CBC-SHA";

static void openssl_init()
{
  static bool ssl_init_done = false;

  pthread_mutex_lock(&ssl_init_lock);
  if (!ssl_init_done) {
	  if (user_data_index == -1) {
      user_data_index = SSL_get_ex_new_index(0, "pinfo index", NULL, NULL, NULL);
	  }

    ssldbg_utils_init();

    ssl_init_done = true;
  }
  pthread_mutex_unlock(&ssl_init_lock);
}


static int handle_socket_error(int sys_errno)
{
  LOG_TRACE("");

  switch (sys_errno) {
    case EINTR:
      /* Interrupted system call.
       * Just ignore.
       */
      LOG_DEBUG("Interrupted system call!");
      return 1;
    case EBADF:
      /* Invalid socket.
       * Must close connection.
       */
      LOG_ERROR("Invalid socket!");
      return 0;
      break;
#ifdef EHOSTDOWN
    case EHOSTDOWN:
      /* Host is down.
       * Just ignore, might be an attacker
       * sending fake ICMP messages.
       */
      LOG_DEBUG("Host is down!");
      return 1;
#endif
#ifdef ECONNRESET
    case ECONNRESET:
      /* Connection reset by peer.
       * Just ignore, might be an attacker
       * sending fake ICMP messages.
       */
      LOG_DEBUG("Connection reset by peer!");
      return 1;
#endif
    case ENOMEM:
      /* Out of memory.
       * Must close connection.
       */
      LOG_FATAL("Out of memory!");
      return 0;
      break;
    case EACCES:
      /* Permission denied.
       * Just ignore, we might be blocked
       * by some firewall policy. Try again
       * and hope for the best.
       */
      LOG_DEBUG("Permission denied!");
      return 1;
      break;
    case EAGAIN:
      return 1;
    default:
      /* Something unexpected happened */
      LOG_ERRNO(LOGLVL_ERROR, "Unexpected error!", sys_errno);
      return 0;
      break;
  }
  return 0;
}

static int str_to_bioaddr(BIO_ADDR *ba, const char *addr_str, int port)
{
  LOG_TRACE("");
  if (addr_str == NULL || strlen(addr_str) == 0) {
    BIO_ADDR_rawmake(ba, AF_INET6, &in6addr_any, sizeof(in6addr_any), htons(port));
  } else {
    struct in_addr addr4;
    struct in6_addr addr6;
    if (inet_pton(AF_INET, addr_str, &addr4) == 1) {
      BIO_ADDR_rawmake(ba, AF_INET, &addr4, sizeof(addr4), htons(port));
    } else if (inet_pton(AF_INET6, addr_str, &addr6) == 1) {
      BIO_ADDR_rawmake(ba, AF_INET6, &addr6, sizeof(addr6), htons(port));
    } else {
      LOG_ERROR("\"%s\" is not a valid IPv4 or IPv6 address.", addr_str);
      return 0;
    }
  }
  return 1;
}

static void bioaddr_to_str(const BIO_ADDR *ba, char *buf, int max_len)
{
  char *hostname = BIO_ADDR_hostname_string(ba, 1);
  char *service  = BIO_ADDR_service_string(ba, 1);

  const char *fmt = "%s:%s";
  if (BIO_ADDR_family(ba) == AF_INET6) {
    fmt = "[%s]:%s";
  }
  snprintf(buf, max_len, fmt, hostname, service);

  OPENSSL_free(hostname);
  OPENSSL_free(service);
}

static int generate_cookie(SSL *ssl, unsigned char *cookie,
                           unsigned int *cookie_len)
{
  // TODO rewrite this using BIO_ADDR, BIO_ADDR_rawaddress(), BIO_ADDR_rawport()

  unsigned char *buffer, result[EVP_MAX_MD_SIZE];
  unsigned int length = 0, resultlength;
  union {
    struct sockaddr_storage ss;
    struct sockaddr_in6 s6;
    struct sockaddr_in s4;
  } peer;

  LOG_TRACE("");

  /* Initialize a random secret */
  if (!cookie_initialized) {
    if (!RAND_bytes(cookie_secret, COOKIE_SECRET_LENGTH)) {
      LOG_ERROR("error setting random cookie secret");
      return 0;
    }
    cookie_initialized = true;
  }

  /* Read peer information */
  (void)BIO_dgram_get_peer(SSL_get_rbio(ssl), &peer);

  /* Create buffer with peer's address and port */
  length = 0;
  switch (peer.ss.ss_family) {
    case AF_INET:
      length += sizeof(struct in_addr);
      break;
    case AF_INET6:
      length += sizeof(struct in6_addr);
      break;
    default:
      OPENSSL_assert(0);
      break;
  }
  length += sizeof(in_port_t);
  buffer = (unsigned char *)OPENSSL_malloc(length);

  if (buffer == NULL) {
    LOG_FATAL("out of memory");
    return 0;
  }

  switch (peer.ss.ss_family) {
    case AF_INET:
      memcpy(buffer, &peer.s4.sin_port, sizeof(in_port_t));
      memcpy(buffer + sizeof(peer.s4.sin_port), &peer.s4.sin_addr,
             sizeof(struct in_addr));
      break;
    case AF_INET6:
      memcpy(buffer, &peer.s6.sin6_port, sizeof(in_port_t));
      memcpy(buffer + sizeof(in_port_t), &peer.s6.sin6_addr,
             sizeof(struct in6_addr));
      break;
    default:
      OPENSSL_assert(0);
      break;
  }

  /* Calculate HMAC of buffer using the secret */
  HMAC(EVP_sha1(), (const void *)cookie_secret, COOKIE_SECRET_LENGTH,
       (const unsigned char *)buffer, length, result, &resultlength);
  OPENSSL_free(buffer);

  memcpy(cookie, result, resultlength);
  *cookie_len = resultlength;

  LOG_TRACE("cookie_len = %d", resultlength);

  return 1;
}

static int verify_cookie(SSL *ssl, const unsigned char *cookie,
                         unsigned int cookie_len)
{
  // TODO rewrite this using BIO_ADDR, BIO_ADDR_rawaddress(), BIO_ADDR_rawport()

  unsigned char *buffer, result[EVP_MAX_MD_SIZE];
  unsigned int length = 0, resultlength;
  union {
    struct sockaddr_storage ss;
    struct sockaddr_in6 s6;
    struct sockaddr_in s4;
  } peer;

  LOG_TRACE("");

  /* If secret isn't initialized yet, the cookie can't be valid */
  if (!cookie_initialized) return 0;

  /* Read peer information */
  (void)BIO_dgram_get_peer(SSL_get_rbio(ssl), &peer);

  /* Create buffer with peer's address and port */
  length = 0;
  switch (peer.ss.ss_family) {
    case AF_INET:
      length += sizeof(struct in_addr);
      break;
    case AF_INET6:
      length += sizeof(struct in6_addr);
      break;
    default:
      OPENSSL_assert(0);
      break;
  }
  length += sizeof(in_port_t);
  buffer = (unsigned char *)OPENSSL_malloc(length);

  if (buffer == NULL) {
    LOG_FATAL("Out of memory");
    return 0;
  }

  switch (peer.ss.ss_family) {
    case AF_INET:
      memcpy(buffer, &peer.s4.sin_port, sizeof(in_port_t));
      memcpy(buffer + sizeof(in_port_t), &peer.s4.sin_addr,
             sizeof(struct in_addr));
      break;
    case AF_INET6:
      memcpy(buffer, &peer.s6.sin6_port, sizeof(in_port_t));
      memcpy(buffer + sizeof(in_port_t), &peer.s6.sin6_addr,
             sizeof(struct in6_addr));
      break;
    default:
      OPENSSL_assert(0);
      break;
  }

  /* Calculate HMAC of buffer using the secret */
  HMAC(EVP_sha1(), (const void *)cookie_secret, COOKIE_SECRET_LENGTH,
       (const unsigned char *)buffer, length, result, &resultlength);
  OPENSSL_free(buffer);

  if (cookie_len == resultlength && memcmp(result, cookie, resultlength) == 0)
  {
    LOG_TRACE("-> cookie OK");
    return 1;
  }

  LOG_TRACE("-> cookie NOT OK");
  return 0;
}

static int dtls_verify_callback(int ok, X509_STORE_CTX *ctx)
{
  LOG_TRACE("");

  /* This function should ask the user
   * if he trusts the received certificate.
   * Here we always trust.
   */
  return 1;
}

static int is_ssl_shutdown(const SSL *ssl)
{
  int shutdown_state = SSL_get_shutdown(ssl);
  switch (shutdown_state) {
    case SSL_SENT_SHUTDOWN:
      LOG_INFO("SSL_SENT_SHUTDOWN. Ending thread ...");
      return 1;
      break;

    case SSL_RECEIVED_SHUTDOWN:
      LOG_INFO("SSL_RECEIVED_SHUTDOWN. Ending thread...");
      return 1;
      break;

    default:
      return 0;
  }
}

static void do_ssl_shutdown(SSL *ssl)
{
  int ret;

  do {
    /* We only do unidirectional shutdown */
    ret = SSL_shutdown(ssl);
    LOG_DEBUG("SSL_shutdown() returned %d", ret);
    if (ret < 0) {
      switch (SSL_get_error(ssl, ret)) {
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
        case SSL_ERROR_WANT_ASYNC:
        case SSL_ERROR_WANT_ASYNC_JOB:
          /* We just do busy waiting. Nothing clever */
          continue;
      }
      ret = 0;
    }
  } while (ret < 0);
}

static void update_connection_handler_thread_is_running(connection_handler_info_t *ch,
                                                        bool new_is_running)
{
  // TODO  add Windows support
#ifndef WIN32
  pthread_mutex_lock(&ch->handshake_mutex);
  ch->is_running = new_is_running;
  pthread_cond_signal(&ch->handshake_cond);
  pthread_mutex_unlock(&ch->handshake_mutex);
#endif
}

static int wait_for_data(int fd1, int fd2, int timeout_ms)
{
  fd_set fds;
  struct timeval timeout = {0};

  timeout.tv_sec  = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;

  /* Create an 'fd_set' that has the two socket descriptors we want to watch */
  FD_ZERO(&fds);
  FD_SET(fd1, &fds);
  FD_SET(fd2, &fds);

  int max_fd_val = fd1 > fd2 ? fd1 : fd2;

  int fd_count = select(max_fd_val + 1, &fds, NULL, NULL, &timeout);

  if (fd_count == -1 ) {
    LOG_ERRNO(LOGLVL_ERROR, "select() failed", errno);
  }
  return fd_count;
}

static read_result_t get_ssl_read_result(const SSL *ssl, int ssl_read_res, int sys_errno)
{
  if (ssl_read_res > 0) {
    return READ_RESULT_GOT_DATA;
  }

  int ssl_error = SSL_get_error(ssl, ssl_read_res);
  switch (ssl_error) {
    case SSL_ERROR_NONE:
      LOG_TRACEX("SSL_ERROR_NONE");
      return READ_RESULT_NO_DATA;
      break;

    case SSL_ERROR_WANT_READ:
      LOG_TRACEX("SSL_ERROR_WANT_READ");
      return READ_RESULT_NO_DATA;
      break;

    case SSL_ERROR_ZERO_RETURN:
      LOG_TRACEX("SSL_ERROR_ZERO_RETURN");
      /* This usually means that the peer has terminated the connection.
       * is_ssl_shutdown() will detect that. */
      return READ_RESULT_RETRY;
      break;

    case SSL_ERROR_SYSCALL:
      LOG_TRACEX("SSL_ERROR_SYSCALL");
      if (handle_socket_error(sys_errno)) {
        return READ_RESULT_RETRY;
      } else {
        return READ_RESULT_FATAL_ERROR;
      }
      break;

    case SSL_ERROR_SSL:
      LOG_TRACEX("SSL_ERROR_SSL");
      ssldbg_print_errorstack();
      return READ_RESULT_FATAL_ERROR;
      break;

    default:
      LOG_ERROR("Unexpected error %d from SSL_get_error()", ssl_error);
      ssldbg_print_errorstack();
      return READ_RESULT_FATAL_ERROR;
      break;
  }
}

static read_result_t get_udp_read_result(const BIO *bio, int res)
{
  if (res > 0) {
    return READ_RESULT_GOT_DATA;
  }
  
  if (BIO_should_retry(bio)) {
    return READ_RESULT_NO_DATA;
  } else {
    return READ_RESULT_FATAL_ERROR;
  }
}

static connection_handler_info_t * conn_handler_info_new(void)
{
  connection_handler_info_t *ch = malloc(sizeof(*ch));
  if (ch) {
    memset(ch, 0, sizeof(*ch));
  }
  return ch;
}

static void conn_handler_info_free(connection_handler_info_t *ch)
{
  if (ch) {
    if (ch->remote_addr) {
      BIO_ADDR_free(ch->remote_addr);
    }
    if (ch->local_addr) {
      BIO_ADDR_free(ch->local_addr);
    }
    /* Just in case ch is still in use by a sub-thread (should never happen),
     * we want it to fail quickly */
    memset(ch, 0, sizeof(*ch));
    free(ch);
  }
}

/**
 * Thread function that constantly checks for incoming data and, if running as a
 * client, periodically sends out keep-alive packages and call zconnection 100ms
 * "clock tick" function (for connection timeout handling)
 *
 * The "context" for this function is defined in the connection_handler_info_t
 * struct "ch". Four basic "modes" are supported:
 *
 * ## Server ## (ch->is_server == true)
 * 1) DTLS server (ch->ssl != NULL && ch->dgram_bio == NULL)
 * 2) UDP server  (ch->ssl == NULL && ch->dgram_bio != NULL)
 *
 * ## Client ## (ch->is_server == false)
 * 3) DTLS client (ch->ssl != NULL && ch->dgram_bio == NULL)
 * 4) UDP client  (ch->ssl == NULL && ch->dgram_bio != NULL)
 *
 * Prior to starting this thread the DTLS or UDP connections must have been
 * established. For DTLS the handshake must have completed. For UDP the BIO
 * datagram object must have been initialized.
 *
 * ## Timeout handling ##
 *
 * a) Servers (DTLS and UDP): Exit the thread if no data is received before
 *    timeout_count gets to max_timeout_count.
 *
 * b) Clients (DTLS): Send a keepalive packet to Z/IP gateway every 30 seconds
 *    (roughly). UDP clients do not (currently) send keepalive packets (there's
 *    no DTLS session to "keep" alive).
 *
 * ## Cleanup ##
 *
 * When running as a server the thread takes ownership of "ch" and must
 * de-allocate it when done.
 *
 * When running as a client the thread does NOT own "ch" (specifically it is
 * owned by zclient_start() and must be available for zclient_stop() even if the
 * thread has exited).
 */
#ifdef WIN32
DWORD WINAPI connection_handler_thread(LPVOID *conn_handler_info)
#else
void* connection_handler_thread(void *conn_handler_info)
#endif
{
  // Configuration parameters
  const uint32_t session_timeout_ms = 25 * 60000; //!< Stop thread if no activity for this long (except for non-DTLS client mode)
  const uint16_t short_recv_timeout_ms = 100;   //!< Fast spinning receive timeout (the only timeout used in client mode. MUST be 100!)
  const uint16_t long_recv_timeout_ms  = 5000;  //!< Slow receive timeout used in server after fast receive timeout.
  const uint32_t keepalive_interval_ms = 30000; //!< Interval between keepalives in client mode

  uint32_t ms_since_last_data = 0; //!< Time since data was sent or received

  connection_handler_info_t *ch = conn_handler_info;

  struct timeval short_recv_timeout;

  BIO *rbio = NULL;
  int read_sock = INVALID_SOCKET;
  read_result_t read_result;

  int len = 0;
  uint8_t buf[BUFFER_SIZE];

  LOG_DEBUG("Thread ENTER");

  /* Create a pipe to pass control messages to this thread (currently only used
   * to wake this thread up from the UI thread when signalling it wants us to
   * terminate). The pipe consist of a pair of connected read (index 0) and
   * write (index 1) file descriptors. The UI (or other) threads write to the
   * write descriptor, we read from the read descriptor.
   */
  int self_pipefd[2];
  int unused ATTR_UNUSED = pipe(self_pipefd);
  /* Tell the other threads about the write descriptor */
  ch->control_msg_pipe_fd = self_pipefd[1]; 

  if (ch->ssl) {
    rbio = SSL_get_rbio(ch->ssl);
  } else {
    rbio = ch->dgram_bio;
  }
  BIO_get_fd(rbio, &read_sock);

  short_recv_timeout.tv_sec  = short_recv_timeout_ms / 1000;
  short_recv_timeout.tv_usec = (short_recv_timeout_ms % 1000) * 1000;

  LOG_DEBUG("Setting RECV timeout for read_sock %d to %d.%03ds",
            read_sock,
            short_recv_timeout.tv_sec,
            short_recv_timeout.tv_usec / 1000);
  BIO_ctrl(rbio, BIO_CTRL_DGRAM_SET_RECV_TIMEOUT, 0, &short_recv_timeout);

  /* Signal to parent thread that we're running */
  update_connection_handler_thread_is_running(ch, true);

  while ((ch->ssl == NULL || !is_ssl_shutdown(ch->ssl))
         && ms_since_last_data < session_timeout_ms
         && ch->is_running)
  {
    /* Read data from either the DTLS or UDP connection and convert to uniform
     * result code (will timeout after short_recv_timeout_ms) */
    if (ch->ssl) {
      len = SSL_read(ch->ssl, buf, sizeof(buf));
      read_result = get_ssl_read_result(ch->ssl, len, errno);
    } else {
      len = BIO_read(rbio, buf, sizeof(buf));
      read_result = get_udp_read_result(rbio, len);
    }

    switch (read_result) {

      case READ_RESULT_GOT_DATA:
        LOG_TRACE("READ_RESULT_GOT_DATA (read %d bytes)", len);
        ms_since_last_data = 0;

        /* Pass received data to recv handler */
        zconnection_recv_raw(&ch->connection, buf, len);
        break;

      case READ_RESULT_NO_DATA:
        if (BIO_dgram_recv_timedout(rbio)) {
          /* There was no data available to read */

          /* This is not super accurate time keeping. In client mode where we
           * spin quickly the count will drift a few seconds behind actual time
           * every minute. But it is sufficient for our use */
          ms_since_last_data += short_recv_timeout_ms;

          if (ch->ssl || ch->is_server) {
            // LOG_TRACE("ms_since_last_data = %d", ms_since_last_data);
          }

          if (ch->is_server) {
            /* As a server we don't have anything else to do than wait for data.
             * To allow for the overall session timeout check we wait
             * long_recv_timeout_ms instead of infinitely (and since we don't
             * need to spin at 100ms intervals as in client mode,
             * long_recv_timeout_ms is significantly larger than
             * short_recv_timeout_ms). */
            int fd_count = wait_for_data(read_sock, self_pipefd[0], long_recv_timeout_ms);

            /* Did an error happen? */
            if (fd_count < 0) {
              goto cleanup;
            }

            /* No file descriptors with data means the wait timed out */
            if (fd_count == 0) {
              ms_since_last_data += long_recv_timeout_ms;
            }
          } else {
            /* In client mode we need to update the zconnection timer every
             * ~100ms. Since we got gere because of a read timeout using
             * recv_timeout (~100ms) then now is a good time to do that */
            zconnection_timer_tick(&ch->connection, short_recv_timeout_ms);

            if (ch->ssl) {
              /* A DTLS client should ping the gateway periodically to keep the
               * gateway from shutting down the DTLS connection */
              if (ms_since_last_data >= keepalive_interval_ms) {
                LOG_TRACE("Send keep-alive");
                zconnection_send_keepalive(&ch->connection);
                ms_since_last_data = 0;
              }
            } else {
              /* Non-DTLS client sessions (i.e. UDP sessions) should never time
               * out automatically */
                ms_since_last_data = 0;
            }
          }
        }
        break;

      case READ_RESULT_RETRY:
        /* Nothing to do here - just try reading again */
        break;        

      case READ_RESULT_FATAL_ERROR:
        LOG_ERROR("Fatal read error - aborting...");
        goto cleanup;
        break;

      default:
        break;
    }
  } // while

cleanup:
  LOG_DEBUG("Cleanup");

  BIO_closesocket(self_pipefd[0]);
  BIO_closesocket(self_pipefd[1]);

  if (ch->ssl) {
    do_ssl_shutdown(ch->ssl);
    /* SSL_free() will deallocate everything associated with the object (BIOs,
     * sockets etc) */
    SSL_free(ch->ssl);
    ch->ssl = NULL;
  } else {
    BIO_free(ch->dgram_bio);
    ch->dgram_bio = NULL;
  }

  if (ch->is_server) {
    /* Nobody are waiting for server threads to exit. Will have to clean up
     * after itself */ 
    conn_handler_info_free(ch);
  }

  /* Signal to parent thread that we're not running */
  update_connection_handler_thread_is_running(ch, false);

  LOG_DEBUG("Thread EXIT");

  return NULL;
}

static bool start_connection_handler_thread(connection_handler_info_t *ch)
{
#ifdef WIN32
  // TODO what to use for pthread_mutex and pthread_cond on windows?

  ch->threadhandle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)connection_handler_thread, ch,
                    0, &ch->tid);
  if (ch->threadhandle == NULL) {
    LOG_FATAL("CreateThread failed. Error = %d", GetLastError());
    return false;
  }
#else
  pthread_mutex_init(&ch->handshake_mutex, 0);
  pthread_mutex_init(&ch->connection.mutex, 0);
  pthread_cond_init(&ch->handshake_cond, 0);

  if (pthread_create(&ch->tid, NULL, connection_handler_thread, ch) != 0) {
    LOG_ERRNO(LOGLVL_FATAL, "pthread_create() failed", errno);
    return false;
  }

  pthread_mutex_lock(&ch->handshake_mutex);
  if (!ch->is_running) {
    /* connection_handler_thread has not initialized yet. Wait here until
     * it signals that it has updated ch->is_running */
	  pthread_cond_wait(&ch->handshake_cond, &ch->handshake_mutex);
  }
  pthread_mutex_unlock(&ch->handshake_mutex);
#endif

  return ch->is_running;
}

static void term_connection_handler_thread(connection_handler_info_t *ch)
{
  // TODO add Windows support

  pthread_mutex_lock(&ch->handshake_mutex);

  // Tell the connection handler thread to stop
  ch->is_running = false;

  /* Send a single byte to the connection handler thread to wake it up from the
   * select() call (if it's not sleeping in a select() that will work too - it
   * checks "is_running" in its main loop anyway)
   */
  ssize_t bytes_written = write(ch->control_msg_pipe_fd, "0", 1);
  LOG_DEBUG("write() on ch handler control_msg_pipe_fd returned %ld (%s)",
             bytes_written,
             (bytes_written == 1) ? "OK" : "ERROR");
  if (bytes_written == 1) {
    /* Only wait if we delivered the stop message */
    pthread_cond_wait(&ch->handshake_cond, &ch->handshake_mutex);
  }
  pthread_mutex_unlock(&ch->handshake_mutex);
}

static bool wait_for_connection_handler_thread(connection_handler_info_t *ch)
{
#ifdef WIN32
  if (WaitForSingleObject(ch->threadhandle, INFINITE) == WAIT_FAILED) {
    LOG_ERROR("Failed waiting for connection handler thread to exit. Error = %d", GetLastError());
    return false;
  }
#else
  if (pthread_join(ch->tid, NULL)) {
    LOG_ERRNO(LOGLVL_ERROR, "pthread_join() failed", errno);
    return false;
  }
#endif

  return true;
}

static unsigned int psk_server_callback(SSL *ssl, const char *identity,
                                        unsigned char *psk,
                                        unsigned int max_psk_len)
{
  LOG_TRACE("");
  unsigned int bytes_to_copy = 0;

  connection_handler_info_t *ch = SSL_get_ex_data(ssl, user_data_index);
  if (ch) {
    bytes_to_copy = (ch->psk_len < max_psk_len) ? ch->psk_len : max_psk_len;
    memcpy(psk, ch->psk, bytes_to_copy);
  }

  LOG_DEBUG("return %d", bytes_to_copy);
  return bytes_to_copy;
}

static unsigned int psk_client_callback(SSL *ssl, const char *hint,
                                        char *identity,
                                        unsigned int max_identity_len,
                                        unsigned char *psk,
                                        unsigned int max_psk_len)
{
  LOG_TRACE("");
  unsigned int bytes_to_copy = 0;

  connection_handler_info_t *ch = SSL_get_ex_data(ssl, user_data_index);
  if (ch) {
    const char *client_identity = "Client_identity";
    assert(strlen(client_identity) < max_identity_len);
    strcpy(identity, client_identity);

    bytes_to_copy = (ch->psk_len < max_psk_len) ? ch->psk_len : max_psk_len;
    memcpy(psk, ch->psk, bytes_to_copy);
  }
  LOG_DEBUG("return %d", bytes_to_copy);
  return bytes_to_copy;
}

static void send_dtls(struct zconnection *connection,
                      const uint8_t *data,
                      uint16_t datalen)
{
  LOG_TRACE("");
  int errno_save = 0;
  int ssl_error_num = 0;
  int ssl_write_ret = 0;

  connection_handler_info_t *ch = connection->handler_info;
  if (ch == NULL || ch->is_running == false) {
    return;
  }

  errno = 0;
  ssl_write_ret = SSL_write(ch->ssl, data, datalen);
  errno_save = errno;

  ssl_error_num = SSL_get_error(ch->ssl, ssl_write_ret);
  switch (ssl_error_num) {
    case SSL_ERROR_NONE:
      LOG_TRACE("Wrote %d bytes", ssl_write_ret);
      break;
    case SSL_ERROR_WANT_WRITE:
      /* Can't write because of a renegotiation, so
       * we actually have to retry sending this message...
       */
      break;
    case SSL_ERROR_WANT_READ:
      /* continue with reading */
      break;
    case SSL_ERROR_SYSCALL:
      LOG_ERROR("Socket write error: ");
      if (!handle_socket_error(errno_save)) {
        goto cleanup;
      }
      break;
    case SSL_ERROR_SSL:
      {
        char errstr_buf[512];
        LOG_ERROR("SSL write error: %s (%d)",
                  ERR_error_string(ERR_get_error(), errstr_buf),
                  ssl_error_num);
        goto cleanup;
      }
      break;
    default:
      LOG_ERROR("Unexpected error while writing: %d", ssl_error_num);
      goto cleanup;
      break;
  }
  return;
cleanup:
  SSL_shutdown(ch->ssl);
}

static void send_udp(struct zconnection *connection,
                     const uint8_t *data,
                     uint16_t datalen)
{
  LOG_TRACE("");

  connection_handler_info_t *ch = connection->handler_info;

  if (ch && ch->is_running && ch->dgram_bio) {
    int written = BIO_write(ch->dgram_bio, data, datalen);
    if (written < 0) {
      ssldbg_print_errorstack();
    }
    LOG_DEBUG("send_udp: BIO_write(dgram) returned %d", written);
  }
}

static BIO* create_dgram_bio(int sock, int timeout_ms)
{
  LOG_TRACE("");

  /* After next call "sock" is owned by the bio and will be closed by BIO_free()
   * If the socket should not be closed by BIO_free() replace BIO_CLOSE with
   * BIO_NOCLOSE.
   */

  BIO* bio = BIO_new_dgram(sock, BIO_CLOSE);
  if (bio)
  {
    struct timeval timeout = {0};

    /* Uncomment next line to enable BIO debug */
    // BIO_set_callback(bio, BIO_debug_callback);

    /* Set and activate timeouts (currently same for send and receive) */
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    LOG_DEBUG("Setting RECV timeout for socket %d to %d ms", sock, timeout_ms);

    BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_RECV_TIMEOUT, 0, &timeout);
    BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_SEND_TIMEOUT, 0, &timeout);

    /* Want to do MTU discovery  */
    BIO_ctrl(bio, BIO_CTRL_DGRAM_MTU_DISCOVER, 0, NULL);
  } else {
    // Unable to create BIO object
    ssldbg_print_errorstack();
  }

  return bio; // Use BIO_free() after use
} 

/** Create a datagram socket
 *
 * Close the socket with BIO_closesocket() UNLESS:
 *
 * a) If the socket is used to create a bio with the close flag set, e.g.
 *    BIO_new_dgram(socket, BIO_CLOSE), then when using BIO_free(bio) the socket
 *    will be closed.
 *
 * b) If the bio mentioned under a) is assigned to a SSL object with
 *    SSL_set_bio(ssl, bio), then when using SSL_free(ssl) it will call
 *    BIO_free(bio) and thereby close the socket.
 */
static int create_socket(BIO_ADDR *ba, bool is_server)
{
  LOG_TRACE("");
  int sock = BIO_socket(BIO_ADDR_family(ba), SOCK_DGRAM, 0, 0);
  if (sock == INVALID_SOCKET) {
    ssldbg_print_errorstack();
    return INVALID_SOCKET;
  }

  if (is_server) {
    /* Create a listen socket (does setsockopt() and bind() but does not
     * actually call listen() since socket type is SOCK_DGRAM - this is what we
     * want) */
    if (BIO_listen(sock, ba, BIO_SOCK_REUSEADDR) != 1) {
      ssldbg_print_errorstack();
      BIO_closesocket(sock);
      return INVALID_SOCKET;
    }
  }

  return sock; 
}

static SSL* create_dtls_client_connection(void)
{
  LOG_TRACE("");
  SSL_CTX *client_ctx = SSL_CTX_new(DTLS_client_method());
  if (!client_ctx) {
    ssldbg_print_errorstack();
    LOG_ERROR("Error allocating DTLS client context");
    return NULL;
  }

  if (!SSL_CTX_set_cipher_list(client_ctx, dtls_cipher_list)) {
    ssldbg_print_errorstack();
    LOG_ERROR("Error setting %s as cipher", dtls_cipher_list);
    SSL_CTX_free(client_ctx);
    return NULL;
  }

  SSL_CTX_set_psk_client_callback(client_ctx, psk_client_callback);
  SSL_CTX_set_verify_depth(client_ctx, 2);
  SSL_CTX_set_read_ahead(client_ctx, 1);

  SSL *ssl = SSL_new(client_ctx);
  if (!ssl)
  {
    ssldbg_print_errorstack();
    LOG_ERROR("Error allocating DTLS client SSL object");
    SSL_CTX_free(client_ctx);
  }

  return ssl;
}

static SSL_CTX * create_dtls_server_context(void)
{
  LOG_TRACE("");
  SSL_CTX *server_ctx = SSL_CTX_new(DTLS_server_method());

  if (!server_ctx) {
    ssldbg_print_errorstack();
    LOG_ERROR("Error allocating DTLS server context");
    return NULL;
  }

  SSL_CTX_clear_mode(server_ctx, SSL_MODE_AUTO_RETRY);

  if (SSL_CTX_set_cipher_list(server_ctx, dtls_cipher_list) != 1) {
    ssldbg_print_errorstack();
    LOG_ERROR("Error setting %s as cipher", dtls_cipher_list);
    SSL_CTX_free(server_ctx);
    return NULL;
  }

  SSL_CTX_set_psk_server_callback(server_ctx, psk_server_callback);
  SSL_CTX_set_verify(server_ctx,
                      SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE,
                      dtls_verify_callback);
  /* The SSL_CTX_set_read_ahead() is maybe only needed on Ubuntu (see
    * https://groups.google.com/forum/#!topic/discuss-webrtc/TqZ9N0eTn24 as
    * mentioned in zipgw commit dd11f915c41da208971906a1a05a3247ef8d305a) */
  SSL_CTX_set_read_ahead(server_ctx, 1);

  /* Set DTLS cookie generation and verification callbacks */
  SSL_CTX_set_cookie_generate_cb(server_ctx, generate_cookie);
  SSL_CTX_set_cookie_verify_cb(server_ctx, verify_cookie);

  return server_ctx;
}

struct zconnection *zclient_start(const char *remote_address,
                                  uint16_t port,
                                  bool use_dtls,
                                  const uint8_t *psk,
                                  int psk_len,
                                  transfer_func_t handle_received_data_func)
{
  int  sock      = INVALID_SOCKET;
  BIO *dgram_bio = NULL;

  openssl_init();

  connection_handler_info_t *ch = conn_handler_info_new();

  if (!ch) {
    return NULL;
  }

  ch->connection.handler_info = ch;
  ch->connection.recv         = handle_received_data_func;
  ch->is_server               = false;
  ch->remote_addr             = BIO_ADDR_new();

  if (use_dtls) {
    ch->connection.send = send_dtls;
    memcpy(ch->psk, psk, psk_len);
    ch->psk_len = psk_len;
  } else {
    ch->connection.send = send_udp;
  }

  if (!str_to_bioaddr(ch->remote_addr, remote_address, port)) {
    goto error;
  }

  sock = create_socket(ch->remote_addr, false);
  if (sock == INVALID_SOCKET) {
    goto error;
  }

  /* Associate the socket with the remote address. Since socket type is
   * SOCK_DGRAM no network communication is actually happening here.
   *
   * NB: Testing reveals that both BIO_connect() and BIO_ctrl_set_connected()
   * are needed. Not sure why.
   */
  if (!BIO_connect(sock, ch->remote_addr, 0)) {
    ssldbg_print_errorstack();
    goto error;
  }

  dgram_bio = create_dgram_bio(sock, 2000);
  if (!dgram_bio) {
    goto error;
  }

  BIO_ctrl_set_connected(dgram_bio, &ch->remote_addr);

  if (use_dtls) {
    ch->ssl = create_dtls_client_connection();
    if (!ch->ssl) {
      goto error;
    }

    SSL_set_bio(ch->ssl, dgram_bio, dgram_bio);
    SSL_set_ex_data(ch->ssl, user_data_index, ch);

    /* Set SSL handshake to work in client mode */
    SSL_set_connect_state(ch->ssl);

    if (SSL_do_handshake(ch->ssl) != 1) {
      ssldbg_print_errorstack();
      LOG_ERROR("Client DTLS handshake failed");
      goto error;
    }

    if (libzw_log_check_severity(LOGLVL_TRACE)) {
      ssldbg_print_peer_certificate(ch->ssl);
    }

    if (!SSL_is_init_finished(ch->ssl)) {
      LOG_ERROR("Client DTLS session init not completed");
      goto error;
    }

    LOG_DEBUG("Negotiated DTLS version: %s", ssldbg_get_ssl_version_str(ch->ssl));
  } else {
    ch->dgram_bio = dgram_bio;
  }

  /* Create thread to process incoming data and keep connection open */
  LOG_DEBUG("Starting connection handler thread");
  if (start_connection_handler_thread(ch)) {
    /* The thread should keep running until stopped with zclient_stop() */
    return &ch->connection;
  } else {
    LOG_ERROR("Failed to start connection handler thread");
  }

error:
  // TODO the cleanup should be conditional
  // BIO_closesocket(sock);
  // SSL_free(ch->ssl);

  conn_handler_info_free(ch);
  return NULL;
}

void zclient_stop(struct zconnection *connection)
{
  LOG_DEBUG("");

  connection_handler_info_t *ch = connection->handler_info;
  
  /* Tell the thread to terminate */
  term_connection_handler_thread(ch);

  /* Wait here for the thread to exit */
  wait_for_connection_handler_thread(ch);

  /* The zconnection struct must not bee freed, it's a member of the ch object,
   * and will go away with it. Don't use *connection after calling
   * conn_handler_info_free(ch) */
  conn_handler_info_free(ch);

  LOG_DEBUG("Connection handler terminated");
}

static bool wait_for_dtls_handshake(connection_handler_info_t *ch)
{
  int res = 0;
  char buf[200] = {0};

  bioaddr_to_str(ch->local_addr, buf, sizeof(buf));
  LOG_INFO("Listening for DTLS handshake on: %s", buf);

  ch->remote_addr = BIO_ADDR_new();

  do {
    /* DTLSv1_listen() will perform the initial steps of the DTLS handshake.
     * First time it returns is most likely after receiving a Client Hello
     * (without cookie) and sending back a Client Verify Request Must then be
     * called again to wait for the follow-up Client Hello (with cookie) from
     * the client. When the handshake is verified is returns 1 to indicate that
     * the handshake has been accepted.
     *
     * The read timeout for the underlying datagram BIO (set with
     * create_dgram_bio()) controls how often DTLSv1_listen() will return (at
     * least). In those cases the return value will be zero, and we should
     * simply try again.
     */
    res = DTLSv1_listen(ch->ssl, ch->remote_addr);

    // A negative return code is a fatal error since OpenSSL 1.1.0
    if (res < 0) {
      ssldbg_print_errorstack(); 
      LOG_ERROR("DTLSv1_listen() returned fatal error %d. Aborting.", res);
      return false;
    }
    LOG_DEBUG("DTLSv1_listen() returned %d (%s)", res, (res > 0) ? "OK_CONTINUE" : "TRY_AGAIN");
  } while (res <= 0);

  char *hostname = BIO_ADDR_hostname_string(ch->remote_addr, 1);
  LOG_INFO("Connection received from: %s", hostname);
  OPENSSL_free(hostname);

  return true;
}

static bool do_dtls_handshake(SSL *ssl)
{
  int is_retryable = 0;
  int ret = 0;
  do {
    ret = SSL_accept(ssl);

    if (ret <= 0) {
      LOG_ERRNO(LOGLVL_ERROR, "SSL_accept() failed", errno);
      ssldbg_print_errorstack();

      int err = SSL_get_error(ssl, ret);

      LOG_DEBUG("SSL_get_error(ret=%d) returned %d", ret, err);

      /* If it's not a fatal error, it must be retryable */
      is_retryable = (err != SSL_ERROR_SSL)
                      && (err != SSL_ERROR_SYSCALL)
                      && (err != SSL_ERROR_ZERO_RETURN);
    }
  } while (ret <= 0 && is_retryable);

  return (ret == 1) ? true : false;
}


void zserver_start(char *local_address_str,
                   uint16_t port,
                   bool use_dtls,
                   const uint8_t *psk,
                   int psk_len,
                   transfer_func_t handle_received_data_func)
{
  int      listen_sock = INVALID_SOCKET;
  BIO     *dgram_bio   = NULL;
  SSL_CTX *server_ctx  = NULL;

  openssl_init();

  /* Wrap everything inside an infinite loop where we setup everything fresh and
   * listen for a DTLS connection and spawns off a (single) handler thread. The
   * loop repeats when the handler thread ends (typically when the DTLS session
   * is closed).
   *
   * The connection handler thread (currently) calls SSL_free() on ch->ssl. This
   * will basically de-allocate everything associated with the object (BIOs and
   * sockets). So we need to start over again. 
   *
   * NB: With OpenSSL 1.1.0 DTLSv1_listen() was apparently rewritten such that
   *     it only peeked at the file descriptor for the client hello. This
   *     inhibits the server design where the handler thread could be spawned
   *     off immediately when DTLSv1_listen() returned. See details here:
   *        https://github.com/openssl/openssl/issues/6934
   *        https://github.com/openssl/openssl/pull/7375 
   *
   *     A fix was released with OpenSSL 1.1.1a, but currently we still want to
   *     handle the "bug" by completing the DTLS session establishment fully here
   *     in the main thread before handing it over to the handler thread.
   *     (it might affect how quickly we can handle new DTLS client request, some
   *     further performance testing should be performed).
   */


  /* Create SSL context here - it will be reused for each new DTLS session */
  if (use_dtls) {
    server_ctx = create_dtls_server_context();
    if (!server_ctx) {
      LOG_ERROR("Error creating DTLS server CTX object");
      goto error;
    }
  }

  while (1) {
    LOG_INFO("---------- STARTING NEW %s SERVER SESSION ----------", use_dtls ? "DTLS" : "UDP");

    connection_handler_info_t *ch = conn_handler_info_new();

    if (!ch) {
      goto error;
    }

    ch->connection.handler_info = ch;
    ch->connection.recv         = handle_received_data_func;
    ch->is_server               = true;
    ch->local_addr              = BIO_ADDR_new();

    if (use_dtls) {
      ch->connection.send = send_dtls;
      memcpy(ch->psk, psk, psk_len);
      ch->psk_len = psk_len;
    } else {
      ch->connection.send = send_udp;
    }

    if (!str_to_bioaddr(ch->local_addr, local_address_str, port)) {
      goto error;
    }

    listen_sock = create_socket(ch->local_addr, true);
    LOG_DEBUG("listen_sock = %d", listen_sock);
    if (listen_sock  == INVALID_SOCKET) {
      goto error;
    }

    dgram_bio = create_dgram_bio(listen_sock, 5000);
    if (!dgram_bio) {
      goto error;
    }

    if (use_dtls) {
      ch->ssl = SSL_new(server_ctx);
      if (!ch->ssl)
      {
        ssldbg_print_errorstack();
        LOG_ERROR("Could not allocate DTLS server SSL object");
        goto error;
      }

      SSL_set_bio(ch->ssl, dgram_bio, dgram_bio);
      SSL_set_ex_data(ch->ssl, user_data_index, ch);
      SSL_set_options(ch->ssl, SSL_OP_COOKIE_EXCHANGE);

      if (libzw_log_check_severity(LOGLVL_TRACEX)) {
        BIO_set_callback(dgram_bio, ssldbg_bio_dump_callback);
        SSL_set_info_callback(ch->ssl, ssldbg_ssl_info_callback);
        // SSL_set_msg_callback(ch->ssl, SSL_trace);
      }

      /* Set SSL handshake to work in server mode */
      SSL_set_accept_state(ch->ssl);

      /* Wait until we receive a valid handshake from a client (will set
       * ch->remote_addr) */
      if (!wait_for_dtls_handshake(ch)) {
        goto error;
      }

      if (BIO_ADDR_family(ch->remote_addr) != BIO_ADDR_family(ch->local_addr)) {
        LOG_ERROR("Local and remote address not same address family");
        goto error;
      }

      if (!BIO_connect(listen_sock, ch->remote_addr, 0)) {
        ssldbg_print_errorstack();
        LOG_ERROR("Error connecting to remote address");
        goto error;
      }

      /* We've connected the socket, now also set datagram state to connected */
      BIO_ctrl_set_connected(SSL_get_rbio(ch->ssl), ch->remote_addr);

      // Perform/complete the DTLS handshake
      if (!do_dtls_handshake(ch->ssl)) {
        ssldbg_print_errorstack();
        LOG_ERROR("DTLS handshake failed");
        goto error;
      }

      if (libzw_log_check_severity(LOGLVL_TRACE)) {
        ssldbg_print_peer_certificate(ch->ssl);
      }

      if (!SSL_is_init_finished(ch->ssl)) {
        LOG_ERROR("Server DTLS session init not completed");
        goto error;
      }

      LOG_INFO("Negotiated DTLS version: %s", ssldbg_get_ssl_version_str(ch->ssl));
    } else {
      ch->dgram_bio = dgram_bio;
      /* For non-DTLS connections we don't do listen or accept. Instead we are
       * doing recvfrom() (via BIO_read()) in the connection thread. */
    }

    /* Create thread to process incoming data. Ownership of ch (including
     * listen_sock) is transferred to the thread. */
    LOG_DEBUG("Starting connection handler thread");
    if (!start_connection_handler_thread(ch)) {
      LOG_ERROR("Failed to start connection handler thread");
      goto error;
    }
  } // while(1)

error:
  LOG_ERROR("Error in zserver_start()");
  if (server_ctx) {
    SSL_CTX_free(server_ctx);
  }
}


