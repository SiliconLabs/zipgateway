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

#ifndef LIBZWAVEIP_H_
#define LIBZWAVEIP_H_

#include <stdbool.h>
#include "zconnection.h"

/**
 * @defgroup zwaveip Z-Wave for IP Library API
 *
 * Server and client implementation using OpenSSL.
 *
 * This module implements a server and client to use with Z-Wave for
 * IP. Both DTLS and UDP connections are supported.
 *
 * @{
 */

/**
 * The standard Z/IP gateway port number for DTLS Z/IP sessions.
 */
#define ZGW_DTLS_PORT 41230

/**
 * The standard Z/IP gateway port number for UDP Z/IP sessions.
 */
#define ZGW_UDP_PORT 4123

/**
 * Maximum length of a pre-shared key (in bytes) allowed
 *
 * According to RFC4279, arbitrary PSKs up to 64 octets in length must be
 * supported.
 */
#define MAX_PSK_LEN 64

/**
 * Start an DTLS or UDP listening socket. This function will block forever.
 *
 * A new thread will be spawned for each client connection. Currently, only one
 * client connection is supported at any given time.
 *
 * @param local_address The local address of the listening
 * socket. If a specific interface is needed, provide the IPv4 or IPv6
 * address of that interface.
 * @param port The local port number the socket will listen on.
 * @param use_dtls Set to true to use DTLS.
 * @param psk The pre-shared key, which this DTLS session uses. Ignored if
 *        use_dtls is false.
 * @param psk_len The length of the PSK used. Ignored if use_dtls is false.
 * @param handle_received_data_func A callback function to call when an incoming
 *        package is received.
 */
void zserver_start(char* local_address, uint16_t port,
                   bool use_dtls,
                   const uint8_t* psk, int psk_len,
                   transfer_func_t handle_received_data_func);

/**
 * Open up a Z/IP connection to a remote socket.
 *
 * This function will spawn a new thread.
 *
 * @param remote_address The address of the remote  Z/IP service.
 * @param port port of the remote service. In general, this should be \ref
 * ZGW_DTLS_PORT (if use_dtls is true) or \ref ZGW_UDP_PORT (if use_dtls is
 * false).
 * @param use_dtls Set to true to connect using DTLS.
 * @param psk The pre-shared key which this DTLS session uses. Ignored if
 *            use_dtls is false.
 * @param psk_len The length of the PSK used. Ignored if use_dtls is false.
 * @param handle_received_data_func A callback function to call when an incoming
 *        package is received.
 * @return a handle to the connection. see zconnection.h
 */
struct zconnection* zclient_start(const char* remote_address, uint16_t port,
                                  bool use_dtls,
                                  const uint8_t* psk, int psk_len,
                                  transfer_func_t handle_received_data_func
                                  );

/**
 * Stop a Z/IP client thread and free associated resources.
 *
 *@param handle Handle to the connection.
 */
void zclient_stop(struct zconnection* handle);

/**
 * @}
 */

#endif /* LIBZWAVEIP_H_ */
