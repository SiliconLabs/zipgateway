/* © 2018 Silicon Laboratories Inc.
 */
#ifndef MULTICAST_TLV_H_
#define MULTICAST_TLV_H_

#include "zgw_nodemask.h"
#include <ZW_classcmd_ex.h>
#include "ZW_zip_classcmd.h"
#include "uip.h"
#include "RD_types.h"

extern nodemask_t mcast_node_list;
extern BYTE cur_send_ima;

#define MCAST_ACK_STATUS_ACK_FLAG            0x10
#define MCAST_ACK_STATUS_SUPERVISION_FLAG    0x20

typedef enum {
  MCAST_ACK_IP6_COMPRESSION_8_BYTE = 0x00,
  MCAST_ACK_IP6_COMPRESSION_1_BYTE = 0x01,
} mcast_ack_ip6_compression_t;


/*
 * Number of bytes needed (at most) for a single multicast ack value element.
 * (1 flags byte + 1 (optional) supervision byte + (up to) 8 ip address bytes)
 */
#define MAX_MCAST_ACK_SIZE_PER_NODE 10

typedef struct
{
  u8_t flag;
  union {
    u8_t compressed[1];
    u8_t non_compressed[8];
  } ipv6;
} mcast_address_t;

typedef struct
{
  union {
    mcast_address_t address;
  };
} mcast_tlv_data_t;

typedef struct
{
  u8_t type;
  u8_t length[2];
  u8_t value[1];
} mcast_tlv_t;

typedef struct {
  BOOL ack_status;
  BOOL supervision_present;
  nodeid_t node_ID;
  u8_t supervision_status;
} mcast_status_t;

/**
 * Array for collecting TX statuses for multicast follow-ups
 */
extern mcast_status_t global_mcast_status[ZW_MAX_NODES];
/**
 * Populated length of the global_mcast_status array
 */
extern unsigned int global_mcast_status_len;

/* Return codes from parse_CC_ZIP_EXT_HDR() */
typedef enum
{
  ZIP_EXT_HDR_SUCCESS, DROP_FRAME, OPT_ERROR
} return_codes_zip_ext_hdr_t;

/**
 * Parse the extension TLVs in ZIP header
 * @param payload First byte of first TLV
 * @param length Length of TLVs, excluding length of entire extended header
 * @param ext_hdr_size of extended header (not counting the one byte size field)
 *
 * @returns Status of parsing.
 */
int parse_CC_ZIP_EXT_HDR(const BYTE* payload, uint16_t length, zwave_connection_t *zw_conn, uint16_t *ext_hdr_size);

/**
 * Generates the extended header multicast acknowledgemen status value.
 *
 * NB: Does not write the Type and Length to the buffer.
 *
 * @param zw_conn       Active Z-Wave connection
 * @param mcast_status  Array of multicast statuses.
 * @param num_nodes     Number of elements in array mcast_status.
 * @param buf           Buffer ack values should be placed
 * @param buf_size      Size of buf.
 *
 * @return Length of generated option values. 0 if buf_size too small.
 */
uint16_t gen_mcast_ack_value(const zwave_connection_t *zw_conn, const mcast_status_t mcast_status[], uint16_t num_nodes, u8_t *buf, u16_t buf_size);

/**
 * Add an extended header option to a frame
 *
 * @param pkt          Pointer to ZIP_PACKET
 * @param bufsize      Size of buffer containing ZIP_PACKET including variable
 *                     length payload buffer. The actual space used is encoded
 *                     in headers in the payload as per this function.
 * @param opt_type     Type of extended header option to add.
 * @param opt_val      Value for new extended header option (does not include type and length)
 * @param opt_len      Length of value for new extended header option.
 * @param opt_len_size Number of bytes in the option header to use for the length. Must be 1 or 2 bytes.
 *
 * @return Number of bytes added to the packet. 0 if bufsize is too small to add the provided extended header option.
 */
uint16_t add_ext_header(ZW_COMMAND_ZIP_PACKET* pkt, int bufsize, uint8_t opt_type, uint8_t* opt_val, uint16_t opt_len, uint8_t opt_len_size);


#endif
