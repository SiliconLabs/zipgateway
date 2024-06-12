/*
 * Copyright (c) 2002, Adam Dunkels.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
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
 * This file is part of the Contiki OS
 *
 * $Id: contiki-main.c,v 1.13 2010/06/14 18:58:45 adamdunkels Exp $
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "contiki.h"

#include "dev/serial-line.h"
#include "tapdev-drv.h"
#include "tapdev6.h"
#include "net/uip.h"

#include "dev/eeprom.h"
#include "router_events.h"
#include "parse_config.h"
#include "zgw_backup.h"
#include <stdlib.h>
#include "serial_api_process.h"
PROCINIT(&etimer_process);

extern void set_landev_outputfunc(u8_t (* f)(uip_lladdr_t *a));


int prog_argc;
char** prog_argv;
static int interrupted = 0;

extern int net_fd;
extern int serial_fd;

void sigusr1_handler(int num)
{

  printf("Received SIGUSR1, asking gateway to backup\n");
  if(zgw_backup_init()) {
      process_post(&zip_process, ZIP_EVENT_BACKUP_REQUEST, NULL);
  } else {
      printf("ERROR: Backup init failed\n");
  }
}

void exit_handler(int num)
{
  if (num == SIGHUP) {
     signal(SIGHUP,exit_handler );
     printf("Received SIGHUP, resetting Gateway\n");
     process_post(&zip_process,ZIP_EVENT_RESET,0);
     return;
  }
  //num=num;
  printf("Interrupted\n");

  /*Force quit if this is the second interrupt*/
  if(interrupted) {
    exit(1);
  }

  process_post(PROCESS_BROADCAST,PROCESS_EVENT_EXIT,0);
  interrupted = 1;
}

/*---------------------------------------------------------------------------*/
int
main(int argc, char** argv)
{
  prog_argc = argc;
  prog_argv = argv;

  signal(SIGINT,exit_handler );
  signal(SIGUSR1,sigusr1_handler );
  signal(SIGTERM,exit_handler );
  signal(SIGHUP,exit_handler );

  ConfigInit();

  printf("Starting Contiki\n");
  set_landev_outputfunc(tapdev_send);

  process_init();
  ctimer_init();

  procinit_init();

  serial_line_init();
  process_start(&tapdev_process,0);

  autostart_start(autostart_processes);


  /* Make standard output unbuffered. */
  setvbuf(stdout, (char *)NULL, _IONBF, 0);

  while(1) {
    fd_set fds;
    int n;
    struct timeval tv;

    /* Keep going as long as there are events on the event queue or poll has
     * been requested (process_run() processes one event every time it's called)
     */
    while (process_run()) {

    }

    // Bail out if zip_process has stopped running for any reason
    if (!process_is_running(&zip_process))
    {
      break;
    }

    clock_time_t delay  = etimer_next_expiration_time();

    if (delay < clock_time())
        delay = 100;
    else if ( delay ) {
      delay -= clock_time();
    } else {
      delay = 200;
    }

    tv.tv_sec = delay / 1000;
    tv.tv_usec = (delay % 1000)*1000;

    FD_ZERO(&fds);
    if (isatty(STDIN_FILENO))
    {
       FD_SET(STDIN_FILENO, &fds);
    }
    FD_SET(serial_fd, &fds);
    FD_SET(net_fd, &fds);

    n = serial_fd > net_fd ? serial_fd : net_fd;
    //printf("delay %i\n",delay);
    if( select(n+1, &fds, NULL, NULL, &tv) > 0) {

      if (FD_ISSET(net_fd, &fds)) {
        process_poll(&tapdev_process);
        //printf("net input\n");
      }

      if (FD_ISSET(serial_fd, &fds)) {
        process_poll(&serial_api_process);
        //printf("serial input\n");
      }

      if(FD_ISSET(STDIN_FILENO, &fds) && interrupted==0) {
        char c;
        //printf("stdin input\n");
        if(read(STDIN_FILENO, &c, 1) > 0) {
          serial_line_input_byte(c);
        }
      }
    } else {
      //printf("timeout %i\n",delay);
    }
    etimer_request_poll();
  }

  return 0;
}
/*---------------------------------------------------------------------------*/
void
log_message(char *m1, char *m2)
{
  printf("%s%s\n", m1, m2);
}
/*---------------------------------------------------------------------------*/
void
uip_log(char *m)
{
  printf("%s\n", m);
}
