#ifndef _ZGW_LOG_INT_H_
#define _ZGW_LOG_INT_H_

#ifdef ZGW_LOG

/** \ingroup zgw_log
 * Initialize the logging feature, Linux implementation.
 */
int zgw_log_setup_int(char *filename);

/** \ingroup zgw_log
 * Tear down the logging feature, Linux implementation.
 */
int zgw_log_teardown_int();

/** \ingroup zgw_log
 * Shorten a file name to fit in the log prefix area.
 */
const char *zgw_log_name_trim(const char *name);

#if defined(ZGW_LOG_LOG_TO_FILE) && defined(ZGW_LOG_MAX_SZ)
/**
 * \ingroup zgw_log
 */
int zgw_log_logfile_move(void);
#endif

#endif

#endif
