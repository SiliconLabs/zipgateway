/*
 * Copyright (c) 2010, Swedish Institute of Computer Science.
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
 * $Id: uip-debug.c,v 1.1 2010/04/30 13:20:57 joxe Exp $
 */

/**
 * \file
 *         A set of debugging tools
 * \author
 *         Nicolas Tsiftes <nvt@sics.se>
 *         Niclas Finne <nfi@sics.se>
 *         Joakim Eriksson <joakime@sics.se>
 */

#define DEBUG DEBUG_FULL
#include "net/uip-debug.h"
/*---------------------------------------------------------------------------*/


void uip_udp_print_conn(struct uip_udp_conn* conn) {
  char buf1[128];
  char buf2[128];

  uip_ipaddr_sprint(buf1,&conn->sipaddr);
  uip_ipaddr_sprint(buf2,&conn->ripaddr);

  printf("lipaddr: %s lport: %i\nripaddr: %s rport: %i\n",
      buf1, UIP_HTONS(conn->lport),
      buf2, UIP_HTONS(conn->rport)
  );
}
void uip_dump_buf() {
  int i;

  printf("uip_buf: ");
  for(i=0; i < uip_len+UIP_LLH_LEN; i++) {
    printf("%02x ",uip_buf[i]);
  }
  printf("\n");
}


int
uip_ipaddr_sprint(char* start,const uip_ipaddr_t *addr)	CC_REENTRANT_ARG
{
  const char hex_digits[] = "0123456789abcdef";
  uint16_t a;
  int i, f;
  char* s = start;
  for(i = 0, f = 0; i < sizeof(uip_ipaddr_t); i += 2) {
    a = (addr->u8[i] << 8) + addr->u8[i + 1];
    if(a == 0 && f >= 0) {
      if(f++ == 0) {
        *s++ =':';
        *s++ =':';
      }
    } else {
      if(f > 0) {
        f = -1;
      } else if(i > 0) {
        *s++ =':';
      }
      *s++ = hex_digits[ (a >> 12) & 0xF ];
      *s++ = hex_digits[ (a >> 8) & 0xF ];
      *s++ = hex_digits[ (a >> 4) & 0xF ];
      *s++ = hex_digits[ (a >> 0) & 0xF ];
    }
  }
  *s=0;
  return s-start;
}

void
uip_debug_ipaddr_print(const uip_ipaddr_t *addr)
{
#if UIP_CONF_IPV6
  uint16_t a;
  int i, f;
  for(i = 0, f = 0; i < sizeof(uip_ipaddr_t); i += 2) {
    a = (addr->u8[i] << 8) + addr->u8[i + 1];
    if(a == 0 && f >= 0) {
      if(f++ == 0) {
        printf(":");
        printf(":");
      }
    } else {
      if(f > 0) {
        f = -1;
      } else if(i > 0) {
        printf(":");
      }
      printf("%02x",a);
    }
  }
  printf("\n");
#else /* UIP_CONF_IPV6 */
  printf("%u.%u.%u.%u", addr->u8[0], addr->u8[1], addr->u8[2], addr->u8[3]);
#endif /* UIP_CONF_IPV6 */
}
/*---------------------------------------------------------------------------*/
void
uip_debug_lladdr_print(const uip_lladdr_t *addr)
{
  int i;
  for(i = 0; i < sizeof(uip_lladdr_t); i++) {
    if(i > 0) {
      printf(":");
    }
    printf("%02x",addr->addr[i]);
  }
  printf("\n");
}
