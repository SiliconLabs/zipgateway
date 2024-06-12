/* © 2014 Silicon Laboratories Inc.
 */


#ifndef ZWPGRMR_H_
#define ZWPGRMR_H_



#include "zpgp.h"

/**
 * \defgroup libzpg
 * @{*/


/**
 * Write SDRAM.
 * \param addr start address to write
 * \param len length to read.
 * \param buf output buffer
 *
 * \return -1 on error
 */
int zpg_bulk_write_SRAM(zw_pgmr_t* p,u16_t addr, u16_t len,const  u8_t*buf);

/**
 *  Read a region of SRAM.
 * \param addr start address to read
 * \param len length to read.
 * \param buf output buffer
 *
 * \return -1 on error
 */
int zpg_bulk_read_SRAM(zw_pgmr_t* p, u16_t addr, u16_t len,u8_t* buf);

/**
 * Read a single sector from flash.
 * \param sector Sector to read
 * \param buf buffer to store data in. This buffer must be able to contain the whole sector.
 *
 * \return -1 on error
 */
int zpg_read_flash_sector(zw_pgmr_t* p,u8_t sector,u8_t*buf);


/**
 * Programmer read chip.
 * \param data A memory buffer that contains the binary data. It must be at least the same size as the flash.
 * \param offset to start from
 * \param length of data to read.
 * \raturn the number of bytes actually read
 *
 * Note that this function is sector based. This means that both offset and length is rounded down
 * to n mod block_size (2k)
 */
int zpg_read_chip(zw_pgmr_t* p, u8_t* data,int offset, int length);


struct zpg_interface {
  const char* interface_name;
  int (*parse_dev_string)(zw_pgmr_t* p,const char* dev_string);
  const char* dev_string_help;
};


/**
 * Initialize the programmer.
 * \return -1 on failure
 */
int zpg_init(zw_pgmr_t* p);


/**
 * Program the CHIP.
 * Programmer init must be called before this.
 * \param data A memory buffer that contains the binary data. It must be at least the same size as the flash.
 * \param size Size of the FLASH
 */
int zpg_program_chip(zw_pgmr_t* p, u8_t* data, int size);

/**@}*/


#endif /* ZWPGRMR_H_ */
