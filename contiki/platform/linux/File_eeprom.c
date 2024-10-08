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
 * Author: Adam Dunkels <adam@sics.se>
 *
 * $Id: eeprom.c,v 1.1 2006/06/17 22:41:31 adamdunkels Exp $
 */
#include "dev/eeprom.h"
#include <stdio.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int f;
const char* linux_conf_eeprom_file;


void eeprom_init(void) {
  printf("Opening eeprom file %s\n", linux_conf_eeprom_file);
  f = open(linux_conf_eeprom_file, O_RDWR | O_CREAT, 0644);
  if(f<0) {
    fprintf(stderr, "Error opening eeprom file %s\n",linux_conf_eeprom_file);
    perror("");
    exit(1);
  }

}

void eeprom_close() {
  close(f);
}

void
eeprom_write(eeprom_addr_t addr, unsigned char *buf, int size)
{
  lseek(f, addr, SEEK_SET);
  if(write(f, buf, size) != size) {
    perror("Write error");
  }

//  sync();
}

void
eeprom_read(eeprom_addr_t addr, unsigned char *buf, int size)
{
  lseek(f, addr, SEEK_SET);
  if(read(f, buf, size)!=size) {
    perror("Read error");
  }
}

eeprom_addr_t eeprom_size()
{
  return lseek(f, 0, SEEK_END);
}
