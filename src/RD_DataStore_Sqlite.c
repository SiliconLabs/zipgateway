/* © 2019 Silicon Laboratories Inc.  */
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>

#include "RD_DataStore.h"
#include "ZIP_Router_logging.h"
#include "zw_network_info.h" /* MyNodeID */
#include "Bridge.h"
#include "zip_router_config.h"
#include "Bridge_ip_assoc.h"
#include "memb.h"
#include "DataStore.h"
#include "s2_protocol.h"

static sqlite3 *db = NULL;
static sqlite3_stmt *node_select_stmt = NULL;
static sqlite3_stmt *ep_select_stmt = NULL;
static sqlite3_stmt *node_insert_stmt = NULL;
static sqlite3_stmt *ep_insert_stmt = NULL;
extern const char *linux_conf_database_file;

#define DATA_BASE_SCHEMA_VERSION_MAJOR 1
#define DATA_BASE_SCHEMA_VERSION_MINOR 0

//Convenience  macros, it turns out that sqlite_bind_xxx is
// 1 indexed and the sqlite3_column_xxx functions are all 0 indexed
#define sqlite3_bind_ex_int(stmt, idx, val) \
  sqlite3_bind_int(stmt, idx + 1, val)
#define sqlite3_bind_ex_blob(stmt, idx, ptr, sz, func) \
  sqlite3_bind_blob(stmt, idx + 1, ptr, sz, func)

//Node table columns
enum
{
  nt_col_nodeid,
  nt_col_wakeUp_interval,
  nt_col_lastAwake,
  nt_col_lastUpdate,
  nt_col_security_flags,
  nt_col_mode,
  nt_col_state,
  nt_col_manufacturerID,
  nt_col_productType,
  nt_col_productID,
  nt_col_nodeType,
  nt_col_nAggEndpoints,
  nt_col_name,
  nt_col_dsk,
  nt_col_node_version_cap_and_zwave_sw,
  nt_col_probe_flags,
  nt_col_properties_flags,
  nt_col_cc_versions,
  nt_col_node_is_zws_probed,
};

//Endpoint table columns
enum
{
  et_col_endpointid,
  et_col_nodeid,
  et_col_info,
  et_col_aggr,
  et_col_name,
  et_col_location,
  et_col_state,
  et_col_installer_iconID,
  et_col_user_iconID,
};

// Network table columns
enum
{
  nw_col_homeid,
  nw_col_nodeid,
  nw_col_version_major,
  nw_col_version_minor,
};

// IP association columns
enum
{
  ipa_col_virtual_id,
  ipa_col_type,
  ipa_col_resource_ip,
  ipa_col_resource_endpoint,
  ipa_col_resource_port,
  ipa_col_virtual_endpoint,
  ipa_col_grouping,
  ipa_col_han_nodeid,
  ipa_col_han_endpoint,
  ipa_col_was_dtls,
  ipa_col_mark_removal,
};

enum
{
  vn_col_virtual_id,
};

enum
{
  gw_col_mode,
  gw_col_showlock,
  gw_col_peerProfile,
};

enum
{
  peer_col_id,
  peer_col_ip,
  peer_col_port,
  peer_col_name,
};

enum
{
  span_col_d,
  span_col_lnode,
  span_col_rnode,
  span_col_rx_seq,
  span_col_tx_seq,
  span_col_class_id,
  span_col_state
};

/**
 * @brief  Helper function to execute SQL statement and check for errors
 *
 * @param sql SQL to execute
 * @return int sqlite3 error code
 */
static int
datastore_exec_sql(const char *sql)
{
  char *err_msg = NULL;
  int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
  if (rc != SQLITE_OK)
  {
    if (err_msg != NULL)
    {
      ERR_PRINTF("SQL Error: %s\n", err_msg);
    }
    else
    {
      ERR_PRINTF("SQL Error: Unknown\n");
    }
    sqlite3_free(err_msg);
  }
  return rc;
}

static void clear_database()
{
  sqlite3_stmt *stmt = NULL;

  WRN_PRINTF("Clearing database HomeID=%08X nodeID=%i\n", homeID, MyNodeID);
  rd_data_store_invalidate();
  sqlite3_prepare_v2(db, "INSERT INTO network VALUES(?,?,?,?)", -1, &stmt, NULL);
  //Initialize database with network info and database schema version.
  sqlite3_bind_ex_int(stmt, nw_col_homeid, homeID);
  sqlite3_bind_ex_int(stmt, nw_col_nodeid, MyNodeID);
  sqlite3_bind_ex_int(stmt, nw_col_version_major, DATA_BASE_SCHEMA_VERSION_MAJOR);
  sqlite3_bind_ex_int(stmt, nw_col_version_minor, DATA_BASE_SCHEMA_VERSION_MINOR);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  bridge_reset();
}

bool prepare_statements()
{
  int rc;
  const char *sql;

  rc = sqlite3_prepare_v2(db, "SELECT * FROM nodes WHERE nodeid = ?", -1, &node_select_stmt, NULL);
  if (rc != SQLITE_OK)
  {
    goto fail;
  }

  rc = sqlite3_prepare_v2(db, "SELECT * FROM endpoints WHERE nodeid = ?", -1, &ep_select_stmt, NULL);
  if (rc != SQLITE_OK)
  {
    goto fail;
  }

  sql = "INSERT INTO nodes VALUES(?, ?,?, ?,?, ?,?, ?,?, ?,?, ?,?, ?,?,?,?,?,?)";
  rc = sqlite3_prepare_v2(db, sql, -1, &node_insert_stmt, NULL);
  if (rc != SQLITE_OK)
  {
    goto fail;
  }

  sql = "INSERT INTO endpoints VALUES(?,?,?,?,?,?,?,?,?)";
  rc = sqlite3_prepare_v2(db, sql, -1, &ep_insert_stmt, NULL);
  if (rc != SQLITE_OK)
  {
    goto fail;
  }

  return true;
fail:
  ERR_PRINTF("prepare failed: %s\n", sqlite3_errmsg(db));
  return false;
}

bool data_store_init(void)
{
  sqlite3_stmt *stmt = NULL;
  int rc;

  if (db == 0)
  {
    LOG_PRINTF("Using db file: %s\n", linux_conf_database_file);
    int rc = sqlite3_open(linux_conf_database_file, &db);
    if (rc != SQLITE_OK)
    {
      ERR_PRINTF("Cannot open database: %s\n", sqlite3_errmsg(db));
      rc = sqlite3_close(db);
      if (rc != SQLITE_OK) {
        ERR_PRINTF("Cannot close database: %s\n", sqlite3_errmsg(db));
      }
      db = NULL;
      return false;
    }

    rc = sqlite3_prepare_v2(db, "SELECT SQLITE_VERSION()", -1, &stmt, 0);
    if (rc != SQLITE_OK)
    {
      ERR_PRINTF("Failed to fetch data: %s\n", sqlite3_errmsg(db));
      if (rc != SQLITE_OK)
      {
        goto fail;
      }
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
      LOG_PRINTF("SQLITE Version: %s\n", sqlite3_column_text(stmt, 0));
    }
    sqlite3_finalize(stmt);

    // Create tables if not exist
    rc = datastore_exec_sql("CREATE TABLE IF NOT EXISTS nodes ("
                            "nodeid INTEGER PRIMARY KEY,"
                            "wakeUp_interval  INTEGER,"
                            "lastAwake  INTEGER,"
                            "lastUpdate  INTEGER,"
                            "security_flags  INTEGER,"
                            "mode  INTEGER,"
                            "state  INTEGER,"
                            "manufacturerID  INTEGER,"
                            "productType  INTEGER,"
                            "productID  INTEGER,"
                            "nodeType  INTEGER,"
                            "nAggEndpoints  INTEGER,"
                            "name  BLOB,"
                            "dsk  BLOB,"
                            "node_version_cap_and_zwave_sw INTEGER,"
                            "probe_flags INTEGER,"
                            "properties_flags INTEGER,"
                            "cc_versions  BLOB,"
                            "node_is_zws_probed INTEGER"
                            ");");

    if (rc != SQLITE_OK)
    {
      goto fail;
    }

    rc = datastore_exec_sql("CREATE TABLE IF NOT EXISTS endpoints ("
                            "endpointid  INTEGER ,"
                            "nodeid      INTEGER KEY,"
                            "info        BLOB,"
                            "aggr        BLOB,"
                            "name        BLOB,"
                            "location    BLOB,"
                            "state       INTEGER,"
                            "installer_iconID       INTEGER,"
                            "user_iconID INTEGER"
                            ");");

    if (rc != SQLITE_OK)
    {
      goto fail;
    }

    rc = datastore_exec_sql("CREATE TABLE IF NOT EXISTS network ("
                            "homeid    INTEGER      NOT NULL UNIQUE,"
                            "nodeid    INTEGER ,"
                            "version_major        INTEGER ,"
                            "version_minor        INTEGER"
                            ");");
    if (rc != SQLITE_OK)
    {
      goto fail;
    }

    rc = datastore_exec_sql("CREATE TABLE IF NOT EXISTS ip_association ("
                            "virtual_id INTEGER,"
                            "type INTEGER,"
                            "resource_ip BLOB,"
                            "resource_endpoint INTEGER,"
                            "resource_port INTEGER,"
                            "virtual_endpoint INTEGER,"
                            "grouping INTEGER,"
                            "han_nodeid INTEGER,"
                            "han_endpoint INTEGER,"
                            "was_dtls INTEGER,"
                            "mark_removal INTEGER"
                            ");");
    if (rc != SQLITE_OK)
    {
      goto fail;
    }

    rc = datastore_exec_sql("CREATE TABLE IF NOT EXISTS virtual_nodes ("
                            "virtual_id INTEGER PRIMARY KEY"
                            ");");
    if (rc != SQLITE_OK)
    {
      goto fail;
    }

    rc = datastore_exec_sql("CREATE TABLE IF NOT EXISTS gw ("
                            "mode INTEGER,"
                            "showlock INTEGER,"
                            "peerProfile INTEGER"
                            ");");
    if (rc != SQLITE_OK)
    {
      goto fail;
    }

    rc = datastore_exec_sql("CREATE TABLE IF NOT EXISTS peer ("
                            "id INTEGER PRIMARY KEY,"
                            "ip BLOB,"
                            "port INTEGER,"
                            "name TEXT"
                            ");");
    if (rc != SQLITE_OK)
    {
      goto fail;
    }

    rc = datastore_exec_sql("CREATE TABLE IF NOT EXISTS s2_span ("
                          "d BLOB,"
                          "lnode INTEGER,"
                          "rnode INTEGER,"
                          "rx_seq INTEGER,"
                          "tx_seq INTEGER,"
                          "class_id INTEGER,"
                          "state INTEGER"
                          ");");
    if (rc != SQLITE_OK)
    {
      goto fail;
    }

    //  rc = datastore_exec_sql(
    //      "CREATE TRIGGER network_no_insert BEFORE INSERT ON network WHEN (SELECT COUNT(*) FROM network) >= 1 BEGIN SELECT RAISE(FAIL, 'only one row!'); END;");
    //  if (rc != SQLITE_OK)
    //    goto fail;
    if (!prepare_statements())
    {
      goto fail;
    }
  }

  //Check network info store in DB vs current network info
  sqlite3_prepare_v2(db, "SELECT * FROM network;", -1, &stmt, 0);

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW)
  {
    if ((sqlite3_column_int(stmt, nw_col_homeid) != homeID) ||
        (sqlite3_column_int(stmt, nw_col_nodeid) != MyNodeID))
    {
      DBG_PRINTF("%08X != %08X\n", sqlite3_column_int(stmt, nw_col_homeid), homeID);
      DBG_PRINTF("%i != %i \n", sqlite3_column_int(stmt, nw_col_nodeid), MyNodeID);

      clear_database();
    }
  }
  else
  {
    clear_database();
  }
  sqlite3_finalize(stmt);
  return true;
fail:
  rc = sqlite3_close(db);
  if (rc != SQLITE_OK) {
    ERR_PRINTF("Cannot close database: %s\n", sqlite3_errmsg(db));
  }
  db = NULL;
  return false;
}

void data_store_exit(void)
{
  sqlite3_finalize(node_select_stmt);
  node_select_stmt = NULL;
  sqlite3_finalize(ep_select_stmt);
  ep_select_stmt = NULL;
  sqlite3_finalize(node_insert_stmt);
  node_insert_stmt = NULL;
  sqlite3_finalize(ep_insert_stmt);
  ep_insert_stmt = NULL;
  int rc = sqlite3_close(db);
  if (rc != SQLITE_OK) {
    ERR_PRINTF("Cannot close database: %s\n", sqlite3_errmsg(db));
  }
  db = NULL;
}
/***************************** Node database **********************/

rd_node_database_entry_t *rd_data_store_read(nodeid_t nodeID)
{
  rd_node_database_entry_t *n = NULL;
  int rc;

  sqlite3_reset(node_select_stmt);
  sqlite3_bind_ex_int(node_select_stmt, 0, nodeID);
  int step = sqlite3_step(node_select_stmt);
  if (step != SQLITE_ROW)
  {
    DBG_PRINTF("Node ID %i not found\n", nodeID);
    return NULL;
  }
  n = rd_data_mem_alloc(sizeof(rd_node_database_entry_t));
  if (n == 0)
  {
    ERR_PRINTF("Out of memory\n");
    return NULL;
  }

  n->nodeid = sqlite3_column_int(node_select_stmt, nt_col_nodeid);
  n->wakeUp_interval = sqlite3_column_int(node_select_stmt, nt_col_wakeUp_interval);
  n->lastAwake = sqlite3_column_int(node_select_stmt, nt_col_lastAwake);
  n->lastUpdate = sqlite3_column_int(node_select_stmt, nt_col_lastUpdate);
  n->security_flags = sqlite3_column_int(node_select_stmt, nt_col_security_flags);
  n->mode = sqlite3_column_int(node_select_stmt, nt_col_mode);
  n->state = sqlite3_column_int(node_select_stmt, nt_col_state);
  n->manufacturerID = sqlite3_column_int(node_select_stmt, nt_col_manufacturerID);
  n->productType = sqlite3_column_int(node_select_stmt, nt_col_productType);
  n->productID = sqlite3_column_int(node_select_stmt, nt_col_productID);
  n->nodeType = sqlite3_column_int(node_select_stmt, nt_col_nodeType);
  n->nAggEndpoints = sqlite3_column_int(node_select_stmt, nt_col_nAggEndpoints);

  n->nodeNameLen = sqlite3_column_bytes(node_select_stmt, nt_col_name);
  n->nodename = rd_data_mem_alloc(n->nodeNameLen);
  memcpy(n->nodename, sqlite3_column_blob(node_select_stmt, nt_col_name), n->nodeNameLen);

  n->dskLen = sqlite3_column_bytes(node_select_stmt, nt_col_dsk);
  n->dsk = rd_data_mem_alloc(n->dskLen);
  memcpy(n->dsk, sqlite3_column_blob(node_select_stmt, nt_col_dsk), n->dskLen);

  n->node_version_cap_and_zwave_sw = sqlite3_column_int(node_select_stmt, nt_col_node_version_cap_and_zwave_sw);
  n->probe_flags = sqlite3_column_int(node_select_stmt, nt_col_probe_flags);
  n->node_properties_flags = sqlite3_column_int(node_select_stmt, nt_col_properties_flags);

  n->node_cc_versions_len = sqlite3_column_bytes(node_select_stmt, nt_col_cc_versions);
  n->node_cc_versions = rd_data_mem_alloc(n->node_cc_versions_len);
  memcpy(n->node_cc_versions, sqlite3_column_blob(node_select_stmt, nt_col_cc_versions), n->node_cc_versions_len);
  n->node_is_zws_probed = sqlite3_column_int(node_select_stmt, nt_col_node_is_zws_probed);

  LIST_STRUCT_INIT(n, endpoints);
  n->pcvs = NULL;
  n->nEndpoints = 0;
  n->refCnt = 0;

  //Now for the endpoints
  sqlite3_reset(ep_select_stmt);
  sqlite3_bind_ex_int(ep_select_stmt, 0, nodeID);
  while (1)
  {
    int step = sqlite3_step(ep_select_stmt);
    if (step != SQLITE_ROW)
    {
      break;
    }
    rd_ep_database_entry_t *e = rd_data_mem_alloc(sizeof(rd_ep_database_entry_t));
    e->list = 0;
    e->node = n;
    e->endpoint_id = sqlite3_column_int(ep_select_stmt, et_col_endpointid);
    e->installer_iconID = sqlite3_column_int(ep_select_stmt, et_col_installer_iconID);
    e->user_iconID = sqlite3_column_int(ep_select_stmt, et_col_user_iconID);
    e->state = sqlite3_column_int(ep_select_stmt, et_col_state);

    e->endpoint_name_len = sqlite3_column_bytes(ep_select_stmt, et_col_name);
    e->endpoint_name = rd_data_mem_alloc(e->endpoint_name_len);
    memcpy(e->endpoint_name, sqlite3_column_blob(ep_select_stmt, et_col_name), e->endpoint_name_len);

    e->endpoint_loc_len = sqlite3_column_bytes(ep_select_stmt, et_col_location);
    e->endpoint_location = rd_data_mem_alloc(e->endpoint_loc_len);
    memcpy(e->endpoint_location, sqlite3_column_blob(ep_select_stmt, et_col_location), e->endpoint_loc_len);

    e->endpoint_aggr_len = sqlite3_column_bytes(ep_select_stmt, et_col_aggr);
    e->endpoint_agg = rd_data_mem_alloc(e->endpoint_aggr_len);
    memcpy(e->endpoint_agg, sqlite3_column_blob(ep_select_stmt, et_col_aggr), e->endpoint_aggr_len);

    e->endpoint_info_len = sqlite3_column_bytes(ep_select_stmt, et_col_info);
    e->endpoint_info = rd_data_mem_alloc(e->endpoint_info_len);
    memcpy(e->endpoint_info, sqlite3_column_blob(ep_select_stmt, et_col_info),
           e->endpoint_info_len);

    list_add(n->endpoints, e);
    n->nEndpoints++;
  }

  return n;
}

void rd_data_store_nvm_write(rd_node_database_entry_t *n)
{
  rd_ep_database_entry_t *e;
  int rc;

  //Just delete the old one
  rd_data_store_nvm_free(n);

  sqlite3_reset(node_insert_stmt);
  sqlite3_bind_ex_int(node_insert_stmt, nt_col_nodeid, n->nodeid);
  sqlite3_bind_ex_int(node_insert_stmt, nt_col_wakeUp_interval, n->wakeUp_interval);
  sqlite3_bind_ex_int(node_insert_stmt, nt_col_lastAwake, n->lastAwake);
  sqlite3_bind_ex_int(node_insert_stmt, nt_col_lastUpdate, n->lastUpdate);
  sqlite3_bind_ex_int(node_insert_stmt, nt_col_security_flags, n->security_flags);
  sqlite3_bind_ex_int(node_insert_stmt, nt_col_mode, n->mode);
  sqlite3_bind_ex_int(node_insert_stmt, nt_col_state, n->state);
  sqlite3_bind_ex_int(node_insert_stmt, nt_col_manufacturerID, n->manufacturerID);
  sqlite3_bind_ex_int(node_insert_stmt, nt_col_productType, n->productType);
  sqlite3_bind_ex_int(node_insert_stmt, nt_col_productID, n->productID);
  sqlite3_bind_ex_int(node_insert_stmt, nt_col_nodeType, n->nodeType);
  sqlite3_bind_ex_int(node_insert_stmt, nt_col_nAggEndpoints, n->nAggEndpoints);
  sqlite3_bind_ex_int(node_insert_stmt, nt_col_node_version_cap_and_zwave_sw, n->node_version_cap_and_zwave_sw);
  sqlite3_bind_ex_int(node_insert_stmt, nt_col_probe_flags, n->probe_flags);
  sqlite3_bind_ex_int(node_insert_stmt, nt_col_properties_flags, n->node_properties_flags);

  sqlite3_bind_ex_blob(node_insert_stmt, nt_col_name, n->nodename, n->nodeNameLen, SQLITE_STATIC);
  sqlite3_bind_ex_blob(node_insert_stmt, nt_col_dsk, n->dsk, n->dskLen, SQLITE_STATIC);
  sqlite3_bind_ex_blob(node_insert_stmt, nt_col_cc_versions, n->node_cc_versions, n->node_cc_versions_len, SQLITE_STATIC);
  sqlite3_bind_ex_int(node_insert_stmt, nt_col_node_is_zws_probed, n->node_is_zws_probed);

  rc = sqlite3_step(node_insert_stmt);
  if (rc != SQLITE_DONE && rc != SQLITE_ROW)
  {
    ERR_PRINTF("execution failed: %s\n", sqlite3_errmsg(db));
  }

  for (e = list_head(n->endpoints); e; e = list_item_next(e))
  {
    sqlite3_reset(ep_insert_stmt);
    sqlite3_bind_ex_int(ep_insert_stmt, et_col_endpointid, e->endpoint_id);
    sqlite3_bind_ex_int(ep_insert_stmt, et_col_nodeid, n->nodeid);
    sqlite3_bind_ex_blob(ep_insert_stmt, et_col_info, e->endpoint_info, e->endpoint_info_len, SQLITE_STATIC);
    sqlite3_bind_ex_blob(ep_insert_stmt, et_col_aggr, e->endpoint_agg, e->endpoint_aggr_len, SQLITE_STATIC);
    sqlite3_bind_ex_blob(ep_insert_stmt, et_col_name, e->endpoint_name, e->endpoint_name_len, SQLITE_STATIC);
    sqlite3_bind_ex_blob(ep_insert_stmt, et_col_location, e->endpoint_location, e->endpoint_loc_len, SQLITE_STATIC);
    sqlite3_bind_ex_int(ep_insert_stmt, et_col_state, e->state);
    sqlite3_bind_ex_int(ep_insert_stmt, et_col_installer_iconID, e->installer_iconID);
    sqlite3_bind_ex_int(ep_insert_stmt, et_col_user_iconID, e->user_iconID);

    rc = sqlite3_step(ep_insert_stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW)
    {
      ERR_PRINTF("execution failed: %s\n", sqlite3_errmsg(db));
    }
  }
}

void rd_data_store_nvm_free(rd_node_database_entry_t *n)
{
  sqlite3_stmt *stmt = NULL;
  int rc;
  rc = sqlite3_prepare_v2(db, "DELETE FROM nodes WHERE nodeid = ?", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
  {
    ERR_PRINTF("SQL Error: Failed to execute statement: %s\n",
               sqlite3_errmsg(db));
    return;
  }
  sqlite3_bind_int(stmt, 1, n->nodeid);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  rc = sqlite3_prepare_v2(db, "DELETE FROM endpoints WHERE nodeid = ?", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
  {
    ERR_PRINTF("SQL Error: Failed to execute statement: %s\n",
               sqlite3_errmsg(db));
    return;
  }
  sqlite3_bind_int(stmt, 1, n->nodeid);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void rd_data_store_update(rd_node_database_entry_t *n)
{
  //We just rewrite the whole thing:
  rd_data_store_nvm_write(n);
}

/****************** IP associations **********************/

void rd_data_store_persist_associations(list_t ip_association_table)
{
  sqlite3_stmt *stmt = NULL;

  //Clear the old table
  datastore_exec_sql("DELETE FROM ip_association;");
  const char *sql = "INSERT INTO ip_association VALUES(?,?,?,?,?,?,?,?,?,?,?)";

  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
  {
    ERR_PRINTF("prepare failed: %s\n", sqlite3_errmsg(db));
    return;
  }

  for (ip_association_t *a = list_head(ip_association_table); a != NULL; a = list_item_next(a))
  {
    sqlite3_reset(stmt);
    sqlite3_bind_ex_int(stmt, ipa_col_virtual_id, a->virtual_id);
    sqlite3_bind_ex_int(stmt, ipa_col_type, a->type);
    sqlite3_bind_ex_blob(stmt, ipa_col_resource_ip, &a->resource_ip, sizeof(uip_ip6addr_t), NULL);
    sqlite3_bind_ex_int(stmt, ipa_col_resource_endpoint, a->resource_endpoint);
    sqlite3_bind_ex_int(stmt, ipa_col_resource_port, a->resource_port);
    sqlite3_bind_ex_int(stmt, ipa_col_virtual_endpoint, a->virtual_endpoint);
    sqlite3_bind_ex_int(stmt, ipa_col_grouping, a->grouping);
    sqlite3_bind_ex_int(stmt, ipa_col_han_nodeid, a->han_nodeid);
    sqlite3_bind_ex_int(stmt, ipa_col_han_endpoint, a->han_endpoint);
    sqlite3_bind_ex_int(stmt, ipa_col_was_dtls, a->was_dtls);
    sqlite3_bind_ex_int(stmt, ipa_col_mark_removal, a->mark_removal);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW)
    {
      ERR_PRINTF("execution failed: %s\n", sqlite3_errmsg(db));
    }
  }
  sqlite3_finalize(stmt);
}

void rd_datastore_unpersist_association(list_t ip_association_table, struct memb *ip_association_pool)
{
  sqlite3_stmt *stmt = NULL;
  int rc;

  const char *sql = "SELECT * FROM ip_association;";
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
  {
    ERR_PRINTF("prepare failed: %s\n", sqlite3_errmsg(db));
    return;
  }

  while (1)
  {
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW)
    {
      break;
    }

    ip_association_t *a = (ip_association_t *)memb_alloc(ip_association_pool);
    if (!a)
    {
      ERR_PRINTF("Out of memory during unpersist of association table\n");
      return;
    }
    a->virtual_id = sqlite3_column_int(stmt, ipa_col_virtual_id);
    a->type = sqlite3_column_int(stmt, ipa_col_type);

    if (sqlite3_column_bytes(stmt, ipa_col_resource_ip) == sizeof(uip_ip6addr_t))
    {
      memcpy(a->resource_ip.u8, sqlite3_column_blob(stmt, ipa_col_resource_ip), sizeof(uip_ip6addr_t));
    }
    else
    {
      ERR_PRINTF("Invalid ip address length in database");
    }

    a->virtual_id = sqlite3_column_int(stmt, ipa_col_virtual_id);
    a->resource_endpoint = sqlite3_column_int(stmt, ipa_col_resource_endpoint);
    a->resource_port = sqlite3_column_int(stmt, ipa_col_resource_port);
    a->virtual_endpoint = sqlite3_column_int(stmt, ipa_col_virtual_endpoint);
    a->grouping = sqlite3_column_int(stmt, ipa_col_grouping);
    a->han_nodeid = sqlite3_column_int(stmt, ipa_col_han_nodeid);
    a->han_endpoint = sqlite3_column_int(stmt, ipa_col_han_endpoint);
    a->was_dtls = sqlite3_column_int(stmt, ipa_col_was_dtls);
    a->mark_removal = sqlite3_column_int(stmt, ipa_col_mark_removal);
    list_add(ip_association_table, a);
  }
  sqlite3_finalize(stmt);
}

void rd_datastore_persist_virtual_nodes(const nodeid_t *nodelist, size_t node_count)
{
  sqlite3_stmt *stmt = NULL;

  //Clear the old table
  datastore_exec_sql("DELETE FROM virtual_nodes");
  const char *sql = "INSERT INTO virtual_nodes VALUES(?)";

  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
  {
    ERR_PRINTF("prepare failed: %s\n", sqlite3_errmsg(db));
    return;
  }

  for (int i = 0; i < node_count; i++)
  {
    DBG_PRINTF("Storing virtual node %i\n", nodelist[i]);
    sqlite3_reset(stmt);
    sqlite3_bind_ex_int(stmt, vn_col_virtual_id, nodelist[i]);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW)
    {
      ERR_PRINTF("execution failed: %s\n", sqlite3_errmsg(db));
      break;
    }
  }
  sqlite3_finalize(stmt);
}

size_t rd_datastore_unpersist_virtual_nodes(nodeid_t *nodelist, size_t max_node_count)
{
  size_t count = 0;
  sqlite3_stmt *stmt = NULL;
  int rc;

  const char *sql = "SELECT * FROM virtual_nodes;";
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
  {
    ERR_PRINTF("prepare failed: %s\n", sqlite3_errmsg(db));
    return 0;
  }

  for (int i = 0; i < max_node_count; i++)
  {
    int step = sqlite3_step(stmt);
    if (step != SQLITE_ROW)
    {
      break;
    }
    nodelist[count++] = sqlite3_column_int(stmt, vn_col_virtual_id);
    LOG_PRINTF("Found virtual node %i\n", sqlite3_column_int(stmt, vn_col_virtual_id));
  }
  sqlite3_finalize(stmt);
  return count;
}

/**************************** Generic stuff *************************/
void rd_data_store_invalidate()
{
  datastore_exec_sql("DELETE FROM nodes;");
  datastore_exec_sql("DELETE FROM endpoints;");
  datastore_exec_sql("DELETE FROM network;");
  datastore_exec_sql("DELETE FROM ip_association;");
  datastore_exec_sql("DELETE FROM s2_span;");
}

void rd_data_store_version_get(uint8_t *major, uint8_t *minor)
{
  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db, "SELECT * FROM network", -1, &stmt, NULL);

  int step = sqlite3_step(stmt);
  if (step != SQLITE_ROW)
  { //This should not happen
    assert(0);
    *major = 0;
    *minor = 0;
  }
  else
  {
    *major = sqlite3_column_int(stmt, nw_col_version_major);
    *minor = sqlite3_column_int(stmt, nw_col_version_minor);
  }
  sqlite3_finalize(stmt);
}

/********************* Standard memory allocations *************************/

void rd_data_store_mem_free(rd_node_database_entry_t *n)
{
  rd_ep_database_entry_t *ep;

  while ((ep = list_pop(n->endpoints)))
  {
    rd_store_mem_free_ep(ep);
  }

  if (n->nodename)
  {
    rd_data_mem_free(n->nodename);
  }
  if (n->dsk)
  {
    rd_data_mem_free(n->dsk);
  }
  if (n->node_cc_versions)
  {
    rd_data_mem_free(n->node_cc_versions);
  }
  /* pcvs is not persisted in eeprom */
  if (n->pcvs)
  {
    free(n->pcvs);
  }

  rd_data_mem_free(n);
}

void rd_store_mem_free_ep(rd_ep_database_entry_t *ep)
{
  if (ep->endpoint_info)
  {
    rd_data_mem_free(ep->endpoint_info);
  }
  if (ep->endpoint_name)
  {
    rd_data_mem_free(ep->endpoint_name);
  }
  if (ep->endpoint_location)
  {
    rd_data_mem_free(ep->endpoint_location);
  }
  rd_data_mem_free(ep);
}

void *rd_data_mem_alloc(uint8_t size)
{
  return malloc(size);
}

void rd_data_mem_free(void *p)
{
  free(p);
}

uint32_t rd_zgw_homeid_get()
{
  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(db, "SELECT * FROM network;", -1, &stmt, 0);
  uint32_t homeid = 0;
  int rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW)
  {
    homeid = sqlite3_column_int(stmt, nw_col_homeid);
  }
  sqlite3_finalize(stmt);
  return homeid;
}

nodeid_t rd_zgw_nodeid_get()
{
  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(db, "SELECT * FROM network;", -1, &stmt, 0);
  nodeid_t nodeid = 0;
  int rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW)
  {
    nodeid = sqlite3_column_int(stmt, nw_col_nodeid);
  }
  sqlite3_finalize(stmt);
  return nodeid;
}

void rd_datastore_persist_gw_config(const Gw_Config_St_t *gw_cfg)
{
  sqlite3_stmt *stmt = NULL;

  //Clear the old table
  datastore_exec_sql("DELETE FROM gw;");
  if (gw_cfg->actualPeers == 0)
  {
    datastore_exec_sql("DELETE FROM peers");
  }

  sqlite3_prepare_v2(db, "INSERT INTO gw VALUES(?,?,?)", -1, &stmt, NULL);
  sqlite3_bind_ex_int(stmt, gw_col_mode, gw_cfg->mode);
  sqlite3_bind_ex_int(stmt, gw_col_showlock, gw_cfg->showlock);
  sqlite3_bind_ex_int(stmt, gw_col_peerProfile, gw_cfg->peerProfile);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void rd_datastore_unpersist_gw_config(Gw_Config_St_t *gw_cfg)
{

  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(db, "SELECT * FROM gw;", -1, &stmt, 0);
  int rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW)
  {
    gw_cfg->mode = sqlite3_column_int(stmt, gw_col_mode);
    gw_cfg->showlock = sqlite3_column_int(stmt, gw_col_showlock);
    gw_cfg->peerProfile = sqlite3_column_int(stmt, gw_col_peerProfile);
    sqlite3_finalize(stmt);

    sqlite3_prepare_v2(db, "SELECT COUNT() FROM peer;", -1, &stmt, 0);
    sqlite3_step(stmt);
    gw_cfg->actualPeers = sqlite3_column_int(stmt, 0);
  } else {
    memset(gw_cfg,0,sizeof(Gw_Config_St_t));
  }
  sqlite3_finalize(stmt);
}

void rd_datastore_persist_peer_profile(int index, const Gw_PeerProfile_St_t *profile)
{
  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(db, "DELETE FROM peer WHERE id = ?;", -1, &stmt, NULL);

  sqlite3_bind_int(stmt, 1, index);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  sqlite3_prepare_v2(db, "INSERT INTO peer VALUES(?,?,?,?)", -1, &stmt, 0);
  sqlite3_bind_ex_int(stmt, peer_col_id, index);
  sqlite3_bind_ex_blob(stmt, peer_col_ip, profile->peer_ipv6_addr.u8, sizeof(uip_ip6addr_t), SQLITE_STATIC);

  sqlite3_bind_ex_int(stmt, peer_col_port,
                      (profile->port2 << 8) | (profile->port1));

  sqlite3_bind_text(stmt, peer_col_name + 1,
                    profile->peerName, profile->peerNameLength, SQLITE_STATIC);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void rd_datastore_unpersist_peer_profile(int index, Gw_PeerProfile_St_t *profile)
{

  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(db, "SELECT * FROM peer WHERE id = ?;", -1, &stmt, NULL);
  sqlite3_bind_ex_int(stmt, peer_col_id, index);

  int rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW)
  {
    profile->peerNameLength = sqlite3_column_bytes(stmt, peer_col_name);
    memcpy(profile->peerName, sqlite3_column_blob(stmt, peer_col_name), profile->peerNameLength);
    profile->peerName[profile->peerNameLength] = 0;
  profile->port1 = sqlite3_column_int(stmt, peer_col_port) & 0xff;
  profile->port2 = (sqlite3_column_int(stmt, peer_col_port) >> 8) & 0xff;

  if (sqlite3_column_bytes(stmt, peer_col_ip) == sizeof(uip_ip6addr_t))
  {
    memcpy(profile->peer_ipv6_addr.u8, sqlite3_column_blob(stmt, peer_col_ip), sizeof(uip_ip6addr_t));
  }
  } else {
    WRN_PRINTF("Request for undefined peer %i\n",index);
    memset(profile,0,sizeof(Gw_PeerProfile_St_t));
  }

  sqlite3_finalize(stmt);
}

void rd_datastore_persist_s2_span_table(const struct SPAN *span_table, size_t span_table_size)
{
  sqlite3_stmt *stmt;
  LOG_PRINTF("Persisting S2 SPAN table\n");
  datastore_exec_sql("DELETE FROM s2_span;");
  int rc = sqlite3_prepare_v2(db, "INSERT INTO s2_span VALUES(?,?,?,?,?,?,?);", -1, &stmt, 0);
  if (rc != SQLITE_OK) {
    WRN_PRINTF("Failed to persist S2 SPAN table, Sqlite prepare failed: %d\n", rc);
    return;
  }
  for (size_t i = 0; i < span_table_size; i++) {
    const struct SPAN *entry = &(span_table[i]);
    if (entry->state == SPAN_NEGOTIATED)
    { // only backup if state is SPAN_NEGOTIATED
      sqlite3_reset(stmt);
      sqlite3_bind_ex_blob(stmt, span_col_d,        &entry->d, sizeof(((struct SPAN *)0)->d), SQLITE_STATIC);
      sqlite3_bind_ex_int(stmt,  span_col_lnode,    entry->lnode);
      sqlite3_bind_ex_int(stmt,  span_col_rnode,    entry->rnode);
      sqlite3_bind_ex_int(stmt,  span_col_rx_seq,   entry->rx_seq);
      sqlite3_bind_ex_int(stmt,  span_col_tx_seq,   entry->tx_seq);
      sqlite3_bind_ex_int(stmt,  span_col_class_id, entry->class_id);
      sqlite3_bind_ex_int(stmt,  span_col_state,    entry->state);
      sqlite3_step(stmt);
    }
  }
  sqlite3_finalize(stmt);
}

void rd_datastore_unpersist_s2_span_table(struct SPAN *span_table, size_t span_table_size)
{
  sqlite3_stmt *stmt;
  LOG_PRINTF("Unpersisting S2 SPAN table\n");

  int rc = sqlite3_prepare_v2(db, "SELECT * from s2_span;", -1, &stmt, 0);
  if (rc != SQLITE_OK) {
    WRN_PRINTF("Failed to unpersist S2 SPAN table, Sqlite prepare failed: %d\n", rc);
    return;
  }
  unsigned int idx = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    LOG_PRINTF("SPAN Entry: %d\n", idx);
    if (idx < span_table_size) {
      memcpy(&(span_table[idx].d), sqlite3_column_blob(stmt, span_col_d), sizeof(((struct SPAN *)0)->d));
      span_table[idx].lnode =      sqlite3_column_int(stmt, span_col_lnode);
      span_table[idx].rnode =      sqlite3_column_int(stmt, span_col_rnode);
      span_table[idx].rx_seq =     sqlite3_column_int(stmt, span_col_rx_seq);
      span_table[idx].tx_seq =     sqlite3_column_int(stmt, span_col_tx_seq);
      span_table[idx].class_id =   sqlite3_column_int(stmt, span_col_class_id);
      span_table[idx].state =      sqlite3_column_int(stmt, span_col_state);
      idx++;
    }
    else {
      WRN_PRINTF("s2 span idx (%d) out of range", idx);
      break;
    }
  }
  sqlite3_finalize(stmt);
}
