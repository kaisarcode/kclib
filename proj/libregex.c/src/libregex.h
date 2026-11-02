/**
 * libregex.h - Portable regular expression matching.
 * Summary: Compiles reusable patterns and returns byte-range captures.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef KC_REGEX_H
#define KC_REGEX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KC_REGEX_OK 0
#define KC_REGEX_NOMATCH 1
#define KC_REGEX_EINVAL -1
#define KC_REGEX_ENOMEM -2
#define KC_REGEX_EPATTERN -3
#define KC_REGEX_ELIMIT -4
#define KC_REGEX_CAPTURES 16

typedef struct kc_regex kc_regex_t;

typedef struct {
    size_t start;
    size_t end;
} kc_regex_capture_t;

/**
 * Compile one portable regular expression for repeated matching.
 * Supported syntax: literals, escaped bytes, dot, character classes,
 * capture groups, alternatives, *, + and ?. Input is copied as compiled
 * state. Matching is byte-oriented, not UTF-8 code-point-aware.
 * @param out Receives the owned compiled regex.
 * @param pattern Borrowed NUL-terminated expression.
 * @return KC_REGEX_OK or a negative status.
 */
int kc_regex_open(kc_regex_t **out, const char *pattern);

/**
 * Match the entire input and report the complete match plus group spans.
 * Group zero is the entire match; subsequent groups follow parenthesis order.
 * Unmatched optional captures have start and end set to SIZE_MAX. Results are
 * byte offsets into the borrowed text. A null result buffer with zero capacity
 * is allowed; count still reports the needed slots on a successful match.
 * @param regex Compiled expression.
 * @param text Borrowed NUL-terminated input.
 * @param captures Optional output capture array.
 * @param capacity Number of available capture slots.
 * @param count Optional number of capture slots, including group zero.
 * @return KC_REGEX_OK, KC_REGEX_NOMATCH or a negative status.
 */
int kc_regex_match(const kc_regex_t *regex, const char *text,
    kc_regex_capture_t *captures, size_t capacity, size_t *count);

/**
 * Find the earliest match at or after a byte position.
 * Search results use the same capture convention as kc_regex_match().
 * @param regex Compiled expression.
 * @param text Borrowed NUL-terminated input.
 * @param offset Starting byte position; must not exceed strlen(text).
 * @param captures Optional output capture slots.
 * @param capacity Number of slots.
 * @param count Optional number of groups including group zero.
 * @return KC_REGEX_OK, KC_REGEX_NOMATCH or a negative status.
 */
int kc_regex_search(const kc_regex_t *regex, const char *text, size_t offset,
    kc_regex_capture_t *captures, size_t capacity, size_t *count);

/**
 * Release a compiled regular expression.
 * @param regex Expression or NULL.
 * @return None.
 */
void kc_regex_close(kc_regex_t *regex);

/**
 * Describe a regex result status.
 * @param status Public result code.
 * @return Static description.
 */
const char *kc_regex_strerror(int status);

/**
 * Return the generated library build timestamp.
 * @return Build timestamp or zero.
 */
uint64_t kc_regex_version(void);

#ifdef __cplusplus
}
#endif
#endif
