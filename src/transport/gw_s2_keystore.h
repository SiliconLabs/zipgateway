
/* © 2014 Silicon Laboratories Inc.
 */


#if !defined(GW_S2KEYSTORE_H)
#define GW_S2KEYSTORE_H


/**
 * Get the private ECDH key.
 * @param buf     buffer bit enough to hold the 32 bytes key.
 * @param dynamic flag indicating if its the dynamic or the static key that should be retrieved.
 *                the static key is read from the NVR/Userpage of the chip. The dynamic key is
 *                random and updated after each inclusion or GW restart.
 */
void gw_keystore_private_key_read(uint8_t *buf, int dynamic) ;


#endif // GW_S2KEYSTORE_H

