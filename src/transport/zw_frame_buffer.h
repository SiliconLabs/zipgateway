/* © 2019 Silicon Laboratories Inc. */
#ifndef ZW_FRAME_BUFFER_H
#define ZW_FRAME_BUFFER_H
#include<stdint.h>
#include<ZW_SendDataAppl.h>
#include<ZW_udp_server.h>
/**
 * This module contains a structure for allocating Z-Wave frames buffers.
 * A Z-Wave frame buffer is defined as a Z-Wave application frame, and
 * a Z-Wave
 *
 */

#define FRAME_BUFFER_ELEMENT_SIE 256

typedef struct {
  ts_param_t param;
  uint8_t frame_data[FRAME_BUFFER_ELEMENT_SIE];
  uint16_t frame_len;
} zw_frame_buffer_element_t;

typedef struct {
  zwave_connection_t conn;
  uint8_t frame_data[FRAME_BUFFER_ELEMENT_SIE];
  uint16_t frame_len;
} zw_frame_ip_buffer_element_t;


/**
 * Allocate a new frame buffer, the buffer must be freed with
 * \ref zw_frame_buffer_free
 */
zw_frame_buffer_element_t* zw_frame_buffer_alloc();

/**
 * Allocate and initialize a new frame buffer, the buffer must be freed with
 * \ref zw_frame_buffer_free.
 */
zw_frame_buffer_element_t* zw_frame_buffer_create(const ts_param_t *p,const uint8_t* cmd, uint16_t length);

/**
 * Free a framebuffer
 */
void zw_frame_buffer_free(zw_frame_buffer_element_t* e);


/**
 * Allocate a new frame IP buffer, the buffer must be freed with
 * \ref zw_frame_buffer_free
 */
zw_frame_ip_buffer_element_t* zw_frame_ip_buffer_alloc();

/**
 * Allocate and initialize a new  IP frame buffer, the buffer must be freed with
 * \ref zw_frame_buffer_free.
 */
zw_frame_ip_buffer_element_t* zw_frame_ip_buffer_create(const zwave_connection_t *p,const uint8_t* cmd, uint16_t length);

/**
 * Free a frame IP buffer
 */
void zw_frame_ip_buffer_free(zw_frame_ip_buffer_element_t* e);



#endif
