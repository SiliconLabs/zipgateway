/* © 2018 Silicon Laboratories Inc.
 */
#include <stdlib.h>

#include "zip_router_ipv6_utils.h" /* nodeOfIP */
#include "ZIP_Router_logging.h"
#include "ZW_udp_server.h"
#include "ClassicZIPNode.h"
#include "zip_router_config.h"
#include "multicast_tlv.h"
#include "ipv46_addr.h"

#ifdef TEST_MULTICAST_TX
nodemask_t mcast_node_list;
#endif
BYTE cur_send_ima;

#ifdef TEST_MULTICAST_TX
/* Used for collecting follow up statuses */
mcast_status_t global_mcast_status[ZW_MAX_NODES];
unsigned int global_mcast_status_len;
#endif

int parse_CC_ZIP_EXT_HDR(const BYTE* payload, uint16_t length, zwave_connection_t *zw_conn, uint16_t *ext_hdr_size)
{
  tlv_t* tlv = (tlv_t*) payload;
  const BYTE* head_payload = payload;
  BYTE* tail_payload = (BYTE*)payload + length;
  *ext_hdr_size = length;

  mcast_tlv_t* mcast_tlv = NULL;
  mcast_tlv_data_t* mcast_tlv_data = NULL;
  BYTE* mcast_payload;
  u16_t mcast_tlv_data_length;

  cur_send_ima = 0;
  while ( (BYTE*)tlv < tail_payload)
  {
    switch(tlv->type & ZIP_EXT_HDR_OPTION_TYPE_MASK) {
      case INSTALLATION_MAINTENANCE_GET:
        cur_send_ima = TRUE;
        tlv = (tlv_t*)((BYTE*) tlv + (tlv->length + 2));
        break;
      case ENCAPSULATION_FORMAT_INFO:
        if(tlv->length < 2) {
          return OPT_ERROR;
        }
        zw.scheme = efi_to_shceme(tlv->value[0],tlv->value[1]);
        DBG_PRINTF("zw.scheme: %d\n", zw.scheme);
        tlv = (tlv_t*)((BYTE*) tlv + (tlv->length + 2));
        break;
#ifdef TEST_MULTICAST_TX
      case EXT_ZIP_PACKET_HEADER_LENGTH:
        /* Extended header option must be present firstly and length has to be 2 */
        if(tlv != (tlv_t*) head_payload || tlv->length != 2) {
          return OPT_ERROR;
        }
        /* Extend the length by what's been indicated and substract 1 byte field */
        uint16_t hdr_length = (tlv->value[0]<<8 | tlv->value[1]) - 1;
        tail_payload = tail_payload - length + hdr_length;
        *ext_hdr_size = hdr_length;
        DBG_PRINTF("mcast address length extended to %d\n", hdr_length);
        tlv = (tlv_t*)((BYTE*) tlv + (tlv->length + 2));
        break;
      case MULTICAST_DESTINATION_ADDRESS:
        /* Mark the multicast transmit flag */
        ClassicZIPNode_addTXOptions(TRANSMIT_OPTION_MULTICAST);

        /* Set the start, end, and length of address list */
        mcast_tlv = (mcast_tlv_t*) tlv;
        mcast_tlv_data_length = mcast_tlv->length[0]<<8 | mcast_tlv->length[1];
        mcast_payload = (BYTE*)mcast_tlv + mcast_tlv_data_length;
        DBG_PRINTF("mcast address total length %d\n", mcast_tlv_data_length);

        /* Parse the address list and fill out nodemask */
        nodemask_clear(mcast_node_list);
        while((BYTE*)mcast_tlv < mcast_payload) {
          uip_ip6addr_t ip6;
          mcast_tlv_data = (mcast_tlv_data_t*) mcast_tlv->value;

          /*
           * Formulate one IPv6 address as a template to fill in compressed
           * mcast IP and query the node ID afterwards. The local ip address
           * from zw.conn only contains PAN prefix + the node id and we are also
           * considering IPv4 case here.
           */
          ipOfNode(&ip6, zw_conn->lipaddr.u8[15]);
          if (uip_is_4to6_addr(&zw_conn->ripaddr)) {
            uip_ipv4addr_t ip4;

            if (!ipv46nat_ipv4addr_of_node(&ip4, zw_conn->lipaddr.u8[15])) {
              ERR_PRINTF("ipv46nat_ipv4addr_of_node() failed\n");
            }

            ip6.u16[0] = 0;
            ip6.u16[1] = 0;
            ip6.u16[2] = 0;
            ip6.u16[3] = 0;
            ip6.u16[4] = 0;
            ip6.u16[5] = 0xFFFF;
            ip6.u16[6] = ip4.u16[0];
            ip6.u16[7] = ip4.u16[1];
          }

          if (mcast_tlv_data->address.flag & MCAST_ACK_IP6_COMPRESSION_1_BYTE) {
            memcpy(&ip6.u8[15], mcast_tlv_data->address.ipv6.compressed, 1);
            mcast_tlv = (mcast_tlv_t*)((BYTE*) mcast_tlv + 2);
          } else { /* MCAST_ACK_IP6_COMPRESSION_8_BYTE */
            memcpy(&ip6.u8[8], mcast_tlv_data->address.ipv6.non_compressed, 8);
            mcast_tlv = (mcast_tlv_t*)((BYTE*) mcast_tlv + 9);
          }
          nodemask_add_node(nodeOfIP(&ip6), mcast_node_list);
        }
        tlv = (tlv_t*)((BYTE*) tlv + (mcast_tlv_data_length + 3));
        break;
#endif
    default:
        if (tlv->type & ZIP_EXT_HDR_OPTION_CRITICAL_FLAG)
        {
          ERR_PRINTF("Unsupported critical option %i\n", (tlv->type & ZIP_EXT_HDR_OPTION_TYPE_MASK));
          /* Send option error */
          return OPT_ERROR;
        }
        tlv = (tlv_t*)((BYTE*) tlv + (tlv->length + 2));
        break;
    }
  }

  if((BYTE*)tlv != tail_payload) {
    ERR_PRINTF("Invalid extended header\n");
    return DROP_FRAME;
  }
  return ZIP_EXT_HDR_SUCCESS;
}

#ifdef TEST_MULTICAST_TX
uint16_t gen_mcast_ack_value(const zwave_connection_t *zw_conn, const mcast_status_t mcast_status[], uint16_t num_nodes, u8_t *buf, u16_t buf_size)
{
  int i = 0;
  u16_t count = 0;
  uip_ipv4addr_t ip4;
  uip_ip6addr_t  ip6;
  BOOL is_ipv4;
  mcast_ack_ip6_compression_t ip6_compression;

  uip_ipv4addr_t ack_src_ip4;
  BOOL is_ack_src_ipv4 = FALSE;

  if (uip_is_4to6_addr(&zw_conn->ripaddr)) {
    is_ack_src_ipv4 = ipv46nat_ipv4addr_of_node(&ack_src_ip4, zw_conn->lipaddr.u8[15]);
  }

  /*
   * Before processing the next status element we check that
   * buf has room for MAX_MCAST_ACK_SIZE_PER_NODE more bytes.
   */
  for (i = 0; i < num_nodes && (count + MAX_MCAST_ACK_SIZE_PER_NODE) <= buf_size; i++) {

    ip6_compression = MCAST_ACK_IP6_COMPRESSION_8_BYTE;

    if (is_ack_src_ipv4 && ipv46nat_ipv4addr_of_node(&ip4, mcast_status[i].node_ID)) {
      is_ipv4 = TRUE;
    } else {
      is_ipv4 = FALSE;
    }

    if (is_ipv4)
    {
      /*
       * If three first bytes of node ip and gw ip are equal we
       * can use single byte compression
       */
      if (uip_ipaddr_prefixcmp(&ip4, &ack_src_ip4, 24)) {
        ip6_compression = MCAST_ACK_IP6_COMPRESSION_1_BYTE;
      }
    } else {
      ipOfNode(&ip6, mcast_status[i].node_ID);
      if (uip_ipaddr_prefixcmp(&ip6, &zw_conn->lipaddr, 120)) {
        ip6_compression = MCAST_ACK_IP6_COMPRESSION_1_BYTE;
      }
    }

    /* IPv6 compression bits */
    buf[count] = ip6_compression;

    /* ACK status bit */
    if (mcast_status[i].ack_status) {
      buf[count] |= MCAST_ACK_STATUS_ACK_FLAG;
    }

    /* Supervision status */
    if (mcast_status[i].supervision_present) {
      buf[count++] |= MCAST_ACK_STATUS_SUPERVISION_FLAG;
      buf[count] = mcast_status[i].supervision_status;
    }
    count++;

    /* IP address */
    switch (ip6_compression)
    {
      case MCAST_ACK_IP6_COMPRESSION_1_BYTE:
        if (is_ipv4) {
          buf[count++] = ip4.u8[3];
        } else {
          buf[count++] = ip6.u8[15];
        }
        break;
      case MCAST_ACK_IP6_COMPRESSION_8_BYTE:
      default: /* Fall through */
        if (is_ipv4) {
          buf[count++] = 0x00;
          buf[count++] = 0x00;
          buf[count++] = 0xff;
          buf[count++] = 0xff;
          memcpy(&buf[count], &ip4.u8[0], 4);
          count += 4;
        } else {
          memcpy(&buf[count], &ip6.u8[8], 8);
          count += 8;
        }
        break;
    }
  }

  /* If we ended the for-loop before processing all multicast statuses
   * it means that buf was too small
   */
  if (i < num_nodes) {
    ERR_PRINTF("Insuficcient buffer size for multicast acknowledge status.");
    return 0;
  } else {
    return count;
  }
}
#endif

uint16_t add_ext_header(ZW_COMMAND_ZIP_PACKET* pkt, int bufsize, uint8_t opt_type, uint8_t* opt_val, uint16_t opt_len, uint8_t opt_len_size)
{
  uint16_t payload_size_before  = 0;
  uint16_t payload_size_after   = 0;
  uint16_t required_buffer_size = 0;
  uint16_t new_option_offset    = 0;
  uint8_t *p_length_option      = NULL;

  ASSERT(bufsize >= sizeof(ZW_COMMAND_ZIP_PACKET));
  ASSERT(opt_len_size == 1 || opt_len_size == 2);

  if (opt_len_size == 1 && opt_len > 255) {
    ERR_PRINTF("Can't write an extended header option length larger than 255 to a single byte!\n");
    return 0;
  }

  /* Do we already have one or more extended header options? */
  if (pkt->flags1 & ZIP_PACKET_FLAGS1_HDR_EXT_INCL) {

    /* Check for presence of an extended header length option */
    if (pkt->payload[0] == 0xff &&
        (pkt->payload[1] & ZIP_EXT_HDR_OPTION_TYPE_MASK) == EXT_ZIP_PACKET_HEADER_LENGTH) {

      /* Get payload size from two-byte length field in extended
       * header length option
       */
      p_length_option = &pkt->payload[1];

      /* 2'nd byte of the length option is the length of the option
       * itself (must always be 2)
       */
      ASSERT(p_length_option[1] == 2);

      /* 3'rd byte = length MSB, 4'th byte = length LSB */
      payload_size_before = (p_length_option[2] << 8) | p_length_option[3];

    } else {

      /* Get payload size from "standard" single byte
       * header extension length field
       */
      payload_size_before = pkt->payload[0];
    }

    new_option_offset = payload_size_before;

  } else {
    /* We are adding the very first extended header to the packet
     *
     * First byte of the payload is the 1-byte length. We'll update
     * it later, but for now we need to ensure the new option is
     * added after.
     */
    new_option_offset = 1;
  }

  /* Payload size after adding new option */
  payload_size_after = new_option_offset
                       + 1              // One byte for option type
                       + opt_len_size   // One or two bytes for length
                       + opt_len;       // The header option data

  required_buffer_size = ZIP_HEADER_SIZE + payload_size_after;

  /* Do we need to add the four byte length option? */
  if (payload_size_after > 255 && !p_length_option) {
    required_buffer_size += 4;
  }

  if (required_buffer_size > bufsize) {
    ERR_PRINTF("Can't add extended header. Packet buffer too small!\n");
    return 0;
  }

  /* Buffer size OK. Now start appending the new option to buf */

  u8_t *p = pkt->payload + new_option_offset;

  *p++ = opt_type;

  /* Should the length be written to one or two bytes? */
  if (opt_len_size == 2)
  {
    *p++ = opt_len >> 8;    // Length 1 (MSB)
  }
  *p++ = opt_len & 0x00ff;  // Length (single byte) or Length 2 (LSB)

  memcpy(p, opt_val, opt_len);

  /* Do we need two bytes for the total payload size? */
  if (payload_size_after > 255) {
    if (!p_length_option) {
      /* The four byte length option is not already there. Make space for it
       * by moving the memory following the "legacy" length field payload[0]
       * down by four bytes (payload[0] is part of the payload counted by
       * "payload_size_after" but it's not part of the memory to move so we
       * subtract one when calling memmove)
       */

      memmove(&pkt->payload[5], &pkt->payload[1], payload_size_after - 1);
      p_length_option = &pkt->payload[1];
      payload_size_after += 4;
    }

    pkt->payload[0]    = 255;
    p_length_option[0] = ZIP_EXT_HDR_OPTION_CRITICAL_FLAG | EXT_ZIP_PACKET_HEADER_LENGTH;
    p_length_option[1] = 2;
    p_length_option[2] = payload_size_after >> 8;      // MSB
    p_length_option[3] = payload_size_after & 0x00ff;  // LSB
  } else {
    pkt->payload[0] = payload_size_after;
  }

  pkt->flags1 |= ZIP_PACKET_FLAGS1_HDR_EXT_INCL;

  /* Return number of bytes added to zip packet */
  return payload_size_after - payload_size_before;
}
