/* © 2020 Silicon Laboratories Inc.  */

#if !defined(RD_DATASTORE_LEGACY)
#define RD_DATASTORE_LEGACY

#include "smalloc.h"
#include "Bridge_temp_assoc.h"
#include "Bridge_ip_assoc.h"

/** Version of the eeprom.dat file format, major.
 * \see #RD_MAGIC_V1
 */
#define RD_EEPROM_VERSION_MAJOR 2
/** Version of the eeprom.dat file format, minor.
 * \see #RD_MAGIC_V1
 */
#define RD_EEPROM_VERSION_MINOR 3

/** Obsoleted eeprom.dat magic number.  This magic indicates that the
 * hdr struct is unversioned.  The gateway will assume v2.61 format
 * and try to convert the data. */
#define RD_MAGIC_V0 0x491F00E5

/** The eeprom.dat magic number.  This magic indicates that the hdr struct
 * includes a versioning field. */
#define RD_MAGIC_V1 0x94F100E5

/** Size of the dynamic allocation area in the RD data store.
 * 
 * \ingroup rd_data_store
*/
#define RD_SMALLOC_SIZE 0x5E00

/** Layout of the RD persistent data store.
 * \ingroup rd_data_store
 *
 * The data store contains the actual static header, a list of node
 * pointers, information about the virtual nodes, and information
 * about associations.  It also contains a small area for dynamic
 * allocation of node-related information.
 *
 * */
typedef struct rd_eeprom_static_hdr {
  uint32_t magic;
   /** Home ID of the stored gateway. */
  uint32_t homeID;
   /** Node ID of the stored gateway. */
  uint8_t  nodeID;
  uint8_t version_major;
  uint8_t version_minor;
  uint32_t flags;
  uint16_t node_ptrs[ZW_CLASSIC_MAX_NODES];
  /** The area used for dynamic allocations with the \ref small_mem. */
  uint8_t  smalloc_space[RD_SMALLOC_SIZE];
  uint8_t temp_assoc_virtual_nodeid_count;
  uint8_t temp_assoc_virtual_nodeids[MAX_CLASSIC_TEMP_ASSOCIATIONS];
  uint16_t association_table_length;
  uint8_t association_table[IP_ASSOCIATION_TABLE_EEPROM_SIZE];
} rd_eeprom_static_hdr_t;

/** Read bytes from offset in the data store to data.
 * \ingroup rd_data_store
 *
 * \param offset Offset to read from, measured from the start of the static header.
 * \param len How many bytes to copy.
 * \param data Pointer to a buffer that can hold at least len bytes.
 * \return This function always returns len.
 */
uint16_t rd_eeprom_read(uint16_t offset,uint8_t len,void* data);

/**
 * Write len bytes from data to offset offset in the data store.
 * \ingroup rd_data_store
 *
 * \param offset Offset to write into, measured from the start of the static header.
 * \param len How many bytes to write.
 * \param data Pointer to the data to write.
 * \return This function always returns len.
 */
uint16_t rd_eeprom_write(uint16_t offset,uint8_t len,void* data);


#endif // RD_DATASTORE_LEGACY
