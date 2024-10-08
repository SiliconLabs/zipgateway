/*
 * Copyright (c) 2004, Adam Dunkels and the Swedish Institute of
 * Computer Science.
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
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.  
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  
 *
 * This file is part of the uIP TCP/IP stack and the Contiki operating system.
 *
 * $Id: uiplib.c,v 1.2 2010/05/31 15:22:08 nifi Exp $
 *
 */


#include "net/uip.h"
#include "net/uiplib.h"
#include <string.h>

#define DEBUG DEBUG_NONE
#include "net/uip-debug.h"

/*-----------------------------------------------------------------------------------*/
int
uiplib_ipaddrconv(const char *addrstr, uip_ipaddr_t *ipaddr)
{
#if UIP_CONF_IPV6
  uint16_t value;
  int tmp, zero;
  size_t len;
  char c=0;

  value = 0;
  zero = -1;
  for(len = 0; len < sizeof(uip_ipaddr_t) - 1; addrstr++) {
    c = *addrstr;
    if(c == ':' || c == '\0') {
      ipaddr->u8[len] = (value >> 8) & 0xff;
      ipaddr->u8[len + 1] = value & 0xff;
      len += 2;
      value = 0;

      if(c == '\0') {
        break;
      }

      if(*(addrstr + 1) == ':') {
        /* Zero compression */
        if(zero < 0) {
          zero = len;
        }
        addrstr++;
      }
    } else {
      if(c >= '0' && c <= '9') {
        tmp = c - '0';
      } else if(c >= 'a' && c <= 'f') {
        tmp = c - 'a' + 10;
      } else if(c >= 'A' && c <= 'F') {
        tmp = c - 'A' + 10;
      } else {
        PRINTF("uiplib: illegal char: '%c'\n", c);
        return 0;
      }
      value = (value << 4) + (tmp & 0xf);
    }
  }
  if(c != '\0') {
    PRINTF("uiplib: too large address\n");
    return 0;
  }
  if(len < sizeof(uip_ipaddr_t)) {
    if(zero < 0) {
      PRINTF("uiplib: too short address\n");
      return 0;
    }
    memmove(&ipaddr->u8[zero + sizeof(uip_ipaddr_t) - len],
            &ipaddr->u8[zero], len - zero);
    memset(&ipaddr->u8[zero], 0, sizeof(uip_ipaddr_t) - len);
  }

#else /* UIP_CONF_IPV6 */

  unsigned char tmp;
  char c;
  unsigned char i, j;

  tmp = 0;
  
  for(i = 0; i < 4; ++i) {
    j = 0;
    do {
      c = *addrstr;
      ++j;
      if(j > 4) {
	return 0;
      }
      if(c == '.' || c == 0) {
	ipaddr->u8[i] = tmp;
	tmp = 0;
      } else if(c >= '0' && c <= '9') {
	tmp = (tmp * 10) + (c - '0');
      } else {
	return 0;
      }
      ++addrstr;
    } while(c != '.' && c != 0);
  }
#endif /* UIP_CONF_IPV6 */
  return 1;
}

/*-----------------------------------------------------------------------------------*/
