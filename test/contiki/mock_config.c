/* © 2020 Silicon Laboratories Inc. */
#include <stdint.h>
#include <string.h>
#include "mock_config.h"

mock_zgw_cfg_t cfg_values[] = {
   {"ZipPanIp6", "fd00:bbbb::1"    },
   {"ZipLanIp6", "fd00:aaaa::3"    },
   {"ZipLanGw6", "fd00:aaaa::1234" },
   {"ZipPSK", "123456789012345678901234567890AA" },
   {"ZipNodeIdentifyScript", ""},
};

const char* config_get_val(const char* key, const char* def) {
   uint8_t ii = 0;
   mock_zgw_cfg_t* p;
   for (ii=0; ii<(sizeof(cfg_values)/sizeof(struct mock_zgw_cfg_pair)); ii++) {
      if (strcmp(key, cfg_values[ii].key) == 0)
         return cfg_values[ii].val;
   }
   return def;
}
