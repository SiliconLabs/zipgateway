/* © 2020 Silicon Laboratories Inc. */

#include "ResourceDirectory.h"
#include "RD_types.h"
#include <stdlib.h>

rd_node_database_entry_t *ndb[255];

rd_node_database_entry_t *rd_lookup_by_dsk(uint8_t dsklen, const uint8_t *dsk)
{
  for (int i = 0; i < 255; i++)
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
  ndb[node]->dskLen = dsklen;
  ndb[node]->dsk = malloc(dsklen);
  memcpy(ndb[node]->dsk, dsk, dsklen);
}

rd_node_database_entry_t *rd_node_entry_alloc(nodeid_t nodeid)
{

  ndb[nodeid] = (rd_node_database_entry_t *)calloc(1, sizeof(rd_node_database_entry_t));

  ndb[nodeid]->nodeid = nodeid;
  LIST_STRUCT_INIT(ndb[nodeid], endpoints);
  return ndb[nodeid];
}

rd_node_database_entry_t *rd_node_get_raw(nodeid_t nodeid)
{
  return ndb[nodeid];
}
