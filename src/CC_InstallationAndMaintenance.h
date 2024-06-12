/* © 2014 Silicon Laboratories Inc.
 */

#ifndef IMA_H_
#define IMA_H_

#include "Serialapi.h"
#include "ZW_udp_server.h"
#include "RD_types.h"


struct route {
  u8_t repeaters[4];
  u8_t speed;
} __attribute__((packed));


struct node_ima {
  /**
   * Packet Error Count (PEC) – Also sometimes referred to as PER. PEC is measured via the
   * SerialAPI from the Z/IP Gateway. PEC is incremented each time the Z/IP Gateway receives a
   * callback from ZW_SendData with the status value ‘TRANSMIT_COMPLETE_NO_ACK’ for each
   * specified Z-Wave destination node.
   */
  u8_t pec;

  /* Sum of transmission time */
  u32_t mts;

  /* Sum of transmission time squared */
  u32_t mts2;

  /**
   * Transmission Counter (TC) – Number of transmissions sent each specified Z-Wave destination
   * node
   */
  u8_t tc;

  /**
   * Route Changes (RC) – RC is the number of times the protocol needed additional routes to
   * reach a destination device because of transmit failure. The number is a combination of Last
   * Working Route (LWR) changes and Jitter measurements during transmission attempts between
   * the Z/IP Gateway and the Z-Wave device.
       - RC is incremented automatically by the Z/IP Gateway when either of the below
       conditions are true:
       - Last Working Route is different between two subsequent calls to
       - ZW_SendData()  Tn – Tn-1 > 150ms where Tn and Tn-1 = time from SendData() returns to callback is received
       - IF 2 channel and FLIRS node, RC: Tn = Tn mod 1100
       - IF 3 channel and FLIRS node, RC cannot increment based on time calculation   *
   */
   u8_t rc;

   /**
    * Last Working Route max transmit power reduction (LWRdb)
    *  -  LWRdb is gathered through the already supported Powerlevel Command Class
    */
   //u8_t lwr_db;


   /*The last working route*/
   struct route lwr;

   u16_t last_tt;
};


/**
 * Called when just before senddata is issued
 */
void ima_send_data_start(nodeid_t node);

/**
 * Get a IMA statistics object
 */
const struct node_ima* ima_get_entry(nodeid_t node);

/*
 * Called when send_data is completed
 */
void ima_send_data_done(nodeid_t node,u8_t status, TX_STATUS_TYPE* tx_info);

/**
 * Reset all IMA statistic
 */
void ima_reset();


/**
 * RSSI – The Received Signal Strength Indication for the transmitted frame. If frame is routed,
 * NOTE that this will only contain the RSSI between the Z/IP GW and the first hop
 */
extern u8_t ima_last_snr;

/**
 * Transmission Time (TT) – Time from SendData() return to callback is received, in ms
 */
extern u16_t ima_last_tt;

/**
 * Indicates if the route was changed on the last transmission
 */
extern u8_t ima_was_route_changed;

/**
 * Ima options types
 */
enum {
  IMA_OPTION_RC = 0,
  IMA_OPTION_TT = 1,
  IMA_OPTION_LWR = 2,
  IMA_OPTION_INCOMING_RSSI,
  IMA_OPTION_ACK_CHANNEL,
  IMA_OPTION_TRANSMIT_CHANNEL,
  IMA_OPTION_TX_POWER = 9,
  IMA_OPTION_MEASURE_NOISE_FLOOR,
  IMA_OPTION_OUTGOING_RSSI
};

#endif /* IMA_H_ */


