/* © 2019 Silicon Laboratories Inc. */

#if !defined(ENDIAN_WRAP_H)
#define ENDIAN_WRAP_H

/**
 * The purpose of this file is to make the calls  be32toh and friends available on all platforms.
 */

#ifdef __APPLE__
#include<libkern/OSByteOrder.h>
#define htobe32(x) OSSwapHostToBigInt32(x)
#define htole32(x) OSSwapHostToLittleInt32(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)
#else
#include<endian.h>
#endif

#endif // ENDIAN_WRAP_H



