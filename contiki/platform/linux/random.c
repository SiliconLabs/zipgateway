#include <stdint.h>
#include <stdio.h>
int dev_urandom(int len, uint8_t *buf)
{ 
  FILE *fp;

  fp = fopen("/dev/urandom", "r");
  size_t ret = fread(buf, 1, len, fp);
  if (ret < len) {
      printf("Error: Random number genration failed\n");
      fclose(fp);
      return 0;
  }
  fclose(fp);
  return 1;

}
