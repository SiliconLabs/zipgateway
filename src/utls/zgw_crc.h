/* © 2018 Silicon Laboratories Inc.
 */

#ifndef ZGW_CRC_H_
#define ZGW_CRC_H_

#include <stdint.h>

/**
 * CRC-16 verification
 * @param crc initial crc value
 * @param[in] data raw data
 * @param data_len length of raw data
 * @return return unsigned crc-16 value
 */

#define CRC_POLY          0x1021
#define CRC_INIT_VALUE    0x1D0F

uint16_t zgw_crc16(uint16_t crc16, uint8_t *data, unsigned long data_len);

#endif