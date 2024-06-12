/* © 2014 Silicon Laboratories Inc.
 */

#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include<string.h>
#include<stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "zpg.h"
#include "linux_serial_interface.h"

#define error_message printf
int fd;
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/file.h>

// Given the path to a serial device, open the device and configure it.
// Return the file descriptor associated with the device.
static int OpenSerialPort(const char *bsdPath)
{
    int                         fileDescriptor = -1;
    struct termios      options;

    // Open the serial port read/write, with no controlling terminal, and don't wait for a connection.
    // The O_NONBLOCK flag also causes subsequent I/O on the device to be non-blocking.
    // See open(2) ("man 2 open") for details.

    fileDescriptor = open(bsdPath, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fileDescriptor == -1 || flock(fileDescriptor, LOCK_EX) == -1 )
    {
        printf("Error opening serial port %s - %s(%d).\n",
               bsdPath, strerror(errno), errno);
        goto error;
    }
    // Now that the device is open, clear the O_NONBLOCK flag so subsequent I/O will block.
    // See fcntl(2) ("man 2 fcntl") for details.
    if (fcntl(fileDescriptor, F_SETFL, 0) == -1)
    {
        printf("Error clearing O_NONBLOCK %s - %s(%d).\n",
            bsdPath, strerror(errno), errno);
        goto error;
    }

    if (ioctl(fileDescriptor, TIOCEXCL, (char *) 0) < 0) {
      printf("Error setting TIOCEXCL %s - %s(%d).\n",
          bsdPath, strerror(errno), errno);
      goto error;
    }
    memset(&options,0,sizeof(options));
    // The baud rate, word length, and handshake options can be set as follows:
    options.c_iflag=0;
    options.c_oflag=0;


    options.c_cflag=CS8|CREAD|CLOCAL|CSTOPB;           // 8n2, see termios.h for more information
    options.c_lflag=0;
    options.c_cc[VMIN]=1;
    options.c_cc[VTIME]=100;
    cfsetospeed(&options, B115200);            // Set 115200 baud
    cfsetispeed(&options, B115200);

    tcflush(fileDescriptor, TCIFLUSH);
    //cfmakeraw(&options);

    // Cause the new options to take effect immediately.
    if (tcsetattr(fileDescriptor, TCSANOW, &options) == -1)
    {
        printf("Error setting tty attributes %s - %s(%d).\n",
            bsdPath, strerror(errno), errno);
        goto error;
    }

      // Success
    return fileDescriptor;

    // Failure path
error:
    if (fileDescriptor != -1)
    {
        close(fileDescriptor);
    }

    return -1;
}


void
set_blocking(int fd, int should_block)
{
  struct termios tty;
  memset(&tty, 0, sizeof tty);
  if (tcgetattr(fd, &tty) != 0)
  {
    error_message("error %d from tggetattr", errno);
    return;
  }

  tty.c_cc[VMIN] = should_block ? 1 : 0;
  tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

  if (tcsetattr(fd, TCSANOW, &tty) != 0)
    error_message("error %d setting term attributes", errno);
}


int xfer(u8_t* buf,u8_t len,u8_t rlen) {
  u8_t null =0 ;
  u8_t r;
  int i;
  if(buf) {
    i=write(fd, buf, len);
    //tcdrain(fd);
    //printf("TX %2.2x %2.2x %2.2x %2.2x\n",buf[0],buf[1],buf[2],buf[3]);

    r=0;
    i=0;
    while((r < rlen) && (i < 3)) {
      r+= read(fd, &buf[r], rlen-r );
      i++;
    }

    if(rlen ==  r )  {
      //printf("RX %2.2x %2.2x %2.2x %2.2x\n",buf[0],buf[1],buf[2],buf[3]);
      return r;
    } else {
      printf("Read too short len = %i\n",r);
      return r;
    }
  } else {
    i=write(fd,&null,1);
    tcdrain(fd);
    return 1;
  }
}

void close_port(void) {
  flock(fd, LOCK_UN);
  close(fd);
}

int open_port(zw_pgmr_t* p, const char* dev_string) {
  struct stat s;

  if(stat(dev_string,&s) == -1) {
    return 0;
  }

  if(!S_ISCHR(s.st_mode)) {
    return 0;
  }

  fd = OpenSerialPort(dev_string);
  set_blocking(fd, 0); // set no blocking

  p->xfer = xfer;
  p->close = close_port;
  p->open = open_port;
  p->usb = 0;
  return 1;
}


const struct zpg_interface linux_serial_interface  = {
    "Linux Serial programming interface\n",
    open_port,
    "/dev/ttyXXX"
};


#if 0
void hexdump(u8_t *buf, int len) {
  int i=0;

  for(i=0; i < len; i++) {
    if((i & 0xF) == 0x0) printf("\n %4.4x: ",i);
    printf("%2.2x",buf[i]);
  }
  printf("\n");
}


void sram_read_write_test(zw_pgmr_t* p) {
  int i=0;
  unsigned char buf1[2048];	
  unsigned char buf2[2048];	
  
  for(i=0; i < sizeof(buf1); i++) {
    buf1[i] = i & 0xFF;
  }
  bulk_write_SRAM(p,0, sizeof(buf1), buf1 );
  printf("Buffer written\n");
  bulk_read_SRAM(p,0, sizeof(buf1), buf2 );
  printf("Buffer read\n");
  if(memcmp(buf1,buf2,sizeof(buf1))==0 ) {
    printf("SRAM test passed\n");
  } else {
    printf("SRAM test FAILED\n");
    hexdump(buf2,sizeof(buf2));
  }
}
#endif

#if 0
void flash_read_write_test(zw_pgmr_t* p) {
  int i=0;
  unsigned char buf1[2048];
  unsigned char buf2[2048];

  for(i=0; i < sizeof(buf1); i++) {
    buf1[i] = (i & 0xFF);
  }
  erase_sector(p,0);

  program_flash_code_space(p,0,buf1);

  read_flash_sector(p,0,buf2);
  printf("Buffer read\n");
  if(memcmp(buf1,buf2,sizeof(buf1))==0 ) {
    printf("FLASH test passed\n");
  } else {
    printf("FLASH test FAILED\n");
    hexdump(buf2,sizeof(buf2));
  }
}
#endif

#if 0
int
main(int argc, char** argv)
{
  int i;
  u8_t flash[2048*64];
  FILE* f;

  char *portname = argv[1];
  char *filename = argv[2];

  memset(flash,0xFF,sizeof(flash));



  zw_pgmr_t pgmr = {xfer,printf,printf};
  /**
   * Enable serial programming after reset has been low t_PE,
   */
  if(enable_interface(&pgmr)) {
    printf("Programming interface enabled\n");
  }



  programmer_init(&pgmr);

  /*
  for(i=0; i < 64; i++) {
    printf("Read sector %i\r",i);
    read_flash_sector(&pgmr,i,&flash[i*2048]);
  }
  printf("\n");
  f = fopen(filename,"wb");
  fwrite(flash,sizeof(flash),1,f);
  fclose(f);*/

  f = fopen(filename,"r");
  fread(flash,sizeof(flash),1,f);
  fclose(f);

  programmer_program_chip(&pgmr,flash,sizeof(flash));


 // sram_read_write_test(&pgmr);
  //flash_read_write_test(&pgmr);;
  //read_flash_sector(&pgmr,0,sector);
  //hexdump(sector,sizeof(sector));

}
#endif
