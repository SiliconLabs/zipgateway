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
 * This file is part of the Contiki operating system.
 *
 * $Id: uip-debug.h,v 1.1 2010/04/30 13:20:57 joxe Exp $
 */
/**
 * \file
 *         A set of debugging macros.
 *
 * \author Nicolas Tsiftes <nvt@sics.se>
 *         Niclas Finne <nfi@sics.se>
 *         Joakim Eriksson <joakime@sics.se>
 */

#ifndef UIP_DEBUG_H
#define UIP_DEBUG_H

#include "net/uip.h"

#include <stdio.h>

#ifdef ZIP_NATIVE
#include <ZW_typedefs.h>
#include <ZW_uart_api.h>
#else
#ifndef ZW_DEBUG_SEND_NUM
#define ZW_LOG(a,b)
#define ZW_DEBUG_SEND_BYTE(c) printf("%i",c)
#define ZW_DEBUG_SEND_NUMW(n) printf("%i",n)
#define ZW_DEBUG_SEND_NUM(n) printf("%i",n)
#define ZW_DEBUG_SEND_NL() printf("\n")
#endif
#endif

void uip_dump_buf();
void uip_debug_ipaddr_print(const uip_ipaddr_t *addr);
void uip_debug_lladdr_print(const uip_lladdr_t *addr);
int uip_ipaddr_sprint(char* start,const uip_ipaddr_t *addr)CC_REENTRANT_ARG;
void uip_udp_print_conn(struct uip_udp_conn* conn);

#define DEBUG_NONE      0
#define DEBUG_PRINT     1
#define DEBUG_ANNOTATE  2
#define DEBUG_FULL      DEBUG_ANNOTATE | DEBUG_PRINT

#if (DEBUG) & DEBUG_ANNOTATE
#include <stdio.h>
#if ! CC_NO_VA_ARGS
#define ANNOTATE(...) printf(__VA_ARGS__)
#else
// TODO: Add ANNOTATE substitute here (without variadic macro)
#endif
#else
#if ! CC_NO_VA_ARGS
#define ANNOTATE(...)
#else
// TODO: Add ANNOTATE substitute here (without variadic macro)
#endif
#endif /* (DEBUG) & DEBUG_ANNOTATE */

#if (DEBUG) & DEBUG_PRINT
//#include <printf.h>
#if ! CC_NO_VA_ARGS
#ifdef ZIP_NATIVE
#define PRINTF(...)
#else
#define PRINTF(...) printf(__VA_ARGS__)
#endif
#else
#ifdef __ASIX_C51__
//#define PRINTF	printf
#define PRINTF
#endif /* __ASIX_C51__ */
// TODO: Find a way to get rid of PRINTF(...) without variadic macros
#endif /* ! CC_NO_VA_ARGS */
#define PRINT6ADDR(addr) uip_debug_ipaddr_print(addr)
#define PRINTLLADDR(lladdr) uip_debug_lladdr_print(lladdr)
#ifdef ZIP_NATIVE
#define CHAR(X)  #X[0] // From http://en.wikipedia.org/wiki/C_preprocessor#Brainteaser

#define ZW_LOG(module, msgno) \
    do { \
        ZW_DEBUG_SEND_BYTE(CHAR(module)); \
        ZW_DEBUG_SEND_NUM(msgno); \
        ZW_DEBUG_SEND_NL(); \
    } while (0)     /* Standard pattern for multiple statements in one macro */

#endif /* ZIP_NATIVE */
#else /* DEBUG_PRINT */
#if ! CC_NO_VA_ARGS
#define PRINTF(...)
#else
#ifdef __ASIX_C51__
#define PRINTF(x)	
#endif /* __ASIX_C51__ */
// TODO: Find a way to get rid of PRINTF(...) without variadic macros
#endif
#define PRINTLLADDR(lladdr)
#define PRINT6ADDR(addr)
#ifdef ZIP_NATIVE
#define ZW_LOG(module, msgno)
#endif
#endif /* (DEBUG) & DEBUG_PRINT */

#endif
