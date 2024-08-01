/*
 * Copyright 2020 Silicon Laboratories Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LIBZW_LOG_H
#define LIBZW_LOG_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/**
 * @defgroup logging Logging Severities API
 * @ingroup zwaveip
 *
 * Logging API with fine severities tuning
 *
 * @{
 */
typedef enum {
  LOGLVL_ALL = 0,
  LOGLVL_TRACEX,  // Used for low level OpenSSL tracing
  LOGLVL_TRACE,
  LOGLVL_DEBUG,
  LOGLVL_INFO,
  LOGLVL_WARN,
  LOGLVL_ERROR,
  LOGLVL_FATAL,
  LOGLVL_NONE  // Keep this one last
} log_severity_t;

/**
 * Initialize the logging module.
 *
 * This function must be called before logging anything. Don't call more than one time.
 *
 * @param path The full filename of the logfile
 * @return true Log file created and module initialized.
 * @return false Log file could not be created (error printed to stderr) or log
 *         module already initialized.
 */
bool libzw_log_init(const char *path);

/**
 * Set the logging severity filter level.
 *
 * Note that log requests below this severity are ignored.
 *
 * @param level Severity level
 * @return log_severity_t Previous severity level in effect.
 */
log_severity_t libzw_log_set_filter_level(log_severity_t level);

/**
 * Set the logging severity level causing messages to be displayed to the user.
 *
 * Log requests where the severity is at least equal to the UI message severity
 * level will cause the message to be passed to either UI_MSG() or UI_MSG_ERR()
 * in addition to being logged to file.
 *
 * @param level Severity level
 * @return log_severity_t Previous severity level
 */
log_severity_t libzw_log_set_ui_message_level(log_severity_t level);

/**
 * Check whether the logging module will accept logging requests at the indicated severity.
 *
 * This function can be used to check if blocks of code generating logging output need to be called.
 *
 * Compares with the value previously set with libzw_log_set_filter_level()
 *
 * @param severity Logging severity to check
 * @return true Message at this severity will be logged
 * @return false Message at this severity will not be logged
 */
bool libzw_log_check_severity(log_severity_t severity);

/**
 * Format log messages and write to log file.
 *
 * Don't call this function directly. Use one of the LOG_XXX macros instead.
 *
 * Note that this function will add a line feed to all messages.
 *
 * @param severity Message severity
 * @param file Source file name
 * @param line Source file line number
 * @param func Function name
 * @param fmt Format string (like printf)
 * @param ... Arguments to use with the format string
 */
void libzw_log(log_severity_t severity, const char *file, uint32_t line, const char *func, const char *fmt, ...);

/**
 * Convenience macros to log with severity and detailed location information.
 * Use instead of calling libzw_log() directly.
 */
#define LOG_TRACEX(fmt, ...) libzw_log(LOGLVL_TRACEX, __FILE__, __LINE__, __func__, fmt, ## __VA_ARGS__)
#define LOG_TRACE(fmt, ...)  libzw_log(LOGLVL_TRACE, __FILE__, __LINE__, __func__, fmt, ## __VA_ARGS__)
#define LOG_DEBUG(fmt, ...)  libzw_log(LOGLVL_DEBUG, __FILE__, __LINE__, __func__, fmt, ## __VA_ARGS__)
#define LOG_INFO(fmt, ...)   libzw_log(LOGLVL_INFO,  __FILE__, __LINE__, __func__, fmt, ## __VA_ARGS__)
#define LOG_WARN(fmt, ...)   libzw_log(LOGLVL_WARN,  __FILE__, __LINE__, __func__, fmt, ## __VA_ARGS__)
#define LOG_ERROR(fmt, ...)  libzw_log(LOGLVL_ERROR, __FILE__, __LINE__, __func__, fmt, ## __VA_ARGS__)
#define LOG_FATAL(fmt, ...)  libzw_log(LOGLVL_FATAL, __FILE__, __LINE__, __func__, fmt, ## __VA_ARGS__)

/**
 * Short hand for logging errors based on errno
 */
// NB: Don't add line breaks to the macro definition - will likely mess up the __LINE__ macro
#define LOG_ERRNO(loglvl, fmt, errnum, ...) do { char buf[1024] = {0}; strerror_r((errnum), buf, sizeof(buf)); libzw_log((loglvl), __FILE__, __LINE__, __func__, fmt " errno=%d: \"%s\"", ## __VA_ARGS__, (errnum), buf); } while (0)

/**
 * A wrapper to send a message to the user interface. Currently prints to
 * stdout. Redefine for other requirements. Message will not go to the log file.
 */
#define UI_MSG(fmt, ...) do { fprintf(stdout, fmt, ## __VA_ARGS__); fflush(stdout); } while(0)

/**
 * A wrapper to send an error message to the user interface. Currently
 * prints to stderr. Redefine for other requirements. Message will not go to the
 * log file.
 */
#define UI_MSG_ERR(fmt, ...) do { fprintf(stderr, fmt, ## __VA_ARGS__); fflush(stderr); } while(0)

/**
 * @}
 */

#endif // LIBZW_LOG_H
