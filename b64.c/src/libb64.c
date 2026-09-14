/**
 * libb64.c - Base64 encode and decode
 * Summary: Core implementation for the b64 library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libb64.h"
#include <stdlib.h>
#include <string.h>

#ifndef KC_B64_BUILD_VERSION
#define KC_B64_BUILD_VERSION 0
#endif

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_b64_version(void) {
    return (uint64_t)KC_B64_BUILD_VERSION;
}

/**
 * Base64-encodes binary data into a malloc'd string.
 * @param data Input data.
 * @param size Input size.
 * @return malloc'd base64 string, or NULL on failure.
 */
char *kc_b64_encode(const void *data, size_t size) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const unsigned char *in;
    size_t out_len;

    if (data == NULL) return NULL;

    in = (const unsigned char *)data;
    out_len = 4 * ((size + 2) / 3);
    char *out = (char *)malloc(out_len + 1);
    size_t i, j;

    if (out == NULL) return NULL;

    for (i = 0, j = 0; i < size;) {
        size_t start = i;
        unsigned int a = i < size ? in[i++] : 0;
        unsigned int b = i < size ? in[i++] : 0;
        unsigned int c = i < size ? in[i++] : 0;
        unsigned int triple = (a << 16) | (b << 8) | c;
        size_t n = i - start;

        out[j++] = tbl[(triple >> 18) & 0x3F];
        out[j++] = tbl[(triple >> 12) & 0x3F];
        out[j++] = n < 2 ? '=' : tbl[(triple >> 6) & 0x3F];
        out[j++] = n < 3 ? '=' : tbl[triple & 0x3F];
    }
    out[j] = '\0';
    return out;
}

/**
 * Base64-decodes a string into malloc'd binary data.
 * @param str Base64 string.
 * @param out_size Receives the decoded size.
 * @return malloc'd data, or NULL on failure.
 */
void *kc_b64_decode(const char *str, size_t *out_size) {
    static const unsigned char tbl[256] = {
        ['A']=0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,
        ['a']=26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,
        ['0']=52,53,54,55,56,57,58,59,60,61,
        ['+']=62,
        ['/']=63
    };
    size_t len, out_len, i, j;
    unsigned char *out;
    unsigned int accum;
    int bits;

    if (str == NULL || out_size == NULL) return NULL;
    len = strlen(str);
    if (len % 4 != 0) return NULL;

    out_len = len / 4 * 3;
    if (len >= 2 && str[len - 1] == '=') out_len--;
    if (len >= 4 && str[len - 2] == '=') out_len--;

    out = (unsigned char *)malloc(out_len);
    if (out == NULL) return NULL;

    accum = 0;
    bits = 0;
    j = 0;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c == '=') break;
        if (tbl[c] == 0 && c != 'A') { free(out); return NULL; }
        accum = (accum << 6) | tbl[c];
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[j++] = (unsigned char)((accum >> bits) & 0xFF);
        }
    }
    *out_size = j;
    return out;
}
