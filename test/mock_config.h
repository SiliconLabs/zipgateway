/* © 2020 Silicon Laboratories Inc. */

#ifndef MOCK_CONFIG_H_
#define MOCK_CONFIG_H_

/* External interface of parse_config.
 *
 * Default version is in mock_config.c.
 *
 * The interface can also be replaced in a test case, to provide more
 * fine-grained control over the configuration used in the test.
 *
 */

struct mock_zgw_cfg_pair {
   char* key;
   char* val;
};

typedef struct mock_zgw_cfg_pair mock_zgw_cfg_t;

/**
 * Struct that mocks the data in the config file.
 *
 * A unit test can provide its own implementation of this to run with
 * a different zipgateway configuration.
 *
 */
extern mock_zgw_cfg_t cfg_values[];


/** Function to get the configured values.
 *
 * Looks up in cfg_values.
 *
 * A unit test can provide its own implementation of this function if
 * it wants to override its own default settings in some scenarios.
 *
 * \param key The key of the configuration parameter.
 * \param default_setting The default string if \p key is not set in the file.
 * \return Pointer to a string.  Could be \p def.
 */
const char* config_get_val(const char* key, const char* default_setting);
#endif
