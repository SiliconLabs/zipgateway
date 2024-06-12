#include <stdio.h>
#include <stdint.h>

#define DATA_RECORD     0x00
#define END_OF_FILE     0x01
#define EXTENDED_LIN_ADR_RECORD 0x04

// this function opens the HEX-file, and fills an imaginary FLASH with data
// from the hex-file.
int convert_hex_to_bin(FILE *input, uint8_t *dst, size_t maxlen)
{
  unsigned int length, adrMsb, adrLsb, type, data, adr, chkSum;
  char         startOfLine;
#ifdef __GNUC__
  char lineFeed;
#endif
  unsigned long byteNo;
  unsigned char stopReading;
  unsigned int upperAdr;

  upperAdr = 0;
  stopReading = 0;


  printf("converting hex file to binary data\n");
  while (!stopReading)
  {
    fscanf(input, "\r%c%2x%2x%2x%2x", &startOfLine, &length, &adrMsb, &adrLsb, &type);  // read record information

    if (startOfLine != ':')      // check line start marker
    {
      printf("Wrong file type\n");
      printf("Received: %c%02X%02X%02X%02X\n",  startOfLine, length, adrMsb, adrLsb, type);
      return 0;
    }
//    printf("Received: %c%02X%02X%02X%02X\n",  startOfLine, length, adrMsb, adrLsb, type);
//    printf("length:%d\n", length);

    if (type == DATA_RECORD)      // if data in record
    {
      adr = (adrMsb << 8) | adrLsb;  // get flash address
      adr |= (upperAdr << 16);    // Add the extended adr field
//      printf("adr:%d\n", adr);
      for (byteNo = 0; byteNo < length; byteNo++)
      {
        fscanf(input, "%2x", &data);    // read data byte from file
//        printf("writing at %lu\n", adr+byteNo);
        if ((adr+byteNo) > maxlen) {
            printf("array over run\n");
        }
        dst[adr + byteNo] = (unsigned char)(data&0xFF);  // write into virtuel flash
      }
      fscanf(input, "%2x", &chkSum);  // read rest of information on line, chksum
    }
    else if (type == EXTENDED_LIN_ADR_RECORD)
    {
      fscanf(input, "\r%4x%2x", &upperAdr,&chkSum);  // read upperAdr and then the rest information, that is chksum
    }
    else if (type == END_OF_FILE)
    {
        //printf("Conversion done \n");
      stopReading = 1;
    }
    else
    {
      printf("Unkown type field in record\n");
      stopReading = 1;
    }
  }
  return adr + byteNo;
}
