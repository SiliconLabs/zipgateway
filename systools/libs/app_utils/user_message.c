/* © 2019 Silicon Laboratories Inc. */
#include <stdio.h>
#include <stdarg.h>
#include "user_message.h"

static message_severity_t severity_limit = MSG_WARNING;  // Show warnings and above

message_severity_t set_message_level(message_severity_t severity)
{
  message_severity_t old_severity_limit = severity_limit;
  severity_limit = severity;
  return old_severity_limit;
}

message_severity_t get_message_level(void)
{
  return severity_limit;
}

bool is_message_severity_enabled(message_severity_t severity)
{
  return (severity >= severity_limit) ? true : false;
}

void user_message(message_severity_t severity, const char *fmt, ...) 
{
  if (is_message_severity_enabled(severity))
  {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
  }
}
