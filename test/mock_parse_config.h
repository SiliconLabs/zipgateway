/* © 2020 Silicon Laboratories Inc. */

#ifndef MOCK_PARSE_CONFIG_H
#define MOCK_PARSE_CONFIG_H
/**
 * Mock of parse_config.c
 *
 * \ingroup CTF
 *
 * Most of the configuration parameters are found using \ref
 * config_get_val().  The default version of config_get_val looks up
 * in \ref cfg_values.
 *
 * A test case can provide its own configuration by changing the
 * settings in the cfg_values array or, depending on the case, by
 * replacing the entire mock_parse_config.
 *
 */
void mock_ConfigInit(void);
#endif
