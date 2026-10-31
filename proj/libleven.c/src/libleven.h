/**
 * libleven.h - Levenshtein distance.
 * Summary: Public API for bounded Unicode edit distance.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef KC_LEVEN_H
#define KC_LEVEN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KC_LEVEN_OK 0
#define KC_LEVEN_EINVAL -1
#define KC_LEVEN_EUTF8 -2
#define KC_LEVEN_ENOMEM -3
#define KC_LEVEN_ELIMIT -4

/**
 * Calculate Levenshtein distance between valid UTF-8 strings.
 * The cost of insertion, deletion, or substitution of one Unicode code point
 * is one. No normalization, case folding, or locale rules are applied.
 * An omitted max_distance computes the exact distance. If the threshold is
 * exceeded, returns KC_LEVEN_ELIMIT and writes max_distance + 1; this is not
 * an exact distance. The function allocates O(min(codepoint lengths)) memory.
 * @param left First borrowed NUL-terminated UTF-8 string.
 * @param right Second borrowed NUL-terminated UTF-8 string.
 * @param max_distance Optional inclusive maximum edit distance.
 * @param out_distance Receives exact distance or limit-exceeded sentinel.
 * @return KC_LEVEN_OK, or a negative error/status code.
 */
int kc_leven_distance(
    const char *left,
    const char *right,
    const size_t *max_distance,
    size_t *out_distance
);

/**
 * Return a static description of a public status code.
 * @param status Status returned by kc_leven_distance().
 * @return Static description string.
 */
const char *kc_leven_strerror(int status);

/**
 * Return the generated build timestamp.
 * @return Unix timestamp, or zero if unspecified.
 */
uint64_t kc_leven_version(void);

#ifdef __cplusplus
}
#endif

#endif
