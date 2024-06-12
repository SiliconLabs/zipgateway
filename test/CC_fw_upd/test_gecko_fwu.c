/* © 2018 Silicon Laboratories Inc.
 */

/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/
#include "test_helpers.h"
#include "test_CC_helpers.h"
#include "test_gw_helpers.h"
#include "zip_router_config.h"
#include <lib/zgw_log.h>
#include "ZW_classcmd.h"
#include <ZW_classcmd_ex.h>
#include <CC_FirmwareUpdate.h>
#include <stdlib.h>
#include "Serialapi.h"

#define ZW_GECKO_CHIP_TYPE_MOCK 7
/* Mock the chip descriptor to say we are using a Gecko chip */
struct chip_descriptor chip_desc = {ZW_GECKO_CHIP_TYPE_MOCK, 7};

zgw_log_id_define(cc_fwu_test);

zgw_log_id_declare(cc_fwu_test);
zgw_log_id_default_set(cc_fwu_test);

/* stubs*/
// gateway stubs
uint8_t nodeOfIP(const uip_ip6addr_t* ip) {
   return 0;
}

// protocol stubs
/*===============================   ZW_Version   ============================
**    Copy Z-Wave basis version from code memory (Flash) to data memory (SRAM)
**    destintaion should be in XDATA
** extern void         RET  Nothing
** ZW_Version(
** XBYTE *buffer);      IN Destination pointer in RAM
**--------------------------------------------------------------------------*/
#define ZW_VERSION(buf) ZW_Version(buf)
uint8_t ZW_Version(uint8_t *dst) {
   *dst = 8;
   return 0;
}

void ZW_SoftReset(void) {
  return;
}

// contiki stubs
u16_t uip_htons(u16_t val)
{
  return UIP_HTONS(val);
}

u32_t uip_htonl(u32_t val)
{
  return UIP_HTONL(val);
}

/* Contiki crc 16 */
u16_t chksum(u16_t sum, const u8_t *data, u16_t len)
{
  u16_t t;
  const u8_t *dataptr;
  const u8_t *last_byte;

  dataptr = data;
  last_byte = data + len - 1;

  while(dataptr < last_byte) {   /* At least two more bytes */
    t = (dataptr[0] << 8) + dataptr[1];
    sum += t;
    if(sum < t) {
      sum++;      /* carry */
    }
    dataptr += 2;
  }

  if(dataptr == last_byte) {
    t = (dataptr[0] << 8) + 0;
    sum += t;
    if(sum < t) {
      sum++;      /* carry */
    }
  }

  /* Return sum in host byte order. */
  return sum;
}

void ctimer_set(struct ctimer *c, unsigned long t, // clock_time_t t,
                void (*f)(void *), void *ptr) {
   return;
}

void ctimer_stop(struct ctimer *c) {
   return;
}

/* mock for contiki stuff - definitely to be moved to contiki helper. */

/* #define PROCESS_STATE_NONE        0 */
/* #define PROCESS_STATE_RUNNING     1 */
/* #define PROCESS_STATE_CALLED      2 */
struct process zip_process = {NULL, "zip_process", NULL, 42, 43, 0};
struct process serial_api_process = {NULL, "serial_api_process", NULL, 62, 63, 0};

void process_start(struct process *p, const char *arg)
{
   return;
}

int process_post(struct process *p, process_event_t ev, process_data_t data)
{
   return 0;
}

void process_exit(struct process *p) {
   return;
}

// ZW_udp_server.c stubs
int zwave_connection_compare(zwave_connection_t* a, zwave_connection_t* b) {
   return 1;
}

// ZW_tcp_client.c stubs
#define ZIPR_NOT_READY 0x0
#define HANDSHAKE_DONE 0x1
#define ZIPR_READY 0x2
uint8_t gisZIPRReady = ZIPR_NOT_READY;


//Serialapi.c stubs
uint32_t SerialAPI_nvm_open() {
   return 0;
}

uint8_t SerialAPI_nvm_close() {
   return 0;
}

uint8_t SerialAPI_nvm_restore(uint16_t offset, uint8_t *buf, uint8_t length, uint8_t *length_written) {
   return 0;
}

uint8_t SerialAPI_nvm_backup(uint16_t offset, uint8_t *buf, uint8_t length, uint8_t *length_read) {
   return 0;
}

/*
  * \ingroup ZWCMD
  * Read number of bytes from the EEPROM to a RAM buffer.
  * \param[in] offset   Application area offset
  * \param[in] buffer   Buffer pointer
  * \param[in] length   Number of bytes to read
  */
uint8_t MemoryGetBuffer(WORD offset, BYTE *buf, BYTE length) {
   // TODO
   return 1;
}

BYTE MemoryPutBuffer(WORD offset, BYTE *buf, WORD length, void ( *func ) (void) ) {
   return 0;
}

/**
 * Create a new timer.
 *
 * @param[in] func         Callback to be called when timer expires.
 * @param[in] timerTicks   Timer interval in units of 10ms
 * @param[in] repeats      Number of times the timer fires. 0xFF makes the timer fire forever
 * \retval a handle to the timer just created.
 */
BYTE ZW_LTimerStart(void (*func)(),unsigned long timerTicks, BYTE repeats) {
   return 1;
}

/**
 * Cancel the timer
 * @param[in] handle Handle of timer to cancel
 */
BYTE ZW_LTimerCancel(BYTE handle) {
   return 1;
}

uint8_t SerialAPI_SupportsCommand_func(uint8_t cmd)
{
   return 1;
}

void ZW_WatchDogEnable() {
   return; //SendFrame(FUNC_ID_ZW_WATCHDOG_ENABLE,0, 0);
}

BYTE ZW_SetRFReceiveMode( BYTE mode )
{
   return 1;
}

void Get_SerialAPI_AppVersion(uint8_t *major, uint8_t *minor)
{
   *major = 8;
   *minor = 0;
   return;
}

/*
 TcpTunnel_ReStart restarts the tcp client and ntp processes.
 */
void TcpTunnel_ReStart(void)
{
   return;
}

void SerialAPI_GetChipTypeAndVersion(uint8_t *type, uint8_t *version)
{
   *type = ZW_GECKO_CHIP_TYPE_MOCK;
   *version = 0;
}

struct router_config cfg;

void cfg_init() {
   cfg.serial_port = "dummy";
   cfg.manufacturer_id = 0;
   cfg.product_type = 1;
   cfg.product_id = 1;
   cfg.hardware_version = 1;
   cfg.ca_cert = "/usr/local/etc/Portal.ca_x509.pem";
   cfg.cert = "/usr/local/etc/ZIPR.x509_1024.pem";
   cfg.priv_key = "/usr/local/etc/ZIPR.key_1024.pem";
   //   cfg. = ;
}

// utls stubs
uint16_t zgw_crc16(uint16_t crc16, uint8_t *data, unsigned long data_len) {
   return 0;
}

// MD5 stubs
typedef struct MD5state_st {
    /* MD5_LONG A, B, C, D; */
    /* MD5_LONG Nl, Nh; */
    /* MD5_LONG data[MD5_LBLOCK]; */
    unsigned int num;
} MD5_CTX;

void MD5_Init(MD5_CTX *context) {
   return;
}
int MD5_Update(MD5_CTX *c, const void *data, size_t len) {
   return 1;
}

int MD5_Final(unsigned char *md, MD5_CTX *c) {
   return 1;
}

/**
 * prototypes for local functions in the tested file.
 */
command_handler_codes_t Fwupdate_Md_CommandHandler(zwave_connection_t *c,
                                                   uint8_t* pData,
                                                   uint16_t bDatalen);
void Fwupdate_MD_init();


/** Expected output from ZWFirmwareUpdate/ZWGeckoFirmwareUpdate. */
int exp_ZWFwU_out = TRUE;
int ZWFirmwareUpdate(unsigned char isAPM, char *fw_filename, int size)
{
   return exp_ZWFwU_out;
}
int ZWGeckoFirmwareUpdate(struct image_descriptor * fw_desc, struct chip_descriptor *chip_desc,
                          char *filename, size_t len) {
   return exp_ZWFwU_out;
}


/* Test helpers */
void send_fw_upd_md_req_get() {
   command_handler_codes_t res = 0;
   uip_ipaddr_t dummy_addr =  {0,0,0,0,  0,0,0,0, 0,0,0,0, 0,0,0,5};

   dummy_connection.ripaddr = dummy_addr;

   // V5
   //pack("!2B2HBHB", B COMMAND_CLASS_FIRMWARE_UPDATE_MD, B FIRMWARE_UPDATE_MD_PREPARE_GET,
   // H manufacturer_id, H fw_id , B target_id , H fragment_size, B hardware_version)
   static uint8_t cmd_fw_upd_md_req_get[] = {COMMAND_CLASS_FIRMWARE_UPDATE_MD_V4,
                                             FIRMWARE_UPDATE_MD_REQUEST_GET,
                                             0x00, 0x00, // manufac id 1, 2
                                             0x00, 0x00, // fw id 1, 2
                                             0x00, 0x00, // fw cksum 1, 2
                                             0x00, // fw target
                                             0x01, 0x28, // frag size 1, 2
                                             0x00, // reserved
                                             0x01 // hw version
    };

   res = Fwupdate_Md_CommandHandler(&dummy_connection,
                                    cmd_fw_upd_md_req_get,
                                    sizeof(cmd_fw_upd_md_req_get));
   printf("res: %d\n", res);
}

void send_fw_upd_md_rep() {
   command_handler_codes_t res = 0;
   static uint8_t cmd_fw_upd_md_rep1[] = {COMMAND_CLASS_FIRMWARE_UPDATE_MD_V4,
                                         FIRMWARE_UPDATE_MD_REPORT,
                                         0x00, 0x01, // report number
                                          0xAA, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0xAA, 0x43, 0x44, 0x45,
                                         0xAA, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0xAA, 0x43, 0x44, 0x45,
                                         0xAA, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0xAA, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0xBB,
                                          0x1D, 0x0F}; // crc init
   static uint8_t cmd_fw_upd_md_rep2[] = {COMMAND_CLASS_FIRMWARE_UPDATE_MD_V4,
                                         FIRMWARE_UPDATE_MD_REPORT,
                                         0x00, 0x01, // report number
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                         0x42, 0x43, 0x44, 0x45, 0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0xAA, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0xAA, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0xAA, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0xCC, 0x43, 0x44, 0x45,0xAA, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0xAA, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0xDD,
                                          0x1D, 0x0F}; // crc init
   static uint8_t cmd_fw_upd_md_replast[] = {COMMAND_CLASS_FIRMWARE_UPDATE_MD_V4,
                                         FIRMWARE_UPDATE_MD_REPORT,
                                         0x80, 0x03, // report number
                                          0xCC, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0x45,
                                          0x42, 0x43, 0x44, 0xDD,
                                          0x1D, 0x0F}; // crc init
   int ii;
   res = Fwupdate_Md_CommandHandler(&dummy_connection,
                                    cmd_fw_upd_md_rep1,
                                    sizeof(cmd_fw_upd_md_rep1));
   for (ii = 0; ii< 250; ii++) {
   printf("res: %d\n", res);
   cmd_fw_upd_md_rep2[3] +=1;
   res = Fwupdate_Md_CommandHandler(&dummy_connection,
                                    cmd_fw_upd_md_rep2,
                                    sizeof(cmd_fw_upd_md_rep2));
   printf("res: %d, sending %d\n", res, ii+1);
   }
   cmd_fw_upd_md_replast[3] = ii+2;
   res = Fwupdate_Md_CommandHandler(&dummy_connection,
                                    cmd_fw_upd_md_replast,
                                    sizeof(cmd_fw_upd_md_replast));
   printf("res: %d\n", res);
}

void send_fw_upd_prep_get() {
   command_handler_codes_t res = 0;
   // V5
   //pack("!2B2HBHB", B COMMAND_CLASS_FIRMWARE_UPDATE_MD, B FIRMWARE_UPDATE_MD_PREPARE_GET,
   // H manufacturer_id, H fw_id , B target_id , H fragment_size, B hardware_version)
   static uint8_t cmd_fw_upd_md_prep_get[] = {COMMAND_CLASS_FIRMWARE_UPDATE_MD_V5,
                                             FIRMWARE_UPDATE_MD_PREPARE_GET,
                                             0x00, 0x00, // manufac id 1, 2
                                             0x00, 0x05, // fw id 1, 2
                                             0x01, // fw target
                                             0x00, 0x28, // frag size 1, 2
                                             0x01 // hw version
    };

   test_print(1, "Prep test");
   res = Fwupdate_Md_CommandHandler(&dummy_connection,
                                    cmd_fw_upd_md_prep_get,
                                    sizeof(cmd_fw_upd_md_prep_get));

   check_true(res == COMMAND_HANDLED, "prep command handled");

   static uint8_t cmd_rep0[] = {COMMAND_CLASS_FIRMWARE_UPDATE_MD_V5,
                                FIRMWARE_UPDATE_MD_PREPARE_REPORT,
                                0x03,// status FIRMWARE_NOT_UPGRADABALE
                                0x00,
                                0x00   }; // fw checksum

  check_true(
             (ZW_SendDataZIP_args.datalen == sizeof cmd_rep0)
             &&
             (memcmp(ZW_SendDataZIP_args.dataptr,
                     cmd_rep0, ZW_SendDataZIP_args.datalen) == 0),
             "Prep report");
}


int main()
{
   verbosity = test_case_start_stop;

   /* Start the logging system at the start of main. */
   zgw_log_setup("test_CC_FirmwareUpdate.log");

   /* Log that we have entered this function. */
   zgw_log_enter();

   cfg_init();
   Fwupdate_MD_init();

   send_fw_upd_prep_get();

   send_fw_upd_md_req_get();
   send_fw_upd_md_rep();


  close_run();

  /* We are now exiting this function */
  zgw_log_exit();
  /* Close down the logging system at the end of main(). */
  zgw_log_teardown();

  return numErrs;
}
