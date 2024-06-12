/* © 2014 Silicon Laboratories Inc.
 */
#ifndef DATASTORE_H_
#define DATASTORE_H_

#include "Bridge.h"
#include "Bridge_temp_assoc.h"
#include "Bridge_ip_assoc.h"
#include "CC_Gateway.h"
#include "memb.h"
#include "list.h"

/**
 * Persist ip association list in datastore, by replacing the old list
 * and writing a new one.
 */
void rd_data_store_persist_associations(list_t ip_association_table);

/**
 * Read ip association table from datastore
 *
 * @param ip_association_table List to add the read entries to.
 * @param ip_association_pool Memory pool to allocate the ipassociations from.
 */
void rd_datastore_unpersist_association(list_t ip_association_table, struct memb* ip_association_pool);


/**
 * @brief Write GW owned virtual nodes to the datastore
 *
 * @param nodelist Array of nodeid which are GW owned virtual nodes
 * @param node_count Number of element in nodelist
 */
void rd_datastore_persist_virtual_nodes(const nodeid_t* nodelist, size_t node_count);

/**
 * @brief Read GW owned virtual nodes from the datastore
 *
 * @param nodelist Pointer to array in which to store the nodes list
 * @param max_node_count Maximum element nodelist can hold.
 * @return Number of GW owned virtual nodes in the database
 */
size_t rd_datastore_unpersist_virtual_nodes(nodeid_t* nodelist, size_t max_node_count);


void rd_datastore_persist_gw_config(const Gw_Config_St_t* gw_cfg);
void rd_datastore_unpersist_gw_config(Gw_Config_St_t* gw_cfg);
void rd_datastore_persist_peer_profile(int index,const Gw_PeerProfile_St_t* profile);
void rd_datastore_unpersist_peer_profile(int index, Gw_PeerProfile_St_t* profile);

struct SPAN;
/**
 * @brief Persist the S2 SPAN table
 *
 * @param span_table SPAN table
 * @param span_table_size number of elements in the SPAN table
 */
void rd_datastore_persist_s2_span_table(const struct SPAN *span_table, size_t span_table_size);

/**
 * @brief Unpersist S2 SPAN table
 *
 * @param span_table SPAN table to unpersist into
 * @param span_table_size number of elements in the SPAN table
 */
void rd_datastore_unpersist_s2_span_table(struct SPAN *span_table, size_t span_table_size);

#endif /* DATASTORE_H_ */
