
#include<stdint.h>
typedef enum {UNKNOWN_CLASS,UNKNOWN_COMMAND,UNKNOWN_PARAMETER,PARSE_FAIL,PARSE_OK } validator_result_t;

validator_result_t ZW_command_validator(uint8_t* cmd, int cmdLen,int* version);


