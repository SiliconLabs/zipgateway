/* © 2020 Silicon Laboratories Inc. */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>

#include <lib/zgw_log.h>
#include "test_helpers.h"
#include "process.h"
#include "procinit.h"
#include "uip.h"

#include "Serialapi.h"
#include "zip_router_config.h" //for struct cfg
#include "tapdev6.h"
#include "contiki-net.h"
#include "ipv46_if_handler.h"
#include "uip-debug.h"

#include "mock_tapdev.h"

zgw_log_id_define(mockTap);
zgw_log_id_default_set(mockTap);

#define BUF ((struct uip_eth_hdr *)&uip_buf[0])
#define IPBUF ((struct uip_tcpip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP6_STRLEN 64

extern void set_landev_outputfunc(u8_t (* f)(uip_lladdr_t *a));

/** Callback to tell the test case when gateway is sending on LAN.
 *
 * Data is in uip_buf, uip_len.
 */
lan_send_notifier_t send_cb = NULL;

void mock_lan_set_tc_callback(lan_send_notifier_t f) {
   send_cb = f;
}

size_t lan_pkt_len = 0;

/* tapdev.c */
uint8_t do_send(void)
{
   zgw_log(zwlog_lvl_control, "Sending on LAN: %d bytes\n", uip_len);
/*   for (int ii = 0; ii < uip_len; ii++) {
      printf("0x%02x, ", uip_buf[ii]);
      }
   printf("\n"); */
   if (send_cb != NULL) {
      send_cb();
   }
   lan_pkt_len = uip_len;
   uip_len = 0;
   return 1;
}

/* tapdev6.c */
void system_net_hook(int init) {
   zgw_log(zwlog_lvl_control, "mock tapdev %s\n", (init == 1? "up" : "down"));
   return;
}

bool setup_landev_output(void) {
   /* Initalize LAN side connections in ZIP_Router.c. */
   set_landev_outputfunc(lan_out_mock);
   return true;
}

/* Mock replacement for landev_send/tapdev_send.
 * Needed in cases that include ZIP_Router.c. */
uint8_t lan_out_mock(uip_lladdr_t *lladdr){
  if(lladdr == NULL) {
      zgw_log(zwlog_lvl_control, "tapdev Send to broadcast addr\n");
    /* the dest must be multicast */
    (&BUF->dest)->addr[0] = 0x33;
    (&BUF->dest)->addr[1] = 0x33;
    (&BUF->dest)->addr[2] = IPBUF->destipaddr.u8[12];
    (&BUF->dest)->addr[3] = IPBUF->destipaddr.u8[13];
    (&BUF->dest)->addr[4] = IPBUF->destipaddr.u8[14];
    (&BUF->dest)->addr[5] = IPBUF->destipaddr.u8[15];
  } else {
      zgw_log(zwlog_lvl_control, "tapdev Send to addr 0x%x\n", lladdr->addr[0]);
    memcpy(&BUF->dest, lladdr, UIP_LLADDR_LEN);
  }
  memcpy(&BUF->src, &uip_lladdr, UIP_LLADDR_LEN);
  BUF->type = UIP_HTONS(UIP_ETHTYPE_IPV6); //math tmp
  uip_len += sizeof(struct uip_eth_hdr);

  /* Filter between LAN side and PAN side */
  ipv46nat_interface_output();

  if(uip_len>0) {
    do_send();
  }


   return 1;
}
