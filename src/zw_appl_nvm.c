#include <stdint.h>
#include "Serialapi.h"

#ifdef NO_ZW_NVM

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern const char* linux_conf_nvm_file;
static int nvm_file = 0;

void zw_appl_nvm_init() {
  if(nvm_file==0) {
    nvm_file = open(linux_conf_nvm_file, O_RDWR | O_CREAT , 0644);
  }

  if(nvm_file<0) {
    fprintf(stderr, "Error opening NVM file %s\n",linux_conf_nvm_file);
    perror("");
    exit(1);
  }
}

void zw_appl_nvm_close(void) {
   close(nvm_file);
}

int
zw_appl_nvm_read(uint16_t start, void* dst, uint8_t size)
{
  lseek(nvm_file, start, SEEK_SET);
  if(read(nvm_file,dst,size) != size) {
    perror(__FUNCTION__);
    return 0;
  }
  return 1;
}


void
zw_appl_nvm_write(uint16_t start, const void* dst, uint8_t size)
{
  lseek(nvm_file, start, SEEK_SET);
  if(write(nvm_file,dst,size) != size) {
    perror(__FUNCTION__);
  }
}

#else
/**
 * Read NVM of Z-Wave module
 */
int
zw_appl_nvm_read(uint16_t start, void* dst, uint8_t size)
{
  return MemoryGetBuffer(start ,dst, size);
}

/**
 * Write NVM of Z-Wave module
 */
void
zw_appl_nvm_write(uint16_t start,const void* dst, uint8_t size)
{
  MemoryPutBuffer(start,(BYTE*)dst,size,0);
}
#endif

