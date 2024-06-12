/* © 2017 Silicon Laboratories Inc.
 */
#ifndef PROVISIONING_LIST_TYPES_H_
#define PROVISIONING_LIST_TYPES_H_

/** @brief Enum of the type codes for provisioning list TLV meta data.
 *
 * These are the codes used internally in the gateway and provisioning
 * list module.
 *
 * The provisioning list also supports types not listed here, for
 * reasons of extensibility.
 *
 * Note that these may be different from the types of the QR Code TLV
 * defined in SDS13937.
 */
typedef enum
{
  PVS_TLV_TYPE_PRODUCT_TYPE=0,
  PVS_TLV_TYPE_PRODUCT_ID,
  PVS_TLV_TYPE_MAX_INCL_REQ_INTERVAL,
  PVS_TLV_TYPE_UUID16,
  PVS_TLV_TYPE_NAME = 0x32,
  PVS_TLV_TYPE_LOCATION,
  PVS_TLV_TYPE_STATUS, /**< Note: Not stored as TLV in this implementation */
  PVS_TLV_TYPE_ADV_JOIN,
  PVS_TLV_TYPE_BOOTSTRAP_MODE, /**< Note: Not stored as TLV in this implementation */
  PVS_TLV_TYPE_NETWORK_INFO, /**< Network status and and node
                              * id. Note: Not stored as TLV.  Should
                              * be looked up in the Resource
                              * Directory. */
} pvs_tlv_type_t;

/** Type for device's provisioning bootmode (S2 or SmartStart).
 */
typedef enum {
    PVS_BOOTMODE_S2 = 0, /**< The item represents a classic S2 device. */
    PVS_BOOTMODE_SMART_START = 1, /**< The item represents a smart start device */
    PVS_BOOTMODE_LONG_RANGE_SMART_START = 2, /**<The item represents a Long Range smart start device */
    PVS_BOOTMODE_TLV_NOT_PRESENT=0xffff,
  } provisioning_bootmode_t;

/**
 * @brief Smart Start Inclusion status for an item in the provisioning list.
 */
typedef enum {
    PVS_STATUS_PENDING=0, /**< Pending inclusion */
    PVS_STATUS_PASSIVE=2, /**< Inclusion request for this device is not being accepted until user views/edits PL again */
    PVS_STATUS_IGNORED=3, /**< Inclusion request for this device is not being accepted until user un-ignores this device */
    PVS_STATUS_TLV_NOT_PRESENT=0xffff,
} pvs_status_t;

/**
 * @brief Network status for an item in the provisioning
 * list.  Should be looked up in the Resource Directory.
 */
typedef enum {
   PVS_NETSTATUS_NOT_IN_NETWORK=0, /**< The node in the Provisioning
                                    * List is not currently included in
                                    * the network. */
   PVS_NETSTATUS_INCLUDED, /**< The node in the Provisioning List is
                            * included in the network. */
   PVS_NETSTATUS_FAILING, /**< The node in the Provisioning List has
                           * been included in the Z-Wave network but
                           * is now marked STATUS_FAILING.*/
} pvs_netstatus_t;

#endif /* PROVISIONING_LIST_TYPES_H_ */
