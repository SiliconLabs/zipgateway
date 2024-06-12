/* © 2014 Silicon Laboratories Inc.
 */

#ifndef SMALLOC_H_
#define SMALLOC_H_

#include <stdint.h>

/**
 * \defgroup small_mem Small Data Storage
 * \ingroup rd_data_store
 *
 * The small memory device  supports dynamic allocation of memory
 * blocks smaller than 0x80 bytes and operates with uint16_t pointers.
 *
 * It uses  a (relatively) larger  block of (more) static  memory that
 * can be read and written to at any valid address.
 *
 * @{
 */

/** Abstraction of a small dynamic-memory allocation device.
 */
typedef struct small_memory_device {
   /** Relative pointer to the start of the dynamic allocation area. */
  uint16_t offset;
   /** Size of the dynamic allocation area. */
  uint16_t size;
  uint8_t psize;

   /** Read function to access the underlying memory. */
  uint16_t (*read)(uint16_t offset,uint8_t len,void* data);
   /** Write function to access the underlying memory. */
  uint16_t (*write)(uint16_t offset,uint8_t len,void* data);
} small_memory_device_t;


/** Allocate memory from a small memory device.
 *
 * Locate free space in the small memory device and write a small
 * memory chunk to reserve the space.
 *
 * \param dev The small memory device to allocate from. 
 * \param datalen Length of the new memory.  Must be smaller than 0x80. 
 * \return A pointer to the data area of the small memory chunk.
 */
uint16_t smalloc(const small_memory_device_t* dev ,uint8_t datalen);

/** Allocate memory from a small memory device and write a data block to it.

 * \return A pointer to the newly written data in the memory device.
 * The returned pointer must be freed by caller using smfree().
 */
uint16_t smalloc_write(const small_memory_device_t* dev ,uint8_t datalen,void* data);

/**
 * Deallocate a previously allocated memory chunk.
 *
 * Mark the chunk as free in its control area.
 */
void smfree(const small_memory_device_t* dev ,uint16_t ptr);

/*Format the small memory device so it will be clean and ready for use */
void smformat(const small_memory_device_t* dev);

/**
@}
*/

#endif /* SMALLOC_H_ */
