#include <stdlib.h> /* malloc. */
#include <stdio.h> /* printf */
#include <string.h> /* strlen. */
#include <stdint.h> /* uint8 */
#include <arpa/inet.h> /* htons */
#include "pvs_parse_help.h"
#include "provisioning_list.h"
#include "pvs_cfg.tab.h" /* For grammar */

/* UTF-8 TLV string allocator */
/*@null@*/struct pvs_parse_buf * pvs_str_add(char const *text, size_t text_len)
{
    size_t strlen;
    struct pvs_parse_buf *parsed_buf;

    if (text_len < 8) {
        return NULL;
    }
    parsed_buf = malloc(sizeof(struct pvs_parse_buf));
    if (parsed_buf != NULL) {
        strlen = text_len-8;  /* remove the 'utf-8:' and quotation marks */
        if (strlen > 254) {
            /* This is an error, so drop everything */
            printf("String too long.\n");
            free(parsed_buf);
            return NULL;
        }
        parsed_buf->buf = malloc(strlen + 1); /* add space for 0-termination */
        if (parsed_buf->buf != NULL) {
            strncpy((char*)(parsed_buf->buf), text+7, strlen);
            parsed_buf->buf[strlen] = '\0';
            /* printf("Converted to string: '%s'.\n", parsed_buf->buf); */
            parsed_buf->len = strlen+1;
        } else {
            free(parsed_buf);
            parsed_buf = NULL;
        }        
    }
    return parsed_buf;
}

/*@null@*/struct pvs_parse_buf * pvs_asciihex_add(char const *text, size_t text_len)
{
    size_t buf_sz;
    size_t ii;
    int res;
    struct pvs_parse_buf *parsed_buf;

    if (text_len < 4 || text_len % 2 != 0) {
        /* This should be impossible */
        return NULL;
    }
    parsed_buf = malloc(sizeof(struct pvs_parse_buf));
    if (parsed_buf != NULL) {
        buf_sz = (text_len-2)/2; /* remove the 0x prefix*/
        if (buf_sz > 255) {
            /* This is an error, so drop everything */
            printf("Hex value too long.\n");
            free(parsed_buf);
            return NULL;
        }
        parsed_buf->buf = malloc(buf_sz);
        if (parsed_buf->buf == NULL) {
            printf("Memory allocation error\n");
            free(parsed_buf);
            return NULL;
        }
        parsed_buf->len = buf_sz;
        for (ii = 0; ii < buf_sz; ii++) {
            res = sscanf(&text[2*ii+2], "%2hhx ", &(parsed_buf->buf)[ii]);
            if (res != 1) {
                parsed_buf->len = ii;
                printf("Hex scanning error at position %zu in %s\n", ii+2, text);
                break;
            }
        }
        if (parsed_buf->len == 0) {
            /* Nothing was converted, clean up and return NULL. */
            free(parsed_buf->buf);
            free(parsed_buf);
            parsed_buf = NULL;
        }
    }
    return parsed_buf;
}

/*@null@*/struct pvs_parse_buf * pvs_qr_dsk_add(uint16_t q0, uint16_t q1, uint16_t q2, uint16_t q3, 
                                                uint16_t q4, uint16_t q5, uint16_t q6, uint16_t q7)
{
    struct pvs_parse_buf *parsed_buf = malloc(sizeof(struct pvs_parse_buf));
    uint16_t tmp[8];

    if (parsed_buf != NULL) {
        parsed_buf->buf = malloc(8*sizeof(uint16_t));
        if (parsed_buf->buf != NULL) {
            tmp[0] = htons(q0);
            tmp[1] = htons(q1);
            tmp[2] = htons(q2);
            tmp[3] = htons(q3);
            tmp[4] = htons(q4);
            tmp[5] = htons(q5);
            tmp[6] = htons(q6);
            tmp[7] = htons(q7);
            memcpy(parsed_buf->buf, tmp, 16);
            parsed_buf->len = 16;
        } else {
            free(parsed_buf);
            parsed_buf = NULL;
        }
    }
    return parsed_buf;
}

void pvs_parse_buf_free(struct pvs_parse_buf *parsed_buf)
{
    if (parsed_buf) {
        free(parsed_buf->buf);
        free(parsed_buf);
    }
}
