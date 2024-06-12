/* -*- C -*- */
%{
/* © 2017 Silicon Laboratories Inc.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "provisioning_list.h"
#include "pvs_internal.h"
#include "pvs_parse_help.h"
#include <ZIP_Router_logging.h>

    extern FILE *yyin;
    extern int yyleng;
    /* size_t pvs_parse_buf_len; */

  extern int yylex(void);
  extern int yylex_destroy (void );

  void yyerror (char const *);

  static uint16_t device_count = 0;
  static uint16_t devices_added = 0;
  static uint16_t errors_found = 0;

#ifndef DOXYGEN_SHOULD_SKIP_THIS

%}

%locations
%defines
%define parse.error verbose

%define api.value.type union
/* %define api.value.type {int} */
 /* The terminals */
%token <int> NUM
%token <uint16_t> QRBLOCK
%token <int> HEADER
%token <int> DEVICE
%token <int> QR
%token <int> TYPE
%token <struct pvs_parse_buf*> DSK
%token <int> S2
%token <int> SS
%token <int> LR 
%token <int> DSK-BOOTMODE
%token <int> VALUE
%token <int> TLVS

 //%token <struct pvs_parse_buf*> DSKVAL
%token <struct pvs_parse_buf*> ASCIIHEX
%token <struct provision*> DEVICEPTR

%token <int> TLVTYPE
%token <struct pvs_parse_buf*> STR
%token <struct pvs_tlv*> TLV

 /* The non-terminals */
%type <int> dsk-bootmode-spec
%type <struct provision*> pvscfg
%type <struct provision*> devspec
%type <struct pvs_parse_buf*> dsk-spec
%type <struct pvs_tlv*> tlvspec
%type <struct pvs_tlv*> tlv-list
%type <struct pvs_tlv*> tlv-list-spec
%type <struct pvs_parse_buf*> tlv-value-spec

%destructor {
    /* printf("Cleaning up strings automagically\n"); */
    pvs_parse_buf_free($$);
} dsk-spec ASCIIHEX STR tlv-value-spec

%destructor {
    /* printf("Cleaning up tlvs automagically\n"); */
    pvs_tlv_clear($$);
  } tlv-list tlv-list-spec tlvspec

%% /* Grammar rules and actions follow.  */

config:
HEADER pvscfg {
    printf("Created %u devices\n", devices_added);
}

/* A provisioning list specification is a collection of device specifications */
pvscfg:
%empty {
    printf("Starting import.\n");
}
| pvscfg devspec {
    /* printf("Reduce on device %d of %d\n", devices_added, device_count); */
}
| pvscfg error {
    printf("Error on %d, %d\n", devices_added, device_count);
}


/* A device specification gives the DSK, the bootmode and the list of tlvs */
devspec:
  DEVICE '=' '[' dsk-spec ',' dsk-bootmode-spec ',' tlv-list-spec ']'  {
      device_count++;
      if ($6 >= 0 && $4 != NULL) {
          struct provision *dev;
          size_t ii;
          dev = provisioning_list_dev_add($4->len, $4->buf, $6);
          if (dev != NULL) {
              devices_added++;
              dev->tlv_list = $8;
              printf("Created device %3d (0x",
                     device_count);
              for (ii=0; ii<$4->len; ii++) {
                  printf("%02x", $4->buf[ii]);
              }
          printf(").\n");
          } else {
              pvs_tlv_clear($8);
              errors_found++;
              if (device_count > PROVISIONING_LIST_SIZE) {
                  printf("Failed to add device %d. Max %d devices allowed.\n",
                         device_count, PROVISIONING_LIST_SIZE);
              } else {
                  printf("Failed to add device %d.  DSK overlap or memory shortage.\n", device_count);
              }
          }
      } else {
          pvs_tlv_clear($8);
          errors_found++;
          printf("Errors in specification.  Skipping device %d.\n", device_count);
      }
      if ($4) {
          if ($4->buf) {
              free($4->buf);
              $4->buf = NULL;
          }
          free($4);
      }
/* | '\n' { printf("> "); } */
  }
|  DEVICE '=' '[' dsk-spec ',' dsk-bootmode-spec ',' error ']'  {
    pvs_parse_buf_free($4);
    device_count++;
    errors_found++;
    printf("Invalid TLV specification in device %u.\n",
           device_count);
   }
|  DEVICE '=' '[' dsk-spec ',' error ',' tlv-list-spec ']'  {
    pvs_parse_buf_free($4);
    pvs_tlv_clear($8);
    device_count++;
    errors_found++;
    printf("Invalid device boot mode specification in device %u, line %d.\n",
           device_count, yylloc.last_line);
   }
|  DEVICE '=' '[' error ',' dsk-bootmode-spec ',' tlv-list-spec ']'  {
    device_count++;
    errors_found++;
    pvs_tlv_clear($8);
    printf("Invalid DSK specification in device %u.\n",
        device_count);
   }
|  DEVICE '=' '[' error ',' error ',' error ']'  {
    device_count++;
    errors_found++;
    printf("Invalid device specification in device %u.\n",
        device_count);
   }


dsk-spec:
DSK '=' ASCIIHEX {
    $$ = $3;
    if ($3 != NULL) {
         /* printf("Read ascii-hex dsk of length %d, converted to ascii buffer (size %u).\n", */
         /*        yyleng, $3->len); */
        if ($3->len != 16) {
            printf("DSK length incorrect in device %u, line %d (%d), got %zu, expected %u.\n",
                   device_count+1, yylloc.last_line, yylloc.first_column, $3->len, 16);
            free($3->buf);
            free($3);
            $$ = NULL;
            YYERROR;
        }
    }
}
| DSK '=' QR ':' QRBLOCK '-' QRBLOCK '-' QRBLOCK '-' QRBLOCK '-' QRBLOCK '-' QRBLOCK '-' QRBLOCK '-' QRBLOCK {
    /* printf("Read QR block\n"); */
    $$ = pvs_qr_dsk_add($5, $7, $9, $11, $13, $15, $17, $19);
}
| DSK '=' QR ':' error {
    printf("Incorrect DSK QR format in device %d, line %d (%d).\n",
           device_count+1, yylloc.last_line, yylloc.first_column);
    $$ = NULL;
}

dsk-bootmode-spec:
  TYPE '=' S2                { $$ = PVS_BOOTMODE_S2; }
| TYPE '=' SS              { $$ = PVS_BOOTMODE_SMART_START; }
| TYPE '=' LR              { $$ = PVS_BOOTMODE_LONG_RANGE_SMART_START; }

tlv-list-spec:
  TLVS '=' '(' tlv-list ')' { $$ = $4; }
| TLVS '=' '(' error ')' {
    printf("Invalid tlv list for device %u\n", device_count+1);
    $$ = NULL;
    YYERROR;
  }

tlv-list:
%empty { $$ = NULL;}
| tlv-list tlvspec {
    /*  The list is reversed here.  That is ok, since the order is not important. */
    if ($2) {
        if (pvs_tlv_get($1, $2->type) != NULL) { 
            printf("Duplicate TVL definition (type %u) in line %d\n", $2->type, yylloc.last_line);
            errors_found++;
            free($2->value);
            free($2);
            $$ = $1; /* Should be cleaned up later */
        } else {
            $2->next = $1;
            $$ = $2;
        }
    } else {
        $$ = $1;
    }
 }

tlvspec:
'[' TYPE '=' NUM ',' VALUE '=' tlv-value-spec ']' {
    if ($8 != NULL) {
        $$ = malloc(sizeof(struct pvs_tlv));
        memset($$, 0, sizeof(struct pvs_tlv));
        if ($$ != NULL) {
            $$->type = $4;
            $$->length = $8->len;
            $$->value = $8->buf;
            $8->buf = NULL;
        } else {
            if ($8->buf) {
                free($8->buf);
            }
        }
        free($8);
    } else {
        $$ = NULL;
    }
}
| '[' TYPE '=' NUM ',' VALUE '=' error ']' {
    printf("Illegal tlv value specification on line %d.\n", yylloc.last_line);
    $$ = NULL;
    YYERROR;
  }
| '[' error ']' {
    printf("Illegal tlv specification on line %d.\n", yylloc.last_line);
    $$ = NULL;
  }

tlv-value-spec:
  ASCIIHEX
| STR

%%

  int pvs_parse_config_file(FILE *strm)
  {
      int res = 0;

      device_count = 0;
      devices_added = 0;
      errors_found = 0;
      yylloc.last_line = 1;

      yyin = strm;
      res = yyparse();
      yylex_destroy();

      if (errors_found > 0) {
          ERR_PRINTF("Ignoring config file and starting with empty provisioning list.  Found %d errors during import.\n",
                     errors_found);
          provisioning_list_clear();
          return -1; /* Parsing succeeded, but we found semantic errors */
      } else {
          return res;
      }
  }

/* Called by yyparse on error.  */
void
yyerror (char const *s)
{
    errors_found++;
    fprintf (stderr, "Cannot import the PVL configuration file: %s\n", s);
}

#endif /* DOXYGEN_SHOULD_SKIP_THIS */
