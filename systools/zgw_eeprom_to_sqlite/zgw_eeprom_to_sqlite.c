

#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

#include "RD_DataStore_Eeprom.h"
#include "RD_DataStore_Eeprom20.h"
#include "RD_DataStore.h"
#include "dev/eeprom.h"
#include "ZIP_Router_logging.h"
#include "ZIP_Router.h"
#include "Bridge.h"
#include "test_helpers.h"
extern const char *linux_conf_eeprom_file;
extern const char *linux_conf_database_file;
extern uint8_t MyNodeID;
extern uint32_t homeID;

void usage(const char* progname)
{
  printf(
    "Usage %s: -e <eeprom.dat> -d <database.db> [-f] \n\n"
    "Converts eeprom.dat files form ZGW >= 2.61 to a sqlite database,\n"
    "after doing some validation of the input  eeprom.dat file.\n"
    "The program will return 0 if conversion was succsessfull.\n"
    "\n"
    "Options:\n"
    "\t-e <eeprom.dat> eeprom file to use.\n"
    "\t-d <database.db> filename of the sqlite database to generate\n"
    "\t-f Force conversion of remaining nodes despite detecting an\n"
    "\t   invalid node. This flag should be used with caution. Some\n"
    "\t   nodes may be left unconverted.\n"
    ,progname);
  exit(1);
}

int main(int argc, char *const *argv)
{
  LIST(ip_association_table);
  MEMB(ip_association_pool, ip_association_t, MAX_IP_ASSOCIATIONS);
  nodeid_t virtual_nodes[4] = {0};
  size_t virtual_node_count;
  struct stat db_stat;
  bool force = false;

  const char *eeprom_file;
  const char *database_file;
  int ch, rc = 0;

  while ((ch = getopt(argc, argv, "e:d:f")) != -1)
  {
    switch (ch)
    {
    case 'e':
      linux_conf_eeprom_file = optarg;
      break;
    case 'd':
      linux_conf_database_file = optarg;
      break;
    case 'f':
      force = true;
      break;
    case '?':
    default:
      usage(argv[0]);
    }
  }

  if ((linux_conf_eeprom_file == NULL) || (linux_conf_database_file == NULL))
  {
    usage(argv[0]);
  }

  if (stat(linux_conf_database_file, &db_stat) == 0)
  {
    ERR_PRINTF("%s already exists\n", linux_conf_database_file);
    return 1;
  }

  LOG_PRINTF("zgw_eeprom_to_sqlite build " PACKAGE_SVN_REVISION "\n");
  eeprom_init();

  int version = rd_eeprom_version_get_legacy();
  LOG_PRINTF("Detected eeprom version %i.%i\n", version / 100, version % 100);

  /* 2.81.00-2.81.01 would leave the version field at 0, and this will be carried over in
    upgrades up to and including 2.81.02. */
  if ((rd_eeprom_magic_get() == RD_MAGIC_V1) && (version == 0)) {
    // is_smalloc_consistent() expects the correct version number, let's correct it
    LOG_PRINTF("This is a 2.81.02 eeprom upgraded from 2.81.01/02 - correcting version from 0 to 2.00\n");
    version = 200;
  }

  if (!rd_data_store_validate(version))
  {
    ERR_PRINTF("eeprom file seems corrupt\n");
    eeprom_close();
    return 1;
  }

  homeID = rd_eeprom_homeid_get();
  MyNodeID = rd_eeprom_nodeid_get();

  if (rd_eeprom_version_get_legacy() == RD_MAGIC_V0)
  {
    WRN_PRINTF("Converting eeprom v0 to v2.0\n");
    if (!data_store_convert_none_to_2_0(rd_eeprom_nodeid_get()))
    {
      ERR_PRINTF("Conversion failed\n");
      return 1;
    }
    version = rd_eeprom_version_get_legacy();
  }

  LOG_PRINTF("Controller network info HomeID:%08X  NodeID:%i\n", homeID, MyNodeID);

  data_store_init();

  //Validate homeid and node id
  check_equal(rd_zgw_homeid_get(), rd_eeprom_homeid_get(),"Checking HomeID." );
  check_equal(rd_zgw_nodeid_get(), rd_eeprom_nodeid_get(),"Checking NodeID." );
  if(numErrs) {
    rc = 1;    
    goto exit;
  }

  for (int i = 1; i < ZW_CLASSIC_MAX_NODES + 1; i++)
  {
    rd_node_database_entry_t *n = 0;

    if ((version == 200) && rd_eeprom_magic_get() == RD_MAGIC_V1)
    {
      //We know the old 20 format does not support 232 nodes.
      if(i == ZW_CLASSIC_MAX_NODES) continue;

      n = rd_data_store_read_2_0(i);
    }
    else if (version == 201)
    {
      n = rd_data_store_read_2_1(i);
    }
    else if (version == 202)
    {
      n = rd_data_store_read_2_2(i);
    }
    else if (version == 203)
    {
      n = rd_data_store_read_2_3(i);
    }
    else
    {
      ERR_PRINTF("Unsupported eeprom version");
      return 1;
    }

    if (n)
    {
      if (node_sanity_check(n, i) == false) {
        ERR_PRINTF("Node %u at index %d failed sanity check\n", n->nodeid, i);
        rc = 1;
        rd_data_store_mem_free(n);
        if (force == false) {
          LOG_PRINTF("Flushing database\n");
          rd_data_store_invalidate();
          goto exit;
        }
      } else {
        rd_data_store_nvm_write(n);
        rd_data_store_mem_free(n);
      }
    }

    // Do same verification as done in test/eeprom_v20_to_v23_converter 
    // delivered to customer July 2020.
    if(version==200) {
      rd_node_database_entry_v20_t *n20 = rd_data_store_read_v20(i);
      rd_node_database_entry_v23_t* n_sql = (rd_node_database_entry_v23_t*)rd_data_store_read(i);

      int nerrors = rd_data_store_entry_compare_v20_v23(n20,n_sql);

      if(n20) {
        rd_data_store_mem_free_v20(n20);
      }
      if(n_sql) {
        rd_data_store_mem_free((rd_node_database_entry_t*)n_sql);
      }

      if (nerrors)
      {
        rc = 1;
        ERR_PRINTF("Compare with original v20 reader failed\n");
        if(!force) {
          rd_data_store_invalidate();
          goto exit;
        }
      }
    }
  }
  

  eeprom_datastore_unpersist_association(
      ip_association_table, &ip_association_pool);
  rd_data_store_persist_associations(ip_association_table);

  virtual_node_count =
      eeprom_datastore_unpersist_virtual_nodes(
          virtual_nodes, sizeof(virtual_nodes));
  int i = 0;
  for (i = 0; i < virtual_node_count; i++) {
      if (virtual_nodes[i] == 0) {
          ERR_PRINTF("ERROR:One of the virtual node id in the eeprom file was 0. Exiting.\n");
          rc = 1;
          goto exit;
      }
  }
  rd_datastore_persist_virtual_nodes(virtual_nodes, virtual_node_count);

  LOG_PRINTF("Conversion complete\n");

exit:
  data_store_exit();
  eeprom_close();

  return rc;
}
