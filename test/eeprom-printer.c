/* © 2019 Silicon Laboratories Inc.  */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include "eeprom-printer.h"

const cc_version_pair_t controlled_cc_v[] = {{COMMAND_CLASS_VERSION, 0x0},
                                             {COMMAND_CLASS_ZWAVEPLUS_INFO, 0x0},
                                             {COMMAND_CLASS_MANUFACTURER_SPECIFIC, 0x0},
                                             {COMMAND_CLASS_WAKE_UP, 0x0},
                                             {COMMAND_CLASS_MULTI_CHANNEL_V4, 0x0},
                                             {COMMAND_CLASS_ASSOCIATION, 0x0},
                                             {COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, 0x0},
                                             {0xffff, 0xff}};

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

void rd_node_cc_versions_set_default(rd_node_database_entry_v21_t *n)
{
  if(!n)
    return;
  if(n->node_cc_versions) {
    memcpy(n->node_cc_versions, controlled_cc_v, n->node_cc_versions_len);
  } else {
    perror("Node CC version set default failed.\n");
  }
}


/**
 *  Print an array in decimal.
 */
static void
print_array_decimal(uint8_t* buf, int len)
{
  int i;
  for (i = 0; i < len; i++)
  {
    printf("%d ", buf[i]);
  }
  printf("\n");
}

/**
 * Print the temporary virtual nodes information.
 * This works for all eeprom versions at the time of writing, from v2.0 to v2.4.
 */
static void print_virtual_nodes()
{
  uint16_t file_idx = offsetof(rd_eeprom_static_hdr_v20_t, temp_assoc_virtual_nodeid_count);
  uint8_t virtual_node_count;
  rd_eeprom_read(file_idx, 1, &virtual_node_count);
  uint8_t *virtual_nodes = malloc(virtual_node_count * sizeof(nodeid_t));
  file_idx += 1;
  rd_eeprom_read(file_idx, virtual_node_count, virtual_nodes);
  printf("Virtual node count: %d\n", virtual_node_count);
  printf("Virtual node IDs: ");
  print_array_decimal(virtual_nodes, virtual_node_count);
  free(virtual_nodes);
}

int check_structure_alignemnts()
{
    int *ptr;
    if (sizeof(ptr) != 4) {
        printf("Error: This program is developed for 32bit systems\n");
        return 1;
    }
    if ((offsetof(rd_ep_data_store_entry_ancient_t, state) != 4) || offsetof(rd_ep_data_store_entry_ancient_t, iconID) != 8) {
        printf("-------------error. Eeprom conversion failed. Eeprom file should not be used\n");
        return 1;
    }
    if(sizeof(rd_ep_data_store_entry_v0_t) != 16) {
        printf("-------------error. Eeprom conversion failed. Eeprom file should not be used\n");
        return 1;
    }
    if ((offsetof(rd_ep_data_store_entry_v0_t, endpoint_aggr_len) != 3)) {
        printf("-------------error. Eeprom conversion failed. Eeprom file should not be used\n");
        return 1;
    }
    if ((offsetof(rd_ep_data_store_entry_v0_t, state) != 8) || (offsetof(rd_ep_data_store_entry_v0_t, iconID) != 12)) {
        printf("-------------error. Eeprom conversion failed. Eeprom file should not be used2\n");
        return 1;
    }
    if (sizeof(rd_node_database_entry_v0_t) != sizeof(rd_node_database_entry_ancient_t)) {
        printf("-------------error. Eeprom conversion failed. Eeprom file should not be used1\n");
        return 1;
    }
    return 0;
}

void print_v0_eeprom(int f)
{
    uint16_t n_ptr, e_ptr;
    rd_node_database_entry_v0_t r;
    rd_ep_data_store_entry_v0_t e;
    int i, j;
    size = lseek(f, 0, SEEK_END);
    const uint16_t static_size = offsetof(rd_node_database_entry_v0_t, nodename);
    for (i = 1; i < ZW_MAX_NODES; i++) {
        rd_eeprom_read(offsetof(rd_eeprom_static_hdr_v0_t, node_ptrs[i]), sizeof(uint16_t), &n_ptr);
        if(n_ptr == 0) {
            continue;
        }
        printf("------------------------------------\n");
        rd_eeprom_read(n_ptr, static_size , &r);
        printf("n_ptr %x node ID %d\n", n_ptr, r.nodeid);
        printf("\twake up interval %d\n\tlast awake %d\n\tlast update %d\n", r.wakeUp_interval, r.lastAwake, r.lastUpdate);
        printf("\tipv6 address %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n", ENDIAN(r.ipv6_address.u16[0]), ENDIAN(r.ipv6_address.u16[1]), ENDIAN(r.ipv6_address.u16[2]), ENDIAN(r.ipv6_address.u16[3]), ENDIAN(r.ipv6_address.u16[4]), ENDIAN(r.ipv6_address.u16[5]), ENDIAN(r.ipv6_address.u16[6]), ENDIAN(r.ipv6_address.u16[7]));
        printf("\tsecurity flags %02x\n\tmode %02x\n\tstate %02x\n\tmanufacturerID %04x\n\tproduct type %04x\n\tproduct ID %04x\n\tnode type %02x\n\tref count %02x\n\tnEndpoints %02x\n\tnAggEndpoints %02x\n\tnode name length %02x\n", r.security_flags, r.mode, r.state, r.manufacturerID, r.productType, r.productID, r.nodeType, r.refCnt, r.nEndpoints, r.nAggEndpoints, r.nodeNameLen);

        n_ptr = n_ptr + static_size + r.nodeNameLen;

        for(j = 0; j < r.nEndpoints; j++) {
            rd_eeprom_read(n_ptr, sizeof(uint16_t), &e_ptr);
            printf("\t\te_ptr %x\n", e_ptr);

            rd_eeprom_read(e_ptr, sizeof(rd_ep_data_store_entry_v0_t), &e);
            printf("\t\tendpoint ID %d\n\t\tendpoint info len %d\n\t\tendpoint name len %d\n\t\tendpoint location len %d\n\t\tendpoint aggr len %d\n\t\tstate %d\n\t\ticon ID %d\n", e.endpoint_id, e.endpoint_info_len, e.endpoint_name_len, e.endpoint_loc_len, e.endpoint_aggr_len, e.state, e.iconID);

            n_ptr+=sizeof(uint16_t);
        }
    }
    print_virtual_nodes();
}

void print_v20_eeprom(int f)
{
  uint16_t n_ptr, e_ptr;
  rd_node_database_entry_v20_t r;
  rd_ep_data_store_entry_v20_t e;
  int i, j;
  size = lseek(f, 0, SEEK_END);
  const uint16_t static_size = offsetof(rd_node_database_entry_v20_t, nodename);
  for (i = 1; i < ZW_MAX_NODES; i++) {
    rd_eeprom_read(offsetof(rd_eeprom_static_hdr_v20_t, node_ptrs[i]), sizeof(uint16_t), &n_ptr);
    if(n_ptr == 0) {
      continue;
    }
    printf("------------------------------------\n");
    rd_eeprom_read(n_ptr, static_size , &r);
    printf("n_ptr %04x node ID %d\n", n_ptr, r.nodeid);
    printf("\twake up interval %d\n\tlast awake %d\n\tlast update %d\n", r.wakeUp_interval, r.lastAwake, r.lastUpdate);
    printf("\tipv6 address %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n", ENDIAN(r.ipv6_address.u16[0]), ENDIAN(r.ipv6_address.u16[1]), ENDIAN(r.ipv6_address.u16[2]), ENDIAN(r.ipv6_address.u16[3]), ENDIAN(r.ipv6_address.u16[4]), ENDIAN(r.ipv6_address.u16[5]), ENDIAN(r.ipv6_address.u16[6]), ENDIAN(r.ipv6_address.u16[7]));
    printf("\tsecurity flags %02x\n\tmode %d\n\tstate %d\n\tmanufacturerID %d\n\tproduct type %d\n\tproduct ID %d\n\tnode type %d\n\tref count %d\n\tnEndpoints %d\n\tnAggEndpoints %d\n\tnode name length %d\n\tdsk length %d\n", r.security_flags, r.mode, r.state, r.manufacturerID, r.productType, r.productID, r.nodeType, r.refCnt, r.nEndpoints, r.nAggEndpoints, r.nodeNameLen, r.dskLen);

    n_ptr = n_ptr + static_size + r.nodeNameLen + r.dskLen;

    for(j = 0; j < r.nEndpoints; j++) {
      rd_eeprom_read(n_ptr, sizeof(uint16_t), &e_ptr);
      printf("\t\te_ptr %04x\n", e_ptr);

      rd_eeprom_read(e_ptr, sizeof(rd_ep_data_store_entry_v20_t), &e);
      printf("\t\tendpoint ID %d\n\t\tendpoint info len %d\n\t\tendpoint name len %d\n\t\tendpoint location len %d\n\t\tendpoint aggr len %d\n\t\tstate %d\n\t\ticon ID %d\n", e.endpoint_id, e.endpoint_info_len, e.endpoint_name_len, e.endpoint_loc_len, e.endpoint_aggr_len, e.state, e.iconID);

      n_ptr+=sizeof(uint16_t);
    }
  }
  print_virtual_nodes();
}

void print_node_cc_version(rd_node_database_entry_v21_t n)
{
  int i = 0;
  if(!n.node_cc_versions)
    return;
  printf("\tNode CC version list\n");
  int cnt = n.node_cc_versions_len / sizeof(cc_version_pair_t);
  for(i = 0; i < cnt; i++) {
    printf("\t\tCC: %04x, Version: %04x\n", n.node_cc_versions[i].command_class, n.node_cc_versions[i].version);
  }
}



void print_v21_eeprom(int f)
{
    uint16_t n_ptr[2], e_ptr;
    rd_node_database_entry_v21_t r;
    rd_ep_data_store_entry_v21_t e;
    int i, j;
    size = lseek(f, 0, SEEK_END);
    const uint16_t static_size = offsetof(rd_node_database_entry_v21_t, nodename);
    for (i = 1; i < ZW_MAX_NODES; i++) {
        rd_eeprom_read(offsetof(rd_eeprom_static_hdr_v21_t, node_ptrs[i]), sizeof(uint16_t), &n_ptr[0]);
        if(n_ptr[0] == 0) {
            continue;
        }
        printf("------------------------------------\n");
        rd_eeprom_read(n_ptr[0], static_size , &r);
        printf("n_ptr %04x node ID %d\n", n_ptr[0], r.nodeid);
        printf("\twake up interval %d\n\tlast awake %d\n\tlast update %d\n", r.wakeUp_interval, r.lastAwake, r.lastUpdate);
        printf("\tipv6 address %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n", ENDIAN(r.ipv6_address.u16[0]), ENDIAN(r.ipv6_address.u16[1]), ENDIAN(r.ipv6_address.u16[2]), ENDIAN(r.ipv6_address.u16[3]), ENDIAN(r.ipv6_address.u16[4]), ENDIAN(r.ipv6_address.u16[5]), ENDIAN(r.ipv6_address.u16[6]), ENDIAN(r.ipv6_address.u16[7]));
        printf("\tsecurity flags %02x\n\tmode %d\n\tstate %d\n\tmanufacturerID %d\n\tproduct type %d\n\tproduct ID %d\n\tnode type %d\n\tref count %d\n\tnEndpoints %d\n\tnAggEndpoints %d\n\tnode name length %d\n\tdsk length %d\n", r.security_flags, r.mode, r.state, r.manufacturerID, r.productType, r.productID, r.nodeType, r.refCnt, r.nEndpoints, r.nAggEndpoints, r.nodeNameLen, r.dskLen);
        printf("\tcap_report %02x\n\tprobe_flags %04x\n\tnode_properties_flags %04x\n\tnode_cc_versions_len %d\n\tnode_is_zws_probed %02x\n", r.node_version_cap_and_zwave_sw, r.probe_flags, r.node_properties_flags, r.node_cc_versions_len, r.node_is_zws_probed);

        rd_eeprom_read(n_ptr[0] + static_size, r.node_cc_versions_len, r.node_cc_versions);
        print_node_cc_version(r);
        rd_eeprom_read(n_ptr[0] + static_size + r.node_cc_versions_len, sizeof(uint16_t), &n_ptr[1]);
        n_ptr[1] = n_ptr[1] + r.nodeNameLen + r.dskLen;

        printf("\tEndpoints:\n");
        for(j = 0; j < r.nEndpoints; j++) {
            rd_eeprom_read(n_ptr[1], sizeof(uint16_t), &e_ptr);
            printf("\t\te_ptr %04x\n", e_ptr);

            rd_eeprom_read(e_ptr, sizeof(rd_ep_data_store_entry_v21_t), &e);
            printf("\t\tendpoint ID %d\n\t\tendpoint info len %d\n\t\tendpoint name len %d\n\t\tendpoint location len %d\n\t\tendpoint aggr len %d\n\t\tstate %d\n\t\ticon ID %d\n", e.endpoint_id, e.endpoint_info_len, e.endpoint_name_len, e.endpoint_loc_len, e.endpoint_aggr_len, e.state, e.iconID);

            n_ptr[1]+=sizeof(uint16_t);
        }
    }
    print_virtual_nodes();
}

void print_v22_eeprom(int f)
{
  uint16_t n_ptr, e_ptr;
  rd_node_database_entry_v22_t r;
  rd_ep_data_store_entry_v22_t e;
  int i, j;
  size = lseek(f, 0, SEEK_END);
  const uint16_t static_size = offsetof(rd_node_database_entry_v22_t, nodename);
  for (i = 0; i < ZW_MAX_NODES; i++) {
    rd_eeprom_read(offsetof(rd_eeprom_static_hdr_v22_t, node_ptrs[i]), sizeof(uint16_t), &n_ptr);
    if(n_ptr == 0) {
      continue;
    }
    printf("------------------------------------\n");
    rd_eeprom_read(n_ptr, static_size , &r);
    printf("n_ptr %04x node ID %d\n", n_ptr, r.nodeid);
    printf("\twake up interval %d\n\tlast awake %d\n\tlast update %d\n", r.wakeUp_interval, r.lastAwake, r.lastUpdate);
    printf("\tipv6 address %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n", ENDIAN(r.ipv6_address.u16[0]), ENDIAN(r.ipv6_address.u16[1]), ENDIAN(r.ipv6_address.u16[2]), ENDIAN(r.ipv6_address.u16[3]), ENDIAN(r.ipv6_address.u16[4]), ENDIAN(r.ipv6_address.u16[5]), ENDIAN(r.ipv6_address.u16[6]), ENDIAN(r.ipv6_address.u16[7]));
    printf("\tsecurity flags %02x\n\tmode %d\n\tstate %d\n\tmanufacturerID %d\n\tproduct type %d\n\tproduct ID %d\n\tnode type %d\n\tref count %d\n\tnEndpoints %d\n\tnAggEndpoints %d\n\tnode name length %d\n\tdsk length %d\n", r.security_flags, r.mode, r.state, r.manufacturerID, r.productType, r.productID, r.nodeType, r.refCnt, r.nEndpoints, r.nAggEndpoints, r.nodeNameLen, r.dskLen);

    n_ptr = n_ptr + static_size + r.nodeNameLen + r.dskLen;

    for(j = 0; j < r.nEndpoints; j++) {
      rd_eeprom_read(n_ptr, sizeof(uint16_t), &e_ptr);
      printf("\t\te_ptr %04x\n", e_ptr);

      rd_eeprom_read(e_ptr, sizeof(rd_ep_data_store_entry_v22_t), &e);
      printf("\t\tendpoint ID %d\n\t\tendpoint info len %d\n\t\tendpoint name len %d\n\t\tendpoint location len %d\n\t\tendpoint aggr len %d\n\t\tstate %d\n\t\ticon ID %d\n", e.endpoint_id, e.endpoint_info_len, e.endpoint_name_len, e.endpoint_loc_len, e.endpoint_aggr_len, e.state, e.iconID);

      n_ptr+=sizeof(uint16_t);
    }
  }
  print_virtual_nodes();
}

void print_v23_eeprom(int f)
{
  uint16_t n_ptr[2], e_ptr;
  rd_node_database_entry_v23_t r;
  rd_ep_data_store_entry_v23_t e;
  int i, j;
  size = lseek(f, 0, SEEK_END);
  const uint16_t static_size = offsetof(rd_node_database_entry_v23_t, nodename);
  for (i = 0; i < ZW_MAX_NODES; i++) {
    rd_eeprom_read(offsetof(rd_eeprom_static_hdr_v23_t, node_ptrs[i]), sizeof(uint16_t), &n_ptr[0]);
    if(n_ptr[0] == 0) {
      continue;
    }
    printf("------------------------------------\n");
    rd_eeprom_read(n_ptr[0], static_size , &r);
    printf("n_ptr %04x node ID %d\n", n_ptr[0], r.nodeid);
    printf("\twake up interval %d\n\tlast awake %d\n\tlast update %d\n", r.wakeUp_interval, r.lastAwake, r.lastUpdate);
    printf("\tipv6 address %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n", ENDIAN(r.ipv6_address.u16[0]), ENDIAN(r.ipv6_address.u16[1]), ENDIAN(r.ipv6_address.u16[2]), ENDIAN(r.ipv6_address.u16[3]), ENDIAN(r.ipv6_address.u16[4]), ENDIAN(r.ipv6_address.u16[5]), ENDIAN(r.ipv6_address.u16[6]), ENDIAN(r.ipv6_address.u16[7]));
    printf("\tsecurity flags %02x\n\tmode %d\n\tstate %d\n\tmanufacturerID %d\n\tproduct type %d\n\tproduct ID %d\n\tnode type %d\n\tref count %d\n\tnEndpoints %d\n\tnAggEndpoints %d\n\tnode name length %d\n\tdsk length %d\n", r.security_flags, r.mode, r.state, r.manufacturerID, r.productType, r.productID, r.nodeType, r.refCnt, r.nEndpoints, r.nAggEndpoints, r.nodeNameLen, r.dskLen);
    printf("\tcap_report %02x\n\tprobe_flags %04x\n\tnode_properties_flags %04x\n\tnode_cc_versions_len %d\n\tnode_is_zws_probed %02x\n", r.node_version_cap_and_zwave_sw, r.probe_flags, r.node_properties_flags, r.node_cc_versions_len, r.node_is_zws_probed);

    r.node_cc_versions = malloc(r.node_cc_versions_len);
    rd_node_cc_versions_set_default(&r);

    rd_eeprom_read(n_ptr[0] + static_size, r.node_cc_versions_len, r.node_cc_versions);
    print_node_cc_version(r);
    rd_eeprom_read(n_ptr[0] + static_size + r.node_cc_versions_len, sizeof(uint16_t), &n_ptr[1]);
    n_ptr[1] = n_ptr[1] + r.nodeNameLen + r.dskLen;

    printf("\tEndpoints:\n");
    for(j = 0; j < r.nEndpoints; j++) {
      rd_eeprom_read(n_ptr[1], sizeof(uint16_t), &e_ptr);
      printf("\t\te_ptr %04x\n", e_ptr);

      rd_eeprom_read(e_ptr, sizeof(rd_ep_data_store_entry_v23_t), &e);
      printf("\t\tendpoint ID %d\n\t\tendpoint info len %d\n\t\tendpoint name len %d\n\t\tendpoint location len %d"
          "\n\t\tendpoint aggr len %d\n\t\tstate %d\n\t\ticon ID %d\n",
          e.endpoint_id, e.endpoint_info_len, e.endpoint_name_len, e.endpoint_loc_len, e.endpoint_aggr_len, e.state, e.iconID);


      n_ptr[1]+=sizeof(uint16_t);
    }
  }
  print_virtual_nodes();
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
    int i, j;

    if (argc < 2) {
        printf("Usage: eeprom_printer <eeprom_file_path>\n");
        printf("Format will be decided based on the version in eeprom file. Note that eeprom without version is not supported by this printer.\n");
        exit(1);
    }
    linux_conf_eeprom_file = argv[1];
    printf("Opening eeprom file %s\n", linux_conf_eeprom_file);
    f = open(linux_conf_eeprom_file, O_RDWR | O_CREAT, 0644);

    if(f < 0) {
        fprintf(stderr, "Error opening eeprom file %s\n.", linux_conf_eeprom_file);
        perror("");
        exit(1);
    }

    size = lseek(f, 0, SEEK_END);
    buf = malloc(size + 64000);
    eeprom_buf = malloc(size);

    if(check_structure_alignemnts()) {
        exit(1);
    }

    rd_eeprom_static_hdr_v20_t static_hdr;
    rd_eeprom_read(0, offsetof(rd_eeprom_static_hdr_v20_t, flags), &static_hdr);

    printf("MAGIC %08x version %d.%d\n", static_hdr.magic, static_hdr.version_major, static_hdr.version_minor);
    printf("Home ID %08x My node ID %02x\n", static_hdr.homeID, static_hdr.nodeID);
    if(static_hdr.magic == RD_MAGIC_V0) {
      print_v0_eeprom(f);
    } else if(static_hdr.magic == RD_MAGIC_V1
        && ((static_hdr.version_major == 0 && static_hdr.version_minor == 0)
          || (static_hdr.version_major == 2 && static_hdr.version_minor == 0))) {
      print_v20_eeprom(f);
    } else if(static_hdr.magic == RD_MAGIC_V1
        && static_hdr.version_major == 2
        && static_hdr.version_minor == 1) {
      print_v21_eeprom(f);
    } else if(static_hdr.magic == RD_MAGIC_V1
        && static_hdr.version_major == 2
        && static_hdr.version_minor == 2) {
      print_v22_eeprom(f);
    } else if(static_hdr.magic == RD_MAGIC_V1
        && static_hdr.version_major == 2
        && static_hdr.version_minor == 3) {
      print_v23_eeprom(f);
    } else if(static_hdr.magic == RD_MAGIC_V1
        && static_hdr.version_major == 2
        && static_hdr.version_minor == 4) {
      print_v23_eeprom(f);
    } else {
      printf("Unsupported eeprom MAGIC %08x version %d.%d\n", static_hdr.magic, static_hdr.version_major, static_hdr.version_minor);
    }

    close(f);
    free(buf);
    free(eeprom_buf);
}
