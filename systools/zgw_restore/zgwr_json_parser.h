/* © 2019 Silicon Laboratories Inc. */

#ifndef _ZGWR_JSON_PARSER_H
#define _ZGWR_JSON_PARSER_H

#include "json.h"
#include "zgw_data.h"
#include "lib/zgw_log.h"

zgw_log_id_declare(js_imp);

/** \defgroup json-reader ZGW Data File Reader for the Restore Tool
 * \ingroup zgw-restore
 *
 * Parse a ZGW Data File, synchronizing the data with
 * zip_gateway_backup_data::controller and collect data in \ref
 * zip_gateway_backup_data::zgw.
 *
@{
 */

/**
 * Read the ZGW Data File, which is in JSON format, into a json_object
 * with json-c.
 *
 * Uses json_filename_get() to find the file.
 */
json_object *zgw_restore_json_read(void);

/**
 * Parse a JSON object into \ref restore-db.
 *
 * The JSON object must be created by reading a ZGW Data File with
 * zgw_restore_json_read() before calling this function.
 *
 * The controller data structure must have been populated by fetching
 * data from the bridge controller over the serial API with \ref
 * zgw_restore_serial_data_read().  Relevant data will be copied from
 * the ctrl structure to zgw_backup_data.
 *
 * Only critical errors will cause the parser to fail, e.g., missing
 * critical data for the Z/IP Gateway itself.
 *
 * If a critical error occurs in the description of another network
 * node, e.g., missing a nodeid, the node will be "dropped" by the parser.
 * When the Z/IP Gateway process starts up, it will see the node in the
 * bridge controller and start interviewing it in the usual way.
 *
 * If a node exists in the ZGW Data File, but not in the bridge
 * controller, the parser will print out a warning message and drop
 * the node.
 *
 * The parser prints out indented status messages as it goes through
 * the JSON object.  Errors and warnings are not indented.
 *
 * \param zgw_backup_obj Pointer to a Restore DB data structure.
 * \param ctrl Pointer to a data structure representing the Z-Wave data in the bridge controller.
 * \return 0 on success, -1 on ctrl errors, -2 on zgw_backup_obj errors.
 */
int zgw_restore_parse_backup(json_object *zgw_backup_obj, const zw_controller_t *ctrl);

/**
@}
*/
#endif
