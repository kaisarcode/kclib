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
 * @param data_size Input size.
 * @return malloc'd base64 string, or NULL on failure.
 */
char *kc_b64_encode(const void *data, size_t data_size) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const unsigned char *in;
    size_t out_len;

    if (data == NULL) return NULL;

    in = (const unsigned char *)data;
    out_len = 4 * ((data_size + 2) / 3);
    char *out = (char *)malloc(out_len + 1);
    size_t i, j;

    if (out == NULL) return NULL;

    for (i = 0, j = 0; i < data_size;) {
        size_t start = i;
        unsigned int a = i < data_size ? in[i++] : 0;
        unsigned int b = i < data_size ? in[i++] : 0;
        unsigned int c = i < data_size ? in[i++] : 0;
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
 * Base64-decodes an RFC 4648 string into malloc'd binary data.
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
    size_t len;
    size_t out_len;
    size_t i;
    size_t j;
    size_t padding;
    unsigned char *out;

    if (!out_size) return NULL;
    *out_size = 0;
    if (!str) return NULL;

    len = strlen(str);
    if (len % 4 != 0) return NULL;

    padding = 0;
    if (len > 0 && str[len - 1] == '=') padding++;
    if (len > 1 && str[len - 2] == '=') padding++;
    if (padding > 2) return NULL;

    for (i = 0; i < len - padding; i++) {
        unsigned char ch = (unsigned char)str[i];
        if (ch == '=' || (tbl[ch] == 0 && ch != 'A')) {
            return NULL;
        }
    }
    for (; i < len; i++) {
        if (str[i] != '=') {
            return NULL;
        }
    }

    if (len > 0) {
        unsigned char a = tbl[(unsigned char)str[len - 4]];
        unsigned char b = tbl[(unsigned char)str[len - 3]];
        unsigned char cval = 0;

        if (padding == 2) {
            if ((b & 0x0F) != 0) {
                return NULL;
            }
        } else if (padding == 1) {
            cval = tbl[(unsigned char)str[len - 2]];
            if ((cval & 0x03) != 0) {
                return NULL;
            }
        }

        (void)a;
    }

    out_len = len / 4 * 3 - padding;
    out = (unsigned char *)malloc(out_len > 0 ? out_len : 1);
    if (!out) return NULL;

    j = 0;
    for (i = 0; i < len; i += 4) {
        unsigned int a = tbl[(unsigned char)str[i]];
        unsigned int b = tbl[(unsigned char)str[i + 1]];
        unsigned int cval = str[i + 2] == '=' ? 0U :
            tbl[(unsigned char)str[i + 2]];
        unsigned int d = str[i + 3] == '=' ? 0U :
            tbl[(unsigned char)str[i + 3]];
        unsigned int triple = (a << 18) | (b << 12) | (cval << 6) | d;

        if (j < out_len) out[j++] = (unsigned char)((triple >> 16) & 0xFF);
        if (j < out_len) out[j++] = (unsigned char)((triple >> 8) & 0xFF);
        if (j < out_len) out[j++] = (unsigned char)(triple & 0xFF);
    }

    *out_size = out_len;
    return out;
}

/**
 * Release memory returned by the b64 library.
 * @param ptr Library-owned allocation, or NULL.
 * @return No return value.
 */
void kc_b64_free(void *ptr) {
    free(ptr);
}
