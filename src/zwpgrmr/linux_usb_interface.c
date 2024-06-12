/* © 2014 Silicon Laboratories Inc.
 */



/*
 * linux_serial.c
 *
 *  Created on: Nov 28, 2010
 *      Author: esban
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
//#include <errno.h>
#include <sys/time.h>

#include <ctype.h>

#include <libusb.h>

#include "zpg.h"
#include "linux_usb_interface.h"
//#include <ZIP_Router.h>

// The Z-Wave 500-series chip uses a max packet size of 32 bytes
#define USB_MAX_PACKET_SIZE 32

libusb_device_handle *dev_handle; //a device handle
libusb_context *ctx = 0; //a libusb session

static unsigned char rx_buf[USB_MAX_PACKET_SIZE];

#define DUMP 0 /* Dump APM communications to debug log if enabled  */
#if DUMP
static FILE *fp;


void hexdump(u8_t *buf, int len) {
  int i=0;
  int j=0;
  for(i=0; i < len; i++) {
    for(j=0; (j < 0xF) && (i+j < len); j++) {
      fprintf(fp, "%2.2x",buf[i+j]);
    }
    i+=j;
  }
  fprintf(fp,"\n");
}
#endif

static int xfer(u8_t* buf,u8_t len,u8_t rlen) {
  u8_t dummy[4] ;
  int actual=0;
  int i;
  int r;
  int m;
  if(buf) {

    for(i=0; i < len ; ) {
#if DUMP
    fprintf(fp, "%lu TX:", clock_time());
    hexdump(&buf[i],len);
#endif
      m = len -i;
/*      if(m > 12) {
        m = 12; //Set a 12 byte limit
      }*/

      r = libusb_bulk_transfer(dev_handle, (2 | LIBUSB_ENDPOINT_OUT), &buf[i], m, &actual, 200);
      i +=actual;
      if(r) {
        perror("out error");
        return 0;
      }
    }
    memset(buf,0,rlen);
    memset(rx_buf, 0xaa, sizeof rx_buf);
    actual = 0;
    for(i=0; i < rlen ; ) {
      r = libusb_bulk_transfer(dev_handle, (2 | LIBUSB_ENDPOINT_IN), rx_buf, USB_MAX_PACKET_SIZE, &actual, 2000);
      if(actual == 0) {
        if(r) {
          printf("libusb error %i, name: %s\n", r, libusb_error_name(r));
        }
        break;
      }
#if DUMP
      fprintf(fp, "%lu RX:", clock_time());
      hexdump(&rx_buf[i],actual);
#endif
      if (actual < 0) {
        /* This probably cannot happen, but better safe than sorry */
        printf("actual usb read was negative - skipping!\n");
        continue;
      }
      /* Make sure we don't overflow buf by writing more than rlen*/
      if (actual > rlen - i) {
        printf("Warning: USB read overflow. Discarding %i bytes", actual - (rlen - i));
        actual = rlen - i;
      }
      memcpy(&buf[i], rx_buf, actual);
      i +=actual;
      if(r) {
        printf("libusb error %i, name: %s\n", r, libusb_error_name(r));
#if DUMP
        fprintf(fp, "libusb error %i, name: %s\n", r, libusb_error_name(r));
#endif
        //perror("in error:);
        printf("got %i bytes expcted %i\n",i,rlen);
        return i;
      }
    } /* for (... ) */
    return i;
  } else {
    printf("sync\n");
    dummy[0]=0;
    r = libusb_bulk_transfer(dev_handle, (2 | LIBUSB_ENDPOINT_OUT), dummy, 1, &actual, 0);
    /*r = libusb_bulk_transfer(dev_handle, (2 | LIBUSB_ENDPOINT_IN), dummy, 4, &actual, 2);*/
    return 1;
  }
}

static  void close_port() {

  int r;
  r = libusb_release_interface(dev_handle, 0); //release the claimed interface
  if (r != 0)
  {
    printf( "Cannot Release Interface\n");
  }
  else
  {
    printf( "Released Interface\n");
  }

  libusb_close(dev_handle); //close the device we opened
  libusb_exit(ctx); //needs to be called to end the
#if DUMP
  fclose(fp);
#endif

  sleep(1);
}

int linux_usb_detect_by_id(int idVendor,int idProduct) {
  libusb_init(&ctx); //initialize the library for the session we just declared

  libusb_device **list = NULL;
  int rc = 0;
  ssize_t count = 0;
  size_t idx;
  count = libusb_get_device_list(ctx, &list);
  for (idx = 0; idx < count; ++idx) {
      libusb_device *device = list[idx];
      struct libusb_device_descriptor desc;
      rc = libusb_get_device_descriptor(device, &desc);    
//      printf("Vendor:Device = %04x:%04x\n", desc.idVendor, desc.idProduct);
      if((idVendor == desc.idVendor) && (idProduct == desc.idProduct) ) break;
  }

  libusb_free_device_list(list, count);
  libusb_exit(ctx);
  return (idx != count);
}

static int open_port(zw_pgmr_t* p, const char* port) {
  struct libusb_device_descriptor desc;
  libusb_device* dev;
  int r;

  if(strncmp("USB",port,3) !=0) {
    return 0;
  }


#if DUMP
  fp = fopen("./prog.log", "a");
  if (!fp)
    ERR_PRINTF("Error opening APM debug log file\n");
#endif
  r = libusb_init(&ctx); //initialize the library for the session we just declared
  if (r < 0)
  {
    printf( "Init Error %i\n" ,r); //there was an error
    return 0;
  }
  //libusb_set_debug(ctx, 3); //set verbosity level to 3, as suggested in the documentation

  dev_handle = libusb_open_device_with_vid_pid(ctx, 0x0658, 0x0280); //these are vendorID and productID I found for my usb device
  if (dev_handle == 0) {
    printf( "Cannot open device\n");
    return 0;
  }

  if (libusb_kernel_driver_active(dev_handle, 0) == 1)
  { //find out if kernel driver is attached
    printf( "Kernel Driver Active\n");
    if (libusb_detach_kernel_driver(dev_handle, 0) == 0) //detach it
    {
      printf( "Kernel Driver Detached!\n");
    }
  }
  r = libusb_claim_interface(dev_handle, 0); //claim interface 0 (the first) of device (mine had jsut 1)
  if (r < 0)
  {
    printf( "Cannot Claim Interface\n");
    return 0;
  }

  dev = libusb_get_device(dev_handle);
  libusb_get_device_descriptor(dev,&desc);
  p->xfer = xfer;
  p->close = close_port;
  p->open = open_port;

  /*Check if this is the native programmer or the usb_loader workaround */
  p->usb = desc.bcdDevice == 0 ? 1 : 2;

  printf("Using usb serial driver with %s interface(%x)....\n",desc.bcdDevice == 0 ? "native" : "usb_loader",desc.bcdDevice);

  return 1;
}


const struct zpg_interface linux_usb_interface  = {
    "Linux USB programming interface\n",
    open_port,
    "USB"
};
