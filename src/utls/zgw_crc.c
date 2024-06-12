/* © 2018 Silicon Laboratories Inc.
 */

#include "zgw_crc.h"
/*
 * CRC-16 verification
 */

uint16_t zgw_crc16(uint16_t crc16, uint8_t *data, unsigned long data_len)
{
  uint8_t crc_data;
  uint8_t bitmask;
  uint8_t new_bit;
  //printf("zgw_crc16: data_len = %u\r\n", data_len);
  while (data_len--)
  {
    crc_data = *data++;
    for (bitmask = 0x80; bitmask != 0; bitmask >>= 1)
    {
      /* Align test bit with next bit of the message byte, starting with msb. */
      new_bit = ((crc_data & bitmask) != 0) ^ ((crc16 & 0x8000) != 0);
      crc16 <<= 1;
      if (new_bit)
      {
        crc16 ^= CRC_POLY;
      }
    } /* for (bitMask = 0x80; bitMask != 0; bitMask >>= 1) */
  }
  return crc16;
}
