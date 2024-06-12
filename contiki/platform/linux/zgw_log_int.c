/** @file zgw_log.c
 *
 * \addtogroup zgw_log
 */

#include <zgw_log.h>
#include "zgw_log_int.h"
#include <stdarg.h>

#if defined(ZGW_LOG_LOG_TO_FILE) && defined(ZGW_LOG_MAX_SZ)
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#endif

zgw_log_id_define(log_feat)
zgw_log_id_default_set(log_feat)

/* ************************************************************
   External definitions
 ************************************************************ */

#ifdef ZGW_LOG

FILE *zgw_log_f = NULL;

uint32_t zgw_log_cnt = 0;

#ifdef ZGW_LOG_LOG_TO_FILE

/** Default name of the log file.  Can be set at compile time.
 */
char *zgw_log_filename = ZGW_LOG_FILENAME;

/* **************************************************
   Local declarations
 ************************************************** */

#ifdef ZGW_LOG_MAX_SZ
/** Secondary log file, for the rotating log.
 *
 * Allocated and stored in the log component.  Free'd in case of
 * orderly close (TODO: add to handlers).
 */
static char *zgw_log_filename2 = NULL;
#endif

#else

/** Pointer to the name of the log file.
 *
 * If set at launch time, log assumes the actual string is malloc'd
 * and stored elsewhere and will not copy it or free it.
 *
 * TODO: Set at launch time.
 */
static char *zgw_log_filename = NULL;

#endif


/**
 * Set up log destination to file or stdout
*/
static int zgw_logfile_setup(char* filename);

/**
 * Remove path from a file name.
 */
static const char *path_trim(const char *name);

/**
 *  Shorten \ref name to at most \ref sz character and put it in \ref dest.
 */
static void print_name_trim(char *dest, const char *name, size_t sz);

/** Log statement timestamp.
 *
 * To simplify debugging and cross-file identification, all log statements are timestamped.
 */
static struct timeval zgw_log_timeval;

static struct tm zgw_log_tm;

/**
 * A static place to put the log prefix, so that logging can
 * manipulate strings without malloc.
 *
 * Note that this assumes that everything that goes into the prefix
 * has fixed sizes and fit into this buffer.
 *
 * The current layout is:
 * 0-5 - date
 * 6 - blank
 * 7-21 time
 * 22 - blank
 * 23-33 - file
 * 34 - :
 * 35-39 - line number
 * 40 - blank
 * 41-57 - function
 * 58 - blank
 * 59 - sequence number
 * 60-61 - ': '
 *
 * This could be shorter!
 */
char zgw_log_str_buf[ZGW_LOG_STR_BUF_SZ];

/*
   Implementation of local funtions.
*/

/** Setup a log file.  No attempts to make this platform independent.
 *
 * If file handling fails, just continue with stdout.
 */
static int zgw_logfile_setup(char* filename) {
  zgw_log_f = stdout;
#ifdef ZGW_LOG_LOG_TO_FILE
  zgw_log_f = fopen(filename, "w");
  if (zgw_log_f) {
    printf("Logging enabled, logging to file %s\n", filename);
  } else {
    printf("Opening log file %s failed, logging to stdout\n", filename);
    zgw_log_f = stdout;
  }
#endif
  return 0;
}

/*
   Implementation of external funtions.
*/

/** Look up time and put a timestamp in \ref zgw_log_timeval.
 *
 * Also leaves a formatted date/time in \ref zgw_log_str_buf.
 * \return How many bytes were printed into the buffer.
*/
size_t _zgw_log_get_timestamp(void) {
    gettimeofday(&zgw_log_timeval, NULL);
    zgw_log_tm = *localtime(&zgw_log_timeval.tv_sec);
    return strftime(zgw_log_str_buf,
		    ZGW_LOG_STR_BUF_SZ,
		    ZGW_LOG_TIMESTAMP_FORMAT,
		    &zgw_log_tm);
}

/** Put filename, timestamp, etc in the log line prefix.
 */
void _zgw_log_format_prefix(const char* id_name, const char *file, int line, const char *func) {
  size_t offset = _zgw_log_get_timestamp();
  size_t name_len = 0;
  
  file = path_trim(file);
  
  sprintf(zgw_log_str_buf+offset, ".%06ld ", (long int )zgw_log_timeval.tv_usec);
  offset += 8;
  print_name_trim(zgw_log_str_buf+offset, file, 8);
  offset += 16;
  sprintf(zgw_log_str_buf+offset, ":%-5d ", line);
  offset += 7;
  print_name_trim(zgw_log_str_buf+offset, func, 8);
  offset += 16;
  snprintf(zgw_log_str_buf+offset, 12, " %-10s", id_name);
  offset += 11;
  zgw_log_str_buf[offset] =' ';
  zgw_log_str_buf[offset+1] ='\0';
}

/** Print exactly 2*sz bytes of name to dest.
 * 
 * If name is shorter than 2*sz, pad with blanks.
 *
 * If name is longer, print the first and last (sz-1) bytes and print
 * '<>' in the middle to indicate name has been abbreviated.
 */
static void print_name_trim(char *dest, const char *name, size_t sz) {
  size_t len;
  char frmt[5];

  /* Cut down to 16 bytes */
  len = strlen(name);
  if (len > sz*2) {
    strncpy(dest, name, sz-1);
    dest[sz-1] = '<';
    dest[sz] = '>';
    strncpy(dest+sz+1, name + (len - (sz-1)), sz-1);
  } else {
    sprintf(frmt, "%%%zus", 2*sz);
    sprintf(dest, frmt, name);
  }
}

/**
 * This version is for Linux with '/' path delimiter.
 *
 */
static const char *path_trim(const char *name) {
  const char *res = strrchr(name, '/');

  /* Remove the path */
  if (res) {
    /* Skip the '/' */
    res++;
  } else {
    /* There was no path in the name, so just use as is */
    res = name;
  }
  return res;
}

#if defined(ZGW_LOG_LOG_TO_FILE) && defined(ZGW_LOG_MAX_SZ)
/**
 * \return 0 if rotation is not needed, 1 if rotation happened, -1 in case of error.
 */
int zgw_logfile_trim(void) {
  struct stat buf;
  int res;

  if (fstat(fileno(zgw_log_f), &buf)) {
      printf("Failed to find size of log file %d, %s\n", errno, strerror(errno));
      return -1;
  }

  if (buf.st_size > ZGW_LOG_MAX_SZ) {
    zgw_log(0, "Should trim, %zd > %zd\n", (size_t)buf.st_size, (size_t)ZGW_LOG_MAX_SZ);

    /* Close the full file */
    fclose(zgw_log_f);

    /* just in case, back up output to stdout */
    zgw_log_f = stdout;

    /* Move log file to secondary file (overwriting old secondary) */
    if (zgw_log_filename2) {
      if (rename(zgw_log_filename, zgw_log_filename2) != 0) {
	printf("Failed to save log file with error %s (%d)\n",
	       strerror(errno), errno);
      }
    } else {
      printf("No backup file available.");
    }

    /* Open new log file. */
    zgw_log_f = fopen(zgw_log_filename, "w");
    if (zgw_log_f) {
      fprintf(zgw_log_f, "Restarted log file %s\n", zgw_log_filename);
    } else {
      printf("Opening log file %s failed, logging to stdout\n", zgw_log_filename);
      zgw_log_f = stdout;
      return -1;
    }
    return 1;
  }

  return 0;
}
#endif

/**
 * @param filename Pointer to a log file name somewhere permanent.
 */
int zgw_log_setup_int(char* filename) {
  zgw_log_id_default_set(log_feat);
  printf("Logging enabled, initial level: %d\n", ZGW_LOG_LVL_INIT);

#ifdef ZGW_LOG_LOG_TO_FILE
  if (filename) {
    zgw_log_filename = filename;
  }
#ifdef ZGW_LOG_MAX_SZ
  {
    /* Set up second filename for rotating */
    size_t bufsize = strlen(zgw_log_filename)+1;
    char *tmpname = malloc(bufsize+1);

    printf("Set up secondary log file\n");
    if (tmpname) {
      strncpy(tmpname, zgw_log_filename, bufsize);
      tmpname[bufsize] = '\0';
      tmpname[bufsize-1] = '2';
    }
    printf("tmpname: %s\n", tmpname);
    zgw_log_filename2 = tmpname;
  }
#endif
#endif

  zgw_logfile_setup(zgw_log_filename);

  /* Now we can start logging */
  zgw_log(0, "Logging enabled, initial level: %d\n", ZGW_LOG_LVL_INIT);

  zgw_log(zwlog_lvl_critical, "Level %u enabled\n", 0);
  zgw_log(zwlog_lvl_error, "Level %u enabled\n", 1);
  zgw_log(2, "Level %u enabled\n", 2);
  zgw_log(3, "Level %u enabled\n", 3);
  zgw_log(4, "Level %u enabled\n", 4);
  zgw_log(5, "Level %u enabled\n", 5);
  zgw_log(6, "Level %u enabled\n", 6);
  zgw_log(7, "Level %u enabled\n", 7);
  zgw_log(8, "Level %u enabled\n", 8);
  zgw_log(9, "Level %u enabled\n", 9);
  return 0;
}

int zgw_log_teardown_int(void) {
  zgw_log(0, "Stopping log\n");
#ifdef ZGW_LOG_LOG_TO_FILE
  if (zgw_log_f) {
    fclose(zgw_log_f);
    zgw_log_f = NULL;
  }
#ifdef ZGW_LOG_MAX_SZ
  free(zgw_log_filename2);
  zgw_log_filename2 = NULL;
#endif
#endif
  return 0;
}

#else // ZGW_LOG

//

#endif
