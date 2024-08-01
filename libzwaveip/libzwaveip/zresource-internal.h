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

#ifndef LIBZWAVEIP_ZRESOURCE_INTERNAL_H_
#define LIBZWAVEIP_ZRESOURCE_INTERNAL_H_

/**
 * Update service info. This should be called by the mDNS clinet.
 * @param n pointer to the service to update.
 * @param hosttarget Hostname of service
 * @param txtRecord of service
 * @param txtLen length of TXT record
 * @param in ipaddress of service
 */
void zresource_update_service_info(struct zip_service* n,const char* hosttarget,const uint8_t* txtRecord, int txtLen,struct sockaddr_storage* in);


/**
 * Called by and mDNS client when it discovers a new service name
 * @param serviceName Name of the service to add
 * @return return The added zresource
 */
struct zip_service* zresource_add_service(const char* serviceName);

/**
 * Called by a mDNS client when a service is to be removed
 * @param serviceName Name of the service to remove
 */
void zresource_remove_service(const char* serviceName);

/**
 * Helper function to loop over zip_service and return the matched zresource
 * service.
 * @param serviceName Name of the service to query
 * @return zip_service The matched zresource
 */
struct zip_service* find_service_by_service_name(const char* serviceName);


#endif /* LIBZWAVEIP_ZRESOURCE_INTERNAL_H_ */
