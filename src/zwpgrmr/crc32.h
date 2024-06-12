/* © 2014 Silicon Laboratories Inc.
 */

 void
reverseBitsInBuf(unsigned char *buf, unsigned int offset, unsigned int len);

unsigned long
chksum_crc32(unsigned char *buf, unsigned long len, unsigned long *crc_tab);

void
chksum_crc32gentab(unsigned long *crc_tab);

void
insertCRC(unsigned char *iobuf, unsigned long hexsize);
