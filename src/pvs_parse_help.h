#ifndef PVS_PARSE_HELP_H
#define PVS_PARSE_HELP_H

/** \ingroup pvslist_cfg 

 */


struct pvs_parse_buf {
    size_t len;
    uint8_t *buf;
};

/** Create a string object suitable to put in a PVS TLV from a parsed text. 
 * \param text A flex pointer to the parsed string.
 * \param text_len The length of the parsed token. 
 * \return The string we want to use and its length. 
 */
/*@null@*/struct pvs_parse_buf * pvs_str_add(char const *text, size_t text_len);

/** Create a string object suitable to put in a PVS TLV from a parsed
 * string of letters and digits representing a hex number. */
/*@null@*/struct pvs_parse_buf * pvs_asciihex_add(char const *text, size_t text_len);

/** Create a string object suitable to put in a PVS DSK from a parsed
 * text containing a DSK in the QR code digit format. */
/*@null@*/struct pvs_parse_buf * pvs_qr_dsk_add(uint16_t q0, uint16_t q1, uint16_t q2, uint16_t q3, 
                                                uint16_t q4, uint16_t q5, uint16_t q6, uint16_t q7);

/** Clean up a parsed string object */
void pvs_parse_buf_free(/*@null@*/struct pvs_parse_buf *parsed_buf);

/**
 * @}
 */
#endif
