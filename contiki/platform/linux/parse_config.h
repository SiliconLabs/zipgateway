/*
 * parse_config.h
 *
 *  Created on: Jan 14, 2011
 *      Author: aes
 */

#ifndef PARSE_CONFIG_H_
#define PARSE_CONFIG_H_
#include <pkgconfig.h>

#define DATA_DIR INSTALL_LOCALSTATEDIR "/lib/" PACKAGE_TARNAME "/"

/**
 * Platform dependent routine which fills the \ref router_config structure
 * */
void ConfigInit();


void config_update(const char* key, const char* value);
char *get_cfg_filename();
#endif /* PARSE_CONFIG_H_ */
