/* -*- C -*- */
%{
/* © 2014 Silicon Laboratories Inc. */

#include <stdint.h>
#include "provisioning_list.h"
#include "pvs_parse_help.h"
#include "pvs_cfg.tab.h"
%}

%option warn

%option noyywrap
%option yylineno

DIGIT [0-9]
HEXDIGIT [0-9a-fA-F]
ALPHA [a-zA-Z]
ALPHANUM [0-9a-zA-Z]
ID    [a-z][a-z0-9]*
ASC     [\x00-\x7f]
ASCC    [\x00-\x21\x23-\x7f]
ASCN    [\x00-\t\v-\x7f]
ASCNS    [\x00-\t\v-\x19\x21-\x7f]
U       [\x80-\xbf]
U2      [\xc2-\xdf]
U3      [\xe0-\xef]
U4      [\xf0-\xf4]
UANY    {ASC}|{U2}{U}|{U3}{U}{U}|{U4}{U}{U}{U}
UANYC   {ASCC}|{U2}{U}|{U3}{U}{U}|{U4}{U}{U}{U}
UANYN   {ASCN}|{U2}{U}|{U3}{U}{U}|{U4}{U}{U}{U}
UANYNS   {ASCNS}|{U2}{U}|{U3}{U}{U}|{U4}{U}{U}{U}

%%
{DIGIT}{5} {
    uint32_t tmp;
    yylloc.first_column = yylloc.last_column;
    yylloc.last_column += yyleng;
    tmp = atoi(yytext);
    if (tmp > 0xffff) {
        return *yytext;
    } else {
        yylval.QRBLOCK = (uint16_t)tmp;
        return QRBLOCK;
    }
}

0x({HEXDIGIT}{2})+ {
/* 0x{HEXDIGIT}+ { */
    yylval.ASCIIHEX = pvs_asciihex_add(yytext, yyleng);

    yylloc.first_column = yylloc.last_column;
    yylloc.last_column += yyleng;
    /* printf("Found hex string %s, length %d\n", yytext, yyleng); */
    return ASCIIHEX;
}

{DIGIT}+ {
    /* printf("Found number %d\n", atoi(yytext)); */
    yylval.NUM = atoi( yytext );
    yylloc.first_column = yylloc.last_column;
    yylloc.last_column += yyleng;
    if (yylval.NUM > 255) {
        return *yytext;
    } else {
        return NUM;
    }
}

"ZIPGateway Smart Start Provisioning List Configuration, version = 1.0." {
    /* printf("Found keyword %s\n", yytext); */
    yylloc.first_column = yylloc.last_column;
    yylloc.last_column += yyleng;
    return HEADER;
}

device {
    /* printf("Found keyword %s\n", yytext); */
    yylloc.first_column = yylloc.last_column;
    yylloc.last_column += yyleng;
    return DEVICE;
}

dsk {
    /* printf("Found keyword %s\n", yytext); */
    yylloc.first_column = yylloc.last_column;
    yylloc.last_column += yyleng;
    return DSK;
}

QR {
    /* printf("Found keyword %s\n", yytext); */
    yylloc.first_column = yylloc.last_column;
    yylloc.last_column += yyleng;
    return QR;
}

type {
    /* printf("Found keyword %s\n", yytext); */
    yylloc.first_column = yylloc.last_column;
    yylloc.last_column += yyleng;
    return TYPE;
}

SmartStart {
    /* printf("Found keyword %s\n", yytext); */
    yylloc.first_column = yylloc.last_column;
    yylloc.last_column += yyleng;
    return SS;
}

S2 {
    /* printf("Found keyword %s\n", yytext); */
    yylloc.first_column = yylloc.last_column;
    yylloc.last_column += yyleng;
    return S2;
}

value {
    /* printf("Found keyword %s\n", yytext); */
    yylloc.first_column = yylloc.last_column;
    yylloc.last_column += yyleng;
    return VALUE;
}

tlvs {
    /* printf("Found keyword %s\n", yytext); */
    yylloc.first_column = yylloc.last_column;
    yylloc.last_column += yyleng;
    return TLVS;
}

utf-8:\"{UANYC}*\" {
    /* For now, no citation mark in string! */

    // TODO: escaped characters in the string.

    /* printf("Read input string %s\n", yytext); */
    yylval.STR = pvs_str_add(yytext, yyleng);
    yylloc.first_column = yylloc.last_column;
    yylloc.last_column += yyleng;
    if (!(yylval.STR)) {
        printf("Failed to allocate a string in line %d\n", yylloc.last_line);
        return 0;
    }
    return STR;
}

^#.*$ {
    /* printf("Skip one-line comments starting with #\n"); */
 }

[ \t]+ {
    yylloc.last_column += yyleng;
    /* printf("Ignoring whitespace\n"); */
}

[\n]   {
  ++yylloc.last_line;
  yylloc.last_column = 1;
}

<<EOF>> {
    /* yy_delete_buffer(YY_CURRENT_BUFFER);  - handled in yylex_destroy() */
    yyterminate();
  }

. {
  yylloc.last_column++;
  /* printf("Unknown character: '%02x'\n", *yytext); */
  return *yytext;
  }

%%

  /* not used */
  //int test_main(int argc, char **argv) {
  //yylex();
  //}
