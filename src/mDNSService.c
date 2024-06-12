/* © 2014 Silicon Laboratories Inc.
 */

/*******************************  ZW_udp_server.c  ***************************
 *           #######
 *           ##  ##
 *           #  ##    ####   #####    #####  ##  ##   #####
 *             ##    ##  ##  ##  ##  ##      ##  ##  ##
 *            ##  #  ######  ##  ##   ####   ##  ##   ####
 *           ##  ##  ##      ##  ##      ##   #####      ##
 *          #######   ####   ##  ##  #####       ##  #####
 *                                           #####
 *          Z-Wave, the wireless language.
 *
 *              Copyright (c) 2010
 *              Zensys A/S
 *              Denmark
 *
 *              All Rights Reserved
 *
 *    This source file is subject to the terms and conditions of the
 *    Zensys Software License Agreement which restricts the manner
 *    in which it may be used.
 *
 *---------------------------------------------------------------------------
 *
 * Description: ...
 *
 * Author:   jbu
 *
 * Last Changed By:  $Author: $
 * Revision:         $Revision: $
 * Last Changed:     $Date: $
 *
 ****************************************************************************/

/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/

#include <TYPES.H>
#include <ZW_typedefs.h>
#include <ZW_classcmd.h>
//#include <ZW_zip_api.h>
#include "zip_router_ipv6_utils.h" /* nodeOfIP, ipOfNode */
#include "ZIP_Router_logging.h" /*  */

#ifdef DEBUG_ALLOW_NONSECURE
#define ZWAVE_PORT 4123
#else
#define ZWAVE_PORT 41230
#endif


#ifndef MDNS_TEST
#include <ZW_zip_classcmd.h>
#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"
#include "uip.h"
//#include "contiki-net-ipv4.h"
#include "NodeCache.h"
#include "S2_wrap.h"
#include "net/uip-udp-packet.h"
//#include "net/uip-debug.h"
#include "sys/ctimer.h"
#include "ipv46_nat.h"
#include "ResourceDirectory.h"
#include "lib/random.h"
#include <s2_keystore.h> /* for security key types only */
#endif
#include <ctype.h>
#include <strings.h>

#include "mDNSService.h"
//#define PRINTF(...) printf(__VA_ARGS__)

/****************************************************************************/
/*                     EXPORTED TYPES and DEFINITIONS                       */
/****************************************************************************/

/****************************************************************************/
/*                      PRIVATE TYPES and DEFINITIONS                       */
/****************************************************************************/
//#define l3_udp_hdr_len (UIP_IPUDPH_LEN + uip_ext_len)

/****************************************************************************/
/*                              EXPORTED DATA                               */
/****************************************************************************/

/****************************************************************************/
/*                              PRIVATE DATA                                */
/****************************************************************************/
/****************************************************************************/
/*                              PRIVATE FUNCTIONS                           */
/****************************************************************************/

/****************************************************************************/
/*                              EXPORTED FUNCTIONS                          */
/****************************************************************************/

#ifdef MDNS_TEST
#include "lib/memb.h"
extern char output_buf[1024];
extern int output_len;
#define udp_data_len test_uip_datalen()
#else
#define udp_data_len uip_datalen()
#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#endif

#include <stdlib.h> // strtol
#define UIP_UDP_BUF  ((struct uip_udp_hdr *)&uip_buf[uip_l2_l3_hdr_len])

#define MDNS_PORT 5353
#define MAX_LABEL_SIZE 63
#define MAX_DOMAIN_SIZE 255
#define UIP_UDP_APP_SIZE (UIP_LINK_MTU - UIP_IPUDPH_LEN)

#define IPV6_LENGTH 16
#define MDNS_SRV_RECORD_DEFAULT_PRIORITY 0
#define MDNS_SRV_RECORD_DEFAULT_WEIGHT 0

static struct uip_udp_conn *server_conn;
static u8_t exiting;
#ifndef MDNS_TEST
PROCESS(mDNS_server_process, "mDNS server process");
#endif
int tie_braking_won = 0;

enum probe_type
{
NODE,
SERVICE,
IP
};

int find_duplicate_probe(const char*);
unsigned int probe_rr_len = 0;
char *probe_rr = 0;

static uint8_t tc_bit_set = 0;
static void *last_dns_pkt = NULL;
static uint16_t last_dns_pkt_len = 0;
uip_ip6addr_t last_src_ipaddr;

extern void
rd_node_probe_update(rd_node_database_entry_t* n);
extern void
rd_ep_probe_update(rd_ep_database_entry_t* ep);

extern int dont_set_cache_flush_bit;

/* Query session is used for probing. Right now its single session. */
typedef struct query_session
{
  u16_t inuse;
  char name[3][MAX_DOMAIN_SIZE];
  u8_t count;
  struct ctimer timer;
  void (*callback)(int bInt, void*);
  void* ctx;
  uint8_t no_questions;
  uint8_t no_authrrs;
  void *rd_node_database_entry;
  enum probe_type prob_type;

} query_session_t;

static query_session_t the_query_session;

/**
 * This buffer is used for building mDNS replies.
 * 
 * It must be large enough to hold a whole dns reply + plus 1 additional record.
 * All records holds a domain_string which worst case is
 *
 * RRecordSize
 * MAX_DOMAIN_SIZE + 12 + rdlength
 *
 */
static char reply[UIP_UDP_APP_SIZE + 512];
static int do_we_have_ptr();
enum
{
  PROBE_DONE_EVENT,
};

//#define DEBUG 1

#if !DEBUG
#undef DBG_PRINTF
#define DBG_PRINTF(a,...)
#endif

/** \internal The DNS message header. */
struct dns_hdr
{
  u16_t id;
  u8_t flags1, flags2;
  u16_t numquestions;
  u16_t numanswers;
  u16_t numauthrr;
  u16_t numextrarr;
};

typedef struct rr_record_hdr
{
  uint16_t type, class;
  uint32_t ttl;
  uint16_t rlength;
} rr_record_hdr_t;


typedef struct {
  uint16_t qtype;
  uint16_t qclass;
} query_hdr_t;

typedef struct srv_record
{
  uint16_t priority;
  uint16_t weight;
  uint16_t port;
} srv_record_t;

#define DNS_FLAG1_RESPONSE        0x80
#define DNS_FLAG1_OPCODE_STATUS   0x10
#define DNS_FLAG1_OPCODE_INVERSE  0x08
#define DNS_FLAG1_OPCODE_STANDARD 0x00
#define DNS_FLAG1_AUTHORATIVE     0x04
#define DNS_FLAG1_TRUNC           0x02
#define DNS_FLAG1_RD              0x01
#define DNS_FLAG2_RA              0x80
#define DNS_FLAG2_ERR_MASK        0x0f
#define DNS_FLAG2_ERR_NONE        0x00
#define DNS_FLAG2_ERR_NAME        0x03

const char* rr_type_to_string[] =
{ "", "A", "NS", "MD", "MF", "CNAME", "SOA", "MB", "MG", "MR", "NULL", "WKS", "PTR", "HINFO", "MINFO", "MX", "TXT" };
#define RR_TYPE_A               1  //a host address
#define RR_TYPE_NS              2 //an authoritative name server
#define RR_TYPE_MD              3 //a mail destination (Obsolete - use MX)
#define RR_TYPE_MF              4 //a mail forwarder (Obsolete - use MX)
#define RR_TYPE_CNAME           5 //the canonical name for an alias
#define RR_TYPE_SOA             6 //marks the start of a zone of authority
#define RR_TYPE_MB              7 //a mailbox domain name (EXPERIMENTAL)
#define RR_TYPE_MG              8 //a mail group member (EXPERIMENTAL)
#define RR_TYPE_MR              9 //a mail rename domain name (EXPERIMENTAL)
#define RR_TYPE_NULL            10 //a null RR (EXPERIMENTAL)
#define RR_TYPE_WKS             11 //a well known service description
#define RR_TYPE_PTR             12 //a domain name pointer
#define RR_TYPE_HINFO           13 //host information
#define RR_TYPE_MINFO           14 //mailbox or mail list information
#define RR_TYPE_MX              15 //mail exchange
#define RR_TYPE_TXT             16 //text strings
#define RR_TYPE_AAAA 			28
#define RR_TYPE_SRV 			33
#define RR_TYPE_NSEC      47
#define RR_TYPE_ANY       255

const char* rr_class_to_string[] =
{ "", "IN", "CS", "CH", "HS" };
#define RR_CLASS_IN              1 //the Internet
#define RR_CLASS_CS              2 //the CSNET class (Obsolete - used only for examples in some obsolete RFCs)
#define RR_CLASS_CH              3 //the CHAOS class
#define RR_CLASS_HS              4 //Hesiod [Dyer 87]
#define isPointer(a) (((uint8_t)a & 0xc0) == 0xc0)
/**
 * Z-Wave common domain name
 */
const char zwave_domain_name[] = "\007_z-wave\004_udp\005local";
const char services_domain_name[] = "\011_services\007_dns-sd\004_udp\005local";

#ifdef MDNS_TEST
const uip_ipaddr_t mDNS_mcast_addr;
#define STATIC
#else
#define STATIC static
const uip_ipaddr_t mDNS_mcast_addr  =
{ .u8 =
{ 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xfb } };
#endif

#define FIELD_PTR      0x01
#define FIELD_TXT      0x02
#define FIELD_SRV      0x04
#define FIELD_AAAA     0x08
#define FIELD_A        0x10
#define FIELD_ARPA4    0x20
#define FIELD_ARPA6    0x40


#define SERVICES_ENDPOINT (rd_ep_database_entry_t*) 0x1 //Dummy endpoint used for  _services._dns-sd._udp queries

struct answers
{
  struct answers *list;
  uint8_t fields;
  uint32_t cls;
  //rd_group_entry_t* ge;
  rd_ep_database_entry_t *endpoint;
};

/*
 * This uses a lot of memory.
 * We could consider some special treatment for ptr queries.
 */
LIST(answers);
MEMB(answer_memb, struct answers, 255);

static struct ctimer answer_timer;
static void send_answers(void* data);
static void gen_domain_name_for_ip6(nodeid_t nodeid, char *s);
static void gen_domain_name_for_ip4(nodeid_t nodeid,char* s);

/**
 * Initialize DNS message structure
 */
STATIC void init_dns_message(struct dns_message* msg, char* buffer, int buf_len, uint16_t flags)
{
  msg->hdr = (struct dns_hdr*) buffer;
  memset(msg->hdr, 0, sizeof(struct dns_hdr));
  msg->hdr->flags1 = flags;
  msg->ptr = buffer + sizeof(struct dns_hdr);
  msg->end = buffer + buf_len;
  msg->last_domain_name = 0;
}

/**
 * Return a pointer to end of domain name in a DNS record
 */
static const char* domain_name_end(const char* domain_name)
{
  uint8_t *p;

  p = (uint8_t*) domain_name;
  while (*p)
  {
    if (isPointer(*p))
    {
      return (char*) p + 2;
    } else
    {
      p += *p + 1;
    }
  }
  return (char*) p + 1;
}

int mdns_str_cmp(const char* s1, const char* s2, u8_t len)
{
  u8_t i;
  for (i = 0; i < len; i++)
  {
    if (toupper(s1[i]) != toupper(s1[i]))
    {
      return 0;
    }
  }
  return 1;
}

static int string_right_compare(const char* s1, const char* s2)
{
  int i, j;

  i = strlen(s1);
  j = strlen(s2);

  if (i == 0 || j == 0)
    return 0;

  while (i > 0 && j > 0)
  {
    if (toupper(s1[i - 1]) != toupper(s2[j - 1]))
      return 0;
    i--;
    j--;
  }
  return 1;
}

/**
 * Return a pointer to the next domain label. Follow domain name pointers.
 */
static char* domain_name_next_label(dns_message_t *m, const char* label)
{
  char* p;
  uint8_t len;
  uint16_t offset;

  p = (char*) label;
  len = *p++;
  if (isPointer(len))
  {
    offset = ((len & 0x3f) << 8) | (uint8_t) *p;
    p = (char*) ((char*) m->hdr + offset);
  } else
  {
    p += len;
  }

  return p;
}

/**
 * Return the number of labels in a compressed domain name
 */
static int domian_name_label_count(dns_message_t *m, const char* compressed_domain_name)
{
  const char* p;
  uint8_t n;
  n = 0;
  for (p = compressed_domain_name; *p; p = domain_name_next_label(m, p))
  {
    if (((uint8_t) *p) <= MAX_LABEL_SIZE)
    {
      n++;
    }
  }
  return n;
}

/**
 * Get the n-th label of a domain name
 */
static char* domain_name_label_get(dns_message_t *m, const char* compressed_domain_name, int n)
{
  const char* p;
  for (p = compressed_domain_name; *p; p = domain_name_next_label(m, p))
  {
    if (((uint8_t) *p) <= MAX_LABEL_SIZE)
    {
      if (n == 0)
        return (char*) p;
      n--;
    }
  }
  return 0;
}

/**
 * Convert a single domain label to a zero terminated string.
 * return a pointer to the beginning of the string
 */
static char* domain_label_to_string(const char* label, char* buf)
{
  uint8_t len;
  len = *label;
  memcpy(buf, label + 1, len);
  buf[len] = 0;
  return buf;
}

/**
 * Convert an entire domain name to a zero terminated string. Follow label pointers if
 * needed.
 * \return a pointer to the string.
 */
static char* domain_name_to_string(dns_message_t *m, const char* compressed_domain_name)
{
  static char buf[MAX_DOMAIN_SIZE];
  const char* p;
  char *s = buf;
  for (p = compressed_domain_name; *p; p = domain_name_next_label(m, p))
  {
    if (((uint8_t) *p) <= MAX_LABEL_SIZE)
    {
      memcpy(s, p + 1, (uint8_t) *p);
      s += (uint8_t) *p;
      *s++ = '.';
    }
  }
  *s++ = 0;
  return buf;
}

/* Copy and decompress a domain name */
static uint8_t domain_name_copy(dns_message_t *m, const char* compressed_domain_name, char* buf, uint16_t maxlen)
{
  const char* p;
  char *s = buf;
  ASSERT(buf);
  uint16_t bytes_generated = 0;
  uint16_t len;
  for (p = compressed_domain_name; *p; p = domain_name_next_label(m, p))
  {
    if (!isPointer(*p))
    {
      len = *p + 1;
      if (bytes_generated + len > maxlen - 1)
      {
        assert(0);
        break;
      }
      memcpy(s, p, (uint8_t) len);
      bytes_generated += len;
      s += len;
    }
  }
  *s++ = 0;

  return bytes_generated + 1;
}

/**
 * Check if two domain labels are identical
 */
static int domain_name_label_compare(dns_message_t *m, const char* a, const char* b)
{
  if (isPointer(*a))
    a = domain_name_next_label(m, a);
  if (isPointer(*a))
    b = domain_name_next_label(m, b);

  return (*a) == (*b) && (strncmp(a + 1, b + 1, (uint8_t) *a) == 0);
}

/**
 * attempt to insert a pointer into dest
 * 
 * if pointer is inserted, returns dest address after where pointer is inserted
 * else returns 0
 */
static char* domain_name_insert_ptr(dns_message_t *m, char *dest, const char* src)
{
  int i, j, n;
  char *a = 0, *b = 0;
  uint16_t offset;
	char buf[256];

  i = domian_name_label_count(m, dest);
  j = domian_name_label_count(m, src);
//    	printf("%i %i\n",i,j);
//  	printf("dest=%s\n", domain_name_to_string(m, dest));
//  	printf("src=%s\n", domain_name_to_string(m, src));

  n = 0;
  for (; j > 0 && i > 0; j--, i--, n++)
  {
    a = domain_name_label_get(m, dest, i - 1);
    b = domain_name_label_get(m, src, j - 1);
    if (!domain_name_label_compare(m, a, b))
    {
      a = (char*) domain_name_next_label(m, a);
      b = (char*) domain_name_next_label(m, b);
      break;
    }
  }

//	printf("%i in common %i %i\n",n,i,j);

  if (n == 0)
    return 0;

  /*Exhaust pointers */
  while (isPointer(*b))
  {
    b = domain_name_next_label(m, b);
  }
//	printf("a=%s\n",domain_name_to_string(m,a));
//	printf("b=%s\n",domain_name_to_string(m,b));

  offset = b - (char*) m->hdr;
  *a++ = ((offset >> 8) & 0x3F) | 0xc0;
  *a++ = (offset & 0xFF);
  return a;
}

/**
 * Add domain name to end of message. Use compression if possible
 * Return the length of the name after compression
 */
STATIC uint16_t add_domain_name_compressed(dns_message_t* m, const char* domain_name)
{
  uint16_t len;

  char* start;

  start = m->ptr;
  len = domain_name_copy(m, domain_name, m->ptr, MAX_DOMAIN_SIZE);

  if (m->last_domain_name)
  {
    char* p;
    p = domain_name_insert_ptr(m, m->ptr, m->last_domain_name);
    if (p) // if pointer is inserted
    {
      assert(m->last_domain_name < m->ptr);
      m->last_domain_name = m->ptr; /* we store domain lable of this interation as last_domain_name, remember it can have pointers too */
      m->ptr = p; // p is now end of original domain label with pointers inserted 
      return p - start; // return the pointered domain name len 
    }
  }
  m->last_domain_name = m->ptr;
  m->ptr += len;
  return len;
}
/**
 * Append a zero terminated string to the domian name and return a pointer
 * to the end of the string
 */
static char* domain_name_append(const char* zstring, char* domain_name)
{
  uint8_t len;

  if (zstring == 0)
    return domain_name;
  len = strlen(zstring);
  if (len == 0)
    return domain_name;

  domain_name[0] = len;
  memcpy(domain_name + 1, zstring, len);
  domain_name[1 + len] = 0;
  return domain_name + 1 + len;
}

/**
 * Add a RR record to a DNS message.
 * \param m DNS message to add this PTR answer to
 * \param domain_name The name which this RR record is a response to in.
 * \param type
 * \param class
 * \param ttl
 * \param rlength length of the data in rdata
 * \param rrdata data to write to the RR record
 * \return True if record was added
 */
static rr_record_hdr_t* add_rrecord_hdr(dns_message_t* m, const char* query_name, uint16_t type, uint16_t class,
    uint32_t ttl)
{
  rr_record_hdr_t* rr_fields_p;

  
  DBG_PRINTF("Service_name %s\n", query_name+1);
  add_domain_name_compressed(m, query_name);

  rr_fields_p = (rr_record_hdr_t*) m->ptr;
  rr_fields_p->type = uip_htons(type);
  //rr_fields_p->class = uip_htons(class | 0x8000); // 0x8000 is the cache flush flag
  rr_fields_p->class = uip_htons(class); // 0x8000 is the cache flush flag
  rr_fields_p->ttl = uip_htonl(ttl);
  m->ptr += 10;
  return rr_fields_p;
}

/**
 * Add a query record to a DNS message.
 * \param m DNS message to add this PTR answer to
 * \param domain_name The name for this Query
 * \param qtype
 * \param qclass
 * \return pointer to the header just added
 */
static query_hdr_t* add_qrecord_hdr(dns_message_t* m, const char* query_name, uint16_t qtype, uint16_t qclass)
{
  query_hdr_t* rr_fields_p;

  add_domain_name_compressed(m, query_name);

  rr_fields_p = (query_hdr_t*) m->ptr;
  rr_fields_p->qtype = uip_htons(qtype);
  rr_fields_p->qclass = uip_htons(qclass); // 0x8000 is the cache flush flag
  m->ptr += 4;
  return rr_fields_p;
}

/**
 * Return the dns service name of an endpoint
 */
static const char* ep_service_name(rd_ep_database_entry_t* ep)
{
  static char service_name[MAX_DOMAIN_SIZE];
  char *d;
  u8_t l;
  service_name[0] = 0;
  d = service_name;

  if(ep == SERVICES_ENDPOINT) {
    d = domain_name_append("_services", d);
    d = domain_name_append("_dns-sd", d);
  } else {
    l = rd_get_ep_name(ep, d + 1, sizeof(service_name) - 6);
    if (ep->endpoint_loc_len)
    {
      d[1 + l++] = '.';
      memcpy(d +1 + l, ep->endpoint_location, ep->endpoint_loc_len);
      l += ep->endpoint_loc_len;
    }

    *d = l;
    d += (*d) + 1;
    d = domain_name_append("_z-wave", d);
  }

  d = domain_name_append("_udp", d);
  d = domain_name_append("local", d);

  //printf("service_name: %s\n", service_name);
  return service_name;
}

static rd_ep_database_entry_t* lookup_by_service_name(const char* name)
{
  rd_ep_database_entry_t* ep;

  for (ep = rd_ep_first(0); ep; ep = rd_ep_next(0, ep))
  {
//    ERR_PRINTF("comparing ep_service_name(ep): %s, name: %s\n", ep_service_name(ep), name);
    if (strcasecmp(ep_service_name(ep), name)==0)
    {
      return ep;
    }
  }
  return 0;
}

/**
 * Add a nodes PTR record to a DNS response.
 * \param node corresponding node.
 * \param domain_name The name which this PTR record is a response to in.
 * \param m DNS message to add this PTR answer to
 * \return True if record was added
 */
static int add_ptr_for_node(rd_ep_database_entry_t* ep, dns_message_t* m, u32_t cls)
{
  uint16_t len;
  rr_record_hdr_t *h;
  uint8_t ttl;
  char buf[64];
  uint8_t sz;
  /*
   * WARNING! Do not set the cache flush flag on the PTR record. The resource "_z-wave._udp.local"
   * is a shared resource.
   */
 // DBG_PRINTF("Adding PTR record TARGET %s (node = %d %d %d)\n",
 //     domain_name_to_string(0,ep_service_name(ep)), ep->node->nodeid, ep->endpoint_name_len, ep->endpoint_info_len);

  ttl = (ep->node->mode & MODE_FLAGS_DELETED) ? 0 : 120;
  if (cls)
  {
    if (cls & 0x00EF0000)
    {
      sz = sprintf(buf + 1, "_ef%02x", cls & 0xFFFF);
    } else
    {
      sz = sprintf(buf + 1, "_%02x", cls);
    }
    buf[0] = sz;
    sprintf(buf + sz + 1, "\004_sub%s", zwave_domain_name);
    h = add_rrecord_hdr(m, buf, RR_TYPE_PTR, RR_CLASS_IN, ttl);
  } else
  {
    h = add_rrecord_hdr(m, zwave_domain_name, RR_TYPE_PTR, RR_CLASS_IN, ttl);
  }
  len = add_domain_name_compressed(m, ep_service_name(ep));
  h->rlength = uip_htons(len);
  return 1;
}


 static int add_ptr_for_service(dns_message_t* m) {
 uint16_t len;
 rr_record_hdr_t *h;

 h = add_rrecord_hdr(m, services_domain_name, RR_TYPE_PTR, RR_CLASS_IN, 120);
 len = add_domain_name_compressed(m,zwave_domain_name);
 h->rlength = uip_htons(len);
 return 1;
}


#if 0
static int add_nsec_for_node(rd_ep_database_entry_t* ep, const char* query_name, dns_message_t* m)
{
  char node_domain_name[MAX_DOMAIN_SIZE];
  char *d;
  uint16_t len;
  rr_record_hdr_t *h;

  DBG_PRINTF("Adding NSEC record TARGET %s \n", ep->endpoint_name);

  node_domain_name[0] = 0;
  d = node_domain_name;
  d = domain_name_append(ep->endpoint_name,d);
  d = domain_name_append(ep->endpoint_location,d);
  d = domain_name_append("_z-wave",d);
  d = domain_name_append("_udp",d);
  d = domain_name_append("local",d);

  h = add_rrecord_hdr(m, query_name, RR_TYPE_NSEC, RR_CLASS_IN, 120);
  len = add_domain_name_compressed(m,node_domain_name);

  m->ptr = (char*)h + len + sizeof(rr_record_hdr_t);

  /*Bit field but endian swapped!! */
  *m->ptr++ = 0; //Block 0
  *m->ptr++ = 5;// Bitmap length
  *m->ptr++ = 0;//Offset 1-8
  *m->ptr++ = 0x08;//Offset 9-16 RR_TYPE_PTR=12 RR_TYPE_TXT=16
  *m->ptr++ = 0x80;//Offset 17-24
  *m->ptr++ = 0;//Offset 25-32
  *m->ptr++ = 0x40;//Offset 33      RR_TYPE_SRV=33

  h->rlength = uip_htons(len+7);
  return 1;
}
#endif

static rd_node_database_entry_t* lookup_by_node_name(const char* node_name)
{
  char name[64];
  uint8_t len;

  len = node_name[0];
  if(len >=sizeof(name)) {
    assert(0);
    return 0;
  }

  memcpy(name, node_name + 1, len);
  name[len] = 0;

  return rd_lookup_by_node_name(name);
}

/**
 * Add a nodes SRV record to a DNS response.
 * \param node corresponding node.
 * \param domain_name The name which this PTR record is a response to in.
 * \param m DNS message to add this PTR answer to
 * \return True if record was added
 */
static int add_srv_for_node(rd_ep_database_entry_t* ep, dns_message_t* m)
{
  char lqn[MAX_DOMAIN_SIZE], *s;
  rr_record_hdr_t *h;
  srv_record_t* srv;
  uint16_t len;
  u32_t ttl;
  s = lqn;

  *s = rd_get_node_name(ep->node, s + 1, MAX_DOMAIN_SIZE - 1 - 6);
  s += (*s) + 1;
  s = domain_name_append("local", s);

  ttl = (ep->node->mode & MODE_FLAGS_DELETED) ? 0 : 120;

  //DBG_PRINTF("Adding SRV target %s ttl = %i\n", domain_name_to_string(0,lqn), ttl);

  if (dont_set_cache_flush_bit) {
        h = add_rrecord_hdr(m, ep_service_name(ep), RR_TYPE_SRV, RR_CLASS_IN, ttl);
  }else {
        h = add_rrecord_hdr(m, ep_service_name(ep), RR_TYPE_SRV, 0x8000 | RR_CLASS_IN, ttl);
  }
  srv = (srv_record_t*) m->ptr;
  srv->port = UIP_HTONS(ZWAVE_PORT);
  srv->priority = UIP_HTONS(MDNS_SRV_RECORD_DEFAULT_PRIORITY);
  srv->weight = UIP_HTONS(MDNS_SRV_RECORD_DEFAULT_WEIGHT);
  m->ptr += 6;
  len = add_domain_name_compressed(m, lqn) + 6;
  h->rlength = uip_htons(len);
  return 1;
}

static void txt_add_binary(dns_message_t *m, const char* key, const char*data, int len)
{
  uint8_t i, j;

  i = strlen(key);
  j = len;
  *m->ptr++ = (i + j) + 1;

  assert(m->ptr < m->end);

  memcpy(m->ptr, key, i);
  m->ptr += i;
  *m->ptr++ = '=';
  memcpy(m->ptr, data, j);
  m->ptr += j;
}

static void txt_add_pair(dns_message_t *m, const char* key, const char*value)
{
  txt_add_binary(m, key, value, strlen(value));
}

/*Create extended nif with new exteded classes for Meter,SensorMultilevel, Sensor Alarm and Alarm*/
static u8_t gen_extnif(rd_ep_database_entry_t* ep, u8_t* enif, u8_t size)
{
  u8_t *p, *q;

  p = enif;
  q = ep->endpoint_info;

  if ((ep->endpoint_info_len+1) > size || ep->endpoint_info_len < 2)
  {
    return 0;
  }

  *p++ = *q++; //Generic specific
  *p++ = *q++;

  if(ep->endpoint_id>0) {
    *p++ = COMMAND_CLASS_ZIP_NAMING;
  }

  while (q < ep->endpoint_info + ep->endpoint_info_len)
  {
    if ((*q & 0XF0) == 0xF0)
    {
      /*Extended command class*/
      *p++ = *q++;
      *p++ = *q++;
    } else
    {
      switch (*q)
      {
      case COMMAND_CLASS_METER:
      case COMMAND_CLASS_SENSOR_MULTILEVEL:
      case COMMAND_CLASS_SENSOR_ALARM:
      case COMMAND_CLASS_ALARM:
        *p++ = *q++;
        *p++ = 0;
        break;
        /*Hide these command classes */
      case COMMAND_CLASS_TRANSPORT_SERVICE:
      case COMMAND_CLASS_SECURITY:
      case COMMAND_CLASS_MULTI_INSTANCE:
        q++;
        break;
      case COMMAND_CLASS_ASSOCIATION:
        /* Association support is translated to IP Association
         * for LAN hosts. */
        *p++ = COMMAND_CLASS_IP_ASSOCIATION;
        *p++ = *q++;
        break;
      default:
        *p++ = *q++;
        break;
      }
    }
  }

  if (ep->endpoint_info_len)
  {
    return (p - enif);
  } else
  {
    return 0;
  }
}
/**
 * Add a nodes TXT record to a DNS response.
 * \param m DNS message to add this PTR answer to
 * \return True if record was added
 */
static int add_txt_for_node(rd_ep_database_entry_t* ep, dns_message_t* m)
{
  rr_record_hdr_t *h;
  char* txt;
  u16_t mode;
  u32_t icon;
  u8_t exnif[64];
  u8_t exnif_len;
  u32_t ttl;
  u8_t sec_classes;
  exnif_len = gen_extnif(ep, exnif, sizeof(exnif));
  assert(ep && ep->node);
  assert(m->ptr);

  ttl = (ep->node->mode & MODE_FLAGS_DELETED) ? 0 : 120;
  //DBG_PRINTF("Adding %s TXT record ttl =%i\n", domain_name_to_string(0,ep_service_name(ep)), ttl);

  if (dont_set_cache_flush_bit) {
        h = add_rrecord_hdr(m, ep_service_name(ep), RR_TYPE_TXT, RR_CLASS_IN, ttl);
  }else {
        h = add_rrecord_hdr(m, ep_service_name(ep), RR_TYPE_TXT, 0x8000 | RR_CLASS_IN, ttl);
  }

  txt = (char*) m->ptr;
  txt_add_binary(m, "info", (char*) exnif, exnif_len);
  txt_add_binary(m, "epid", (char*) &(ep->endpoint_id), 1);

  mode = uip_htons((u16_t)(ep->node->mode));
  txt_add_binary(m, "mode", (char*) &(mode), sizeof(mode));

  //productID is the concatenation of all 3 fields: manufacturerID, productType, productID
  u16_t productID[3];
  productID[0] = uip_htons(ep->node->manufacturerID);
  productID[1] = uip_htons(ep->node->productType);
  productID[2] = uip_htons(ep->node->productID);
  txt_add_binary(m, "productID", (char*) &(productID), 6);

  if(ep->endpoint_aggr_len) {
    txt_add_binary(m,"aggregated", (char*)ep->endpoint_agg, ep->endpoint_aggr_len);
  }

  sec_classes = sec2_gw_node_flags2keystore_flags(GetCacheEntryFlag(ep->node->nodeid));
  txt_add_binary(m, "securityClasses", (char*) &sec_classes, 1);

  txt_add_pair(m, "txtvers", "1");

  icon = uip_htonl((u32_t)(((u32_t)(ep->installer_iconID)<<16) | ep->user_iconID));
  txt_add_binary(m, "icon", (char*)&icon, sizeof(icon));

 /* txt_add_pair(m, "product", "Implement Me");
  txt_add_pair(m, "productURL", "http://www.google.com");*/


  /**m->ptr = 0;
   m->ptr++;*/
  h->rlength = uip_htons(m->ptr-txt);

  return 1;
}

/**
 * Add a nodes AAAA record to a DNS response.
 * \param node corresponding node.
 * \param domain_name The name which this PTR record is a response to in.
 * \param m DNS message to add this PTR answer to
 * \return True if record was added
 */
static int add_aaaa_for_node(rd_node_database_entry_t* node, dns_message_t* m)
{
  rr_record_hdr_t *h;
  char lqn[MAX_DOMAIN_SIZE], *s;
  s = lqn;

  *s = rd_get_node_name(node, s + 1, MAX_DOMAIN_SIZE - 1 - 6);
  s += (*s) + 1;
  s = domain_name_append("local", s);

  //DBG_PRINTF("Adding AAAA record for %s\n", domain_name_to_string(0,lqn));

  if((tie_braking_won == 1) || !dont_set_cache_flush_bit) {
      h = add_rrecord_hdr(m, lqn, RR_TYPE_AAAA, 0x8000 | RR_CLASS_IN, 120);
  } else {
      h = add_rrecord_hdr(m, lqn, RR_TYPE_AAAA, RR_CLASS_IN, 120);
  }
  h->rlength = UIP_HTONS(IPV6_LENGTH);
  ipOfNode((uip_ipaddr_t*) m->ptr, node->nodeid);
  m->ptr += 16;

  return 1;
}

static int add_arpa6_for_node(rd_node_database_entry_t* node, dns_message_t* m)
{
  rr_record_hdr_t *h;
  char lqn[MAX_DOMAIN_SIZE], *s;
  u16_t len;
  s = lqn;

  *s = 0;
  gen_domain_name_for_ip6(node->nodeid, s);

  h = add_rrecord_hdr(m, lqn, RR_TYPE_PTR, RR_CLASS_IN, 120);
  s = lqn;
  *s = rd_get_node_name(node, s + 1, MAX_DOMAIN_SIZE - 1 - 6);
  s += (*s) + 1;
  s = domain_name_append("local", s);

  len = add_domain_name_compressed(m, lqn);

  h->rlength = uip_htons(len);

  return 1;
}

static int add_arpa4_for_node(rd_node_database_entry_t* node, dns_message_t* m)
{
  rr_record_hdr_t *h;
  char lqn[MAX_DOMAIN_SIZE], *s;
  u16_t len;
  s = lqn;
  *s = 0;
/*
  ipv4addr_t a; //An IPv4 address is 4 bytes

  ipv46nat_ipv4addr_of_node(&a, node->nodeid);
  for (i = 4; i >= 0; i--)
  {
    snprintf(digit, 1, "%i", a.u8[i]);
    s = domain_name_append(digit, s);
  }
  s = domain_name_append("in-addr", s);
  s = domain_name_append("arpa", s);
*/
  gen_domain_name_for_ip4(node->nodeid, s);
  h = add_rrecord_hdr(m, lqn, RR_TYPE_PTR, RR_CLASS_IN, 120);
  s = lqn;
  *s = rd_get_node_name(node, s + 1, MAX_DOMAIN_SIZE - 1 - 6);
  s += (*s) + 1;
  s = domain_name_append("local", s);
  len = add_domain_name_compressed(m, lqn);

  h->rlength = uip_htons(len);

  return 1;
}

/**
 * Add a nodes A record to a DNS response.
 * \param node corresponding node.
 * \param domain_name The name which this PTR record is a response to in.
 * \param m DNS message to add this PTR answer to
 * \return True if record was added
 */
static int add_a_for_node(rd_node_database_entry_t* node, dns_message_t* m)
{
  rr_record_hdr_t *h;
  char lqn[MAX_DOMAIN_SIZE], *s;
  uip_ipv4addr_t a; //An IPv4 address is 4 bytes
  s = lqn;

  if (ipv46nat_ipv4addr_of_node(&a, node->nodeid))
  {

    *s = rd_get_node_name(node, s + 1, MAX_DOMAIN_SIZE - 1 - 6);
    s += (*s) + 1;
    s = domain_name_append("local", s);
    DBG_PRINTF("Adding A record for %s\n", domain_name_to_string(0,lqn));

    if((tie_braking_won == 1) || !dont_set_cache_flush_bit) {
        h = add_rrecord_hdr(m, lqn, RR_TYPE_A, 0x8000| RR_CLASS_IN, 120);
    } else {
        h = add_rrecord_hdr(m, lqn, RR_TYPE_A, RR_CLASS_IN, 120);
    }
    h->rlength = UIP_HTONS(4);
    memcpy(m->ptr, &a, 4);
    m->ptr += 4;
    return 1;
  } else
  {
    return 0;
  }
}


/**
 * Send a dns message to dest on port.
 * port should be in machine byte order.
 * (host order)
 */
static void send_dns_message(dns_message_t *m, const uip_ipaddr_t *dest, uint16_t port)
{
  uint16_t len;
  uip_ipaddr_t ip4a;

  if (m->hdr->numanswers == 0 && m->hdr->numquestions == 0 && m->hdr->numextrarr == 0)
    return;
  len = m->ptr - (char*) m->hdr;

  /*
   * Fix endian
   */
  m->hdr->numanswers = uip_htons(m->hdr->numanswers);
  m->hdr->numauthrr = uip_htons(m->hdr->numauthrr);
  m->hdr->numquestions = uip_htons(m->hdr->numquestions);
  m->hdr->numextrarr = uip_htons(m->hdr->numextrarr);

  //LOG_PRINTF("mDNS Z-WAVE reply len=%i\n", len);

#ifdef MDNS_TEST
  //mdns_hexdump((char*)m->hdr, len);
  memcpy(output_buf, (char *) m->hdr, len);
  output_len = len;
  return;
#else
  
  if (server_conn)
  {
    /* multicast responses should also be sent in a IPv4 version*/
    if (uip_is_addr_mcast(dest))
    {
      ip4a.u16[0] = 0;
      ip4a.u16[1] = 0;
      ip4a.u16[2] = 0;
      ip4a.u16[3] = 0;
      ip4a.u16[4] = 0;
      ip4a.u16[5] = 0xFFFF;
      ip4a.u16[6] = UIP_HTONS(0xe000); //224.0
      ip4a.u16[7] = dest->u16[7];
      //DBG_PRINTF("Sending IPv4 packet as well\n");
      uip_udp_packet_sendto(server_conn, (char*) m->hdr, len, &ip4a, uip_htons(port));
    }
    uip_udp_packet_sendto(server_conn, (char*) m->hdr, len, dest, uip_htons(port));
  } else
  {
    ERR_PRINTF("mDNS socket not ready\n");
  }
#endif
}

/*
 * Add an answer to the list of answers
 */
static void add_answer(rd_ep_database_entry_t* ep, rd_group_entry_t* ge, uint8_t field, u32_t cls)
{
  struct answers* a;

  /* Check is we already have an answer for this endpoint. */
  for (a = list_head(answers); a != NULL; a = list_item_next(a))
  {
    //if(a->endpoint == ep && a->ge == ge) break;
    if (a->endpoint == ep)
      break;
  }

  if (a == NULL)
  {
    a = memb_alloc(&answer_memb);
    if (!a)
    {
      goto drop_answer;
    }

    a->fields = 0;
    //a->ge = ge;
    a->endpoint = ep;
    a->cls = cls;
    list_add(answers, a);
  }

  a->fields |= field;

  return;
  drop_answer:
  ERR_PRINTF("Dropping answer due to lack of resources. %s\n", ep->endpoint_name);
  return;
}

static int del_answer(rd_ep_database_entry_t* ep, uint8_t field)
{
  struct answers* a;

  for (a = list_head(answers); a != NULL; a = list_item_next(a))
  {
    if (a->endpoint == ep)
      break;
  }

  if (a == NULL)
  {
    return 0;
  }

  a->fields &= ~field;
  if (a->fields == 0)
  {
    list_remove(answers, a);
    memb_free(&answer_memb, a);
  }
  return 1;
}

#if 0
typedef char* dns_str_t;

static uint8_t dns_str_len(const dns_str_t dst)
{
  return *((uint8_t*)dst) +1;
}

/*Copy a domain label to  */
static const dns_str_t dns_str_cpy(const dns_str_t dst,dns_str_t src)
{
  uint8_t l;
  l = dns_str_len(src);
  memcpy(dst,src,l);
  dst[l] = 0;
  return dst;
}

static const dns_str_t dns_domain_copy(const dns_str_t dst,dns_str_t src)
{
  dns_str_t s,d;
  s = src;
  d = dst;

  while(dns_str_len(s)>1)
  {
    dns_str_cpy(d,s);
    s+= dns_str_len(s);
    d+= dns_str_len(d);
  }
  return dst;
}

static int mdns_str_cmp(const dns_str_t a,const dns_str_t b)
{
  return (memcmp(a,b,dns_str_len(a)) ==0);
}

static int dns_domain_cmp(const dns_str_t a,const dns_str_t b)
{

  while((dns_str_len(a) > 1) && (dns_str_len(b)) > 1)
  {
    if(!mdns_str_cmp(a,b))
    {
      return 0;
    }
  }
  return (dns_str_len(a) == dns_str_len(b));
}

/**
 * Parse an uncompressed dns query string. on the form
 * _<group>._sub._<service type>.local or
 * <service name>._<service type>.local
 *
 * The this function copies the three parts into  service_name, group_name and service_type
 * TODO Maybe it would be better to just return pointers instead to copying?
 *
 */
static int parse_query_string(const dns_str_t query, dns_str_t service_name, dns_str_t group_name, dns_str_t service_type )
{
  dns_str_t q,n;

  *group_name =0;
  *service_type=0;
  *service_name=0;
  if(query[1]!='_')
  {
    q = query;
    dns_str_cpy(service_name,q);
    q += dns_str_len(q);
  }

  /*DBG_PRINTF("STR len %i\n",dns_str_len(service_name));*/
  n = service_type;
  while(dns_str_len(q)> 1)
  {
    //DBG_PRINTF("LABEL %s\n",domain_name_to_string(0,q));
    if(mdns_str_cmp(q,"\004_sub"))
    {
      dns_domain_copy(group_name,service_type);
      if(dns_str_len(group_name) == 1)
      {
        ERR_PRINTF("dns String parse error\n");
        return 0;
      }
      n = service_type;
      *n=0;
      q+= dns_str_len(q);
    } else if(mdns_str_cmp(q,"\005local"))
    {
      return 1;
    } else
    {
      dns_str_cpy(n,q);

      n+= dns_str_len(n);
      q+= dns_str_len(q);
    }
  }
  return 0;
}

#endif
/**
 * Build or delete and answer to a query.
 *
 *
 */
static void build_or_del_ptr_answer(const char* query, int op)
{
  char buf[MAX_LABEL_SIZE];
  char group_name[256];
  char *p;
  const char* label;
  uint32_t cls = 0;
  uint8_t field;
  rd_group_entry_t* ge = 0;
  u8_t supported; //Is the search for a supported or a controlled command class

  /* First check for class filter */
  label = query;

  domain_label_to_string(label, buf);
  //DBG_PRINTF("buf: %s\n", buf);
  if (*buf == '_')
  {
    if (buf[1] == 'e' && buf[2] == 'f')
    {
      cls = strtol(buf + 3, &p, 16);
      supported = 0;
    } else
    {
      cls = strtol(buf + 1, &p, 16);
      supported = 1;
    }
    if (*p == 0 && cls > 0)
    {
      label = domain_name_next_label(0, label);
    }
  } else
  {
    //DBG_PRINTF("here 1\n");
    goto error;
  }

  /* Now compile group name */
  *group_name = 0;
  while (*label)
  {
    domain_label_to_string(label, buf);
    //DBG_PRINTF("buf: %s\n", buf);
    if (strcasecmp(buf, "_sub") == 0)
      break;
    if (strcasecmp(buf, "_z-wave") == 0)
      break;
    if (*buf == '_')
    {
      strcat(group_name, buf + 1);
    } else
    {
    //DBG_PRINTF("here 2\n");
      goto error;
    }
    label = domain_name_next_label(0, label);
  }

  if (*group_name)
  {
    ge = rd_lookup_group_by_name(group_name);
    if (ge == 0)
    {
      //DBG_PRINTF("Unknown group %s\n", buf + 1);
      return;
    }
  }

  /*
   * foreach node_ep
   *   if has_class && has_grop
   *     add_ptr
   *
   * */

  {
    rd_ep_database_entry_t* ep;
    /*Loop over all endpoints */

    if (ge)
    {
      ep = rd_ep_iterator_group_begin(ge);
    } else
    {
      ep = rd_ep_first(RD_ALL_NODES);
    }

    while (ep)
    {
      if (ep->state == EP_STATE_PROBE_DONE || ep->state == EP_STATE_PROBE_FAIL)
      {
        if (cls == 0)
        {
          field = FIELD_PTR| FIELD_TXT | FIELD_SRV |FIELD_A|FIELD_AAAA;
          if (op) {
              add_answer(ep, ge, field, 0);
          } else {
              del_answer(ep, field);
          }
        } else
        {
          /* Make Classic nodes supporting ASSOCIATION respond to IP_ASSOCIATION queries as well */
          uint32_t lookup_cls = (cls == COMMAND_CLASS_IP_ASSOCIATION ? COMMAND_CLASS_ASSOCIATION : cls);
          if (supported && (rd_ep_class_support(ep, lookup_cls) & SUPPORTED))
          {
             if (op) {
                 add_answer(ep, ge, FIELD_PTR, cls);
             } else {
                 del_answer(ep, field);
             }
          } else if (rd_ep_class_support(ep, cls) & CONTROLLED)
          {
             if (op) {
                 add_answer(ep, ge, FIELD_PTR, cls | 0x00FE0000);
             } else {
                 del_answer(ep, field);
             }
          }
        }
      }

      if (ge)
      {
        ep = rd_group_ep_iterator_next(ge, ep);
      } else
      {
        ep = rd_ep_next(RD_ALL_NODES, ep);
      }
    }
  }

  return;
  error:
  ERR_PRINTF("Parse error in mDNS query\n");
}

void mdns_endpoint_notify(rd_ep_database_entry_t* ep, u8_t express)
{
  if (express)
  {
    dns_message_t answer_m;

    init_dns_message(&answer_m, reply, sizeof(reply), DNS_FLAG1_RESPONSE | DNS_FLAG1_AUTHORATIVE);
    add_ptr_for_node(ep, &answer_m, 0);
    add_txt_for_node(ep, &answer_m);
    //DBG_PRINTF("mdns_endpoint_notify\n");
    add_srv_for_node(ep, &answer_m);
    answer_m.hdr->numanswers = 3;
    send_dns_message(&answer_m, &mDNS_mcast_addr, MDNS_PORT);
  } else
  {
    if (ep->state == EP_STATE_PROBE_DONE || ep->state == EP_STATE_PROBE_FAIL)
    {
      //add_answer(ep,0,bTxt ? FIELD_TXT : FIELD_PTR,0);
      add_answer(ep, 0, FIELD_PTR, 0);
      add_answer(ep, 0, FIELD_TXT, 0);
      add_answer(ep, 0, FIELD_SRV, 0);
      add_answer(ep, 0, FIELD_A, 0);
      add_answer(ep, 0, FIELD_AAAA, 0);
      ctimer_set(&answer_timer, 20, send_answers, 0);
    }
  }
}

/**
 * Algorithm for mDNS service:
 *
 * query_in:
 *   foreach query
 *     add_responses
 *
 *   foreach known_answers
 *     remove_responses
 *
 * wait 20ms - 120ms
 *
 * foreach response:
 *   if(last_transmit_time < 1 sec ):
 *      add_to_pkt_buffer
 *
 * send.
 */

rd_ep_database_entry_t* query_to_endpoint(const char* query_name, uint16_t qtype, uint8_t *field)
{
  rd_ep_database_entry_t* ep;
  rd_node_database_entry_t* node;

  ep = 0;
  *field = 0;
  if (string_right_compare(query_name, zwave_domain_name))
  {
    ep = lookup_by_service_name(query_name);

    if (ep == 0) {
      DBG_PRINTF("Service not found %s\n",domain_name_to_string(0,query_name));
      return 0;
    }

    switch (qtype)
    {
    case RR_TYPE_PTR:
      /* Unused */
      break;
    case RR_TYPE_SRV:
      *field = FIELD_SRV | FIELD_A |FIELD_AAAA;
      break;
    case RR_TYPE_TXT:
      *field = FIELD_TXT;
      break;
    case RR_TYPE_ANY:
      *field = FIELD_TXT | FIELD_SRV;
      break;
    case RR_TYPE_CNAME:
      DBG_PRINTF("CNAME query");
      break;
    }
  } else if (string_right_compare(query_name, services_domain_name))
  {
    DBG_PRINTF("Service query\n");
    ep = SERVICES_ENDPOINT;
    *field = 0;
  } else if (string_right_compare(query_name, "\005local"))
  {
    node = lookup_by_node_name(query_name);
    if (node)
    {
      ep = list_head(node->endpoints);
      assert(ep->node == node);

      switch (qtype)
      {
      case RR_TYPE_ANY:
        *field = FIELD_AAAA | FIELD_A;
        break;
      case RR_TYPE_A:
        *field = FIELD_A;
        break;
      case RR_TYPE_AAAA:
        *field = FIELD_AAAA;
        break;
      }
    }

  }
  return ep;
}


void mdns_remove_from_answers(rd_ep_database_entry_t* ep) {
  struct answers* a;

  for(a=list_head(answers); a; a = list_item_next(a)) {
    if(a->endpoint == ep) {
      break;
    }
  }
  if(a) {
    list_remove(answers, a);
    memb_free(&answer_memb, a);
  }
}

static void send_answers(void* data)
{
  int j;
  int n, mask,rc;
  dns_message_t answer_m;
  struct answers* a, *aa;
  char* ptr_save;
  int latency = 0;

  /*prepare to answer */
  init_dns_message(&answer_m, reply, sizeof(reply), DNS_FLAG1_RESPONSE | DNS_FLAG1_AUTHORATIVE);

  /* In multicast responses, including unsolicited multicast responses,
   * the Query Identifier MUST be set to zero on transmission, and MUST be
   * ignored on reception.*/
  answer_m.hdr->id = 0;

  /* Now add the rest */
  a = list_head(answers);
  while (a != 0)
  {
    n = 0;
    mask = 0;
    ptr_save = answer_m.ptr;

    if(a->endpoint == SERVICES_ENDPOINT) {
      add_ptr_for_service(&answer_m);
      n++;
    }
    else
    {
      for (j = 1; j < 0x80; j = j << 1)
      {
        if (a->fields & j)
        {
          switch (j)
          {
          case FIELD_PTR:
            rc =add_ptr_for_node(a->endpoint, &answer_m, a->cls);
            break;
          case FIELD_SRV:
            rc =add_srv_for_node(a->endpoint, &answer_m);
            break;
          case FIELD_TXT:
            rc =add_txt_for_node(a->endpoint, &answer_m);
            break;
          case FIELD_AAAA:
            rc =add_aaaa_for_node(a->endpoint->node, &answer_m);
            break;
          case FIELD_A:
            rc =add_a_for_node(a->endpoint->node, &answer_m);
            break;
          case FIELD_ARPA6:
            rc =add_arpa6_for_node(a->endpoint->node, &answer_m);
            break;
          case FIELD_ARPA4:
            rc =add_arpa4_for_node(a->endpoint->node, &answer_m);
            break;
          }

          if ((answer_m.ptr - (char*) answer_m.hdr) > UIP_UDP_APP_SIZE)
          {
            answer_m.ptr = ptr_save;
            goto send_truncated;
          }

          if(rc) n++;
          mask |= j;
        }
      }

      a->fields &= ~mask;
    }
    answer_m.hdr->numanswers += n;
    a = list_item_next(a);
  }

  a = list_head(answers);
  while (a)
  {
    aa = list_item_next(a);
    if (a->fields == 0)
    {
      list_remove(answers, a);
      memb_free(&answer_memb, a);
    }
    a = aa;
  }

  if (list_head(answers) == 0)
  {
    DBG_PRINTF("------------ Answered all questions ----------------\n");
    if (exiting)
    {
#ifndef MDNS_TEST
      process_exit(&mDNS_server_process);
#endif
    }
  }

  if (answer_m.hdr->numanswers)
  {
    DBG_PRINTF("Sending mDNS answers\n");
    send_dns_message(&answer_m, &mDNS_mcast_addr, MDNS_PORT);
  } else
  {
    DBG_PRINTF("no new answers to send.\n");
  }
  if (dont_set_cache_flush_bit) {
      dont_set_cache_flush_bit = 0;
  }
  return;

  send_truncated:
  DBG_PRINTF("Truncated reply\n");

  assert(answer_m.hdr->numanswers>0);

  /*answer_m.hdr->flags1 |= DNS_FLAG1_TRUNC;*/
  send_dns_message(&answer_m, &mDNS_mcast_addr, MDNS_PORT);
  if(!do_we_have_ptr(answers)) {
      latency = 0;
  } else {
      latency = (rand() & 0x7f) + 20;
  }
  DBG_PRINTF("send_answers in latency:%d ms \n", latency);
  ctimer_set(&answer_timer, latency, send_answers, 0);
}

void mdns_sync()
{
  /*FIXME We might not keep the right timing here*/
  while (list_head(answers))
  {
    send_answers(0);
  }
}
/* Return 1 if ipv6 address is lexicographically later than u8 array passed */
static int are_we_lex_later_ip6(uint8_t *u8, uip_ip6addr_t *ipv6_address)
{
  int i;

  for (i = 0; i < 16; i++) {
     DBG_PRINTF("Comparing u[8]: %d with a.u8[i]: %d\n", u8[i], ipv6_address->u8[i]);
     if (u8[i] > ipv6_address->u8[i])
        return 0;
  }

  return 1;
}
/* Return 1 if ip nodeid is lexicographically later than u8 array passed */
static int are_we_lex_later_ip4(uint8_t *u8, uint8_t nodeid, uint8_t flags1)
{
  uip_ipv4addr_t a; //An IPv4 address is 4 bytes
  int i;
  uint8_t zero_ip[4] = {0,0,0,0};

  if ((memcmp(u8, zero_ip, 4) == 0) && (flags1 & 0x84)) {
      DBG_PRINTF("This is a probe denial\n");
      return 0;
  }
  ipv46nat_ipv4addr_of_node(&a, nodeid);

  for (i = 0; i < 4; i++)
  {
     DBG_PRINTF("Comparing u[8]: %d with a.u8[i]: %d\n", u8[i], a.u8[i]);
     if (u8[i] > a.u8[i])
        return 0;
  }

  return 1;
}
int match_srv_target(dns_message_t* m)
{
    char temp_query_name[MAX_DOMAIN_SIZE];
    uint8_t len;
    len = domain_name_copy(m,m->ptr +6, temp_query_name, MAX_DOMAIN_SIZE);
    temp_query_name[len] = 0;
    DBG_PRINTF("srv_trarget: %s\n", domain_name_to_string(m,temp_query_name));
    if (lookup_by_node_name(temp_query_name) || lookup_by_service_name(temp_query_name)) {
          DBG_PRINTF("srv target is matching our database\n");
          return 1;
    }
    return 0;
}
int match_rdata(dns_message_t* m)
{
    char temp_query_name[MAX_DOMAIN_SIZE];

    uint8_t len;
    len = domain_name_copy(m,m->ptr, temp_query_name, MAX_DOMAIN_SIZE);
    temp_query_name[len] = 0;
    DBG_PRINTF("rdata: %s\n", domain_name_to_string(m,temp_query_name));
    if (lookup_by_node_name(temp_query_name) || lookup_by_service_name(temp_query_name)) {
          DBG_PRINTF("rdata is matching our database\n");
          return 1;
    }
    return 0;
}

static int do_we_have_ptr()
{ 
  struct answers* a;
  /* Check is we already have an answer for this endpoint. */
  for (a = list_head(answers); a != NULL; a = list_item_next(a))
  {
    //if(a->endpoint == ep && a->ge == ge) break;
    if (a->fields & FIELD_PTR) {
        DBG_PRINTF("PTR query present adding latency\n");
        return 1;
     }
  }
  return 0;
}


/*---------------------------------------------------------------------------*/
static void tcpip_handler(void)
{
  char query_name[MAX_DOMAIN_SIZE];
  //char answer_name[MAX_DOMAIN_SIZE];
  uint16_t qtype, qclass, ttl = 0;
  uint16_t i;
  rd_node_database_entry_t *n, *n1;

  rd_ep_database_entry_t *ep, *ep1 ;
  rd_group_entry_t* ge = 0;
  dns_message_t query_m;
  dns_message_t query_m_new;
  rd_node_database_entry_t *nd;
  uint8_t field;

  uint32_t rlength;
  uint8_t domain_name_q = 0;
  uint8_t endpoint_q = 0;
  static int latency;
#ifndef MDNS_TEST
  if (!uip_newdata() || exiting)
  {
    return;
  }
#endif
      DBG_PRINTF("______________ mDNS Code data len %d __________________\n",udp_data_len);
      DBG_PRINTF("From: ");
#ifndef MDNS_TEST
#if DEBUG
      uip_debug_ipaddr_print(&UIP_IP_BUF->srcipaddr);
#endif
#endif

  assert( (int)sizeof(struct dns_hdr) <= udp_data_len );

  query_m.hdr = (struct dns_hdr*) uip_appdata;
  query_m.ptr = (uip_appdata + sizeof(struct dns_hdr));
  query_m.end = (uip_appdata + udp_data_len);

  //if ((query_m.hdr->flags1 & 0xf8) == 0)
  {
#if 0
    if (tc_bit_set) {
        tc_bit_set = 0;
        if (memcmp(&UIP_IP_BUF->srcipaddr, &last_src_ipaddr, sizeof(uip_ip6addr_t)) == 0){ /*Continuation of previous truncated packet */
            DBG_PRINTF("This is continuation of previous mdns packet\n");
            if(query_m.hdr->flags1 & 0x02) {
                ERR_PRINTF("More than one packets with TC bit not supported. We already have one packet stored.\n");
                goto skip_and_free;
            }
            /* Stitching two packets together */
            last_dns_pkt = realloc(last_dns_pkt, last_dns_pkt_len + (udp_data_len - sizeof(struct dns_hdr)));
            if (!last_dns_pkt) {
                ERR_PRINTF("Realloc failed\n");
                goto skip_and_free;
            }
            memcpy(last_dns_pkt+last_dns_pkt_len, query_m.ptr, (udp_data_len - sizeof(struct dns_hdr)));
            query_m_new.hdr = (struct dns_hdr*) last_dns_pkt;
            query_m_new.hdr->numquestions += query_m.hdr->numquestions;
            query_m_new.hdr->numanswers +=query_m.hdr->numanswers;
            query_m_new.hdr->numauthrr += query_m.hdr->numauthrr;
            query_m_new.hdr->numextrarr += query_m.hdr->numextrarr;
            query_m.hdr = (struct dns_hdr*) last_dns_pkt;

            if(!(query_m.hdr->flags1 & 0x02)) {
                ERR_PRINTF("Something wrong! Saved msg does not have TC bit set\n");
                query_m.hdr = (struct dns_hdr*) uip_appdata;
                goto skip_and_free;
            }
            query_m.hdr->flags1 &= 0xfd; /*Reset the TC bit */
            query_m.ptr = last_dns_pkt + sizeof(struct dns_hdr);
            query_m.end =  last_dns_pkt + (last_dns_pkt_len + (udp_data_len - sizeof(struct dns_hdr)));
            if (UIP_HTONS(query_m.hdr->numquestions == 0) && (UIP_HTONS(query_m.hdr->numanswers == 1))) {
                 ERR_PRINTF("This has no question and this just one answer\n");
            }
        }
    }
    if (query_m.hdr->flags1 & 0x02) { /* Does the packet have TC bit set?*/
        tc_bit_set = 1;
        memcpy(&last_src_ipaddr, &UIP_IP_BUF->srcipaddr, sizeof(uip_ip6addr_t));
        last_dns_pkt = malloc(udp_data_len);
        if (!last_dns_pkt) {
            ERR_PRINTF("Malloc failed\n");
            goto skip;
        }
        last_dns_pkt_len = udp_data_len;
        memcpy(last_dns_pkt, uip_appdata, udp_data_len);
        DBG_PRINTF("Skipping processing this packet as its truncated\n");
        goto skip;
    }
#endif

    DBG_PRINTF("query_m.hdr->numquestions: %u\n", (unsigned int)query_m.hdr->numquestions);
    /*First process the questions section */
    for (i = 0; i < uip_htons(query_m.hdr->numquestions) ; i++)
    {
      if(query_m.ptr >= query_m.end) {
        WRN_PRINTF("mDNS package is cropped\n");
        break;
      }

      /* Parse a query record */
      query_m.last_domain_name = query_m.ptr;
      query_m.ptr = (char*) domain_name_end(query_m.ptr);

      qtype = uip_htons( *((uint16_t*) query_m.ptr) );
      query_m.ptr += sizeof(uint16_t);
      qclass = uip_htons( *((uint16_t*) query_m.ptr) );
      query_m.ptr += sizeof(uint16_t);

      /*Parse the domain name */
      domain_name_copy(&query_m, query_m.last_domain_name, query_name, MAX_DOMAIN_SIZE);

      DBG_PRINTF("Query(%i of %i) %s class=%i type=%i\n",i,uip_htons(query_m.hdr->numquestions), domain_name_to_string(&query_m,query_m.last_domain_name), qclass, qtype & 0x7FFF);

      DBG_PRINTF("From: ");
#ifndef MDNS_TEST
#if DEBUG        
      uip_debug_ipaddr_print(&UIP_IP_BUF->srcipaddr);
#endif
#endif
      DBG_PRINTF("\n");

#if 0
      {
        char service[MAX_LABEL_SIZE];
        char group[MAX_LABEL_SIZE];
        char type[MAX_LABEL_SIZE];

        if(parse_query_string(query_name,service,group,type))
        {
          DBG_PRINTF("service %s\n", domain_name_to_string(0, service));
          DBG_PRINTF("group %s\n", domain_name_to_string(0, group));
          DBG_PRINTF("type %s\n", domain_name_to_string(0, type));

          /* _z-wave._udp is a service discovery*/
          if(dns_domain_cmp(type,"\007_z-wave\004_udp"))
          {

            if(qtype == RR_TYPE_PTR)
            {
              //   build_ptr_answer(service,group);
              continue;
            }
          }
        }

      }
#endif

      /* If Query add answers */
      /*TODO Lookups must be case insensitive! */
      if ((query_m.hdr->flags1 & 0xf8) == 0)
      {
            
        if (string_right_compare(query_name, zwave_domain_name))
        {
          if (qtype == RR_TYPE_PTR)
          {
            //DBG_PRINTF("looks like a PTR query 1\n");
            build_or_del_ptr_answer(query_name, 1);
            domain_name_q = 1;
            continue;
          }
        }
        ep = query_to_endpoint(query_name, qtype, &field);
        if (ep)
        {
          add_answer(ep, ge, field, 0);
          endpoint_q = 1;
        }
      }
    }

    if (!query_m.hdr->numanswers)
    {
       if(find_duplicate_probe(query_m.ptr))
       {
          return;
       }
    }
/*
    DBG_PRINTF("query_m.hdr->numanswers: %u\n", (unsigned int) query_m.hdr->numanswers);
    DBG_PRINTF("query_m.hdr->numauthrr: %u\n", (unsigned int)query_m.hdr->numauthrr);
*/
    /* Known answers */
    for (i = 0; i < (uip_htons(query_m.hdr->numanswers) + uip_htons(query_m.hdr->numauthrr)) ; i++)
    {
      if(query_m.ptr >= query_m.end) {
        WRN_PRINTF("mDNS package is cropped\n");
        break;
      }
      /* Parse a RR record */
      query_m.last_domain_name = query_m.ptr;
      query_m.ptr = (char*) domain_name_end(query_m.ptr);
      qtype = uip_htons( *((uint16_t*) query_m.ptr) );
      query_m.ptr += sizeof(uint16_t);
      qclass = uip_htons( *((uint16_t*) query_m.ptr) );
      query_m.ptr += sizeof(uint16_t);
      ttl = uip_htonl( *((uint32_t*) query_m.ptr) );
      query_m.ptr += sizeof(uint32_t);
      rlength = uip_htons( *((uint16_t*) query_m.ptr) );
      query_m.ptr += sizeof(uint16_t);

      /*Parse the domain name */
      domain_name_copy(&query_m, query_m.last_domain_name, query_name, MAX_DOMAIN_SIZE);

      DBG_PRINTF("Known answer rr_type: %s %s %i zwave_domain_name: %s\n",rr_type_to_string[qtype], domain_name_to_string(&query_m, query_m.last_domain_name),rlength, zwave_domain_name);

      /* TODO: make it take real original TTL than 120. 120 seems to be common everywhere though */
      if ((strncasecmp(query_name, zwave_domain_name, strlen(zwave_domain_name)) == 0) && domain_name_q && (qtype == RR_TYPE_PTR) && match_rdata(&query_m) )
      {
          if (ttl > (120/2)) {
            DBG_PRINTF("This packet had question for %s. And then answer for it. We know this answer and TTl of the dns packet is not less than half of original so not sending the answer again\n", zwave_domain_name);
            build_or_del_ptr_answer(query_name, 0);
          }
          goto cont;
      }

      ep = lookup_by_service_name(query_name);
      if( i < uip_htons(query_m.hdr->numanswers)) {
        if (ep && endpoint_q && (qtype == RR_TYPE_SRV) && match_srv_target(&query_m)) // This is not just rr/answer but a question and rr/answer
        {
            if (ttl > (120/2)) {
              DBG_PRINTF("Removing known SRV answer %s from list\n", domain_name_to_string(0,query_name));
              del_answer(ep, FIELD_SRV);
            }
            goto cont;
        } else if(ep && !the_query_session.inuse) {
          DBG_PRINTF("One of our endpoint name or node name got conflict while we are not in query session\n");
            /* We should check if the Resource directory is in IDLE state or else skip the conflict packets */
          //ep->node->state = STATUS_MDNS_PROBE;
          goto cont;
        }
      }

      /*TODO Lookups must be case insensitive! */
      if (string_right_compare(query_name, zwave_domain_name))
      {
        DBG_PRINTF("string_right_compare passed\n");
        /*If this is a known answer in a query */
        if ((query_m.hdr->flags1 & 0xf8) == 0)
        {
          if (qtype == RR_TYPE_PTR)
          {
            /*Decompress PTR */
            domain_name_copy(&query_m, query_m.last_domain_name, query_name, MAX_DOMAIN_SIZE);
            ep = lookup_by_service_name(query_name);
            if (ep)
            {
              DBG_PRINTF("Removing known PTR %s from list\n", domain_name_to_string(0,query_name));
              del_answer(ep, FIELD_PTR);
            }
          }
        }
      }

      if(i >= (uip_htons(query_m.hdr->numanswers))) {
        rd_node_database_entry_t *n = lookup_by_node_name(query_name);
        if(n && !the_query_session.inuse) {
          DBG_PRINTF("One of our endpoint name or node name got conflict while we are not in query session\n");
            /* We should check if the Resource directory is in IDLE state or else skip the conflict packets */
          //n->state = STATUS_MDNS_PROBE;
          goto cont;
        }
      }

      /* This is an answer to a query, check if it is one of our queries*/
      if (the_query_session.inuse )
      {
           for (i = 0; i < the_query_session.no_questions; i ++) {
              if (string_right_compare(the_query_session.name[i], query_name)) {
                if (lookup_by_node_name(query_name)) {
                    DBG_PRINTF("here22\n");
                    nd = (rd_node_database_entry_t *)the_query_session.rd_node_database_entry;
                    if (!(query_m.hdr->flags1 & 0x84)) { /* If the msg is nor response and server is not authority for domain. Probe denials have these flags */
                        /* Simultaneous tie braking starts*/
                        /* tie_braking_won = 1 : we win the tie-break we ignore the other probe msg */
                        /* tie_braking_won = 2 : we lose the tie-break we wait for 1 second and probe for the same name again */
                        if (((qclass & 0x7F) > RR_CLASS_IN)) {
                            DBG_PRINTF("qclass is bigger thanRR_CLASS_IN\n");
                            DBG_PRINTF("Simultaneous tie braking other side won!!!\n");
                            tie_braking_won = 2;
                        } else if ( (((qclass & 0x7F) == RR_CLASS_IN))) {
                            if (qtype > RR_TYPE_A) {
                                 DBG_PRINTF("qclass is RR_CLASS_IN but qtype is bigger than our RR_TYPE_A\n");
                                 DBG_PRINTF("Simultaneous tie braking other side won!!!\n");
                                 tie_braking_won = 2;
                            } else if (qtype == RR_TYPE_A) {
                                if (are_we_lex_later_ip4((uint8_t *)query_m.ptr, nd->nodeid, query_m.hdr->flags1)) { 
                                   DBG_PRINTF("qclass is RR_CLASS_IN but qtype is RR_TYPE_A but our ip4 is lexicograpphically later than that of received\n");
                                   DBG_PRINTF("Simultaneous tie braking We won!!!\n");
                                       tie_braking_won = 1;
                                } else {
                                       tie_braking_won = 2;
                                }
                            } else if (qtype == RR_TYPE_AAAA) {
                                uip_ip6addr_t ip;
                                ipOfNode(&ip,nd->nodeid);
                                if (are_we_lex_later_ip6((uint8_t *)query_m.ptr, &ip)) {
                                   DBG_PRINTF("qclass is RR_CLASS_IN but qtype is RR_TYPE_AAAA but our ip6 is lexicograpphically later than that of received\n");
                                   DBG_PRINTF("Simultaneous tie braking We won!!!\n");
                                       tie_braking_won = 1;
                                } else {
                                       tie_braking_won = 2;
                                }
                            } 
                        }
                        /* Simultaneous tie braking ends*/
                    }
                }
                DBG_PRINTF("-------------- Matching the_query_session.name[i]: %s, query_name: %s\n",the_query_session.name[i], query_name);

                if(tie_braking_won != 0) {
                  ctimer_stop(&the_query_session.timer);
#ifndef MDNS_TEST
                  process_post(&mDNS_server_process, PROBE_DONE_EVENT, &the_query_session);
#endif
                }
                break;
              }
           }
     }

cont:
     query_m.ptr += rlength;
    }
    if ((query_m.hdr->flags1 & 0xf8) == 0 && list_length(answers) > 0 && (the_query_session.inuse == 0))
    {
      latency = query_m.hdr->flags1 & DNS_FLAG1_TRUNC ? 400 : 20;
      if(!do_we_have_ptr()) {
          latency = 0;
      } else {
          latency += (rand() & 0x7f);
      }
      DBG_PRINTF("send_answers in latency:%d ms \n", latency);
      ctimer_set(&answer_timer, latency, send_answers, 0);
    }
  }
skip_and_free:
  if(last_dns_pkt) {
      free(last_dns_pkt);
      last_dns_pkt = NULL;
      last_dns_pkt_len = 0;
  }
#ifndef MDNS_TEST
skip:
  /*This is needed to "release" the connection, and make it accept new connections*/
  uip_create_unspecified(&server_conn->ripaddr);
  server_conn->rport = 0;
#endif
}

/***************************** PROBING ***************************************/

/*Called when a query session is done*/
static void free_query_session(query_session_t* qs)
{
  //DBG_PRINTF("free_query_session\n");
  qs->inuse = 0;
}

static void gen_domain_name_for_ip6(nodeid_t nodeID, char *s)
{
  char digit[4];
  int i;
  uip_ip6addr_t ipv6_address;
  ipOfNode(&ipv6_address,nodeID );
#ifndef MDNS_TEST
#if DEBUG
  uip_debug_ipaddr_print(ipv6_address);
#endif
#endif
  for (i = 15; i >= 0; i--)
  {
    snprintf(digit, 2, "%01x", (ipv6_address.u8[i] >> 0) & 0xF);
    s = domain_name_append(digit, s);
    snprintf(digit, 2, "%01x", (ipv6_address.u8[i] >> 4) & 0xF);
    s = domain_name_append(digit, s);
  }
  s = domain_name_append("ip6", s);
  s = domain_name_append("arpa", s);
}



static void gen_domain_name_for_ip4(nodeid_t nodeid,char* s)
{
  char digit[4];
  int i;
  uip_ipv4addr_t a; //An IPv4 address is 4 bytes

  
  if(!ipv46nat_ipv4addr_of_node(&a, nodeid)) {
    memset(&a,0,sizeof(uip_ipv4addr_t)); 
  }

  for (i = 3; i >= 0; i--)
  {
    snprintf(digit, 4, "%i", a.u8[i]);
    s = domain_name_append(digit, s);
  }
/*
  s = domain_name_append("26", s);
  s = domain_name_append("1", s);
  s = domain_name_append("0", s);
  s = domain_name_append("10", s);
*/

  s = domain_name_append("in-addr", s);
  s = domain_name_append("arpa", s);

}

#if 0
void m_print_hex(const char *str, int len)
{
    int i;

    for ( i = 0; i < len; i++)
    {
        printf(" %2x ", (unsigned char)str[i]);
        if (!(i % 10))
        {
            printf("\n");
        }
    }
}
#endif
int find_duplicate_probe(const char* received)
{
    int i;

    if (probe_rr_len && probe_rr)
    {
        if(memcmp(received, probe_rr, probe_rr_len) == 0)
        {
            ERR_PRINTF("This is what we sent as probe. Looks like duplicate. Dropping!\n");
            return 1;
        }
#if 0
        ERR_PRINTF("received:\n");
        m_print_hex(received, probe_rr_len);
        ERR_PRINTF("probe_rr:\n");
        m_print_hex(probe_rr, probe_rr_len);
        printf("\n");
#endif
    }
    return 0;
}
static void send_name_probe(query_session_t* qs)
{
  dns_message_t query_and_answer_m;
  char reply[UIP_UDP_APP_SIZE + 512];
  int i = 0;
  const char *dns_name;
  char lqn[MAX_DOMAIN_SIZE], *s;
  rd_node_database_entry_t* nd;
  probe_rr_len = 0;
  const char *offset;

  if (qs->inuse == 0) {
      //DBG_PRINTF("send_name_probe qs->inuse is 0 returning\n");
      return;
  }
  init_dns_message(&query_and_answer_m, reply, sizeof(reply), 0);
  query_and_answer_m.hdr->id = 0; //qs->transation_id; //Transaction id should always be 0

//  DBG_PRINTF("------------send_name_probe qs->no_questions: %d, qs->no_authrrs: %d\n",qs->no_questions,  qs->no_authrrs);
  qs->no_questions = 0;
  qs->no_authrrs = 0;
  switch (qs->prob_type) {
  case SERVICE:
      qs->no_questions = 1;
      dns_name = ep_service_name(qs->rd_node_database_entry);
      domain_name_copy(0, dns_name, qs->name[i], MAX_DOMAIN_SIZE);
      add_qrecord_hdr(&query_and_answer_m, qs->name[i], RR_TYPE_ANY, RR_CLASS_IN);
      break;
  case NODE:
      qs->no_questions++;
      s = lqn;
      *s = rd_get_node_name(qs->rd_node_database_entry, s + 1, MAX_DOMAIN_SIZE - 1 - 6);
      s += (*s) + 1;
      s = domain_name_append("local", s);
      dns_name = (const char *)lqn;
      domain_name_copy(0, dns_name, qs->name[i], MAX_DOMAIN_SIZE);
      add_qrecord_hdr(&query_and_answer_m, qs->name[i], RR_TYPE_ANY, RR_CLASS_IN);
      i++;

  /*no break */
  case IP:
      qs->no_questions++;
      s = lqn;
      nd = (rd_node_database_entry_t*)qs->rd_node_database_entry;
#ifndef MDNS_TEST
#if DEBUG
      uip_debug_ipaddr_print(&nd->ipv6_address);
#endif
#endif
      gen_domain_name_for_ip6(nd->nodeid, s);
            

      dns_name = (const char *) lqn;
      domain_name_copy(0, dns_name, qs->name[i], MAX_DOMAIN_SIZE);
      add_qrecord_hdr(&query_and_answer_m, qs->name[i], RR_TYPE_ANY, RR_CLASS_IN);

      i++;
      qs->no_questions++;
      s = lqn;
      gen_domain_name_for_ip4(nd->nodeid, s);

      dns_name = (const char *)lqn;
    
      domain_name_copy(0, dns_name, qs->name[i], MAX_DOMAIN_SIZE);
      //DBG_PRINTF("qs->name[0]:%s \n qs->name[1]:%s \nqs->name[2]:%s \n", qs->name[0],qs->name[1],qs->name[2]);
      add_qrecord_hdr(&query_and_answer_m, qs->name[i], RR_TYPE_ANY, RR_CLASS_IN);
  }
  offset = query_and_answer_m.ptr; 

  switch (qs->prob_type) {
  case SERVICE:
      add_ptr_for_node(qs->rd_node_database_entry, &query_and_answer_m, 0);
      add_txt_for_node(qs->rd_node_database_entry, &query_and_answer_m);
      add_srv_for_node(qs->rd_node_database_entry, &query_and_answer_m);
      qs->no_authrrs += 3;
      break;
  case NODE:
      DBG_PRINTF("This is a probe so not setting cache-flush bit\n");
      dont_set_cache_flush_bit = 1;
      add_aaaa_for_node(qs->rd_node_database_entry, &query_and_answer_m);
      add_a_for_node(qs->rd_node_database_entry, &query_and_answer_m);
      qs->no_authrrs += 2;
  /*no break */
  case IP:
      add_arpa6_for_node(qs->rd_node_database_entry, &query_and_answer_m);
      add_arpa4_for_node(qs->rd_node_database_entry, &query_and_answer_m);
      qs->no_authrrs += 2;
  }
  
  if (query_and_answer_m.ptr > offset)
  {
      probe_rr_len = query_and_answer_m.ptr - offset;
      if(probe_rr)
      {
        free(probe_rr);
        probe_rr = 0;
      }
      probe_rr = malloc(probe_rr_len);
      memcpy(probe_rr, offset, probe_rr_len);
  }
  
//  ERR_PRINTF("probe_rr_len: %d\n", probe_rr_len);
//  m_print_hex(probe_rr, probe_rr_len);

  query_and_answer_m.hdr->numquestions = qs->no_questions;
  query_and_answer_m.hdr->numauthrr = qs->no_authrrs;
  /**prepare a query */

  DBG_PRINTF("probing %i\n", qs->count);
  send_dns_message(&query_and_answer_m, &mDNS_mcast_addr, MDNS_PORT);
  qs->count++;


  if (qs->count >= 3)
  {
    if (dont_set_cache_flush_bit) {
        dont_set_cache_flush_bit = 0;
    }
#ifdef MDNS_TEST
    free_query_session(qs);
#else
    process_post(&mDNS_server_process, PROBE_DONE_EVENT, qs);
#endif
    free(probe_rr);
    probe_rr = 0;
    probe_rr_len = 0;
  } else
  {
    //DBG_PRINTF("Scheduling send_name_probe again \n");
    /*Delay 255 ms */

    ctimer_set(&qs->timer, 750, (void (*)(void *)) send_name_probe, qs);
  }
}


/**
 * When a new resource is added to the system the mDNS service must perform a probe to make
 * sure that no records with that name exists on the network.
 */
static int start_name_probe(void (*callback)(int bSucsess, void*), 
                            void* rd_ptr, enum probe_type probe_type, void* ctx)
{
  query_session_t* qs = &the_query_session;
  //DBG_PRINTF("start_name_probe: qs->inuse: %d\n", qs->inuse);

  if (qs->inuse)
  {
    //DBG_PRINTF("No more probe sessions available\n");
    return 0;
  }

  qs->inuse = 1;
  qs->callback = callback;
  qs->rd_node_database_entry = rd_ptr;
  qs->prob_type = probe_type;
  qs->ctx = ctx;
  qs->count = 0;

  DBG_PRINTF("Starting probe\n");
  /*Delay between 0 an 255 ms */
  ctimer_set(&qs->timer, random_rand() & 0xff, (void (*)(void *)) send_name_probe, qs);
  return 1;
}

int mdns_endpoint_name_probe(rd_ep_database_entry_t* ep, void (*callback)(int bSucsess, void*), void*ctx)
{
   return start_name_probe(callback, (void*)ep, SERVICE, ctx);
}

int mdns_node_name_probe(rd_node_database_entry_t* nd, void (*callback)(int bSucsess, void*), void*ctx)
{
//  DBG_PRINTF("----------------------------------2");
//  uip_debug_ipaddr_print(&nd->ipv6_address);
  return start_name_probe(callback, (void* )nd, NODE, ctx);
}


bool mdns_idle(void) {
   return (list_head(answers) == 0);
}


/**
 * Initiate mdns shutdown, by sending what is left to be sent, but not accepting new queries. Other processes
 * should receive a PROCESS_EVENT_EXITED event when shutdown is complete.
 */
void mdns_exit()
{
  if (list_head(answers) == 0)
  {
#ifndef MDNS_TEST
    process_exit(&mDNS_server_process);
#endif
  } else
  {
    exiting = 1;
  }
}

/*---------------------------------------------------------------------------*/

#ifdef MDNS_TEST
void hook_tcpip_handler() {
    tcpip_handler();
}
#else
PROCESS_THREAD(mDNS_server_process, ev, data)
{
  PROCESS_BEGIN()
    ;

    WRN_PRINTF("mDNS server started\n");

    ctimer_stop(&answer_timer);
    list_init(answers);

    ctimer_stop(&the_query_session.timer);
    the_query_session.inuse = 0;

    exiting = 0;
    server_conn = udp_new(0, UIP_HTONS(0), &server_conn);
    if (NULL == server_conn)
    {
      DBG_PRINTF("could not initialize connection 1\n");
    }
    udp_bind(server_conn, UIP_HTONS(MDNS_PORT));

    if (!uip_ds6_maddr_add((uip_ipaddr_t*) &mDNS_mcast_addr))
    {
      ERR_PRINTF("Could not Register Multicast address\n");
    }

    while (1)
    {
      PROCESS_YIELD()
      ;
      if (ev == tcpip_event)
      {
        if (data == &server_conn)
        {
          tcpip_handler();
        }
      } else if (ev == PROBE_DONE_EVENT)
      {
        query_session_t* qs = (query_session_t*) data;
        void (*cb)(int, void*);

        if (qs->count == 3)
        {
          WRN_PRINTF("Name probing done and no duplicates found\n");
        } else
        {
          WRN_PRINTF("Name probing done and duplicates found\n");
        }
        cb = qs->callback;
        free_query_session(qs);
        if (cb)
        {
          //DBG_PRINTF("Calling callback\n");
          cb(qs->count == 3, qs->ctx);
        }
      }
    }

  PROCESS_END();
}
#endif

/**
 * TODO
 * - IPv6 reverse lookup: b.e.f.ip6.arpa.
 * - negative answers for records that don't exists.
 * - Delayed responses(
 *   if the responde has good reason to believe that it is the only one who has an answer on the link, then it should not delay its response.
 *   . Uniform random delay 20ms-120ms)
 * - We must not answer a given record within 1 sec intervals.
 */
