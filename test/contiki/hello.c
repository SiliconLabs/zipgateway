#include <stdbool.h>
#include "zgw_log.h"

zgw_log_id_define(parrot);
zgw_log_id_default_set(parrot);


bool hello(void) {
   zgw_log(0, "Hello world\n");
   return true;
}
