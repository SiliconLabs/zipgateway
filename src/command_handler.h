/* © 2017 Silicon Laboratories Inc.
 */
/*
 * command_handler.h
 *
 *  Created on: Apr 7, 2016
 *      Author: aes
 */

#ifndef SRC_COMMAND_HANDLER_H_
#define SRC_COMMAND_HANDLER_H_

#include "ZW_typedefs.h"
#include "ZW_udp_server.h"


typedef enum {
  COMMAND_HANDLED,      ///< the command was handled by the command handler. Supervision returns SUCCESS
  COMMAND_PARSE_ERROR,  ///< the command handler was unable to parse the command. Supervision returns FAIL
  COMMAND_BUSY,         ///< the command handler was unable to process to request because it is currently performing another operation. Supervision returns FAIL
  COMMAND_NOT_SUPPORTED,///< the command handler does not support this command. Supervision returns. Supervision returns NO_SUPPORT
  CLASS_NOT_SUPPORTED,  ///< there is no command handler registered for this class. This is an internal return code and should not be returned by any registered handler. Supervision returns NO_SUPPORT
  COMMAND_POSTPONED,      ///< the command has been postponed  because the receiver is a sleeping device
  COMMAND_CLASS_DISABLED,  ///< Command class has been disabled. This is an internal return code and should not be returned by any registered handler. Supervision returns NO_SUPPORT
} command_handler_codes_t;

typedef struct {
  /**
   * This is the function which will be executed when frame of the given command class is received.
   * The handler MUST return proper \ref command_handler_codes_t code.
   *
   * \param connection  a zwave_connection_t structure, this has info about the transport properties of
   * this frame.
   * \param payload  The data payload  of this  frame.
   * \param len      The length of this frame
   */
  command_handler_codes_t (*handler)(zwave_connection_t *, uint8_t* frame, uint16_t length); /// the command handler self

  /**
   * Initializer, this function initializes the command handler, ie. resetting state machines etc.
   */
  void (*init)(void);  /// Initialize the command handler
  uint16_t cmdClass;   /// command class that this handler implements
  uint8_t  version;    /// version of the implemented command class
  uint8_t  padding[3];    /// padding for having correct alignment on both 32 and 64bit platform
  security_scheme_t  minimal_scheme; ///the minimal security level which this command is supported on.
} command_handler_t;

#ifdef __APPLE__
#define REGISTER_HANDLER(handler_func, init_func, cmd_class, cmd_class_version, scheme) \
  static const command_handler_t  __handler##cmd_class##__ \
    __attribute__(( __section__("__TEXT,__handlers") )) \
    __attribute__(( __used__ )) \
    = { .handler  = handler_func, \
        .init     = init_func, \
        .cmdClass = cmd_class, \
        .version  = cmd_class_version, \
        .minimal_scheme = scheme};
#else
#define REGISTER_HANDLER(handler_func, init_func, cmd_class, cmd_class_version, scheme) \
  static const command_handler_t __handler##cmd_class##__ \
    __attribute__(( __section__("_handlers") )) \
    __attribute__(( __used__ )) \
    = { .handler  = handler_func, \
        .init     = init_func, \
        .cmdClass = cmd_class, \
        .version  = cmd_class_version, \
        .minimal_scheme = scheme};
#endif

/**
 * Find a command handler in the database for the frame pointed to by \a payload.
 *
 * If no command handler is found \ref CLASS_NOT_SUPPORTED is
 * returned. Otherwise the return code of the handler is returned.
 *
 * \param connection  a zwave_connection_t structure, this has info about the transport properties of
 * this frame.
 * \param payload  The data payload  of this  frame.
 * \param len      The length of this frame
 * \param bSupervisionUnwrapped bSupervisionUnwrapped should be true if this frame has been supervision
 * unwrapped, namely the left bytes next to the payload we are passing is supervision.
 */
command_handler_codes_t ZW_command_handler_run(zwave_connection_t *connection, uint8_t* payload, uint16_t len, uint8_t bSupervisionUnwrapped);

/**
 * Get the version of a command handler.
 * \param scheme  Security scheme of the handler.
 * \param cmdClass  Command class to query for.
 * \return Command handler version, if no handler is found, this function returns 0
 */
uint8_t ZW_comamnd_handler_version_get( security_scheme_t scheme, uint16_t cmdClass);


/**
 * Build the command class list for the NIF based on the registered command handlers
 *
 * \param scheme Security level to build the NIF for (secure NIF, non-secure NIF)
 * \param nif Where to store the command class list
 * \param max_len Maximum number of bytes to output. Output will be silently truncated if max_len is exceeded.
 */
uint8_t ZW_command_handler_get_nif( security_scheme_t scheme, uint8_t* nif,uint8_t max_len);

/**
 * Initialize the command handlers
 */
void ZW_command_handler_init();


/**
 * Set a list of commands which are disabled
 */
void ZW_command_handler_disable_list(uint16_t *cmdList,uint8_t cmdListLen);


#endif /* SRC_COMMAND_HANDLER_H_ */
