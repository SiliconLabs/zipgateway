/* © 2014 Silicon Laboratories Inc.
 */
#include <stdlib.h>
#include <crc32alg.h>

void
reverseBitsInBuf(unsigned char *buf, unsigned int offset, unsigned int len)
{
  unsigned int i;

  for (i = offset; i < (len + offset); i++)
  {
    buf[i] = ((buf[i] << 4) & 0xF0) | ((buf[i] >> 4) & 0x0F);
    buf[i] = ((buf[i] << 2) & 0xCC) | ((buf[i] >> 2) & 0x33);
    buf[i] = ((buf[i] << 1) & 0xAA) | ((buf[i] >> 1) & 0x55);
  }
}

#if 1
/* chksum_crc32() -- to a given block, this one calculates the
 *        crc32-checksum until the length is
 *        reached. the crc32-checksum will be
 *        the result.
 */
unsigned long
chksum_crc32(unsigned char *buf, unsigned long len, unsigned long *crc_tab)
{
  register unsigned long crc;
//  unsigned  char crcmod[4];
  unsigned long i;

  crc = 0xFFFFFFFF;
  for (i = 0; i < len; i++)
  {
    crc = ((crc >> 8) & 0x00FFFFFF) ^ crc_tab[(crc ^ *buf++) & 0xFF];
  }
  return crc;
}

/* chksum_crc32gentab() --      to an array crc_tab[256], this one will
 *        calculate the crcTable for crc32-checksums.
 *        it is generated to the polynom [..]
 */
void
chksum_crc32gentab(unsigned long *crc_tab)
{
  unsigned long crc, poly;
  int i, j;

  poly = 0xEDB88320L;
  for (i = 0; i < 256; i++)
  {
    crc = i;
    for (j = 8; j > 0; j--)
    {
      if (crc & 1)
      {
        crc = (crc >> 1) ^ poly;
      }
      else
      {
        crc >>= 1;
      }
    }
    crc_tab[i] = crc;
  }
}
#endif

void
insertCRC(unsigned char *iobuf, unsigned long hexsize)
{
  unsigned long crc_tab[256];
  unsigned long i;
  unsigned long crc;
  //unsigned long crc2;

  unsigned char *crcbuf;

  crcbuf = (unsigned char*) malloc(hexsize + 1);
  if (crcbuf == NULL)
    exit(1);

  for (i = 0; i < hexsize; i++)
  {
    crcbuf[i] = (unsigned char) iobuf[i];
  }

  reverseBitsInBuf(crcbuf, 0, hexsize);

  chksum_crc32gentab(crc_tab);
  /* run crc over all but the last 4 bytes of the buffer */
  crc = chksum_crc32(crcbuf, hexsize - 4, crc_tab);
  //crc2 = crc32(crcbuf, hexsize-4, 0xFFFFFFFF);

  //printf("old CRC %lx new crc32 %lx\n",crc,crc2);

  iobuf[hexsize - 4] = (unsigned char) (0x000000FF & crc);
  iobuf[hexsize - 3] = (unsigned char) (0x000000FF & (crc >> 8));
  iobuf[hexsize - 2] = (unsigned char) (0x000000FF & (crc >> 16));
  iobuf[hexsize - 1] = (unsigned char) (0x000000FF & (crc >> 24));

  reverseBitsInBuf(iobuf, hexsize - 4, 4);
}
