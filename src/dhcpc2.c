/* © 2014 Silicon Laboratories Inc.
 */


/*
 * Copyright (c) 2005, Swedish Institute of Computer Science
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
 * @(#)$Id: dhcpc.c,v 1.9 2010/10/19 18:29:04 adamdunkels Exp $
 */
#include <stdint.h>
#include <string.h>
#include "TYPES.H"
#include "contiki.h"
#include "sys/etimer.h"
#include "rand.h"
#include "contiki-net-ipv4.h"
#include "ipv46_internal.h"
#include "ipv46_addr.h"
#include "TYPES.H"
#include "dhcpc2.h"

#include "ZIP_Router_logging.h"
#include "zip_router_config.h"
#include "zw_network_info.h"

#define uip_appdata uip_ipv4_appdata
#define uip_send uip_ipv4_send
#define uip_datalen uip_ipv4_datalen
#define tcpip_event tcpip_ipv4_event

#define udp_bind udp_ipv4_bind
#define udp_new udp_ipv4_new

extern void macOfNode(uip_lladdr_t* dst, nodeid_t nodeID);
PROCESS(dhcp_client_process, "DHCP client process");
#include "router_events.h"
struct dhcp_msg
{
  u8_t op, htype, hlen, hops;
  u8_t xid[4];
  u16_t secs, flags;
  u8_t ciaddr[4];
  u8_t yiaddr[4];
  u8_t siaddr[4];
  u8_t giaddr[4];
  u8_t chaddr[16];
  u8_t sname[64];
  u8_t file[128];
  u8_t options[312];
} CC_ALIGN_PACK;

#define BOOTP_BROADCAST 0x8000

#define DHCP_REQUEST        1
#define DHCP_REPLY          2
#define DHCP_HTYPE_ETHERNET 1
#define DHCP_HLEN_ETHERNET  6
#define DHCP_MSG_LEN      236

#define DHCPC_SERVER_PORT  67
#define DHCPC_CLIENT_PORT  68

#define DHCPDISCOVER  1
#define DHCPOFFER     2
#define DHCPREQUEST   3
#define DHCPDECLINE   4
#define DHCPACK       5
#define DHCPNAK       6
#define DHCPRELEASE   7

#define DHCP_OPTION_SUBNET_MASK   1
#define DHCP_OPTION_ROUTER        3
#define DHCP_OPTION_DNS_SERVER    6
#define DHCP_OPTION_REQ_IPADDR   50
#define DHCP_OPTION_LEASE_TIME   51
#define DHCP_OPTION_MSG_TYPE     53
#define DHCP_OPTION_SERVER_ID    54
#define DHCP_OPTION_REQ_LIST     55
#define DHCP_OPTION_CLIENT_ID    61
#define DHCP_OPTION_END         255

typedef enum {
  DHCP_IDLE,
  DHCP_SEND_DISCOVER,
  DHCP_RECV_OFFER,
  DHCP_SEND_REQUEST,
  DHCP_RECV_ACK,
  DHCP_RECV_NAK,
  DHCP_TIME_OUT,
  DHCP_UDP_POLL,
} dhcp_session_state_t;


typedef struct dhcpc_state {
  u32_t lease_time;
  u32_t ticks;
  u32_t timeout;



  dhcp_session_state_t state;
  struct uip_udp_conn *conn;
  struct etimer timer;
  u32_t xid;
  u8_t last_renew; /* Indicates which session was last renewed, this will be set to 0 when we need to refresh all entries*/
  u8_t session_timeout; /* When this reaches 0 the current session times out */
  u8_t retry;
  u8_t renew_failed; /*Did the renew fail? */
  u8_t serverid[4];

  /*Thees apply for all sessions */
  uip_ipaddr_t netmask;
  uip_ipaddr_t dnsaddr;
  uip_ipaddr_t default_router;
  uip_ipaddr_t ipaddr;

  /* The session we are handling at the moment */
  nat_table_entry_t* session;
} dhcpc_state_t;

dhcpc_state_t dhcpc_state = {0xFFFFFFFF, 0xFFFFFFFF};

uip_ipv4addr_t uip_ipv4_net_broadcast_addr;

/*Forward */
void update_current_dhcp_session(dhcp_session_state_t s);

static const u8_t  magic_cookie[4] =
{ 99, 130, 83, 99 };
/*---------------------------------------------------------------------------*/
static u8_t *
add_msg_type(u8_t *optptr, u8_t type)
{
  *optptr++ = DHCP_OPTION_MSG_TYPE;
  *optptr++ = 1;
  *optptr++ = type;
  return optptr;
}
/*---------------------------------------------------------------------------*/
static u8_t *
add_server_id(u8_t *optptr)
{
  *optptr++ = DHCP_OPTION_SERVER_ID;
  *optptr++ = 4;
  memcpy(optptr, dhcpc_state.serverid, 4);
  return optptr + 4;
}
/*---------------------------------------------------------------------------*/

static u8_t *
add_client_id(u8_t *optptr)
{
  *optptr++ = DHCP_OPTION_CLIENT_ID;
  *optptr++ = 9; // Length field: 1 byte type + 2 byte node ID + 6 byte home ID
  *optptr++ = 0; //Other than ethernet

  /*Keep the Z/IP gateway for changing its lan addres when entering a new network as a secondary controller */
  if(dhcpc_state.session->nodeid == MyNodeID) {
    *optptr++ =0x0;
    *optptr++ =0x0;
  } else {
    *optptr++ = dhcpc_state.session->nodeid >> 8;
    *optptr++ = dhcpc_state.session->nodeid & 0xff;
  }
  memcpy(optptr, uip_lladdr.addr, 6);
  return optptr + 6;
}
/*---------------------------------------------------------------------------*/

static u8_t *
add_req_ipaddr(u8_t *optptr)
{
  *optptr++ = DHCP_OPTION_REQ_IPADDR;
  *optptr++ = 4;
  memcpy(optptr, dhcpc_state.ipaddr.u16, 4);
  return optptr + 4;
}
/*---------------------------------------------------------------------------*/
static u8_t *
add_req_options(u8_t *optptr)
{
  *optptr++ = DHCP_OPTION_REQ_LIST;
  *optptr++ = 3;
  *optptr++ = DHCP_OPTION_SUBNET_MASK;
  *optptr++ = DHCP_OPTION_ROUTER;
  *optptr++ = DHCP_OPTION_DNS_SERVER;
  return optptr;
}
/*---------------------------------------------------------------------------*/
static u8_t *
add_end(u8_t *optptr)
{
  *optptr++ = DHCP_OPTION_END;
  return optptr;
}
/*---------------------------------------------------------------------------*/
static void create_msg(CC_REGISTER_ARG struct dhcp_msg *m)
{
  m->op = DHCP_REQUEST;
  m->htype = DHCP_HTYPE_ETHERNET;
  m->hlen = sizeof(uip_lladdr); //MAC address length
  m->hops = 0;
  memcpy(m->xid, &dhcpc_state.xid, sizeof(m->xid));
  m->secs = 0;
  m->flags = UIP_HTONS(BOOTP_BROADCAST); /*  Broadcast bit. */

  /*Assign client IP*/
  if(dhcpc_state.session->ip_suffix== 0) {
    memset(m->ciaddr, 0, sizeof(m->ciaddr));
  } else {
    ipv46nat_ipv4addr_of_entry( (uip_ipv4addr_t*)&m->ciaddr,dhcpc_state.session );
  }

  memset(m->yiaddr, 0, sizeof(m->yiaddr));
  memset(m->siaddr, 0, sizeof(m->siaddr));
  memset(m->giaddr, 0, sizeof(m->giaddr));

  memset(m->chaddr,0,sizeof(m->chaddr));
  macOfNode((uip_lladdr_t*)&m->chaddr, dhcpc_state.session->nodeid);

  memset(m->sname,0,sizeof(m->sname));
  memset(m->file,0,sizeof(m->file));

  memcpy(m->options, magic_cookie, sizeof(magic_cookie));
}
/*---------------------------------------------------------------------------*/
static void send_discover(void) CC_REENTRANT_ARG
{
  u8_t *end;
  uip_ipaddr_t addr_save;
  struct dhcp_msg *m = (struct dhcp_msg *) uip_appdata;

  create_msg(m);

  end = add_msg_type(&m->options[4], DHCPDISCOVER);
  end = add_client_id(end);
  end = add_req_options(end);
  end = add_end(end);

  addr_save = uip_hostaddr;
  memset(uip_hostaddr.u8,0,sizeof(uip_hostaddr));

  uip_ipv4_udp_packet_send(dhcpc_state.conn,uip_appdata, (int) (end - (u8_t *) uip_appdata));

  uip_hostaddr = addr_save;
}
/*---------------------------------------------------------------------------*/
static void send_request(void) CC_REENTRANT_ARG
{
  u8_t *end;
  struct dhcp_msg *m = (struct dhcp_msg *) uip_appdata;
  uip_ipaddr_t addr_save;
  create_msg(m);

  end = add_msg_type(&m->options[4], DHCPREQUEST);
  end = add_server_id(end);
  end = add_client_id(end);
  end = add_req_ipaddr(end);
  end = add_end(end);

  addr_save = uip_hostaddr;
  memset(uip_hostaddr.u8,0,sizeof(uip_hostaddr));

  uip_ipv4_udp_packet_send(dhcpc_state.conn,uip_appdata, (int) (end - (u8_t *) uip_appdata));

  uip_hostaddr = addr_save;
}

/*---------------------------------------------------------------------------*/
static void send_release(void) CC_REENTRANT_ARG
{
  u8_t *end;
  struct dhcp_msg *m = (struct dhcp_msg *) uip_appdata;
  uip_ipaddr_t addr_save;
  create_msg(m);

  end = add_msg_type(&m->options[4], DHCPRELEASE);
  end = add_server_id(end);
  end = add_client_id(end);
  end = add_end(end);

  addr_save = uip_hostaddr;
  memset(uip_hostaddr.u8,0,sizeof(uip_hostaddr));

  uip_ipv4_udp_packet_send(dhcpc_state.conn,uip_appdata, (int) (end - (u8_t *) uip_appdata));

  uip_hostaddr = addr_save;
}


/*---------------------------------------------------------------------------*/
static u8_t parse_options(u8_t *optptr, int len) CC_REENTRANT_ARG
{
  u8_t *end = optptr + len;
  u8_t type = 0;
  u32_t lease_time;

  while (optptr < end)
  {
    switch (*optptr)
    {
    case DHCP_OPTION_SUBNET_MASK:
      memcpy(dhcpc_state.netmask.u16, optptr + 2, 4);
      break;
    case DHCP_OPTION_ROUTER:
      memcpy(dhcpc_state.default_router.u16, optptr + 2, 4);
      break;
    case DHCP_OPTION_DNS_SERVER:
      memcpy(dhcpc_state.dnsaddr.u16, optptr + 2, 4);
      break;
    case DHCP_OPTION_MSG_TYPE:
      type = *(optptr + 2);
      break;
    case DHCP_OPTION_SERVER_ID:
      memcpy(dhcpc_state.serverid, optptr + 2, 4);
      break;
    case DHCP_OPTION_LEASE_TIME:
      memcpy(&lease_time, optptr + 2, 4);
      lease_time = UIP_HTONL(lease_time);

      if(dhcpc_state.lease_time > lease_time) {
        dhcpc_state.lease_time = lease_time;
        if(dhcpc_state.ticks > lease_time) {
          dhcpc_state.ticks = lease_time;
        }
      }
      break;
    case DHCP_OPTION_END:
      return type;
    }

    optptr += optptr[1] + 2;
  }
  return type;
}
/*---------------------------------------------------------------------------*/
static u8_t parse_msg(void)
{
  uip_lladdr_t mac;
  struct dhcp_msg *m = (struct dhcp_msg *) uip_appdata;
  if(dhcpc_state.session == 0 ) {
	  return 0;
  }

  macOfNode(&mac,dhcpc_state.session->nodeid);
  if (m->op == DHCP_REPLY &&
      memcmp(&m->xid, &dhcpc_state.xid, sizeof(u32_t)) == 0 &&
      memcmp(m->chaddr, &mac, sizeof(uip_lladdr)) == 0)
  {
    memcpy(dhcpc_state.ipaddr.u8, m->yiaddr, 4);
    return parse_options(&m->options[4], uip_datalen());
  }
  return 0;
}
/*---------------------------------------------------------------------------*/
/*
 * Is this a "fresh" reply for me? If it is, return the type.
 */
static int msg_for_me(void) CC_REENTRANT_ARG
{
  uip_lladdr_t mac;
  struct dhcp_msg *m = (struct dhcp_msg *) uip_appdata;
  u8_t *optptr = &m->options[4];
  u8_t *end = (u8_t*) uip_appdata + uip_datalen();

  if(dhcpc_state.session == 0 ) {
	return -1;
  }

  macOfNode(&mac,dhcpc_state.session->nodeid);
  if (m->op == DHCP_REPLY &&
      memcmp(&m->xid, &dhcpc_state.xid, sizeof(u32_t)) == 0 &&
      memcmp(m->chaddr, &mac, sizeof(uip_lladdr)) == 0)
  {
    while (optptr < end)
    {
      if (*optptr == DHCP_OPTION_MSG_TYPE)
      {
        return *(optptr + 2);
      } else if (*optptr == DHCP_OPTION_END)
      {
        return -1;
      }
      optptr += optptr[1] + 2;
    }
  }
  return -1;
}


void dhcp_release( nat_table_entry_t *s ) {
  nat_table_entry_t * tmp = dhcpc_state.session;

  if (!(process_is_running(&dhcp_client_process))) {
      return;
  }
  if(s->ip_suffix) {
    dhcpc_state.session = s;
    send_release();
    dhcpc_state.session = tmp;
  }
}

static void start_discover( nat_table_entry_t *s ) {
  dhcpc_state.session =s;
  dhcpc_state.xid=rand();

  DBG_PRINTF("We should send a discover\n");
  dhcpc_state.retry = 0;
  dhcpc_state.state = DHCP_SEND_DISCOVER;
  tcpip_ipv4_poll_udp(dhcpc_state.conn);

}

/*
 * Should be called when we are ready for a new DHCP session.
 *
 * Uses uip_hostaddr to determine if we have an address for the
 * gateway itself.
 */
void dhcp_check_for_new_sessions() {
  u8_t i;

  if (!(process_is_running(&dhcp_client_process))) {
      return;
  }
  DBG_PRINTF("Checking for new sessions\n");
  if (dhcpc_state.state != DHCP_IDLE) {
    return;
  }

  if(dhcpc_state.last_renew < nat_table_size) {
    if(nat_table[dhcpc_state.last_renew].ip_suffix) {
      dhcpc_state.session = &nat_table[dhcpc_state.last_renew];
      dhcpc_state.xid++;
      ipv46nat_ipv4addr_of_entry((uip_ipv4addr_t*)&dhcpc_state.ipaddr, dhcpc_state.session  );

      DBG_PRINTF("We should send a request for node %i\n",dhcpc_state.session->nodeid);
      dhcpc_state.state = DHCP_SEND_REQUEST;
      tcpip_ipv4_poll_udp(dhcpc_state.conn);

      dhcpc_state.last_renew++;
      return;
    } else {
      dhcpc_state.last_renew++;
    }
  }

  /*First discover the gateway itself*/
  if(uip_hostaddr.u16[0] == 0 && uip_hostaddr.u16[1] == 0) {
    for(i=0; i < nat_table_size; i++) {
      if(nat_table[i].nodeid == MyNodeID && nat_table[i].ip_suffix ==0) {
        start_discover(&nat_table[i]);
        return;
      }
    }
  }

  for(i=0; i < nat_table_size; i++) {
    if(nat_table[i].ip_suffix ==0) {
      start_discover(&nat_table[i]);
      return;
    }
  }

  /*We are done with the pass and all renews has succeeded*/
  if(dhcpc_state.renew_failed == 0) {
    dhcpc_state.ticks = dhcpc_state.lease_time;
    dhcpc_state.timeout = dhcpc_state.lease_time/2;
  }
  process_post(&zip_process,ZIP_EVENT_ALL_IPV4_ASSIGNED,0);
}

void dhcpc_session_abort() {
  update_current_dhcp_session(DHCP_IDLE);
}


/* Should be called when the current session has been updated, but also when the timer expires */
void update_current_dhcp_session(dhcp_session_state_t new_state) {
  //PRINTF("New DHCP state %i\n", new_state);
  if(new_state == DHCP_UDP_POLL) {
    new_state = dhcpc_state.state;
    dhcpc_state.state = DHCP_IDLE;
  }

  if(dhcpc_state.session==0) {
    return;
  }

  switch(new_state) {
  case DHCP_IDLE:
    dhcpc_state.state = DHCP_IDLE;
    dhcpc_state.session = 0;
    dhcpc_state.retry=0;
    dhcpc_state.session_timeout = 0;
    dhcp_check_for_new_sessions();
    return;
  case DHCP_SEND_DISCOVER:
    if(dhcpc_state.state == DHCP_IDLE || dhcpc_state.state == DHCP_SEND_DISCOVER) {
      if(dhcpc_state.session->nodeid == MyNodeID) {
        if(uip_hostaddr.u16[0] !=0 && uip_hostaddr.u16[1] !=0) {
          dhcpc_state.ipaddr=uip_hostaddr;
          dhcpc_state.netmask=uip_netmask;
          dhcpc_state.default_router= uip_draddr;
          update_current_dhcp_session(DHCP_SEND_REQUEST);
          return;
        }
      }

      //uip_hostaddr = dhcpc_state.ipaddr;

      DBG_PRINTF("Sending DISCOVER\n");

      etimer_restart(&dhcpc_state.timer);

      dhcpc_state.retry++;
      if(dhcpc_state.retry > 4) {
        dhcpc_state.retry=1;
      }
      dhcpc_state.session_timeout = 1<< dhcpc_state.retry;

      send_discover();
      dhcpc_state.state = DHCP_SEND_DISCOVER;
    }
    break;
  case DHCP_RECV_OFFER:
    if(dhcpc_state.state == DHCP_SEND_DISCOVER) {
      DBG_PRINTF("got OFFER\n");
      parse_msg();
      dhcpc_state.state = DHCP_SEND_REQUEST;
      tcpip_ipv4_poll_udp(dhcpc_state.conn); //Make the tcpip process kick the dhcp state*/
    }
    break;
  case DHCP_SEND_REQUEST:
    if(dhcpc_state.state == DHCP_RECV_OFFER || dhcpc_state.state == DHCP_IDLE) {
      DBG_PRINTF("send REQUEST\n");
      /* TODO: Add exponential backoff */
      dhcpc_state.session_timeout = 4;
      etimer_restart(&dhcpc_state.timer);
      send_request();
      dhcpc_state.state = DHCP_SEND_REQUEST;
    }
    break;
  case DHCP_RECV_ACK:
    if(dhcpc_state.state == DHCP_SEND_REQUEST) {
      uip_ipaddr_t a;

      DBG_PRINTF("got ACK\n");
      dhcpc_state.session->ip_suffix = dhcpc_state.ipaddr.u16[1] & (~dhcpc_state.netmask.u16[1]);

      if(dhcpc_state.session->nodeid == MyNodeID) {
        uip_hostaddr = dhcpc_state.ipaddr;
        uip_netmask = dhcpc_state.netmask;
        uip_draddr = dhcpc_state.default_router;
        uip_ipv4_net_broadcast_addr.u16[0] = uip_hostaddr.u16[0] | ~uip_netmask.u16[0];
        uip_ipv4_net_broadcast_addr.u16[1] = uip_hostaddr.u16[1] | ~uip_netmask.u16[1];

        resolv_conf(&dhcpc_state.dnsaddr);

#ifdef __ASIX_C51__
        //Update STOE engine with IPv4 address
        STOE_SetIPAddr(*((uint32_t *)dhcpc_state.ipaddr.u8));
        STOE_SetGateway(*((uint32_t *)dhcpc_state.default_router.u8));
        STOE_SetSubnetMask(*((uint32_t *)dhcpc_state.netmask.u8));
		printf("Default Gateway address %bu.%bu.%bu.%bu\n", dhcpc_state.default_router.u8[0],dhcpc_state.default_router.u8[1],dhcpc_state.default_router.u8[2],dhcpc_state.default_router.u8[3] );
		printf("DNS Server address %bu.%bu.%bu.%bu\n", dhcpc_state.dnsaddr.u8[0],dhcpc_state.dnsaddr.u8[1],dhcpc_state.dnsaddr.u8[2],dhcpc_state.dnsaddr.u8[3] );
#endif
      }
#ifdef __C51__
      process_post(&zip_process,ZIP_EVENT_NODE_IPV4_ASSIGNED,(void*) (DWORD)dhcpc_state.session->nodeid);
#else
      process_post(&zip_process,ZIP_EVENT_NODE_IPV4_ASSIGNED,(void*) (intptr_t)dhcpc_state.session->nodeid);
#endif
      ipv46nat_ipv4addr_of_entry((uip_ipv4addr_t*)&a,dhcpc_state.session);
      LOG_PRINTF("Node %u has ipv4 address %u.%u.%u.%u\n",dhcpc_state.session->nodeid,a.u8[0],a.u8[1],a.u8[2],a.u8[3] );

      update_current_dhcp_session(DHCP_IDLE);
    }
    break;
  case DHCP_RECV_NAK:
    if(dhcpc_state.state == DHCP_SEND_REQUEST) {
      WRN_PRINTF("got DHCP NAK\n");

      /**
       * Clear my address
       */
      if(dhcpc_state.session->nodeid == MyNodeID) {
        memset(&uip_hostaddr,0,sizeof(uip_ipaddr_t));
        memset(&uip_netmask,0,sizeof(uip_ipaddr_t));
        memset(&uip_draddr,0,sizeof(uip_ipaddr_t));
      }

      dhcpc_state.session->ip_suffix = 0;

      /*Trigger a timeout*/
      dhcpc_state.session_timeout = 1;
      etimer_restart(&dhcpc_state.timer);
//      update_current_dhcp_session(DHCP_IDLE);
    }
    break;
  case DHCP_TIME_OUT:
#ifdef __C51__
      process_post(&zip_process,ZIP_EVENT_NODE_DHCP_TIMEOUT,(void*) (DWORD)dhcpc_state.session->nodeid);
#else
      process_post(&zip_process,ZIP_EVENT_NODE_DHCP_TIMEOUT,(void*) (intptr_t)dhcpc_state.session->nodeid);
#endif
    WRN_PRINTF("DHCP Timeout\n");
    if(dhcpc_state.state == DHCP_SEND_DISCOVER) {
      update_current_dhcp_session(DHCP_SEND_DISCOVER);
    } else {
      dhcpc_state.renew_failed = 1;
      update_current_dhcp_session(DHCP_IDLE);
    }

    break;
  case DHCP_UDP_POLL: //dummy
    break;
  }
}

static void timeout(void) {

  if(dhcpc_state.session_timeout) {
    dhcpc_state.session_timeout--;
    if(dhcpc_state.session_timeout==0) {
      update_current_dhcp_session(DHCP_TIME_OUT);
    }
  }


  if(dhcpc_state.ticks == 0) {
    int i;
    /*This is T2, all is lost */
    ERR_PRINTF("DHCP Lease timeout. Dropping all addresses.\n");
    memset(&uip_hostaddr,0,sizeof(uip_ipaddr_t));
    memset(&uip_netmask,0,sizeof(uip_ipaddr_t));
    memset(&uip_draddr,0,sizeof(uip_ipaddr_t));

    for(i=0; i < nat_table_size; i++) {
      nat_table[i].ip_suffix =0;
    }

    dhcpc_state.ticks   = 120;
    dhcpc_state.timeout = dhcpc_state.ticks;
  } else {
    dhcpc_state.ticks--;
  }

  if(dhcpc_state.ticks == dhcpc_state.timeout) {
    /*This is T1 */
    dhcpc_state.last_renew=0;

    if (dhcpc_state.timeout > 120) {
      dhcpc_state.timeout /= 2;
    } else {
      if (dhcpc_state.ticks <= 60) {
	// No more timeouts
	dhcpc_state.timeout = 0;
      } else {
	dhcpc_state.timeout = dhcpc_state.ticks - 60;
      }
    }

    dhcpc_state.renew_failed = 0;

    WRN_PRINTF("timeout leasetime %u\n",dhcpc_state.lease_time);
    dhcp_check_for_new_sessions();
  }
  etimer_restart(&dhcpc_state.timer);
}

PROCESS_THREAD(dhcp_client_process, ev, data) {
  int type;
  uip_ipaddr_t addr;
  PROCESS_BEGIN();

  if (cfg.ipv4disable) {
      PROCESS_EXIT();
  }
    LOG_PRINTF("DHCP client started\n");

//    etimer_stop(&dhcpc_state.timer);

    uip_ipv4_ipaddr(&addr, 255,255,255,255);

    dhcpc_state.conn = udp_ipv4_new(&addr, UIP_HTONS(DHCPC_SERVER_PORT), &dhcpc_state);
    if(dhcpc_state.conn != NULL) {
      udp_ipv4_bind(dhcpc_state.conn, UIP_HTONS(DHCPC_CLIENT_PORT));
    } else {
      ERR_PRINTF("could not initialize dhcp connection\n");
    }

    udp_bind(dhcpc_state.conn, UIP_HTONS(DHCPC_CLIENT_PORT));

    //Do not clear the all the states, we need stuff like the dhcp server id.
    //memset(&dhcpc_state,0,sizeof(dhcpc_state));
    dhcpc_state.lease_time = 0xFFFFFFFF;
    dhcpc_state.state = DHCP_IDLE;
    dhcpc_state.last_renew = 0;
//    dhcpc_state.ticks = 0;

    /*1s timer*/
    etimer_set(&dhcpc_state.timer,1000);
    //ctimer_set(&dhcpc_state.timer,1000,timeout,0);

    while (1) {
      PROCESS_YIELD();
      if(ev == PROCESS_EVENT_TIMER&& (data == &dhcpc_state.timer) ) {
        timeout();
      }else if (ev == tcpip_event && data == &dhcpc_state) {

        if(uip_newdata()) {
          type = msg_for_me();
          switch(type) {
          case DHCPOFFER:
            update_current_dhcp_session(DHCP_RECV_OFFER);
            break;
          case DHCPACK:
            update_current_dhcp_session(DHCP_RECV_ACK);
            break;
          case DHCPNAK:
            update_current_dhcp_session(DHCP_RECV_NAK);
            break;
           /*These are client messages */
          case DHCPDECLINE:
          case DHCPREQUEST:
          case DHCPDISCOVER:
          case DHCPRELEASE:
            break;
          default:
            //PRINTF("message is not for me type=%d\n",type);
            break;
          }
        } else {
          update_current_dhcp_session(DHCP_UDP_POLL);
        }

      }
    }
  PROCESS_END();
  return 0;
}

u8_t dhcpc_answer_pending() {
  return (dhcpc_state.state == DHCP_SEND_DISCOVER ||dhcpc_state.state == DHCP_SEND_REQUEST);
}
/*--------------------------------------------------------------------------*/
