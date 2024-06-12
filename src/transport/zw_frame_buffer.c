/* © 2019 Silicon Laboratories Inc. */

#include "zw_frame_buffer.h"

#include<stdlib.h>
#include<string.h>
#include"assert.h"

zw_frame_buffer_element_t* zw_frame_buffer_alloc() {
  return malloc(sizeof(zw_frame_buffer_element_t));
}

zw_frame_buffer_element_t* zw_frame_buffer_create(const ts_param_t *p,const uint8_t* cmd, uint16_t length) {
  zw_frame_buffer_element_t* f=zw_frame_buffer_alloc();

  if(f && (length < sizeof(f->frame_data))) {
    f->param = *p;
    f->frame_len = length;
    memcpy(f->frame_data, cmd, length);
    return f;
  } else {
    if(f) zw_frame_buffer_free(f);
    assert(0);
    return NULL;
  }
}

void zw_frame_buffer_free(zw_frame_buffer_element_t* e) {
  free(e);
}



zw_frame_ip_buffer_element_t* zw_frame_ip_buffer_alloc() {
  return malloc(sizeof(zw_frame_ip_buffer_element_t));
}

zw_frame_ip_buffer_element_t* zw_frame_ip_buffer_create(const zwave_connection_t *p,const uint8_t* cmd, uint16_t length) {
  zw_frame_ip_buffer_element_t* f=zw_frame_ip_buffer_alloc();

  if(f && (length < sizeof(f->frame_data))) {
    f->conn = *p;
    f->frame_len = length;
    memcpy(f->frame_data, cmd, length);
    return f;
  } else {
    if(f) zw_frame_ip_buffer_free(f);
    assert(0);
    return NULL;
  }
}

void zw_frame_ip_buffer_free(zw_frame_ip_buffer_element_t* e) {
  free(e);
}