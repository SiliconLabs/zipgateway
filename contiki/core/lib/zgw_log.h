#ifndef _ZGW_LOG_H_
#define _ZGW_LOG_H_

/** \defgroup zgw_log Configurable Logging Feature.
 *
 * Adjustable logging and debug trace feature.
 *
 * Under development.
 *
 * Currently supports:
 * - printing if certain conditions (on level and feature-set) are met
 * - not printing if compiled for no support
 * - configuring initial level at compile time
 * - configuring level at run time in the running process
 * - logging to more than one feature in one file.
 * - gcc compiler on Ubuntu
 * - log to file (not very well tested)
 * Soon to be added:
 * - rotating file (max-size on file)
 * - compile-time controlled max level.
 * Maybe to be added:
 * - asserts
 * - external run-time control of (default) levels (inter-process communication).
 * - launch-time controlled levels (through command-line options or file).
 * @{
 */

#ifdef ZGW_LOG

#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>

/* Include version-specific .h file */
#include <zgw_log_int.h>

/** Type of the feature logging identifiers. */
typedef uint8_t zgw_log_feat_t;

/** Handy names for the log levels
 * This part is still TBD
 */
/** Essential information - zgw is about to crash or in an unstable
 * state. */
#define  zwlog_lvl_critical        0
/** Information about errors - something important failed to happen,
 * but the zgw itself is stable. */
#define  zwlog_lvl_error           1
/** Information about potential problems.  Something failed to happen
 * in a legal way, but the user probably needs to know. */
#define  zwlog_lvl_warning         2
/** Successful configuration changes in gateway or networks. */
#define  zwlog_lvl_configuration   3
/** Interesting information that is neither important in general nor erroneous in nature */
#define  zwlog_lvl_info            4
/** Verbose information, but still relevant to user. */
#define  zwlog_lvl_verbose         5
  /** Information about control operations.  Development only level.  

    Do not print to a lower level than this if the log output is very
    verbose or might otherwise affect timing (ie, in a tight loop) */
#define  zwlog_lvl_control         6
/** Entering and leaving interesting functions.  Development only level. */
#define  zwlog_lvl_function_trace  7
/** Misc debug helpers.  Development only level. */
#define  zwlog_lvl_debug           8
/** Data dumps.  Devel only level. */
#define  zwlog_lvl_data            9
/** max number of levels */
#define  zwlog_lvl_max_lvls       11

#if !defined(ZGW_LOG_LVL_MAX)
/* TODO: max compiled levels */
/** This is the max number of levels included in the build.  Do not
    confuse with the max number defined. */
#define ZGW_LOG_LVL_MAX 16
#endif

#if ZGW_LOG_LVL_INIT > ZGW_LOG_LVL_MAX
#error "Cannot set ZGW_LOG_LVL_INIT > ZGW_LOG_LVL_MAX."
#endif

#ifdef ZGW_LOG_COLOR
/* Colorization helpers for bash */
/** Log colorization helper.
 */
#define zwlog_color_yellow "\033[33;1m"
/** Log colorization helper.
 */
#define zwlog_color_green "\033[32;1m"
/** Log colorization helper.
 */
#define zwlog_color_red "\033[31;1m"
/** Log colorization helper.
 */
#define zwlog_color_none "\033[0m"
#else
/** Dummy definition for disabled feature.
 */
#define zwlog_color_yellow ""
/** Dummy definition for disabled feature.
 */
#define zwlog_color_green ""
/** Dummy definition for disabled feature.
 */
#define zwlog_color_red ""
/** Dummy definition for disabled feature.
 */
#define zwlog_color_none ""
#endif

/** Size of the zwlog helper buffer.
 *
 * It must be big enough to fit the timestamp you want to print in the zwlog.
 */
#define ZGW_LOG_STR_BUF_SZ 160

/** Format of the zwlog timestamps.
 *
 * See 'man strftime' on Linux.
 * For other platforms, refer to your local documentation.
 */
#define ZGW_LOG_TIMESTAMP_FORMAT "%y%m%d %T"

#ifdef ZGW_LOG_LOG_TO_FILE

#ifndef ZGW_LOG_FILENAME
#define ZGW_LOG_FILENAME  "/tmp/zgw.log"
#if defined(ZGW_LOG_LOG_TO_FILE) && defined(ZGW_LOG_MAX_SZ)
#define ZGW_LOG_FILENAME2  "/tmp/zgw.log2"
#endif
#endif

#endif /* ZGW_LOG_LOG_TO_FILE */

/** The stream for the zwlog file.  Initialized to log to stdout.
 */
extern FILE *zgw_log_f;

/** Log statement counter.
 *
 * To simplify debugging and cross-file identification, all log statements are counted.
 */
extern uint32_t zgw_log_cnt;

/** Log timestamp helper buffer.
 */
extern char zgw_log_str_buf[];

/** The representation of a loggable feature. */
struct zgw_log_feat_entry {
  /** The current setting for the feature. */
  zgw_log_feat_t zwlog_feat_val;
  /* The name of the feature, to print in the log. */
  const char * zwlog_feat_name; /**< Printable name for the feature group. */
};

/** The type of zgw_log_feat_entry. */
typedef struct zgw_log_feat_entry zgw_log_feat_entry_t;

/** Declare a feature id, to be able to log to this feature in this
 * scope.
 * \param zwlog_id The logging name for the feature.
 */
#  define zgw_log_id_declare(zwlog_id)              \
  extern zgw_log_feat_entry_t *zgw_##zwlog_id;

/** Define a feature id.
 * \param zwlog_id A logging name for the feature.
 */
#  define zgw_log_id_define(zwlog_id)               \
  zgw_log_feat_entry_t zgw_##zwlog_id##_entry = {ZGW_LOG_LVL_INIT, #zwlog_id}; \
  zgw_log_feat_entry_t *zgw_##zwlog_id = &zgw_##zwlog_id##_entry;

/** Declare the primary log id to be used in this scope.
 */
#define zgw_log_id_default_set(_zwlog_id) static zgw_log_feat_entry_t **zgw_default_id = &zgw_##_zwlog_id;

/** Set the output level of a feature id
 * \param zwlog_id The  feature id.
 * \param lvl The new log level.
*/
#  define zgw_log_lvl_set(zwlog_id, lvl)             \
  zgw_##zwlog_id->zwlog_feat_val = lvl

/** Set the output level of the current default feature id
 * \param lvl The new log level.
*/
#  define zgw_log_lvl_default_set(lvl)             \
  (*zgw_default_id)->zwlog_feat_val = lvl

#if defined(ZGW_LOG_LOG_TO_FILE) && defined(ZGW_LOG_MAX_SZ)
/** Check every 64 lines whether the log is too big in bytes */
#  define zgw_log_check_file_sz()		     \
  do {						     \
    if ((zgw_log_cnt & 0x3F) == 0) {	      \
      zgw_logfile_trim();			      \
    }						      \
  } while (0)
#else
/** Dummy definition for disabled feature.
 */
#  define zgw_log_check_file_sz()
#endif

/** Put a string representing a timestamp in \ref zgw_log_timeval.
 *
 * Can be adjusted to match different platforms.
 */
size_t _zgw_log_get_timestamp(void);

void _zgw_log_format_prefix(const char* id_name, const char *file, int line, const char *func);

/** Check the size of the log file and rotate if it is too big.
 */
int zgw_logfile_trim(void);


/** The main logging statement.  Log to the default log_id feature.
 * 
 * \param lvl The logging level for this statement.  Statement will be
 * printed only if this level is enabled.
 * \param fmtstr A printf style formatting string, followed by varargs.
 *
 * TBD: Do we want to always add a newline? */
#define zgw_log(lvl, fmtstr, ...) \
  zgw_log_to_int(lvl, \
		 (*zgw_default_id)->zwlog_feat_val,\
		 (*zgw_default_id)->zwlog_feat_name, \
		 __FILE__, __LINE__, __func__, \
		 fmtstr, ##__VA_ARGS__)

/** Log to the named log_id feature.
 * \param log_id The feature logging name.
 * \param lvl The logging level for this statement.  Statement will be
 * printed only if this level is enabled.
 * \param fmtstr A printf style formatting string, followed by varargs.
 *
 * TBD: Do we want to always add a newline? */
#define zgw_log_to(log_id, lvl, fmtstr, ...) \
  zgw_log_to_int(lvl,				\
		 (zgw_##log_id)->zwlog_feat_val,\
		 (zgw_##log_id)->zwlog_feat_name,	\
		 __FILE__, __LINE__, __func__, \
		 fmtstr, ##__VA_ARGS__) 

/** Log macro to log at level 0 (always print if logging is included).
 */
#define zgw_log_at_0(zwlog_feat_val, id_name, file, line, func, fmtstr, ...) \
  do {									\
    zwlog_do_log(id_name, file, line, func, fmtstr, ##__VA_ARGS__);	\
  } while (0)

/** Log macro to log at level 1.
 */
#if ZGW_LOG_LVL_MAX >= 1
#define zgw_log_at_1(zwlog_feat_val, id_name, file, line, func, fmtstr, ...) \
  do {									\
    if (1 <= zwlog_feat_val) {						\
      zwlog_do_log(id_name, file, line, func, zwlog_color_red fmtstr zwlog_color_none , ##__VA_ARGS__); \
    }									\
  } while (0)
#else
#define zgw_log_at_1(log_id, fmtstr, ...)
#endif

/** Log macro to log at level 2 */
#if ZGW_LOG_LVL_MAX >= 2
#define zgw_log_at_2(zwlog_feat_val, id_name, file, line, func, fmtstr, ...) \
  do {									\
    if (2 <= zwlog_feat_val) {						\
      zwlog_do_log(id_name, file, line, func, zwlog_color_yellow fmtstr zwlog_color_none, ##__VA_ARGS__);	\
    }									\
  } while (0)
#else
#define zgw_log_at_2(log_id, fmtstr, ...)
#endif

/** Log macro to log at level 3 */
#if ZGW_LOG_LVL_MAX >= 3
#define zgw_log_at_3(zwlog_feat_val, id_name, file, line, func, fmtstr, ...) \
  do {									\
    if (3 <= zwlog_feat_val) {						\
      zwlog_do_log(id_name, file, line, func, zwlog_color_green fmtstr zwlog_color_none, ##__VA_ARGS__);	\
    }									\
  } while (0)
#else
#define zgw_log_at_3(log_id, fmtstr, ...)
#endif

/** Log macro to log at level 4 */
#if ZGW_LOG_LVL_MAX >= 4
#define zgw_log_at_4(zwlog_feat_val, id_name, file, line, func, fmtstr, ...) \
  do {									\
    if (4 <= zwlog_feat_val) {						\
      zwlog_do_log(id_name, file, line, func, fmtstr, ##__VA_ARGS__);	\
    }									\
  } while (0)
#else
#define zgw_log_at_4(log_id, fmtstr, ...)
#endif

/** Log macro to log at level 5 */
#if ZGW_LOG_LVL_MAX >= 5
#define zgw_log_at_5(zwlog_feat_val, id_name, file, line, func, fmtstr, ...) \
  do {									\
    if (5 <= zwlog_feat_val) {						\
      zwlog_do_log(id_name, file, line, func, fmtstr, ##__VA_ARGS__);	\
    }									\
  } while (0)
#else
#define zgw_log_at_5(log_id, fmtstr, ...)
#endif

/** Log macro to log at level 6 */
#if ZGW_LOG_LVL_MAX >= 6
#define zgw_log_at_6(zwlog_feat_val, id_name, file, line, func, fmtstr, ...) \
  do {									\
    if (6 <= zwlog_feat_val) {						\
      zwlog_do_log(id_name, file, line, func, fmtstr, ##__VA_ARGS__);	\
    }									\
  } while (0)
#else
#define zgw_log_at_6(log_id, fmtstr, ...)
#endif

/** Log macro to log at level 7 */
#if ZGW_LOG_LVL_MAX >= 7
#define zgw_log_at_7(zwlog_feat_val, id_name, file, line, func, fmtstr, ...) \
  do {									\
    if (7 <= zwlog_feat_val) {						\
      zwlog_do_log(id_name, file, line, func, fmtstr, ##__VA_ARGS__);	\
    }									\
  } while (0)
#else
#define zgw_log_at_7(log_id, fmtstr, ...)
#endif

/** Log macro to log at level 8 */
#if ZGW_LOG_LVL_MAX >= 8
#define zgw_log_at_8(zwlog_feat_val, id_name, file, line, func, fmtstr, ...) \
  do {									\
    if (8 <= zwlog_feat_val) {						\
      zwlog_do_log(id_name, file, line, func, fmtstr, ##__VA_ARGS__);	\
    }									\
  } while (0)
#else
#define zgw_log_at_8(log_id, fmtstr, ...)
#endif

/** Log macro to log at level 9 */
#if ZGW_LOG_LVL_MAX >= 9
#define zgw_log_at_9(zwlog_feat_val, id_name, file, line, func, fmtstr, ...) \
  do {									\
    if (9 <= zwlog_feat_val) {						\
      zwlog_do_log(id_name, file, line, func, fmtstr, ##__VA_ARGS__);	\
    }									\
  } while (0)
#else
#define zgw_log_at_9(log_id, fmtstr, ...)
#endif


/** The actual logger.
 */
#define zwlog_do_log(id_name, file, line, func, fmtstr, ...)	\
  do {									\
    _zgw_log_format_prefix(id_name, file, line, func);						\
    fprintf(zgw_log_f, "%5u ", zgw_log_cnt);			\
    fprintf(zgw_log_f, "%s", zgw_log_str_buf);			\
    fprintf(zgw_log_f, fmtstr, ##__VA_ARGS__);				\
    zgw_log_cnt++;							\
    zgw_log_check_file_sz();						\
  } while (0)

#define zgw_log_to_int(lvl, zwlog_feat_val, id_name, file, line, func, fmtstr, ...) \
  zgw_log_at_##lvl(zwlog_feat_val, id_name, file, line, func, fmtstr, ##__VA_ARGS__)

/** Log entering a function to the default feature.
 * 
 */
#define zgw_log_enter()\
      zgw_log(zwlog_lvl_function_trace, "%s, l. %d: Enter %s\n", __FILE__, __LINE__, __func__)

/** Log exiting a function to the default feature .
 *
 * For readability, always use this when \ref zgw_log_enter() is used.
 */
#define zgw_log_exit()						\
      zgw_log(zwlog_lvl_function_trace, "%s, l. %d: Exit %s\n", __FILE__, __LINE__, __func__)

/** Initialize the logging feature.
 *
 * The exact implementation of this may depend on the underlying platform.
 *
 * Setup the defaults, open the log file, etc.
 */
#define zgw_log_setup(filename) zgw_log_setup_int(filename)

/** Tear down the logging feature.
 *
 * The exact implementation of this may depend on the underlying platform.
 *
 * Close the log file, release memory, etc.
 */
#define zgw_log_teardown() zgw_log_teardown_int()

#else // ZGW_LOG

/** Empty define, for the no logging scenario. */
#define zgw_log_id_define(log_id)
/** Empty define, for the no logging scenario. */
#define zgw_log_id_declare(log_id)
/** Empty define, for the no logging scenario. */
#define zgw_log_id_default_set(log_id) 

/** Empty define, for the no logging scenario. */
#define zgw_log_lvl_set(log_id, lvl)
/** Empty define, for the no logging scenario. */
#define zgw_log_lvl_default_set(lvl)

/** Empty define, for the no logging scenario. */
#define zgw_log(lvl, fmtstr, ...)
#define zgw_log_to(log_id, lvl, fmtstr, ...)			\
/** Empty define, for the no logging scenario. */
#define zgw_log_enter()
/** Empty define, for the no logging scenario. */
#define zgw_log_exit()
/** Empty define, for the no logging scenario. */
#define zgw_log_setup(filename) printf("Logging is disabled\n")
/** Empty define, for the no logging scenario. */
#define zgw_log_teardown() 
#endif

/** @} */
#endif
