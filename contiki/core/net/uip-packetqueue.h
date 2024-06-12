#ifndef UIP_PACKETQUEUE_H
#define UIP_PACKETQUEUE_H

#include "sys/ctimer.h"
#include "lib/list.h"
#include "uipopt_ipv4.h"
#ifdef __ASIX_C51__
#undef data
#define data _data
#endif

struct uip_packetqueue_handle;

struct uip_packetqueue_packet {
  struct uip_ds6_queued_packet *next;
#ifdef __ASIX_C51__
  uint8_t *queue_buf;
#else
  uint8_t queue_buf[UIP_BUFSIZE - UIP_LLH_LEN];
#endif
  uint16_t queue_buf_len;
  struct ctimer lifetimer;
  struct uip_packetqueue_handle *handle;
};

struct uip_packetqueue_handle {
  struct uip_packetqueue_packet* list;
};

void uip_packetqueue_new(struct uip_packetqueue_handle *handle);


struct uip_packetqueue_packet *
uip_packetqueue_alloc(struct uip_packetqueue_handle *handle,uint8_t* data,int len, clock_time_t lifetime);


void
uip_packetqueue_pop(struct uip_packetqueue_handle *handle);


uint8_t *uip_packetqueue_buf(struct uip_packetqueue_handle *h);
uint16_t uip_packetqueue_buflen(struct uip_packetqueue_handle *h);
void uip_packetqueue_set_buflen(struct uip_packetqueue_handle *h, uint16_t len);
int uip_packetqueue_len(struct uip_packetqueue_handle *h);
int uip_packetqueue_free_len(void);

#endif /* UIP_PACKETQUEUE_H */
