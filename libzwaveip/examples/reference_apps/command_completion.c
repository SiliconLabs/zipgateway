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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <assert.h>

#include "libedit/readline/readline.h"

#include "zw_cmd_tool.h"
#include "command_completion.h"

#include "zresource.h"

char* commands[] = {"help",       "quit",     "bye",       "exit",
                    "send",       "hexsend",  "addnode",   "removenode",
                    "setdefault", "list",     "acceptdsk", "grantkeys",
                    "learnmode",  "pl_list",  "pl_add",    "pl_remove",
                    "pl_reset",   "identify", "lifeline",  "nodeinfo",
                    0};


typedef enum {
  OPERATION = 0,
  DESTINATION,
  COMMAND_CLASS,
  COMMAND_CMD,
  PARAMETER,
  PARSE_DONE
} cmd_parser_state_t;

static struct cmd_parser {
  cmd_parser_state_t state;
  enum {
    OP_HELP, /* also covers gwsend */
    OP_SEND,
    OP_OTHER
  } op;
  struct zip_service *zwservice;
  const struct zw_command_class* zwcmdClass;
  const struct zw_command* zwcmd;
} parser;

/* We keep a local copy of the gateway's IP address. It's needed to lookup the
 * associated zresource/service for the gateway so we can determine its homeID
 * and all the other services with the same homeID (if more than one gateway is
 * on the local IP network mDNS will include the services from all of them -
 * we're only interested in "our" gateway)
 */
static char gateway_ip_str[INET6_ADDRSTRLEN];

/**
 * Search array of strings ("haystack") for string "needle.
 * \return 1 if found, 0 if not found.
 *
 * Last element of haystack array must be 0.
 */
static int is_in_array(const char* needle, char** haystack) {
  for (int i = 0; 0 != haystack[i]; i++) {
    if (0 == strcmp(needle, haystack[i])) {
      return 1;
    }
  }
  return 0;
}

/*
 * Parse the (most likely incomplete) command line to determine what element we
 * should present autocomplete choices for.
 * 
 * Sets the global variable parser (struct).
 *
 * Called in a loop from parse_completion_state that normally initializes
 * parser.state to OPERATION at the beginning of the command line.
 *
 * \return 1 if parser would like to see more tokens from command line (i.e. this
 *           parser state is complete and we should progress to next parser state).
 *           0 if no more tokens are wanted.
 */
int static parse_token(const char* token) {

  rl_basic_word_break_characters = " ";
  rl_completion_append_character = ' ';
  rl_quote_completion = 0;
  rl_special_prefixes = 0;

  switch (parser.state) {
    case OPERATION:
      if ((strcmp("help", token) == 0) || (strcmp("gwsend", token) == 0)) {
        parser.op = OP_HELP; /* OP_HELP also covers send and GW Send - same
                                autocompletion procedure*/
        parser.state = COMMAND_CLASS;
      } else if ((strcmp("send", token) == 0)) {
        parser.op = OP_SEND;
        parser.state = DESTINATION;
      } else if ((strcmp("hexsend", token) == 0)) {
        parser.op = OP_SEND;
        parser.state = DESTINATION;
      } else if ((strcmp("identify", token) == 0)) {
        parser.op = OP_OTHER;
        parser.state = DESTINATION;
      } else if ((strcmp("lifeline", token) == 0)) {
        parser.op = OP_OTHER;
        parser.state = DESTINATION;
      } else if ((strcmp("nodeinfo", token) == 0)) {
        parser.op = OP_OTHER;
        parser.state = DESTINATION;
      } else if (is_in_array(token, commands)) {
        parser.op = OP_OTHER;
        parser.state = PARSE_DONE;
        return 0;
      } else {
        return 0;
      }
      break;
    case DESTINATION:
      rl_basic_word_break_characters = "\"";
      rl_quote_completion = 1;
      parser.zwservice = find_service_by_friendly_name(token);
      if (parser.zwservice) {
        if (parser.op == OP_SEND) {
          parser.state = COMMAND_CLASS;
        } else {
          parser.state = PARSE_DONE;
        }
      }
      break;
    case COMMAND_CLASS:
      /* TODO: Here we should return the correct version of the cmdclass
       * structure (currently it fetches the lates version). In order to avoid
       * sending a version get for all command classes and all nodes, we
       * should only do it for the command classes actually used. So here we
       * should actually send a COMMAND_CLASS_VERSION,
       * VERSION_COMMAND_CLASS_GET and store the result in cc_info item for
       * the service and then use that to look up the correct command class
       * structure. The current send/recv handling is not well suited for such
       * requests, so I'll leave it for a future exercise.
       */
      parser.zwcmdClass = zw_cmd_tool_get_cmd_class(parser.zwservice,
                                                    zw_cmd_tool_class_name_to_num(token),
                                                    0);
      if (parser.zwcmdClass) {
        parser.state = COMMAND_CMD;
      } else {
        return 0;
      }
      break;
    case COMMAND_CMD:
      parser.zwcmd = zw_cmd_tool_get_cmd_by_name(parser.zwcmdClass, token);

      if (parser.zwcmd && (parser.op == OP_HELP || parser.op == OP_SEND)) {
        parser.state = PARSE_DONE;
      } else {
        return 0;
      }
      break;
    case PARSE_DONE:
    case PARAMETER:
      break;
  }
  return 1;
}

/* Tokenize a sub-string
 *
 * This is intended to be used on the readline 'rl_line_buffer' variable that
 * contains the current line input *but* could also contain the remains of
 * previous input lines. The readline variable 'rl_end' tells how many
 * characters in rl_line_buffer belong to the current input line.
 *
 * \param string The string to get space separated tokens from. Only set when
 * getting the first token. Set to NULL for the succeeding calls to get the
 * remaining tokens.
 *
 * \param num_chars Only process this many characters in the string (for
 * readline the 'rl_end' variable should be passed here).
 */
char* my_strtok(const char* string, int num_chars) {
  static const char* s;
  static int pos;
  static enum {
    TOKEN_STATE_WORD_START,
    TOKEN_STATE_WORD,
    TOKEN_STATE_QUOTED_STING
  } token_state;
  static char token_buffer[512];
  static char* d;

  if (string) {
    /* Since string != NULL this is the first call in a sequence to tokenize
     * string. In subsequent calls string will (should) be NULL
     */
    s = string;
    pos = 0;
    token_state = TOKEN_STATE_WORD_START;
  }

  if (num_chars == 0 || pos >= num_chars || s[pos] == 0) {
    return 0;
  }

  memset(token_buffer, 0, sizeof(token_buffer));

  while (pos < num_chars && s[pos] != 0) {
    switch (token_state) {
      case TOKEN_STATE_WORD_START:
        if (s[pos] == '"') {
          token_state = TOKEN_STATE_QUOTED_STING;
          d = token_buffer;
        } else if (!isspace(s[pos])) {
          token_state = TOKEN_STATE_WORD;
          d = token_buffer;
          *d++ = s[pos];
        }
        break;
      case TOKEN_STATE_WORD:
        if (isspace(s[pos])) {
          goto return_token;
        } else {
          *d++ = s[pos];
        }
        break;
      case TOKEN_STATE_QUOTED_STING:
        if (s[pos] == '"') {
          goto return_token;
        } else {
          *d++ = s[pos];
        }
        break;
    }
    pos++;
  }

return_token:
  *d = 0;
  pos++; // Get ready for next call
  token_state = TOKEN_STATE_WORD_START;
  return token_buffer;
}

/* Run through the input line (from beginning to end) and determine the parsing
 * state based on the recognized tokens (this is done every time the completion
 * key (tab) is pressed)
 */
static void parse_completion_state(void) {
  const char* token;

  memset(&parser, 0, sizeof(parser));
  parser.state = OPERATION;

  token = my_strtok(rl_line_buffer, rl_end);
  while (token) {
    if (parse_token(token) == 0) {
      break;
    }
    token = my_strtok(0, rl_end);
  }
}

/* For every call, return the next possible token value that the input parameter
 * 'text' can be completed to.
 *
 * The 'text' parameter contains the current (incomplete) token value from the
 * input line.
 *
 * The parser.state (set by parse_completion_state()) determines what set of
 * values should be used to match the 'text' parameter.
 */
static char* operation_generator(const char* text, int state) {
  static int list_index, len;
  static char gw_homeid[20];
  const char* name;

  if (state == 0) {
    parse_completion_state();
    list_index = 0;
    len = strlen(text);
    if (parser.state == DESTINATION) {
      struct zip_service *gw_s = zresource_find_service_by_ip_str(gateway_ip_str);
      if (gw_s) {
        zresource_get_homeid(gw_s, gw_homeid, sizeof(gw_homeid));
      }
    }
  }
  switch (parser.state) {
    case OPERATION:
      // Provide next matching command based on the array of known command names
      while (commands[list_index] != 0) {
        name = commands[list_index++];
        if (strncmp(name, text, len) == 0) {
          return (strdup(name));
        }
      }
      break;

    case DESTINATION:
      // Provide next matching destination based on the known zip_resources
      // with the same homeID as the connected gateway.
      {
        struct zip_service *s = zresource_list_matching_services(text, gw_homeid, list_index++);
        if (s) {
          return strdup(s->friendly_name);
        }
      }
      break;

    case COMMAND_CLASS:
      // Provide next matching command class name based on the list of known
      // command classes and the command classes supported by the selected
      // destination (zip service)
      {
        cc_name_map_t *cc_item = NULL;
        while ((cc_item = zw_cmd_tool_match_class_name(text, len, &list_index))) {
          if (!parser.zwservice || zresource_get_cc_info(parser.zwservice, cc_item->number)) {
            return strdup(cc_item->name);
          }
        }
      }
      break;

    case COMMAND_CMD:
      // Provide next matching command based on the selected command class.
      while (parser.zwcmdClass->commands[list_index]) {
        name = parser.zwcmdClass->commands[list_index++]->name;
        if (strncmp(name, text, len) == 0) {
          return (strdup(name));
        }
      }
      break;

    default:
      break;
  }
  // If no names matched, then return NULL.
  return NULL;
}

static char** my_completion(const char* text, int start, int end) {
  char** matches;
  matches = (char**)NULL;

  matches = rl_completion_matches((char*)text, &operation_generator);
  return (matches);
}

#define HISTORY_FILENAME ".reference_client_history"

/* **************************************************************** */
/*                                                                  */
/*                  Interface to Readline Completion                */
/*                                                                  */
/* **************************************************************** */

/* Tell the GNU Readline library how to complete.  We want to try to complete
   on command names if this is the first word in the line, or on filenames
   if not. */
static void initialize_readline() {
  /* Allow conditional parsing of the ~/.inputrc file. */
  rl_readline_name = "cmd_gen";

  /* Tell the completer that we want a crack first. */
  rl_attempted_completion_function = my_completion;

  read_history(HISTORY_FILENAME);
}

void initialize_completer(const char *gateway_ip) {
  zw_cmd_tool_fill_cc_name_map();

  assert((strlen(gateway_ip) + 1) <= sizeof(gateway_ip_str));
  
  strncpy(gateway_ip_str, gateway_ip, sizeof(gateway_ip_str));
  gateway_ip_str[sizeof(gateway_ip_str) - 1] = '\0';

  initialize_readline();
}

void stop_completer() {
  write_history(HISTORY_FILENAME);
  // history_truncate_file(HISTORY_FILENAME, 200);
}
