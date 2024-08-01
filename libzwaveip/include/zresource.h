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

#ifndef ZRESOURCE_H_
#define ZRESOURCE_H_

#include <sys/types.h>
#include <sys/socket.h>

#include <stdint.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
/**
 * @defgroup zresource Z/IP resource discovery (mDNS)
 *
 * This module implements a mDNS listener, which listens for Z/IP services on
 * the local network.
 *
 * Start the mDNS listener by calling \ref
 * zresource_mdns_thread_func. After the mDNS listener is started, the application
 * may iterate to the list of resources starting with \ref zresource_services.
 *
 * \see SDS11633
 *
 * @{
 */

/// The node is deleted from the network, and will soon be removed from the resource list
#define MODE_FLAGS_DELETED 0x01
/// The node is failing
#define MODE_FLAGS_FAILED  0x02
/// The node has reported to be low on battery
#define MODE_FLAGS_LOWBAT  0x04

/**
 * Describing the communication mode of the node
 */
typedef enum {
  MODE_PROBING = 0x00,              //!< Node has not yet been fully probed
  MODE_NONLISTENING = 0x01,         //!< Node is a non-listening node
  MODE_ALWAYSLISTENING = 0x02,      //!< Node is an always listening node
  MODE_FREQUENTLYLISTENING = 0x03,  //!< Node is a Flirs node
  MODE_MAILBOX = 0x04,  //!< Node is a non-listening node which supports the wakeup
                        // command class
} node_mode_t;

typedef enum {
  NON_SECURE_CC = 0,
  SECURE_CC
} cc_type_t;

typedef struct {
  uint8_t cc;      //<! Command Class number
  cc_type_t type;  //<! Command class type (secure / non-secure)
  uint8_t version; //<! Command class version. 0 if unknown.
} cc_ver_info_t;

/**
 * Structure holding information about a zip service.
 * See SDS11633 for further details
 */
struct zip_service
{
  struct zip_service* next; //!< pointer to next Z/IP service in the list
  char* service_name;  //!< Name of the zip service as received from Z/IP gateway
  char* friendly_name; //!< Name of the zip service to display to a user (derived from service_name)
  char* host_name;     //!< Hostname of the resource containing this service
  uint8_t *info;       //!< Array of supported command classes.
  int infolen;         //!< Length of the info array
  cc_ver_info_t *cc_info; //<! Command class version information
  int cc_info_count;      //<! Number of elements in the cc_info array
  uint8_t *aggregated; //!< If this is an aggregated endpoint, which endpoints is this service an aggregation of.
  int aggregatedlen;   //!< Length of the aggregated endpoints
  int epid;            //!< Endpoint id of service
  node_mode_t comm_mode;    //!< Communication mode (how to reach the node)
  int oper_flags;           //!< Operational mode bitmask. The following bit flags can be used: \ref MODE_FLAGS_DELETED, \ref MODE_FLAGS_FAILED or \ref MODE_FLAGS_LOWBAT

  uint16_t manufacturerID; //!< The manufacturer id
  uint16_t productType;    //!< The product type
  uint16_t productID;      //!< The product ID

  int securityClasses;        //!< Bitmask of the active security classes
  uint16_t installer_iconID;  //!< ID of the icon show for installers
  uint16_t user_iconID;       //!< ID of the icon show for users

  struct sockaddr_storage ss_addr; //!< IPv4 or IPv6 address of the resource
};

/**
 * Look up a zip_service by its "friendly" name.
 *
 * @param friendlyName The friendly name of the service (as displayed by the
 *                     "list" command and by command completion).
 *
 * @return Pointer to the matching zip_service. NULL if not found.
 */
struct zip_service* find_service_by_friendly_name(const char* friendlyName);

/**
 * Get the IP address of a zip_service.
 *
 * @param s Pointer to the zip_service.
 * @param buf Buffer to write the IP address string into.
 * @param len Length of buffer (in bytes)
 *
 * @return pointer to generated address string (in buffer). NULL if invalid IP address.
 */
const char * zresource_get_ip_str(const struct zip_service *s, char *buf, size_t len);

/**
 * Look up a zip_service having a specific IP address.
 *
 * @param ip the IP address as a string
 * @return Pointer to the matching zip_service. NULL if not found.
 */
struct zip_service * zresource_find_service_by_ip_str(const char *ip);

/**
 * Get Z-Wave home ID  of the zip service.
 *
 * @param s Pointer to the zip_service.
 * @param buf Buffer to write the home ID string to.
 * @param len Length of buffer (in bytes)
 * @return Length of home ID
 */
size_t zresource_get_homeid(const struct zip_service *s, char *homeid_buf, size_t homeid_buf_len);

/**
 * Progressively give the next item in a list of zip_services matching the
 * parameters.
 *
 * Set idx to 0 on first call to generate the internal (static) list of matching
 * zip_services and return the first item in the list. Then, continue to
 * increment idx and call again until NULL is returned.
 *
 * For example, this will iterate all known zip_services:
 * @code{.c}
 * for (i = 0; s = zresource_list_matching_services(0, 0, i); i++) {
 *   // use s
 * }
 * @endcode
 *
 * @param matchstr Only include services having friendly names beginning with
 *                 this (sub)string. If NULL or empty string, no filtering on
 *                 friendly name is performed (i.e., all will match).
 * @param homeid   Only include services having this homeid. If NULL or empty
 *                 string, no filtering on homeid is performed.
 * @param idx      Request item at index=idx in list of matching zip services.
 *                 First, call MUST set idx=0 to generate the result list based
 *                 on matchstr and homeid.
 *
 * @return Pointer to the next zip_service in the list. NULL if no more items.
 */
struct zip_service* zresource_list_matching_services(const char *matchstr,
                                                     const char *homeid,
                                                     int idx);

/**
 * Check if a zip service supports a specific command class.
 *
 * Uses the NIF previously received via mDNS (i.e., no messages are sent or
 * received by this).
 *
 * @param s Pointer to zip service.
 * @param cc The command class number to check.
 * @return 1 if supported, 0 otherwise.
 */
int zresource_is_cc_supported(struct zip_service *s, uint8_t cc);

/**
 * Look up a command class version info struct for a zip_service.
 *
 * @param zip_service zip_service to query.
 * @param cc The command class number.
 * @return Pointer to the command class version struct. NULL if the command class is
 *                 not supported by the zip_service
 */
cc_ver_info_t* zresource_get_cc_info(struct zip_service *s, uint8_t cc);

/**
 * Set the version of a supported command class for a zip_service.
 *
 * @param zip_service zip_service to modify
 * @param cc The command class number
 * @param versino The command class version
 * @return Pointer to the modified command class version struct. NULL if the command
 *                 class is not supported by the zip_service
 */
cc_ver_info_t* zresource_set_cc_version(struct zip_service *s, uint8_t cc, uint8_t version);

/**
 * Thread function of the mdns listner. This is the main loop of the mdns
 * thread and should normally be run in a thread. For example,
 *
 * @code{.c}
 *   pthread_t mdns_thread;
 *   pthread_create(&mdns_thread,0,&zresource_mdns_thread_func,0);
 * @endcode
 */
void* zresource_mdns_thread_func(void*);

/**
 * @}
 */
#endif /* ZRESOURCE_H_ */
