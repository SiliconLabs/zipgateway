/* © 2014 Silicon Laboratories Inc.
 */

/**
 * Update Z-Wave module firmware, with the image stored in eeprom
 */
#include "ZIP_Router.h"
#include "dev/eeprom.h"
#include <zpg.h>
#include <linux_serial_interface.h>
#include <linux_usb_interface.h>
#include "Serialapi.h"
#include <stdlib.h>
#include "ZIP_Router_logging.h"
#include "CC_FirmwareUpdate.h"
#include "port.h"
#include "txmodem.h"
#include "serial_api_process.h"

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

int ZWFirmwareUpdate(unsigned char isAPM, char *fw_filename, int size)
{
  zw_pgmr_t pgmr;
  int i;
  u8_t flash[64 * 0x800];
  
  int retry = 5;

  /*Send the autoprogramming string*/
  if (isAPM)
  {
    ZW_AutoProgrammingEnable();
    wait_1sec();
  }

  /* Shutdown the serial API process */
  SerialAPI_Destroy();

  /* Wait for the USB device to reappear after the
     power cycling of the Z-Wave chip */
  wait_1sec();
  wait_1sec();
  wait_1sec();

  pgmr.con = printf;
  pgmr.err = printf;
try_again:
  /* First try USB then Serial */
  if (linux_usb_interface.parse_dev_string(&pgmr, "USB"))
  {
    /*...*/
  }
  else if (linux_serial_interface.parse_dev_string(&pgmr, cfg.serial_port))
  {
    /*...*/
  }
  else
  {
    if (retry) {
        wait_1sec();
        WRN_PRINTF("Retrying in 1 sec...\n");
        retry--;
        goto try_again;
    }

    goto error;
  }

  if (zpg_init(&pgmr) < 0)
  {
    goto error;
  };

  memset(flash, 0xff, sizeof(flash));
  FILE *fw = fopen(fw_filename, "r");
  fseek(fw, ZW_FW_HEADER_LEN, SEEK_SET);
  /* FW file is smaller than expected */
  if (size < (sizeof(flash) + ZW_FW_HEADER_LEN))
  {
    goto error;
  }
  fread(flash, sizeof(u8_t), sizeof(flash), fw);
  fclose(fw);

  if (zpg_program_chip(&pgmr, flash, sizeof(flash)) == 0)
  {
    pgmr.close();
    wait_1sec();
    /* Re-start the serial API  */
    serial_process_serial_init(cfg.serial_port);
    return TRUE;
  }

error:
  ERR_PRINTF("Programming failed!!!!");
  pgmr.close();
  /* Re-start the serial API  */
  serial_process_serial_init(cfg.serial_port);

  return FALSE;
}

int ZWGeckoFirmwareUpdate(struct image_descriptor * fw_desc, struct chip_descriptor *chip_desc,
                          char *filename, size_t len)
{
   unsigned char c = 0;
   DBG_PRINTF("TODO: Update gecko from file %s.\n", filename);
   ZW_AutoProgrammingEnable();

   SerialFlush();
   DBG_PRINTF("Auto Programming Enabled\n");
   wait_1sec();

   /* Wait for following msg from Bootloader */
   /*Gecko Bootloader v1.5.1
    * 1. upload gbl
    * 2. run
    * 3. ebl info
    * BL >
    */
   while ( c != '>') {
     c = SerialGetByte();
     //printf("%c", c);
   }

   //DBG_PRINTF("1\n");
   SerialPutByte('1');
   SerialFlush();

   c = 0;
   while ( c != 'd') { // wait for "begin upload"
     c = SerialGetByte();
     //printf("%c", c);
   }

   if (!xmodem_tx(filename)) {
     ERR_PRINTF("Error in transmission\n");
     return FALSE;
   } else {
     LOG_PRINTF("Success in transmission\n");
     c = 0;
     while ( c != '>') {
       c = SerialGetByte();
       //printf("%c", c);
     }
 
     DBG_PRINTF("Run the firmware on Gecko\n");
     SerialPutByte('2');
     SerialFlush();

    SerialAPI_Destroy();
    serial_process_serial_init(cfg.serial_port);
   
   
   return TRUE;
   }
}
