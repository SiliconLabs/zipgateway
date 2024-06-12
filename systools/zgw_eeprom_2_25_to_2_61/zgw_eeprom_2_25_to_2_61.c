#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include "smalloc.h"
/*------------------------------------------------------*/
/*      Taken from Resource Directory                   */
/*------------------------------------------------------*/

typedef union uip_ip6addr_t {
  uint8_t  u8[16];         /* Initializer, must come first!!! */
  uint16_t u16[8];
} uip_ip6addr_t ;

#define ZW_MAX_NODES        232
#define RD_SMALLOC_SIZE 0x5E00

#define PREALLOCATED_VIRTUAL_NODE_COUNT 4
#define MAX_IP_ASSOCIATIONS (200 + PREALLOCATED_VIRTUAL_NODE_COUNT)

typedef uint8_t nodeid_t;
enum ASSOC_TYPE {temporary_assoc, permanent_assoc, local_assoc, proxy_assoc};
 
struct IP_association {
  void *next;
  nodeid_t virtual_id;
  enum ASSOC_TYPE type; /* unsolicited or association */
  uip_ip6addr_t resource_ip; /*Association Destination IP */
  uint8_t resource_endpoint;  /* From the IP_Association command. Association destination endpoint */
  uint16_t resource_port;
  uint8_t virtual_endpoint;   /* From the ZIP_Command command */
  uint8_t grouping;
  uint8_t han_nodeid; /* Association Source node ID*/
  uint8_t han_endpoint; /* Association Source endpoint*/
  uint8_t was_dtls;
  uint8_t mark_removal;
}; // __attribute__((packed));   /* Packed because we copy byte-for-byte from mem to eeprom */

#define ASSOCIATION_TABLE_EEPROM_SIZE (sizeof(uint16_t) + MAX_IP_ASSOCIATIONS * sizeof(struct IP_association))
/* General layout of the datastore. This is the place to add more data. */
typedef struct rd_eeprom_static_hdr {
  uint32_t magic;
  uint32_t homeID;
  uint8_t  nodeID;
  uint32_t flags;
  uint16_t node_ptrs[ZW_MAX_NODES];
  uint8_t  smalloc_space[RD_SMALLOC_SIZE];
  uint8_t temp_assoc_virtual_nodeid_count;
  nodeid_t temp_assoc_virtual_nodeids[PREALLOCATED_VIRTUAL_NODE_COUNT];
  uint16_t association_table_length;
  uint8_t association_table[ASSOCIATION_TABLE_EEPROM_SIZE];
} rd_eeprom_static_hdr_t;

typedef enum {
  MODE_PROBING,
  MODE_NONLISTENING,
  MODE_ALWAYSLISTENING,
  MODE_FREQUENTLYLISTENING,
  MODE_MAILBOX,
} rd_node_mode_t;

typedef enum {
  //STATUS_ADDING,
  STATUS_CREATED,
  //STATUS_PROBE_PROTOCOL_INFO,
  STATUS_PROBE_NODE_INFO,
  STATUS_PROBE_PRODUCT_ID,
  STATUS_ENUMERATE_ENDPOINTS,
  STATUS_SET_WAKE_UP_INTERVAL,
  STATUS_ASSIGN_RETURN_ROUTE,
  STATUS_PROBE_WAKE_UP_INTERVAL,
  STATUS_PROBE_ENDPOINTS,
  STATUS_MDNS_PROBE,
  STATUS_MDNS_EP_PROBE,
  STATUS_DONE,
  STATUS_PROBE_FAIL,
  STATUS_FAILING,
} rd_node_state_t;

typedef enum {
    EP_STATE_PROBE_INFO,
    EP_STATE_PROBE_SEC_INFO,
    EP_STATE_PROBE_ZWAVE_PLUS,
    EP_STATE_MDNS_PROBE,
    EP_STATE_MDNS_PROBE_IN_PROGRESS,
    EP_STATE_PROBE_DONE,
    EP_STATE_PROBE_FAIL
} rd_ep_state_t;

typedef void ** list_t;

#define LIST_CONCAT2(s1, s2) s1##s2
#define LIST_CONCAT(s1, s2) LIST_CONCAT2(s1, s2)
#define LIST_STRUCT(name) \
         void *LIST_CONCAT(name,_list); \
         list_t name
typedef struct rd_node_database_entry_new {

  uint32_t wakeUp_interval;
  uint32_t lastAwake;
  uint32_t lastUpdate;

  uip_ip6addr_t ipv6_address;

  uint8_t nodeid;
  uint8_t security_flags;
  /*uint32_t homeID;*/

  rd_node_mode_t mode;
  rd_node_state_t state;

  uint16_t manufacturerID;
  uint16_t productType;
  uint16_t productID;

  uint8_t nodeType; // Is this a controller, routing slave ... etc

  uint8_t refCnt;
  uint8_t nEndpoints;
  uint8_t nAggEndpoints;

  LIST_STRUCT(endpoints);

  uint8_t nodeNameLen;
  char* nodename;
} rd_node_database_entry_new_t;

typedef struct rd_node_database_entry {

  uint32_t wakeUp_interval;
  uint32_t lastAwake;
  uint32_t lastUpdate;

  uip_ip6addr_t ipv6_address;

  uint8_t nodeid;
  uint8_t security_flags;
  /*uint32_t homeID;*/

  rd_node_mode_t mode;
  rd_node_state_t state;

  uint16_t manufacturerID;
  uint16_t productType;
  uint16_t productID;

  uint8_t nodeType; // Is this a controller, routing slave ... etc

  uint8_t refCnt;
  uint8_t nEndpoints;

  LIST_STRUCT(endpoints);

  uint8_t nodeNameLen;
  char* nodename;
} rd_node_database_entry_t;

typedef struct rd_ep_data_store_entry_new {
  uint8_t endpoint_info_len;
  uint8_t endpoint_name_len;
  uint8_t endpoint_loc_len; 
  uint8_t endpoint_aggr_len; 
  uint8_t endpoint_id;
  rd_ep_state_t state;
  uint16_t iconID;
} rd_ep_data_store_entry_new_t;

typedef struct rd_ep_data_store_entry {
  uint8_t endpoint_info_len;
  uint8_t endpoint_name_len;
  uint8_t endpoint_loc_len; 
  uint8_t endpoint_id;
  rd_ep_state_t state;
  uint16_t iconID;
} rd_ep_data_store_entry_t;

/*------------------------------------------------------*/

unsigned long size = 0;

/* Buf for reading from old eeprom */
unsigned char *buf;

/* Buf for the whole eeprom file with size 65548 */
unsigned char *eeprom_buf;
static int f;
const char *linux_conf_eeprom_file;

void eeprom_read(unsigned long addr, unsigned char *buf, int size)
{
  lseek(f, addr, SEEK_SET);
  if(read(f, buf, size)!=size) {
    perror("Read error. Eeprom conversion failed. Eeprom file should not be used");
  }
}

uint16_t rd_eeprom_read(uint16_t offset, int len,void* data)
{
  //DBG_PRINTF("Reading at %x\n", 0x100 + offset);
  eeprom_read(0x40 + offset,data,len);
  return len;
}

void eeprom_write(unsigned long addr, unsigned char *buf, int size)
{
  lseek(f, addr, SEEK_SET);
  if(write(f, buf, size) != size) {
    perror("Write error. Eeprom conversion failed. Eeprom file should not be used");
  }

  sync();
}

uint16_t rd_eeprom_write(uint16_t offset,int len,void* data)
{
  //DBG_PRINTF("Writing at %x\n", 0x100 + offset);
  if(len) {
    eeprom_write(0x40 + offset,data,len);
  }
  return len;
}

void buf_read(unsigned long addr, unsigned char *buf, int size)
{
  memcpy(buf, eeprom_buf + addr, size);
}

uint16_t eeprom_buf_read(uint16_t offset, uint8_t len, void* data)
{
  buf_read(0x40 + offset, data, len);
  return len;
}

void buf_write(unsigned long addr, unsigned char *buf, int size)
{
  memcpy(eeprom_buf + addr, buf, size);
}

uint16_t eeprom_buf_write(uint16_t offset, uint8_t len, void* data)
{
  if(len) {
    buf_write(0x40 + offset, data, len);
  }
  return len;
}

static const small_memory_device_t buf_dev = {
  .offset = offsetof(rd_eeprom_static_hdr_t, smalloc_space),
  .size = sizeof(((rd_eeprom_static_hdr_t*)0)->smalloc_space),
  .psize = 8,
  .read = eeprom_buf_read,
  .write = eeprom_buf_write,
};

int check_structure_alignemnts()
{
    int *ptr;
    if (sizeof(ptr) != 4) {
        printf("Error: This program is developed for 32bit systems\n");
        return 1;
    }
    if ((offsetof(rd_ep_data_store_entry_t, state) != 4) || offsetof(rd_ep_data_store_entry_t, iconID) != 8) {
        printf("-------------error. Eeprom conversion failed. Eeprom file should not be used\n");
        return 1;
    }
    if(sizeof(rd_ep_data_store_entry_new_t) != 16) {
        printf("-------------error. Eeprom conversion failed. Eeprom file should not be used\n");
        return 1;
    }
    if ((offsetof(rd_ep_data_store_entry_new_t, endpoint_aggr_len) != 3)) {
        printf("-------------error. Eeprom conversion failed. Eeprom file should not be used\n");
        return 1;
    }
    if ((offsetof(rd_ep_data_store_entry_new_t, state) != 8) || (offsetof(rd_ep_data_store_entry_new_t, iconID) != 12)) {
        printf("-------------error. Eeprom conversion failed. Eeprom file should not be used2\n");
        return 1;
    }
    if (sizeof(rd_node_database_entry_new_t) != sizeof(rd_node_database_entry_t)) {
        printf("-------------error. Eeprom conversion failed. Eeprom file should not be used1\n");
        return 1;
    }
//    printf("offsetof(rd_ep_data_store_entry_t, state): %d\n", offsetof(rd_ep_data_store_entry_t, state));
//    printf("offsetof(rd_ep_data_store_entry_t, iconID): %d\n", offsetof(rd_ep_data_store_entry_t, iconID));
    return 0;
}

void print_261_eeprom(int f)
{
    uint16_t n_ptr, e_ptr;
    rd_node_database_entry_new_t r;
    rd_ep_data_store_entry_new_t e;
    int i, j;
    size = lseek(f, 0, SEEK_END);
    const uint16_t static_size = offsetof(rd_node_database_entry_t, nodename);
    for (i = 1; i < ZW_MAX_NODES; i++) {
        rd_eeprom_read(offsetof(rd_eeprom_static_hdr_t, node_ptrs[i]), sizeof(uint16_t), &n_ptr);
        if(n_ptr == 0) {
            continue;
        }
        printf("------------------------------------\n");
        rd_eeprom_read(n_ptr, static_size , &r);
        printf("n_ptr %x node ID %d\n", n_ptr, r.nodeid);
        printf("\twake up interval %d\n\tlast awake %d\n\tlast update %d\n", r.wakeUp_interval, r.lastAwake, r.lastUpdate);
        printf("\tipv6 address %04x%04x%04x%04x%04x%04x%04x%04x\n", r.ipv6_address.u16[0], r.ipv6_address.u16[1], r.ipv6_address.u16[2], r.ipv6_address.u16[3], r.ipv6_address.u16[4], r.ipv6_address.u16[5], r.ipv6_address.u16[6], r.ipv6_address.u16[7]);
        printf("\tsecurity flags %d\n\tmode %d\n\tstate %d\n\tmanufacturerID %d\n\tproduct type %d\n\tproduct ID %d\n\tnode type %d\n\tref count %d\n\tnEndpoints %d\n\tnode name length %d\n", r.security_flags, r.mode, r.state, r.manufacturerID, r.productType, r.productID, r.nodeType, r.refCnt, r.nEndpoints, r.nodeNameLen);

        n_ptr = n_ptr + static_size + r.nodeNameLen;

        for(j = 0; j < r.nEndpoints; j++) {
            rd_eeprom_read(n_ptr, sizeof(uint16_t), &e_ptr);
            printf("\t\te_ptr %x\n", e_ptr);

            rd_eeprom_read(e_ptr, sizeof(rd_ep_data_store_entry_new_t), &e);
            printf("\t\tendpoint ID %d\n\t\tendpoint info len %d\n\t\tendpoint name len %d\n\t\tendpoint location len %d\n\t\tendpoint aggr len %d\n\t\tstate %d\n\t\ticon ID %d\n", e.endpoint_id, e.endpoint_info_len, e.endpoint_name_len, e.endpoint_loc_len, e.endpoint_aggr_len, e.state, e.iconID);

            n_ptr+=sizeof(uint16_t);
        }
    }
}

void print_old_node_entry(rd_node_database_entry_t r)
{
  printf("\twake up interval %d\n\tlast awake %d\n\tlast update %d\n", r.wakeUp_interval, r.lastAwake, r.lastUpdate);
  printf("\tipv6 address %04x%04x%04x%04x%04x%04x%04x%04x\n", r.ipv6_address.u16[0], r.ipv6_address.u16[1], r.ipv6_address.u16[2], r.ipv6_address.u16[3], r.ipv6_address.u16[4], r.ipv6_address.u16[5], r.ipv6_address.u16[6], r.ipv6_address.u16[7]);
  printf("\tsecurity flags %d\n\tmode %d\n\tstate %d\n\tmanufacturerID %d\n\tproduct type %d\n\tproduct ID %d\n\tnode type %d\n\tref count %d\n\tnEndpoints %d\n\tnode name length %d\n", r.security_flags, r.mode, r.state, r.manufacturerID, r.productType, r.productID, r.nodeType, r.refCnt, r.nEndpoints, r.nodeNameLen);
}

void print_old_endpoint_entry(rd_ep_data_store_entry_t ep)
{
  printf("\t\tendpoint ID %d\n\t\tendpoint info len %d\n\t\tendpoint name len %d\n\t\tendpoint location len %d\n\t\tstate %d\n\t\ticon ID %d\n", ep.endpoint_id, ep.endpoint_info_len, ep.endpoint_name_len, ep.endpoint_loc_len, ep.state, ep.iconID);
}

/*
 * EEPROM layout
 *
 * magic | homeID | nodeID | flags | node_ptrs[ZW_MAX_NODES]
 *
 * SMALLOC_SPACE[RD_SMALLOC_SIZE] //5E00
 *     rd_node_database_entry_1 | rd_ep_data_store_entry_1 + ep_info_1 + ep_name_1 + ep_loc_1 | rd_ep_data_store_entry_2 ...
 *     rd_node_database_entry_2 | rd_ep_data_store_entry_1 + ep_info_1 + ep_name_1 + ep_loc_1 | rd_ep_data_store_entry_2 ...
 *
 * virtual node info
 */

/*
 * Create a eeprom buffer(eeprom_buf) with the same size and fill it with
 * converted data in the offset indicated by smalloc. When done, copy it back to
 * original eeprom.
 */
int main(int argc, char **argv)
{
    uint16_t n_ptr, e_ptr, new_e_ptr, new_n_ptr;
    rd_node_database_entry_t old_r;
    rd_node_database_entry_new_t new_r;
    rd_ep_data_store_entry_t old_e;
    rd_ep_data_store_entry_new_t new_e;
    uint8_t endpoint_info[100];
    char endpoint_location[100], endpoint_name[100];
    int i, j;
    //uint8_t c;

    if (argc < 2) {
        printf("Usage: convert_eeprom <eeprom_file_path>\n");
        printf("eeprom file in changed inline. it can not be used in ver2_25 of zipgateway again after changing \n");
        exit(1);
    }
    linux_conf_eeprom_file = argv[1];
    printf("Opening eeprom file %s\n", linux_conf_eeprom_file);
    f = open(linux_conf_eeprom_file, O_RDWR | O_CREAT, 0644);

    if(f < 0) {
        fprintf(stderr, "Error opening eeprom file %s\n. Eeprom conversion failed. Eeprom file should not be used", linux_conf_eeprom_file);
        perror("");
        exit(1);
    }

    size = lseek(f, 0, SEEK_END);
    buf = malloc(size + 64000);
    eeprom_buf = malloc(size);

    if(check_structure_alignemnts()) {
        exit(1);
    }

    rd_eeprom_static_hdr_t static_header;
    rd_eeprom_read(0, sizeof(static_header), &static_header);

    /* Write everything in static header except node pointer address, use
     * buf_write for writing large size non-smalloc data */
    eeprom_buf_write(0, offsetof(rd_eeprom_static_hdr_t, node_ptrs), &static_header);
    buf_write(0x40 + offsetof(rd_eeprom_static_hdr_t, temp_assoc_virtual_nodeid_count), &static_header.temp_assoc_virtual_nodeid_count, sizeof(static_header) - offsetof(rd_eeprom_static_hdr_t, temp_assoc_virtual_nodeid_count));

    /* The size of nodename is variant */
    const uint16_t old_static_size = offsetof(rd_node_database_entry_t, nodename);
    const uint16_t new_static_size = offsetof(rd_node_database_entry_new_t, nodename);
    for (i = 1; i < ZW_MAX_NODES; i++) {
        n_ptr = static_header.node_ptrs[i];
        if(n_ptr == 0) {
            continue;
        }
        printf("------------------------------------\n");
        printf("n_ptr %x", n_ptr);
        
        /* Read the old node entry pointer */
        n_ptr += rd_eeprom_read(n_ptr, old_static_size , &old_r);

        printf(" node ID %d\n", old_r.nodeid);

        /* Smalloc the new space and update the new node entry address */
        new_n_ptr = smalloc(&buf_dev, new_static_size + old_r.nodeNameLen + old_r.nEndpoints*sizeof(uint16_t));
        eeprom_buf_write(offsetof(rd_eeprom_static_hdr_t, node_ptrs[i]), sizeof(uint16_t), &new_n_ptr);

        /* Fill the value in new struct */
        memset(&new_r, 0, offsetof(rd_node_database_entry_new_t, nodename));
        new_r.wakeUp_interval = old_r.wakeUp_interval;
        new_r.lastAwake = old_r.lastAwake;
        new_r.lastUpdate = old_r.lastUpdate;
        memcpy(&new_r.ipv6_address, &old_r.ipv6_address, sizeof(uip_ip6addr_t));
        new_r.nodeid = old_r.nodeid;
        new_r.security_flags = old_r.security_flags;
        new_r.mode = old_r.mode;
        new_r.state = old_r.state;
        new_r.manufacturerID = old_r.manufacturerID;
        new_r.productType = old_r.productType;
        new_r.productID = old_r.productID;
        new_r.nodeType = old_r.nodeType;
        new_r.refCnt = old_r.refCnt;
        new_r.nEndpoints = old_r.nEndpoints;
        new_r.nAggEndpoints = 0; // new netry
        new_r.nodeNameLen = old_r.nodeNameLen;

        /* Write new node entry to eeprom buf */
        new_n_ptr += eeprom_buf_write(new_n_ptr, new_static_size, &new_r);

        /* Write nodename if there is */
        n_ptr += rd_eeprom_read(n_ptr, old_r.nodeNameLen , &old_r.nodename);
        new_n_ptr += eeprom_buf_write(new_n_ptr, old_r.nodeNameLen, old_r.nodename);


        /* debug message */
        print_old_node_entry(old_r);

        for(j = 0; j < old_r.nEndpoints; j++) {
            rd_eeprom_read(n_ptr, sizeof(uint16_t), &e_ptr);
            printf("\t\te_ptr %x\n", e_ptr);

            /* Read endpoint entry + endpoint_info + endpoint_name +
             * endpoint_location */
            e_ptr += rd_eeprom_read(e_ptr, sizeof(rd_ep_data_store_entry_t), &old_e);
            e_ptr += rd_eeprom_read(e_ptr, old_e.endpoint_info_len, endpoint_info);
            e_ptr += rd_eeprom_read(e_ptr, old_e.endpoint_name_len, endpoint_name);
            e_ptr += rd_eeprom_read(e_ptr, old_e.endpoint_loc_len, endpoint_location);
    
            /* Smalloc the new space and update the new endpoint entry address */
            new_e_ptr = smalloc(&buf_dev, sizeof(rd_ep_data_store_entry_new_t) + old_e.endpoint_info_len + old_e.endpoint_loc_len + old_e.endpoint_name_len);
            new_n_ptr += eeprom_buf_write(new_n_ptr, sizeof(uint16_t), &new_e_ptr);

            /* Fill the value in new endpoint structure */
            new_e.endpoint_info_len = old_e.endpoint_info_len;
            new_e.endpoint_name_len = old_e.endpoint_name_len;
            new_e.endpoint_loc_len = old_e.endpoint_loc_len;
            new_e.endpoint_aggr_len = 0; // new entry
            new_e.endpoint_id = old_e.endpoint_id;
            new_e.state = old_e.state;
            new_e.iconID = old_e.iconID;

            /* Write the endpoint into eeprom buf */
            new_e_ptr += eeprom_buf_write(new_e_ptr, sizeof(rd_ep_data_store_entry_new_t), &new_e);
            new_e_ptr += eeprom_buf_write(new_e_ptr, new_e.endpoint_info_len, endpoint_info);
            new_e_ptr += eeprom_buf_write(new_e_ptr, new_e.endpoint_name_len, endpoint_name);
            new_e_ptr += eeprom_buf_write(new_e_ptr, new_e.endpoint_loc_len, endpoint_location);
            /* The converted eeprom must have zero length of endpoint_agg */
            //new_e_ptr += eeprom_buf_write(new_e_ptr, new_e.endpoint_aggr_len, endpoint_agg);

            /* debug message */
            print_old_endpoint_entry(old_e);
        }
    }
    printf("====================================\n");

    /* Write the eeprom buf back to eeprom file */
    lseek(f, 0, SEEK_SET);
    write(f, eeprom_buf, size);

    /* Print out the new eeprom */
    print_261_eeprom(f);

    close(f);
    free(buf);
    free(eeprom_buf);
    printf("Converted done! %s file changed inline now. it can not be used in ver2_25 of zipgateway again after changing \n", linux_conf_eeprom_file);
}
