/* © 2014 Silicon Laboratories Inc.
 */

#ifndef ZWPGM_PROTOCOL_H_
#define ZWPGM_PROTOCOL_H_


/**
 * \defgroup zpgp Z-Wave programmer Protocol
 *
 * Low level programming functions supported by the 50x series chip.
 *
 * @{*/

#ifndef __CONTIKI_CONF_H__
typedef unsigned char u8_t;
typedef unsigned short u16_t;
#endif


typedef enum {EP0,EP1,EP2,EP3,EP4,EP5,EP6,EP7,RBAP} lock_byte_t;
#define RBAP_READBACK_PROTECTION  1
#define RBAP_AUTOPROG0 2
#define RBAP_AUTOPROG1 4

struct signature {
  u8_t jdec_id[5];
  u8_t chip_type;
  u8_t chip_revision;
  u8_t unused; // alignment
};


typedef struct zw_pgmr {
  /**
   * Send a 4 byte command and read a 4 byte response.
   *
   * Then called with a NULL pointer a single dummy byte is sent
   */
  int(*xfer)( u8_t* cmd,u8_t len,u8_t rlen);

  /**
   * Output errors.
   */
  int (*err)(const char *format, ...);

  /**
   * Output to the console.
   */
  int (*con)(const char *format, ...);


  struct signature sig;

  /* Indicates whether the APM is enabled.*/
  int apm;

  /*Flash block size */
  int blk_sz;

  /*Number of blocks.*/
  int blk_num;

  /*USB programmer.*/
  int usb;

  int (*open)(struct zw_pgmr* p,const char* path);
  void (*close)(void);


}zw_pgmr_t;


/**
 * Enable serial programming after reset has been low t_PE.
 */
int zpgp_enable_interface(zw_pgmr_t* p);


/**
 * Read data at the first byte of the Flash sector number.
 * Sector. Valid range of Sector is 00h-3Fh
 */
int zpgp_read_flash(zw_pgmr_t* p,u8_t sector);

/**
 * Read data at SRAM address Addr1:Addr0.
 * Valid range of Addr1:Addr0 is 0000h-3FFFh
 */
int zpgp_read_SRAM(zw_pgmr_t* p, u16_t addr);

/**
 * Follow a "Read SRAM" or a
 * "Read Flash" command. This command will
 * read the next three memory locations from the
 * SRAM or Flash, depending on the preceding
 * command.
 */
int zpgp_continue_read(zw_pgmr_t* p,u8_t* data,u8_t burst);

/**
 * Write data Data to SRAM page buffer at
 * address Addr1:Addr0. Valid range of
 * Addr1:Addr0 is 0000h-3FFFh.
 */
int zpgp_write_SRAM(zw_pgmr_t* p, u16_t addr, u8_t data);

/**
 * Follow a "Write SRAM"
 * command and write three bytes, Data0,
 * Data1, Data2 to the next memory locations in
 * SRAM.
 *
 */
int zpgp_continue_write(zw_pgmr_t* p, const u8_t* data,u8_t bulk);


/**
 * Erase the code space and the NVR
 * space including lock bits.
 */
int zpgp_erase_chip(zw_pgmr_t* p);

/**
 * Erase the code sector number
 * Sector. Valid range of Sector is 00h-3Fh
 */
int zpgp_erase_sector(zw_pgmr_t* p,u8_t sector);

/**
 * Write previously loaded SRAM data to the
Flash Memory. The Flash start address will be
the first byte of the sector set by the Sector
value. Valid range of Sector is 00h-3Fh
 */
int zpgp_write_flash_sector(zw_pgmr_t* p,u8_t sector);


/**
 * Read the status of the programming logic.
 */
int zpgp_check_state(zw_pgmr_t* p);

/**
 * Read the signature byte number Num.
 */
int zpgp_read_signature_byte(zw_pgmr_t* p,u8_t num);

/**
 * Disable the EooS mode.
 */

int zpgp_disable_EooS_mode(zw_pgmr_t* p);

/**
 * Set the mode in
 * EooS Mode after a reset. The mode is
 * disabled by power cycling the chip.
 */
int zpgp_enable_EooS_mode(zw_pgmr_t* p);



/**
 * Read the lock bits, LockData.
 * Nine bytes can be set by executing the "Write Lock Bits" command. The bits in these bytes can be set
 * one at the time or both at the same time, but once they are set they can't be cleared. The state of the
 * lock bits can always be read with the "Read Lock Bits" command.
 * The lock bits can be read and set from a programming interface. Only an "Erase Chip" operation can
 * erase the lock bits. The lock bits are used to:
 *  - Enable read back-protection of code space
 *  - Auto Programming Mode lock bits.
 *  - Set erase-protection on part of code area. Only protects against erase operations initiated by the
 *    MCU. Contained in the bytes EP0-EP7
 *
 *    Lock bit bytes:
 *     Byte    0   1   2   3   4   5   6   7   8
 *     Field EP0 EP1 EP2 EP3 EP4 EP5 EP6 EP7 RBAP
 * Setting either of these Lock bits will not prevent the internal MCU from reading the Flash memory
 * Note: A "Set Lockbit" command must always be followed by "Check State" command(s) to ensure that
 * the chip is idle before new commands are issued.
 *
 * The RBAP byte, as shown in Table 7, contains the 2 lock bits for controlling the Non-volatile Auto Prog
 * setting (bits 2 and 1), see section 5.2, for a description of how to use these bits.
 * The RBAP byte also contains the Read-Back protection lock bit (bit 0). When this bit is 0b, the chip will
 * return 00h when reading from Flash code space. The CRC32 check function still works even if the chip is
 * read-back protected.
 * Bits 7-3 are unused and MUST be kept high, as they are reserved for future use.
 * Bit   |   7|  6 |  5 |  4 |  3 |         2 |          1 |                   0 |
 * Field | 1b | 1b | 1b | 1b | 1b | AutoProg1 | Auto Prog0 | Readback Protection |
 *
 *
 * The string "EP7:EP6:EP5:EP4:EP3:EP2:EP1:EP0" is a 64 bit string, where each bit controls the MCU's
 * ability to erase and program a Flash code sector. The bits are coded so that EP7 bit 7 controls sector 63
 * and EP0 bit 0 controls sector 0. E.g. EP3 bit 2 controls sector 26.
 * When a bit is 0b, the associated sector is protected from erasure and programmed by the internal MCU.
 * The bits can only be set to1b by executing the "Chip Erase" command.
 *
 * \param num byte number to read
 * \return lock byte, -1 if operation was unsuccessful.
 *
 */
int zpgp_read_lock_bits(zw_pgmr_t* p, lock_byte_t num);


/**
 * See section \ref zpgp_read_lock_bits  for a description of the
 * LockData contents.
 */
int zpgp_set_lock_bits(zw_pgmr_t* p,lock_byte_t num,u8_t lock_data);


/**
 * Set a byte in NVR space. Valid range for Addr
 * is 09h - FFh
 */
int zpgp_set_nvr(zw_pgmr_t* p,u8_t addr, u8_t value);

/**
 * Read from NVR space. Valid range for Addr is
 * 09h - FFh.
 */
int zpgp_read_nvr(zw_pgmr_t* p,u8_t addr);

/**
 * Run the CRC check procedure. Used to verify
 * that the correct data has been written to the
 * Flash.
 */
int zpgp_run_CRC_check(zw_pgmr_t* p);


/**
 * If the Auto Prog mode register bit is set,
 * the command clears the Auto Prog mode
 * register bit and resets the chip.
 */
void zpgp_reset_chip(zw_pgmr_t* p);

/**
 * This bit is set when a "Run CRC Check" command is
 * issued and it will be cleared when the CRC check
 *procedure is done
 */
#define CRC_BUSY 1

/**
 * This bit is cleared when a "Run CRC Check"
 * command is issued and it will be set if the CRC check
 * procedure passes.
 */
#define CRC_DONE 2


/**
 * This bit is cleared when a "Run CRC Check"
 * command is issued and it will be set if the CRC check
 * procedure fails.
 */
#define CRC_FAILED 4
/**
 * This bit is set when the flash is the flash state
 * machine is busy.
 */
#define FLASH_FSM_BUSY 8

/**
 * This bit will be set if either a "Continue Write
 * Operation" or a "Continue Read Operation" are
 * refused. These operations will be refused if:
 * A "Continue Write operation" is not succeeding a
 * "Write SRAM" or a "Continue Write" command
 * A "Continue Read Operation" is not succeeding a
 * "Read Flash", a "Read SRAM" or a "Continue Read"
 * command
 */
#define CONT_OPERATION_REFUSED 0x20

/**
 * This bit is set if the "Execute out of SRAM" Mode has
 * been enabled
 */
#define EXEC_SRAM_ENABLED 0x80


/**
 @}*/
#endif /* ZWPGM_PROTOCOL_H_ */
