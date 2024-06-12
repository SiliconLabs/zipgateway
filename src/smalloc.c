/* © 2014 Silicon Laboratories Inc.
 */
#include <assert.h>
#include <smalloc.h>
#define ASSERT assert

/** Busy flag for the small memory device.
 * \ingroup small_mem
 *
 * Memory chunks are stored as flag-len1 | data1| flag-len2 | data2 | .....| flag-lenN | dataN
 *
 * flag-len is a uint8, and flag is either #CHUNK_USED or 0.
*/
#define CHUNK_USED 0x80
#define MIN_CHUNK_SIZE 4

/*
 * A better approach would be to store len1|len2|...|lenN| data1 | data2 | ...| dataN
 * However in the second approach fragmentation might be more difficult to handle.
 * */

/* Write a small memory chunk to storage and return a pointer to the
 * small memory chunk in the device storage. */
uint16_t smalloc(const small_memory_device_t* dev ,uint8_t datalen) {
  uint16_t p,start,end;
  uint8_t l,d;
  uint8_t reminder;

  ASSERT(datalen < 0x80);
  ASSERT(dev->offset > 0);

  p = dev->offset;
  end = (dev->offset) +  dev->size - 1;
  while((p+datalen) < end) {
    /*Find the number of consecutive free bytes */
    start = p;

    while( 1 ) {
      dev->read(p,1,&l);
      p = (p + (l & 0x7F) + 1);

      if( l & CHUNK_USED ) break;

      /*The end of the allocated area has 0 length*/
      if(l==0) {
        d=datalen | CHUNK_USED;
        dev->write(start,1,&d);

        d=0; /*Write the new end of list marker*/
        dev->write(start+1+datalen,1,&d);
        return start+1;
      }

      if(p >= end) {
        return 0;
      }

      /*We found an exact match, the +1 is because we need to have room for the length field */
      if( (datalen+1) == (p-start) ) {
        d = datalen | CHUNK_USED;
        dev->write(start,1,&d);
        return start+1;
      }

      /*
       * We found a number of free segments which we may divide into a new allocated segment
       * and a smaller free one.
       */
      if( (datalen+1)  <  (p-start) ) {
        reminder = (p-start) - (datalen+1);

        if(reminder < MIN_CHUNK_SIZE) {
          d = (p-start-1) | CHUNK_USED;
        } else {
          /*Divide length marker at the end */
          d=reminder-1;
          dev->write( start+datalen+1,1,&d );
          d = datalen | CHUNK_USED;
        }
        dev->write(start,1,&d);
        return start+1;
      }
    }
  }
  return 0;
}

void smfree(const small_memory_device_t* dev ,uint16_t ptr) {
  uint8_t c;
  if ((ptr > (dev->offset)) 
      && (ptr < (dev->offset +  dev->size))) {
    dev->read(ptr-1,1,&c);

    ASSERT(c & CHUNK_USED);
    c &= ~CHUNK_USED;
    dev->write(ptr-1,1,&c);
  } else {
    ASSERT(0);
    return;
  }
}


uint16_t smalloc_write(const small_memory_device_t* dev ,uint8_t datalen,void* data) {
  uint16_t ptr;
  ptr = smalloc(dev,datalen);
  if(ptr) {
      dev->write(ptr,datalen,data);
  }
  return ptr;
}

void smformat(const small_memory_device_t* dev) {
  uint8_t nul;
  nul=0;
  dev->write(dev->offset,1,&nul);
}
