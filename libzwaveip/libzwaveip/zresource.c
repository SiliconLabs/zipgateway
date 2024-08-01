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
#include <string.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <pthread.h>

#include "zresource.h"
#include "zresource-internal.h"
#include "libzw_log.h"

#define MAX_LIST_SERVICES 240  // Max services that can be handled with zresource_list_friendly_names()
// TODO Next one is also in zw_classcmd.h - do we want to include it here?
#define COMMAND_CLASS_SECURITY_SCHEME0_MARK 0xF100

/* We must protect the list of zresource services by a mutex. The list can be
 * iterated from the main UI thread at the same time the mDNS (Avahi) thread
 * tries to add or remove elements to/from the list. */
static pthread_mutex_t zservice_list_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct zip_service* zresource_services;

struct zip_service* find_service_by_service_name(const char* name) {
  struct zip_service *s = NULL;
  pthread_mutex_lock(&zservice_list_mutex);
  for (s = zresource_services; s != NULL; s = s->next) {
    if (strcmp(name, s->service_name) == 0) {
      break;
    }
  }
  pthread_mutex_unlock(&zservice_list_mutex);
  return s;
}

struct zip_service* find_service_by_friendly_name(const char* name) {
  struct zip_service *s = NULL;
  pthread_mutex_lock(&zservice_list_mutex);
  for (s = zresource_services; s != NULL; s = s->next) {
    if (strcmp(name, s->friendly_name) == 0) {
      break;
    }
  }
  pthread_mutex_unlock(&zservice_list_mutex);
  return s;
}

static struct zip_service* find_service_by_ip_addr(const struct sockaddr_storage *ss_addr) {
  struct zip_service *s = NULL;
  pthread_mutex_lock(&zservice_list_mutex);
  for (s = zresource_services; s != NULL; s = s->next) {
    if (s->ss_addr.ss_family == ss_addr->ss_family) {
      int res = 1;
      if (ss_addr->ss_family == AF_INET) {
        res = memcmp(&((struct sockaddr_in*)&s->ss_addr)->sin_addr,
                     &((struct sockaddr_in*)ss_addr)->sin_addr,
                     sizeof(struct in_addr));
      } else if (ss_addr->ss_family == AF_INET6) {
        res = memcmp(&((struct sockaddr_in6*)&s->ss_addr)->sin6_addr,
                     &((struct sockaddr_in6*)ss_addr)->sin6_addr,
                     sizeof(struct in6_addr));

      }
      if (res == 0) {
        break;
      }
    } else {
      /* Compare IPv6 and IPv4 - Can we? Should we?
       *
       * This function is usually used to lookup the service record for the
       * gateway. But if we have connected to the gateway with IPv6 (to the
       * address configured as ZipLanIp6 in zipgateway.cfg) then that address
       * will not in any way be related to the IPv4 addresses stored in
       * zip_service (the zip_service is based on mDNS announcements received
       * with IPv4). We can compare IPv6 encapsulated IPv4 addresses to plain
       * IPv4 addresses, but in the actual use case just described the IPv6
       * address of the gateway will typically not contain an encapsulated IPv4
       * address.
       */
    }
  }
  pthread_mutex_unlock(&zservice_list_mutex);
  return s;
}

static sa_family_t str_to_ip(const char *str_addr, struct sockaddr_storage *ss_addr) {
  memset(ss_addr, 0, sizeof(struct sockaddr_storage));

  if (inet_pton(AF_INET, str_addr, &((struct sockaddr_in*)ss_addr)->sin_addr) == 1) {
    ss_addr->ss_family = AF_INET;
  } else if (inet_pton(AF_INET6, str_addr, &((struct sockaddr_in6*)ss_addr)->sin6_addr) == 1) {
    ss_addr->ss_family = AF_INET6;
  }
  return ss_addr->ss_family;
}

struct zip_service * zresource_find_service_by_ip_str(const char *ip) {
  struct zip_service *s = NULL;
  struct sockaddr_storage ss_gateway = {0};

  if (str_to_ip(ip, &ss_gateway)) {
    s = find_service_by_ip_addr(&ss_gateway);
  }
  return s;
}

// Compare function for qsort() (used in zresource_list_matching_services())
static int qs_service_cmp(const void *s1, const void *s2) {
  // The "void *" arguments are pointers to an array of struct zip_service pointers
  const struct zip_service *zs1 = *((const struct zip_service **)s1);
  const struct zip_service *zs2 = *((const struct zip_service **)s2);
  // Sort by host name (includes node id), then by end point
  int hostcmp = strcmp(zs1->host_name, zs2->host_name);
  if (hostcmp == 0) {
    return zs1->epid - zs2->epid;
  } else {
    return hostcmp;
  }
}

struct zip_service* zresource_list_matching_services(const char *matchstr, const char *homeid, int idx) {

  /* Static array of pointers for qsort() - NB: This breaks thread safety for
   * this function, use only from one thread! */
  static struct zip_service* services[MAX_LIST_SERVICES] = {0};

  // First call in a sequence is expected to use idx=0
  if (idx == 0) {
    int service_count = 0;
    int matchstr_len = 0;

    memset(services, 0, sizeof(services));

    if (matchstr) {
      matchstr_len = strlen(matchstr);
    }

    // Fill array of pointers for qsort
    int add_service = 1;

    pthread_mutex_lock(&zservice_list_mutex);
    for (struct zip_service *s = zresource_services;
         s != NULL && service_count < MAX_LIST_SERVICES;
         s = s->next) {
      if (matchstr_len == 0 || strncmp(s->friendly_name, matchstr, matchstr_len) == 0) {
        add_service = 1;

        // Optionally filter out services that have another homeid
        if (homeid && strlen(homeid) > 0) {
          char this_homeid[20] = {0};
          zresource_get_homeid(s, this_homeid, sizeof(this_homeid));
          if (strcmp(homeid, this_homeid) != 0) {
            // HomeID did not match - don't add this one to the list
            add_service = 0;
          }
        }

        if (add_service) {
          services[service_count++] = s;
        }
      }
    }
    pthread_mutex_unlock(&zservice_list_mutex);

    // Sort it
    qsort(&services, service_count, sizeof(services[0]), qs_service_cmp);
  }

  /* TODO: This is not fully thread safe. One of the services in the "services"
   * array could have been deallocated by zresource_remove_service() since the
   * array was created (when called with idx=0) */
  if (idx < MAX_LIST_SERVICES) {
    return services[idx];
  } else {
    return NULL;
  }
}

cc_ver_info_t* zresource_get_cc_info(struct zip_service *s, uint8_t cc) {
  for (int i = 0; i < s->cc_info_count; i++) {
    if (s->cc_info[i].cc == cc) {
      return &s->cc_info[i];
    }
  }
  return NULL; // Not found
}

cc_ver_info_t* zresource_set_cc_version(struct zip_service *s, uint8_t cc, uint8_t version) {
  cc_ver_info_t *cc_info = zresource_get_cc_info(s, cc);
  if (cc_info) {
    cc_info->version = version;
    return cc_info;
  }
  return NULL; // Not found
}

static int zresource_update_cc_info(struct zip_service *s) {
  int in_secure_section = 0;

  /* Will allocate more than required because s->infolen includes
   * generic and specific device class values plus various markers.
   * We're not *that* limited on memory so we ignore that detail.
   */
  cc_ver_info_t *cc_info_array = calloc(s->infolen, sizeof(cc_ver_info_t));
  int cc_info_count = 0;

  // Skip first two bytes containing generic and specific device class
  for (int i = 2; i < s->infolen; i++) {
    // Check for COMMAND_CLASS_SECURITY_SCHEME0_MARK
    if ((i + 1) < s->infolen) {
      uint16_t marker = (s->info[i] << 8) + s->info[i + 1];
      if (marker == COMMAND_CLASS_SECURITY_SCHEME0_MARK) {
        in_secure_section = 1;
        i++;  // Since mark consist of two chars we "eat" the next one too
        continue;
      }
    }

    /* At this point s->info[i] is a command class identifier */

    /* Re-use CC version information if we have it */
    cc_ver_info_t *cc_info = zresource_get_cc_info(s, s->info[i]);
    if (cc_info) {
      cc_info_array[cc_info_count].version = cc_info->version;
    }
    cc_info_array[cc_info_count].cc = s->info[i];
    cc_info_array[cc_info_count].type = (in_secure_section) ? SECURE_CC : NON_SECURE_CC;
    cc_info_count++;
  }

  /* Now cc_info_array is filled from the arrays s->info and s->cc_info.
   * Replace s->cc_info with cc_info_array.
   */
  free(s->cc_info);
  s->cc_info = cc_info_array;
  s->cc_info_count = cc_info_count;

  return cc_info_count;
}

int zresource_is_cc_supported(struct zip_service *s, uint8_t cc) {
  LOG_DEBUG("Checking for support of 0x%02X", cc);
  const cc_ver_info_t *cc_info = zresource_get_cc_info(s, cc);
  return (cc_info) ? 1 : 0;
}
  
static void free_service(struct zip_service* s) {
  /* TODO: The service should probably not be deallocated. A pointer to the
   * struct could already have been handed out to some other functions, e.g. via
   * zresource_list_matching_services(). */
  if (s->info) free(s->info);
  if (s->cc_info) free(s->cc_info);
  if (s->aggregated) free(s->aggregated);
  if (s->service_name) free(s->service_name);
  if (s->friendly_name) free(s->friendly_name);
  free(s);
}

void zresource_remove_service(const char *service_name) {
  struct zip_service *last = 0;
  struct zip_service *s = 0;

  pthread_mutex_lock(&zservice_list_mutex);
  for (s = zresource_services; s != NULL; s = s->next) {
    if (strcmp(service_name, s->service_name) == 0) {
      if (s == zresource_services) {
        zresource_services = s->next;
      } else {
        last->next = s->next;
      }
      free_service(s);
      break;
    }
    last = s;
  }
  pthread_mutex_unlock(&zservice_list_mutex);
}

static struct zip_service * zresource_get_root_service(struct zip_service *s) {
  if (s->epid == 0 || s->host_name == 0) {
    return s;
  }
  struct zip_service *s_root = NULL;
  pthread_mutex_lock(&zservice_list_mutex);
  for (struct zip_service *si = zresource_services; si && si->host_name; si = si->next) {
    if (si->epid == 0 && strcmp(s->host_name, si->host_name) == 0) {
      s_root = si;
      break;
    }
  }
  pthread_mutex_unlock(&zservice_list_mutex);
  return s_root;
}

static int zresource_count_services_on_same_host(struct zip_service *s) {
  int count = 0;
  if (s->host_name) {
    pthread_mutex_lock(&zservice_list_mutex);
    for (struct zip_service *si = zresource_services; si && si->host_name; si = si->next) {
      if (strcmp(s->host_name, si->host_name) == 0) {
        count++;
      }
    }
    pthread_mutex_unlock(&zservice_list_mutex);
  }
  return count;
}

/*
 * Extract the type (e.g. "Switch Binary") from the service name
 * 
 * The "raw" service name from Z/IP gateway is (by default) generated as:
 *   printf("%s [%08x%02x%02x]", type, homeid, nodeid, endpoint);
 * (see rd_get_ep_name() in the Z/IP gateway source)
 * 
 * It can be overridden though such that we will get something else as
 * service name.
 */
size_t zresource_get_type(const struct zip_service *s, char *type_buf, size_t type_buf_len) {
  int i = 0;
  memset(type_buf, 0, type_buf_len);

  // Locate last instance of '[' (assuming the default format)
  const char *type_end = strrchr(s->service_name, '[');
  if (type_end) {
    i = 0;
    for (const char *p = s->service_name; p != type_end && i < type_buf_len - 1; p++) {
      type_buf[i++] = *p;
    }
    type_buf[i] = '\0';
  } else {
    // '[' not found - must be custom format. Just grab all of it
    strncpy(type_buf, s->service_name, type_buf_len);
    type_buf[type_buf_len - 1] = '\0'; // strncpy does not guarantee null-termination of string
  }

  // Remove trailing spaces (if any)
  for (i = strlen(type_buf) - 1; i > 0 && type_buf[i] == ' '; i--) {
    type_buf[i] = '\0';
  }

  return i;
}

/* Get the Z-Wave home id of the service */
size_t zresource_get_homeid(const struct zip_service *s, char *homeid_buf, size_t homeid_buf_len) {

  /* host_name of service assumed to be like zwDA5F731814.local where the homeid
   * is embedded in bytes [2...9] ("DA5F7318" in this example)
   */

  memset(homeid_buf, 0, homeid_buf_len);

  if (homeid_buf_len >= 9) { // 9: homeid (8) + '\0' (1)
    if (strlen(s->host_name) >= 10) { // 10: "zw" prefix (2) + homeid (8)
      strncpy(homeid_buf, &s->host_name[2], 8); // Skip the "zw" prefix
    }
  }

  size_t len = strlen(homeid_buf);

  for (int i = 0; i < len; i++) {
    homeid_buf[i] = tolower(homeid_buf[i]);
  }

  return len;
}


size_t zresource_get_nodeid(const struct zip_service *s) {

  /* host_name of service assumed to be like zwDA5F731814.local where the nodeid
   * is embedded in bytes [10..11] ("14" hex in this example)
   */

  unsigned int nodeid = 0;

  if (strlen(s->host_name) >= 14) { // 10: "zw" prefix (2) + homeid (8) + nodeid (4)
    if (sscanf(&s->host_name[10], "%04X.", &nodeid) != 1) {
      LOG_ERROR("Error getting nodeid from hostname \"%s\"", s->host_name);
    }
  }

  return nodeid;
}

const char * zresource_get_ip_str(const struct zip_service *s, char *buf, size_t len) {
  const void *addr_ptr = NULL;

  if (s->ss_addr.ss_family == AF_INET) {
    addr_ptr = &((struct sockaddr_in*)&s->ss_addr)->sin_addr;
  } else if (s->ss_addr.ss_family == AF_INET6) {
    addr_ptr = &((struct sockaddr_in6*)&s->ss_addr)->sin6_addr;
  }

  if (addr_ptr) {
    return inet_ntop(s->ss_addr.ss_family, addr_ptr, buf, len);
  } else {
    return NULL;
  }
}

/* Generate a "friendly" service name to display to the user.
 *
 * The "raw" service name from Z/IP gateway is generated as:
 *   printf("%s [%08x%04x%02x]", type, homeid, nodeid, endpoint);
 * (see rd_get_ep_name() in the Z/IP gateway source)
 */
static void zresource_set_friendly_name(struct zip_service *s) {
  if (!s) {
    return;
  }

  if (s->friendly_name) {
    free(s->friendly_name);
  }

  char type[60] = {0};
  char homeid[10] = {0};
  unsigned int nodeid = 0;

  nodeid = zresource_get_nodeid(s);

  if (nodeid != 0 &&
      zresource_get_type(s, type, sizeof(type)) &&
      zresource_get_homeid(s, homeid, sizeof(homeid)))
  {
    char buf[200] = {0};

    if (s->epid == 0) {
      /* This service "could" be a root device on a multichannel device.
       *
       * Let's count how many services we know for the same host. If there are
       * more than one it's a multichannel device and we put "ROOT" in
       * the name og this one.
       */
      int n = zresource_count_services_on_same_host(s);
      const char *root_str = (n > 1) ? " -- ROOT" : "";
      snprintf(buf, sizeof(buf), "%s%s [%s-%04d-%03d]", type, root_str, homeid, nodeid, s->epid);
    } else {
      /* This service is an endpoint on a multichannel device.
       *
       * We would like to prefix the endpoint name with the "type" name of the root device.
       * 
       * more than one it's a multichannel device and we put "ROOT" in
       * the name og this one.
       */

      struct zip_service *sroot = zresource_get_root_service(s);
      char root_type[50] = {0};
      if (sroot && zresource_get_type(sroot, root_type, sizeof(root_type))) {
        /* Let's make sure the name of the root device is generated properly
         *
         * The order of the mDNS events are unknown, so the friendly name of the
         * root device could have been generated before there were any endpoints
         * services registered. Now we for sure have an endpoint and then the
         * root device would be properly detected as a root device
         */
        zresource_set_friendly_name(sroot);
      }      
      snprintf(buf, sizeof(buf), "%s -- %s [%s-%04d-%03d]", root_type, type, homeid, nodeid, s->epid);
    }

    s->friendly_name = strdup(buf);
  } else {

    /* Simply use the service_name if we could not get all the info we wanted */
    s->friendly_name = strdup(s->service_name);
  }
}

struct zip_service* zresource_add_service(const char* serviceName) {
  struct zip_service* s;

  s = find_service_by_service_name(serviceName);

  if (s == 0) {
    s = (struct zip_service*)calloc(1, sizeof(struct zip_service));
    s->service_name = strdup(serviceName);
    pthread_mutex_lock(&zservice_list_mutex);
    s->next = zresource_services;
    zresource_services = s;
    pthread_mutex_unlock(&zservice_list_mutex);
  }
  return s;
}

void zresource_update_service_info(struct zip_service* s,
                                   const char* hosttarget,
                                   const uint8_t* txtRecord, int txtLen,
                                   struct sockaddr_storage* addr) {
  char record[64];

  if (s->host_name != 0) {
    free(s->host_name);
  }
  s->host_name = strdup(hosttarget);

  if (addr) {
    memset(&s->ss_addr, 0, sizeof(s->ss_addr));
    if (addr->ss_family == AF_INET) {
      memcpy(&s->ss_addr, addr, sizeof(struct sockaddr_in));
    } else if (addr->ss_family == AF_INET6) {
      memcpy(&s->ss_addr, addr, sizeof(struct sockaddr_in6));
    }
  }

  const uint8_t*  p;
  char *strtok_last = NULL;
  for (p = txtRecord; p < (txtRecord + txtLen); p += *p + 1) {
    if (*p > sizeof(record) - 1) {
      continue;
    }
    memcpy(record, p+1, *p);
    record[*p] = 0;
    /* TODO: Verify that neither key nor val exceeds record[] */
    char* key = strtok_r(record, "=", &strtok_last);
    if (key == NULL) {
          continue;
    }
    uint8_t *val = (uint8_t*)key + strlen(key) + 1;
    int vallen = *p - strlen(key) - 1;

    if (strcmp(record, "info") == 0) {
      if (s->info) free(s->info);
      s->info = malloc(vallen);
      s->infolen = vallen;
      memcpy(s->info, val, vallen);
      zresource_update_cc_info(s);

    } else if (strcmp(record, "epid") == 0 && vallen >= 1) {
      s->epid = *(val);

    } else if ((strcmp(record, "mode") == 0) && (vallen >= sizeof(uint16_t))) {
      uint16_t mode = ntohs(*(uint16_t*) val);
      s->comm_mode = mode & 0xFF;
      s->oper_flags = (mode >> 8) & 0xFF;

    } else if ((strcmp(record, "productID") == 0) && (vallen >= 3 * sizeof(uint16_t))) {
      uint16_t *productID = (uint16_t*) val;
      s->manufacturerID = ntohs(productID[0]);
      s->productType    = ntohs(productID[1]);
      s->productID      = ntohs(productID[2]);

    } else if (strcmp(record, "aggregated") == 0) {
      if (s->aggregated) free(s->aggregated);
      s->aggregated = malloc(vallen);
      memcpy(s->aggregated, val, vallen);
      s->aggregatedlen = vallen;

    } else if (strcmp(record, "securityClasses") == 0 && vallen >= 1) {
      s->securityClasses = *(val);

    } else if ((strcmp(record, "icon") == 0) && (vallen >= sizeof(uint32_t))) {
      uint32_t iconID = ntohl(*(uint32_t*) val);
      s->installer_iconID = (iconID >> 16) & 0xFFFF;
      s->user_iconID      = iconID & 0xFFFF;
    }
  }
  zresource_set_friendly_name(s);
}
