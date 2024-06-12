
#include "net/uip.h"

#include "lib/memb.h"
#include "lib/list.h"
#include <string.h>
#include "net/uip-packetqueue.h"

#define MAX_NUM_QUEUED_PACKETS 64
MEMB(packets_memb, struct uip_packetqueue_packet, MAX_NUM_QUEUED_PACKETS);

#define DEBUG 0
#if DEBUG
#include <printf.h>
#define PRINTF(...) printf(__VA_ARGS__)
#else
#ifdef __ASIX_C51__
#define PRINTF(x)
#else
#define PRINTF(...)
#endif
#endif

/*---------------------------------------------------------------------------*/
static void
packet_timedout(void *ptr)
{
  struct uip_packetqueue_packet *p = ptr;

  PRINTF("uip_packetqueue_free timed out %p\n", p);


  list_remove( (list_t) &(p->handle->list), p);
#ifdef __ASIX_C51__
    zfree(p->queue_buf);
#endif
  memb_free(&packets_memb, p);
}
/*---------------------------------------------------------------------------*/
void
uip_packetqueue_new(struct uip_packetqueue_handle *handle)
{
  PRINTF("uip_packetqueue_new %p\n", handle);
  handle->list = NULL;
}
/*---------------------------------------------------------------------------*/
struct uip_packetqueue_packet *
uip_packetqueue_alloc(struct uip_packetqueue_handle *handle,uint8_t* data,int len, clock_time_t lifetime)
{
  struct uip_packetqueue_packet* p;


   PRINTF("uip_packetqueue_alloc list=0x%x   %d\n",(unsigned int)handle,  list_length((list_t)&(handle->list)));

  p = memb_alloc(&packets_memb);
  if(p != NULL) {
    p->handle = handle;
#ifdef __ASIX_C51__
    p->queue_buf = zmalloc(len);
    if(p->queue_buf == NULL)
    {
    	printf("uip_packetqueue_alloc failed.\r\n");
    	memb_free(&packets_memb, p);
    	return NULL;
    }
#endif
    ctimer_set(&p->lifetimer, lifetime,
               packet_timedout, p);

    memcpy(p->queue_buf,data,len);
    p->queue_buf_len = len;

    list_add((list_t) &(handle->list), p);
  } else {
    PRINTF("uip_packetqueue_alloc failed\n");
  }
  return p;
}
/*---------------------------------------------------------------------------*/
void
uip_packetqueue_pop(struct uip_packetqueue_handle *handle)
{
  struct uip_packetqueue_packet* p;
  PRINTF("uip_packetqueue_pop , %d\n",list_length((list_t)&(handle->list)));

  p =list_head( (list_t) & (handle->list) );
  if(p != NULL) {
    ctimer_stop(&p->lifetimer);
#ifdef __ASIX_C51__
    zfree(p->queue_buf);
#endif
    memb_free(&packets_memb, p);
    list_pop( (list_t) &(handle->list));
  }
}
/*---------------------------------------------------------------------------*/
uint8_t *
uip_packetqueue_buf(struct uip_packetqueue_handle *h)
{
  struct uip_packetqueue_packet* p=list_head( (list_t) &h->list);
  return p != NULL? p->queue_buf: NULL;
}
/*---------------------------------------------------------------------------*/
uint16_t
uip_packetqueue_buflen(struct uip_packetqueue_handle *h)
{
  struct uip_packetqueue_packet* p = list_head((list_t) &h->list);
  return p != NULL? p->queue_buf_len: 0;
}
/*---------------------------------------------------------------------------*/
void
uip_packetqueue_set_buflen(struct uip_packetqueue_handle *h, uint16_t len)
{
  struct uip_packetqueue_packet* p=list_head((list_t) &h->list);

  if(p!= NULL) {
    p->queue_buf_len = len;
  }
}

int
uip_packetqueue_len(struct uip_packetqueue_handle *h)
{
  return list_length((list_t)&(h->list));
}

int uip_packetqueue_free_len(void)
{

#if 0
	printf("first_attempt_queue = %d\r\n", uip_packetqueue_len(&first_attempt_queue));
    printf("long_queue = %d\r\n", uip_packetqueue_len(&long_queue));
    printf("ipv6TxQueue = %d\r\n", uip_packetqueue_len(&ipv6TxQueue));
    printf("async_queue = %d\r\n", uip_packetqueue_len(&async_queue));
    printf("sslread_queue = %d\r\n", uip_packetqueue_len(&sslread_queue));
#endif

	return memb_free_count(&packets_memb);
}
/*---------------------------------------------------------------------------*/
