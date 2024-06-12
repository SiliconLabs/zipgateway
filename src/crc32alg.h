/* © 2017 Silicon Laboratories Inc.
 */
/*
 * crc32.h
 *
 *  Created on: Apr 22, 2015
 *      Author: aes
 */

#ifndef CRC32_H_
#define CRC32_H_


/**
 * Calculate the CRC32 checksum
 */
#include <stdint.h>

uint32_t
crc32( const void *buf, uint32_t size,uint32_t crc);

#endif /* CRC32_H_ */
