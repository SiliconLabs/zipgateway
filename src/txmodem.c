/* © 2018 Silicon Laboratories Inc.  */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <txmodem.h>
#include "port.h"
#include "ZIP_Router_logging.h"
#include "zgw_crc.h"

#define DEBUG 0
#ifndef DEBUG
#define printf(fmt, ...) (0)
#endif

typedef unsigned char uint8_t;
typedef unsigned short int uint16_t;
typedef unsigned char BYTE;
typedef unsigned short int WORD;
#define CRC_POLY 0x1021
#define CRC_INIT_VALUE 0x1D0F
#define X_MODEM_PAYLOAD_SIZE 128
#define X_MODEM_PKT_MAX_SIZE 133

#define  SOH  0x01 /*   Start of Header  */ 
#define  EOT  0x04 /*   End of Transmission */ 
#define  ACK  0x06 /*   Acknowledge  */ 
#define  NAK  0x15 /*   Not Acknowledge */ 
#define  CAN  0x18 /*   Cancel (Force receiver to start sending C's) */ 
#define  C    0x43 /*   ASCII "C" */ 

uint8_t buf[X_MODEM_PKT_MAX_SIZE] = {0};
uint8_t retry = 15;
/*FIXME: what if we never get ACK? */

int pktNumber = 0;
int didx = 0;
static uint8_t recv_byte()
{
    /* loop until there is something */
    uint8_t c;

    printf( "Enter C(67) ACK(1) NAK (2)\n");
    //fread(&c, sizeof(char), 1, stdin); 
    //sleep (1);
    c = SerialGetByte();
    printf("Received: %x %c\n", c, c);
    if (c == '1') {
        c = ACK;
    } else if (c == '2') {
        c = NAK;
    }
    return c;
}
 
int end;
int step;
static void send_data(const uint8_t *data, int len)
{
    int i = 0;
    uint16_t crc = 0;
    int chunk_size = (((didx + X_MODEM_PAYLOAD_SIZE) > len)? (len % X_MODEM_PAYLOAD_SIZE): X_MODEM_PAYLOAD_SIZE);
     

    printf("send_data %d %d %d\n", didx, len, pktNumber);
    printf("Sent %d bytes, remaining:%d bytes\n",didx, len-didx);
    if (!(didx % step)) {
        fprintf(stderr, ">");
    }
    if (buf[0] == EOT) {
        goto send_ETB_EOT;
    }

    buf[i++] = SOH;
    buf[i++] = pktNumber; 
    buf[i++] = ~pktNumber;    
    memcpy(&buf[i], &data[didx], chunk_size);
    crc = zgw_crc16(0, &buf[i], chunk_size);
    i += chunk_size;
    buf[i++] = (crc >> 8) & 0xff;
    buf[i] = (crc) & 0xff;

    if (i > X_MODEM_PKT_MAX_SIZE) {
        ERR_PRINTF("Sending more than X_MODEM_PKT_MAX_SIZE?\n");
    }
    printf( "sent offset :%d\n", didx);
    if ((didx + X_MODEM_PAYLOAD_SIZE) > len) {
        didx = len; /* last chunk */
    } else {
        didx += X_MODEM_PAYLOAD_SIZE;
    }
    printf( "new offset :%d\n", didx);

    pktNumber++;
send_ETB_EOT:
    //fwrite(buf, i+1, 1, stdout); 
    printf( "sending %d bytes:", i+1);
    printf( " 0x%02x 0x%02x 0x%02x\n", buf[0],  buf[1], buf[2]);
    SerialPutBuffer(buf, i+1);
    SerialFlush();
#if 0
    int k;
    char c;
    for ( k = 0; k < i+1; k++){
        SerialPutByte(buf[k]);
        SerialFlush();
    c = SerialGetByte();
    LOG_PRINTF("Received: %x %c\n", c, c);
    }
#endif
    //sleep(2);
}
static void wait_1sec()
{
  int t;

  /*Wait one 1 sec */
  t = clock_seconds() + 1;
  while (t >= clock_seconds())
  {
    __asm("nop");
  }
}

void print_prgrs_bar()
{
   fprintf(stderr,"|");
   int x;
   for(x = 0; x< end; x+=step)
   {
      fprintf(stderr," ");
   }
   fprintf(stderr,"|");
   fprintf(stderr,"\r");
   fprintf(stderr,"|");

}
uint8_t xmodem_tx(const char *filename)
{
    long numbytes = 0;
    long oldnumbytes = 0;
    FILE *fd;
    unsigned char *data;
    int i = 0;
    int len;

    fd = fopen(filename, "r");
    if (fd == NULL) {
        ERR_PRINTF("Can not open the firmware file: %s\n", filename);
        return 0;
    }

    fseek(fd, 0L, SEEK_END);
    numbytes = ftell(fd);
    fseek(fd, 0L, SEEK_SET);
    data = (unsigned char*)calloc(numbytes, sizeof(unsigned char));
    if(data == NULL) {
        ERR_PRINTF("Can not allocate memory to load the firmware file in memory\n");
        fclose(fd);
        return 0;
    }
    fread(data, sizeof(char), numbytes, fd);
    fclose(fd);

#if 0
    if (len != numbytes) {
        ERR_PRINTF("Length of actual file does not seem to be correct \n");
        free(data);
        return 0;
    }
#endif

   if (numbytes % 128) {
        oldnumbytes = numbytes;
        numbytes = numbytes + (128 - (numbytes %128));
        printf( "numbytes: %ld, oldnumbytes: %ld\n", numbytes, oldnumbytes);
        data = realloc(data, numbytes);
        memset(&data[oldnumbytes], 0, numbytes - oldnumbytes);
    }

   len = numbytes;
   end = numbytes  / 128;
   step = end / 50;

    while(retry) {
        uint8_t rb = recv_byte();
        switch (rb) {
            case C:
                printf("Received C\n");
                DBG_PRINTF("Sending Firmware file to Gecko module\n");
                print_prgrs_bar();

                if (pktNumber != 0) {
                    ERR_PRINTF("Received C in the middle of Transmission?\n");
                    free(data);
                    printf("\n");
                    pktNumber = 0;
                    didx = 0;
                    return 0;
                }
                pktNumber = 1;
                didx = 0;
                send_data(data, len);
                retry = 15;
                break;
            case ACK: /*send next pkt */
                if (didx== 0) { //That means we havent even started and we got ACK from XMODEM Receiver. Only C is expected here
                    ERR_PRINTF("Unexpected ACK\n");
                    free(data);
                    printf("\n");
                    pktNumber = 0;
                    didx = 0;
                    return 0;
                }

                if (buf[0] == EOT) {
                    buf[0] = 0;
                    free(data);
                    pktNumber = 0;
                    didx = 0;
                    printf("\n");
                    return 1;
                }

                if (didx == len) {
                   buf[0] = EOT;
                   printf( "sending EOT\n");
                } 
               
                //DBG_PRINTF("Received ACK\n");
                send_data(data, len);
                retry = 15;
                break;
                /* TODO: When we recieve NAK on gecko we have to start allover again? */
            case NAK: /* send same pkt */
                if (didx == 0) {//That means we havent even started and we got NAK from XMODEM Receiver. Only C is expected here
                    ERR_PRINTF("Unexpected NAK\n");
                    free(data);
                    printf("\n");
                    pktNumber = 0;
                    didx = 0;
                    return 0;
                }
                ERR_PRINTF("Received NAK");
                pktNumber--;
                retry--;
                didx -= ((didx == len)? (len % X_MODEM_PAYLOAD_SIZE): X_MODEM_PAYLOAD_SIZE);
                printf( "Resending offset %d\n", didx);
                send_data(data, len);
                break;
            case CAN:
                printf("\n");
                free(data);
                ERR_PRINTF("Received CAN");
                pktNumber = 0;
                didx = 0;
                return 0;
            default:
                retry--;
                fprintf(stderr, "unknown char received %x\n", rb); 
                break;
        }
    }
    free(data);
    pktNumber = 0;
    didx = 0;
    return 0;
}

#if 0
int main()
{
    uint8_t data[200];
    memset(data, 45, 200);
    xmodem_tx(data, 200);
}
#endif
