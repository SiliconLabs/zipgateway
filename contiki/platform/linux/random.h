#ifndef RANDOM_H_
#define RANDOM_H_
#include <stdint.h>
int dev_urandom(int len, uint8_t *buf);
#endif
