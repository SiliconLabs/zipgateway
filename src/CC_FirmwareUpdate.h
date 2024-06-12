/* © 2014 Silicon Laboratories Inc.
 */

#ifndef FIRMWAREUPDATE_H_
#define FIRMWAREUPDATE_H_

//#include "contiki-net.h"
//#include "TYPES.H"
//#include "ZW_udp_server.h"
#include "Serialapi.h"
#include <stdint.h>

/** The number of Firmware images (except the Z-Wave chip image).
 * Used in CC Firmware Update MD Report. */
#define ZIPGW_NUM_FW_TARGETS 1
#define ZW_FW_HEADER_LEN 0x08UL
#define MD5_CHECKSUM_LEN 0x10
#define MAX_ZW_FWLEN (0x20000UL + ZW_FW_HEADER_LEN)
#define MAX_ZW_WITH_MD5_FWLEN (0x20000UL + ZW_FW_HEADER_LEN + MD5_CHECKSUM_LEN)

/** Max firmware image size for the 700 series */
#define MAX_ZW_GECKO_FWLEN (256 * 1024)

/** \ingroup CMD_handler
 * \defgroup firmware_cmd_handler Firmware Update Handler
 *
 * Handles the firmware update command class.
 * The Z/IP Gateway can update the firmware of the Z-Wave module and also
 * update its certificates. This modules handles the firmware update protocol
 * and the actual upgrading.
 * @{
 */

/**
 * Meta-data for the firmware image.
 */
struct image_descriptor {
  uint32_t firmware_len;
  uint16_t firmware_id;
  uint16_t crc;
  uint8_t target;
};

/**
 * Check that no firmware update process is in progress.
 *
 * \return True if the Z/IP Gateway is updating firmware, false otherwise.
 */
bool FirmwareUpdate_idle();

/**
 * Update the 500 series with an image stored in the gateways eeprom.dat file.
 */
int ZWFirmwareUpdate(unsigned char isAPM, char *fw_filename, int size);

/**
 * Update the 700 series with an image stored in a file.
 *
 * \param fw_desc Meta-data about the image.
 * \param chip_desc Meta-data about the chip.
 * \param filename The image file.
 * \param len Length of the filename.
 * \return TRUE if update was successful, FALSE otherwise.
 */
int ZWGeckoFirmwareUpdate(struct image_descriptor *fw_desc,
                          struct chip_descriptor *chip_desc, char *filename,
                          size_t len);

/**
 * @}
 */
#endif /* FIRMWAREUPDATE_H_ */
