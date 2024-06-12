/* © 2017 Silicon Laboratories Inc.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <assert.h>
#include <provisioning_list.h>
#include <provisioning_list_files.h>
#include <pvs_internal.h>
#include "pvs_cfg.h"

#include "RD_internal.h"
#include <ZIP_Router_logging.h>

/**
 * \ingroup pvslist
 * \defgroup pvslist_int Provisioning List Internals
 * @{
 */

/**
 * Maximum number of elements in the provisioning list.
 */
#ifndef PROVISIONING_LIST_SIZE
#define PROVISIONING_LIST_SIZE 1000
#endif

/*
  Internals
*/

/**
 * Pretty print helper.
 */
#define NAME_OF(enumname) #enumname

/** The provisioning list. */
static provisioning_list_t pvs_list[PROVISIONING_LIST_SIZE];


/** Add a new provision unconditionally.
 */
/*@dependent@*//*@null@*/static struct provision * pvs_dev_add(uint8_t dsk_len, uint8_t *dsk, provisioning_bootmode_t bootmode)    __attribute__((nonnull));

/**
 * Find a device by matching tail of the DSK from index start_idx.
 */
/*@dependent@*//*@null@*/static struct provision * pvs_dev_get_idx(uint8_t dsk_len, uint8_t *dsk, uint8_t start_idx)__attribute__((nonnull));

static pvs_result_t pvs_tlv_set(struct provision *pvs, uint8_t type, uint8_t len, uint8_t *val, /*@null@*//*@dependent@*/struct pvs_tlv *tmp_tlv);

/** Insert tlv at the head of the list */
static void pvs_tlv_insert_front(struct pvs_tlv **tlv_hd, struct pvs_tlv *new);

/** Release a tlv */
static void pvs_tlv_free(struct pvs_tlv **prev, struct pvs_tlv *dead)__attribute__((nonnull));

// Move external for testing
/* void pvs_dsk_print(FILE *strm, uint8_t dsk_len, /\*@null@*\/uint8_t *dsk); */

/** Print a list of tlvs */
static void pvs_tlv_print(FILE *strm, /*@null@*/struct pvs_tlv *tlvs)__attribute__((nonnull));

/** Printable name of a tlv type. */
/*@temp@*/static const char* pvs_tlv_type_to_str(uint8_t t);


/** The filename for the persistent storage for the provisioning list.
 * The actual name is expected to be allocated and stored
 * elsewhere. */
/*@null@*/static const char *pvs_store_filename = PROVISIONING_LIST_STORE_FILENAME_DEFAULT;

/** File formatting helper. */
#ifdef PVS_TEST
static const char *pvs_store_hdr = "zgwpvssttstv1.0";
#else
static const char *pvs_store_hdr = "zgwpvsstorev1.0";
#endif
/** File formatting helper. */
static const char * pvs_entry_hdr_fmt = "pvs entry 000";
static const char * pvs_fmt = "pvs entry %03d";


static pvs_result_t pvs_list_store_find_file(const char *filename);
static pvs_result_t pvs_list_store_find(const char *filename);


/** Set up persistent storage file and import data.
 *
 * If a file name is configured, use that if it works.  Fall back to
 * default file name if the configured name cannot be used or if the
 * file contains other data (to protect the other data from being
 * overwitten).
 *
 * \param filename The configured storage file, including path. Can be NULL.
 */
static void pvs_list_store_setup_and_import(/*@null@*/const char *filename);

static pvs_result_t pvs_list_cfg_read_file(const char *filename);

static pvs_result_t pvs_list_store_writeable_test(const char* filename);

/** Try to use filename as a storage file.
 *
 * Import from the storage file if it already exists.
 *
 * \param filename The name of the file to import from.
 * \param protect_format Boolean: Return error if file has the wrong header.
 * \return PVS_ERROR if the file cannot be created or if it contains
 * incorrect header.
 */
static pvs_result_t pvs_list_store_setup_and_import_file(/*@null@*/const char *filename, uint8_t protect_format);

/**
 * Populate the provisioning list from an open storage file.
 * Assume that the provisioning list is empty.
 */
static pvs_result_t pvs_list_import_file(FILE *strm)__attribute__((nonnull));

/** Import one device provision from file.
 */
static pvs_result_t pvs_list_dev_import(FILE *strm, uint16_t ii)__attribute__((nonnull));

/**
 * Clear the storage file and dump the current pvs_list instead.
 */
static void pvs_list_persist_in_file(void);

/**
 * Update the provisioning list storage with one new/modified item.
 *
 * \param strm The provisioning list storage file.
 * \param pvs The provision to write to storage.
 * \param ii Provision count.
 * \return 0 on success, error code on failure.
 */
static pvs_result_t pvs_list_dev_store(FILE *strm, const struct provision *pvs, uint16_t ii) __attribute__((nonnull));


/* ****
   Locals
   **** */

/* **** Storage in file stuff ****/

static void pvs_list_persist_in_file()
{
    FILE *strm;
    size_t written;
    pvs_result_t res;
    uint16_t ii;

    if (!pvs_store_filename) {
        return;
    }

    /* Open file, dumping the previous content. */
    strm = fopen(pvs_store_filename, "w");
    if (strm == NULL) {
        ERR_PRINTF("Failed to open provisioning list file %s: %s (%d)\n",
                   pvs_store_filename, strerror(errno), errno);
        return;
    }
    written = fwrite(pvs_store_hdr, strlen(pvs_store_hdr), 1, strm);
    if (written != 1) {
        ERR_PRINTF("Failed to write to provisioning list file %s: %s (%d)\n",
                   pvs_store_filename, strerror(errno), errno);
        (void)fclose(strm);
        return;
    }

    for (ii = 0; ii < PROVISIONING_LIST_SIZE; ii++)
    {
        if (pvs_list[ii].dsk_len)
        {
            res = pvs_list_dev_store(strm, &pvs_list[ii], ii);
            if (PVS_ERROR == res) {
                /* No need to continue writing */
                break;
            }
        }
    }

    (void)fclose(strm);
}

/**
 * Store one device provision.
 */
static pvs_result_t pvs_list_dev_store(FILE *strm, const struct provision *pvs, uint16_t ii)
{
    size_t res;
    struct pvs_tlv *tlv;

    fprintf(strm, pvs_fmt, ii);
    res = fwrite(pvs, sizeof(struct provision), 1, strm);
    res += fwrite(pvs->dsk, pvs->dsk_len, 1, strm);
    if (res != 2) {
        WRN_PRINTF("Failed to write item %u to provisioning list file: %s (%d).\n",
                   ii, strerror(errno), errno);
        return PVS_ERROR;
    }

    tlv = pvs->tlv_list;
    while (tlv) {
        res = fwrite(tlv, sizeof(struct pvs_tlv), 1, strm);
        res += fwrite(tlv->value, tlv->length, 1, strm);
        if (res != 2) {
            WRN_PRINTF("Failed to write tlvs for item %u to provisioning list file: %s (%d).\n",
                   ii, strerror(errno), errno);
            return PVS_ERROR;
        }
        tlv = tlv->next;
    }
    return PVS_SUCCESS;
}

/** Try to use filename to read pvs list initial configuration.
 *
 * If it exists, but does not contain pvs list data, return error.
 *
 * If filename is not useable, return PVS_ERROR.
 */
static pvs_result_t pvs_list_cfg_read_file(const char *filename)
{
    FILE *strm;
    pvs_result_t res;

    if (filename == NULL) {
        return PVS_ERROR;
    }

    strm = fopen(filename, "r");
    if (strm) {
        DBG_PRINTF("Using file %s for provisioning list initialization.\n",
                   filename);

        res = pvs_parse_config_file(strm);
        if (res == 0) {
            printf("Import done.\n");
        } else {
            printf("Import stopped with code %d.\n", res);
        }
        (void)fclose(strm);
        if (res) {
            /* File contents are not pvs config format */
            ERR_PRINTF("File %s does not contain valid provisioning list configuration.\n",
                       filename);
            return PVS_ERROR;
        }
    } else {
        WRN_PRINTF("Failed to open provisioning list config file %s: %s (%d)\n", filename, strerror(errno), errno);
    }
    return PVS_SUCCESS;
}

static pvs_result_t pvs_list_store_find_file(const char *filename)
{
    FILE *strm;

    strm = fopen(filename, "r");
    if (strm) {
        DBG_PRINTF("Found storage file %s\n", filename);
        (void)fclose(strm);
    } else {
        DBG_PRINTF("Did not find storage file %s\n", filename);
        return PVS_ERROR;
    }
    return PVS_SUCCESS;
}

static pvs_result_t pvs_list_store_find(const char *filename)
{
    if (filename != NULL) {
        /* If there is an argument, we only care about that one. */
        return pvs_list_store_find_file(filename);
    } else {
        /** Try the default */
        if (PROVISIONING_LIST_STORE_FILENAME_DEFAULT != NULL) {
            return pvs_list_store_find_file(PROVISIONING_LIST_STORE_FILENAME_DEFAULT);
        } else {
            return PVS_ERROR;
        }
    }
    return PVS_SUCCESS;
}

static pvs_result_t pvs_list_store_writeable_test(const char* filename)
{
    FILE * strm = fopen(filename, "w");

    if (strm) {
        WRN_PRINTF("Creating new file %s for provisioning list storage.\n",
                   filename);
        (void)fclose(strm);
        return PVS_SUCCESS;
    } else {
        ERR_PRINTF("Error creating provisioning list file: %s, %s (%d)\n",
                   filename, strerror(errno), errno);
        /* File cannot be opened for writing. */
        return PVS_ERROR;
    }
}

/* Try to use filename to read stored pvs list. Create it if it does not exist.
 *
 * If it exists, but does not contain pvs list storage file header, delete it, except
 * if protect_file is set.
 *
 * If filename is not useable, return PVS_ERROR.
 */
static pvs_result_t pvs_list_store_setup_and_import_file(const char *filename, uint8_t protect_file)
{
    FILE *strm;
    pvs_result_t res;

    if (filename == NULL) {
        return PVS_ERROR;
    }

    DBG_PRINTF("Using file %s for provisioning list storage.\n", filename);
    strm = fopen(filename, "r");
    if (strm) {
        res = pvs_list_import_file(strm);
        (void)fclose(strm);
        /* If protect_file is false, we do not care if import fails,
         * the file will be used anyway */
        if (res == PVS_ERROR && protect_file) {
            /* File contents are not pvs format, use default for writing (for safety) */
            ERR_PRINTF("File %s does not contain valid provisioning list data, using default file instead.\n",
                       filename);
            return PVS_ERROR;
        }
    } else {
        return pvs_list_store_writeable_test(filename);
    }
    return PVS_SUCCESS;
}

/* Set up persistent storage file and initialize from file. */
static void pvs_list_store_setup_and_import(const char *filename)
{
    pvs_result_t res;

    res = pvs_list_store_setup_and_import_file(filename, 1);
    if (res == PVS_ERROR)
    {
        /** Fall back to default */
        pvs_store_filename = PROVISIONING_LIST_STORE_FILENAME_DEFAULT;

        res = pvs_list_store_setup_and_import_file(pvs_store_filename, 0);
        if (res == PVS_ERROR)
        {
            pvs_store_filename = NULL;
        }
        return;
    }
    /* Provided file in filename is good, we can use that one for storage. */
    pvs_store_filename = filename;
}

/** Import from file.  Return error if the file should not be used to
 * store pvs data (ie, because the header is incorrect).
 * Print warnings to log if other things go wrong, eg, memory errors.
 *
 */
static pvs_result_t pvs_list_import_file(FILE *strm)
{
    char txt[16];
    uint16_t ii;
    pvs_result_t res;
    size_t rd;

    rd = fread(txt, strlen(pvs_store_hdr), 1, strm);
    if ((rd != 1) || (strncmp(txt, pvs_store_hdr, strlen(pvs_store_hdr)) != 0)) {
        return PVS_ERROR;
    }
    for (ii = 0; ii < PROVISIONING_LIST_SIZE; ii++)
    {
        res = pvs_list_dev_import(strm, ii);
        if (res == PVS_ERROR) {
            DBG_PRINTF("Imported %u provisions, stored %u\n", ii, provisioning_list_get_count());
            if (ii != provisioning_list_get_count()) {
                ERR_PRINTF("Errors during provisioning list import, item %u.\n", ii);
            }
            break;
        }
    }
    return PVS_SUCCESS;
}

static pvs_result_t pvs_list_dev_import(FILE *strm, uint16_t ii)
{
    struct provision pvs;
    struct pvs_tlv tlv;
    uint8_t val[256];
    uint8_t *dsk;
    char   txt[14];
    pvs_result_t res;
    size_t rd;

    pvs.dsk = NULL;
    pvs.tlv_list = NULL;
    rd = fread(txt, strlen(pvs_entry_hdr_fmt), 1, strm);
    if (rd != 1) {
        /* This can be an error or EOF */
        //printf("Finished reading from provisioning list store file.\n");
        return PVS_ERROR;
    }
    /*@ignore@*/
    txt[strlen(pvs_entry_hdr_fmt)] = '\0';
    if (strncmp(txt, pvs_entry_hdr_fmt, 10) == 0) {
//        printf("Reading entry %d from file (%s)\n", ii, txt);
    } else {
        WRN_PRINTF("Import error on entry %d\n", ii);
    }
    rd = fread(&pvs, sizeof(struct provision), 1, strm);
    if (rd == 1) {
        /* The dsk pointer here is invalid, but still confuses lint.*/
        if (pvs.dsk_len < 4) {
            return PVS_ERROR;
        } else {
            dsk = malloc(pvs.dsk_len);
            if (dsk == NULL) {
                return PVS_ERROR;
            } else {
                rd = fread(dsk, pvs.dsk_len, 1, strm);
            }
            pvs.dsk = dsk;
        }
    }
    if (pvs.tlv_list != NULL) {
        /* The pointer here is invalid, but still confuses lint.*/
        /* Start over creating the list */
        pvs.tlv_list = NULL;
#ifdef PVS_TEST
        pvs.num_tlvs = 0;
#endif
        rd = fread(&tlv, sizeof(struct pvs_tlv), 1, strm);
        if (rd != 1) {
            WRN_PRINTF("Error reading first tlv, %s (%d)\n", strerror(errno), errno);
            return PVS_ERROR;
        }
        rd = fread(&val, tlv.length, 1, strm);
        if (rd != 1) {
            WRN_PRINTF("Error reading first tlv value, expected length %u\n", tlv.length);
            return PVS_ERROR;
        }
        res = pvs_tlv_set(&pvs, tlv.type, tlv.length, val, NULL);
        if (res != PVS_SUCCESS) {
            WRN_PRINTF("Error populating first tlv\n");
            return PVS_ERROR;
        }

        while (tlv.next != NULL) {
            rd = fread(&tlv, sizeof(struct pvs_tlv), 1, strm);
            if (rd != 1) {
                WRN_PRINTF("Error reading tlv\n");
                return PVS_ERROR;
            }
            rd = fread(&val, tlv.length, 1, strm);
            if (rd != 1) {
                WRN_PRINTF("Error reading tlv value\n");
                return PVS_ERROR;
            }
            res = pvs_tlv_set(&pvs, tlv.type, tlv.length, val, NULL);
            if (res != PVS_SUCCESS) {
                WRN_PRINTF("Error populating tlv\n");
                return PVS_ERROR;
            }
        }
    }

    memcpy(&pvs_list[ii], &pvs, sizeof(struct provision));
    /*@end@*/
    return PVS_SUCCESS;
}

/**
 * @}
 *
 * \addtogroup pvslist
 * @{
 */

/*
  Externals
*/

/* Operations on the entire provisioning list */
void provisioning_list_init(/*@null@*/const char* storage_filename, /*@null@*/const char* cfg_filename)
{
    /* Zero provisioning list. */
    memset(pvs_list, 0, sizeof(struct provision) * PROVISIONING_LIST_SIZE);

    /* Do we have a persisten storage file to import from?  Otherwise
     * this is a "first run", initialize from config. */
    if (pvs_list_store_find(storage_filename) == PVS_ERROR) {
        /* Set up the persistent storage file to use, but do not read from it. */
        if ((storage_filename != NULL) &&
            (pvs_list_store_writeable_test(storage_filename) == PVS_SUCCESS)) {
            pvs_store_filename = storage_filename;
        } else if (pvs_list_store_writeable_test(PROVISIONING_LIST_STORE_FILENAME_DEFAULT)) {
            pvs_store_filename = PROVISIONING_LIST_STORE_FILENAME_DEFAULT;
        } else {
            pvs_store_filename = NULL;
        }
        /* Import from the config file */
        DBG_PRINTF("FILENAME..........%s\n",cfg_filename);
        pvs_list_cfg_read_file(cfg_filename);
    } else {
        /* Import from the storage file and set it up for storage, but
         * protect storage_filename if it has incorrect data. */
        pvs_list_store_setup_and_import(storage_filename);
    }

    /* Write down the new layout, even if it might be the same data. */
    pvs_list_persist_in_file();
}

uint16_t provisioning_list_get_count()
{
    uint16_t i, j;

    for (i = 0, j = 0; i < PROVISIONING_LIST_SIZE; i++)
    {
        if (pvs_list[i].dsk_len)
        {
          j++;;
        }
    }
    return j;
}

int provisioning_list_pending_count()
{
    uint16_t ii, j;
    rd_node_database_entry_t *rd_dbe = NULL;

    for (ii = 0, j = 0; ii < PROVISIONING_LIST_SIZE; ii++)
    {
       if (pvs_list[ii].dsk_len
           && (pvs_list[ii].status == PVS_STATUS_PENDING)
           && ((pvs_list[ii].bootmode == PVS_BOOTMODE_SMART_START) || 
               (pvs_list[ii].bootmode == PVS_BOOTMODE_LONG_RANGE_SMART_START)))
       {
           rd_dbe = rd_lookup_by_dsk(pvs_list[ii].dsk_len, pvs_list[ii].dsk);
           if (rd_dbe == NULL) {
              j++;
           }
       }
    }
    return j;
}

void provisioning_list_clear()
{
    uint16_t i = 0;

    for (i = 0; i < PROVISIONING_LIST_SIZE; i++)
    {
        /* If the ith provisioning_list slot is blank, it is already removed */
        if (!pvs_list[i].dsk_len) {
            continue;
        }

        pvs_list[i].dsk_len = 0;
        free(pvs_list[i].dsk);
        pvs_list[i].dsk = NULL;
        pvs_tlv_clear(pvs_list[i].tlv_list);
#ifdef PVS_TEST
        pvs_list[i].num_tlvs = 0;
#endif
        pvs_list[i].tlv_list = NULL;
    }
    pvs_list_persist_in_file();
}

/* Provisioning list iterator. */
provisioning_list_iter_t * provisioning_list_iterator_get(uint32_t id)
{
    uint16_t cnt = provisioning_list_get_count();
    provisioning_list_iter_t *res = malloc(sizeof(provisioning_list_iter_t));
    uint16_t ii, jj;

    if (res == NULL) {
        return NULL;
    }
    res->id = id;
    res->cnt = cnt;
    res->next = 0;
    if (cnt == 0) {
        res->entries = NULL;
    } else {
        res->entries = malloc(sizeof(pvs_iter_entry_t) * cnt);
        if (res->entries == NULL) {
            free(res);
            return NULL;
        }
        jj = 0;
        for (ii = 0; ii < PROVISIONING_LIST_SIZE; ii++)
        {
            /* If the entry is blank or the dsk too large, skip it */
            if ((pvs_list[ii].dsk == NULL)
                || (pvs_list[ii].dsk_len < 4)
                || (pvs_list[ii].dsk_len > PROVISIONING_LIST_DSK_MAX_SIZE)) {
                continue;
            }
            res->entries[jj].dsk_len = pvs_list[ii].dsk_len;
            memcpy(res->entries[jj].dsk, pvs_list[ii].dsk, pvs_list[ii].dsk_len);
            jj++;
        }
        if (jj != cnt) {
            ERR_PRINTF("Skipped %u provisioning list entries with too large DSKs\n", cnt-jj);
            res->cnt = jj;
        }
    }
    return res;
}

void provisioning_list_iterator_delete(provisioning_list_iter_t *iter)
{
    if (iter) {
        iter->cnt = 0;
        if (iter->entries) {
            free(iter->entries);
            iter->entries = NULL;
        }
        free(iter);
    }
}

/** Look up next index in iterator iter and update next. */
struct provision * provisioning_list_iter_get_next(provisioning_list_iter_t *iter)
{
    struct provision *pvs;

    if (iter == NULL || (iter->next >= iter->cnt))
    {
        return NULL;
    }
    pvs = provisioning_list_dev_get(iter->entries[iter->next].dsk_len,
                                    iter->entries[iter->next].dsk);
    iter->next++;
    return pvs;
}

/** Look up index idx in iterator iter.
 * Do not modify iter.
 */
struct provision * provisioning_list_iter_get_index(provisioning_list_iter_t *iter, uint16_t idx)
{
    struct provision *pvs;

    if ((iter == NULL) || (idx >= iter->cnt)) // This also catches empty iterator
    {
        return NULL;
    }

    pvs = provisioning_list_dev_get(iter->entries[idx].dsk_len,
                                    iter->entries[idx].dsk);
    return pvs;
}


/* Operations on devices/provisions in the provisioning list */
struct provision * provisioning_list_dev_add(uint8_t dsk_len, uint8_t *dsk,
                                             provisioning_bootmode_t bootmode)
{
    if (dsk_len < 4) {
        return NULL;
    }

    if (provisioning_list_dev_get(dsk_len, dsk)) {
        /* Do not add if it exists already. */
        return NULL;
    } else {
        return pvs_dev_add(dsk_len, dsk, bootmode);
    }
}

/**
 * Add a new device and persist the change.
 * This function assumes as a precondition that the dsk does not exist.
 */
static struct provision * pvs_dev_add(uint8_t dsk_len, uint8_t *dsk, provisioning_bootmode_t bootmode)
{
    int i = 0;
    struct provision *ret = NULL;

    for (i = 0; i < PROVISIONING_LIST_SIZE; i++)
    {
        if (pvs_list[i].dsk_len == 0)
        {
            pvs_list[i].dsk = malloc(dsk_len);
            if (pvs_list[i].dsk != NULL) {
                pvs_list[i].dsk_len = dsk_len;
                memcpy(pvs_list[i].dsk, dsk, dsk_len);
                pvs_list[i].status = PVS_STATUS_PENDING;
                pvs_list[i].bootmode = bootmode;
#ifdef PVS_TEST
                pvs_list[i].num_tlvs = 0;
#endif
                pvs_list[i].tlv_list = NULL;
                ret = &(pvs_list[i]);
                pvs_list_persist_in_file();
            }
            break;
        }
    }
    /* If there was no more space in the list or if malloc failed,
     * return NULL. */
    return ret;
}

struct provision * provisioning_list_dev_set(uint8_t dsk_len, uint8_t *dsk, provisioning_bootmode_t bootmode)
{
    struct provision *prev = NULL;

    if (dsk_len < 4) {
        return NULL;
    }

    prev = provisioning_list_dev_get(dsk_len, dsk);

    if (prev) {
        prev->bootmode = bootmode;
        pvs_list_persist_in_file();
        return prev;
    } else {
        return pvs_dev_add(dsk_len, dsk, bootmode);
    }
}

struct provision * provisioning_list_dev_get(uint8_t dsk_len, const uint8_t *dsk)
{
    int i = 0;

    if (dsk_len < 4) {
        return NULL;
    }

    for (i = 0; i < PROVISIONING_LIST_SIZE; i++)
    {
        if (pvs_list[i].dsk_len != dsk_len || pvs_list[i].dsk == NULL) {
            continue;
        }

        if (memcmp(pvs_list[i].dsk, dsk, dsk_len) == 0)
        {
            return &pvs_list[i];
        }
    }
    return NULL;
}

struct provision * provisioning_list_dev_get_homeid(uint8_t *homeid)
{
    int i = 0;
    uint8_t buf[4];

    for (i = 0; i < PROVISIONING_LIST_SIZE; i++)
    {
        /* If the dsk is shorter than 12 bytes, it will not match. */
        if (pvs_list[i].dsk_len < 12 || pvs_list[i].dsk == NULL)
            continue;

        memcpy(buf, &(pvs_list[i].dsk[8]), 4);
        buf[0] |= 0xC0;
        buf[3] &= 0xFE;

        if (memcmp(buf, homeid, 4) == 0)
        {
            return &pvs_list[i];
        }
    }
    return NULL;
}

static struct provision * pvs_dev_get_idx(uint8_t dsk_len, uint8_t *dsk, uint8_t start_idx)
{
    int i = 0;
    uint16_t min_len = dsk_len + start_idx; /* minimum length of a matching dsk */

    for (i = 0; i < PROVISIONING_LIST_SIZE; i++)
    {
        /* If the dsk of the slot is shorter than needed, dont try to match it */
        if (pvs_list[i].dsk_len < min_len || pvs_list[i].dsk == NULL)
            continue;

        if (memcmp(&(pvs_list[i].dsk[start_idx]), dsk, dsk_len) == 0)
        {
            return &pvs_list[i];
        }
    }
    return NULL;
}

struct provision * provisioning_list_dev_match_challenge(uint8_t challenge_len, uint8_t *challenge)
{
  /* The security challenge contains the full 32 byte public key. The
   * provisioning_list only contains the 16 byte DSK, so we must limit
   * the comparison to those 16 bytes */
  if (challenge_len <= 2)
  {
    return NULL;
  }
  if (challenge_len > 16)
  {
    challenge_len = 16;
  }

  return pvs_dev_get_idx(challenge_len-2, &challenge[2], 2);
}

int provisioning_list_dev_remove(uint8_t dsk_len, uint8_t *dsk)
{
    int i = 0;
    int ret = PVS_ERROR;

    if (dsk_len < 4) {
        return ret;
    }

    for (i = 0; i < PROVISIONING_LIST_SIZE; i++)
    {
        if ((pvs_list[i].dsk != NULL)
            && (pvs_list[i].dsk_len == dsk_len)
            && (memcmp(pvs_list[i].dsk, dsk, dsk_len) == 0))
        {
            pvs_list[i].dsk_len = 0;
            free(pvs_list[i].dsk);
            pvs_tlv_clear(pvs_list[i].tlv_list);
#ifdef PVS_TEST
            pvs_list[i].num_tlvs = 0;
#endif
            pvs_list[i].tlv_list = NULL;
            pvs_list_persist_in_file();
            ret = PVS_SUCCESS;
            break;
        }
    }
    return ret;
}

/* The pseudo tlvs */
int provisioning_list_bootmode_set(struct provision *pvs, provisioning_bootmode_t bootmode)
{
    if (!pvs) {
      return PVS_ERROR;
    }

    pvs->bootmode = bootmode;
    pvs_list_persist_in_file();
    return PVS_SUCCESS;
}

uint8_t provisioning_list_tlv_crt_flag(pvs_tlv_type_t tlv_type)
{
   if (tlv_type == PVS_TLV_TYPE_ADV_JOIN) {
      return 0x01;
   }
   return 0;
}

int provisioning_list_tlv_set(struct provision *pvs, uint8_t type, uint8_t len, uint8_t *val)
{
    pvs_result_t res;

    if (pvs && (pvs->dsk_len > 0) && val && (len > 0))
    {
        struct pvs_tlv *tmp_tlv = provisioning_list_tlv_get(pvs, type);

        res = pvs_tlv_set(pvs, type, len, val, tmp_tlv);
        if (res == PVS_SUCCESS) {
            pvs_list_persist_in_file();
            return PVS_SUCCESS;
        } else {
            WRN_PRINTF("TLV creation failed\n");
            return PVS_ERROR;
        }
    } else {
        WRN_PRINTF("Invalid tlv parameters\n");
        return PVS_ERROR;
    }
}

static void pvs_tlv_insert_front(struct pvs_tlv **tlv_hd, struct pvs_tlv *new)
{
    new->next = *tlv_hd;
    *tlv_hd = new;
}

/**
 * Create or modify a tlv.
 *
 * This helper is used during initialization (storage import) and when
 * a tlv is added/modified by a client.
 */
static pvs_result_t pvs_tlv_set(struct provision *pvs, uint8_t type, uint8_t len, uint8_t *val, struct pvs_tlv *tmp_tlv)
{
    uint8_t *tmp_value = malloc(len);

    if (tmp_value == NULL)
    {
        /* No memory, leave everything as it was */
        return PVS_ERROR;
    }
    if (tmp_tlv == NULL) {
        /* Make a new one and insert it */
        tmp_tlv = malloc(sizeof(struct pvs_tlv));
        if (tmp_tlv == NULL)
        {
            free(tmp_value);
            return PVS_ERROR;
        }
        memset(tmp_tlv, 0, sizeof(struct pvs_tlv));
        pvs_tlv_insert_front(&pvs->tlv_list, tmp_tlv);
#ifdef PVS_TEST
        pvs->num_tlvs++;
#endif
    } else {
        /* Use the old tlv, but flush the old value first */
        free(tmp_tlv->value);
    }
    /* Whether it was old or new, update the contents of the tlv */
    tmp_tlv->type = type;
    tmp_tlv->length = len;
    tmp_tlv->value = memcpy(tmp_value, val, len);
    return PVS_SUCCESS;
}

struct pvs_tlv * pvs_tlv_get(struct pvs_tlv *tlv, uint8_t type)
{
    while (tlv)
    {
        if (tlv->type == type) {
            return tlv;
        } else {
            tlv = tlv->next;
        }
    }
    return NULL;
}

struct pvs_tlv * provisioning_list_tlv_dsk_get(uint8_t dsk_len, const uint8_t *dsk, uint8_t type)
{
    struct provision *pvs = provisioning_list_dev_get(dsk_len, dsk);

    if (!pvs) {
        return NULL;
    }
    return pvs_tlv_get(pvs->tlv_list, type);
}

struct pvs_tlv * provisioning_list_tlv_get(struct provision *pvs, uint8_t type)
{
    if (!pvs || (pvs->dsk_len == 0)) {
        return NULL;
    }
    return pvs_tlv_get(pvs->tlv_list, type);
}

/** Clear the entire tlv list.  Do not bother to update the list pointer along the way. */
void pvs_tlv_clear(struct pvs_tlv *tlv)
{
    /*@null@*/struct pvs_tlv *next = NULL;

    while (tlv)
    {
        next = tlv->next;
        /* If the tlv has allocated data, free it */
        if (tlv->length)
        {
            free(tlv->value);
        }
        free(tlv);
        tlv = next;
    }
}

/** Unlink a tlv from the tlv list (changing the pointer that pointed
 * to this element to point to the next element instead) and free the
 * associated memory. */
static void pvs_tlv_free(struct pvs_tlv **prev, struct pvs_tlv *dead)
{
    *prev = dead->next;
    if (dead->length) {
        free(dead->value);
    }
    free(dead);
}

int provisioning_list_tlv_remove(struct provision *pvs, uint8_t type)
{
    /*@null@*/struct pvs_tlv **tlv_handle;

    if (!pvs || (pvs->dsk_len == 0)) {
        return PVS_ERROR;
    }
    tlv_handle = &pvs->tlv_list;
    while (*tlv_handle)
    {
        if ((*tlv_handle)->type == type) {
            pvs_tlv_free(tlv_handle, *tlv_handle);
#ifdef PVS_TEST
            pvs->num_tlvs--;
#endif
            pvs_list_persist_in_file();
            return PVS_SUCCESS;
        } else {
            tlv_handle = &((*tlv_handle)->next);
        }
    }
    return PVS_ERROR;
}

static const char* pvs_tlv_type_to_str(uint8_t t)
{
    switch (t) {
    case PVS_TLV_TYPE_NAME:
        return NAME_OF(PVS_TLV_TYPE_NAME);

    case PVS_TLV_TYPE_LOCATION:
        return NAME_OF(PVS_TLV_TYPE_LOCATION);

    case PVS_TLV_TYPE_PRODUCT_TYPE:
        return NAME_OF(PVS_TLV_TYPE_PRODUCT_TYPE);

    case PVS_TLV_TYPE_PRODUCT_ID:
        return NAME_OF(PVS_TLV_TYPE_PRODUCT_ID);

    }
    return "unknown type";
}

void provisioning_list_print()
{
    pvs_list_print(stdout);
}

static void pvs_tlv_print(FILE *strm, struct pvs_tlv *__tlv)
{
    int ii;
    struct pvs_tlv * tlv = __tlv;
    while (tlv) {
        fprintf(strm, "type: %s (%d), len: %u, val: 0x",
                pvs_tlv_type_to_str(tlv->type), tlv->type, tlv->length);
        for (ii=0; ii<tlv->length; ii++) {
            fprintf(strm, ", 0x%02x", tlv->value[ii]);
        }
        if (((tlv->type == PVS_TLV_TYPE_NAME) || (tlv->type == PVS_TLV_TYPE_LOCATION))
            && (tlv->value[tlv->length-1] == 0) )
        {
            /* Let us go out on a limb and asume this is a printable string */
            fprintf(strm, "(%s)", (char*)(tlv->value));
        }
        fprintf(strm, "\n");
        tlv = tlv->next;
    }
}

void pvs_dsk_print(FILE *strm, uint8_t dsk_len, uint8_t *dsk)
{
    uint8_t j;

    for (j = 0; j < dsk_len; j++)
    {
        fprintf(strm, ", 0x%02X", dsk[j]);
    }
}

void pvs_list_print(FILE *strm)
{
    uint16_t i = 0;
    rd_node_database_entry_t *rd_dbe = NULL;

    fprintf(strm, "Provisioning list contents:\n");
    for (i = 0; i < PROVISIONING_LIST_SIZE; i++)
    {
        if (!pvs_list[i].dsk_len) /* If the ith provisioning_list slot is blank, dont try to print it */
        {
            assert(pvs_list[i].tlv_list == NULL);
            continue;
        }

        rd_dbe = rd_lookup_by_dsk(pvs_list[i].dsk_len, pvs_list[i].dsk);

        fprintf(strm, "-----------------------------------\n");
        fprintf(strm, "Item %u:\ndsk: 0x", i);
        pvs_dsk_print(strm, pvs_list[i].dsk_len, pvs_list[i].dsk);
        if (rd_dbe != NULL) {
           fprintf(strm, "\nInclusion status: Included, nodeid: %u", rd_dbe->nodeid);
           fprintf(strm, " %s",
                   (rd_dbe->state == STATUS_FAILING) ?
                   "PVS_NETSTATUS_FAILING" :
                   "PVS_NETSTATUS_INCLUDED");
        } else {
           fprintf(strm, "\nNot included\n");
        }
        fprintf(strm, "\nstatus: %u\n", pvs_list[i].status);
        pvs_tlv_print(strm, pvs_list[i].tlv_list);
    }
    if (pvs_store_filename) {
        fprintf(strm, "Provisioning list storage:%s\n", pvs_store_filename);
    } else {
        fprintf(strm, "No provisioning list storage configured.\n");
    }
}

int provisioning_list_status_set(struct provision *pvs, pvs_status_t status)
{
    assert(pvs);
    if (!pvs) {
      return PVS_ERROR;
    }

    pvs->status = status;
    pvs_list_persist_in_file();
    return PVS_SUCCESS;
}

/**
 * @}
 */
