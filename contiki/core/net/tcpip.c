/*
 * Copyright (c) 2004, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 *
 * $Id: tcpip.c,v 1.28 2010/05/01 13:04:31 joxe Exp $
 */
/**
 * \file
 *         Code for tunnelling uIP packets over the Rime mesh routing module
 *
 * \author  Adam Dunkels <adam@sics.se>\author
 * \author  Mathilde Durvy <mdurvy@cisco.com> (IPv6 related code)
 * \author  Julien Abeille <jabeille@cisco.com> (IPv6 related code)
 */
#ifdef ZIP_NATIVE
#include <string_adaptation.h>
#else
#include <string.h>
#endif


#include "contiki-net.h"

#include "net/uip-split.h"

#include "net/uip-packetqueue.h"

#if UIP_CONF_IPV6
#include "net/uip-nd6.h"
#include "net/uip-ds6.h"
#endif

#ifndef __ASIX_C51__
#define DEBUG DEBUG_FULL
#endif
#include "net/uip-debug.h"

#if RPL_CONF_NON_STORING
#include "net/rpl/rpl-sr.h"
#endif

#include "ZIP_Router.h"

#if UIP_LOGGING
//#include <printf.h>
void uip_log(char *msg);
#define UIP_LOG(m) uip_log(m)
#else
#define UIP_LOG(m)
#endif

#ifdef __C51__
#define data _data
#endif

#ifdef __ASIX_C51__
//#define PRINTF	printf   //Enabled Debugging prints
#define PRINTF
#endif
#define UIP_ICMP_BUF ((struct uip_icmp_hdr *)&uip_buf[UIP_LLIPH_LEN + uip_ext_len])
#define UIP_IP_BUF ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP_TCP_BUF ((struct uip_tcpip_hdr *)&uip_buf[UIP_LLH_LEN])

#ifdef UIP_FALLBACK_INTERFACE
extern struct uip_fallback_interface UIP_FALLBACK_INTERFACE;
#endif
#if UIP_CONF_IPV6_RPL
void rpl_init(void);
#endif
process_event_t tcpip_event = 0;
#if UIP_CONF_ICMP6
process_event_t tcpip_icmp6_event = 0;
#endif /* UIP_CONF_ICMP6 */

static uint8_t tcpip_ready;


/*static struct tcpip_event_args ev_args;*/

/*periodic check of active connections*/
static struct etimer periodic;

#if UIP_CONF_IPV6 && UIP_CONF_IPV6_REASSEMBLY
/*timer for reassembly*/
extern struct etimer uip_reass_timer;
#endif

#if UIP_TCP
/**
 * \internal Structure for holding a TCP port and a process ID.
 */
struct listenport {
  u16_t port;
  struct process *p;
};

static struct internal_state {
  struct listenport listenports[UIP_LISTENPORTS];
  struct process *p;
} s;
#endif

enum {
  TCP_POLL,
  UDP_POLL,
  PACKET_INPUT
};

/* Called on IP packet output. */
#if UIP_CONF_IPV6

static u8_t (* outputfunc)(uip_lladdr_t *a);

extern uip_ds6_route_t uip_ds6_routing_table[];
void dump_routes() {
  int i;
  PRINTF("Routes:\n");
  for(i = 0; i < UIP_DS6_ROUTE_NB; i++)
  {
    if(uip_ds6_routing_table[i].isused)
    {
      PRINT6ADDR(&uip_ds6_routing_table[i].ipaddr);
      PRINTF(" via ");
      PRINT6ADDR(&uip_ds6_routing_table[i].nexthop);
      PRINTF("\n");
    }
  }

}

u8_t
tcpip_output(uip_lladdr_t *a)
{
  int ret;

  ZW_LOG(I, 42);
  ZW_DEBUG_SEND_NUM(uip_buf[UIP_LLH_LEN]);
  if(outputfunc != NULL) {
    ret = outputfunc(a);
    return ret;
  }
  UIP_LOG("tcpip_output: Use tcpip_set_outputfunc() to set an output function");
  return 0;
}

void
tcpip_set_outputfunc(u8_t (*f)(uip_lladdr_t *))
{
  outputfunc = f;
}
#else

static u8_t (* outputfunc)(void);
u8_t
tcpip_output(void)
{
  if(outputfunc != NULL) {
    return outputfunc();
  }
  UIP_LOG("tcpip_output: Use tcpip_set_outputfunc() to set an output function");
  return 0;
}

void
tcpip_set_outputfunc(u8_t (*f)(void))
{
  outputfunc = f;
}
#endif

#if UIP_CONF_IP_FORWARD
unsigned char tcpip_is_forwarding; /* Forwarding right now? */
#endif /* UIP_CONF_IP_FORWARD */

PROCESS(tcpip_process, "TCP/IP stack");

/*---------------------------------------------------------------------------*/
static void
start_periodic_tcp_timer(void)
{
  if(etimer_expired(&periodic)) {
    etimer_restart(&periodic);
  }
}
/*---------------------------------------------------------------------------*/
static void
check_for_tcp_syn(void)
{
  /* This is a hack that is needed to start the periodic TCP timer if
     an incoming packet contains a SYN: since uIP does not inform the
     application if a SYN arrives, we have no other way of starting
     this timer.  This function is called for every incoming IP packet
     to check for such SYNs. */
#define TCP_SYN 0x02
  if(UIP_IP_BUF->proto == UIP_PROTO_TCP &&
     (UIP_TCP_BUF->flags & TCP_SYN) == TCP_SYN) {
    start_periodic_tcp_timer();
  }
}
/*---------------------------------------------------------------------------*/
static void
packet_input(void)
{
#if UIP_CONF_IP_FORWARD
  if(uip_len > 0) {
    tcpip_is_forwarding = 1;
    if(uip_fw_forward() == UIP_FW_LOCAL) {
      tcpip_is_forwarding = 0;
      check_for_tcp_syn();
      uip_input();
      if(uip_len > 0) {
#if UIP_CONF_TCP_SPLIT
        uip_split_output();
#else /* UIP_CONF_TCP_SPLIT */
#if UIP_CONF_IPV6
        tcpip_ipv6_output();
#else
	PRINTF("tcpip packet_input forward output len %d\n", uip_len);
        tcpip_output();
#endif
#endif /* UIP_CONF_TCP_SPLIT */
      }
    }
    tcpip_is_forwarding = 0;
  }
#else /* UIP_CONF_IP_FORWARD */
  if(uip_len > 0) {
    check_for_tcp_syn();
    uip_input();
    if(uip_len > 0) {
#if UIP_CONF_TCP_SPLIT
      uip_split_output();
#else /* UIP_CONF_TCP_SPLIT */
#if UIP_CONF_IPV6
      tcpip_ipv6_output();
#else
      PRINTF("tcpip packet_input output len %d\n", uip_len);
      tcpip_output();
#endif
#endif /* UIP_CONF_TCP_SPLIT */
    }
  }
#endif /* UIP_CONF_IP_FORWARD */
}
/*---------------------------------------------------------------------------*/
#if UIP_TCP
#if UIP_ACTIVE_OPEN
struct uip_conn *
tcp_connect(uip_ipaddr_t *ripaddr, u16_t port, void *appstate)
{
  struct uip_conn *c;

  c = uip_connect(ripaddr, port);
  if(c == NULL) {
    return NULL;
  }

  c->appstate.p = PROCESS_CURRENT();
  c->appstate.state = appstate;

  tcpip_poll_tcp(c);

  return c;
}
#endif /* UIP_ACTIVE_OPEN */
/*---------------------------------------------------------------------------*/
void
tcp_unlisten(u16_t port)
{
  static unsigned char i;
  struct listenport *l;

  l = s.listenports;
  for(i = 0; i < UIP_LISTENPORTS; ++i) {
    if(l->port == port &&
       l->p == PROCESS_CURRENT()) {
      l->port = 0;
      uip_unlisten(port);
      break;
    }
    ++l;
  }
}
/*---------------------------------------------------------------------------*/
void
tcp_listen(u16_t port)
{
  static unsigned char i;
  struct listenport *l;

  l = s.listenports;
  for(i = 0; i < UIP_LISTENPORTS; ++i) {
    if(l->port == 0) {
      l->port = port;
      l->p = PROCESS_CURRENT();
      uip_listen(port);
      break;
    }
    ++l;
  }
}
/*---------------------------------------------------------------------------*/
void
tcp_attach(struct uip_conn *conn,
	   void *appstate)
{
  register uip_tcp_appstate_t *s;

  s = &conn->appstate;
  s->p = PROCESS_CURRENT();
  s->state = appstate;
}

#endif /* UIP_TCP */
/*---------------------------------------------------------------------------*/
#if UIP_UDP
void
udp_attach(struct uip_udp_conn *conn,
	   void *appstate)
{
  register uip_udp_appstate_t *s;

  s = &conn->appstate;
  s->p = PROCESS_CURRENT();
  s->state = appstate;
}
/*---------------------------------------------------------------------------*/
struct uip_udp_conn *
udp_new(const uip_ipaddr_t *ripaddr, u16_t port, void *appstate)
{
  struct uip_udp_conn *c;
  uip_udp_appstate_t *s;

  c = uip_udp_new(ripaddr, port);
  if(c == NULL) {
    return NULL;
  }

  s = &c->appstate;
  s->p = PROCESS_CURRENT();
  s->state = appstate;

  return c;
}
/*---------------------------------------------------------------------------*/
struct uip_udp_conn *
udp_broadcast_new(u16_t port, void *appstate)
{
  uip_ipaddr_t addr;
  struct uip_udp_conn *conn;

#if UIP_CONF_IPV6
  uip_create_linklocal_allnodes_mcast(&addr);
#else
  uip_ipaddr(&addr, 255,255,255,255);
#endif /* UIP_CONF_IPV6 */
  conn = udp_new(&addr, port, appstate);
  if(conn != NULL) {
    udp_bind(conn, port);
  }
  return conn;
}
#endif /* UIP_UDP */
/*---------------------------------------------------------------------------*/
#if UIP_CONF_ICMP6
u8_t
icmp6_new(void *appstate) {
  if(uip_icmp6_conns.appstate.p == PROCESS_NONE) {
    uip_icmp6_conns.appstate.p = PROCESS_CURRENT();
    uip_icmp6_conns.appstate.state = appstate;
    return 0;
  }
  return 1;
}

void
tcpip_icmp6_call(u8_t type)
{
  if(uip_icmp6_conns.appstate.p != PROCESS_NONE) {
    /* XXX: This is a hack that needs to be updated. Passing a pointer (&type)
       like this only works with process_post_synch. */
    process_post_synch(uip_icmp6_conns.appstate.p, tcpip_icmp6_event, &type);
  }
  return;
}
#endif /* UIP_CONF_ICMP6 */
/*---------------------------------------------------------------------------*/
static void
eventhandler(process_event_t ev, process_data_t data)
{
#if UIP_TCP
  static unsigned char i;
  register struct listenport *l;
#endif /*UIP_TCP*/
  struct process *p;

  switch(ev) {
    case PROCESS_EVENT_EXITED:
      /* This is the event we get if a process has exited. We go through
         the TCP/IP tables to see if this process had any open
         connections or listening TCP ports. If so, we'll close those
         connections. */

      p = (struct process *)data;
#if UIP_TCP
      l = s.listenports;
      for(i = 0; i < UIP_LISTENPORTS; ++i) {
        if(l->p == p) {
          uip_unlisten(l->port);
          l->port = 0;
          l->p = PROCESS_NONE;
        }
        ++l;
      }

      {
        register struct uip_conn *cptr;

        for(cptr = &uip_conns[0]; cptr < &uip_conns[UIP_CONNS]; ++cptr) {
          if(cptr->appstate.p == p) {
            cptr->appstate.p = PROCESS_NONE;
            cptr->tcpstateflags = UIP_CLOSED;
          }

        }

      }
#endif /* UIP_TCP */
#if UIP_UDP
      {
        register struct uip_udp_conn *cptr;
        for(cptr = &uip_udp_conns[0];
            cptr < &uip_udp_conns[UIP_UDP_CONNS]; ++cptr) {
          if(cptr->appstate.p == p) {
            cptr->lport = 0;
          }
        }

      }
#endif /* UIP_UDP */
      break;

    case PROCESS_EVENT_TIMER:
      /* We get this event if one of our timers have expired. */
      {
        /* Check the clock so see if we should call the periodic uIP
           processing. */
        if(data == &periodic &&
           etimer_expired(&periodic)) {
#if UIP_TCP
          for(i = 0; i < UIP_CONNS; ++i) {
            if(uip_conn_active(i)) {
              /* Only restart the timer if there are active
                 connections. */
              etimer_restart(&periodic);
              uip_periodic(i);
#if UIP_CONF_IPV6
              tcpip_ipv6_output();
#else
              if(uip_len > 0) {
		PRINTF("tcpip_output from periodic len %d\n", uip_len);
               tcpip_output();
		PRINTF("tcpip_output after periodic len %d\n", uip_len);
              }
#endif /* UIP_CONF_IPV6 */
            }
          }
#endif /* UIP_TCP */
#if UIP_CONF_IP_FORWARD
          uip_fw_periodic();
#endif /* UIP_CONF_IP_FORWARD */
        }

#if UIP_CONF_IPV6
#if UIP_CONF_IPV6_REASSEMBLY
        /*
         * check the timer for reassembly
         */
        if(data == &uip_reass_timer &&
           etimer_expired(&uip_reass_timer)) {
          uip_reass_over();
          tcpip_ipv6_output();
        }
#endif /* UIP_CONF_IPV6_REASSEMBLY */
        /*
         * check the different timers for neighbor discovery and
         * stateless autoconfiguration
         */
        /*if(data == &uip_nd6_timer_periodic &&
           etimer_expired(&uip_nd6_timer_periodic)) {
          uip_nd6_periodic();
          tcpip_ipv6_output();
        }*/
#if !UIP_CONF_ROUTER
        if(data == &uip_ds6_timer_rs &&
           etimer_expired(&uip_ds6_timer_rs)){
          uip_ds6_send_rs();
          tcpip_ipv6_output();
        }
#endif /* !UIP_CONF_ROUTER */
#ifdef ZIP_ND6
        if(data == &uip_ds6_timer_periodic &&
           etimer_expired(&uip_ds6_timer_periodic)){
          ASSERT(uip_len == 0);
          uip_ds6_periodic();
          tcpip_ipv6_output();

          if(!tcpip_ready && !uip_ds6_dad_in_progress) {
            tcpip_ready = 1;
            process_post(PROCESS_BROADCAST,tcpip_event,(void*)TCP_READY);
          }
        }
#endif /* #ifdef ZIP_ND6 */
#endif /* UIP_CONF_IPV6 */
      }
      break;

#if UIP_TCP
    case TCP_POLL:
      if(data != NULL) {
        uip_poll_conn(data);
#if UIP_CONF_IPV6
        tcpip_ipv6_output();
#else /* UIP_CONF_IPV6 */
        if(uip_len > 0) {
	  PRINTF("tcpip_output from tcp poll len %d\n", uip_len);
          tcpip_output();
        }
#endif /* UIP_CONF_IPV6 */
        /* Start the periodic polling, if it isn't already active. */
        start_periodic_tcp_timer();
      }
      break;
#endif /* UIP_TCP */
#if UIP_UDP
    case UDP_POLL:
      if(data != NULL) {
        uip_udp_periodic_conn(data);
#if UIP_CONF_IPV6
        tcpip_ipv6_output();
#else
        if(uip_len > 0) {
          tcpip_output();
        }
#endif /* UIP_UDP */
      }
      break;
#endif /* UIP_UDP */

    case PACKET_INPUT:
      packet_input();
      break;
  };
}
/*---------------------------------------------------------------------------*/
void
tcpip_input(void)
{
#if 0
  {
      printf("src ipv6: ");
      uip_debug_ipaddr_print(&UIP_IP_BUF->srcipaddr);
      printf("\r\n");

      printf("dst ipv6: ");
      uip_debug_ipaddr_print(&UIP_IP_BUF->destipaddr);
      printf("\r\n");  
  }
#endif
  process_post_synch(&tcpip_process, PACKET_INPUT, NULL);
  uip_len = 0;
#if UIP_CONF_IPV6
  uip_ext_len = 0;
#endif /*UIP_CONF_IPV6*/
}
/*---------------------------------------------------------------------------*/
#if UIP_CONFIG_SOURCE_ROUTING
#define DISABLE_NS
void
tcpip_ipv6_output(void)
{
  uip_ipaddr_t* nexthop;
  rpl_sr* sr;

  if(uip_len == 0)
    return;
  if(uip_len > UIP_LINK_MTU){
    UIP_LOG("tcpip_ipv6_output: Packet to big");
    uip_len = 0;
    return;
  }
  if(uip_is_addr_unspecified(&UIP_IP_BUF->destipaddr)){
    UIP_LOG("tcpip_ipv6_output: Destination address unspecified");
    uip_len = 0;
    return;
  }

  if (uip_is_4to6_addr(&UIP_IP_BUF->destipaddr)) {
    tcpip_output(NULL);
    uip_len = 0;
    return
  }

  if(uip_is_addr_mcast(&UIP_IP_BUF->destipaddr)) {
    tcpip_output(0);
    uip_len = 0;
    return;
  }

  if(uip_ds6_is_addr_onlink(&UIP_IP_BUF->destipaddr)){
    nexthop = &UIP_IP_BUF->destipaddr;
    goto nexthop_done;
  }

  /*
   * FIXME uip_is_ip_sr_prefix(&UIP_IP_BUF->destipaddr) && ...
   * Insert source routing header if it is not already there
   */
  if(UIP_IP_BUF->proto != UIP_PROTO_ROUTING) {
    /* Try to get a source route */
    uip_ds6_addr_t* a;
    /*FIXME I'm not sure that this will always work... this should
     * be my address in the prefix announced by rpl. */
    a = uip_ds6_get_global(ADDR_PREFERRED);
    sr = rpl_sr_get_route(&(a->ipaddr), &UIP_IP_BUF->destipaddr);
    if(sr) {
      int i;
      PRINTF("Source route from ");
      PRINT6ADDR(&UIP_IP_BUF->srcipaddr);
      PRINTF(" to ");
      PRINT6ADDR( &UIP_IP_BUF->destipaddr);
      PRINTF("\n");

      for(i=0; i < sr->count; i++) {
        PRINTF("HOP %x\n",sr->addresses[i*RPL_SR_SH]);
      }
      /* This inserts a routing header in uip_buf*/
      rpl_sr_add_hdr(sr);
      nexthop = &UIP_IP_BUF->destipaddr;
      goto nexthop_done;
    }
  }

  if(UIP_IP_BUF->proto == UIP_PROTO_ROUTING) {
    nexthop = &UIP_IP_BUF->destipaddr;
    goto nexthop_done;
  }
  /*Route table lookup */
  {
    uip_ds6_route_t* locrt;
    locrt = uip_ds6_route_lookup(&UIP_IP_BUF->destipaddr);
    if(locrt) {
      nexthop = &locrt->nexthop;
      goto nexthop_done;
    }
  }

  /*Default route choose */
  nexthop = uip_ds6_defrt_choose();

  if(nexthop)
    goto nexthop_done;


  PRINT6ADDR(&UIP_IP_BUF->destipaddr);
  PRINTF("tcpip_ipv6_output: Destination off-link but no route\n");
  ZW_LOG(I, 17);
  uip_len = 0;
  return;
nexthop_done:


#ifdef DISABLE_NS
    {
      /*This hack sets the destination equal to the last byte if the nexthop address,
       * This does not work for the ZIP router. */
      uip_lladdr_t ll;
      memset(&ll,0,sizeof(ll));
      ll.addr[0] = nexthop->u8[15];
      tcpip_output(&ll);
      uip_len = 0;
      return;
    }

#else
    if((nbr = uip_ds6_nbr_lookup(nexthop)) == NULL) {
      PRINTF("Neighbor: ");
      PRINT6ADDR(nexthop);
      PRINTF("not in cache.\n");

      if((nbr = uip_ds6_nbr_add(nexthop, NULL, 0, NBR_INCOMPLETE)) == NULL) {
        uip_len = 0;
        return;
      } else {
#if UIP_CONF_IPV6_QUEUE_PKT
        /* copy outgoing pkt in the queuing buffer for later transmmit */
        if(uip_packetqueue_alloc(&nbr->packethandle, UIP_DS6_NBR_PACKET_LIFETIME) != NULL) {
          memcpy(uip_packetqueue_buf(&nbr->packethandle), UIP_IP_BUF, uip_len);
          uip_packetqueue_set_buflen(&nbr->packethandle, uip_len);
        }
#endif
      /* RFC4861, 7.2.2:
       * "If the source address of the packet prompting the solicitation is the
       * same as one of the addresses assigned to the outgoing interface, that
       * address SHOULD be placed in the IP Source Address of the outgoing
       * solicitation.  Otherwise, any one of the addresses assigned to the
       * interface should be used."*/
       if(uip_ds6_is_my_addr(&UIP_IP_BUF->srcipaddr)){
          uip_nd6_ns_output(&UIP_IP_BUF->srcipaddr, NULL, &nbr->ipaddr);
        } else {
          uip_nd6_ns_output(NULL, NULL, &nbr->ipaddr);
        }

        stimer_set(&(nbr->sendns), uip_ds6_if.retrans_timer / 1000);
        nbr->nscount = 1;
      }
    } else {
      if(nbr->state == NBR_PERMANTLY_UNREACHABLE) {
        PRINTF("tcpip_ipv6_output: destination is permanently unreachable\n");
        uip_icmp6_error_output(ICMP6_DST_UNREACH,ICMP6_DST_UNREACH_ADDR, 0);
        return;
      }

      if (nbr->state == NBR_INCOMPLETE){
        PRINTF("tcpip_ipv6_output: nbr cache entry incomplete\n");
        ZW_LOG(I, 18);
#if UIP_CONF_IPV6_QUEUE_PKT
        /* copy outgoing pkt in the queuing buffer for later transmmit and set
           the destination nbr to nbr */
        if(uip_packetqueue_alloc(&nbr->packethandle, UIP_DS6_NBR_PACKET_LIFETIME) != NULL) {
          memcpy(uip_packetqueue_buf(&nbr->packethandle), UIP_IP_BUF, uip_len);
          uip_packetqueue_set_buflen(&nbr->packethandle, uip_len);
        }
        /*        memcpy(nbr->queue_buf, UIP_IP_BUF, uip_len);
                  nbr->queue_buf_len = uip_len;*/
        uip_len = 0;
#endif /*UIP_CONF_IPV6_QUEUE_PKT*/
        return;
      }
      /* if running NUD (nbc->state == STALE, DELAY, or PROBE ) keep
         sending in parallel see rfc 4861 Node behavior in section 7.7.3*/

      if (nbr->state == NBR_STALE){
        nbr->state = NBR_DELAY;
        stimer_set(&(nbr->reachable),
                  UIP_ND6_DELAY_FIRST_PROBE_TIME);
        nbr->nscount = 0;
        PRINTF("tcpip_ipv6_output: nbr cache entry stale moving to delay\n");
        ZW_LOG(I, 19);
      }

      stimer_set(&(nbr->sendns),
                uip_ds6_if.retrans_timer / 1000);

      PRINTF(" L2 addr (UNICAST) ");
      PRINTLLADDR(&(nbr->lladdr));
      PRINTF("\n");

      tcpip_output(&(nbr->lladdr));

#if UIP_CONF_IPV6_QUEUE_PKT
      /* Send the queued packets from here, may not be 100% perfect though.
       * This happens in a few cases, for example when instead of receiving a
       * NA after sendiong a NS, you receive a NS with SLLAO: the entry moves
       *to STALE, and you must both send a NA and the queued packet
       */
      /*      if(nbr->queue_buf_len != 0) {
        uip_len = nbr->queue_buf_len;
        memcpy(UIP_IP_BUF, nbr->queue_buf, uip_len);
        nbr->queue_buf_len = 0;
        tcpip_output(&(nbr->lladdr));
        }*/
      if(uip_packetqueue_buflen(&nbr->packethandle) != 0) {
        uip_len = uip_packetqueue_buflen(&nbr->packethandle);
        memcpy(UIP_IP_BUF, uip_packetqueue_buf(&nbr->packethandle), uip_len);
        uip_packetqueue_free(&nbr->packethandle);
        tcpip_output(&(nbr->lladdr));
      }
#endif /*UIP_CONF_IPV6_QUEUE_PKT*/
      uip_len = 0;
      return;
    }
#endif
}
#endif
/*---------------------------------------------------------------------------*/
#if UIP_CONF_IPV6 && !UIP_CONFIG_SOURCE_ROUTING
void
tcpip_ipv6_output(void)
{
  uip_ds6_nbr_t *nbr = NULL;
  uip_ipaddr_t* nexthop;

 // printf("tcpip_ipv6_output called.\r\n");

  if(uip_len == 0)
  {
	//printf("tcpip_ipv6_output: uip_len = 0\r\n");
    return;
  }

  if(uip_len > UIP_LINK_MTU){
    UIP_LOG("tcpip_ipv6_output: Packet to big");
    uip_len = 0;
    return;
  }
  if(uip_is_addr_unspecified(&UIP_IP_BUF->destipaddr)){
    UIP_LOG("tcpip_ipv6_output: Destination address unspecified");
    uip_len = 0;
    return;
  }
  if (uip_is_4to6_addr(&UIP_IP_BUF->destipaddr)) {
    tcpip_output(NULL);
    uip_len = 0;
    return;
    }


  if(!uip_is_addr_mcast(&UIP_IP_BUF->destipaddr)) {
    /* Next hop determination */
    nbr = NULL;
    if(uip_ds6_is_addr_onlink(&UIP_IP_BUF->destipaddr)){
      nexthop = &UIP_IP_BUF->destipaddr;
    } else {
      uip_ds6_route_t* locrt;
      locrt = uip_ds6_route_lookup(&UIP_IP_BUF->destipaddr);
      if(locrt == NULL) {
        if((nexthop = uip_ds6_defrt_choose()) == NULL) {
#ifdef UIP_FALLBACK_INTERFACE
      PRINTF("Sending to dst addr : ");
      PRINT6ADDR(&UIP_IP_BUF->destipaddr);
      PRINTF("\r\n");
	  UIP_FALLBACK_INTERFACE.output();
#else
          printf("tcpip6: Dst off-link but no route\n");
          ZW_LOG(I, 17);
#endif
          uip_len = 0;
          return;
        }
      } else {
        nexthop = &locrt->nexthop;
      }
    }
    /* end of next hop determination */
    if((nbr = uip_ds6_nbr_lookup(nexthop)) == NULL) {
      if((nbr = uip_ds6_nbr_add(nexthop, NULL, 0, NBR_INCOMPLETE)) == NULL) {
        uip_len = 0;
        return;
      } else {
#if UIP_CONF_IPV6_QUEUE_PKT
        /* copy outgoing pkt in the queuing buffer for later transmmit */
        uip_packetqueue_alloc(&nbr->packethandle,(uint8_t *)UIP_IP_BUF,uip_len, UIP_DS6_NBR_PACKET_LIFETIME);
#endif



#ifdef ZIP_ND6
        /* RFC4861, 7.2.2:
       * "If the source address of the packet prompting the solicitation is the
       * same as one of the addresses assigned to the outgoing interface, that
       * address SHOULD be placed in the IP Source Address of the outgoing
       * solicitation.  Otherwise, any one of the addresses assigned to the
       * interface should be used."*/
       if(uip_ds6_is_my_addr(&UIP_IP_BUF->srcipaddr)){
          uip_nd6_ns_output(&UIP_IP_BUF->srcipaddr, NULL, &nbr->ipaddr);
        } else {
          uip_nd6_ns_output(NULL, NULL, &nbr->ipaddr);
        }

        stimer_set(&(nbr->sendns), uip_ds6_if.retrans_timer / 1000);
        nbr->nscount = 1;
#endif /* ifdef ZIP_ND6 */
      }
    } else {
#ifdef ZIP_ND6
      if(nbr->state == NBR_PERMANTLY_UNREACHABLE) {
        PRINTF("tcpip_ipv6_output: destination is permanently unreachable\n");
        //uip_icmp6_error_output(ICMP6_DST_UNREACH,ICMP6_DST_UNREACH_ADDR, 0);
        return;
      }

      if (nbr->state == NBR_INCOMPLETE){
        PRINTF("tcpip_ipv6_output: nbr cache entry incomplete\n");
        ZW_LOG(I, 18);
#if UIP_CONF_IPV6_QUEUE_PKT
        PRINTF("Queueing IP package len %d\n",uip_len);

        /* copy outgoing pkt in the queuing buffer for later transmmit and set
           the destination nbr to nbr */
        uip_packetqueue_alloc(&nbr->packethandle,(uint8_t *)UIP_IP_BUF,uip_len, UIP_DS6_NBR_PACKET_LIFETIME);
        /*        memcpy(nbr->queue_buf, UIP_IP_BUF, uip_len);
                  nbr->queue_buf_len = uip_len;*/
        uip_len = 0;
#endif /*UIP_CONF_IPV6_QUEUE_PKT*/
        return;
      }
      /* if running NUD (nbc->state == STALE, DELAY, or PROBE ) keep
         sending in parallel see rfc 4861 Node behavior in section 7.7.3*/

      if (nbr->state == NBR_STALE){
        nbr->state = NBR_DELAY;
        stimer_set(&(nbr->reachable),
                  UIP_ND6_DELAY_FIRST_PROBE_TIME);
        nbr->nscount = 0;
        PRINTF("tcpip_ipv6_output: nbr cache entry stale moving to delay\n");
        ZW_LOG(I, 19);
      }

      stimer_set(&(nbr->sendns),
                uip_ds6_if.retrans_timer / 1000);
#endif /* ZIP_ND6 */
     // printf("Calling tcpip_output\r\n");
      tcpip_output(&(nbr->lladdr));

#if UIP_CONF_IPV6_QUEUE_PKT
      /* Send the queued packets from here, may not be 100% perfect though.
       * This happens in a few cases, for example when instead of receiving a
       * NA after sendiong a NS, you receive a NS with SLLAO: the entry moves
       *to STALE, and you must both send a NA and the queued packet
       */
      /*      if(nbr->queue_buf_len != 0) {
        uip_len = nbr->queue_buf_len;
        memcpy(UIP_IP_BUF, nbr->queue_buf, uip_len);
        nbr->queue_buf_len = 0;
        tcpip_output(&(nbr->lladdr));
        }*/
      if(uip_packetqueue_buflen(&nbr->packethandle) != 0) {
        PRINTF("sending queued IP package\n");
        uip_len = uip_packetqueue_buflen(&nbr->packethandle);
        memcpy(UIP_IP_BUF, uip_packetqueue_buf(&nbr->packethandle), uip_len);
        uip_packetqueue_pop(&nbr->packethandle);
        tcpip_output(&(nbr->lladdr));
      }
#endif /*UIP_CONF_IPV6_QUEUE_PKT*/


      uip_len = 0;
      return;
    }
  }

  /*multicast IP destination address */
  tcpip_output(NULL);
  uip_len = 0;
  uip_ext_len = 0;

}
#endif
/*---------------------------------------------------------------------------*/
#if UIP_UDP
void
tcpip_poll_udp(struct uip_udp_conn *conn)
{
  process_post(&tcpip_process, UDP_POLL, conn);
}
#endif /* UIP_UDP */
/*---------------------------------------------------------------------------*/
#if UIP_TCP
void
tcpip_poll_tcp(struct uip_conn *conn)
{
  process_post(&tcpip_process, TCP_POLL, conn);
}
#endif /* UIP_TCP */
/*---------------------------------------------------------------------------*/
void
tcpip_uipcall(void)
{
  register uip_udp_appstate_t *ts;

#if UIP_UDP
  if(uip_conn != NULL) {
    ts = &uip_conn->appstate;
  } else {
    ts = &uip_udp_conn->appstate;
  }
#else /* UIP_UDP */
  ts = &uip_conn->appstate;
#endif /* UIP_UDP */

#if UIP_TCP
 {
   static unsigned char i;
   register struct listenport *l;

   /* If this is a connection request for a listening port, we must
      mark the connection with the right process ID. */
   if(uip_connected()) {
     l = &s.listenports[0];
     for(i = 0; i < UIP_LISTENPORTS; ++i) {
       if(l->port == uip_conn->lport &&
	  l->p != PROCESS_NONE) {
	 ts->p = l->p;
	 ts->state = NULL;
	 break;
       }
       ++l;
     }

     /* Start the periodic polling, if it isn't already active. */
     start_periodic_tcp_timer();
   }
 }
#endif /* UIP_TCP */

  if(ts->p != NULL) {
    process_post_synch(ts->p, tcpip_event, ts->state);
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(tcpip_process, ev, data)
{
  PROCESS_BEGIN();

#if UIP_TCP
 {
   static unsigned char i;

   for(i = 0; i < UIP_LISTENPORTS; ++i) {
     s.listenports[i].port = 0;
   }
   s.p = PROCESS_CURRENT();
 }
#endif

  tcpip_ready = 0;
  if (!tcpip_event)
  {
    tcpip_event = process_alloc_event();
  }
#if UIP_CONF_ICMP6
  if (!tcpip_icmp6_event)
  {
	  tcpip_icmp6_event = process_alloc_event();
  }
#endif /* UIP_CONF_ICMP6 */
  etimer_set(&periodic, CLOCK_SECOND / 2);

  uip_init();
#ifdef UIP_FALLBACK_INTERFACE
  UIP_FALLBACK_INTERFACE.init();
#endif
/* initialize RPL if configured for using RPL */
#if UIP_CONF_IPV6_RPL
  rpl_init();
#endif /* UIP_CONF_IPV6_RPL */

  while(1) {
    //PROCESS_YIELD();
    do {
      PT_YIELD_FLAG = 0;
      LC_SET((process_pt)->lc);
      if(PT_YIELD_FLAG == 0) {
        tcpip_process.state = 0x01; /* PROCESS_STATE_RUNNING = 0x01 */
        return PT_YIELDED;
      }
    } while(0);

    eventhandler(ev, data);
  }

  ZW_LOG(I, 50);
  PROCESS_END();
}
