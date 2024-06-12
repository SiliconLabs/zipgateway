/* © 2018 Silicon Laboratories Inc.
 */

/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/
#include "test_helpers.h"
#include "ZW_udp_server.h"
#include "multicast_tlv.h"
#include "zip_router_config.h"
#include "ipv46_nat.h"
#include "RD_types.h"


/****************************************************************************/
/*                      PRIVATE TYPES and DEFINITIONS                       */
/****************************************************************************/

#define XSTR(s) #s
#define STR(t) XSTR(t)
#define CT(x) (check_true((x), "Line " STR(__LINE__)))

/****************************************************************************/
/*                              EXPORTED DATA                               */
/****************************************************************************/

/****************************************************************************/
/*                              PRIVATE DATA                                */
/****************************************************************************/

BYTE MyNodeID; // From ZIP_Router.c

struct router_config cfg;

#define PAN_NODES_SIZE 2
static uip_ip6addr_t pan_nodes_mapping[PAN_NODES_SIZE] = {
  {0xfd, 0x00, 0xaa, 0xaa, 0x2a, 0xf1, 0x0e, 0xff, 0x2a, 0xf1, 0x0e, 0xff, 0xfe, 0x02, 0xdd, 0xa1},
  {0xfd, 0x00, 0xaa, 0xaa, 0x2a, 0xf1, 0x0e, 0xff, 0x2a, 0xf1, 0x0e, 0xff, 0xfe, 0x02, 0xcc, 0xa2},
};
static uint8_t CC_ZIP_EXT_HDR_ipv6_non_compressed[] = {
  /* Type */
  0x80 | MULTICAST_DESTINATION_ADDRESS,
  /* Length */
  0x00, 0x12,
  /* Value */
  0x00, 0x2a, 0xf1, 0x0e, 0xff, 0xfe, 0x02, 0xdd, 0xa1,
  0x00, 0x2a, 0xf1, 0x0e, 0xff, 0xfe, 0x02, 0xcc, 0xa2,
};

static uint8_t CC_ZIP_EXT_HDR_ipv6_compressed[] = {
  /* Type */
  0x80 | MULTICAST_DESTINATION_ADDRESS,
  /* Length */
  0x00, 0x04,
  /* Value */
  0x01, 0xa1,
  0x01, 0xa2,
};

static mcast_status_t mcast_status[2] = {{TRUE, TRUE, 0, 0x00}, {TRUE, FALSE, 1, 0x01}};
static uint8_t expected_mcast_status_output[] = {
  /* Value */
  0x31, 0x00, 0xa1,
  0x10, 0x2a, 0xf1, 0x0e, 0xff, 0xfe, 0x02, 0xcc, 0xa2,
};

static uint8_t CC_ZIP_EXTRA_EXT_HDR_non_compressed[] = {
  /* Type */
  0x80 | EXT_ZIP_PACKET_HEADER_LENGTH,
  /* Length */
  0x02,
  /* Value */
  0x01, 0x16,
  /* Type */
  0x80 | MULTICAST_DESTINATION_ADDRESS,
  /* Length */
  0x01, 0x0e,
  /* Value */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1d, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x23
};
/****************************************************************************/
/*                              MOCKS FUNCTIONS                           */
/****************************************************************************/

uint16_t zip_payload_len = 200;
zwave_connection_t zw = {.lipaddr = {0xfd, 0x00, 0xaa, 0xaa, 0x2a, 0xf1, 0x0e, 0xff, 0x2a, 0xf1, 0x0e, 0xff, 0xfe, 0x02, 0xdd, 0xa1}};
static u8_t txOpts;

BYTE nodeOfIP(const uip_ip6addr_t* ip)
{
  return 0;
}

void ipOfNode(uip_ip6addr_t* dst, BYTE nodeID)
{
  memcpy(dst, &pan_nodes_mapping[nodeID], sizeof(uip_ip6addr_t));
}

security_scheme_t efi_to_shceme(uint8_t ext1, uint8_t ext2)
{
  return 0;
}

u8_t ipv46nat_ipv4addr_of_node(uip_ipv4addr_t* ip,nodeid_t node)
{
  /*
  u8_t i;

  for(i=0; i < nat_table_size;i++) {
    PRINTF("%d of %d %d\n",i,nat_table[i].nodeid, node);
    if(nat_table[i].nodeid == node && nat_table[i].ip_suffix) {
      ipv46nat_ipv4addr_of_entry( ip,&nat_table[i]);
      return 1;
    }
  }
  */
  return 0;
}

uint8_t ClassicZIPNode_getTXOptions(void)
{
  return txOpts;
}

void ClassicZIPNode_addTXOptions(uint8_t opt)
{
  txOpts |= opt;
}


/****************************************************************************/
/*                              TEST HELPERS                                */
/****************************************************************************/

/**
 * Convert 2 bytes (msb and lsb) to unsigned int
 */
uint16_t msblsb2uint(uint8_t msb, uint8_t lsb)
{
  uint16_t val = ((uint16_t) msb << 8) + lsb;
  test_print(4, "(msb << 8 + lsb) = 0x%x (%u)\n", val, val);
  return val;
}


/**
 * Call add_ext_header() and check results
 */
uint16_t check_add_ext_header(ZW_COMMAND_ZIP_PACKET *pkt, uint16_t buf_size, uint16_t bytes_used, uint8_t opt_type, uint8_t *opt_val, uint16_t opt_len, uint8_t opt_len_size)
{
  uint16_t bytes_added = add_ext_header(pkt,
                                        buf_size,
                                        opt_type,
                                        opt_val,
                                        opt_len,
                                        opt_len_size);

  uint16_t expected_bytes_added = sizeof(opt_type) + opt_len_size + opt_len;
  uint16_t new_opt_idx = bytes_used;

  if (bytes_used == 0) {
    expected_bytes_added += 1;  // Single byte length for first header
    new_opt_idx = 1;
  }

  if (bytes_used <= 255 && (bytes_used + expected_bytes_added) > 255) {
    expected_bytes_added += 4; // Header extension length option
    new_opt_idx += 4;
  }

  uint16_t expected_bytes_total = bytes_used + expected_bytes_added;

  check_true(bytes_added == expected_bytes_added, "Bytes added to packet buffer");

  check_true(pkt->flags1 & ZIP_PACKET_FLAGS1_HDR_EXT_INCL, "Extended header included bit");

  /* Check header length */
  if (expected_bytes_total <= 255) {
    check_true(pkt->payload[0] == expected_bytes_total, "Total extended header length");
  } else {
    check_true(pkt->payload[0] == 255, "Total extended header length");

    // Check extension length option
    check_true(pkt->payload[1] == (ZIP_EXT_HDR_OPTION_CRITICAL_FLAG | EXT_ZIP_PACKET_HEADER_LENGTH), "Extension length option");
    check_true(pkt->payload[2] == 2, "Extension length option length");
    check_true(msblsb2uint(pkt->payload[3], pkt->payload[4]) == expected_bytes_total, "Total extended header length (2 bytes = Extension length)");
  }

  /* Check new option header (type, length, value) */
  check_true(pkt->payload[new_opt_idx + 0] == opt_type, "Extended header option type");

  if (opt_len_size == 1) {
    check_true(pkt->payload[new_opt_idx + 1] == opt_len, "Extended header option length");
  } else {
    check_true(msblsb2uint(pkt->payload[new_opt_idx + 1], pkt->payload[new_opt_idx + 2]) == opt_len, "Extended header option length");
  }

  check_true(memcmp(&(pkt->payload[new_opt_idx + 1 + opt_len_size]), opt_val, opt_len) == 0, "Extended header option value");

  return bytes_added;
}


/****************************************************************************/
/*                              TEST CASES                                  */
/****************************************************************************/


/**
 * Test the multicast ack payload
 */
void test_mcast_ack(void)
{
  const char *tc_name = "test_mcast_ack";
  start_case(tc_name, 0);

  uint8_t mcast_status_output[512];

  int rc_ack = gen_mcast_ack_value(&zw, mcast_status, 2, mcast_status_output, sizeof(mcast_status_output));
  CT(sizeof(expected_mcast_status_output) == rc_ack);
  check_mem(expected_mcast_status_output, mcast_status_output, sizeof(expected_mcast_status_output), "Unexpected mcast ack status", "Data passed to mcast_ack");

  close_case(tc_name);
}

/**
 * Test the multicast TLV parsing
 */
void test_parse_CC_ZIP_EXT_HDR(void)
{
  const char *tc_name = "test_parse_CC_ZIP_EXT_HDR";
  static zwave_connection_t zw_conn;
  uint16_t ext_hdr_size = 0;
  start_case(tc_name, 0);

  CT(ZIP_EXT_HDR_SUCCESS == parse_CC_ZIP_EXT_HDR(CC_ZIP_EXT_HDR_ipv6_non_compressed, sizeof(CC_ZIP_EXT_HDR_ipv6_non_compressed), &zw_conn, &ext_hdr_size));
  CT(ext_hdr_size == sizeof(CC_ZIP_EXT_HDR_ipv6_non_compressed));

  close_case(tc_name);
}

void test_parse_CC_ZIP_EXT_HDR_02(void)
{
  const char *tc_name = "test_parse_CC_ZIP_EXT_HDR_02";
  start_case(tc_name, 0);

  const uint8_t extend_hdr[] = { 0x87, 0x00, 0x04, 0x01, 0x06, 0x01, 0x07};
  uint16_t ext_hdr_size = 0;
  static zwave_connection_t zw_conn;

  CT(ZIP_EXT_HDR_SUCCESS == parse_CC_ZIP_EXT_HDR(extend_hdr, 7, &zw_conn, &ext_hdr_size));
  CT(ext_hdr_size == 7);

  close_case(tc_name);
}

void test_parse_CC_ZIP_EXT_HDR_03(void)
{
  const char *tc_name = "test_parse_CC_ZIP_EXT_HDR_03";
  static zwave_connection_t zw_conn;
  uint16_t ext_hdr_size = 0;
  start_case(tc_name, 0);

  /* 10 being shorter length to verify the ext_hdr_size */
  CT(ZIP_EXT_HDR_SUCCESS == parse_CC_ZIP_EXT_HDR(CC_ZIP_EXTRA_EXT_HDR_non_compressed, 10, &zw_conn, &ext_hdr_size));
  CT(ext_hdr_size == sizeof(CC_ZIP_EXTRA_EXT_HDR_non_compressed));

  close_case(tc_name);
}


/**
 * Test adding a single small extended header
 */
void test_add_ext_header_one_small(void)
{
  const char *tc_name = "test_add_ext_header_one_small";
  start_case(tc_name, 0);

  /*----------------------------------------------------------*/

  static uint8_t zip_packet_buf[2000];
  static uint8_t option_val[] = {1, 2, 3, 4, 5, 6};
  ZW_COMMAND_ZIP_PACKET *pkt = (ZW_COMMAND_ZIP_PACKET*) zip_packet_buf;

  test_print(3, "# Add single small (< 255) option to extended header\n");

  check_add_ext_header(pkt, sizeof(zip_packet_buf), 0, 0x11, option_val, sizeof(option_val), 1);

  /*----------------------------------------------------------*/

  close_case(tc_name);
}


/**
 * Test adding a single large extended header (size > 255)
 */
void test_add_ext_header_one_large(void)
{
  const char *tc_name = "test_add_ext_header_one_large";
  start_case(tc_name, 0);

  /*----------------------------------------------------------*/

  static uint8_t zip_packet_buf[2000];
  static uint8_t option_val[300];
  ZW_COMMAND_ZIP_PACKET *pkt = (ZW_COMMAND_ZIP_PACKET*) zip_packet_buf;

  test_print(3, "# Add single large (> 255) option to extended header\n");

  memset(option_val, 0xfd, sizeof(option_val));

  check_add_ext_header(pkt, sizeof(zip_packet_buf), 0, 0x11, option_val, sizeof(option_val), 2);

  /*----------------------------------------------------------*/

  close_case(tc_name);
}


/**
 * Test adding a single extended header larger than the available buffer
 */
void test_add_ext_header_buf_too_small(void)
{
  const char *tc_name = "test_add_ext_header_buf_too_small";
  start_case(tc_name, 0);

  /*----------------------------------------------------------*/

  static uint8_t zip_packet_buf[200];
  static uint8_t option_val[210];
  ZW_COMMAND_ZIP_PACKET *pkt = (ZW_COMMAND_ZIP_PACKET*) zip_packet_buf;

  test_print(3, "# Add single option to extended header (buf too small)\n");

  uint16_t bytes_added = add_ext_header(pkt,
                                        sizeof(zip_packet_buf), // bufsize
                                        0x11,                   // Option type (dummy value)
                                        option_val,             // Option data
                                        sizeof(option_val),     // Size of option data
                                        1);                     // Size of length field in option header

  check_true(bytes_added == 0, "Bytes added to packet buffer");

  /*----------------------------------------------------------*/

  close_case(tc_name);
}

/**
 * Test adding multiple small extended headers (total size < 255)
 */
void test_add_ext_header_many_small(void)
{
  const char *tc_name = "test_add_ext_header_many_small";
  start_case(tc_name, 0);

  /*----------------------------------------------------------*/

  static uint8_t zip_packet_buf[2000];
  static uint8_t option_val1[] = {1, 2, 3, 4, 5, 6};
  static uint8_t option_val2[] = {11, 12, 13, 14, 15, 16, 17, 18};
  static uint8_t option_val3[] = {21, 22};
  ZW_COMMAND_ZIP_PACKET *pkt = (ZW_COMMAND_ZIP_PACKET*) zip_packet_buf;

  test_print(3, "# Add multiple small (total < 255) options to extended header\n");

  uint16_t total_bytes = 0;

  total_bytes += check_add_ext_header(pkt, sizeof(zip_packet_buf), total_bytes, 0x11, option_val1, sizeof(option_val1), 1);
  total_bytes += check_add_ext_header(pkt, sizeof(zip_packet_buf), total_bytes, 0x21, option_val2, sizeof(option_val2), 2);
  total_bytes += check_add_ext_header(pkt, sizeof(zip_packet_buf), total_bytes, 0x31, option_val3, sizeof(option_val3), 1);

  /*----------------------------------------------------------*/

  close_case(tc_name);
}


/**
 * Test adding multiple large extended headers (total size > 255)
 */
void test_add_ext_header_many_large(void)
{
  const char *tc_name = "test_add_ext_header_many_large";
  start_case(tc_name, 0);

  /*----------------------------------------------------------*/

  static uint8_t zip_packet_buf[2000];

  static uint8_t option_val1[200];
  static uint8_t option_val2[300];
  static uint8_t option_val3[400];

  ZW_COMMAND_ZIP_PACKET *pkt = (ZW_COMMAND_ZIP_PACKET*) zip_packet_buf;

  test_print(3, "# Add multiple large (total > 255) options to extended header\n");

  memset(option_val1, 0xfa, sizeof(option_val1));
  memset(option_val2, 0xfb, sizeof(option_val2));
  memset(option_val3, 0xfc, sizeof(option_val3));

  uint16_t total_bytes = 0;

  total_bytes += check_add_ext_header(pkt, sizeof(zip_packet_buf), total_bytes, 0x11, option_val1, sizeof(option_val1), 2);
  total_bytes += check_add_ext_header(pkt, sizeof(zip_packet_buf), total_bytes, 0x21, option_val2, sizeof(option_val2), 2);
  total_bytes += check_add_ext_header(pkt, sizeof(zip_packet_buf), total_bytes, 0x31, option_val3, sizeof(option_val3), 2);

  uint16_t expected_total_bytes =   1  // Single byte total length
                                  + 4  // header extension length option
                                  + 1 + 2 + sizeof(option_val1)  // Type (1b), length (2b), value
                                  + 1 + 2 + sizeof(option_val2)  // Type (1b), length (2b), value
                                  + 1 + 2 + sizeof(option_val3); // Type (1b), length (2b), value

  check_true(total_bytes == expected_total_bytes, "Total size of all extended headers");

  /*----------------------------------------------------------*/

  close_case(tc_name);
}


/**
 * Extended header and TLV tests begins here
 */
int main()
{
   test_mcast_ack();
   test_parse_CC_ZIP_EXT_HDR();
   test_parse_CC_ZIP_EXT_HDR_02();
   test_parse_CC_ZIP_EXT_HDR_03();

   test_add_ext_header_one_small();
   test_add_ext_header_one_large();
   test_add_ext_header_buf_too_small();
   test_add_ext_header_many_small();
   test_add_ext_header_many_large();

   close_run();
   return numErrs;
}
