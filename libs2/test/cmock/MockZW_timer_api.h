/* © 2017 Silicon Laboratories Inc.
 */
/* AUTOGENERATED FILE. DO NOT EDIT. */
#ifndef _MOCKZW_TIMER_API_H
#define _MOCKZW_TIMER_API_H

#include "ZW_timer_api.h"

/* Ignore the following warnings, since we are copying code */
#if defined(__GNUC__) && !defined(__ICC)
#if !defined(__clang__)
#pragma GCC diagnostic ignored "-Wpragmas"
#endif
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wduplicate-decl-specifier"
#endif

void MockZW_timer_api_Init(void);
void MockZW_timer_api_Destroy(void);
void MockZW_timer_api_Verify(void);




#define TimerStart_IgnoreAndReturn(cmock_retval) TimerStart_CMockIgnoreAndReturn(__LINE__, cmock_retval)
void TimerStart_CMockIgnoreAndReturn(UNITY_LINE_TYPE cmock_line, uint8_t cmock_to_return);
#define TimerStart_ExpectAndReturn(cb, timerTicks, repeats, cmock_retval) TimerStart_CMockExpectAndReturn(__LINE__, cb, timerTicks, repeats, cmock_retval)
void TimerStart_CMockExpectAndReturn(UNITY_LINE_TYPE cmock_line, func cb, uint8_t timerTicks, uint8_t repeats, uint8_t cmock_to_return);
typedef uint8_t (* CMOCK_TimerStart_CALLBACK)(func cb, uint8_t timerTicks, uint8_t repeats, int cmock_num_calls);
void TimerStart_StubWithCallback(CMOCK_TimerStart_CALLBACK Callback);
#define TimerStart_IgnoreArg_cb() TimerStart_CMockIgnoreArg_cb(__LINE__)
void TimerStart_CMockIgnoreArg_cb(UNITY_LINE_TYPE cmock_line);
#define TimerStart_IgnoreArg_timerTicks() TimerStart_CMockIgnoreArg_timerTicks(__LINE__)
void TimerStart_CMockIgnoreArg_timerTicks(UNITY_LINE_TYPE cmock_line);
#define TimerStart_IgnoreArg_repeats() TimerStart_CMockIgnoreArg_repeats(__LINE__)
void TimerStart_CMockIgnoreArg_repeats(UNITY_LINE_TYPE cmock_line);
#define TimerCancel_IgnoreAndReturn(cmock_retval) TimerCancel_CMockIgnoreAndReturn(__LINE__, cmock_retval)
void TimerCancel_CMockIgnoreAndReturn(UNITY_LINE_TYPE cmock_line, uint8_t cmock_to_return);
#define TimerCancel_ExpectAndReturn(timerHandle, cmock_retval) TimerCancel_CMockExpectAndReturn(__LINE__, timerHandle, cmock_retval)
void TimerCancel_CMockExpectAndReturn(UNITY_LINE_TYPE cmock_line, uint8_t timerHandle, uint8_t cmock_to_return);
typedef uint8_t (* CMOCK_TimerCancel_CALLBACK)(uint8_t timerHandle, int cmock_num_calls);
void TimerCancel_StubWithCallback(CMOCK_TimerCancel_CALLBACK Callback);
#define TimerCancel_IgnoreArg_timerHandle() TimerCancel_CMockIgnoreArg_timerHandle(__LINE__)
void TimerCancel_CMockIgnoreArg_timerHandle(UNITY_LINE_TYPE cmock_line);
#define getTickTime_IgnoreAndReturn(cmock_retval) getTickTime_CMockIgnoreAndReturn(__LINE__, cmock_retval)
void getTickTime_CMockIgnoreAndReturn(UNITY_LINE_TYPE cmock_line, uint16_t cmock_to_return);
#define getTickTime_ExpectAndReturn(cmock_retval) getTickTime_CMockExpectAndReturn(__LINE__, cmock_retval)
void getTickTime_CMockExpectAndReturn(UNITY_LINE_TYPE cmock_line, uint16_t cmock_to_return);
typedef uint16_t (* CMOCK_getTickTime_CALLBACK)(int cmock_num_calls);
void getTickTime_StubWithCallback(CMOCK_getTickTime_CALLBACK Callback);
#define getTickTimePassed_IgnoreAndReturn(cmock_retval) getTickTimePassed_CMockIgnoreAndReturn(__LINE__, cmock_retval)
void getTickTimePassed_CMockIgnoreAndReturn(UNITY_LINE_TYPE cmock_line, uint16_t cmock_to_return);
#define getTickTimePassed_ExpectAndReturn(wStartTickTime, cmock_retval) getTickTimePassed_CMockExpectAndReturn(__LINE__, wStartTickTime, cmock_retval)
void getTickTimePassed_CMockExpectAndReturn(UNITY_LINE_TYPE cmock_line, uint16_t wStartTickTime, uint16_t cmock_to_return);
typedef uint16_t (* CMOCK_getTickTimePassed_CALLBACK)(uint16_t wStartTickTime, int cmock_num_calls);
void getTickTimePassed_StubWithCallback(CMOCK_getTickTimePassed_CALLBACK Callback);
#define getTickTimePassed_IgnoreArg_wStartTickTime() getTickTimePassed_CMockIgnoreArg_wStartTickTime(__LINE__)
void getTickTimePassed_CMockIgnoreArg_wStartTickTime(UNITY_LINE_TYPE cmock_line);

#endif
