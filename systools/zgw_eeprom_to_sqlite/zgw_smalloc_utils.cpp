#include <set>
#include <stddef.h>
#include <algorithm>
#include "zgw_smalloc_utils.h"
#include "RD_DataStore_Eeprom.h"
#include "ZIP_Router_logging.h"
#include "DataStore.h"

#define CHUNK_USED 0x80
#define MIN_CHUNK_SIZE 4

/**
 * For storing the first byte of each chunk, which is CHUNK_USED | 7-bit length
 *
 * all_pointers - including nodes and endpoints
 */
static std::set<uint16_t> all_pointers;
/**
 * for all allocated smalloc chunk, which is supposed to equal to all_pointers
 */
static std::set<uint16_t> allocated_chunk;

bool is_smalloc_consistent(const small_memory_device_t *dev, const uint16_t *node_ptrs,
                                  uint8_t length, int eeprom_version) {
  uint16_t p, end, ep;
  uint8_t v;
  bool r = true;
  p = dev->offset;
  end = (dev->offset) + dev->size - 1;
  /**
   * Test 1:
   * See if we have consistent smalloc area, based on CHUNK_USED|7-bit length|data
   * signature; however, none of the cases seem to be fit for inserting a
   * statement r = false; All smalloc space shall pass this test. */
  while (p <= end) {
    dev->read(p, 1, &v);
    /* Reach the end of allocated area */
    if (v == 0) {
      break;
    }
    LOG_PRINTF("Found allocated chunk at %04x flag %02x\n", p, v);
    /* Gather the allocated chunk address */
    if (v & CHUNK_USED) {
      allocated_chunk.insert(p);
    }
    p = p + (v & 0x7F) + 1;
  }
  if (p > end) {
    return false;
  }

  /**
   * Test 2:
   * Check one chunk is allocated if and only if one node or endpoint needs it.
   */
  uint8_t i;
  if (eeprom_version == 200 || eeprom_version == 201) {
    i = 1;
  } else { // eeprom_version 202 and 203
    i = 0;
  }
  for (; i<length; i++) {
    if (node_ptrs[i] != 0) {
      /* Get the "flag" byte of each node */
      dev->read(node_ptrs[i] - 1, 1, &v);
      LOG_PRINTF("Found node at %04x flag %02x\n", node_ptrs[i] - 1, v);
      if (v & CHUNK_USED) {
        all_pointers.insert(node_ptrs[i] - 1);

        /* Now we look for the flag byte of each endpointer */
        rd_node_database_entry_t *n = NULL;
        switch (eeprom_version)
        {
          case 200:
            n = rd_data_store_read_2_0(i);
            break;
          case 201:
            n = rd_data_store_read_2_1(i);
            break;
          case 202:
            n = rd_data_store_read_2_2(i + 1);
            break;
          case 203:
            n = rd_data_store_read_2_3(i + 1);
            break;
          default:
            LOG_PRINTF("Unsupported version number\n");
            return 0;
            break;
        }
        /* Beginning of endpoint pointer array, based on different verions */
        uint16_t n_to_ep = 0;
        if (eeprom_version == 200 || eeprom_version == 202) {
          n_to_ep = node_ptrs[i]
                  + offsetof(rd_node_database_entry_v20_t, nodename)
                  + n->nodeNameLen
                  + n->dskLen;
        } else { // eeprom_version == 202 or 203
          uint16_t nptr_2 = 0;
          dev->read(node_ptrs[i]
                  + offsetof(rd_node_database_entry_t, nodename)
                  + n->node_cc_versions_len, sizeof(uint16_t), &nptr_2);
          all_pointers.insert(nptr_2 - 1);
          n_to_ep = nptr_2 + n->nodeNameLen + n->dskLen;
        }
        for (uint8_t epid=0; epid<n->nEndpoints; epid++) {
          dev->read(n_to_ep, sizeof(uint16_t), &ep);
          LOG_PRINTF("Found endpoint at %04x\n", ep - 1);
          all_pointers.insert(ep - 1);
          n_to_ep += sizeof(uint16_t);
        }
        rd_data_store_mem_free(n);
      }
    }
  }
  r = r & (std::includes(allocated_chunk.begin(), allocated_chunk.end(),
                         all_pointers.begin(), all_pointers.end()));
  return r;
}

