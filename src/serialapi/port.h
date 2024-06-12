/* © 2014 Silicon Laboratories Inc.
 */
#ifndef PORT_H_
#define PORT_H_

#ifdef __ASIX_C51__
#define PLATFORM_TIMER_SLOW_COMPENSATION_FACTOR	1 //if your platform is slow in processing the data, increase the timeouts by this factor
#endif

#ifdef __ROUTER_VERSION__
#include "lib/random.h"
#define PORT_RANDOM random_rand
/* Port of Contiki timers */
#include "sys/timer.h"
#define PORT_TIMER struct timer
#define PORT_TIMER_RESTART(t) timer_restart(&t);
#define PORT_TIMER_EXPIRED(t) timer_expired(&t)
#ifdef __ASIX_C51__
#define PORT_TIMER_INIT(t,msec) timer_set(&t, (msec * PLATFORM_TIMER_SLOW_COMPENSATION_FACTOR))
#else
#define PORT_TIMER_INIT(t,msec) timer_set(&t, msec);
#endif
#else // not __ROUTER_VERSION__
#ifdef _MSC_VER
struct timer {
  int interval;
  DWORD timeHi;
  DWORD timeLo;
};
void timer_set(struct timer *t, int interval);
void timer_restart(struct timer *t);
int timer_expired(struct timer *t);

#define PORT_TIMER struct timer
#define PORT_TIMER_RESTART(t) timer_restart(&t)
#define PORT_TIMER_EXPIRED(t) timer_expired(&t)
#define PORT_TIMER_INIT(t,msec) timer_set(&t, (clock_time_t)msec)
#else
#include <sys/time.h>
#include <stdlib.h>
struct linux_timer {
  int interval;
  struct timeval timeout;
};
typedef unsigned long clock_time_t;
#define PORT_TIMER struct linux_timer

void port_timer_set(PORT_TIMER *t, int interval);
void port_timer_restart(PORT_TIMER *t);
int port_timer_expired(PORT_TIMER *t);

#define PORT_TIMER_RESTART(t) port_timer_restart(&t)
#define PORT_TIMER_EXPIRED(t) port_timer_expired(&t)

#ifdef __ASIX_C51__
#define PORT_TIMER_INIT(t,msec) timer_set(&t, (msec * PLATFORM_TIMER_SLOW_COMPENSATION_FACTOR))
#else
#define PORT_TIMER_INIT(t,msec) port_timer_set(&t, (clock_time_t)msec)
#endif

#define PORT_RANDOM rand

#endif // _MSC_VER

#endif //__ROUTER_VERSION__

/**
 * Initialize the serial port to 115200 BAUD 8N1.
 * */
int SerialInit(const char* serial_port);

/** Returns <0 if no characters are available, otherwise this returns the character */
#ifdef __ASIX_C51__
int SerialGetByte(unsigned char *cptr); //Arif - changed prototype
#else
int SerialGetByte();
#endif

/**
 * Write character to the serial port device.
 */
void SerialPutByte(unsigned char c);

/**
 * Check if new serial data is available. On some system this call in unrelated to \ref SerialGetByte.
 */
int  SerialCheck();

/**
 * Flush the serial output if using buffered output.
 */
void SerialFlush();

/**
 * De-initialize the serial port.
 */
void SerialClose();

/**
 * De-initialize and re-initialize the current serial port to 115200 BAUD 8N1.
 */
int SerialRestart();


int SerialGetBuffer(unsigned char* c, int len);
void SerialPutBuffer(unsigned char* c, int len);


#endif /* PORT_H_ */