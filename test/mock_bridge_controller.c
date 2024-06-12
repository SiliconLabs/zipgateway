/* (c) 2020 Silicon Laboratories Inc. */

#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

#include <lib/zgw_log.h>
#include "mock_conhandle.h"
#include "process.h"

#include "ZW_SerialAPI.h"
#include "ZW_typedefs.h"
#include "ZW_classcmd.h"

#include "router_events.h"
#include "serial_api_process.h"

#include "test_helpers.h"

zgw_log_id_declare(mockCon);
zgw_log_id_default_set(mockCon);

/* protocol/bridge controller stuff */

/**
 * Struct for the data-base holding the "standardized" mock responses
 * on the serial interface to the mock bridge controller.
 */
typedef struct ser_if_reply_entry {
   uint8_t func_id;
   char* func_name;
   uint8_t *frame;
} ser_if_reply_entry_t;

//FUNC_ID_ZW_ADD_NODE_TO_NETWORK, ADD_NODE_STOP // 4A, 05, 00
//REMOVE_NODE_STOP
//ZW_SET_LEARN_MODE_DISABLE

// FUNC_ID_SERIAL_API_GET_CAPABILITIES 0x07
uint8_t ser_cap[] = {0x2b, RESPONSE, FUNC_ID_SERIAL_API_GET_CAPABILITIES, // len, RESPONSE, cmd
                          0x05, 0x0e, // capabilities.appl_version,capabilities.appl_revision
                          0x30, 0x40, //manufacturer
                          0x10, 0x03, // product_type
                          0x07, 0x08, // product_id
                          0xf6, 0x87, 0x77, 0x88, 0xcf,
                          0x7f, 0xc0, 0x4f, 0xfb, 0xdf, 0xfd, 0xe0, 0x67,
                          0x00, 0x80, 0x80, 0x00, 0x80, 0x86, 0x80, 0xba,
                          0x05, 0xe8, 0x73, 0x00, 0x80, 0x0f, 0x00,
                          0x00,
                          0x60, 0x00, 0x00, 0x70 };
ser_if_reply_entry_t ser_cap_reply = {FUNC_ID_SERIAL_API_GET_CAPABILITIES,
                               "FUNC_ID_SERIAL_API_GET_CAPABILITIES", ser_cap};

//FUNC_ID_SERIALAPI_SETUP                         0x0B
uint8_t ser_api_setup[] = {0x05, RESPONSE, FUNC_ID_SERIALAPI_SETUP,
                           0x01, 0x0e, 0xff};
ser_if_reply_entry_t ser_api_setup_reply = {FUNC_ID_SERIALAPI_SETUP,
                                     "FUNC_ID_SERIALAPI_SETUP", ser_api_setup};

//FUNC_ID_ZW_GET_VERSION                          0x15
/* Get the Z-Wave library basis version.  */
uint8_t version_data[] = {0x10, RESPONSE, 0x15,
                          0x5a, 0x2d, 0x57, 0x61,
                          0x76, 0x65, 0x20, 0x36,
                          0x2e, 0x30, 0x31, 0x00, 0x07, 0x97};
ser_if_reply_entry_t version_data_reply = {FUNC_ID_ZW_GET_VERSION,
                                    "FUNC_ID_ZW_GET_VERSION", version_data};
uint8_t protocol_version_data[] = {0x19, RESPONSE, FUNC_ID_ZW_GET_PROTOCOL_VERSION,
                          0x5a, 0x2d, 0x57, 0x61,
                          0x76, 0x65, 0x20, 0x36,
                          0x76, 0x65, 0x20, 0x36,
                          0x76, 0x65, 0x20, 0x36, 0x0a,
                          0x2e, 0x30, 0x31, 0x00, 0x07, 0x97};
ser_if_reply_entry_t protocol_version_data_reply = {FUNC_ID_ZW_GET_PROTOCOL_VERSION,
                                    "FUNC_ID_ZW_GET_PROTOCOL_VERSION", protocol_version_data};


// CONTROLLER_NODEID_SERVER_PRESENT        0x04
// CONTROLLER_IS_SUC                       0x10
//FUNC_ID_SERIAL_API_GET_INIT_DATA 0x02
uint8_t init_data[] = {0x25, RESPONSE, FUNC_ID_SERIAL_API_GET_INIT_DATA,
                       0x08, 0x08, 0x1d,
                       0x1f, 0x00, 0x00, 0x00, 0x00,
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                       0x05, 0x00, 0x0b};
ser_if_reply_entry_t init_data_reply = {FUNC_ID_SERIAL_API_GET_INIT_DATA,
                                 "FUNC_ID_SERIAL_API_GET_INIT_DATA", init_data};
//FUNC_ID_MEMORY_GET_ID
uint8_t mem_id[] = {0x08, RESPONSE, 0x20,
                    0xdd, 0xac, 0x41, 0x08, 0x01, 0xef};
ser_if_reply_entry_t mem_id_reply = {FUNC_ID_MEMORY_GET_ID,
                              "FUNC_ID_MEMORY_GET_ID", mem_id};

//FUNC_ID_ZW_GET_RANDOM 0x1c
uint8_t random_word_not[] = {0x0d, RESPONSE, 0x1c,
                             0x01, 0x08, 0xb6, 0x4f, 0xaf, 0x58, 0xd2, 0xaf, 0x30, 0x90, 0x35};
ser_if_reply_entry_t random_word_not_reply = {FUNC_ID_ZW_GET_RANDOM,
                                       "FUNC_ID_ZW_GET_RANDOM", random_word_not};
//FUNC_ID_ZW_SET_SUC_NODE_ID
uint8_t set_suc[] = {0x03, RESPONSE, 0x54, 0x01, 0x00};
ser_if_reply_entry_t set_suc_reply = {FUNC_ID_ZW_SET_SUC_NODE_ID,
                                     "FUNC_ID_ZW_SET_SUC_NODE_ID", set_suc};
//FUNC_ID_ZW_TYPE_LIBRARY 0xbd
uint8_t lib_type[] = {0x04, RESPONSE, FUNC_ID_ZW_TYPE_LIBRARY, 0x07, 0x40};
ser_if_reply_entry_t lib_type_reply = {FUNC_ID_ZW_TYPE_LIBRARY,
                                      "FUNC_ID_ZW_TYPE_LIBRARY", lib_type};

//FUNC_ID_ZW_GET_SUC_NODE_ID 0x56
uint8_t get_suc[] = {0x04, RESPONSE, FUNC_ID_ZW_GET_SUC_NODE_ID, 0x01, 0xad};
ser_if_reply_entry_t get_suc_reply = {FUNC_ID_ZW_GET_SUC_NODE_ID,
                               "FUNC_ID_ZW_GET_SUC_NODE_ID", get_suc};

//FUNC_ID_ZW_GET_NODE_PROTOCOL_INFO 0x41
uint8_t NIF_resp[] = {0x09, RESPONSE, FUNC_ID_ZW_GET_NODE_PROTOCOL_INFO,
                      0x41, 0xd3, 0x96, 0x01, 0x02, 0x02, 0x07, 0xf5};

//FUNC_ID_ZW_GET_CONTROLLER_CAPABILITIES 0x05
/* Defines for ZW_GetControllerCapabilities */
/* #define CONTROLLER_IS_SECONDARY                 0x01 */
/* #define CONTROLLER_ON_OTHER_NETWORK             0x02 */
/* #define CONTROLLER_NODEID_SERVER_PRESENT        0x04 */
/* #define CONTROLLER_IS_REAL_PRIMARY              0x08 */
/* #define CONTROLLER_IS_SUC                       0x10 */
/* #define NO_NODES_INCUDED                        0x20 */
uint8_t ctrl_cap[] = {0x04, RESPONSE, FUNC_ID_ZW_GET_CONTROLLER_CAPABILITIES,
                           0x1c, 0xe3};
ser_if_reply_entry_t ctrl_cap_reply = {FUNC_ID_ZW_GET_CONTROLLER_CAPABILITIES,
                                       "FUNC_ID_ZW_GET_CONTROLLER_CAPABILITIES", ctrl_cap};

//FUNC_ID_MEMORY_GET_BUFFER 0x23
//uint8_t mem_get_stg[] = {0x01, RESPONSE, FUNC_ID_MEMORY_GET_BUFFER, 0x00};

// FUNC_ID_ZW_GET_NODE_PROTOCOL_INFO               0x41
uint8_t proto_info1[] = {0x09, RESPONSE, FUNC_ID_ZW_GET_NODE_PROTOCOL_INFO,
                        0xd3, 0x96, 0x01, 0x02, 0x02, 0x07, 0xf5};

// virtual node
uint8_t proto_info2[] = {0x09, RESPONSE, FUNC_ID_ZW_GET_NODE_PROTOCOL_INFO,
                         0xd3, 0x96, 0x01, // cap, sec, reserved
                         BASIC_TYPE_ROUTING_SLAVE, GENERIC_TYPE_REPEATER_SLAVE, SPECIFIC_TYPE_VIRTUAL_NODE, //basic, generic, specific
                         0xf5};

/* TO SET UP virtual nodes: */
// FUNC_ID_SERIAL_API_APPL_SLAVE_NODE_INFORMATION  0xA0
// VIRTUAL_SLAVE_LEARN_MODE_ENABLE   0x01
// VIRTUAL_SLAVE_LEARN_MODE_ADD      0x02
// FUNC_ID_ZW_SET_SLAVE_LEARN_MODE   0xA4

uint8_t slave_lm_start[] = {0x04, RESPONSE, FUNC_ID_ZW_SET_SLAVE_LEARN_MODE,
                            0x01, 0x5f};
ser_if_reply_entry_t slave_lm_start_reply = {FUNC_ID_ZW_SET_SLAVE_LEARN_MODE,
                                             "FUNC_ID_ZW_SET_SLAVE_LEARN_MODE", slave_lm_start};
uint8_t slave_lm_found2[] = {0x07, 0x00, FUNC_ID_ZW_SET_SLAVE_LEARN_MODE,
                             // adding_slave,
                            0x03, 0x01, 0x00, 0x02, 0x51};
uint8_t slave_lm_found3[] = {0x07, 0x00, FUNC_ID_ZW_SET_SLAVE_LEARN_MODE,
                            0x03, 0x01, 0x00, 0x03, 0x51};
uint8_t slave_lm_found4[] = {0x07, 0x00, FUNC_ID_ZW_SET_SLAVE_LEARN_MODE,
                            0x03, 0x01, 0x00, 0x04, 0x51};
uint8_t slave_lm_found5[] = {0x07, 0x00, FUNC_ID_ZW_SET_SLAVE_LEARN_MODE,
                            0x03, 0x01, 0x00, 0x05, 0x51};
uint8_t* lm_found_array[] = {NULL, NULL, /* 0 is not a node and 1 is gateway */
                           slave_lm_found2, slave_lm_found3, slave_lm_found4, slave_lm_found5};

// FUNC_ID_ZW_GET_VIRTUAL_NODES                    0xA5
uint8_t virt_nodes[] = {0x20, RESPONSE, FUNC_ID_ZW_GET_VIRTUAL_NODES,
                        0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x84 };

uint8_t virt_nodes_empty[] = {0x20, RESPONSE, FUNC_ID_ZW_GET_VIRTUAL_NODES,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x84 };

//COMMAND_CLASS_MULTI_CHANNEL_V2                                                   0x60
//uint8_t mult_ch_asc[] = {0x20, RESPONSE, COMMAND_CLASS_MULTI_CHANNEL_V2,

#include "nvm_tools.h"
//FUNC_ID_NVM_BACKUP_RESTORE        0x2e
uint8_t nvm_backup_len[] = {0x08, RESPONSE, FUNC_ID_NVM_BACKUP_RESTORE,
                            0x00, 0x00, // return value, buf-len
                            0x00, 0x12,
                            0x00}; //offset1, offset2, no buffer

uint8_t nvm_backup_data[] = {0x18, RESPONSE, FUNC_ID_NVM_BACKUP_RESTORE,
                             nvm_backup_restore_open, 0x12, // return value, buf-len
                             0x00, 0x00, // offset
                             0xde, 0xad, 0xbe, 0xef, 0x01, 0x02, 0x03, 0x04,
                             0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c,
                             0xfa, 0xb5, 0x00};

uint8_t nvm_backup_eof[] = {0x0a, RESPONSE, FUNC_ID_NVM_BACKUP_RESTORE,
                            0xff, 0x00, // return value, buf-len (eof?)
                            0x00, 0x12, //offset?
                            0xde, 0xad, 0xbe, 0xef };

uint8_t nvm_backup_closed[] = {0x0a, RESPONSE, FUNC_ID_NVM_BACKUP_RESTORE,
                               0x00, 0x00, // return value, buf-len (eof?)
                               0x00, 0x12, //offset?
                               0xde, 0xad, 0xbe, 0xef };

uint8_t dummy_reply[] = {CAN};


// SOF, len, type,cmd
uint8_t frame[] = {SOF, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, };
uint8_t frame_index = 0;

/* TODO: put data in here to match what the test case needs. */
/** The frames that are used as reply if there is no standard reply.
 */
uint8_t* replies[] = {NIF_resp, // 41
                      proto_info1, // 41
                      proto_info1, // 41
//                      proto_info1, // 41

/* If there is no pre-existing eeprom.dat, the zgw tries to create
 * virtual nodes.  If the mock gives it the same node ids as in the
 * virt_nodes frame, ZGW does not accept the new node id, since it
 * filters out all pre-existing virtual nodes.
 *
 * TODO: make the add-node mock smarter, so that it starts with no
 * virtual nodes if there is no eeprom.dat.  Alternately fix the ZGW,
 * so that it uses the virtual nodes from the bridge instead of
 * generating new ones. */
#ifdef FIRST_RUN
                      virt_nodes_empty, // a5
#else
                      virt_nodes, // a5
#endif
                      virt_nodes, // a5
                      dummy_reply,
                      NULL};

/* The next reply to send */
uint16_t cur_reply_idx = 0;
/* How many replies should this test case use.
 * TODO: move out so that the fall-over replies are configurable. */
uint16_t num_replies = 6;

/* The entire startup sequence is listed here.  */
//{ser_cap, // 0, 07
//                      ser_api_setup, // 1, 0b
//   version_data, // 2, 15
//                      init_data, // 3, 02
//                      version_data, // 4, 15
                      // 4a
                      // 4b
                      // 50
//                      mem_id, // 5, 20
//                      random_word_not, // 6, 1c

                      /* set up */
/*                      mem_id, // 20
                      set_suc_reply,
                      lib_type_reply, random_word_not, random_word_not,
                      random_word_not, random_word_not, random_word_not, random_word_not,
                      random_word_not, random_word_not, get_suc, random_word_not,
                      random_word_not, random_word_not, random_word_not, random_word_not,
                      random_word_not, random_word_not, random_word_not, random_word_not,
                      random_word_not, random_word_not, random_word_not, random_word_not,
                      random_word_not, random_word_not, random_word_not,
*/                      /* end set up */

//                      NIF_resp, // 41
//                      init_data, // 02
//                      ctrl_cap_resp, // 05
//                      mem_id, // 20
//                      random_word_not, random_word_not, random_word_not, random_word_not, // 1c
//                      random_word_not, random_word_not, random_word_not, random_word_not, // 1c
//                      get_suc, // 56
//                      lib_type_reply, // bd
//                      proto_info1, // 41
//                      // 03
//                      proto_info1, // 41
//                      proto_info1, // 41
                      // a0
//                      virt_nodes, // a5
/* Add virtual nodes */
//                      virt_nodes_empty, // a5
//                      ctrl_cap_resp, // 05
                      //4a
/* Add virtual nodes */
//                      slave_lm_start, /* a4  */ slave_lm_found2, /* request */ get_suc, // 56
//                      slave_lm_start, slave_lm_found3, get_suc, // 56
//                      slave_lm_start, slave_lm_found4, get_suc, // 56
//                      slave_lm_start, slave_lm_found5, get_suc, // 56
//                      mem_id, // 20
//                      virt_nodes, // a5
//                      init_data, // 02
//                      proto_info2, // 41
//                      proto_info2, // 41
//                      proto_info6, // 41
//                      init_data, // 02
                      // a0
//                      virt_nodes, // a5
//                      ctrl_cap_resp, // 05
                      // 4a
//                      version_data,
//                      dummy_reply,
//                      NULL};
/* To set up:                      mem_id,
                      set_suc_reply, lib_type_reply, random_word_not, random_word_not,
                      random_word_not, random_word_not, random_word_not, random_word_not,
                      random_word_not, random_word_not, get_suc, random_word_not,
                      random_word_not, random_word_not, random_word_not, random_word_not,
                      random_word_not, random_word_not, random_word_not, random_word_not,
                      random_word_not, random_word_not, random_word_not, random_word_not,
                      random_word_not, random_word_not, random_word_not,
uint16_t num_replies = 36;*/
/* to set up, more replies are needed. */
//uint16_t num_replies = 31;


/* Whether the test needs any more serial interface with the serial
 * mock.  (This only covers the responses set up specifically for this
 * case, not the automated responses.)
 *
 * This can be used to block the mock from replying during part of the
 * test. */
bool bridge_controller_mock_active = true;

/* Add node mocking */
// 0 idle
// 1 before start reply
// 2 before found reply
uint8_t mp_add_node_mode = 0;
/* The last nodeid given out.  Assumes the gateway is 1. Increase when
 * mp_add_node_mode is set to 1.  Used to find the right
 * FUNC_ID_ZW_SET_SLAVE_LEARN_MODE frame to send to the gateway. */
uint8_t mp_new_nodeid = 1;

/* Does the last frame received on the con_handle mock expect a reply?
 * (ie, was it sent with SendFrameWithResponse())
 *
 * The mock will fetch the reply to this from the replies arrays (until we come
 * up with something smarter).
 *
 * This is only whether a reply is expected, not whether there is one available. */
bool reply_expected = false;

/* Has the mock con received a request and there is a reply in the
 * replies queue?
 *
 * The reply could be from the standard list or from the test case
 * (the "replies" array).
 *
 * This will warn us when we run out of replies. */
bool reply_ready = false;

/* ****************************************** */
/* The standard (automatic) reply frames */

ser_if_reply_entry_t* mp_std_replies[] = {&init_data_reply,      //1 02
                                   &ser_cap_reply,        //2 07
                                   &ser_api_setup_reply,  //3 0b
                                   &version_data_reply,   //4 15
                                   &mem_id_reply,         //5 20
                                   &random_word_not_reply, //6 1c
                                   &ctrl_cap_reply,  //7 05
                                   &slave_lm_start_reply, //8 a4
                                   &get_suc_reply,        //9 56
                                   &set_suc_reply,  //10 54
                                   &lib_type_reply, //11 0xbd
                                   &protocol_version_data_reply,   //12 09
};
int num_std_replies = 12;


/* Inject \p frame into the DUT.
 * AKA, mock sending a frame to the zipgateway's serial interface.
 */
void mp_inject_frame(uint8_t *frame);


uint8_t* mp_find_reply_by_cmd(uint8_t cmd) {
   uint16_t ii = 0;
   for (ii = 0; ii < num_std_replies; ii++) {
      if ((mp_std_replies[ii]->frame)[2] == cmd) {
         zgw_log(zwlog_lvl_data,
                 "Reply to cmd %s (0x%02x) is index %d\n",
                 mp_std_replies[ii]->func_name, cmd, ii);
         return mp_std_replies[ii]->frame;
      }
   }
   return NULL;
}
/* ****************************************** */


/* ********************************************* */
enum mp_nvm_mock_states {
   mp_nvm_normal, /**< Normal operation. TODO: check that we are in
                   * normal operation before accepting frames for the
                   * protocol. */
   mp_nvm_backup_started, /**< We have received an open */
   mp_nvm_send_backup,
   mp_nvm_data_sent, /* waiting for close */
};
enum mp_nvm_mock_states mp_nvm_state = mp_nvm_normal;


/* ********************************************* */
/* Check if the protocol mock should send unsolicited data to the gateway.
 *
 * Data could be the next part of a non-stateless interaction, like add_node.
 * It could also be a frame that a test case has lined up to be injected.
 *
 * This function is called from the main loop.  It checks the state
 * machine of the mock protocol.  If it finds stg to inject, it sends
 * it to the gateway with inject_frame.
 *
 * TODO: can we restrict the serial api poll to only be done from here? */
bool mp_check_state(void) {
   if (mp_add_node_mode == 2) {
      /* When adding a node, two frames arrive from serial as a
       * response to one Tx.  This triggers the second frame.  Add
       * node support is needed to start up the gateway, so that it
       * can create virtual nodes. */
      zgw_log(zwlog_lvl_control, "Inject node %u found\n", mp_new_nodeid);
      mp_inject_frame(lm_found_array[mp_new_nodeid]); // TODO: unsolicited flag ??
//      process_poll(&serial_api_process);
      mp_add_node_mode = 0; /*more??*/
      return true;
   }
   return false;
}
/* ********************************************* */


/* Find the reply frame from the nvm backup state machine */
uint8_t *mp_find_reply_to_backup(uint8_t cmd, uint8_t op) {
   uint8_t *res = NULL;

   switch (mp_nvm_state) {
   case mp_nvm_normal:
      if (op == nvm_backup_restore_open) {
         zgw_log(zwlog_lvl_configuration, "Closing mock serial/protocol for normal operation.\n");
         mp_nvm_state = mp_nvm_backup_started;
         res = nvm_backup_len;
      } else {
         test_print(0, "Unexpected backup open frame in state mp_nvm_normal\n");
         check_true(false, "Backup state change\n");//numErrs++;
      }
      break;
   case mp_nvm_backup_started:
      if (op == nvm_backup_restore_read) {
         /* Send first frame */
         zgw_log(zwlog_lvl_info, "Reading mock NVM on mock serial.\n");
         mp_nvm_state = mp_nvm_send_backup;
         res = nvm_backup_data;
      } else {
         test_print(0, "Unexpected backup read frame in state mp_nvm_started\n");
         check_true(false, "Backup state change\n");//numErrs++;
      }
      break;
   case mp_nvm_send_backup:
      if (op == nvm_backup_restore_read) {
         /* There is only one frame in the mock, so go straight to
          * send EOF.  TODO: if we want to send more frames, we can do
          * a "send until no more data" dance in this state. */
         zgw_log(zwlog_lvl_configuration, "Done reading mock NVM on mock serial.\n");
         mp_nvm_state = mp_nvm_data_sent;
         res = nvm_backup_eof;
      } else {
         test_print(0, "Unexpected backup read frame in state mp_nvm_send_backup\n");
         check_true(false, "Backup state change\n");//numErrs++;
      }
      break;
   case mp_nvm_data_sent:
      if (op == nvm_backup_restore_close) {
         zgw_log(zwlog_lvl_configuration,
                 "Close mock NVM, open mock serial for normal operation.\n");
         mp_nvm_state = mp_nvm_normal;
         res = nvm_backup_closed;
      } else {
         test_print(0, "Unexpected backup read frame in state mp_nvm_data_sent\n");
         check_true(false, "Backup state change\n");//numErrs++;
      }
      break;
   default:
      res = NULL;
      break;
   }
   return res;
}
/* ****************************************** */

void mp_inject_frame(uint8_t *frame) {
   /* Send the frame on the mock serial api */
   mock_conhandle_inject_frame(frame);
   /* Mock the bridge controller state transisions */
   if (mp_add_node_mode == 1) {
      /* When the protocol is in add node mode, it should not send
         other frames before the learn_mode response.  When we send
         the response, remember that next things is to send
         node-found. */
      mp_add_node_mode = 2;
   }
}

/* ****************************************** */
/* Mock an incoming frame to ZGW on the serial API from the hardcoded list.
 *
 * Copy the frame in cur_reply_idx to serBuf.
 * Increase the reply index in the hardcoded list.
 *
 * Called
 * - when the test case wants to inject an unsolicited frame to
 * the ZGW or
 * - when the test mock should respond to a frame that was sent
 * from the gateway (ie, sent with SendFrameWithResponse()), but
 * the response cannot be determined automatically.
 *
 * cur_reply_idx is managed here.
 *
 * TODO:
 * - Automate the last of the startup frames.
 * - Make this list stg dynamic that the test cases can write to. */
void mp_next_incoming() {
   zgw_log_enter();

   if (cur_reply_idx < num_replies) {
      zgw_log(zwlog_lvl_info, "Sending next frame to gateway from test case\n");

      mp_inject_frame(replies[cur_reply_idx]);
      zgw_log(zwlog_lvl_data, "reply %d of %d ready\n", cur_reply_idx, num_replies);
      cur_reply_idx++;
      reply_ready = true;
   } else {
      zgw_log(zwlog_lvl_info, "conTxFrame, no more replies\n");
   }

   zgw_log_exit();
}
/* ****************************************** */

uint8_t *mp_next_reply = NULL;

/* hacked guess-gorithm to find out if the mock/test case should reply to the frame \p cmd.
 *
 * The frame was just sent from the zipgateway.
 *
 * The frame should be copied to tx_buf_last_sent before calling this function for validation.
 *
 * Format SOF; len+3; type; cmd; data.
*/
void mp_parse_frame(uint8_t cmd, uint8_t type, uint8_t len, uint8_t *tx_buf_last_sent) {
   bool is_request = false;

   /* Guesstimate if the mock should reply to the frame */
   /* cmd is in position 4 */
   if (cmd == FUNC_ID_ZW_SET_SUC_NODE_ID) {
      is_request = true;
   } else if (cmd == FUNC_ID_SERIAL_API_APPL_NODE_INFORMATION
              || cmd == FUNC_ID_SERIAL_API_APPL_SLAVE_NODE_INFORMATION) {
      zgw_log(zwlog_lvl_control,
              "No reply needed for APPL_NODE_INFORMATION or APPL_SLAVE_NODE_INFORMATION\n");
      is_request = false;
   } else if ((len > 1) && (tx_buf_last_sent[5] == 0x00)) {
      zgw_log(zwlog_lvl_control, "No reply needed\n");
      is_request = false;
   } else {
      zgw_log(zwlog_lvl_control, "Expects reply\n");
      is_request = true;
   }
   if (len <= 1) {
      zgw_log(zwlog_lvl_control, "No data in frame\n"); /* So it kind of has to be a request */
      is_request = true;
   }

   /* set reply_expected if this frame expects one, but don't clear it
    * if it is already true */
   if (is_request) {
      reply_expected = true;
      zgw_log(zwlog_lvl_control, "MP %sexpect a reply\n",
              is_request?"does ":"does not ");

      /* TODO: make mp_next_reply a queue */
      /* Figure out if the reply should be from the standard frame
       * table or from the test case reply engine.*/
      switch (cmd) {
      case FUNC_ID_ZW_SET_SLAVE_LEARN_MODE:
         zgw_log(zwlog_lvl_info, "Add node expects 2 replies\n");
         mp_add_node_mode = 1;
         mp_new_nodeid++;

         mp_next_reply = mp_find_reply_by_cmd(cmd);
         zgw_log(zwlog_lvl_info,
                 "Replying to FUNC_ID_ZW_SET_SLAVE_LEARN_MODE 0x%02x\n",
                 FUNC_ID_ZW_SET_SLAVE_LEARN_MODE);
         break;
      case FUNC_ID_NVM_BACKUP_RESTORE:
         zgw_log(zwlog_lvl_info,
                 "Replying to FUNC_ID_NVM_BACKUP_RESTORE op: 0x%02x\n",
                 tx_buf_last_sent[4]);

         mp_next_reply = mp_find_reply_to_backup(cmd, tx_buf_last_sent[4]);
         break;
      default:
         mp_next_reply = mp_find_reply_by_cmd(cmd);
         break;
      }
      if (mp_next_reply == NULL) {
         /* If it is not a standard reply, maybe the test case has a
          * reply it wants to give. */
         mp_next_incoming();
      } else {
         zgw_log(zwlog_lvl_control,
                 "Next frame sent to gateway from test framework standard list\n");
         mp_inject_frame(mp_next_reply);
         /* Move standard reply from queued to idle.  */
         mp_next_reply = NULL;
         /* Indicate to the mock conhandle that there is a frame ready. */
         reply_ready = true;
      }

   }
   if (bridge_controller_mock_active && (cur_reply_idx >= num_replies)) {
      bridge_controller_mock_active = false;
      zgw_log(zwlog_lvl_warning,
              "SERIAL INTERFACE MOCK COMPLETED, SENT ALL %d OF %d SCHEDULED REPLIES\n",
              cur_reply_idx, num_replies);
   }

   return;
}
