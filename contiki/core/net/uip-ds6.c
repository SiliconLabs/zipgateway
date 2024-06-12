/**
 * \addtogroup uip6
 * @{
 */

/**
 * \file
 *         IPv6 data structures handling functions
 *         Comprises part of the Neighbor discovery (RFC 4861)
 *         and auto configuration (RFC 4862 )state machines
 * \author Mathilde Durvy <mdurvy@cisco.com>
 * \author Julien Abeille <jabeille@cisco.com>
 */
/*
 * Copyright (c) 2006, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *   may be used to endorse or promote products derived from this software
 *   without specific prior written permission.
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
 */

#ifdef ZIP_NATIVE
#pragma userclass (xdata = HIGH) /* put xdata vars above 4K - inaccessible by framehandler */
#include <string_adaptation.h>
#include <ZW_protocol.h>
#include <ZW_mem.h>
#ifdef ZW_SLAVE_32
#include <ZW_nvm_slave.h>
#else
#ifdef ZW_CONTROLLER
#include <ZW_nvm_controller.h>
#else
#include <stdlib.h>
#include <string.h>
#endif
#endif
#else
#include<stdio.h>
#include<string.h>
#endif


#include "lib/random.h"
#include "net/uip-nd6.h"
#include "net/uip-ds6.h"
#ifdef ZIP_SIM
#include "mac_adaptation.h"
#endif


#ifdef ZIP_ROUTER
#include "zip_router_config.h"
#include "ZW_tcp_client.h"
#endif


#include "net/uip-debug.h"

#ifdef UIP_CONF_DS6_NEIGHBOR_STATE_CHANGED
#define NEIGHBOR_STATE_CHANGED(n) UIP_CONF_DS6_NEIGHBOR_STATE_CHANGED(n)
void NEIGHBOR_STATE_CHANGED(uip_ds6_nbr_t *n);
#else
#define NEIGHBOR_STATE_CHANGED(n)
#endif /* UIP_DS6_CONF_NEIGHBOR_STATE_CHANGED */

struct etimer uip_ds6_timer_periodic;                           /** \brief Timer for maintenance of data structures */

#if UIP_CONF_ROUTER
#ifndef ZIP_NATIVE
struct stimer uip_ds6_timer_ra;                                 /** \brief RA timer, to schedule RA sending */
#endif /* !ZIP_NATIVE */
#if UIP_ND6_SEND_RA
static uint8_t racount;                                         /** \brief number of RA already sent */
static uint16_t rand_time;                                      /** \brief random time value for timers */
#endif
#else /* UIP_CONF_ROUTER */
struct etimer uip_ds6_timer_rs;                                 /** \brief RS timer, to schedule RS sending */
static uint8_t rscount;                                         /** \brief number of rs already sent */
#endif /* UIP_CONF_ROUTER */

#if UIP_CONF_ADAPTIVE_ROUTING
struct etimer uip_ds6_timer_rs;                                 /** \brief RS timer, to schedule RS sending */
static uint8_t rscount;                                         /** \brief number of rs already sent */
#endif
/** \name "DS6" Data structures */
/** @{ */
uip_ds6_netif_t uip_ds6_if;                                       /** \brief The single interface */
uip_ds6_nbr_t uip_ds6_nbr_cache[UIP_DS6_NBR_NB];                  /** \brief Neighor cache */
uip_ds6_defrt_t uip_ds6_defrt_list[UIP_DS6_DEFRT_NB];             /** \brief Default rt list */
uip_ds6_prefix_t uip_ds6_prefix_list[UIP_DS6_PREFIX_NB];          /** \brief Prefix list */
uip_ds6_route_t uip_ds6_routing_table[UIP_DS6_ROUTE_NB];          /** \brief Routing table */

/** @} */

/* "full" (as opposed to pointer) ip address used in this file,  */
static uip_ipaddr_t loc_fipaddr;

/* Pointers used in this file */
static uip_ipaddr_t *locipaddr;
static uip_ds6_addr_t *locaddr;
static uip_ds6_maddr_t *locmaddr;
static uip_ds6_aaddr_t *locaaddr;
static uip_ds6_prefix_t *locprefix;
static uip_ds6_nbr_t *locnbr;
static uip_ds6_defrt_t *locdefrt;
static uip_ds6_route_t *locroute;
uint8_t uip_ds6_dad_in_progress;

/*---------------------------------------------------------------------------*/
#if ASIX_MULTICAST_FILTER_ENABLE

//Mulitcast is used only for node solicitation in the ipv6. if we are not using multi cast in the ipv4 packets.
uint8_t MacList_len = 0;

//2000::9  -> 33:33:ff:00:00:09    //multicast for node solicitation(FF02::1:FF00:0/104)with last 24 bits of address are looking for
//fd06:1e14:c502:ffff::1 -> 33:33:ff:00:00:01
//0x33, 0x33, 0xff, 0x00, 0x00, 0x09,
// 0x33, 0x33, 0xff, 0x00, 0x00, 0x01,

#define MAC_ADDR_LEN 	(6)

uint8_t Multicast_MacFilter [(UIP_DS6_ADDR_NB) * MAC_ADDR_LEN] = {0};

static void Multicast_MacList_Add(uip_ipaddr_t *ipv6addr)
{
	if(ipv6addr == NULL)
	{
		printf("Prepare_Multicast_MacList: Null input pointer.\r\n");
		return;
	}

	Multicast_MacFilter[MacList_len++] = 0x33;
	Multicast_MacFilter[MacList_len++] = 0x33;
	Multicast_MacFilter[MacList_len++] = 0xff;
	Multicast_MacFilter[MacList_len++] = ipv6addr->u8[13];
	Multicast_MacFilter[MacList_len++] = ipv6addr->u8[14];
	Multicast_MacFilter[MacList_len++] = ipv6addr->u8[15];

	return;
}

void Preppare_Multicast_Maclist(void)
{
	uint8_t i = 0;

	for(i=0; i<UIP_DS6_ADDR_NB; i++)
	{
		if (uip_ds6_if.addr_list[i].state == ADDR_PREFERRED)
		{
			Multicast_MacList_Add(&uip_ds6_if.addr_list[i].ipaddr);
		}
	}

	return;
}
#endif

void
uip_ds6_init(void)
{
  PRINTF("Init of IPv6 data structures\n");
  PRINTF("%u neighbors\n%u default routers\n%u prefixes\n%u routes\n%u unicast addresses\n%u multicast addresses\n%u anycast addresses\n",
     (unsigned int)UIP_DS6_NBR_NB,(unsigned int) UIP_DS6_DEFRT_NB, (unsigned int)UIP_DS6_PREFIX_NB, (unsigned int)UIP_DS6_ROUTE_NB,
     (unsigned int)UIP_DS6_ADDR_NB, (unsigned int)UIP_DS6_MADDR_NB, (unsigned int)UIP_DS6_AADDR_NB);
  ZW_LOG(I, 23);
  memset(uip_ds6_nbr_cache, 0, sizeof(uip_ds6_nbr_cache));
  memset(uip_ds6_defrt_list, 0, sizeof(uip_ds6_defrt_list));
  memset(uip_ds6_prefix_list, 0, sizeof(uip_ds6_prefix_list));
  memset(&uip_ds6_if, 0, sizeof(uip_ds6_if));
  memset(uip_ds6_routing_table, 0, sizeof(uip_ds6_routing_table));

  /* Set interface parameters */
  uip_ds6_if.link_mtu = UIP_LINK_MTU;
  uip_ds6_if.cur_hop_limit = UIP_TTL;
#ifdef ZIP_ND6
  uip_ds6_if.base_reachable_time = UIP_ND6_REACHABLE_TIME;
  uip_ds6_if.reachable_time = uip_ds6_compute_reachable_time();
  uip_ds6_if.retrans_timer = UIP_ND6_RETRANS_TIMER;
  uip_ds6_if.maxdadns = UIP_ND6_DEF_MAXDADNS;
#endif /* ZIP_ND6 */

  /* Create link local address, prefix, multicast addresses, anycast addresses */
  uip_create_linklocal_prefix(&loc_fipaddr);
#if UIP_CONF_ROUTER
  uip_ds6_prefix_add(&loc_fipaddr, UIP_DEFAULT_PREFIX_LEN, 0, 0, 0, 0);
#else /* UIP_CONF_ROUTER */
  uip_ds6_prefix_add(&loc_fipaddr, UIP_DEFAULT_PREFIX_LEN, 0);
#endif /* UIP_CONF_ROUTER */
  uip_ds6_set_addr_iid(&loc_fipaddr, &uip_lladdr);
  uip_ds6_addr_add(&loc_fipaddr, 0, ADDR_AUTOCONF);

  uip_create_linklocal_allnodes_mcast(&loc_fipaddr);
  uip_ds6_maddr_add(&loc_fipaddr);
#if UIP_CONF_ROUTER
  uip_create_linklocal_allrouters_mcast(&loc_fipaddr);
  uip_ds6_maddr_add(&loc_fipaddr);
#if UIP_ND6_SEND_RA
  stimer_set(&uip_ds6_timer_ra, 2);     /* wait to have a link local IP address */
  racount = 0;
#endif /* UIP_ND6_SEND_RA */
#else /* UIP_CONF_ROUTER */
  etimer_set(&uip_ds6_timer_rs,
             random_rand() % (UIP_ND6_MAX_RTR_SOLICITATION_DELAY *
                              CLOCK_SECOND));
#endif /* UIP_CONF_ROUTER */
  etimer_set(&uip_ds6_timer_periodic, UIP_DS6_PERIOD);

  uip_ds6_dad_in_progress = 1;
  return;
}


/*---------------------------------------------------------------------------*/
#ifdef ZIP_ND6
void
uip_ds6_periodic(void)
{
  u8_t new_dad_state = 0;

  for(locaddr = uip_ds6_if.addr_list;
      locaddr < uip_ds6_if.addr_list + UIP_DS6_ADDR_NB; locaddr++) {
    if(locaddr->isused && locaddr->state == ADDR_TENTATIVE) {
      new_dad_state=1;
      break;
    }
  }

  if(uip_ds6_dad_in_progress==1 && new_dad_state==0) {
    PRINTF("DAD complete\n");
    racount++;
    uip_ds6_send_ra_periodic();
  }

  uip_ds6_dad_in_progress = new_dad_state;

  /* Periodic processing on unicast addresses */
  for(locaddr = uip_ds6_if.addr_list;
      locaddr < uip_ds6_if.addr_list + UIP_DS6_ADDR_NB; locaddr++) {
    if(locaddr->isused) {
      if((!locaddr->isinfinite) && (stimer_expired(&locaddr->vlifetime))) {
        uip_ds6_addr_rm(locaddr);
      } else if((locaddr->state == ADDR_TENTATIVE)
                && (locaddr->dadnscount <= uip_ds6_if.maxdadns)
                && (timer_expired(&locaddr->dadtimer))) {
        uip_ds6_dad(locaddr);
      }
    }
  }

  /* Periodic processing on default routers */
  for(locdefrt = uip_ds6_defrt_list;
      locdefrt < uip_ds6_defrt_list + UIP_DS6_DEFRT_NB; locdefrt++) {
    if((locdefrt->isused) && (!locdefrt->isinfinite) &&
       (stimer_expired(&(locdefrt->lifetime)))) {
      uip_ds6_defrt_rm(locdefrt);
    }
  }

#if !UIP_CONF_ROUTER
  /* Periodic processing on prefixes */
  for(locprefix = uip_ds6_prefix_list;
      locprefix < uip_ds6_prefix_list + UIP_DS6_PREFIX_NB; locprefix++) {
    if((locprefix->isused) && (!locprefix->isinfinite)
       && (stimer_expired(&(locprefix->vlifetime)))) {
      uip_ds6_prefix_rm(locprefix);
    }
  }
#endif /* !UIP_CONF_ROUTER */

  /* Periodic processing on neighbors */
  for(locnbr = uip_ds6_nbr_cache; locnbr < uip_ds6_nbr_cache + UIP_DS6_NBR_NB;
      locnbr++) {
    if(locnbr->isused) {
      switch (locnbr->state) {
      case NBR_INCOMPLETE:
        if(locnbr->nscount >= UIP_ND6_MAX_MULTICAST_SOLICIT) {
          uip_ds6_nbr_rm(locnbr);
        } else if(stimer_expired(&(locnbr->sendns))) {
          locnbr->nscount++;
          PRINTF("NBR_INCOMPLETE: NS %u\n", (unsigned int)locnbr->nscount);
          ZW_LOG(I, 24);
          uip_nd6_ns_output(NULL, NULL, &locnbr->ipaddr);
          stimer_set(&(locnbr->sendns), uip_ds6_if.retrans_timer / 1000);
        }
        break;
      case NBR_REACHABLE:
        if(stimer_expired(&(locnbr->reachable))) {
          PRINTF("REACHABLE: moving to STALE (");
          ZW_LOG(I, 25);
          PRINT6ADDR(&locnbr->ipaddr);
          PRINTF(")\n");
          locnbr->state = NBR_STALE;
          NEIGHBOR_STATE_CHANGED(locnbr);
        }
        break;
      case NBR_DELAY:
        if(stimer_expired(&(locnbr->reachable))) {
          locnbr->state = NBR_PROBE;
          locnbr->nscount = 1;
          NEIGHBOR_STATE_CHANGED(locnbr);
          PRINTF("DELAY: moving to PROBE + NS %bu\n", locnbr->nscount);
          ZW_LOG(I, 26);
          uip_nd6_ns_output(NULL, &locnbr->ipaddr, &locnbr->ipaddr);
          stimer_set(&(locnbr->sendns), uip_ds6_if.retrans_timer / 1000);
        }
        break;
      case NBR_PROBE:
        if(locnbr->nscount >= UIP_ND6_MAX_UNICAST_SOLICIT) {
          PRINTF("PROBE END \n");
          ZW_LOG(I, 27);
          if((locdefrt = uip_ds6_defrt_lookup(&locnbr->ipaddr)) != NULL) {
            uip_ds6_defrt_rm(locdefrt);
          }
          uip_ds6_nbr_rm(locnbr);
        } else if(stimer_expired(&(locnbr->sendns))) {
          locnbr->nscount++;
          PRINTF("PROBE: NS %bu\n", locnbr->nscount);
          ZW_LOG(I, 28);
          uip_nd6_ns_output(NULL, &locnbr->ipaddr, &locnbr->ipaddr);
          stimer_set(&(locnbr->sendns), uip_ds6_if.retrans_timer / 1000);
        }
        break;
      default:
        break;
      }
    }
  }

#if UIP_CONF_ROUTER & UIP_ND6_SEND_RA
  /* Periodic RA sending */
  if(stimer_expired(&uip_ds6_timer_ra) && !uip_ds6_dad_in_progress) {
    uip_ds6_send_ra_periodic();
  }
#endif /* UIP_CONF_ROUTER & UIP_ND6_SEND_RA */
  etimer_reset(&uip_ds6_timer_periodic);
  return;
}
#endif /* ifdef ZIP_ND6 */

/*---------------------------------------------------------------------------*/
uint8_t
uip_ds6_list_loop(uip_ds6_element_t * list, uint8_t size,
                  uint16_t elementsize, uip_ipaddr_t * ipaddr,
                  uint8_t ipaddrlen, uip_ds6_element_t ** out_element)
{
  uip_ds6_element_t *element;

  *out_element = NULL;

  for(element = list;
      element <
      (uip_ds6_element_t *) ((uint8_t *) list + (size * elementsize));
      element = (uip_ds6_element_t *) ((uint8_t *) element + elementsize)) {
    if(element->isused) {
      if(uip_ipaddr_prefixcmp(&(element->ipaddr), ipaddr, ipaddrlen)) {
        *out_element = element;
        return FOUND;
      }
    } else {
      *out_element = element;
    }
  }

  if(*out_element) {
    return FREESPACE;
  } else {
    return NOSPACE;
  }
}

/*---------------------------------------------------------------------------*/
uip_ds6_nbr_t *
uip_ds6_nbr_add(uip_ipaddr_t * ipaddr, uip_lladdr_t * lladdr,
                uint8_t isrouter, uint8_t state)
{
  if(uip_ds6_list_loop
     ((uip_ds6_element_t *) uip_ds6_nbr_cache, UIP_DS6_NBR_NB,
      sizeof(uip_ds6_nbr_t), ipaddr, 128,
      (uip_ds6_element_t **) & locnbr) == FREESPACE) {
    locnbr->isused = 1;
    uip_ipaddr_copy(&(locnbr->ipaddr), ipaddr);
    if(lladdr != NULL) {
      memcpy(&(locnbr->lladdr), lladdr, UIP_LLADDR_LEN);
    } else {
      memset(&(locnbr->lladdr), 0, UIP_LLADDR_LEN);
    }
    locnbr->isrouter = isrouter;
    locnbr->state = state;
#ifdef ZIP_ND6
    /* timers are set separately, for now we put them in expired state */
    stimer_set(&(locnbr->reachable), 0);
    stimer_set(&(locnbr->sendns), 0);
#endif /* ifdef ZIP_ND6 */
    locnbr->nscount = 0;
    PRINTF("Adding neighbor with ip addr");
    ZW_LOG(I, 29);
    PRINT6ADDR(ipaddr);
    PRINTF("link addr");
    PRINTLLADDR((&(locnbr->lladdr)));
    PRINTF("state %bu\n", state);
    NEIGHBOR_STATE_CHANGED(locnbr);
    return locnbr;
  }
  return NULL;
}

/*---------------------------------------------------------------------------*/
void
uip_ds6_nbr_rm(uip_ds6_nbr_t *nbr)
{
  if(nbr != NULL) {
    nbr->isused = 0;
    NEIGHBOR_STATE_CHANGED(nbr);
  }
  return;
}

#include "ZIP_Router.h"
#include "ZW_tcp_client.h"
uip_ds6_nbr_t dummy_nbr_element;
extern uip_lladdr_t pan_lladdr;
/*---------------------------------------------------------------------------*/
uip_ds6_nbr_t *
uip_ds6_nbr_lookup(uip_ipaddr_t *ipaddr)
{
#ifdef ZWAVE_IP
	dummy_nbr_element.lladdr.addr[0] = ipaddr->u8[15];
	dummy_nbr_element.state = NBR_REACHABLE;
	return &dummy_nbr_element;
#else

#ifdef ZIP_ROUTER
  /*Packages for the HAN */
  if(uip_ipaddr_prefixcmp(&cfg.pan_prefix, ipaddr, 64))
  {
    memcpy(dummy_nbr_element.lladdr.addr,pan_lladdr.addr,5);
    dummy_nbr_element.lladdr.addr[5] = ipaddr->u8[15];

    if( 1 )
    {
      dummy_nbr_element.state = NBR_REACHABLE;
    }
    else
    {
      dummy_nbr_element.state = NBR_PERMANTLY_UNREACHABLE;
    }
    return &dummy_nbr_element;
  }

  /* Packages for the tunnel */
  if((cfg.tun_prefix_length != 0) && uip_ipaddr_prefixcmp(&cfg.tun_prefix, ipaddr, cfg.tun_prefix_length)) {
    memcpy(dummy_nbr_element.lladdr.addr,tun_lladdr.addr,6);
#if 0
    printf("dst ipaddr: ");
    uip_debug_ipaddr_print(ipaddr);
    printf("\r\n");

    printf("tun repfix: ");
    uip_debug_ipaddr_print(&cfg.tun_prefix);
    printf("\r\n");
#endif

    dummy_nbr_element.state = (gisZIPRReady == ZIPR_READY) ? NBR_REACHABLE : NBR_DELAY;
    return &dummy_nbr_element;
  }
  /*Is this a 46 mapped address, then let the IPv4 stack handle it*/
  if(uip_is_4to6_addr(ipaddr)) {
    dummy_nbr_element.state = NBR_REACHABLE;
  }

#endif


  //If we operate in net layer 3 mode all nodes are neighbors on the link layer.
#ifdef L3_ONLY_
  memset(&dummy_nbr_element,0,sizeof(dummy_nbr_element));
  dummy_nbr_element.state = NBR_REACHABLE;
  return &dummy_nbr_element;
#else
  if(uip_ds6_list_loop
     ((uip_ds6_element_t *) uip_ds6_nbr_cache, UIP_DS6_NBR_NB,
      sizeof(uip_ds6_nbr_t), ipaddr, 128,
      (uip_ds6_element_t **) & locnbr) == FOUND) {
    return locnbr;
  }
  return NULL;
#endif

#endif
}

#ifdef ZIP_NATIVE
/*---------------------------------------------------------------------------*/
void
uip_ds6_defrt_save_nvm()
{
  NVM_PUTBUFFER(NVM_ZIP_DEFRT_LIST_START, uip_ds6_defrt_list, NVM_ZIP_DEFRT_LIST_SIZE);
}

void
uip_ds6_defrt_load_nvm()
{
  NVM_GETBUFFER(NVM_ZIP_DEFRT_LIST_START, uip_ds6_defrt_list, NVM_ZIP_DEFRT_LIST_SIZE);
}

void
uip_ds6_defrt_clear_nvm()
{
  NVM_PUTBUFFER(NVM_ZIP_DEFRT_LIST_START, NULL, NVM_ZIP_DEFRT_LIST_SIZE);
}
#else
#ifdef ZIP_SIM
void
uip_ds6_defrt_save_nvm()
{
}

void
uip_ds6_defrt_load_nvm()
{
}

void
uip_ds6_defrt_clear_nvm()
{
}
#endif
#endif

/*---------------------------------------------------------------------------*/
uip_ds6_defrt_t *
uip_ds6_defrt_add(uip_ipaddr_t *ipaddr, unsigned long interval)
{
  if(uip_ds6_list_loop
     ((uip_ds6_element_t *) uip_ds6_defrt_list, UIP_DS6_DEFRT_NB,
      sizeof(uip_ds6_defrt_t), ipaddr, 128,
      (uip_ds6_element_t **) & locdefrt) == FREESPACE) {
    locdefrt->isused = 1;
    uip_ipaddr_copy(&(locdefrt->ipaddr), ipaddr);
    if(interval != 0) {
#ifdef ZIP_ND6
      stimer_set(&(locdefrt->lifetime), interval);
#endif /* #ifdef ZIP_ND6 */
      locdefrt->isinfinite = 0;
    } else {
      locdefrt->isinfinite = 1;
    }
#ifdef ZWAVE_IP
    uip_ds6_defrt_save_nvm();
#endif
    PRINTF("Adding defrouter with ip addr");
    ZW_LOG(I, 29);
    PRINT6ADDR(&locdefrt->ipaddr);
    PRINTF("\n");
    return locdefrt;
  }
  return NULL;
}

/*---------------------------------------------------------------------------*/
void
uip_ds6_defrt_rm(uip_ds6_defrt_t * defrt)
{
  if(defrt != NULL) {
    defrt->isused = 0;
  }
#ifdef ZWAVE_IP
  uip_ds6_defrt_save_nvm();
#endif
  return;
}

/*---------------------------------------------------------------------------*/
uip_ds6_defrt_t *
uip_ds6_defrt_lookup(uip_ipaddr_t * ipaddr)
{
  if(uip_ds6_list_loop((uip_ds6_element_t *) uip_ds6_defrt_list,
		       UIP_DS6_DEFRT_NB, sizeof(uip_ds6_defrt_t), ipaddr, 128,
		       (uip_ds6_element_t **) & locdefrt) == FOUND) {
    return locdefrt;
  }
  return NULL;
}

/*---------------------------------------------------------------------------*/
uip_ipaddr_t *
uip_ds6_defrt_choose(void)
{
  uip_ds6_nbr_t *bestnbr;

  locipaddr = NULL;
  for(locdefrt = uip_ds6_defrt_list;
      locdefrt < uip_ds6_defrt_list + UIP_DS6_DEFRT_NB; locdefrt++) {
    if(locdefrt->isused) {
      PRINTF("Defrt, IP address ");
      PRINT6ADDR(&locdefrt->ipaddr);
      PRINTF("\n");
      bestnbr = uip_ds6_nbr_lookup(&locdefrt->ipaddr);
      if((bestnbr != NULL) && (bestnbr->state != NBR_INCOMPLETE)) {
        PRINTF("Defrt found, IP address ");
        ZW_LOG(I, 30);
        PRINT6ADDR(&locdefrt->ipaddr);
        PRINTF("\n");
        return &locdefrt->ipaddr;
      } else {
        locipaddr = &locdefrt->ipaddr;
        PRINTF("Defrt INCOMPLETE found, IP address ");
        ZW_LOG(I, 31);
        PRINT6ADDR(&locdefrt->ipaddr);
        PRINTF("\n");
      }
    }
  }
  return locipaddr;
}

#if UIP_CONF_ROUTER
/*---------------------------------------------------------------------------*/
uip_ds6_prefix_t *
uip_ds6_prefix_add(uip_ipaddr_t * ipaddr, uint8_t ipaddrlen,
                   uint8_t advertise, uint8_t flags, unsigned long vtime,
                   unsigned long ptime)
{
  if(uip_ds6_list_loop
     ((uip_ds6_element_t *) uip_ds6_prefix_list, UIP_DS6_PREFIX_NB,
      sizeof(uip_ds6_prefix_t), ipaddr, ipaddrlen,
      (uip_ds6_element_t **) & locprefix) == FREESPACE) {
    locprefix->isused = 1;
    uip_ipaddr_copy(&(locprefix->ipaddr), ipaddr);
    locprefix->length = ipaddrlen;
    locprefix->advertise = advertise;
    locprefix->l_a_reserved = flags;
    locprefix->vlifetime = vtime;
    locprefix->plifetime = ptime;
    PRINTF("Adding prefix ");
    ZW_LOG(I, 32);
    PRINT6ADDR(&locprefix->ipaddr);
    PRINTF("length %u, flags %x, Valid lifetime %lx, Preffered lifetime %lx\n",
       (uint16_t)ipaddrlen, (uint16_t)flags, (uint32_t)vtime, (uint32_t)ptime);
    return locprefix;
  } else {
    PRINTF("No more space in Prefix list\n");
    ZW_LOG(I, 33);
  }
  return NULL;
}


#else /* UIP_CONF_ROUTER */
uip_ds6_prefix_t *
uip_ds6_prefix_add(uip_ipaddr_t * ipaddr, uint8_t ipaddrlen,
                   unsigned long interval)
{
  if(uip_ds6_list_loop
     ((uip_ds6_element_t *) uip_ds6_prefix_list, UIP_DS6_PREFIX_NB,
      sizeof(uip_ds6_prefix_t), ipaddr, ipaddrlen,
      (uip_ds6_element_t **) & locprefix) == FREESPACE) {
    locprefix->isused = 1;
    uip_ipaddr_copy(&(locprefix->ipaddr), ipaddr);
    locprefix->length = ipaddrlen;
    if(interval != 0) {
      stimer_set(&(locprefix->vlifetime), interval);
      locprefix->isinfinite = 0;
    } else {
      locprefix->isinfinite = 1;
    }
    PRINTF("Adding prefix ");
    PRINT6ADDR(&locprefix->ipaddr);
    PRINTF("length %u, vlifetime%lu\n", ipaddrlen, interval);
  }
  return NULL;
}
#endif /* UIP_CONF_ROUTER */

/*---------------------------------------------------------------------------*/
void
uip_ds6_prefix_rm(uip_ds6_prefix_t * prefix)
{
  if(prefix != NULL) {
    prefix->isused = 0;
  }
  return;
}
/*---------------------------------------------------------------------------*/
uip_ds6_prefix_t *
uip_ds6_prefix_lookup(uip_ipaddr_t * ipaddr, uint8_t ipaddrlen)
{
  if(uip_ds6_list_loop((uip_ds6_element_t *)uip_ds6_prefix_list,
		       UIP_DS6_PREFIX_NB, sizeof(uip_ds6_prefix_t),
		       ipaddr, ipaddrlen,
		       (uip_ds6_element_t **)&locprefix) == FOUND) {
    return locprefix;
  }
  return NULL;
}

/*---------------------------------------------------------------------------*/
uint8_t
uip_ds6_is_addr_onlink(uip_ipaddr_t * ipaddr)
{
  for(locprefix = uip_ds6_prefix_list;
      locprefix < uip_ds6_prefix_list + UIP_DS6_PREFIX_NB; locprefix++) {
    if(locprefix->isused &&
       uip_ipaddr_prefixcmp(&locprefix->ipaddr, ipaddr, locprefix->length)) {
      return 1;
    }
  }
  return 0;
}

/*---------------------------------------------------------------------------*/
uip_ds6_addr_t *
uip_ds6_addr_add(uip_ipaddr_t * ipaddr, unsigned long vlifetime, uint8_t type)
{
  if(uip_ds6_list_loop
     ((uip_ds6_element_t *) uip_ds6_if.addr_list, UIP_DS6_ADDR_NB,
      sizeof(uip_ds6_addr_t), ipaddr, 128,
      (uip_ds6_element_t **) & locaddr) == FREESPACE) {
    locaddr->isused = 1;
    uip_ipaddr_copy(&locaddr->ipaddr, ipaddr);
#if defined(ZIP_NATIVE) || defined(ZIP_SIM)
    /* Duplicate addr detection disabled, so we go straight to preferred. */
    locaddr->state = ADDR_PREFERRED;
#else
    locaddr->state = ADDR_TENTATIVE;
#endif
    locaddr->type = type;
    if(vlifetime == 0) {
      locaddr->isinfinite = 1;
    } else {
      locaddr->isinfinite = 0;
#ifdef ZIP_ND6
      stimer_set(&(locaddr->vlifetime), vlifetime);
#endif /* #ifdef ZIP_ND6 */
    }
#ifdef ZIP_ND6
    timer_set(&locaddr->dadtimer,
              random_rand() % (UIP_ND6_MAX_RTR_SOLICITATION_DELAY *
                               CLOCK_SECOND));
    locaddr->dadnscount = 0;
#endif
    uip_create_solicited_node(ipaddr, &loc_fipaddr);
    uip_ds6_maddr_add(&loc_fipaddr);
    return locaddr;
  }
  return NULL;
}

/*---------------------------------------------------------------------------*/
void
uip_ds6_addr_rm(uip_ds6_addr_t * addr)
{
  if(addr != NULL) {
    uip_create_solicited_node(&addr->ipaddr, &loc_fipaddr);
    if((locmaddr = uip_ds6_maddr_lookup(&loc_fipaddr)) != NULL) {
      uip_ds6_maddr_rm(locmaddr);
    }
    addr->isused = 0;
  }
  return;
}

/*---------------------------------------------------------------------------*/
uip_ds6_addr_t *
uip_ds6_addr_lookup(uip_ipaddr_t * ipaddr)
{
  if(uip_ds6_list_loop
     ((uip_ds6_element_t *) uip_ds6_if.addr_list, UIP_DS6_ADDR_NB,
      sizeof(uip_ds6_addr_t), ipaddr, 128,
      (uip_ds6_element_t **) & locaddr) == FOUND) {
    return locaddr;
  }
  return NULL;
}

/*---------------------------------------------------------------------------*/
/*
 * get a link local address -
 * state = -1 => any address is ok. Otherwise state = desired state of addr.
 * (TENTATIVE, PREFERRED, DEPRECATED)
 */
uip_ds6_addr_t *
uip_ds6_get_link_local(int8_t state)
{
  for(locaddr = uip_ds6_if.addr_list;
      locaddr < uip_ds6_if.addr_list + UIP_DS6_ADDR_NB; locaddr++) {
    if((locaddr->isused) && (state == -1 || locaddr->state == state)
       && (uip_is_addr_link_local(&locaddr->ipaddr))) {
      return locaddr;
    }
  }
  return NULL;
}

/*---------------------------------------------------------------------------*/
/*
 * get a global address -
 * state = -1 => any address is ok. Otherwise state = desired state of addr.
 * (TENTATIVE, PREFERRED, DEPRECATED)
 */
uip_ds6_addr_t *
uip_ds6_get_global(int8_t state)
{
  for(locaddr = uip_ds6_if.addr_list;
      locaddr < uip_ds6_if.addr_list + UIP_DS6_ADDR_NB; locaddr++) {
    if((locaddr->isused) && (state == -1 || locaddr->state == state)
       && !(uip_is_addr_link_local(&locaddr->ipaddr))) {
      return locaddr;
    }
  }
  return NULL;
}

/*---------------------------------------------------------------------------*/
uip_ds6_maddr_t *
uip_ds6_maddr_add(uip_ipaddr_t * ipaddr)
{
  if(uip_ds6_list_loop
     ((uip_ds6_element_t *) uip_ds6_if.maddr_list, UIP_DS6_MADDR_NB,
      sizeof(uip_ds6_maddr_t), ipaddr, 128,
      (uip_ds6_element_t **) & locmaddr) == FREESPACE) {
    locmaddr->isused = 1;
    uip_ipaddr_copy(&locmaddr->ipaddr, ipaddr);
    return locmaddr;
  }
  return NULL;
}

/*---------------------------------------------------------------------------*/
void
uip_ds6_maddr_rm(uip_ds6_maddr_t * maddr)
{
  if(maddr != NULL) {
    maddr->isused = 0;
  }
  return;
}

/*---------------------------------------------------------------------------*/
uip_ds6_maddr_t *
uip_ds6_maddr_lookup(uip_ipaddr_t * ipaddr)
{
  if(uip_ds6_list_loop
     ((uip_ds6_element_t *) uip_ds6_if.maddr_list, UIP_DS6_MADDR_NB,
      sizeof(uip_ds6_maddr_t), ipaddr, 128,
      (uip_ds6_element_t **) & locmaddr) == FOUND) {
    return locmaddr;
  }
  return NULL;
}


/*---------------------------------------------------------------------------*/
uip_ds6_aaddr_t *
uip_ds6_aaddr_add(uip_ipaddr_t * ipaddr)
{
  if(uip_ds6_list_loop
     ((uip_ds6_element_t *) uip_ds6_if.aaddr_list, UIP_DS6_AADDR_NB,
      sizeof(uip_ds6_aaddr_t), ipaddr, 128,
      (uip_ds6_element_t **) & locaaddr) == FREESPACE) {
    locaaddr->isused = 1;
    uip_ipaddr_copy(&locaaddr->ipaddr, ipaddr);
    return locaaddr;
  }
  return NULL;
}

/*---------------------------------------------------------------------------*/
void
uip_ds6_aaddr_rm(uip_ds6_aaddr_t * aaddr)
{
  if(aaddr != NULL) {
    aaddr->isused = 0;
  }
  return;
}

/*---------------------------------------------------------------------------*/
uip_ds6_aaddr_t *
uip_ds6_aaddr_lookup(uip_ipaddr_t * ipaddr)
{
  if(uip_ds6_list_loop((uip_ds6_element_t *) uip_ds6_if.aaddr_list,
		       UIP_DS6_AADDR_NB, sizeof(uip_ds6_aaddr_t), ipaddr, 128,
		       (uip_ds6_element_t **)&locaaddr) == FOUND) {
    return locaaddr;
  }
  return NULL;
}

/*---------------------------------------------------------------------------*/
uip_ds6_route_t *
uip_ds6_route_lookup(uip_ipaddr_t * destipaddr)
{
  uip_ds6_route_t *locrt = NULL;
  uint8_t longestmatch = 0;

  PRINTF("DS6: Looking up route for");
  PRINT6ADDR(destipaddr);
  PRINTF("\n");

  for(locroute = uip_ds6_routing_table;
      locroute < uip_ds6_routing_table + UIP_DS6_ROUTE_NB; locroute++) {
    if((locroute->isused) && (locroute->length >= longestmatch)
       &&
       (uip_ipaddr_prefixcmp
        (destipaddr, &locroute->ipaddr, locroute->length))) {
      longestmatch = locroute->length;
      locrt = locroute;
    }
  }

  if(locrt != NULL) {
    PRINTF("DS6: Found route:");
    ZW_LOG(I, 34);
    PRINT6ADDR(destipaddr);
    PRINTF(" via ");
    PRINT6ADDR(&locrt->nexthop);
    PRINTF("\n");
  } else {
    PRINTF("DS6: No route found ...\n");
    ZW_LOG(I, 35);
  }

  return locrt;
}
/*---------------------------------------------------------------------------*/
void
uip_ds6_routes_print()
{
  uip_ds6_route_t *locrt = NULL;

  PRINTF("DS6: Routing table");
  PRINTF("\n");

  for(locroute = uip_ds6_routing_table;
      locroute < uip_ds6_routing_table + UIP_DS6_ROUTE_NB; locroute++) {
    if((locroute->isused)) {
	    PRINTF("DS6: Found route:");
	    ZW_LOG(I, 34);
	    PRINT6ADDR(&locroute->ipaddr);
	    PRINTF(" via ");
	    PRINT6ADDR(&(locroute->nexthop));
		PRINTF(" length ");
	    PRINTF("%i",locroute->length);
		    PRINTF(" metric ");
	    PRINTF("%i",locroute->metric);
	    PRINTF("\n");

    }
  }

  if(locrt != NULL) {

  } else {
    PRINTF("DS6: No route found ...\n");
    ZW_LOG(I, 35);
  }

  return;
}


/*---------------------------------------------------------------------------*/
uip_ds6_route_t *
uip_ds6_route_add(uip_ipaddr_t * ipaddr, u8_t length, uip_ipaddr_t * nexthop,
                  u8_t metric)
{

  if(uip_ds6_list_loop
     ((uip_ds6_element_t *) uip_ds6_routing_table, UIP_DS6_ROUTE_NB,
      sizeof(uip_ds6_route_t), ipaddr, length,
      (uip_ds6_element_t **) & locroute) == FREESPACE) {
    locroute->isused = 1;
    uip_ipaddr_copy(&(locroute->ipaddr), ipaddr);
    locroute->length = length;
    uip_ipaddr_copy(&(locroute->nexthop), nexthop);
    locroute->metric = metric;

    PRINTF("DS6: adding route:");
    ZW_LOG(I, 36);
    PRINT6ADDR(ipaddr);
    PRINTF(" via ");
    PRINT6ADDR(nexthop);
    PRINTF("\n");

  }

  return locroute;
}

/*---------------------------------------------------------------------------*/
void
uip_ds6_route_rm(uip_ds6_route_t *route)
{
  route->isused = 0;
}
/*---------------------------------------------------------------------------*/
void
uip_ds6_route_rm_by_nexthop(uip_ipaddr_t *nexthop)
{
  for(locroute = uip_ds6_routing_table;
      locroute < uip_ds6_routing_table + UIP_DS6_ROUTE_NB; locroute++) {
    if((locroute->isused) && uip_ipaddr_cmp(&locroute->nexthop, nexthop)) {
      locroute->isused = 0;
    }
  }
}

/*---------------------------------------------------------------------------*/
void
uip_ds6_select_src(uip_ipaddr_t *src, uip_ipaddr_t *dst)
{
  uint8_t best = 0;             /* number of bit in common with best match */
  uint8_t n = 0;
  uip_ds6_addr_t *matchaddr = NULL;

  if(!uip_is_addr_link_local(dst) && !uip_is_addr_mcast(dst)) {
    /* find longest match */
    for(locaddr = uip_ds6_if.addr_list;
        locaddr < uip_ds6_if.addr_list + UIP_DS6_ADDR_NB; locaddr++) {
      /* Only preferred global (not link-local) addresses */
      if((locaddr->isused) && (locaddr->state == ADDR_PREFERRED) &&
         (!uip_is_addr_link_local(&locaddr->ipaddr))) {
        n = get_match_length(dst, &(locaddr->ipaddr));
        if(n >= best) {
          best = n;
          matchaddr = locaddr;
        }
      }
    }
  } else {
    matchaddr = uip_ds6_get_link_local(ADDR_PREFERRED);
  }

  /* use the :: (unspecified address) as source if no match found */
  if(matchaddr == NULL) {
    uip_create_unspecified(src);
  } else {
    uip_ipaddr_copy(src, &matchaddr->ipaddr);
  }
}

/*---------------------------------------------------------------------------*/
void
uip_ds6_set_addr_iid(uip_ipaddr_t * ipaddr, uip_lladdr_t * lladdr)
{
  /* We consider only links with IEEE EUI-64 identifier or
   * IEEE 48-bit MAC addresses, or Z-Wave */
#if (UIP_LLADDR_LEN == 8)
  memcpy(ipaddr->u8 + 8, lladdr, UIP_LLADDR_LEN);
  ipaddr->u8[8] ^= 0x02;
#elif (UIP_LLADDR_LEN == 6)
  memcpy(ipaddr->u8 + 8, lladdr, 3);
  ipaddr->u8[11] = 0xff;
  ipaddr->u8[12] = 0xfe;
  memcpy(ipaddr->u8 + 13, (uint8_t *) lladdr + 3, 3);
  ipaddr->u8[8] ^= 0x02;
#elif (UIP_LLADDR_LEN == 1)
#ifdef ZWAVE_IP
  memcpy(ipaddr->u8 + 8, homeID, HOMEID_LENGTH);
  ipaddr->u8[13] = ipaddr->u8[11]; // Save fourth byte of homeid from being overwritten
  ipaddr->u8[11] = 0xff;
  ipaddr->u8[12] = 0xfe;
  ipaddr->u8[14] = 0x0;
#if defined(ZIP_NATIVE) || defined(ZIP_SIM)
  ipaddr->u8[15] = nodeID;
#else
  ipaddr->u8[15] = MyNodeID;
#endif
  // TODO: Should we care about local/universal bit convention (http://en.wikipedia.org/wiki/MAC_address) - I think not
  //ipaddr->u8[8] ^= 0x02;
#endif
#else
#error uip-ds6.c cannot build interface address when UIP_LLADDR_LEN is not 6 or 8
#endif
}

/*---------------------------------------------------------------------------*/
uint8_t
get_match_length(uip_ipaddr_t * src, uip_ipaddr_t * dst)
{
  uint8_t j, k, x_or;
  uint8_t len = 0;

  for(j = 0; j < 16; j++) {
    if(src->u8[j] == dst->u8[j]) {
      len += 8;
    } else {
      x_or = src->u8[j] ^ dst->u8[j];
      for(k = 0; k < 8; k++) {
        if((x_or & 0x80) == 0) {
          len++;
          x_or <<= 1;
        } else {
          break;
        }
      }
      break;
    }
  }
  return len;
}

/*---------------------------------------------------------------------------*/
#if !(defined(ZIP_NATIVE) || defined(ZIP_SIM))
void
uip_ds6_dad(uip_ds6_addr_t * addr)
{
  /* send maxdadns NS for DAD  */
  if(addr->dadnscount < uip_ds6_if.maxdadns) {
    uip_nd6_ns_output(NULL, NULL, &addr->ipaddr);
    addr->dadnscount++;
    timer_set(&addr->dadtimer,
              uip_ds6_if.retrans_timer / 1000 * CLOCK_SECOND);
    return;
  }
  /*
   * If we arrive here it means DAD succeeded, otherwise the dad process
   * would have been interrupted in ds6_dad_ns/na_input
   */
  PRINTF("DAD succeeded, ipaddr:");
  ZW_LOG(I, 37);
  uip_debug_ipaddr_print(&addr->ipaddr);
  printf("\n");

  addr->state = ADDR_PREFERRED;
  return;
}
#endif /* ifndef ZIP_NATIVE */
/*---------------------------------------------------------------------------*/
#if !(defined(ZIP_NATIVE) || defined(ZIP_SIM))
/*
 * Calling code must handle when this returns 0 (e.g. link local
 * address can not be used).
 */
int
uip_ds6_dad_failed(uip_ds6_addr_t * addr)
{
  printf("DAD failed for ip addr:");
  uip_debug_ipaddr_print(&addr->ipaddr);
  if(uip_is_addr_link_local(&addr->ipaddr)) {
    PRINTF("Contiki shutdown, DAD for link local address failed\n");
    ZW_LOG(I, 38);
    return 0;
  }
  uip_ds6_addr_rm(addr);
  return 1;
}
#endif /* ifndef ZIP_NATIVE */

#if UIP_CONF_ROUTER
#if UIP_ND6_SEND_RA
/*---------------------------------------------------------------------------*/
void
uip_ds6_send_ra_sollicited(void)
{
  /* We have a pb here: RA timer max possible value is 1800s,
   * hence we have to use stimers. However, when receiving a RS, we
   * should delay the reply by a random value between 0 and 500ms timers.
   * stimers are in seconds, hence we cannot do this. Therefore we just send
   * the RA (setting the timer to 0 below). We keep the code logic for
   * the days contiki will support appropriate timers */
  rand_time = 0;
  PRINTF("Solicited RA, random time %u\n", rand_time);
  ZW_LOG(I, 39);

  if(stimer_remaining(&uip_ds6_timer_ra) > rand_time) {
    if(stimer_elapsed(&uip_ds6_timer_ra) < UIP_ND6_MIN_DELAY_BETWEEN_RAS) {
      /* Ensure that the RAs are rate limited */
/*      stimer_set(&uip_ds6_timer_ra, rand_time +
                 UIP_ND6_MIN_DELAY_BETWEEN_RAS -
                 stimer_elapsed(&uip_ds6_timer_ra));
  */ } else {
      stimer_set(&uip_ds6_timer_ra, rand_time);
    }
  }
}

/*---------------------------------------------------------------------------*/
void
uip_ds6_send_ra_periodic(void)
{
  if(racount > 0) {
    /* send previously scheduled RA */
    uip_nd6_ra_output(NULL);
    PRINTF("Sending periodic RA\n");
    ZW_LOG(I, 40);
  }

  rand_time = UIP_ND6_MIN_RA_INTERVAL + random_rand() %
    (uint16_t) (UIP_ND6_MAX_RA_INTERVAL - UIP_ND6_MIN_RA_INTERVAL);
  PRINTF("Random time 1 = %u\n", rand_time);

  if(racount < UIP_ND6_MAX_INITIAL_RAS) {
    if(rand_time > UIP_ND6_MAX_INITIAL_RA_INTERVAL) {
      rand_time = UIP_ND6_MAX_INITIAL_RA_INTERVAL;
      PRINTF("Random time 2 = %u\n", rand_time);
    }
    racount++;
  }
  PRINTF("Random time 3 = %u\n", rand_time);
  stimer_set(&uip_ds6_timer_ra, rand_time);
}

#endif /* UIP_ND6_SEND_RA */
#endif

#if !UIP_CONF_ROUTER  || UIP_CONF_ADAPTIVE_ROUTING
/*---------------------------------------------------------------------------*/
void
uip_ds6_send_rs(void)
{
  if((uip_ds6_defrt_choose() == NULL)
     && (rscount < UIP_ND6_MAX_RTR_SOLICITATIONS)) {
    PRINTF("Sending RS %u\n", rscount);
    ZW_LOG(I, 41);
    uip_nd6_rs_output();
    rscount++;
    etimer_set(&uip_ds6_timer_rs,
               UIP_ND6_RTR_SOLICITATION_INTERVAL * CLOCK_SECOND);
  } else {
    PRINTF("Router found ? (boolean): %u\n",
           (uip_ds6_defrt_choose() != NULL));
    etimer_stop(&uip_ds6_timer_rs);
  }
  return;
}

#endif /* UIP_CONF_ROUTER */
/*---------------------------------------------------------------------------*/
#ifdef ZIP_ND6
uint32_t
uip_ds6_compute_reachable_time(void)
{
  return (uint32_t) (UIP_ND6_MIN_RANDOM_FACTOR
                     (uip_ds6_if.base_reachable_time)) +
    ((uint16_t) (random_rand() << 8) +
     (uint16_t) random_rand()) %
    (uint32_t) (UIP_ND6_MAX_RANDOM_FACTOR(uip_ds6_if.base_reachable_time) -
                UIP_ND6_MIN_RANDOM_FACTOR(uip_ds6_if.base_reachable_time));
}
#endif
