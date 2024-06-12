

#if !defined(RD_DATASTORE_EEPROM)
#define RD_DATASTORE_EEPROM

#include "RD_DataStore.h"
#include "RD_DataStore_legacy.h"
#include "DataStore.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define ZW_MAX_EEPROM_NODES 232

/**
 * @brief Get the magic byte of an eeprom file opened with eeprom_init
 */
uint32_t rd_eeprom_magic_get();

/**
 * @brief Get the home id of an eeprom file opened with eeprom_init
 */
uint32_t rd_eeprom_homeid_get();

/**
 * @brief Get the node id of an eeprom file opened with eeprom_init
 */
uint8_t rd_eeprom_nodeid_get();

/**
 * @brief Get the verison byte of an eeprom file opened with eeprom_init
 */
uint32_t rd_eeprom_version_get_legacy();

/**
 * @brief Read node database entry form a eerpom file version XX.XX
 * 
 * This function reads the eeprom opened with eeprom_init and converts
 * the data into the current in mememory format. At the time of writing
 * format v2.3 is equivalent to the in memory format.
 * 
 * Theese functions contains the core of the eeprom converters form
 * RD_Datastore.c of a ZGW 7.13.01
 * 
 * @param nodeID Node id of the entry to read
 * @return rd_node_database_entry_t* Newly allocated entry with data filled. must be freed with rd_data_store_mem_free
 */
rd_node_database_entry_t *rd_data_store_read_2_0(uint8_t nodeID);
/** @ref rd_data_store_read_2_0 */
rd_node_database_entry_t *rd_data_store_read_2_1(uint8_t nodeID); 
/** @ref rd_data_store_read_2_0 */
rd_node_database_entry_t *rd_data_store_read_2_2(uint8_t nodeID);
/** @ref rd_data_store_read_2_0 */
rd_node_database_entry_t *rd_data_store_read_2_3(uint8_t nodeID);


/**
 * @brief Convert from 2.6x gateways to eeprom format 2.0
 * 
 * NOTE this does inplace convertion
 * 
 * @param old_MyNodeID Nodeid of GW in eeprom
 * @return TRUE on success
 */
bool data_store_convert_none_to_2_0(uint8_t old_MyNodeID);

/**
 * @brief Perform validation of the eeprom file opened with eeprom_init
 * 
 * @param eeprom_version 
 * @return true Eeprom appears to be valid
 * @return false In consistencies has been found in the eeprom.
 */
bool rd_data_store_validate(int eeprom_version);

/**
 * @brief Read the ip association table form the eeprom file opened with
 * init_eeprom.
 * 
 * @param ip_association_table List to add the associations to
 * @param ip_association_pool Memory pool to allocate the associations from
 */
void eeprom_datastore_unpersist_association(list_t ip_association_table, struct memb* ip_association_pool);

/**
 * @brief Read the temporary virtual node list from the eeprom file.
 * 
 * @param nodelist Array to insert the list into
 * @param max_node_count Number of elements the array can hold
 * @return size_t Number of virtual nodes read.
 */
size_t eeprom_datastore_unpersist_virtual_nodes(nodeid_t* nodelist, size_t max_node_count);

/**
 * @brief Perform validation of a node database entries
 * 
 * This function checks various fields in the node database entry structure for
 * invalid data
 * 
 * @param n In memory entry to check
 * @param nodeid Node id of the entry 
 * @return true Entry appears to be valid.
 * @return false Entry contains invalid data. 
 */

bool node_sanity_check(rd_node_database_entry_t *n, int nodeid);

#ifdef __cplusplus
}
#endif

#endif // RD_DATASTORE_EEPROM
