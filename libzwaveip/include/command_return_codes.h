/*
 * Copyright 2020 Silicon Laboratories Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file
 * This file describes the return codes that commands passed to the libzwaveip can return.
 */

#ifndef _COMMAND_RETURN_CODE_H_
#define _COMMAND_RETURN_CODE_H_

/**
 * @brief Commands status codes indicating the commands execution outcome
 */
typedef enum
{
   E_CMD_RETURN_CODE_SUCCESS        = 0x00,   /**< The command was successfully executed */
   E_CMD_RETURN_CODE_PARSE_ERROR    = 0x01,   /**< The command was not parsed correctly. One or more parameters were incorrect */
   E_CMD_RETURN_CODE_TRANSMIT_FAIL  = 0x02,   /**< The command execution failed to initiate a transmition to the zconnection */
   E_CMD_RETURN_CODE_FAIL           = 0x03    /**< The command execution failed in some general way */
} e_cmd_return_code_t; 

#endif /* _COMMAND_RETURN_CODE_H_ */
