/**
 * libleven.c - Unicode Levenshtein distance.
 * Summary: Bounded edit distance with linear auxiliary memory.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libleven.h"

#include <stdint.h>
#include <stdlib.h>

#ifndef KC_LEVEN_BUILD_VERSION
#define KC_LEVEN_BUILD_VERSION 0
#endif

/**
 * Decode one UTF-8 scalar value and advance an input cursor.
 * Reject overlong encodings, surrogates, and values above U+10FFFF.
 * @param cursor Address of the current input byte pointer.
 * @param value Receives the decoded scalar value.
 * @return KC_LEVEN_OK, KC_LEVEN_EUTF8, or KC_LEVEN_EINVAL at NUL.
 */
static int kc_leven_next(const unsigned char **cursor, uint32_t *value)
{
    const unsigned char *p = *cursor;
    uint32_t cp;
    size_t length;

    if (p[0] == 0) return KC_LEVEN_EINVAL;
    if (p[0] < 0x80) {
        *value = p[0];
        *cursor = p + 1;
        return KC_LEVEN_OK;
    }
    if (p[0] >= 0xC2 && p[0] <= 0xDF) {
        cp = p[0] & 0x1F;
        length = 2;
    } else if (p[0] >= 0xE0 && p[0] <= 0xEF) {
        cp = p[0] & 0x0F;
        length = 3;
    } else if (p[0] >= 0xF0 && p[0] <= 0xF4) {
        cp = p[0] & 0x07;
        length = 4;
    } else {
        return KC_LEVEN_EUTF8;
    }
    for (size_t i = 1; i < length; i++) {
        if (p[i] == 0 || (p[i] & 0xC0) != 0x80)
            return KC_LEVEN_EUTF8;
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    if ((length == 2 && cp < 0x80) ||
        (length == 3 && cp < 0x800) ||
        (length == 4 && cp < 0x10000) ||
        (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)
        return KC_LEVEN_EUTF8;

    *value = cp;
    *cursor = p + length;
    return KC_LEVEN_OK;
}

/**
 * Validate a UTF-8 string and count its Unicode scalar values.
 * @param input Borrowed NUL-terminated UTF-8 input.
 * @param count Receives the number of scalar values.
 * @return KC_LEVEN_OK, KC_LEVEN_EUTF8, or KC_LEVEN_ENOMEM on overflow.
 */
static int kc_leven_count(const char *input, size_t *count)
{
    const unsigned char *cursor = (const unsigned char *)input;
    uint32_t cp;
    int status;

    *count = 0;
    while (*cursor != 0) {
        status = kc_leven_next(&cursor, &cp);
        if (status != KC_LEVEN_OK) return status;
        if (*count == SIZE_MAX) return KC_LEVEN_ENOMEM;
        (*count)++;
    }
    return KC_LEVEN_OK;
}

/**
 * Compute bounded edit distance using two rows and one short operand vector.
 * @param left First borrowed UTF-8 string.
 * @param right Second borrowed UTF-8 string.
 * @param max_distance Inclusive maximum, or NULL for unbounded calculation.
 * @param out_distance Receives the distance or maximum-plus-one sentinel.
 * @return KC_LEVEN_OK, KC_LEVEN_ELIMIT, or an error status.
 */
int kc_leven_distance(
    const char *left,
    const char *right,
    const size_t *max_distance,
    size_t *out_distance
)
{
    const char *short_text, *long_text;
    const unsigned char *cursor;
    uint32_t *short_values = NULL;
    size_t *rows = NULL, *previous, *current;
    size_t left_count, right_count, short_count, long_count;
    size_t maximum = max_distance != NULL ? *max_distance : SIZE_MAX;
    int bounded = max_distance != NULL && maximum < SIZE_MAX;
    int status;

    if (out_distance != NULL) *out_distance = 0;
    if (left == NULL || right == NULL || out_distance == NULL)
        return KC_LEVEN_EINVAL;

    status = kc_leven_count(left, &left_count);
    if (status != KC_LEVEN_OK) return status;
    status = kc_leven_count(right, &right_count);
    if (status != KC_LEVEN_OK) return status;

    if (left_count <= right_count) {
        short_text = left;
        long_text = right;
        short_count = left_count;
        long_count = right_count;
    } else {
        short_text = right;
        long_text = left;
        short_count = right_count;
        long_count = left_count;
    }
    if (bounded && long_count - short_count > maximum) {
        *out_distance = maximum + 1;
        return KC_LEVEN_ELIMIT;
    }
    if (short_count == 0) {
        *out_distance = long_count;
        return bounded && long_count > maximum ?
            KC_LEVEN_ELIMIT : KC_LEVEN_OK;
    }
    if (short_count > SIZE_MAX / sizeof(uint32_t) ||
        short_count == SIZE_MAX ||
        short_count + 1 > SIZE_MAX / (2 * sizeof(size_t)))
        return KC_LEVEN_ENOMEM;

    short_values = (uint32_t *)malloc(short_count * sizeof(*short_values));
    rows = (size_t *)malloc(2 * (short_count + 1) * sizeof(*rows));
    if (short_values == NULL || rows == NULL) {
        free(short_values);
        free(rows);
        return KC_LEVEN_ENOMEM;
    }
    cursor = (const unsigned char *)short_text;
    for (size_t j = 0; j < short_count; j++) {
        status = kc_leven_next(&cursor, &short_values[j]);
        if (status != KC_LEVEN_OK) {
            free(short_values);
            free(rows);
            return status;
        }
    }
    previous = rows;
    current = rows + short_count + 1;
    for (size_t j = 0; j <= short_count; j++) previous[j] = j;

    cursor = (const unsigned char *)long_text;
    for (size_t i = 1; i <= long_count; i++) {
        uint32_t value;
        size_t lowest;
        size_t *swap;

        status = kc_leven_next(&cursor, &value);
        if (status != KC_LEVEN_OK) {
            free(short_values);
            free(rows);
            return status;
        }
        current[0] = i;
        lowest = i;
        for (size_t j = 1; j <= short_count; j++) {
            size_t removal = previous[j] + 1;
            size_t insertion = current[j - 1] + 1;
            size_t change = previous[j - 1] +
                (short_values[j - 1] != value);
            size_t best = removal < insertion ? removal : insertion;

            current[j] = best < change ? best : change;
            if (current[j] < lowest) lowest = current[j];
        }
        if (bounded && lowest > maximum) {
            *out_distance = maximum + 1;
            free(short_values);
            free(rows);
            return KC_LEVEN_ELIMIT;
        }
        swap = previous;
        previous = current;
        current = swap;
    }
    *out_distance = previous[short_count];
    status = bounded && *out_distance > maximum ?
        KC_LEVEN_ELIMIT : KC_LEVEN_OK;
    if (status == KC_LEVEN_ELIMIT) *out_distance = maximum + 1;
    free(short_values);
    free(rows);
    return status;
}

/**
 * Describe one public result status.
 * @param status Public status code.
 * @return Static English description.
 */
const char *kc_leven_strerror(int status)
{
    switch (status) {
        case KC_LEVEN_OK: return "success";
        case KC_LEVEN_EINVAL: return "invalid argument";
        case KC_LEVEN_EUTF8: return "invalid UTF-8";
        case KC_LEVEN_ENOMEM: return "out of memory";
        case KC_LEVEN_ELIMIT: return "distance exceeds maximum";
        default: return "unknown error";
    }
}

/**
 * Return the generated build timestamp.
 * @return Unix timestamp, or zero when not specified.
 */
uint64_t kc_leven_version(void)
{
    return (uint64_t)KC_LEVEN_BUILD_VERSION;
}
