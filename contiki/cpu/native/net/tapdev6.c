/*
 * Copyright (c) 2001, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
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
 * Author: Adam Dunkels <adam@sics.se>
 *
 * $Id: tapdev6.c,v 1.4 2010/10/19 18:29:04 adamdunkels Exp $
 */


#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/socket.h>


#ifdef __linux
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#define DEVTAP "/dev/net/tun"
#else
#define DEVTAP "/dev/tap0"
#endif

#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

#include "zip_router_config.h" //for struct cfg
#include "tapdev6.h"
#include "contiki-net.h"
#include "ipv46_if_handler.h"
#include "uip-debug.h"
#include "ZIP_Router_logging.h"

int net_fd;

static unsigned long lasttime;
extern const char* linux_conf_tun_script;

#define BUF ((struct uip_eth_hdr *)&uip_buf[0])
#define IPBUF ((struct uip_tcpip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP6_STRLEN 64

u8_t do_send(void);
u8_t tapdev_send(uip_lladdr_t *lladdr);
#ifdef __linux
static struct ifreq ifr;
#endif
u16_t
tapdev_poll(void)
{
  fd_set fdset;
  struct timeval tv;
  int ret;

  tv.tv_sec = 0;
  tv.tv_usec = 0;

  FD_ZERO(&fdset);
  if(net_fd > 0) {
    FD_SET(net_fd, &fdset);
  }

  ret = select(net_fd + 1, &fdset, NULL, NULL, &tv);

  if(ret == 0) {
    return 0;
  }
  ret = read(net_fd, uip_buf, UIP_BUFSIZE);

  //PRINTF("tapdev6: read %d bytes (max %d)\n", ret, UIP_BUFSIZE);

  if(ret == -1) {
    perror("tapdev_poll: read");
  }
#define DROP_RX 0
#if DROP_RX
  if ((rand() & 0x3F) == 0)
  {
    printf("Dropping incoming packet\n");
    return 0;
  }
#endif

  return ret;
}

void system_net_hook(int init) {
  char lan_ip[UIP6_STRLEN] = {0};
  char han_prefix[UIP6_STRLEN] = {0}; 
  char tundev[UIP6_STRLEN] = {0};
  char *execve_args[3] = {0};
  int index, status;
  pid_t pid = 0;
  pid_t ret;

  /* LANIP */
  index = 0;
  index += snprintf(lan_ip, UIP6_STRLEN, "LANIP=");
  index += uip_ipaddr_sprint(lan_ip + index, &cfg.lan_addr);
  /* HAN_PREFIX */
  index = 0;
  index += snprintf(han_prefix, UIP6_STRLEN, "HANPREFIX=");
  index += uip_ipaddr_sprint(han_prefix + index, &cfg.pan_prefix);
  index += snprintf(han_prefix + index, UIP6_STRLEN - index, "/%i", 64);
  /* TUNDEV */
#ifdef __linux
  snprintf(tundev, UIP6_STRLEN, "TUNDEV=%s",ifr.ifr_name);
#else
  snprintf(tundev, UIP6_STRLEN, "TUNDEV=tap0");
#endif

  char *const env[] = {"PATH=/bin:/sbin", lan_ip, han_prefix, tundev, 0};
  execve_args[0] = (char*)linux_conf_tun_script;
  execve_args[1] = (init == 1? "up" : "down");
  execve_args[2] = 0;

  /* Resume the child process signal which could possibly be ignored when
   * calling Indicator blinking script */
  if (signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
    ERR_PRINTF("Failed to resume signals from tunnel script process. Call to signal(SIGCHLD, SIG_DFL) failed. errno=%d\n", errno);
  }

  pid = fork();

  /* Process spawn failed */
  if (pid == -1) {
    ERR_PRINTF("Cannot spawn process for tunnel script. Call to fork() failed. errno=%d\n", errno);
    ASSERT(FALSE);
    return;
  }
  /* Parent process - wait for subprocess to be ready by checking the status */
  else if (pid != 0) {
    while ((ret = waitpid(pid, &status, 0)) == -1) {
      if (errno != EINTR) {
        ERR_PRINTF("Failed to wait for tunnel script subprocess state change. errno=%d, strerror=%s\n", errno, strerror(errno));
        break;
      }
    }
    if ((ret != -1) &&
        (!WIFEXITED(status) || !WEXITSTATUS(status)) ) {
      /* Tunnel script failed. Retrieve the error message. */
      ERR_PRINTF("Unexpected tunnel script status=%d, if killed by signal: %d, if stopped by signal: %d\n", status, WIFSIGNALED(status), WIFSTOPPED(status));
    }
  }
  /* Child process */
  else {
    if (execve(linux_conf_tun_script, execve_args, env) == -1) {
      ERR_PRINTF("Error executing: %s. errno=%d, strerror=%s\n",linux_conf_tun_script, errno, strerror(errno));
      _Exit(EXIT_FAILURE);
    }
  }
}

/*---------------------------------------------------------------------------*/
void
tapdev_init(void)
{
  net_fd = open(DEVTAP, O_RDWR);
  if(net_fd == -1) {
    perror("tapdev: tapdev_init: open");
    return;
  }

#ifdef __linux
  {
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP|IFF_NO_PI;
    if (ioctl(net_fd, TUNSETIFF, (void *) &ifr) < 0) {
      perror("Unable to init tunnel interface\n");
      exit(1);
    }
    ioctl(net_fd, SIOCGIFHWADDR, &ifr);
  }
  /* Linux (ubuntu)
     snprintf(buf, sizeof(buf), "ip link set tap0 up");
     system(buf);
     PRINTF("%s\n", buf);
     snprintf(buf, sizeof(buf), "ip -6 address add fc00::231/7 dev tap0");
     system(buf);
     PRINTF("%s\n", buf);
     snprintf(buf, sizeof(buf), "ip -6 route add fc00::0/7 dev tap0");
     system(buf);
     PRINTF("%s\n", buf);
  */
  /* freebsd */

  printf("Lan device %s\n", ifr.ifr_name);
  printf("LAN HW addr %02X:%02X:%02X:%02X:%02X:%02X\n",
          (unsigned char)ifr.ifr_hwaddr.sa_data[0],
          (unsigned char)ifr.ifr_hwaddr.sa_data[1],
          (unsigned char)ifr.ifr_hwaddr.sa_data[2],
          (unsigned char)ifr.ifr_hwaddr.sa_data[3],
          (unsigned char)ifr.ifr_hwaddr.sa_data[4],
          (unsigned char)ifr.ifr_hwaddr.sa_data[5]);
#endif /* Linux */

  //system_hook(1);


  /*  */
  lasttime = 0;

  /*  gdk_input_add(fd, GDK_INPUT_READ,
      read_callback, NULL);*/

}
/*---------------------------------------------------------------------------*/


u8_t do_send(void)
{
  int ret;
  if(net_fd <= 0) {
    return 0;
  }


  //printf("tapdev_send: sending %d bytes\n", uip_len);
  //uip_dump_buf();
  /*  check_checksum(uip_buf, size);*/
#define DROP 0
  #if DROP
  if((rand() & 0x3F) == 0) {
    printf("Dropped an output packet!\n");
    uip_len = 0;
    return 0;
  }
#endif /* DROP */

  ret = write(net_fd, uip_buf, uip_len);
  if(ret == -1) {
    perror("tap_dev: tapdev_send: write");
    //exit(1);
  }
  uip_len = 0;

  return 0;
}

/*---------------------------------------------------------------------------*/
u8_t tapdev_send(uip_lladdr_t *lladdr)
{
  /*
   * If L3 dest is multicast, build L2 multicast address
   * as per RFC 2464 section 7
   * else fill with th eaddrsess in argument
   */
  if(lladdr == NULL) {
    /* the dest must be multicast */
    (&BUF->dest)->addr[0] = 0x33;
    (&BUF->dest)->addr[1] = 0x33;
    (&BUF->dest)->addr[2] = IPBUF->destipaddr.u8[12];
    (&BUF->dest)->addr[3] = IPBUF->destipaddr.u8[13];
    (&BUF->dest)->addr[4] = IPBUF->destipaddr.u8[14];
    (&BUF->dest)->addr[5] = IPBUF->destipaddr.u8[15];
  } else {
    memcpy(&BUF->dest, lladdr, UIP_LLADDR_LEN);
  }
  memcpy(&BUF->src, &uip_lladdr, UIP_LLADDR_LEN);
  BUF->type = UIP_HTONS(UIP_ETHTYPE_IPV6); //math tmp

  uip_len += sizeof(struct uip_eth_hdr);

  ipv46nat_interface_output();
  if(uip_len>0) {
    do_send();
  }

  return 1;
}

/*---------------------------------------------------------------------------*/
void
tapdev_do_send(void)
{
  do_send();
}
/*---------------------------------------------------------------------------*/
// math added function
void
tapdev_exit(void)
{
  close(net_fd);
  system_net_hook(0);
}
