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

#include "ssldbg_utils.h"
#include "libzw_log.h"

#include <stdbool.h>
#include <sys/socket.h>
#include <openssl/err.h>
#include <openssl/opensslv.h>

static bool initialized = false;

static BIO *bio_stdin;
static BIO *bio_stdout;
static BIO *bio_stderr;

void ssldbg_utils_init(void) {
  if (!initialized) {
    ssldbg_show_openssl_versions();

    bio_stdin  = BIO_new_fp(stdin, BIO_NOCLOSE | BIO_FP_TEXT);
    bio_stdout = BIO_new_fp(stdout, BIO_NOCLOSE | BIO_FP_TEXT);
    bio_stderr = BIO_new_fp(stderr, BIO_NOCLOSE | BIO_FP_TEXT);
    initialized = true;
  }
}

void ssldbg_print_errorstack(void)
{
  if (bio_stderr) {
    ERR_print_errors(bio_stderr);
  }
}

void ssldbg_print_peer_certificate(const SSL *ssl)
{
  if (!libzw_log_check_severity(LOGLVL_TRACE)) {
    return;
  }

  X509 *cert = SSL_get_peer_certificate(ssl);
  if (cert) {
    LOG_TRACE("------------------------------------------------------------");
    X509_NAME_print_ex_fp(stderr,
                          X509_get_subject_name(cert),
                          1, XN_FLAG_MULTILINE);
    LOG_TRACE("\n\n Cipher: %s", SSL_CIPHER_get_name(SSL_get_current_cipher(ssl)));
    LOG_TRACE("------------------------------------------------------------");
  }
}

const char * ssldbg_get_ssl_version_str(const SSL *ssl)
{
  int ver = SSL_version(ssl);
  switch (ver) {
    case DTLS1_VERSION:
      return "DTLS 1.0";
      break;
    case DTLS1_2_VERSION:
      return "DTLS 1.2";
      break;
    default:
      return "(unknown)";
      break;
  }
}

static const char * ssldbg_openssl_ver2str(uint32_t v)
{
  static char verstr[100] = {0};
  char suffix[20] = {0};

  uint8_t major  = (v & 0xf0000000) >> 28;
  uint8_t minor  = (v & 0x0ff00000) >> 20;
  uint8_t fix    = (v & 0x000ff000) >> 12;
  uint8_t patch  = (v & 0x00000ff0) >> 4;
  uint8_t status = (v & 0x0000000f);

  if (status == 0) {
    sprintf(suffix, "-dev");
  } else if (status >= 1 && status <= 14) {
    sprintf(suffix, "-beta%u", status);
  } else if (status == 15 && patch > 0) {
    sprintf(suffix, "%c", 'a' + patch - 1);
  }

  sprintf(verstr, "OpenSSL %u.%u.%u%s", major, minor, fix, suffix);
  return verstr;
}

void ssldbg_show_openssl_versions(void)
{
  LOG_DEBUG("Compiled against: %s", OPENSSL_VERSION_TEXT);
  // Next one should produce the same as OPENSSL_VERSION_TEXT (except for a date string)
  // LOG_DEBUG("Compiled against: %s", ssldbg_openssl_ver2str(OPENSSL_VERSION_NUMBER));
  LOG_DEBUG("Linked against  : %s", ssldbg_openssl_ver2str(OpenSSL_version_num()));
}

/* Next one is "borrowed" from OpenSSL s_server (s_cb.c) */ 
long ssldbg_bio_dump_callback(BIO *bio, int cmd, const char *argp,
                              int argi, long argl, long ret)
{
  BIO *out = bio_stderr;

  if (out == NULL || !libzw_log_check_severity(LOGLVL_TRACEX)) {
    return ret;
  }

  if (cmd == (BIO_CB_READ | BIO_CB_RETURN)) {
    LOG_TRACEX("BIO %p READ [%p] (%lu bytes, returned %ld (0x%lX))",
               (void *)bio, (void *)argp, (unsigned long)argi, ret, ret);
    BIO_dump(out, argp, (int)ret);
    return ret;
  } else if (cmd == (BIO_CB_WRITE | BIO_CB_RETURN)) {
    LOG_TRACEX("BIO %p WRITE [%p] (%lu bytes, returned %ld (0x%lX))",
               (void *)bio, (void *)argp, (unsigned long)argi, ret, ret);
    BIO_dump(out, argp, (int)ret);
  } else if (cmd == (BIO_CB_CTRL | BIO_CB_RETURN)) {
    if (argi == BIO_C_SET_FD) {
      LOG_TRACEX("BIO %p CTRL [fd=%d] (BIO_C_SET_FD returned %ld (0x%lX))",
                 (void *)bio, *((int *)argp), ret, ret);
    } else if (argi == BIO_CTRL_DGRAM_SET_PEEK_MODE) {
      LOG_TRACEX("BIO %p CTRL [%p] (BIO_CTRL_DGRAM_SET_PEEK_MODE=%ld returned %ld (0x%lX))",
                 (void *)bio, (void *)argp, argl, ret, ret);
    } else {
      const char *cmd_str = "";
      switch (argi) {
        case BIO_CTRL_PUSH:                     cmd_str = "BIO_CTRL_PUSH"; break;
        case BIO_CTRL_POP:                      cmd_str = "BIO_CTRL_POP"; break;
        case BIO_CTRL_FLUSH:                    cmd_str = "BIO_CTRL_FLUSH"; break;
        case BIO_CTRL_DGRAM_CONNECT:            cmd_str = "BIO_CTRL_DGRAM_CONNECT"; break;
        case BIO_CTRL_DGRAM_SET_CONNECTED:      cmd_str = "BIO_CTRL_DGRAM_SET_CONNECTED"; break;
        case BIO_CTRL_DGRAM_SET_RECV_TIMEOUT:   cmd_str = "BIO_CTRL_DGRAM_SET_RECV_TIMEOUT"; break;
        case BIO_CTRL_DGRAM_GET_RECV_TIMEOUT:   cmd_str = "BIO_CTRL_DGRAM_GET_RECV_TIMEOUT"; break;
        case BIO_CTRL_DGRAM_SET_SEND_TIMEOUT:   cmd_str = "BIO_CTRL_DGRAM_SET_SEND_TIMEOUT"; break;
        case BIO_CTRL_DGRAM_GET_SEND_TIMEOUT:   cmd_str = "BIO_CTRL_DGRAM_GET_SEND_TIMEOUT"; break;
        case BIO_CTRL_DGRAM_GET_RECV_TIMER_EXP: cmd_str = "BIO_CTRL_DGRAM_GET_RECV_TIMER_EXP"; break;
        case BIO_CTRL_DGRAM_GET_SEND_TIMER_EXP: cmd_str = "BIO_CTRL_DGRAM_GET_SEND_TIMER_EXP"; break;
        case BIO_CTRL_DGRAM_GET_PEER:           cmd_str = "BIO_CTRL_DGRAM_GET_PEER"; break;
        case BIO_CTRL_DGRAM_SET_PEER:           cmd_str = "BIO_CTRL_DGRAM_SET_PEER"; break;
        case BIO_CTRL_WPENDING:                 cmd_str = "BIO_CTRL_WPENDING"; break;
        case BIO_CTRL_DGRAM_QUERY_MTU:          cmd_str = "BIO_CTRL_DGRAM_QUERY_MTU"; break;
        case BIO_CTRL_DGRAM_SET_NEXT_TIMEOUT:   cmd_str = "BIO_CTRL_DGRAM_SET_NEXT_TIMEOUT"; break;
        case BIO_CTRL_DGRAM_GET_MTU_OVERHEAD:   cmd_str = "BIO_CTRL_DGRAM_GET_MTU_OVERHEAD"; break;
        case BIO_C_GET_FD:                      cmd_str = "BIO_C_GET_FD"; break;

        default: break;
      }
      LOG_TRACEX("BIO %p CTRL [%p] (BIO CMD %lu %s returned %ld (0x%lX))",
                 (void *)bio, (void *)argp, (unsigned long)argi, cmd_str, ret, ret);
    }
  }
  return ret;
}

void ssldbg_ssl_info_callback(const SSL *s, int where, int ret)
{
  const char *str;
  int w;

  if (!libzw_log_check_severity(LOGLVL_TRACEX)) {
    return;
  }

  w = where & ~SSL_ST_MASK;

  if (w & SSL_ST_CONNECT) {
    str = "SSL_connect";
  } else if (w & SSL_ST_ACCEPT) {
    str = "SSL_accept";
  } else {
    str = "undefined";
  }

  if (where & SSL_CB_LOOP) {
    LOG_TRACEX("## %s:%s", str, SSL_state_string_long(s));
  } else if (where & SSL_CB_ALERT) {
    str = (where & SSL_CB_READ) ? "read" : "write";
    LOG_TRACEX("## SSL3 alert %s:%s:%s",
               str,
               SSL_alert_type_string_long(ret),
               SSL_alert_desc_string_long(ret));
  } else if (where & SSL_CB_EXIT) {
    if (ret == 0) {
      LOG_TRACEX("## %s:failed in %s",
                 str, SSL_state_string_long(s));
    } else if (ret < 0) {
      LOG_TRACEX("## %s:error in %s",
                 str, SSL_state_string_long(s));
    }
  }
}

void ssldbg_print_elapsed_time(void)
{
  static bool first = true;
  static struct timespec prev = {0};

  if (!libzw_log_check_severity(LOGLVL_DEBUG)) {
    return;
  }

  if (first) {
    first = false;
    clock_gettime(CLOCK_MONOTONIC_RAW, &prev);
  } else {
    struct timespec now = {0};
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    uint64_t delta_us = (now.tv_sec - prev.tv_sec) * 1000000 + (now.tv_nsec - prev.tv_nsec) / 1000;
    uint32_t delta_s = delta_us / 1000000;
    uint32_t delta_ms = (delta_us - delta_s * 1000000) / 1000;
    LOG_DEBUG("delta=%d.%03ds", delta_s, delta_ms);
    prev = now;
  }
}

bool ssldbg_is_socket_valid(int s) {
  int optval = 0;
  socklen_t optlen = sizeof(optval);
  if (getsockopt(s, SOL_SOCKET, SO_TYPE, &optval, &optlen) != 0) {
    return false;
  } else {
    return true;
  }
}
