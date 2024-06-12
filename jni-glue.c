/****************************  jni-glue.c  ****************************
 *           #######
 *           ##  ##
 *           #  ##    ####   #####    #####  ##  ##   #####
 *             ##    ##  ##  ##  ##  ##      ##  ##  ##
 *            ##  #  ######  ##  ##   ####   ##  ##   ####
 *           ##  ##  ##      ##  ##      ##   #####      ##
 *          #######   ####   ##  ##  #####       ##  #####
 *                                           #####
 *          Z-Wave, the wireless language.
 *
 *              Copyright (c) 2013
 *              Zensys A/S
 *              Denmark
 *
 *              All Rights Reserved
 *
 *    This source file is subject to the terms and conditions of the
 *    Zensys Software License Agreement which restricts the manner
 *    in which it may be used.
 *
 *---------------------------------------------------------------------------
 *
 * Description: ...
 *
 * Author:   aes
 *
 * Last Changed By:  $Author: $
 * Revision:         $Revision: $
 * Last Changed:     $Date: $
 *
 ****************************************************************************/
#include <jni.h>

#include "contiki.h"
#include "contiki-net.h"
#include "ZIP_Router.h"
#include <stdlib.h>
#include "port.h"
#include "eeprom.h"
#include "uip-debug.h"
#include "serial_api_process.h"
PROCINIT(&etimer_process);

const char* linux_conf_eeprom_file;
const char* linux_conf_provisioning_cfg_file;
const char* linux_conf_provisioning_list_storage_file;


u8_t do_send(uip_lladdr_t* a);

// Globals for Java side
static jmethodID mSend;
static jmethodID mInitDone;

static jmethodID mSerialInit;
static jmethodID mSerialClose;
static jmethodID mSerialPutBuffer;
static jmethodID misSerialBufferEmpty;
static jmethodID mgetSerialBuffer;


static JNIEnv* jenv;
static jobject* jobj_native;

static jobject* jobj_serial;

// Globals for zipgateway
uint8_t gisZIPRReady = 0;
uint8_t gisTnlPacket = FALSE;
uint8_t gGwLockEnable = TRUE;
void TcpTunnel_ReStart(void) {}

int getIntField(JNIEnv *env, jclass clazz, jobject obj, const char *field) {
  jfieldID fieldID = (*env)->GetFieldID(env, clazz, field, "I");

  return (int) (*env)->GetIntField(env, obj, fieldID);
}

char* getStringField(JNIEnv *env, jclass clazz, jobject obj, const char *field)
{
  char *dst =0;
  jfieldID fieldID = (*env)->GetFieldID(env, clazz, field, "Ljava/lang/String;");
  jstring str = (jstring) (*env)->GetObjectField(env, obj, fieldID);

  if (str != NULL) {
    jsize len =(*env)->GetStringLength(env, str);
    dst = malloc(len+1);
    (*env)->GetStringUTFRegion(env, str, 0, len, dst);

    dst[len] = 0;
  }
  return dst;
}

char* getAddrStrField(JNIEnv *env, jclass clazz, jobject obj, const char *field, uip_ip6addr_t* addr)
{
  static char dst[128];
  jfieldID fieldID = (*env)->GetFieldID(env, clazz, field, "Ljava/lang/String;");
  jstring str = (jstring) (*env)->GetObjectField(env, obj, fieldID);

  if (str != NULL) {
    jsize len =(*env)->GetStringLength(env, str);
    (*env)->GetStringUTFRegion(env, str, 0, len, dst);
    dst[len + 1] = 0;
    uiplib_ipaddrconv(dst, addr);
  }
  return dst;
}

JNIEXPORT void JNICALL
Java_com_sigmadesigns_zipgwdroid_ZIPGatewayNative_destroy(JNIEnv * env, jobject obj)
{
  (*env)->DeleteGlobalRef(jenv, jobj_serial);
  (*env)->DeleteGlobalRef(jenv, jobj_native);

  SerialClose();
}


JNIEXPORT void JNICALL
Java_com_sigmadesigns_zipgwdroid_ZIPGatewayNative_tunnelready(JNIEnv * env, jobject  native, jobject config )
{
  jenv = env;
  jclass config_cls = (*jenv)->FindClass(jenv, "com/sigmadesigns/zipgwdroid/network/ZIPGatewayConfig" );

  getAddrStrField(jenv, config_cls, config, "cfg_pan_prefix" , &(cfg.cfg_pan_prefix));
  getAddrStrField(jenv, config_cls, config, "cfg_lan_addr" , &cfg.cfg_lan_addr);
  getAddrStrField(jenv, config_cls, config, "gw_addr" , &cfg.gw_addr);
  getAddrStrField(jenv, config_cls, config, "unsolicited_dest" , &(cfg.unsolicited_dest));

  getAddrStrField(jenv, config_cls, config, "tun_prefix" , &(cfg.tun_prefix));

  cfg.cfg_lan_prefix_length = getIntField(jenv, config_cls, config, "cfg_lan_prefix_length");
  cfg.tun_prefix_length = getIntField(jenv, config_cls, config, "tun_prefix_length");

  process_post(&zip_process, ZIP_EVENT_TUNNEL_READY, 0);
}


JNIEXPORT void JNICALL
Java_com_sigmadesigns_zipgwdroid_ZIPGatewayNative_init(JNIEnv * env, jobject  native, jobject config, jobject serial )
{

  jenv = env;
  jobj_native = native;

  /*
   * Make sure the garbage collector does not collect serial
   */
  jobj_serial = (*env)->NewGlobalRef(jenv, serial);
  jobj_native = (*env)->NewGlobalRef(jenv, native);

  /*Get at handle to the doSend java function*/
  jclass cls = (*jenv)->GetObjectClass(jenv, jobj_native);
  mSend = (*jenv)->GetMethodID(jenv, cls, "OnSend", "([B)V");
  mInitDone= (*jenv)->GetMethodID(jenv, cls, "OnConfigDone", "(Ljava/lang/String;Ljava/lang/String;)V");

  jclass cls_serial = (*jenv)->GetObjectClass(jenv, jobj_serial);

  mSerialInit = (*jenv)->GetMethodID(jenv, cls_serial, "SerialInit", "(Ljava/lang/String;)I");
  mSerialClose = (*jenv)->GetMethodID(jenv, cls_serial, "SerialClose", "()V");
  mSerialPutBuffer = (*jenv)->GetMethodID(jenv, cls_serial, "SerialPutBuffer", "([B)V");
  misSerialBufferEmpty = (*jenv)->GetMethodID(jenv, cls_serial, "isSerialBufferEmpty", "()Z");
  mgetSerialBuffer = (*jenv)->GetMethodID(jenv, cls_serial, "getSerialBuffer", "([BI)V");


  jclass config_cls = (*jenv)->FindClass(jenv, "com/sigmadesigns/zipgwdroid/network/ZIPGatewayConfig" );

  strcpy(cfg.portal_url,getStringField(jenv, config_cls,config, "portal_url"));
  cfg.serial_port = getStringField(jenv, config_cls,config, "serial_port");
  linux_conf_eeprom_file = getStringField(jenv, config_cls,config, "eeprom_file");
  linux_conf_provisioning_cfg_file = getStringField(jenv, config_cls,config, "provisioning_cfg_file");
  linux_conf_provisioning_list_storage_file = getStringField(jenv, config_cls,config, "provisioning_list_storage_file");


  getAddrStrField(jenv, config_cls, config, "cfg_pan_prefix" , &(cfg.cfg_pan_prefix));
  getAddrStrField(jenv, config_cls, config, "cfg_lan_addr" , &cfg.cfg_lan_addr);
  getAddrStrField(jenv, config_cls, config, "gw_addr" , &cfg.gw_addr);
  getAddrStrField(jenv, config_cls, config, "unsolicited_dest" , &(cfg.unsolicited_dest));

  getAddrStrField(jenv, config_cls, config, "tun_prefix" , &(cfg.tun_prefix));


  cfg.cfg_lan_prefix_length = getIntField(jenv, config_cls, config, "cfg_lan_prefix_length");
  cfg.tun_prefix_length = getIntField(jenv, config_cls, config, "tun_prefix_length");

  cfg.client_key_size = getIntField(jenv, config_cls, config, "client_key_size");
  cfg.manufacturer_id = getIntField(jenv, config_cls, config, "manufacturer_id");
  cfg.product_id = getIntField(jenv, config_cls, config, "product_id");
  cfg.product_type = getIntField(jenv, config_cls, config, "product_type");
  cfg.portal_portno = getIntField(jenv, config_cls, config, "portal_portno");;
  cfg.unsolicited_port = getIntField(jenv, config_cls, config, "unsolicited_port");
  cfg.ipv4disable = 1;

  jfieldID fieldID = (*env)->GetFieldID(env, config_cls, "extra_classes", "[B");
  jobject array = (*env)->GetObjectField(env, config, fieldID);

  jsize len = (*env)->GetArrayLength(env, array);
  (*env)->GetByteArrayRegion(env, array, 0, len, cfg.extra_classes);

  printf("Starting Contiki\n");
  set_landev_outputfunc(do_send);

  eeprom_init();
  process_init();
  ctimer_init();
  procinit_init();
  autostart_start(autostart_processes);

}


JNIEXPORT jint JNICALL
Java_com_sigmadesigns_zipgwdroid_ZIPGatewayNative_poll(JNIEnv * env, jobject  obj)
{

  while(process_run()) {

  }

  process_poll(&serial_api_process);

  etimer_request_poll();

  return etimer_next_expiration_time();
}

JNIEXPORT void JNICALL
Java_com_sigmadesigns_zipgwdroid_ZIPGatewayNative_recv(JNIEnv *env, jobject obj, jbyteArray array, jint len){
  jbyte* bufferPtr = (*env)->GetByteArrayElements(env, array, NULL);

  uip_len = len;
  memcpy(&uip_buf[UIP_LLH_LEN], bufferPtr, uip_len);
  tcpip_input();

  (*env)->ReleaseByteArrayElements(env, array, bufferPtr, 0);
}

u8_t do_send(uip_lladdr_t* a) {
  int size = uip_len;


  jbyteArray result=(*jenv)->NewByteArray(jenv, size);
  (*jenv)->SetByteArrayRegion(jenv, result, 0, size, &uip_buf[UIP_LLH_LEN]);
  (*jenv)->CallVoidMethod(jenv, jobj_native, mSend, result);

  uip_len = 0;
  return 0;
}


void system_net_hook(int init) {
  char buf1[128];
  char buf2[128];
  uip_ipaddr_sprint(buf1, &cfg.pan_prefix);
  uip_ipaddr_sprint(buf2, &cfg.lan_addr);

  jstring string1 = (*jenv)->NewStringUTF(jenv, buf1);
  jstring string2 = (*jenv)->NewStringUTF(jenv, buf2);
  (*jenv)->CallVoidMethod(jenv, jobj_native, mInitDone, string1, string2);
}


const char* linux_conf_tun_script="/dev/null";
const char* linux_conf_fin_script="/dev/null";

void config_update(const char* key, const char* value) {
//TODO
  // __android_log_print(0,"Implement me " __FUNCTION__);
}


/*************** Serial handling **************/




/**
 * Initialize the serial port to 115200 BAUD 8N1
 * */
int SerialInit(const char* serial_port) {
  jstring string1 = (*jenv)->NewStringUTF(jenv, serial_port);
  return (*jenv)->CallIntMethod(jenv, jobj_serial, mSerialInit, string1);
}


/**
 * Check if there is new serial data available. On some system this call in unrelated to \ref SerialGetByte
 */
int SerialCheck() {
  return (*jenv)->CallBooleanMethod(jenv, jobj_serial, misSerialBufferEmpty) == FALSE ? 1 : 0;
}

/**
 * Flush the serial output if using buffered output.
 */
void SerialFlush() {

}

/**
 * De-Initialize the seial port.
 */
void SerialClose() {
  (*jenv)->CallVoidMethod(jenv, jobj_serial, mSerialClose);
}


int SerialGetBuffer(unsigned char* c, int len) {
  jbyteArray result = (*jenv)->NewByteArray(jenv, len);
  (*jenv)->CallVoidMethod(jenv, jobj_serial, mgetSerialBuffer, result, len);
  (*jenv)->GetByteArrayRegion(jenv, result, 0, len, c);
  return len;
}



void SerialPutBuffer(unsigned char*c, int size) {
  jbyteArray result = (*jenv)->NewByteArray(jenv, size);
  (*jenv)->SetByteArrayRegion(jenv, result, 0, size, c);
  (*jenv)->CallVoidMethod(jenv, jobj_serial, mSerialPutBuffer, result);

}

int SerialGetByte() {
  unsigned char c;
  if (SerialGetBuffer(&c,1)) {
    return c;
  } else {
    return -1;
  }
}

void SerialPutByte(unsigned char c) {
  SerialPutBuffer(&c, 1);
}
