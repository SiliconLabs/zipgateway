/* © 2019 Silicon Laboratories Inc. */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <clock.h>
#include <hex_to_bin.h>
#include "zpg.h"
#include "linux_serial_interface.h"
#include "linux_usb_interface.h"
#include "Serialapi.h"
#include "conhandle.h"
#include "ZW_SerialAPI.h"
#include "port.h"
#include "txmodem.h"
#include "nvm_tools.h"
#include "sdk_versioning.h"
#include "zw_programmer.h"

typedef struct SerialAPICapbilities {
  uint8_t version;
  uint8_t unused_length;
  uint8_t unused_chip_capabilities;
  uint8_t nodelist[29];
  uint8_t chip_type;
  uint8_t chip_version;
  uint8_t chip_library;
} SerialAPICapabilities_t;


const struct SerialAPI_Callbacks serial_api_callbacks = {0, 0, 0, 0, 0, 0, 0, 0, 0};


// Displays an help message when the programmer was called with wrong/no arguments
void print_usage(const char* path_name)
{
  const char *basename = strrchr(path_name, '/');
  basename = (basename) ? (basename + 1) : path_name;

  printf(
  "Usage: %s -s serial_device [ -r nvm.bin | -w nvm.bin | -p firmware_image_file [-a] | -t ]\n\n"
  "\t -s specifies the path of for the Serial device / Z-Wave module.\n"
  "\t -r read the nvm contents from the serial_device into the specified filename in binary format.\n"
  "\t -w write the nvm contents to the serial_device from the specified filename in binary format.\n"
  "\t -p program a new firmware onto the serial_device from the specified filename.\n"
  "\t    NB: for 500-series chips use a .hex file. For 700-series chips use a .gbl file.\n"
  "\t -a use if the chip is already in Auto Programming Mode (enforced by holding down the reset button).\n"
  "\t    NB: This option is valid only for 500-series chips.\n"
  "\t -t test mode. Will connect to serial device and show info about the Z-Wave module.\n\n"
  ,basename);
}

/* Parse the arguments */
bool parse_main_args(int argc, char **argv, e_zw_programmer_modes_t* mode, char** serial_device, char** filename){
  int opt;
  *mode = ZW_PROGRAMMER_MODE_UNDEFINED;
  *serial_device = NULL;
  *filename = NULL;
  bool apm_flag = false;
  while ((opt = getopt(argc, argv, "s:r:w:p:ta")) != -1)
  {
    switch (opt) {
      case 's':
        *serial_device = optarg;
        break;
      case 'r':
        *mode = ZW_PROGRAMMER_MODE_NVM_READ;
        *filename = optarg;
        break;
      case 'w':
        *mode = ZW_PROGRAMMER_MODE_NVM_WRITE;
        *filename = optarg;
        break;
      case 'p':
        *mode = ZW_PROGRAMMER_MODE_FW_UPDATE;
        *filename = optarg;
        break;
      case 'a':
        apm_flag = true;
        break;
      case 't':
        *mode = ZW_PROGRAMMER_MODE_TEST;
        break;
      default: /* '?' */
        return false;
    }

  }

  if (apm_flag)
  {
    if (*mode == ZW_PROGRAMMER_MODE_FW_UPDATE)
    {
      *mode = ZW_PROGRAMMER_MODE_FW_UPDATE_ALREADY_IN_APM;
    }
    else
    {
      /* The -a flag is only valid if -p is also specified */
      return false;
    }
  }

  if( (*mode == ZW_PROGRAMMER_MODE_UNDEFINED) || (*serial_device == NULL) || ((*mode != ZW_PROGRAMMER_MODE_TEST) && (*filename == NULL))) {
    return false;
  } else {
    printf("Using serial device %s\n",*serial_device);
    return true;
  }
}

static void wait_seconds(int waiting_time)
{
  clock_time_t t;

  /* Wait waiting_time seconds */
  t = clock_time() + waiting_time*1000;
  while (t >= clock_time())
  {
    __asm("nop");
  }
}

int ZWGeckoFirmwareUpdate(const char *filename)
{
   unsigned char c = 0;

   ZW_AutoProgrammingEnable();
   SerialFlush();
   printf("Auto Programming enabled\n");
   wait_seconds(1);

   /* Wait for following msg from Bootloader */
   /*Gecko Bootloader v1.5.1
    * 1. upload gbl
    * 2. run
    * 3. ebl info
    * BL >
    */
   while ( c != '>')
   {
     c = SerialGetByte();
     printf("%c", c);
   }

   printf("1\n");
   SerialPutByte('1');
   SerialFlush();

   c = 0;
   while ( c != 'd') { // wait for "begin upload"
     c = SerialGetByte();
     printf("%c", c);
   }

   if (!xmodem_tx(filename)) {
     printf("Error in transmission\n");
     return 0;
   } else {
     printf("Success in transmission\n");
     c = 0;
     while ( c != '>') {
       c = SerialGetByte();
       //printf("%c", c);
     }

     printf("Running the uploaded gbl on 700/800-series chip\n");
     SerialPutByte('2');
     SerialFlush();
     return 1;
   }
}

int ZWFirmwareUpdate(const char* filename, const char* serial_device, bool send_apm_enable)
{
  zw_pgmr_t pgmr;
  int i;
  u8_t flash[64 * 0x800];
  int retry = 5;
  FILE *fd;
  long numbytes = 0;
  bool usb_programming = false;
  unsigned char flashContent[0x400 * 200];

  fd = fopen(filename, "r");
  if (fd == NULL)
  {
      printf("Cannot open firmware file \"%s\": %s.\n", filename, strerror(errno));
      return 0;
  }

  numbytes = convert_hex_to_bin(fd, flashContent, sizeof(flashContent));
  if (numbytes == 0)
  {
      printf("Error in converting the hex file to binary file: %s\n", filename);
      return 0;
  }
  fclose(fd);


  /*Send the autoprogramming string*/
  if (send_apm_enable)
  {
    ZW_AutoProgrammingEnable();
    wait_seconds(1);
    SerialClose();
    /* Wait for the USB device to reappear after the
      power cycling of the Z-Wave chip */
    wait_seconds(3);
  }


  pgmr.con = printf;
  pgmr.err = printf;
try_again:
  /* First try USB then Serial */
  if (linux_usb_interface.parse_dev_string(&pgmr, "USB"))
  {
    usb_programming = true;
  }
  else if (linux_serial_interface.parse_dev_string(&pgmr, serial_device))
  {
    usb_programming = false;
    /*...*/
  }
  else
  {
    if (retry) {
        wait_seconds(1);
        printf("Retrying in 1 sec...\n");
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
  memcpy(flash,flashContent, sizeof(flash));


  if (zpg_program_chip(&pgmr, flash, sizeof(flash)) == 0)
  {
    pgmr.close();
    wait_seconds(1);

    if(usb_programming) {
      if( linux_usb_detect_by_id(0x0658,0x200) ) {
        printf("Device found!\n");
        return 1;
      } else {
        wait_seconds(1);
        goto try_again;
      }    
    }
    return 1;
  }

error:
  printf("ERROR: Firmware programming failed.\n");
  pgmr.close();

  return 0;
}

static bool connect_serial_api(const char* serial_device , SerialAPICapabilities_t* cap) {
  /* Establish a connection to the serial port */
  bool serial_init_status = SerialAPI_Init(serial_device, &serial_api_callbacks);
  if (false == serial_init_status)
  {
    printf("Serial Init failed\n");
    return EXIT_FAILURE; //failure
  }
  printf("Connected to Serial device: OK\n");

  // Get the Serial API to initialize.
  SerialAPI_GetInitData(
    &(cap->version),
    &(cap->unused_chip_capabilities),
    &(cap->unused_length),
    cap->nodelist,
    &(cap->chip_type),
    &(cap->chip_version)
  );
  printf("Serial version: %d, Chip type: %d, Chip version: %d, ",cap->version, cap->chip_type,cap->chip_version);

  // Find out the library running on the chip.
  BYTE zw_protocol_version[14] = {};
  PROTOCOL_VERSION zw_protocol_version2 = {0};
  cap->chip_library = ZW_Version(zw_protocol_version);
  ZW_GetProtocolVersion(&zw_protocol_version2);

  printf("SDK: %d.%02d.%02d, SDK Build no: %d",
      zw_protocol_version2.protocolVersionMajor,
      zw_protocol_version2.protocolVersionMinor,
      zw_protocol_version2.protocolVersionRevision,
      zw_protocol_version2.zaf_build_no);
  printf(" SDK git hash: ");
  int i;
  for (i = 0; i < 16; i++) {
    printf("%x", zw_protocol_version2.git_hash_id[i]);
  }
  printf("\n");

  const char *nvm_id  = GenerateNvmIdFromSdkVersion(
              zw_protocol_version2.protocolVersionMajor,
              zw_protocol_version2.protocolVersionMinor,
              zw_protocol_version2.protocolVersionRevision,
              cap->chip_library,
              cap->chip_type);

  printf("Chip library: %d, ZW protocol: %s, NVM: %s\n",
        cap->chip_library,
        zw_protocol_version + 7, // 7: Skip the "Z-Wave " prefix
        nvm_id);

  printf("nvm_id: %s\n", nvm_id);
  return true; // this return value is used as True or False in zw_programmer
}

static bool execute_programming_operation(const char* filename, const char* serial_device, e_zw_programmer_modes_t mode)
{
  SerialAPICapabilities_t my_SerialAPI = {};
  int rc = 0;

  /* We need to first initialize the serial API - UNLESS the user has specified that
   * the device is already in Automatic Programming Mode (enforced by holding down
   * the reset button).
   * By switching the device into APM by pressing the reset button, it is possible
   * to program a chip that does not have a serial API.
   */
  if (mode != ZW_PROGRAMMER_MODE_FW_UPDATE_ALREADY_IN_APM) {
    if (!connect_serial_api(serial_device, &my_SerialAPI)) {
      return 0;
    }
  }

  if((mode == ZW_PROGRAMMER_MODE_NVM_READ) || (mode == ZW_PROGRAMMER_MODE_NVM_WRITE))
  {
    if (!SerialAPI_SupportsCommand_func(FUNC_ID_NVM_BACKUP_RESTORE))
    {
      printf("Z-Wave module does not support read/write of NVM\n");
      SerialClose();
      return false;
    }
  }

  switch(mode)
  {
    case ZW_PROGRAMMER_MODE_NVM_READ:
      // Read the NVM content and export to file
      rc = ZW_NVM_Backup(filename,my_SerialAPI.chip_type);
      break;

    case ZW_PROGRAMMER_MODE_NVM_WRITE:
      // Restore the file content onto the NVM :
      return ZW_NVM_Restore(filename,my_SerialAPI.chip_type);
      break;

    case ZW_PROGRAMMER_MODE_FW_UPDATE:
      // Start firmware programming
      if (ZW_GECKO_CHIP_TYPE(my_SerialAPI.chip_type))
      {
        // 700 series programming
        rc = ZWGeckoFirmwareUpdate(filename);
      }
      else if (ZW_CHIP_TYPE == my_SerialAPI.chip_type)
      {
        // 500 series programming (with switch to Auto Programming Mode)
        rc = ZWFirmwareUpdate(filename, serial_device, true);
      }
      break;

    case ZW_PROGRAMMER_MODE_FW_UPDATE_ALREADY_IN_APM:
      // 500 series programming (without switch to Auto Programming Mode)
      rc = ZWFirmwareUpdate(filename, serial_device, false);
      break;

    case ZW_PROGRAMMER_MODE_TEST:
      // Do nothing. Info already printed above.
      rc = 1;
      break;

    case ZW_PROGRAMMER_MODE_UNDEFINED:
    default:
      rc = 0;
      break;
  }

  printf("Closing Serial connection\n");
  SerialClose();

  return (rc != 0);
}

int main(int argc, char **argv)
{
  // Program variables
  bool rc = false;
  char* serial_device = NULL;
  char* filename = NULL;
  e_zw_programmer_modes_t my_mode = ZW_PROGRAMMER_MODE_UNDEFINED;

  // Parse the arguments.
  if (!parse_main_args(argc, argv, &my_mode, &serial_device, &filename))
  {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  rc = execute_programming_operation(filename, serial_device, my_mode);

  return (rc) ? EXIT_SUCCESS : EXIT_FAILURE;
}
