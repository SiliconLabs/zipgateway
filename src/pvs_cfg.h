/* © 2017 Silicon Laboratories Inc.
 */
#ifndef PVS_CFG_H_
#define PVS_CFG_H_


/**
 * \defgroup pvslist_cfg Provisioning List Config File Import
 * \ingroup pvslist
 *
 * Initial population of the Provisioning list can use a
 * human-readable text file.  This component parses such a file and
 * creates the provisioning list entries.


 @{

 * Devices are added incrementally.  However, if parsing fails for any
 * reason, the entire provisioning list is flushed.

\verbatim

<config> ::= <header> <pvs-config>

<header> ::= ZIPGateway Smart Start Provisioning List Configuration, version = 1.0.

<pvs-config> ::= <empty> | <pvs-config> <dev-spec>

<dev-spec> ::= device = [ <dsk-spec> , <dev-bootmode-spec> , <tlv-list-spec> ]

<dsk-spec> ::= dsk = <dsk-hex-string> | <dsk-digit-string>

<dsk-hex-string> ::= 0x<hexdigit><hexdigit><hexdigit><hexdigit><hexdigit><hexdigit><hexdigit><hexdigit><hexdigit><hexdigit><hexdigit><hexdigit><hexdigit><hexdigit><hexdigit><hexdigit>

<dsk-digit-string> ::= QR:<QR-block>-<QR-block>-<QR-block>-<QR-block>-<QR-block>-<QR-block>-<QR-block>-<QR-block>
<QR-block> ::= <digit><digit><digit><digit><digit>

<dsk-bootmode-spec> ::= type = <dsk-bootmode>
<dsk-bootmode> ::= S2 | SmartStart

<tlv-list-spec> ::= tlvs = (<tlv-list> )
<tlv-list> ::=  <empty> | <tlv-spec> <tlv-list>

<tlv-spec> ::= type = <tlv-type>, value = <tvl-value-spec>
<tlv-type> ::= <number>
<tvl-value-spec> ::= <hex-string> | <utf-8-string>

<hex-string> ::= 0x<hexdigit>+
<utf-8-string> ::= utf-8:" [<UQ>|\"|\\]+ "

<number> ::= <digit>+

<digit> ::= 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9

<hexdigit> ::= 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | A | B | C | D | E | F | a | b | c | d | e | f

\endverbatim

A DSK can be specified as an ASCII string with hex characters
   or the way it is specified in the QR code as four blocks of five digits.
A TLV specification gives the name and value.  The length is computed from the value.
A TLV value can be specified as an ASCII string with hex characters
   or as an utf-8 string.  Note that the parser does not validate the
   utf-8, any byte string will be accepted.

TODO: a better description of how the utf-8 parsing reads strings.

*/


/** Read the contents of file strm and populate the provisioning list.
 *
 * \param strm An open FILE stream.
 * \return The yyparse return code (0 on success).
 */
int pvs_parse_config_file(FILE *strm);

/**
 * @}
 */

#endif /* PVS_CFG_H_ */
