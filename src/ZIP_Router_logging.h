/* © 2017 Silicon Laboratories Inc.
 */
/*
 * ZIP_Router_logging.h
 *
 *  Created on: Aug 11, 2015
 *      Author: aes
 */

#ifndef SRC_ZIP_ROUTER_LOGGING_H_
#define SRC_ZIP_ROUTER_LOGGING_H_

#include "assert.h"
#include "pkgconfig.h"


#if defined(WIN32)
#define LOG_PRINTF(f, ...) printf(f,  __VA_ARGS__ )
#define ERR_PRINTF(f, ...) printf(f,  __VA_ARGS__ )
#define WRN_PRINTF(f, ...) printf(f,  __VA_ARGS__ )
#define DBG_PRINTF(f, ...) printf(f,  __VA_ARGS__ )
#elif defined(__ASIX_C51__)
#define LOG_PRINTF
#define ERR_PRINTF printf
#define WRN_PRINTF printf
#define DBG_PRINTF
#elif defined(__BIONIC__)
#include <android/log.h>
#define LOG_PRINTF(f, ...) __android_log_print(ANDROID_LOG_INFO , "zgw", f , ## __VA_ARGS__ )
#define ERR_PRINTF(f, ...) __android_log_print(ANDROID_LOG_ERROR, "zgw", f , ## __VA_ARGS__ )
#define WRN_PRINTF(f, ...) __android_log_print(ANDROID_LOG_WARN , "zgw", f , ## __VA_ARGS__ )
#define DBG_PRINTF(f, ...) __android_log_print(ANDROID_LOG_DEBUG, "zgw", f , ## __VA_ARGS__ )
#elif defined(SYSLOG)
#include <syslog.h>

#define LOG_PRINTF(f, ...) syslog(LOG_INFO , f , ## __VA_ARGS__ )
#define ERR_PRINTF(f, ...) syslog(LOG_ERR,  f , ## __VA_ARGS__ )
#define WRN_PRINTF(f, ...) syslog(LOG_WARNING , f , ## __VA_ARGS__ )
#define DBG_PRINTF(f, ...) syslog(LOG_DEBUG,  f , ## __VA_ARGS__ )


#else
#include <stdio.h>
#include "sys/clock.h"
#include <time.h>
#include <sys/time.h>

char timestamp[80];
struct timeval tv;

#define FORMAT_TIME \
    gettimeofday(&tv, 0); \
    struct tm *time = localtime(&tv.tv_sec); \
    int time_len = 0; \
    time_len += strftime(timestamp, sizeof(timestamp), "%FT%T.", time); \
    time_len += snprintf(timestamp + time_len, sizeof(timestamp) - time_len, "%.6u", (unsigned int)tv.tv_usec); \
    time_len += strftime(timestamp + time_len, sizeof(timestamp) - time_len, "%z", time); \

#define TIMESTAMP_PRINT(COLOR, f, ...) { \
    FORMAT_TIME \
    printf(COLOR f  "\033[0m", timestamp,  ## __VA_ARGS__); \
}

/**
 * Information level logging.
 * \param f argument similar to the one passed to printf
 */

#define LOG_PRINTF(f, ...) TIMESTAMP_PRINT("\033[32;1m%s ", f , ## __VA_ARGS__ );

/**
 * Error level logging.
 * \param f argument similar to the one passed to printf
 */
#define ERR_PRINTF(f, ...) TIMESTAMP_PRINT("\033[31;1m%s ", f , ## __VA_ARGS__ );

/**
 * Warning level logging.
 * \param f argument similar to the one passed to printf
 */
#define WRN_PRINTF(f, ...) TIMESTAMP_PRINT("\033[33;1m%s ", f , ## __VA_ARGS__ );

/**
 * Debug level logging.
 * \param f argument similar to the one passed to printf
 */
#define DBG_PRINTF(f, ...) TIMESTAMP_PRINT("\033[34;1m%s ", f , ## __VA_ARGS__ );
#endif

/**
 * Check on the expression.
 */
#define ASSERT assert

/**
 * Return the name of TRANSMIT_COMPLETE_* status from its value
 *
 * \param state value of the state 
 */
const char *transmit_status_name(int state);
/**
 * Return the string of bytes to print
 *
 * \param cmd byte stream
 * \param len length of the byte stream 
 */
const char *print_frame(const char *cmd, unsigned int len);
/**
 * Print the 16 byte key in hex
 *
 * \param buf 16 byte key 
 */
void print_key(uint8_t *buf);
/**
 * Print the bytestrea in hex
 *
 * \param buf byte array
 * \param len length of the byte array 
 */

void print_hex(uint8_t* buf, int len);
/**
 * Return the name of endpoint state name of type rd_ep_state_t
 *
 * \param state value of the state 
 */
const char *ep_state_name(int state);
/**
 * Return the name of Resource directory node probe state name of type rd_node_state_t
 *
 * \param state value of the state 
 */
const char* rd_node_probe_state_name(int state);
/**
 * Return the name of Network Management state name of type nm_state_t 
 *
 * \param state value of the state 
 */
const char* nm_state_name(int state);
/**
 * Return the name of Network Management event name of type nm_event_t 
 *
 * \param ev value of the event 
 */
const char * nm_event_name(int ev);
const char *s2_inclusion_event_name(int state);

#endif /* SRC_ZIP_ROUTER_LOGGING_H_ */
