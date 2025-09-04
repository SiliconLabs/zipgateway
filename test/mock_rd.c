/* © 2020 Silicon Laboratories Inc. */

#include "ResourceDirectory.h"
#include "RD_types.h"
#include <stdlib.h>
#include <assert.h>
#include <ZIP_Router_logging.h>

rd_node_database_entry_t *ndb[ZW_MAX_NODES];

rd_node_database_entry_t *rd_lookup_by_dsk(uint8_t dsklen, const uint8_t *dsk)
{
  if (dsklen == 0 || dsk == NULL)
  {
    return NULL;
  }
  
  for (int i = 0; i < ZW_MAX_NODES; i++)
  {
    if (ndb[i] && ndb[i]->dsk &&
        (memcmp(ndb[i]->dsk, dsk, dsklen) == 0))
    {
      return ndb[i];
    }
  }
  return NULL;
}

void rd_node_add_dsk(nodeid_t node, uint8_t dsklen, const uint8_t *dsk)
{
  if ((dsklen == 0) || (dsk == NULL)) {
    return;
  }
  
  rd_node_database_entry_t *nd = rd_node_get_raw(node);
  if (nd) {
    nd->dskLen = dsklen;
    nd->dsk = malloc(dsklen);
    memcpy(nd->dsk, dsk, dsklen);
  }
}

rd_node_database_entry_t *rd_node_entry_alloc(nodeid_t nodeid)
{
  rd_node_database_entry_t* nd = (rd_node_database_entry_t *)calloc(1, sizeof(rd_node_database_entry_t));
  if (nd) {
    ndb[nodeid - 1 ] = nd;
    nd->nodeid = nodeid;
    LIST_STRUCT_INIT(nd, endpoints);
  }
  return nd;
}

rd_node_database_entry_t *rd_node_get_raw(nodeid_t nodeid)
{
  // Fix ZGW-3423: Segmentation fault if SAPI controller NVM cleared  
  // Use assert to catch invalid nodeid
  assert((nodeid >= 1 && nodeid <= ZW_MAX_NODES) && "Invalid nodeid out of range [1:ZW_MAX_NODES]!");
  
  return ndb[nodeid - 1];
}
