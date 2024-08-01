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

#ifndef LIBZWAVEIP_SSLDBG_UTILS_H
#define LIBZWAVEIP_SSLDBG_UTILS_H

#include <stdbool.h>
#include <openssl/ssl.h>
#include <openssl/bio.h>

void ssldbg_utils_init(void);

void ssldbg_print_errorstack(void);

void ssldbg_print_peer_certificate(const SSL *ssl);

const char * ssldbg_get_ssl_version_str(const SSL *ssl);

void ssldbg_show_openssl_versions(void);

long ssldbg_bio_dump_callback(BIO *bio, int cmd, const char *argp,
                              int argi, long argl, long ret);

void ssldbg_ssl_info_callback(const SSL *s, int where, int ret);

void ssldbg_print_elapsed_time(void);

bool ssldbg_is_socket_valid(int s);

#endif // LIBZWAVEIP_SSLDBG_UTILS_H