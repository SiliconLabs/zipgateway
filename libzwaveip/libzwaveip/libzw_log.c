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

#include "libzw_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>

/** Configure what you want in the printed output */
#define PRINT_LOCATION     1 //!< Include filename, line and function name
#define PRINT_THREADID     1 //!< Include the (pthreads) thread id (or name)
#define THREADID_SHORT     1 //!< Include an integer (starting from 0) as a thread id instead of the (long) real thread id
#define PRINT_TIME         1 //!< Include a timestamp
#define TIME_WITH_DATE     0 //!< Include the date in the timestamp

#if PRINT_LOCATION
//  #define LOC_FMT " %s:%-4d %s"
  #define LOC_FMT " %20s %4d %30s"
  #define LOC_ARGS my_basename(file), line, func,
#else
  #define LOC_FMT 
  #define LOC_ARGS 
#endif

#if PRINT_THREADID
  #if THREADID_SHORT
    #define THRD_FMT " %s"
    #define THRD_ARGS my_thread_name,
  #else
    #define THRD_FMT " %012lx"
    #define THRD_ARGS (uintptr_t)pthread_self(),
  #endif
#else
  #define THRD_FMT
  #define THRD_ARGS
#endif

#if PRINT_TIME
  #if TIME_WITH_DATE
    #define TIME_FMT "%04d%02d%02d-%02d:%02d:%02d.%03d "
    #define TIME_ARGS t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, t_ms,
  #else
    #define TIME_FMT "%02d:%02d:%02d.%03d "
    #define TIME_ARGS t.tm_hour, t.tm_min, t.tm_sec, t_ms,
  #endif
#else
  #define TIME_FMT 
  #define TIME_ARGS 
#endif


typedef struct {
  const char *name;
  FILE *dest;
} severity_info_t;

// Thread local variable
static __thread char my_thread_name[30];

// Global variables (shared by all threads)
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static int thread_count = 0;
static const char *severity_names[LOGLVL_NONE];
static volatile log_severity_t log_filter_level = LOGLVL_TRACE;
static volatile log_severity_t log_ui_message_level = LOGLVL_WARN;
static FILE *log_file = NULL;
static bool initialized = false;

static void init_severity_names(void) {
  severity_names[LOGLVL_TRACEX] = "TRACE";
  severity_names[LOGLVL_TRACE]  = "TRACE";
  severity_names[LOGLVL_DEBUG]  = "DEBUG";
  severity_names[LOGLVL_INFO]   = "INFO";
  severity_names[LOGLVL_WARN]   = "WARN";
  severity_names[LOGLVL_ERROR]  = "ERROR";
  severity_names[LOGLVL_FATAL]  = "FATAL";
}

static const char * my_basename(const char *filename)
{
  const char *last_delim = strrchr(filename, '/');
  if (last_delim && *last_delim != '\0') {
    return last_delim + 1;
  } else {
    return filename;
  }
}

bool libzw_log_init(const char *path) {
  if (initialized) {
    UI_MSG_ERR("ERROR: %s already called.\n", __func__);
    return false;
  }

  log_file = fopen(path, "a");
  if (log_file) {
    /* NB: We'll leave it to the runtime to flush and close the file when the
     *     program terminates */

    init_severity_names();
    initialized = true;
    UI_MSG("Logging to \"%s\"\n", path);
    return true;
  } else {
    char errbuf[1024];
    strerror_r(errno, errbuf, sizeof(errbuf));
    UI_MSG_ERR("ERROR: Failed to open log file \"%s\": %s\n", path, errbuf);
  }

  return false;
}

log_severity_t libzw_log_set_filter_level(log_severity_t level)
{
  pthread_mutex_lock(&log_mutex);
  log_severity_t old = log_filter_level;
  log_filter_level = level;
  pthread_mutex_unlock(&log_mutex);

  return old;
}

log_severity_t libzw_log_set_ui_message_level(log_severity_t level)
{
  pthread_mutex_lock(&log_mutex);
  log_severity_t old = log_ui_message_level;
  log_ui_message_level = level;
  pthread_mutex_unlock(&log_mutex);

  return old;
}

bool libzw_log_check_severity(log_severity_t severity)
{
  return (severity >= log_filter_level) ? true : false;
}

void libzw_log(log_severity_t severity, const char *file, uint32_t line, const char *func, const char *fmt, ...)
{
  static bool init_error_reported = false;

  if (!initialized && !init_error_reported) {
    UI_MSG_ERR("ERROR: libzw_log module not initialized. Must call libzw_log_init() before use. Logging disabled.\n");
    init_error_reported = true;
    return;
  }

  if (severity >= log_filter_level && severity > LOGLVL_ALL && severity < LOGLVL_NONE) {

    if (my_thread_name[0] == 0) {
      pthread_mutex_lock(&log_mutex);
      int tnum = thread_count++;
      // Make the main/first thread (0) stand out by using 'M'
      snprintf(my_thread_name, sizeof(my_thread_name), "%c:%-3d", (tnum) ? 't' : 'M', tnum);
      pthread_mutex_unlock(&log_mutex);
    }

    const char *label = severity_names[severity];
    if (label) {
      char buf[1024] = {0};

      #if PRINT_TIME
        struct timeval tv; 
        struct tm t;
        gettimeofday(&tv, NULL); 
        localtime_r(&tv.tv_sec, &t);
        uint32_t t_ms = tv.tv_usec / 1000;
      #endif

      va_list args;
      va_start(args, fmt);
      vsnprintf(buf, sizeof(buf), fmt, args);
      va_end(args);

      pthread_mutex_lock(&log_mutex);
      fprintf(log_file, TIME_FMT "%5s" THRD_FMT LOC_FMT ": %s\n", TIME_ARGS label, THRD_ARGS LOC_ARGS buf);
      fflush(log_file);
      pthread_mutex_unlock(&log_mutex);

      /* Ideally this section should also be protected by log_mutex (or another
       * mutex), but since they can potentially be redefined to something that
       * does not return quickly it's left unprotected for now. */
      if (severity >= log_ui_message_level) {
        if (severity >= LOGLVL_WARN) {
          UI_MSG_ERR("%s\n", buf);
        } else {
          UI_MSG("%s\n", buf);
        }
      }
    }
  }
}