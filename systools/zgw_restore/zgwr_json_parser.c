/* © 2019 Silicon Laboratories Inc. */

#include "json.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include "stdint.h"
#include <ctype.h>
#include "stdbool.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "net/uip.h"
#include "net/uiplib.h"
#include "sys/clock.h"
#include "lib/zgw_log.h"

#include "ZW_transport_api.h"
#include "ZW_classcmd.h"
#include "ZIP_Router_logging.h"

#include "zgw_data.h"
#include "zgw_restore.h"
#include "zgw_restore_cfg.h"
#include "zgwr_json_parser_helpers.h"

#include "libs2/include/s2_keystore.h"


#define PROVISION_NODE_ID 0xfa

zgw_log_id_define(js_imp);
zgw_log_id_default_set(js_imp);

extern uint32_t homeID;  // home id in network byte order (big endian)
extern uint8_t MyNodeID;

/* **************** */
/* Prototypes */
/* **************** */

/*
Hierarchy:
top levels:
 - zgw_restore_json_read
 - zgw_restore_parse_backup
   - zgwr_parse_metadata
   - zgw_parse_zgw
     - zgw_parse_lan_data
     - zgw_parse_zwpan_data
     - zgw_parse_nodes
       - zgw_parse_node
         - zgw_parse_node_uid
         - zgw_parse_zwnode
           - zgw_parse_zwnode_prod_id
           - zgw_parse_zwnode_probe_state
             - zgw_parse_cc_versions
           - zgw_parse_zwnode_data
           - zgw_parse_zwnode_liveness
           - zgw_parse_zwnode_endpoints
             - zgw_parse_zwnode_ep
               - zgw_parse_ep_mDNS_data
               - zgw_parse_ep_nifs
         - zgw_parse_nodepvs
         - zgw_parse_node_ipdata

*/

/* JSON to internal-formats helpers */

/** Copy the string of a json string object to a new string.
 *
 * Use when the string must exist even if the json object does not.
 *
 * \param obj A valid json object.
 * \param new_len A pointer to a location for the string size.
 * \return A newly allocated string.
 */
static char* string_create_from_json(json_object *obj, size_t *new_len);

/** Set a homeID from a JSON string.
 *
 * Check that the the string has the right format.  The homeid should
 * be 8 hex digits.  Both \p 0x prefix and no prefix is accepted.
 *
 * Stores the id in network order, since the gateway stores its homeid
 * in network order.
 *
 * \param obj A valid json object.
 * \param slot Pointer to a location for the homeid.
 * \return True if \p obj contained a valid Home ID, false otherwise.
 */
static bool homeid_set_from_json(json_object *obj, uint32_t *slot);

/** Hex conversion helper */
static uint8_t read_half_byte(char c1);

/** Convert a string of hex characters to bytes in a hex array.
 *
 * At most \p exp_new_len will be converted.
 *
 * If \p str_len is odd, just read the even part.
 *
 * If the number of hex numbers represented is smaller than expected,
 * and not all characters in \p the_str are consumed (i.e., a non-hex
 * character was found in the string), print a warning.
 *
 * The string should not start with \p 0x.
 *
 * \param the_str A string, e.g., returned from json_object_get_string().
 * \param str_len The json-c string len, ie, excluding the \p \0 termination.
 * \param target_ptr Pointer to a buffer with at least exp_new_len bytes.
 * \param exp_new_len The ideal new length.
 * \return The number of bytes written to target_ptr.
 */
static size_t hex_set_from_string(const char* the_str, size_t str_len,
                                  uint8_t *target_ptr,
                                  const size_t exp_new_len);

/**  Convert a JSON string object of hex characters to a newly allocated byte array.
 *
 * Check if \p obj is a string, extract the string, malloc a byte string
 * that is large enough and convert with hex_set_from_string().
 *
 * It is the responsibility of the caller to free the pointer returned
 * in \p hex_res if the return value is >0.
 *
 * If the string contains non-convertible characters, the size of the allocated
 * memory may be larger than the return value.
 *
 * The string should not start with \p 0x.
 *
 * \param obj A JSON object that should contain a byte string.
 * \param hex_res Return parameter.  Will contain the converted string if the return value is > 0.
 * \return Number of converted bytes.  0 if anything goes wrong.
 */
static size_t hex_create_from_json(json_object *obj, uint8_t **hex_res);

/** Convert a JSON object representing a byte string to bytes in an existing byte array.
 *
 * Extract a string object with key \p key, that represents a hex
 * number, from a json object and convert the characters to actual hex
 * of the expected length.
 *
 * Eg, given "FF" and len 1, return 255 in \p *target_ptr.
 *
 * At least \p exp_new_len bytes must be represented in the string.
 * The rest of the string will be ignored.
 *
 * The string should not start with \p 0x.
 *
 * The contents of the memory area will undefined in case of errors.
 *
 * \param key JSON key of the string object.
 * \param container_obj A valid json object that should contain the string object.
 * \param exp_new_len The number of hex bytes expected in the result.
 * \param target_ptr A memory area at least exp_new_len long.
 * \return False if key is not found or the input object is malformed wrt the target string.
 */
static bool uint8_array_set_from_json(char* key,
                                      json_object *container_obj,
                                      uint8_t *target_ptr,
                                      size_t exp_new_len);

/* Parsers */

/** Populate the zipgateway data.
 *
 * Required fields: a nodeList that includes gw node.
 * Optional fields: zipLanData, zwNetworkData
 *
 * Calls helper functions to parse: nodeList, zipLanData,
 * zwNetworkData.  Fails if any of the helpers fail.
 *
 * \param zgw JSON object that has the key "zgw".
 * \param bu Pointer to the backup-data struct that the parser is populating.
 * \param ctrl Pointer to a data structure representing the Z-Wave data in the bridge controller.
 * \return True parsing was successful, false otherwise.
 */
static bool zgw_parse_zgw(json_object *zgw, zip_gateway_backup_data_t *bu,
                          const zw_controller_t *ctrl);

/** Populate zip_pan_data_t with Z-Wave PAN-side data from JSON "zwNetworkData".
 *
 * Read the zipgateway's Z-Wave security keys and the ECDH private key.
 *
 * Required fields: none
 * Optional fields: zwSecurity
 *
 * In zwSecurity, the zwSecurity, S2UnauthenticatedKey,
 * S2AuthenticatedKey, S2AccessKey, and ECDHPrivateKey must be correct
 * if they are present.
 *
 * \param zwpan_obj JSON object that has the key "zwNetworkData".
 * \param zw_pan_data Pointer to a struct in the bu object that will hold the results.
 * \return False if there are errors in any of the fields.
 */
static bool zgw_parse_zwpan_data(json_object *zwpan_obj, zip_pan_data_t *zw_pan_data);

/** Import the LAN-side zipgateway data.
 *
 * For migration, this information is not required, since it can be
 * produced from the zipgateway.cfg file. (Even the ZipLanIp6, which
 * is written to NVM, can be replaced by a zipgateway.cfg setting, so it
 * is not needed).
 *
 * Ie, for migration, this function can return true, even if some
 * fields are missing.
 *
 * Optional fields: ZipPSK, macAddress, ZipLanGw6, ZipLanIp6,
 * ZipUnsolicitedDestinationIp6, ZipUnsolicitedDestinationPort,
 * ZipUnsolicitedDestination2Ip6, ZipUnsolicitedDestination2Port.
 *
 * The following fields must not contain errors, if they are present:
 * ZipPSK, macAddress.
 *
 * \param lan_obj JSON object that has the key "zipLanData".
 * \param bu Pointer to the backup-data struct that the parser is populating.
 * \return False if there are errors in macAddress or ZipPSK objects, true otherwise.
 */
static bool zgw_parse_lan_data(json_object *lan_obj, zip_gateway_backup_data_t *bu);

/** Import the temporary association data.
 *
 * This information is not required. If not present, ZIP Gateway will create the
 * virtual nodes on starting.
 *
 * Ie, this function can return true, even if some fields are missing, but
 * return false when parsing failed.
 *
 * \param temp_assoc_obj JSON object that has the key "zgwTemporaryAssociationData".
 * \param bu Pointer to the zgw_temporary_association_data struct that the parser is populating.
 * \return False if errors occur in parsing, true otherwise.
 */
static bool zgw_parse_temporary_association_data(json_object *temp_assoc_obj,
                                                 zgw_temporary_association_data_t *bu);

/** Import the node mode
 * This information is not required since restore tool can deduce it from
 * SerialAPI, but if the field is present, the value will overwrite the
 * mode value regardless of the data from SerialAPI.
 *
 * \param mode_obj JSON object that has the key "mode"
 * \param zgw_node_mode Pointer to the rd_node_mode_t data struct that the parser is populating.
 * \return False if errors occur in parsing, true otherwise.
 */
static bool zgw_parse_node_mode(json_object *mode_obj,
                        rd_node_mode_t *zgw_node_mode);

/** Import node uid from json object into node structure.
 *
 * Required fields: uidType and either DSK or (homeID, nodeId) pair, depending on type.
 *
 * Also sets the nodeid, if applicable, and validates that the homeId
 * matches the home ID found in the bridge controller.
 *
 * The function returns false if any of the fields are missing or invalid.
 *
 * \param uid JSON object that has the key "nodeUID".
 * \param node_data Return parameter.  The struct for the node being parsed.
 * \return True if uid is valid, false otherwise.
 */
static bool zgw_parse_node_uid(json_object *uid, zgw_node_data_t *node_data);

/** Import IP data for a device from json backup file.
 *
 * Optional fields: mDNSNodeName
 *
 * \param ipdata_obj Value of a JSON object that has the key "ipData".
 * \param node_data Pointer to the struct where the node data should be saved.
 * \return False if mDNSNodeName is invalid, true otherwise.
 */
static bool zgw_parse_node_ipdata(json_object *ipdata_obj, zgw_node_data_t *node_data);

/** Import provisioning data for a device from json backup file.
 * Not used in migration.
 */
static bool zgw_parse_nodepvs(json_object *val, zgw_node_data_t *node_data);

/** Import the productId data of a node from a nodeProdId field.
 *
 * Required fields: manufacturerId, productType, productId
 *
 * Checking for required fields is not fully implemented.
 *
 * \param node_obj Value of a JSON object with the key "nodeProdId".
 * \param bu Pointer to the backup-data struct that the parser is populating.
 * \param node_gwdata Pointer to the struct where the product id data should be stored.
 * \return False if there is an error in one of the fields or if there are too few fields, true otherwise.
 */
static bool zgw_parse_zwnode_prod_id(json_object *node_obj,
                                     zip_gateway_backup_data_t *bu,
                                     zgw_node_zw_data_t *node_gwdata);

/** Populate CC versions for a node and its endpoints from JSON ccVersions.
 *
 * The internal version data structure with the controlled CC's
 * (zgw_node_probe_state::node_cc_versions) must already be created in
 * \p node_gwdata.  It will be updated with the versions found in JSON
 * if the command classes match.
 *
 * Required fields: For each object in the array, commandClassId and version are required.
 *
 * It is an error if a commandClassId is not a uint16 or a version is not a uint8.
 *
 * Command-class/version pairs that are not controlled by the gateway are not imported.
 *
 * \param cc_vers_array JSON object of type array.
 * \param node_gwdata Pointer to the ZGW data for the node that has these CC versions.
 * \return false if the cc_version struct does not exist, if the json array is empty or if the versions are invalid.
 */
static bool zgw_parse_cc_versions(json_object *cc_vers_array, zgw_node_zw_data_t *node_gwdata);

/** Populate the zgw_node_probe_state_t for a node from JSON fields.
 *
 * Computes state, interview_state, and probe_flags from the state in
 * JSON, if the state is "DONE" or "RE-INTERVIEW".
 *
 * Optional fields: state, ZWplusRoleType, versionCap, ccVersions, isZwsProbed.
 *
 * If state is "DONE", probe_flags for the node will be set to \ref RD_NODE_FLAG_PROBE_HAS_COMPLETED.
 *
 * If state is "RE-INTERVIEW", probe_flags for the node will be set to \ref RD_NODE_PROBE_NEVER_STARTED.
 *
 * \param node_obj Value of a JSON object with the key "probeState".
 * \param bu Pointer to the backup-data struct that the parser is populating.
 * \param node_gwdata Pointer to the struct where the probe state should be stored.
 * \return False if the state object is present but has an unrecognized value, otherwise true.
 */
static bool zgw_parse_zwnode_probe_state(json_object *node_obj,
                                         zip_gateway_backup_data_t *bu,
                                         zgw_node_zw_data_t *node_gwdata);

/** Import node data from the zwNodeData object.
 *
 * For now, everything except the node id is ignored.
 * Required fields: nodeId
 *
 * It is not necessary to check if the nodeid is out of range, since
 * the node will also be checked against the controller.
 *
 * \param obj JSON object that has the key "zwNodeData"
 * \param bu Pointer to the backup-data struct that the parser is populating.
 * \param node_gwdata Pointer to the struct where the node data should be stored.
 * \return The node_id or 0 if it is invalid or if it does not exist.
 */
static nodeid_t zgw_parse_zwnode_data(json_object *obj,
                                      zip_gateway_backup_data_t *bu,
                                      zgw_node_zw_data_t *node_gwdata);

/** Populate the zgw_node_liveness_data_t for a node from JSON fields.
 *
 * Optional fields: livenessState, wakeupInterval
 *
 * If livenessState is not present, the node's liveness_state is set to \ref ZGW_NODE_OK.
 *
 * \param liveness_obj JSON object that has the key "liveness".
 * \param bu Pointer to the backup-data struct that the parser is populating.
 * \param node_gwdata Pointer to the struct where the node liveness data should be stored.
 * \return True
 */
static bool zgw_parse_zwnode_liveness(json_object *liveness_obj,
                                      zip_gateway_backup_data_t *bu,
                                      zgw_node_zw_data_t *node_gwdata);

/** Populate the zgw_ep_mDNS_data_t from JSON data.
 *
 * Optional fields: name, location.
 *
 * Missing fields or empty strings are legal, but the string length
 * must fit in a uint8.
 *
 * \param ep_part Value of a JSON object that has the key "epMDNSData".
 * \param mDNS_data Pointer to the struct where the node mDNS data should be stored.
 * \return False if the strings given are too long, true otherwise.
 */
static bool zgw_parse_ep_mDNS_data(json_object *ep_part, zgw_ep_mDNS_data_t *mDNS_data);


/** Populate the NIFs of a Z-Wave node from JSON "endpointInfoFrames".
 *
 * Computes endpoint_info and endpoint_info_len.
 *
 * Keeps track of the non-secure NIF, since it is a required field if
 * state is "DONE" and the endpoint is not aggregated.
 *
 * Read the non-secure and secure nifs from JSON.
 *
 * Add the two additional gateway CC's (COMMAND_CLASS_ZIP_NAMING,
 * COMMAND_CLASS_ZIP) to the non-secure nif if the endpoint is the
 * root device (0).  If there is a secure nif, add the command class
 * mark between the two nifs.
 *
 * Allocate memory and write everything in
 * zgw_node_ep_data::endpoint_info and and set the total length in
 * zgw_node_ep_data::endpoint_info_len.
 *
 * Required fields: nonSecureNIF
 * Optional fields: secureNIF
 *
 * \param ep_part The JSON object corresponding to the current endpoint.
 * \param ep_data Pointer to the internal representation of the endpoint.
 * \param index The endpoint id, which is found from the index in the JSON list.
 * \return False if nonSecureNIF is missing or malformed or if endpoint_info cannot be malloc'd.
 */
static bool zgw_parse_ep_nifs(json_object *ep_part, zgw_node_ep_data_t *ep_data, int index);

/** Allocate and populate a Z-Wave node endpoint from an entry in the JSON array of endpoints.
 *
 * Optional fields: installerIcon, userIcon, endpointId, epMDNSData,
 * endpointInfoFrames, supportedCCs, state, endpointAggregation.
 *
 * Rules on fields:
 * - State is ignored if it is not "DONE".
 * - If endpointId is present, it must be identical to \p index.
 *
 * \param ep_obj An element of the JSON array of endpoints.
 * \param node_id The node that the endpoint belongs to.
 * \param index position in the JSON array.  Must match ep id.
 * \return The new endpoint data structure or NULL if there are errors in JSON.
 */
static zgw_node_ep_data_t* zgw_parse_zwnode_ep(json_object *ep_obj,
                                               nodeid_t node_id, int index);

/** Populate the endpoints list of zgw_node_zw_data_t for a node from the JSON "endpoints" array.
 *
 * Calls zgw_parse_zwnode_ep() on each element in the JSON array.
 * Validates the endpoint data.  Inserts the endpoint data into the
 * node's list of endpoints.
 *
 * Computes nAggEndpoints and nEndpoints.
 *
 * Rules:
 * - If a real endpoint is listed after an aggregated endpoint, it is dropped.
 * - If an endpoint is not aggregated and the nonSecureNIF is missing
 *  and the node's interview_state is node_do_not_interview, the
 *  node's interview_state will be changed to \ref node_interview if
 *  the node's security flags were present.  If the node's security flags
 *  were not present in JSON, the endpoint with the missing NIF is dropped.
 *  - If an endpoint is dropped, the function returns false.
 *
 * The other node data must be imported before calling this function,
 * to be able to check the rules.
 *
 * \param ep_array JSON object that contains a JSON array with the endpoints.
 * \param arr_len Length of \p ep_array.
 * \param bu Pointer to the backup-data struct that the parser is populating.
 * \param node_gwdata Pointer to the struct where the node's endpoint list should be stored.
 * \param node_id The node that the endpoints belong to.
 * \return True if all JSON objects in val are valid endpoint descriptions and were successfully imported, false otherwise.
 */
static bool zgw_parse_zwnode_endpoints(json_object *ep_array, int arr_len,
                                       zip_gateway_backup_data_t *bu,
                                       zgw_node_zw_data_t *node_gwdata,
                                       nodeid_t node_id);

/** Populates zgw_node_zw_data_t for a Z-Wave node from JSON "zgwZWNodeData".
 *
 * Optional fields: grantedKeys, nodeProdId, probeState, zwNodeData, liveness, endpoints
 *
 * After parsing zgwZWNodeData, the node must have an id, either from
 * uid or from zwNodeData.  The nodeUID object must be imported before
 * calling this function, so that this can be checked.
 *
 * If the node id in UID and zwNodeData are different, the node is dropped.
 *
 * If the node is not found in the bridge controller, it is dropped.
 * Otherwise, the node properties are imported from the controller.
 *
 * If there are errors returned from probeState
 * (zgw_parse_zwnode_probe_state()) or liveness
 * (zgw_parse_zwnode_liveness()), the node is dropped.
 *
 * If there are errors returned from parsing the endpoints (e.g., if
 * an endpoint was dropped) the node is dropped.
 *
 * If the node is the zipgateway, the security keys are converted and
 * imported to \ref zw_security::assigned_keys.  If the zipgateway has
 * more than one endpoint, it is dropped.
 *
 * If a network node does not have at least one endpoint, it is dropped.
 *
 * If the grantedKeys field is present, the \ref RD_NODE_FLAG_ADDED_BY_ME
 * flag is set on the node.  If the grantedKeys field is not present,
 * the node probe state is set to \ref node_interview.
 *
 * If the node's probe state is \ref node_interview the node's probe_flags
 * field is set to \ref RD_NODE_PROBE_NEVER_STARTED.  All the
 * endpoints' states are set to \ref EP_STATE_PROBE_INFO.
 *
 * If the node's probe state is \ref node_do_not_interview, the node's
 * probe_flags field is set to \ref RD_NODE_FLAG_PROBE_HAS_COMPLETED.
 * If the node is missing endpoints, nodeProductId, or grantedKeys or
 * if the version probe flags are inconsistent, the node is dropped.
 *
 * \param node Value of a JSON object that has the key "zgwZWNodeData".
 * \param bu Pointer to the backup-data struct that the parser is populating.
 * \param node_data Pointer to the struct where the node data should be stored.
 * \param ctrl Pointer to a data structure representing the Z-Wave data in the bridge controller.
 * \return True if the node data is valid, false if the node should be dropped.
 */
static bool zgw_parse_zwnode(json_object *node, zip_gateway_backup_data_t *bu,
                             zgw_node_data_t *node_data, const zw_controller_t *ctrl);

/** Allocates and populates zgw_node_data_t from an entry in the JSON array "nodeList".
 *
 * This function allocates memory for the new node, but it does not
 * insert it into bu.  The result will be checked further and may be
 * dropped by the caller.
 *
 * Required fields: nodeUID, zgwZWNodeData or pvsData
 * Optional fields: ipData
 * Fields where errors block import of node: nodeUID, zgwZWNodeData.
 *
 * A (homeId, nodeId) UID is also checked against controller data.
 *
 * Uses zgw_node_data_init() to create the new object.
 *
 * Uses the helpers zgw_parse_node_uid(), zgw_parse_zwnode(), zgw_parse_nodepvs(), and zgw_parse_node_ipdata().
 *
 * A node MUST have a node id or be a provision.  The node id can be
 * written in nodeUID or in zwData.
 *
 * Errors in "ipData" or "pvsData" only give warnings.
 *
 * \param node JSON object from the nodeList array.
 * \param bu Pointer to the backup-data struct that the parser is populating.
 * \param ctrl Pointer to a data structure representing the Z-Wave data in the bridge controller.
 * \return Pointer to a newly created node struct, NULL if import fails.
 */
static zgw_node_data_t *zgw_parse_node(json_object *node, zip_gateway_backup_data_t *bu,
                                       const zw_controller_t *ctrl);

/** Populate the gateway software data on the network nodes from JSON "nodeList" array.
 *
 * Supports both included and provisioned nodes.
 *
 * Node list must not be empty, since the gateway itself must exist.
 *
 * Calls the helper function zgw_parse_node() on each node.  Prints warnings if nodes are
 * dropped or if the node id of the zipgateway itself is missing.
 *
 * The nodes are inserted in the global backup structure with the
 * helper zgw_node_data_pointer_add().  If a node id is duplicated in
 * json, the helper will drop the second set of data and print a
 * warning.
 *
 * Prints out the number of nodes imported.
 *
 * \param nodes JSON object that has the key "nodeList".
 * \param bu Pointer to the backup-data struct that the parser is populating.
 * \param ctrl Pointer to a data structure representing the Z-Wave data in the bridge controller.
 * \return  False if the zipgateway itself is missing, otherwise true.
 */
static bool zgw_parse_nodes(json_object *nodes, zip_gateway_backup_data_t *bu,
                            const zw_controller_t *ctrl);


/** Populate the zip_gateway_backup_data_t meta-data fields from JSON "ZGWBackupInfo".
 *
 * Not currently used.
 */
static bool zgwr_parse_metadata(json_object *obj, zip_gateway_backup_manifest_t *manifest);


/*
 * Static functions
 */

/* Helpers */
static char* string_create_from_json(json_object *obj, size_t *new_len) {
   const char* the_str = json_object_get_string(obj);
   char *new_str = NULL;
   *new_len = json_object_get_string_len(obj);

   /* json-c never returns NULL on an object, but len is 0 if type is
      not string. */
   if (*new_len > 0) {
      new_str = malloc(*new_len);
      if (new_str != NULL) {
         memcpy(new_str, the_str, *new_len);
      }
   }
   return new_str;
}

/* Helper that needs to know about both json and zgw_data.h. */
static bool homeid_set_from_json(json_object *obj, uint32_t *slot) {
   enum json_type type = json_object_get_type(obj);
   uint32_t homeID;

   if (type == json_type_string) {
      size_t str_len = json_object_get_string_len(obj);
      const char *tmp_str = json_object_get_string(obj);

      if ((str_len == 10) && (tmp_str[1] == 'x') && (tmp_str[0] == '0')) {
         /* If Home ID is, eg, 0xABCD1234, parse ABCD1234 as hex */
         hex_set_from_string(tmp_str+2, json_object_get_string_len(obj),
                             (uint8_t*)(&homeID), 4);
      } else if (str_len == 8) {
         /* If Home ID is, eg, ABCD1234, parse ABCD1234 as hex */
         hex_set_from_string(tmp_str, json_object_get_string_len(obj),
                             (uint8_t*)(&homeID), 4);
      } else {
         /* No other formats are supported. */
         printf("Home ID string has illegal format %s\n", tmp_str);
         return false;
      }
      printf("  Read home ID: %x%x%x%x\n",
             ((uint8_t*)(&homeID))[0],
             ((uint8_t*)(&homeID))[1],
             ((uint8_t*)(&homeID))[2],
             ((uint8_t*)(&homeID))[3]);
   } else {
      printf("Invalid Z-Wave homeid: %s, type %s\n", json_object_get_string(obj),
             json_type_to_name(type));
      return false;
   }
   /* homeID is stored in network order. */
   *slot = homeID;
   return true;
}


/* Helper-helper :-) */
static uint8_t read_half_byte(char c1) {
   uint8_t u1 = 0;
   if (isdigit(c1)) {
      u1 = (c1 - '0');
   } else {
      c1 = toupper(c1);
      u1 += (c1 - 'A') + 10;
   }
   return u1;
}

/* we don't care if it is odd length, we just read the even
   part. */
/* str_len is the json-c string len, ie, excluding the \0 termination.
 * target_ptr must hold at least exp_new_len bytes */
static size_t hex_set_from_string(const char* the_str, size_t str_len,
                                  uint8_t *target_ptr,
                                  const size_t exp_new_len) {
   size_t new_len = 0;
   size_t ii = 1; /* We read 2 characters at a time */
   uint8_t *next_u8 = target_ptr;

   while ((ii < str_len) && (new_len < exp_new_len)) {
      char c1 = the_str[ii-1];
      char c2 = the_str[ii];

      if (isxdigit(c2) && isxdigit(c1)) {
         *next_u8 = (read_half_byte(c1)<<4) + read_half_byte(c2);
      } else {
         /* just stop at invalid character */
         break;
      }
      next_u8++;
      new_len++;
      ii += 2;
   }
   if ((new_len < exp_new_len) && (ii != str_len+1)) {
      printf("Error in hex string %s, length %zu\n", the_str, str_len);
      return new_len;
   }

   #ifdef VERBOSE_PRINT_ENABLED
   if (new_len) {
      VERBOSE_PRINT("  Converted %zu bytes ", new_len);
      print_hex(target_ptr, new_len);
   }
   #endif
   return new_len;
}

static size_t hex_create_from_json(json_object *obj, uint8_t **hex_res) {
   size_t str_len = json_object_get_string_len(obj);
   size_t hex_len = 0;
   const char *the_str = NULL;

   if (str_len > 1) {
      *hex_res = malloc(str_len/2);
      if (*hex_res) {
         the_str = json_object_get_string(obj);
         hex_len = hex_set_from_string(the_str, str_len,
                                       *hex_res, str_len/2);
         if (hex_len == 0) {
            free(*hex_res);
            *hex_res = NULL;
            printf("Not a byte string object %s.\n", the_str);
         } else {
            printf("  Found byte string object %s, byte length %zu.\n", the_str, hex_len);
         }
      } else {
         printf("Failed to allocate memory for byte string import.\n");
      }
   } else {
      printf("Unexpected value in object: %s.  Expected byte string.\n",
             json_object_get_string(obj));
   }
   return hex_len;
}


/*
 * Extract a string object with a hex number from a json object and convert it to actual hex of the expected length.
 *
 * Eg, given \p 0xFFFF and len 2, return 65535 in *target_ptr.
 *
 * The memory area will be overwritten even in case of errors.
 *
 * \param container_obj A valid json object that should contain the string object.
 * \param exp_new_len The number of hex bytes expected in the result.
 * \param target_ptr A memory area at least exp_new_len long.
 * \return false if the input object is malformed wrt the target string.
 */
static bool uint8_array_set_from_json(char* key,
                                      json_object *container_obj,
                                      uint8_t *target_ptr,
                                      size_t exp_new_len) {
   json_object *part_obj = NULL;
   const char* the_str;
   size_t str_len;
   size_t ii;
   size_t new_len = 0;
   bool res = true;

   if (!json_object_object_get_ex(container_obj, key, &part_obj)) {
      printf("Key %s not found.\n", key);
      return false;
   }
   str_len = json_object_get_string_len(part_obj);
   if (str_len < exp_new_len*2) {
      printf("Too few characters in key %s, found %zu, expected %zu.\n",
             key, str_len, exp_new_len*2);
      return false;
   }
   the_str = json_object_get_string(part_obj);
   /* hex_set_from_string() converts at most exp_new_len bytes. */
   new_len = hex_set_from_string(the_str, str_len, target_ptr, exp_new_len);

   if (new_len < exp_new_len) {
      printf("Too few bytes in key %s, found %zu bytes, expected %zu bytes\n",
             key, new_len, exp_new_len);
      res = false;
   }
   if (res) {
      printf("  Installed %s key 0x", key);
      for (ii = 0; ii<new_len; ii++) {
         printf("%02x", target_ptr[ii]);
      }
      printf(".\n");
   } else {
      printf("Failed to convert %s to hex\n", the_str);
   }
   return res;
}


/* **************** */
/*   Parsers        */
/* **************** */

/* Read the gateways Z-Wave security keys.
 *
 * JSON field zwNetworkData.
 *
 * Some of this goes to NVM.
 */
static bool zgw_parse_zwpan_data(json_object *zwpan_obj, zip_pan_data_t *zw_pan_data) {
   json_object *zw_security_obj = NULL;
   json_object *lr_keys_obj = NULL;
   zw_security_t *sec_keys = &(zw_pan_data->zw_security_keys);
   bool res = true;

   printf("\nParsing Z-Wave PAN-side data\n");

   if (json_object_object_get_ex(zwpan_obj, "zwSecurity", &zw_security_obj)) {
      res &= uint8_array_set_from_json("S0key", zw_security_obj,
                                       sec_keys->security_netkey,
                                       16);

      /* S2_UNAUTHENTICATED: 0 */
      res &= uint8_array_set_from_json("S2UnauthenticatedKey",
                                       zw_security_obj,
                                       sec_keys->security2_key[0],
                                       16);
      /* S2_AUTHENTICATED: 1 */
      res &= uint8_array_set_from_json("S2AuthenticatedKey",
                                       zw_security_obj,
                                       sec_keys->security2_key[1],
                                       16);
      /* S2_ACCESS: 2 */
      res &= uint8_array_set_from_json("S2AccessKey",
                                       zw_security_obj,
                                       sec_keys->security2_key[2],
                                       16);
      /* This one should be 32 long */
      /* The private key is currently a required field, but that is not checked here. */
      res &= uint8_array_set_from_json("ECDHPrivateKey",
                                       zw_security_obj,
                                       sec_keys->ecdh_priv_key,
                                       32);
      if (json_object_object_get_ex(zw_security_obj, "S2AccessKeyLR", &lr_keys_obj)) {
         /* S2_ACCESS_LR: 1 in LR */
         res &= uint8_array_set_from_json("S2AccessKeyLR",
                                          zw_security_obj,
                                          sec_keys->security2_lr_key[1],
                                          16);
         zip_gateway_backup_data_unsafe_get()->zgw.zip_pan_data.zw_security_keys.assigned_keys |= KEY_CLASS_S2_ACCESS_LR;
      }
      if (json_object_object_get_ex(zw_security_obj, "S2AuthenticatedKeyLR", &lr_keys_obj)) {
         /* S2_AUTHENTICATED_LR: 0 in LR */
         res &= uint8_array_set_from_json("S2AuthenticatedKeyLR",
                                          zw_security_obj,
                                          sec_keys->security2_lr_key[0],
                                          16);
         zip_gateway_backup_data_unsafe_get()->zgw.zip_pan_data.zw_security_keys.assigned_keys |= KEY_CLASS_S2_AUTHENTICATED_LR;
      }
      /* assignedKeys is set from the grantedKeys in the gateway's node info */
   }
   if (!res) {
      printf("\nSecurity keys missing.\n\n");
   } else {
      printf("\nParsing Z-Wave PAN-side data completed.\n");
   }
   return res;
}

/* Parse the gateway-software data.
 *
 * Required fields: nodeList that includes gw node.
 * Optional fields: zipLanData, zwNetworkData, zgwAssociationData
 * Fields where parsing errors block import: nodeList, zipLanData, zwNetworkData
 */
static bool zgw_parse_zgw(json_object *zgw, zip_gateway_backup_data_t *bu,
                          const zw_controller_t *ctrl) {
   bool res = true;
   enum json_type type;

   printf("\nParsing ZGW object\n");
   json_object_object_foreach(zgw, key, val) {
      type = json_object_get_type(val);
      switch (type) {
      case json_type_array:
         /* Included nodes is array. */
         if (parse_key_match(key, "nodeList")) {
            res &= zgw_parse_nodes(val, bu, ctrl);
            if (!res) {
               printf("Errors in nodeList\n");
            }
         } else {
            printf("Unexpected array %s\n", key);
         }
         break;
      case json_type_object:
         if (parse_key_match(key, "zipLanData")) {
            res &= zgw_parse_lan_data(val, bu);
         } else if (parse_key_match(key, "zwNetworkData")) {
            /* Z-Wave Network data */
            res &= zgw_parse_zwpan_data(val, &(bu->zgw.zip_pan_data));
         } else if (parse_key_match(key, "zgwTemporaryAssociationData")) {
            res &= zgw_parse_temporary_association_data(val, &(bu->zgw.zgw_temporary_association_data));
         }
         break;
      case json_type_string:
      case json_type_int:
      case json_type_boolean:
      case json_type_double:
      case json_type_null:
         printf("zgw: Unexpected json type, key %s\n", key);
         break;
      }
   }
   /* If we did not find the gateway node, return error. */
   if (res) {
      if ((ctrl != NULL) && zgw_node_data_get(MyNodeID) == NULL) {
         printf("Missing import data for the gateway.\n");
         return false;
      } else {
         return res;
      }
   } else {
     return false;
   }
}

/* Gateway's LAN side data.
 *
 * For migration, this information is not required, since it can be
 * produced from the zipgateway.cfg file.
 *
 * Ie, this function can return true, even if some fields are missing.
 *
 * If the fields are present, the following fields must not contain
 * errors: ZipPSK, macAddress.
 */
static bool zgw_parse_lan_data(json_object *lan_obj, zip_gateway_backup_data_t *bu) {
   enum json_type type;
   const char *the_str;
   zip_lan_ip_data_t *zip_lan_data = &(bu->zgw.zip_lan_data);
   bool res = true;

   printf("\nParsing LAN-side data\n");

   json_object_object_foreach(lan_obj, key, the_obj) {
      type = json_object_get_type(the_obj);
      switch (type) {
      case json_type_string:
         {
            the_str = json_object_get_string(the_obj);
            if (parse_key_match(key, "ZipPSK")) {
               size_t the_str_len = json_object_get_string_len(the_obj);
               printf("  ZipPSK:");
               size_t new_len = hex_set_from_string(the_str,
                                                    the_str_len,
                                                    zip_lan_data->zip_psk,
                                                    64); /* size of zip_lan_data->zip_psk */
               if (new_len < 16) {
                  /* It does not have to be in json, since it can be
                     in zipgateway.cfg.  But if it is, it must be
                     correct */
                  printf("Too few bytes in DTLS key %s, found %zu, expected at least %d\n",
                         key, new_len, 16);
                  res = false;
               }
            } else if (parse_key_match(key, "ZipLanGw6")) {
               printf("  For %s:", key);
               ipv6addr_set_from_string(the_str, &(zip_lan_data->zgw_cfg_lan_gw_ip6addr));

            } else if (parse_key_match(key, "ZipLanIp6")) {
               printf("  For %s:", key);
               ipv6addr_set_from_string(the_str, &(zip_lan_data->zgw_cfg_lan_addr));

            } else if (parse_key_match(key, "ZipUnsolicitedDestinationIp6")) {
               printf("  For %s:", key);
               ipv6addr_set_from_string(the_str, &(zip_lan_data->unsol_dest1));

            } else if (parse_key_match(key, "ZipUnsolicitedDestination2Ip6")) {
               printf("  For %s:", key);
               ipv6addr_set_from_string(the_str, &(zip_lan_data->unsol_dest2));

            } else if (parse_key_match(key, "macAddress")) {
               size_t the_str_len = json_object_get_string_len(the_obj);
               printf("  Mac address:");
               size_t new_len = hex_set_from_string(the_str,
                                                    the_str_len,
                                                    zip_lan_data->zgw_uip_lladdr.addr,
                                                    sizeof(uip_lladdr_t));
               if (new_len < UIP_LLADDR_LEN) {
                  printf("Too few bytes in mac address %s, found %zu, expected %zu\n",
                         key, new_len, sizeof(uip_lladdr_t));
                  res = false;
               }
            } else {
               printf("Unexpected string %s with key %s in json file.\n",
                      the_str, key);
            }
         }
         break;
      case json_type_int:
         {
            int32_t the_number = json_object_get_int(the_obj);
            zgwr_key_t uint16_keys[2] = {"ZipUnsolicitedDestinationPort",
                                         "ZipUnsolicitedDestination2Port"};
            uint16_t* uint16_fields[2] = {&(zip_lan_data->unsol_port1),
                                          &(zip_lan_data->unsol_port2)};
            if (!(find_uint16_field(the_number, key,
                                    2, uint16_keys, uint16_fields))) {
               printf("Unexpected integer in lan data, key %s, int %d\n",
                      key, the_number);
            }
            break;
         }
      case json_type_object:
      case json_type_array:
      case json_type_null:
      case json_type_boolean:
      case json_type_double:
         {
            printf("Unexpected object %s in json file.\n",
                   json_object_get_string(the_obj));
         }
         break;
      }
   }

   printf("Parsing LAN-side data completed.\n\n");
   return res;
}

/*Gateway's temporary association data.
 *
 * This information is not required, in which case, ZIP Gateway will create the
 * virtual nodes on starting.
 *
 * Ie, this function can return true, even if some fields are missing, but
 * return false when parsing failed.
 */
static bool zgw_parse_temporary_association_data(json_object *temp_assoc_obj,
                            zgw_temporary_association_data_t *zgw_temp_assoc) {
  json_object *part;
  bool res = true;
  if (json_object_object_get_ex(temp_assoc_obj, "virtualNodeList", &part)) {
    if (json_object_get_type(part) == json_type_array) {
      int arr_len = json_object_array_length(part);
      if (arr_len >= 1 && arr_len <=4) {
        uint8_set_from_intval(arr_len,
                              &(zgw_temp_assoc->virtual_nodes_count));
        for (int ii = 0; ii < arr_len; ii++) {
          json_object *node_obj = json_object_array_get_idx(part, ii);

          printf("Reading virtual node object at index %d\n", ii);
          if (json_object_get_type(node_obj) == json_type_int) {
            node_id_set_from_intval(json_object_get_int(node_obj),
                                    &(zgw_temp_assoc->virtual_nodes[ii]));
          } else {
            printf("Virtual node list contains invalid items at index %d\n", ii);
            res = false;
          }
        }
      } else {
        printf("Invalid virtual node list length: %d\n", arr_len);
        res = false;
      }
    } else {
      printf("Invalid type of virtual node list\n");
      res = false;
    }
  } else {
    printf("No virtual node list found\n");
    res = false;
  }
  return res;
}

static bool zgw_parse_node_mode(json_object *mode_obj,
                     rd_node_mode_t *zgw_node_mode) {
   size_t mode_str_length;
   char *mode_str = string_create_from_json(mode_obj, &mode_str_length);
   bool res = true;
   if (mode_str_length != 0) {
      /* In case json schema check has not been conducted */
      if (!strncmp(mode_str, "MODE_ALWAYSLISTENING", mode_str_length)) {
         *zgw_node_mode = MODE_ALWAYSLISTENING;
      } else if(!strncmp(mode_str, "MODE_FREQUENTLYLISTENING", mode_str_length)) {
         *zgw_node_mode = MODE_FREQUENTLYLISTENING;
      } else if(!strncmp(mode_str, "MODE_NONLISTENING", mode_str_length)) {
         *zgw_node_mode = MODE_NONLISTENING;
      } else if(!strncmp(mode_str, "MODE_MAILBOX", mode_str_length)) {
         *zgw_node_mode = MODE_MAILBOX;
      } else {
         res = false;
         printf("Invalid string in node mode: %s\n", mode_str);
      }
   } else {
      /* json-c returns len as 0 if the type is not string */
      res = false;
      printf("Invalid type of node mode. Expected string.\n");
   }

   free(mode_str);
   return res;
}

static bool zgw_parse_node_type(json_object *node_type_obj, uint8_t *zgw_node_type) {
   size_t type_str_length;
   char *type_str = string_create_from_json(node_type_obj, &type_str_length);
   bool res = true;
   if (type_str_length != 0) {
      /* In case json schema check has not been conducted */
      if (!strncmp(type_str, "BASIC_TYPE_CONTROLLER", type_str_length)) {
         *zgw_node_type = BASIC_TYPE_CONTROLLER;
      } else if(!strncmp(type_str, "BASIC_TYPE_STATIC_CONTROLLER", type_str_length)) {
         *zgw_node_type = BASIC_TYPE_STATIC_CONTROLLER;
      } else if(!strncmp(type_str, "BASIC_TYPE_SLAVE", type_str_length)) {
         *zgw_node_type = BASIC_TYPE_SLAVE;
      } else if(!strncmp(type_str, "BASIC_TYPE_ROUTING_SLAVE", type_str_length)) {
         *zgw_node_type = BASIC_TYPE_ROUTING_SLAVE;
      } else {
         res = false;
         printf("Invalid string in node type: %s\n", type_str);
      }
   } else {
      /* json-c returns len as 0 if the type is not string */
      res = false;
      printf("Invalid type of node type. Expected string.\n");
   }

   free(type_str);
   return res;
}

/* Import node uid from json object
 *
 * Required fields: uidType, either DSK or (homeID, nodeId) pair.
 */
static bool zgw_parse_node_uid(json_object *uid, zgw_node_data_t *node_data) {
   json_object *the_type = NULL;
   const char *the_type_str = NULL;
   size_t the_type_len = 0;
   bool res = true;

   if (json_object_object_get_ex(uid, "uidType", &the_type)) {
      the_type_str = json_object_get_string(the_type);
      the_type_len = json_object_get_string_len(the_type);

      if ((the_type_len >= 3) && (memcmp(the_type_str, "DSK", 3) == 0)) {
         json_object *the_obj;
         if (json_object_object_get_ex(uid, "DSK", &the_obj)) {
            uint8_t *dsk;
            size_t len = 0;
            dsk = create_DSK_uint8_ptr_from_json(the_obj, &len);
            if (dsk != NULL) {
               node_data->uid_type = node_uid_dsk;
               node_data->node_uid.dsk_uid.dsk_len = len;
               node_data->node_uid.dsk_uid.dsk = dsk;
               printf("  Read string %s\n  Set uid type %d, dskLen %d\n",
                      json_object_to_json_string(uid), node_data->uid_type,
                      node_data->node_uid.dsk_uid.dsk_len);
            } else {
               printf("Failed to find DSK in %s\n",
                      json_object_get_string(uid));
               res = false;
            }
         }
      } else if ((the_type_len >= 6) && (memcmp(the_type_str, "ZW Net ID", 6)== 0)) {
         json_object *home;
         json_object *node;

         if (json_object_object_get_ex(uid, "homeId", &home)
             && json_object_object_get_ex(uid, "nodeId", &node)
             && node_id_set_from_intval(json_object_get_int(node),
                                      &(node_data->node_uid.net_uid.node_id))
             && homeid_set_from_json(home, &(node_data->node_uid.net_uid.homeID))) {
            node_data->uid_type = node_uid_zw_net_id;

            /* Check that uid does not have a different home_id from the controller. */
            if(homeID==0) {
              homeID = node_data->node_uid.net_uid.homeID;
            } else if (homeID != node_data->node_uid.net_uid.homeID) {
               printf("Invalid homeid in node uid %s, expected 0x%04x\n",
                      zgw_node_uid_to_str(node_data), UIP_HTONL(homeID));
               res = false;
            } else {
               printf("  Read string %s\n  Set uid type %d, homeId 0x%04x, nodeid 0x%02x\n",
                      json_object_to_json_string(uid), node_data->uid_type,
                      UIP_HTONL(node_data->node_uid.net_uid.homeID),
                      node_data->node_uid.net_uid.node_id);
            }
         } else {
            printf("Illegal UID in %s\n", json_object_get_string(uid));
            res = false;
         }
      } else {
         printf("Missing type of uid object %s, len %zu\n",
                json_object_get_string(uid), the_type_len);
         res = false;
      }
   } else {
      printf("Missing uid type in %s\n", json_object_get_string(uid));
      res = false;
   }
   return res;
}

static bool zgw_parse_node_ipdata(json_object *ipdata_obj, zgw_node_data_t *node_data) {
   json_object *part;
   bool res = true;

   if (json_object_object_get_ex(ipdata_obj, "mDNSNodeName", &part)) {
      size_t the_str_len = 0;
      node_data->ip_data.mDNS_node_name = string_create_from_json(part,
                                                                  &(the_str_len));
      if (the_str_len <= UCHAR_MAX) {
         node_data->ip_data.mDNS_node_name_len = the_str_len;
         printf("Imported mDNS name\n");
      } else {
         printf("Illegal string length %s in node name, length %zu\n",
                node_data->ip_data.mDNS_node_name, the_str_len);
         res &= false;
      }
   }

   if (!res) {
      printf("Failed to import IP data for node\n");
   }

   return res;
}

/* Import provisioning data for a device from json backup file. */
static bool zgw_parse_nodepvs(json_object *val, zgw_node_data_t *node_data) {
   printf("Import of provisioning currently not supported. Ignoring.\n");
   return false;
}

/* Parse the productId field.
 *
 * Should check if all fields are there, since no default exists (not
 * fully implemented).
 */
static bool zgw_parse_zwnode_prod_id(json_object *node_obj,
                                     zip_gateway_backup_data_t *bu,
                                     zgw_node_zw_data_t *node_gwdata) {
   enum json_type type;
   bool res = true;
   zgwr_key_t uint16_keys[3] = {"manufacturerId",
                                "productType",
                                "productId"};
   uint16_t* uint16_fields[3] = {&(node_gwdata->node_prod_id.manufacturerID),
                                 &(node_gwdata->node_prod_id.productType),
                                 &(node_gwdata->node_prod_id.productID)};

   printf("Importing node product information\n");
   if (json_object_object_length(node_obj) != 3) {
      printf("Unexpected data in productID: %d items, expectd %d items.\n",
             json_object_object_length(node_obj), 3);
      res = false;
      /* Don't return here, we still want to parse what we have. */
   }
   json_object_object_foreach(node_obj, key, part) {
      type = json_object_get_type(part);
      switch (type) {
      case json_type_int:
         {
            int32_t the_number = json_object_get_int(part);
            if (!(find_uint16_field(the_number, key,
                                    3, uint16_keys, uint16_fields))) {
               printf("*** Unexpected integer in node, key %s, int %d\n",
                      key, the_number);
               res = false;
            }
         }
      break;
   default:
      printf("zgw: Unexpected json type, key %s\n", key);
      break;
      }
   }
   return res;
}

/* Populate CC versions for a node and its endpoints.
 *
 * Updates node_cc_versions.
 * Internal version data structure is already created with the controlled CC's.
 */
static bool zgw_parse_cc_versions(json_object *cc_vers_array,
                                  zgw_node_zw_data_t *node_gwdata) {
   int array_len = json_object_array_length(cc_vers_array);
   json_object *cc_vers_obj;
   zgw_node_probe_state_t *probe_state = &(node_gwdata->probe_state);
   bool res = true;

   if (array_len == 0) {
      printf("  Found empty CC version list\n");
      return false;
   }
   /* The cc versions structure should be pre-created */
   if (node_gwdata->probe_state.node_cc_versions == NULL) {
      node_gwdata->probe_state.node_cc_versions_len = 0;
      printf("Cannot import CC versions.\n");
      return false;
   }
   for (int ii=0; ii<array_len; ii++) {
      uint16_t cc;
      uint8_t version;

      cc_vers_obj = json_object_array_get_idx(cc_vers_array, ii);
      printf("  Reading CC version at index %d: ", ii);
      if (json_object_get_type(cc_vers_obj) == json_type_object) {
         json_object *cc_obj = NULL;
         json_object *vers_obj = NULL;

         /* Find the CC id and the CC version */
         json_object_object_get_ex(cc_vers_obj, "commandClassId", &cc_obj);
         json_object_object_get_ex(cc_vers_obj, "version", &vers_obj);

         /* Update version if it is controlled by gateway. */
         if (uint8_set_from_json(vers_obj, &version)) {
            if (uint16_set_from_json(cc_obj, &cc)) {
               if (cc_version_set(probe_state, cc, version) == version) {
                  printf("  Set CC 0x%04x (%d) version to %d\n", cc, cc, version);
               } else {
                  printf("Ignoring version of CC 0x%04x (%d), CC is not controlled by ZGW.\n",
                         cc, cc);
               }
            } else {
               printf("Error in version settings at index %d\n", ii);
               res = false;
            }
         }
      } else {
         printf("Error in version object at index %d\n", ii);
         res = false;
      }
   }

   return res;
}

/* Node probe state is persisted by zipgateway when it is \ref
 * STATUS_DONE, \ref STATUS_PROBE_FAIL, or \ref STATUS_FAILING.  The
 * Restore Tool can also persist \ref STATUS_CREATED, which means
 * "RE-INTERVIEW".
 *
 * When the gateway reads in the persistence (eeprom.dat) file,
 * - the endpoint probe state of all the endpoints will be set to \ref
 *   EP_STATE_MDNS_PROBE if the node state is STATUS_DONE.
 * - if the node state is STATUS_CREATED, the endpoints will be set to
 *   EP_STATE_PROBE_INFO.
 * - if the node state is STATUS_PROBE_FAIL or STATUS_FAILING, the endpoints are not
 *   touched, so restore needs to set them correctly.
 *
 *  We can safely assume that the state will never be STATUS_PROBE_FAIL in
 *  JSON in the migration scenario, so only STATUS_DONE and STATUS_CREATED are
 *  supported by restore.
 */
static bool zgw_parse_zwnode_probe_state(json_object *node_obj,
                                         zip_gateway_backup_data_t *bu,
                                         zgw_node_zw_data_t *node_gwdata) {
   enum json_type type;
   bool res = true;
   bool found_versions = false;

   json_object_object_foreach(node_obj, key, part) {
      type = json_object_get_type(part);

      switch (type) {
      case json_type_string:
         /* probe_flags:
            RD_NODE_PROBE_NEVER_STARTED
            RD_NODE_FLAG_PROBE_FAILED
            RD_NODE_FLAG_PROBE_HAS_COMPLETED
            RD_NODE_FLAG_PROBE_STARTED */
         {
            const char* the_str = json_object_get_string(part);
            size_t the_str_len = json_object_get_string_len(part);
            /* Note that probe state overlaps with the liveness state in the gateway!
               That is handled when restoring state for the eeprom. */
            if (parse_key_match(key, "state")) {
               if (strcmp(the_str, "DONE") == 0) {
                  node_gwdata->probe_state.state = (rd_node_state_t)node_do_not_interview;
                  node_gwdata->probe_state.interview_state = node_do_not_interview;
                  node_gwdata->probe_state.probe_flags = RD_NODE_FLAG_PROBE_HAS_COMPLETED;
               } else if (strcmp(the_str, "RE-INTERVIEW") == 0) {
                  node_gwdata->probe_state.state = (rd_node_state_t)node_interview;
                  node_gwdata->probe_state.interview_state = node_interview;
                  /* TODO: set flags to RD_NODE_PROBE_STARTED if we have cc_versions? */
                  node_gwdata->probe_state.probe_flags = RD_NODE_PROBE_NEVER_STARTED;
                  printf("- Interview requested for node.\n");
               } else {
                  printf("Unexpected probeState state: %s.\n", the_str);
                  res = false;
               }
            } else if (parse_key_match(key, "ZWplusRoleType")) {
               if (strcmp(the_str, "portableSlave") == 0) {
                  node_gwdata->probe_state.node_properties_flags |= RD_NODE_FLAG_PORTABLE;
                  printf("  Imported role type portable slave.\n");
               } else {
                  printf("  Read %s %s.  Node is not a portable slave.\n",
                         key, the_str);
               }
            } else if (parse_key_match(key, "versionCap")) {
               hex_set_from_string(the_str, the_str_len,
                                   &(node_gwdata->probe_state.node_version_cap_and_zwave_sw), 1);
            }
         }
         break;
      case json_type_int:
         {
            uint32_t the_number = json_object_get_int(part);
         /* Values used for Version Capabilities Report command: */
         /* #define VERSION_CAPABILITIES_REPORT_PROPERTIES1_VERSION_BIT_MASK_V3 0x01 */
         /* #define VERSION_CAPABILITIES_REPORT_PROPERTIES1_COMMAND_CLASS_BIT_MASK_V3 0x02 */
         /* #define VERSION_CAPABILITIES_REPORT_PROPERTIES1_Z_WAVE_SOFTWARE_BIT_MASK_V3 0x04 */
            if (parse_key_match(key, "versionCap")) {
               uint8_set_from_intval(the_number,
                                     &(node_gwdata->probe_state.node_version_cap_and_zwave_sw));
            } else {
               printf("Unexpected probeState integer with key %s.\n", key);
            }
         }
         break;
      case json_type_array:
            if (parse_key_match(key, "ccVersions")) {
               found_versions = zgw_parse_cc_versions(part, node_gwdata);
            } else {
               printf("Unexpected array object: %s\n", json_object_get_string(part));
            }
         break;
      case json_type_boolean:
         if (parse_key_match(key, "isZwsProbed")) {
            if (json_object_get_boolean(part)) {
               node_gwdata->probe_state.node_is_zws_probed = 1;
            } else {
               node_gwdata->probe_state.node_is_zws_probed = 0;
            }
         }
         break;
      case json_type_object:
      case json_type_null:
      case json_type_double:
         printf("Unexpected json type: %s\n", json_object_get_string(part));
         break;
      }
   }
   if (node_gwdata->probe_state.interview_state == node_do_not_interview) {
      if (!found_versions) {
         printf("Missing CC versions\n");
         /* We have initialized the cc_versions structure to zeroes,
            so it exists. */
      }
      if ((node_gwdata->probe_state.node_is_zws_probed == 1)
          && (node_gwdata->probe_state.node_version_cap_and_zwave_sw == 0)) {
         /* This probe happens after version_cap, so version_cap must also be set. */
         printf("Inconsistent probe state: VERSION_ZWAVE_SOFTWARE is probed, but not supported by node.\n");
      }
   }
   return res;
}

static nodeid_t zgw_parse_zwnode_data(json_object *obj,
                                      zip_gateway_backup_data_t *bu,
                                      zgw_node_zw_data_t *node_gwdata) {
   nodeid_t node_id = 0;
   enum json_type type;

   printf("Parsing ZGW Z-Wave node data looking for node id\n");
   json_object_object_foreach(obj, key, val) {
      type = json_object_get_type(val);
      switch (type) {
      case json_type_int:
         {
            if (parse_key_match(key, "nodeId")) {
               int32_t the_number = json_object_get_int(val);
               node_id = node_id_set_from_intval(the_number,
                                                 &(node_gwdata->zw_node_data.node_id));
               if (node_id == 0) {
                  printf("Skipping node id %d from object %s\n",
                         the_number, json_object_get_string(obj));
               }
            }
         }
         break;
      case json_type_object:
      case json_type_array:
      case json_type_boolean:
      case json_type_double:
      case json_type_null:
      case json_type_string:
         printf("Skipping json object %s in zwnode data\n",
                json_object_to_json_string(val));
         break;
      }
   }

   return node_id;
}

static bool zgw_parse_zwnode_liveness(json_object *liveness_obj,
                                      zip_gateway_backup_data_t *bu,
                                      zgw_node_zw_data_t *node_gwdata) {
   int64_t the_number = 0;
   json_object *part;
   bool res = true;

   if (json_object_object_get_ex(liveness_obj, "livenessState", &part)) {
      if (json_object_get_type(part) == json_type_int) {
         the_number = json_object_get_int64(part);
         if (the_number == ZGW_NODE_OK
             || (the_number == ZGW_NODE_FAILING)) {
            node_gwdata->liveness.liveness_state = (zgw_node_liveness_state_t)the_number;
         } else {
            node_gwdata->liveness.liveness_state = ZGW_NODE_OK;
            printf("Node liveness not included, assuming OK.\n");
         }
      } else if (json_object_get_type(part) == json_type_string) {
         const char *the_string = json_object_get_string(part);
         if (strcmp(the_string, "OK") == 0) {
            node_gwdata->liveness.liveness_state = ZGW_NODE_OK;
         } else if (strcmp(the_string, "FAILING") == 0) {
            node_gwdata->liveness.liveness_state = ZGW_NODE_FAILING;
         }
      }
   }

   if (json_object_object_get_ex(liveness_obj, "wakeupInterval", &part)) {
      if (json_object_get_type(part) == json_type_int) {
         the_number = json_object_get_int64(part);
         uint32_set_from_intval(the_number,
                                &(node_gwdata->liveness.wakeUp_interval));
      }
   } else {
      printf("No wake up interval found for node\n");
   }
   return res;
}

/* No-value or empty strings are legal, but return error on invalid data. */
static bool zgw_parse_ep_mDNS_data(json_object *ep_part, zgw_ep_mDNS_data_t *mDNS_data) {
   json_object *mdns_part = NULL;
   size_t str_len = 0;
   bool res = true;

   if (json_object_object_get_ex(ep_part, "name", &mdns_part)) {
      mDNS_data->endpoint_name = string_create_from_json(mdns_part,
                                                         &(str_len));
      if (str_len <= UCHAR_MAX) {
         mDNS_data->endpoint_name_len = (uint8_t)str_len;
      } else {
         printf("Illegal string length %s, length %zu\n",
                mDNS_data->endpoint_name, str_len);
         free(mDNS_data->endpoint_name);
         mDNS_data->endpoint_name = NULL;
         res = false;
      }
   }
   if (json_object_object_get_ex(ep_part, "location", &mdns_part)) {
      mDNS_data->endpoint_location = string_create_from_json(mdns_part,
                                                             &(str_len));
      if (str_len <= UCHAR_MAX) {
         mDNS_data->endpoint_loc_len = (uint8_t)str_len;
      } else {
         printf("Illegal string length %s, length %zu\n",
                mDNS_data->endpoint_location, str_len);
         free(mDNS_data->endpoint_location);
         mDNS_data->endpoint_location = NULL;
         res = false;
      }
   }
   return res;
}

/* Fill in uint8_t endpoint_info_len and uint8_t *endpoint_info */
static bool zgw_parse_ep_nifs(json_object *ep_part, zgw_node_ep_data_t *ep_data, int index) {
   json_object *cc_obj = NULL;
   size_t str_len;
   uint8_t *nif = NULL;
   uint8_t *sec_nif = NULL;
   uint8_t *node_info = NULL;
   size_t nif_len = 0;
   size_t sec_nif_len = 0;
   size_t node_info_len = 0;
   bool res = true;

   if (json_object_object_get_ex(ep_part, "nonSecureNIF", &cc_obj)) {
      nif_len = hex_create_from_json(cc_obj, &nif);
   }
   if (nif_len == 0) {
      printf("Failed to read nonSecureNIF\n");
      return false;
   }

   if (json_object_object_get_ex(ep_part, "secureNIF", &cc_obj)) {
      /* secure NIF can be blank */
      sec_nif_len = hex_create_from_json(cc_obj, &sec_nif);
   }

   /* Now create the NIF in the right internal format. */
   node_info_len = nif_len;
   if (index == 0) {
      node_info_len += 2; /* The read NIF plus the two gateway CCs */
   }
   if (sec_nif_len > 0) {
      node_info_len += 2 + sec_nif_len;
   }
   if (node_info_len > 0xFF) {
      printf("Illegal combined length of endpointInfoFrames field.\n");
   }
   node_info = malloc(node_info_len);

   if (node_info != NULL) {
      memcpy(node_info, nif, nif_len);
      if (index == 0) {
         node_info[nif_len] = COMMAND_CLASS_ZIP_NAMING;
         node_info[nif_len+1] = COMMAND_CLASS_ZIP;
         nif_len += 2;
      }
      ep_data->endpoint_info = node_info;
      ep_data->endpoint_info_len = node_info_len;

      if (sec_nif_len > 0) {
         node_info += nif_len;

         /* Insert security command class mark */
         *node_info++ = (COMMAND_CLASS_SECURITY_SCHEME0_MARK >> 8);
         *node_info++ = (COMMAND_CLASS_SECURITY_SCHEME0_MARK >> 0) & 0xFF;

         memcpy(node_info, sec_nif, sec_nif_len);
      } else {
         /* This is ok if node is not secure */
         printf("No secure NIF for node\n");
      }
      printf("  Import NIF (len %u) 0x", ep_data->endpoint_info_len);
      print_hex(ep_data->endpoint_info, ep_data->endpoint_info_len);
   } else {
      printf("Failed to import NIF for node or endpoint\n");
      res = false;
   }
   if (sec_nif) free(sec_nif);
   free(nif);
   return res;
}

/*
 * Parse and allocate a Z-Wave node endpoint.
 *
 * Except for malloc failures and index check, most validation is done
 * by the caller.
 *
 * If it exists, the ep id must match the index position in the JSON array.
 */
static zgw_node_ep_data_t* zgw_parse_zwnode_ep(json_object *ep_obj,
                                               nodeid_t node_id, int index) {
   enum json_type type;
   bool found_id = false;
   /* endpoints are linked, so make a new one */
   zgw_node_ep_data_t *ep_data = zgw_node_endpoint_init();

   if (ep_data == NULL) {
      return NULL;
   }
   json_object_object_foreach(ep_obj, key, ep_part) {
      type = json_object_get_type(ep_part);

      switch (type) {
      case json_type_int:
         {
            int32_t the_number = json_object_get_int(ep_part);
            zgwr_key_t uint16_keys[2] = {"installerIcon",
                                         "userIcon"};
            uint16_t* uint16_fields[2] = {&(ep_data->installer_iconID),
                                          &(ep_data->user_iconID)};

            if (parse_key_match(key, "endpointId")) {
               found_id = uint8_set_from_intval(the_number, &(ep_data->endpoint_id));
               ep_data->endpoint_id_field_status = zgwr_field_from_zgw_json;
               printf("  Importing endpoint %u\n", ep_data->endpoint_id);
            } else if (!(find_uint16_field(the_number, key,
                                           2, uint16_keys, uint16_fields))) {
               printf("Unexpected integer in node, key %s, int %d\n",
                      key, the_number);
            }
         }
         break;
      case json_type_object:
         {
            if (parse_key_match(key, "epMDNSData")) {
               zgw_parse_ep_mDNS_data(ep_part, &(ep_data->ep_mDNS_data));
            } else if (parse_key_match(key, "endpointInfoFrames")) {
               zgw_parse_ep_nifs(ep_part, ep_data, index);
            } else if (parse_key_match(key, "supportedCCs")) {
               zgw_parse_ep_nifs(ep_part, ep_data, index);
            } else {
               printf("ep: Unexpected object with key %s in json file.\n",
                      key);
            }
         }
         break;
      case json_type_array:
         /* endpointAggregation is currently parsed as a hex string. */
      case json_type_boolean:
      case json_type_double:
      case json_type_null:
         /* Silently ignore unknown objects in endpoints. */
         break;
      case json_type_string:
         {
            const char *the_str;
            the_str = json_object_get_string(ep_part);

            if (parse_key_match(key, "state")) {
               if ((json_object_get_string_len(ep_part) >= 4)
                   && (memcmp(the_str, "DONE", 4) == 0)) {
                  ep_data->state = EP_STATE_PROBE_DONE;
                  printf("  Setting ep state to %s\n", the_str);
               } else {
                  printf("Unexpected json state %s in ZGW node endpoint data.\n",
                         the_str);
               }
            } else if (parse_key_match(key, "endpointAggregation")) {
               size_t the_len = json_object_get_string_len(ep_part);
               if (the_len >= 2) {
                  ep_data->endpoint_agg = malloc(the_len/2);
                  if (ep_data->endpoint_agg) {
                     ep_data->endpoint_aggr_len = hex_set_from_string(the_str,
                                                                      the_len,
                                                                      ep_data->endpoint_agg,
                                                                      the_len/2);
                     if (ep_data->endpoint_aggr_len == 0) {
                        free(ep_data->endpoint_agg);
                        ep_data->endpoint_agg = NULL;
                     } else {
                        printf("  Imported aggregated endpoints (len %zu) 0x",
                               the_len/2);
                        print_hex(ep_data->endpoint_agg,
                                  ep_data->endpoint_aggr_len);
                     }
                  } else {
                     printf("Failed to import aggregated endpoints, out of memory.\n");
                  }
               } else if (the_len != 0) { /* Assume empty string means NULL */
                  printf("Failed to import aggregated endpoints '%s'.\n", the_str);
               }
            } else {
               printf("Unexpected json string %s with key %s in ZGW node endpoint data.\n",
                      the_str, key);
            }
            break;
         }
      }
   }

   if (found_id) {
      if (ep_data->endpoint_id == index) {
         return ep_data;
      } else {
         printf("Endpoint array is unordered, cannot import endpoint %u at index %d.\n",
                ep_data->endpoint_id, index);
      }
   } else {
      ep_data->endpoint_id = index;
      printf("  Importing endpoint %u\n", ep_data->endpoint_id);
      return ep_data;
   }

   zgw_node_endpoint_free(ep_data);
   return NULL;
}

static bool zgw_parse_zwnode_endpoints(json_object *ep_array, int arr_len,
                                       zip_gateway_backup_data_t *bu,
                                       zgw_node_zw_data_t *node_gwdata,
                                       nodeid_t node_id) {
   int ii;
   json_object *ep_obj;
   zgw_node_ep_data_t *ep_data;
   int aggr_index = ZW_MAX_NODES; /* Some number larger than the max num of endpoints (127). */
   bool keep_ep = true;

   printf("Importing %d endpoints\n", arr_len);

   for (ii=0; ii<arr_len; ii++) {
      ep_obj = json_object_array_get_idx(ep_array, ii);
      printf("  Importing endpoint data at index %d\n", ii);
      if (json_object_get_type(ep_obj) == json_type_object) {
         ep_data = zgw_parse_zwnode_ep(ep_obj, node_id, ii);
         if (ep_data != NULL) {
            /* validate contents against node and previous eps */
            if (ii >= aggr_index) {
               /* All aggregated endpoints must have an endpoint_agg field. */
               if (ep_data->endpoint_aggr_len == 0) {
                  printf("The aggregated endpoints must be last in the list, skipping endpoint %u, index %d\n",
                         ep_data->endpoint_id, ii);
                  keep_ep = false;
               } else {
                  node_gwdata->nAggEndpoints++;
               }
            } else if (ep_data->endpoint_aggr_len > 0) {
               /* First aggregated endpoint */
               node_gwdata->nAggEndpoints++;
               aggr_index = ii;
            } else {
               /* Not aggregated endpoints */
               /* The endpoints are parsed last, so we can check on the node data here. */
               if ((ep_data->endpoint_info_len == 0) &&
                   (node_gwdata->probe_state.interview_state == node_do_not_interview)) {
                  printf("Missing NIF for endpoint %u of node %u in state DONE\n",
                         ep_data->endpoint_id, node_id);
                  if (node_gwdata->probe_state.node_properties_flags & RD_NODE_FLAG_ADDED_BY_ME) {
                     printf("CHANGING requested interview state to RE-INTERVIEW because of missing NIF for endpoint %u of node %u\n",
                            ep_data->endpoint_id, node_id);
                     node_gwdata->probe_state.interview_state = node_interview;
                  } else {
                     /* If the granted keys are not there, we may as well start over. */
                     printf("Ignoring endpoint %u of node %u\n", ep_data->endpoint_id, node_id);
                     keep_ep = false;
                  }
               }
            }

            if (keep_ep) {
               list_add(node_gwdata->endpoints, ep_data);
               (node_gwdata->nEndpoints)++;
            } else {
               zgw_node_endpoint_free(ep_data);
            }
         } else {
            printf("Error in endpoint at index %d\n", ii);
         }
      } else {
         printf("Incorrect endpoint information at index %d\n", ii);
      }
   }
   if (node_gwdata->nEndpoints != arr_len) {
      return false;
   }
   return true;
}


/* Parse the gateway data for a Z-Wave node */

/* Rules for node identification:
 * For nodes included in the Z/IP Gateway's network, the node id is
 * used to link the data from the storage file and the data in the
 * bridge controller.  This works as follows:
 *
 * If the node has a DSK, the uid object must have type DSK.
 * If the node has a zw-network uid, that includes a node id.
 *
 * If the node has a DSK uid object, but no zwNodeData object or no
 * nodeId in the zwNodeData object, it is assumed to be a provision
 * and must have a pvsData object.  Provisions are ignored for now.
 *
 * If the node has a pvsData object, it must have a DSK uid.
 *
 * If the node has a node id in the zwNodeData object, that nodeid must
 * exist in the controller.  Otherwise, the node will be ignored.
 */
/* Populates zgw_node_zw_data_t :
   zgw_node_liveness_data_t liveness;
   zw_node_data_t zw_node_data; (has the node id)
   zgw_node_probe_state_t probe_state;
   zgw_node_product_id_t node_prod_id;
   uint8_t security_flags;
   uint8_t nEndpoints;
   uint8_t nAggEndpoints;
   rd_node_mode_t mode;
   LIST_STRUCT(endpoints);
*/
/* Required fields: either zwNodeData or UID.
 * Fields where errors are blocking: endpoints, probeState, liveness, mode.
 */
static bool zgw_parse_zwnode(json_object *node, zip_gateway_backup_data_t *bu,
                             zgw_node_data_t *node_data, const zw_controller_t *ctrl) {
   zgw_node_zw_data_t *node_gwdata = &(node_data->node_gwdata);
   enum json_type type;
   json_object *endpoint_array = NULL;
   bool found_prod_id = false;
   bool keep_node = true;
   nodeid_t node_id = 0;
   bool found_keys = false;
   
   printf("\nzwnode: parsing Gateway's Z-Wave-related data for a device.\n");

   json_object_object_foreach(node, key, val) {
      type = json_object_get_type(val);
      switch (type) {
      case json_type_int:
         {
            if (parse_key_match(key, "grantedKeys")) {
               int32_t the_number = json_object_get_int(val);
               found_keys = uint8_set_from_intval(the_number,
                                                  &(node_gwdata->security_flags));
            } else {
               printf("Unexpected integer in node, key %s\n", key);
            }
         }
         break;
      case json_type_object:
         {
            printf("  zwnode: json_type_object %s\n", key);
            if (parse_key_match(key, "nodeProdId")) {
               found_prod_id = zgw_parse_zwnode_prod_id(val, bu, node_gwdata);
            } else if (parse_key_match(key, "probeState")) {
               keep_node &= zgw_parse_zwnode_probe_state(val, bu, node_gwdata);
            } else if (parse_key_match(key, "zwNodeData")) {
               /* Only node_id is important here */
               node_id = zgw_parse_zwnode_data(val, bu, node_gwdata);
            } else if (parse_key_match(key, "liveness")) {
               keep_node &= zgw_parse_zwnode_liveness(val, bu, node_gwdata);
            } else {
               printf("zwnode: Unexpected object with key %s in json file.\n",
                      key);
            }
         }
         break;
      case json_type_array:
         if (json_object_array_length(val) > 0
             && parse_key_match(key, "endpoints")) {
            /* Save the array and parse the endpoints last. */
            endpoint_array = val;
         }
         break;
      case json_type_boolean:
      case json_type_double:
      case json_type_null:
         printf("zgw_parse_zwnode: Unexpected json type, key %s\n", key);
         break;
      case json_type_string:
         if (parse_key_match(key, "mode")) {
            keep_node &= zgw_parse_node_mode(val, &node_gwdata->mode);
         } else if (parse_key_match(key, "nodeType")) {
            keep_node &= zgw_parse_node_type(val, &node_gwdata->zw_node_data.node_type.basic);
         } else {
            printf("Unexpected json string %s in ZGW node Z-Wave data.\n",
                json_object_to_json_string(val));
         }
         break;
      }
   }

   /* Get a least one node_id from json */
   if (node_data->uid_type == node_uid_zw_net_id) {
      if (node_id == 0) {
         node_id = node_data->node_uid.net_uid.node_id;
         printf("  Using node Id %u found in UID\n", node_id);
      }
   }

   if (node_id == 0) {
      printf("Missing node_id for node object: %s\n", json_object_get_string(node));
      keep_node = false;
   } else {
      zw_node_data_t *zw_node_data = NULL;

      if (node_data->uid_type == node_uid_zw_net_id) {
         /* Check that uid does not have a different node_id. */
         if ((node_data->node_uid.net_uid.node_id != node_id)) {
            printf("Inconsistent node_id with uid %s, node_id %d\n",
                   zgw_node_uid_to_str(node_data), node_id);
            keep_node = false;
         }
      }

      /* We have an ID, check ID against controller data and copy
         controller data for this node */
      if(ctrl) {
        zw_node_data = ctrl->included_nodes[node_id-1];
        if (zw_node_data == NULL) {
          printf("Node uid (0x%04x:%u) is not included in network\n",
                  UIP_HTONL(node_data->node_uid.net_uid.homeID), node_id);
          keep_node = false;
          node_id = 0;
        } else {
          memcpy(&(node_gwdata->zw_node_data),
                  zw_node_data,
                  sizeof(zw_node_data_t));
          printf("  Imported properties %02x, %02x, %02x from controller\n",
                  node_gwdata->zw_node_data.node_type.basic,
                  node_gwdata->zw_node_data.node_type.generic,
                  node_gwdata->zw_node_data.node_type.specific);
        }
      }
   }

   /* Done checking that node json is OK */
   if (keep_node) {
      /* Import endpoints */
      if (endpoint_array != NULL) {
         /* zgw_parse_zwnode_endpoints() will count the aggregated endpoints */
         node_gwdata->nAggEndpoints = 0;
         keep_node = zgw_parse_zwnode_endpoints(endpoint_array,
                                                json_object_array_length(endpoint_array),
                                                bu, node_gwdata, node_id);
         if (keep_node && node_id == MyNodeID) {
            /* Copy some info upwards. */
            /* LR keys are handled in function zgw_parse_zwpan_data(). Need special handling because node flags dont keep track of LR keys */
            bu->zgw.zip_pan_data.zw_security_keys.assigned_keys = node_flags2keystore_flags(node_gwdata->security_flags);
            printf("Importing Gateway keys 0x%02x to system.\n",
                   bu->zgw.zip_pan_data.zw_security_keys.assigned_keys);
            if (node_gwdata->nEndpoints > 1) {
               printf("Unexpected number of endpoints in gateway node %u.\n",
                      node_id);
               keep_node = false;
            }
         }
      } else {
         if (node_id != MyNodeID) {
            printf("New node must have at least endpoint 0\n");
            keep_node = false;
         } else {
            /* The gateway itself can re-generate its endpoint data, so no problem. */
            /* Copy some info upwards. */
            /* LR keys are handled in function zgw_parse_zwpan_data(). Need special handling because node flags dont keep track of LR keys */
            bu->zgw.zip_pan_data.zw_security_keys.assigned_keys = node_flags2keystore_flags(node_gwdata->security_flags);
            printf("Importing Gateway keys 0x%02x to system.\n",
                   bu->zgw.zip_pan_data.zw_security_keys.assigned_keys);
         }
      }
   }

   if (keep_node) {
      if (node_id == MyNodeID) {
         /* Check required gw fields */
         /* ZGW fields for RD - not strictly needed, since zgw will
            initialize them on startup. */
         if (node_gwdata->nEndpoints == 0) {
            zgw_node_ep_data_t *ep_data = zgw_node_endpoint_init();
            if (ep_data != NULL) {
               ep_data->endpoint_id = 0;
               ep_data->state = EP_STATE_PROBE_DONE;
               list_add(node_gwdata->endpoints, ep_data);
               node_gwdata->nEndpoints = 1;
            } else {
               printf("Failed to allocate data for gateway endpoint 0.\n");
            }
         }
         zgw_node_ep_data_t *ep = list_head(node_gwdata->endpoints);
         if (ep && ep->endpoint_id == 0) {
            ep->user_iconID = ICON_TYPE_GENERIC_GATEWAY;
            ep->installer_iconID = ICON_TYPE_GENERIC_GATEWAY;
         } else {
            printf("Unexpected endpoint in gateway: %u\n", ep->endpoint_id);
            keep_node = false;
         }
      } else {
         /* Inherit and distribute properties of network node */
         if (found_keys) {
            /* Set ADDED_BY_ME if granted_keys field is present, to
               indicate that the keys are trusted. */
            node_gwdata->probe_state.node_properties_flags |= RD_NODE_FLAG_ADDED_BY_ME;
         } else {
            /* If grantedKeys is missing, downgrade interview state. */
            node_gwdata->probe_state.interview_state = node_interview;
         }

         /* Interview state may come from default setting or may have
            changed to node_interview because of missing grantedKeys
            or missing NIF in endpoints, so re-set the dependent
            fields. */
         if (node_gwdata->probe_state.interview_state == node_interview) {
            zgw_node_ep_data_t *ep;

            node_gwdata->probe_state.probe_flags = RD_NODE_PROBE_NEVER_STARTED;

            for (ep = list_head(node_gwdata->endpoints); ep != NULL; ep = list_item_next(ep)) {
               printf("Setting ep %u state to EP_STATE_PROBE_INFO\n", ep->endpoint_id);
               ep->state = EP_STATE_PROBE_INFO;
               ep = list_item_next(ep);
            }
         } else if (node_gwdata->probe_state.interview_state == node_do_not_interview) {
            /* If state is node_do_not_interview, check/set required fields */
            node_gwdata->probe_state.probe_flags = RD_NODE_FLAG_PROBE_HAS_COMPLETED;

            if (node_gwdata->nEndpoints == 0) {
               printf("Invalid data for node %u, missing root device information\n",
                      node_id);
               keep_node = false;
            }
            if (!found_prod_id) {
               printf("Invalid data for node %u, missing product id information\n",
                      node_id);
               keep_node = false;
            }
            if (!found_keys) {
               printf("No granted keys for node %u, assuming 0\n", node_id);
            }
            /* probe flags */
            if (node_gwdata->probe_state.node_is_zws_probed &&
                (((node_gwdata->probe_state.node_version_cap_and_zwave_sw & 0x03) != 0x03))) {
               printf("Illegal versionCap setting when isZwsProbed is true: 0x%u.\n",
                      node_gwdata->probe_state.node_version_cap_and_zwave_sw);
               keep_node = false;
            }
         } else {
            printf("Unexpected interview state\n");
            keep_node = false;
         }
      }
   }
   if (!keep_node) {
      printf("Not importing node %u, due to errors\n", node_id);
   }

   return keep_node;
}

/* Parse the gateway software data for one node */
/* Populates zgw_node_data_t:
   zgw_node_uid_type_t uid_type;
   zgw_node_uid_t node_uid;
   zw_node_data_t node_zwdata;
   zgw_node_zw_data_t node_gwdata;
   zgw_node_IP_data_t ip_data;
   zgw_node_pvs_t pvs_data;
*/
static zgw_node_data_t *zgw_parse_node(json_object *node, zip_gateway_backup_data_t *bu,
                                       const zw_controller_t *ctrl) {
   json_object * node_part_obj;
   zgw_node_data_t *node_data;
   bool has_provision = false;
   nodeid_t node_id = 0;

   printf("Parsing node data\n");

   if (!json_object_object_get_ex(node, "nodeUID", &node_part_obj)) {
      printf("Missing UID object for node\n");
      return 0;
   }

   /* Put in defaults */
   node_data = zgw_node_data_init();
   if (node_data == 0) {
      return 0;
   }
 
   /* If uid is nodeid/home id and does not match ctrl data, ignore
      object with a warning */
   if (!zgw_parse_node_uid(node_part_obj, node_data)) {
      printf("Illegal node uid\n");
      free(node_data);
      return NULL;
   }
   printf("Found node id %s.\n",
          zgw_node_uid_to_str(node_data));

   if((ctrl==NULL) && json_object_object_get_ex( node,"isZGW",&node_part_obj)) {
     if(json_object_get_boolean(node_part_obj)) {
       MyNodeID = node_data->node_uid.net_uid.node_id;
     }
   }
   if(ctrl == NULL) {
    node_data->node_gwdata.zw_node_data.node_id = node_data->node_uid.net_uid.node_id;
   }

   if (json_object_object_get_ex(node, "zgwZWNodeData", &node_part_obj)) {
      if (!zgw_parse_zwnode(node_part_obj, bu, node_data, ctrl)) {
         printf("Illegal node data\n");
         free(node_data);
         return NULL;
      }
   } else {
      /* Node must be provision object */
      printf("Missing zgwZWNodeData\n");
      node_id = PROVISION_NODE_ID;
   }
   if (json_object_object_get_ex(node, "pvsData", &node_part_obj)) {
      has_provision = zgw_parse_nodepvs(node_part_obj, node_data);
   }

   if (!json_object_object_get_ex(node, "ipData", &node_part_obj)) {
      printf("  No mDNS data for node %s\n",
             json_object_get_string(node_part_obj));
   } else {
      if (!zgw_parse_node_ipdata(node_part_obj, node_data)) {
         printf("Errors in ip data for node %s\n",
                json_object_get_string(node_part_obj));
      } else {
         printf("  Imported mDNS data for node %d\n",
                node_data->node_gwdata.zw_node_data.node_id);
      }
   }

   if ((node_id == PROVISION_NODE_ID) && !has_provision) {
      printf("Not importing node id %s.  Missing provision.\n",
             zgw_node_uid_to_str(node_data));
      free(node_data);
      return NULL;
   }

   return node_data;
}


/* Parse the gateway software data on the nodes (included and provisioned). */
static bool zgw_parse_nodes(json_object *nodes, zip_gateway_backup_data_t *bu,
                            const zw_controller_t *ctrl) {
   int arr_len = json_object_array_length(nodes);
   int ii;
   uint16_t num_nodes = 0;
   json_object *node_obj;
   bool res = true;

   printf("Importing %d nodes in node array\n", arr_len);

   for (ii = 0; ii < arr_len; ii++) {
      node_obj = json_object_array_get_idx(nodes, ii);

      printf("\nReading node zgw data at index %d\n", ii);

      if (json_object_get_type(node_obj) == json_type_object) {
         zgw_node_data_t *node_data = zgw_parse_node(node_obj, bu, ctrl);
         if (node_data != NULL) {
            nodeid_t node_id = node_data->node_gwdata.zw_node_data.node_id;
            if (zgw_node_data_pointer_add(node_id, node_data)) {
               printf("Imported node %u at index %u\n", node_id, ii);
               res = true;
            } else {
               res = false;
            }
         } else {
            res = false;
         }
      } else {
         res = false;
      }
      if (!res) {
         printf("Ignoring incorrect node information at index %d\n", ii);
      } else {
         num_nodes++;
      }
   }

   if ((ctrl!=NULL) && !zgw_node_data_get(MyNodeID)) {
      printf("Missing data for gateway, nodeid %u.\n", MyNodeID);
      return false;
   }
   if (num_nodes < arr_len) {
      printf("Failed to import %d out of %d nodes.\n",
             arr_len - num_nodes, arr_len);
   } else {
      printf("  Successfully imported %d nodes\n", num_nodes);
   }
   return true;
}


static bool zgwr_parse_metadata(json_object *obj, zip_gateway_backup_manifest_t *manifest) {
   enum json_type type;

   printf("backup: parsing metadata\n");

   bzero(manifest, sizeof(zip_gateway_backup_manifest_t));

   json_object_object_foreach(obj, key, val) {

      type = json_object_get_type(val);
      switch (type) {
      case json_type_int:
         {
            int64_t the_int = json_object_get_int64(val);
            if (parse_key_match(key, "versionMajor")) {
               manifest->backup_version_major = the_int;
            } else if (parse_key_match(key, "versionMinor")) {
               manifest->backup_version_minor = the_int;
            } else if (parse_key_match(key, "timestamp")) {
               manifest->backup_timestamp = the_int;
            } else if (parse_key_match(key, "sourceChipType")) {
               printf("Read %s\n", json_object_to_json_string(val));
            } else {
               printf("Unexpected integer %" PRId64 ", key %s\n",
                      the_int, key);
         }
         break;

      case json_type_object:
         if (parse_key_match(key, "timestamp")) {
            printf("Structured timestamp not supported yet.");
         }
         break;
      case json_type_string:
         {
            if (parse_key_match(key, "sourceZGWVersion")) {
               printf("Read %s\n", json_object_to_json_string(val));
            } else if (parse_key_match(key, "sourceProtocolVersion")) {
               printf("Read %s\n", json_object_to_json_string(val));
            } else {
               printf("backup: Unexpected json string key %s\n", key);
            }
            break;
         }
      case json_type_array:
      case json_type_null:
      case json_type_boolean:
      case json_type_double:
         printf("backup: Unexpected json type %d, key %s\n", type, key);
         break;
         }
      }
   }
   return true;
}

json_object *zgw_restore_json_read(void) {
   json_object *zgw_backup_obj = NULL;

   //char *log_filename = "restore_api.log";
   //   zgw_log(0, "Setup done\n");
   /* Start the logging system. */
   //zgw_log_setup(restore_cfg.log_filename);
   //zgw_log_setup(log_filename);

   if (json_filename_get() == NULL) {
      return NULL;
   }

   printf("Reading back-up file %s\n",  json_filename_get());

   //   int fd; /* requires newer json-c */
   //   if ((fd = open(filename, O_RDONLY)) < 0) {
   //      printf("Failed to read file: %s, error %s\n",
   //             cfg.filename, strerror(errno));
   //      return NULL;
   //   }
   zgw_backup_obj = json_object_from_file(json_filename_get());
   if (!zgw_backup_obj) {
      printf("Failed parsing json file %s\n", json_filename_get());
   }
   return zgw_backup_obj;
}


/* Extract the backup meta-data from the json object.
   Parse Z/IP gateway data from json object into \ref struct zip_gateway_backup_data.
   Ignores zwController data in json (using the \p ctrl instead).

   0 is success
   -2 is error in ctrl
   -1 is error in json.
*/
int zgw_restore_parse_backup(json_object *zgw_backup_obj, const zw_controller_t *ctrl) {
   enum json_type type;
   zip_gateway_backup_data_t *bu;
   bool res = true;

   zgw_data_reset();

   /* Copy the controller's own data to backup data.
   * (The individual nodes will be copied later.) */
   if(ctrl) {
     zw_controller_add(ctrl->SUCid, ctrl->cc_list);
   }

   bu = zip_gateway_backup_data_unsafe_get();

   printf("Restore_backup for network 0x%04x, gateway node id %d: parsing\n",
          UIP_HTONL(homeID), MyNodeID);

   json_object_object_foreach(zgw_backup_obj, key, val) {

      type = json_object_get_type(val);
      switch (type) {
      case json_type_object:
         {
            if (parse_key_match(key, "ZGWBackupInfo")) {
               /* for migration, we ignore errors in this section. */
               zip_gateway_backup_manifest_t manifest;
               if (zgwr_parse_metadata(val, &manifest)) {
                  bu->manifest = manifest;
               }
            } else if (parse_key_match(key, "zwController")) {
               /* for migration, we get this information directly from
                  the serial API. */
               printf("\nSkipping json representation of controller\n");
               //zw_parse_controller(val, &(bu->controller));
            } else if (parse_key_match(key, "zgw")) {
               res &= zgw_parse_zgw(val, bu, ctrl);
            } else {
               printf("Unexpected object with key %s in json file.\n",
                      key);
            }
         }
         break;
      case json_type_string:
      case json_type_array:
      case json_type_null:
      case json_type_boolean:
      case json_type_double:
      case json_type_int:
         printf("Ignoring unexpected object %s\n", json_object_to_json_string(val));
         break;
      }
   }

   //zgw_log_teardown();

   if (res) {
      return 0;
   } else {
      printf("Found errors in JSON file\n");
      return -1;
   }
}
