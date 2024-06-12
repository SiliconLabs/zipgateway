#include "dev/eeprom.h"
#include "RD_internal.h"

void eeprom_read(eeprom_addr_t addr, unsigned char *buf, int size) {
   return;
}

void eeprom_write(eeprom_addr_t addr, unsigned char *buf, int size) {
   return;
}

/* TODO: make this return value settable from the test case */
eeprom_addr_t eeprom_size()
{
   return 42;
}

void bridge_reset()
{
  /* bridge_clear_association_table(); */
  /* temp_assoc_virtual_nodeid_count = 0; */
  /* memset(temp_assoc_virtual_nodeids, 0, sizeof(temp_assoc_virtual_nodeids)); */
  /* persist_temp_virtual_nodes(); */
  /* bridge_state = booting; */
   return;
}

void mdns_remove_from_answers(rd_ep_database_entry_t* ep) {
   return;
}
