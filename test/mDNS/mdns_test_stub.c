#include "mdns_test.h"

/* Contiki stubs */
u16_t
uip_htons(u16_t val)
{
  return UIP_HTONS(val);
}

u32_t
uip_htonl(u32_t val)
{
  return UIP_HTONL(val);
}