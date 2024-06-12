/* © 2019 Silicon Laboratories Inc. */
#ifndef _APP_UTILS_USER_MESSAGE_H
#define _APP_UTILS_USER_MESSAGE_H

#include <stdbool.h>

/**
 * @brief Simple severity level controlled printing to the console 
 * 
 * Can be used in e.g. command line utilities to output informational
 * messages to the user.
 */

typedef enum 
{
  MSG_TRACE,
  MSG_DIAG,
  MSG_INFO,
  MSG_WARNING,
  MSG_ERROR,
  MSG_ALWAYS
} message_severity_t;

/**
 * @brief Suppress messages with severity below a given thresshold
 * 
 * Messages specified with user_message() having a severity equal
 * to or above the specified threshold will be printed.
 * 
 * @param severity Severity thresshold
 * @return the severity thresshold that was in effect when calling
 *         this function.
 */
message_severity_t set_message_level(message_severity_t severity);

/**
 * @brief Get the currently active message level thresshold
 * 
 * @return message_severity_t Message severity thresshold.
 */
message_severity_t get_message_level(void);

/**
 * @brief Check if a message of a specified severity will be printed
 * 
 * Check if a the message will actually be printed given the currently
 * active severity thresshold.
 * 
 * If e.g. multiple functions must be called in order to generate a
 * specific message those functions can be skipped alltogether if the
 * message will not be printed anyway.
 * 
 * @param severity The message severity to check.
 * @return true    Messages with the given severity will be printed.
 * @return false   Messages with the given severity will NOT be printed.
 */
bool is_message_severity_enabled(message_severity_t severity);

/**
 * @brief Print a message with a given severity.
 * 
 * The message provided will only be printed if the severity is equal
 * to or higher than the currently active message severity thresshold
 * (@see set_message_level()).
 * 
 * @param severity The message severity.
 * @param fmt      printf() format string
 * @param ...      Arguments according to fmt
 */
void user_message(message_severity_t severity, const char *fmt, ...);

#endif // _APP_UTILS_USER_MESSAGE_H