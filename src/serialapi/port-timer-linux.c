/* © 2014 Silicon Laboratories Inc.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <termios.h>
#include <time.h>

#include "port.h"


void port_timer_set(struct linux_timer *t, int interval) {
  t->interval = interval;
  port_timer_restart(t);
}

void port_timer_restart(struct linux_timer *t){
  struct timeval now;
  struct timeval d;
  gettimeofday(&now,0);
  d.tv_sec = (t->interval / 1000);
  d.tv_usec = (t->interval % 1000) *1000;
  timeradd(&d, &now, &(t->timeout));
}

int port_timer_expired(struct linux_timer *t){
  struct timeval now;
  gettimeofday(&now,0);
  return (timercmp(&t->timeout, &now, <));
}

clock_time_t clock_time(void)
{
  struct timeval tv;
  gettimeofday(&tv, 0);
  return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

unsigned long clock_seconds(void)
{
  struct timeval tv;
  gettimeofday(&tv, 0);
  return tv.tv_sec;
}