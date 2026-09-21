/**
 * liblng.h - Public API for lng language detection.
 * Summary: Public API for lng language detection.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_LNG_H
#define KC_LNG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KC_LNG_OK 0
#define KC_LNG_ERROR -1

typedef struct { const char *code; double score; } kc_lng_result_t;

/**
 * Detect languages for input text.
 * Summary: Detect languages for input text.
 *
 * Sanitizes/normalizes text internally; filters results below
 * threshold and bounds output count by limit. NULL or empty text
 * yields zero results (KC_LNG_OK with *out_count == 0).
 *
 * @param text Input text (NULL safe, empty yields no results).
 * @param threshold Minimum score in [0,1] to include;
 * results below are filtered.
 * @param limit Maximum number of results to return.
 * @param out_results Out: allocated array owned by caller,
 * released with kc_lng_free(). NULL on zero results or error.
 * @param out_count Out: number of results in *out_results.
 * @return KC_LNG_OK on success, KC_LNG_ERROR on invalid args
 * or allocation failure.
 *
 * Ownership: array returned via out_results is heap-allocated
 * and owned by the caller; release with kc_lng_free(). Each
 * kc_lng_result_t.code points to static library-owned storage;
 * caller must not free or modify it. score is a heuristic
 * ranking value, not a calibrated probability.
 */
int kc_lng_detect(const char *text, double threshold, size_t limit, kc_lng_result_t **out_results, size_t *out_count);

/**
 * Release memory allocated by kc_lng_detect.
 * Summary: Release memory allocated by kc_lng_detect.
 *
 * @param ptr Pointer returned via out_results (NULL safe, no-op on NULL).
 * @return None.
 */
void kc_lng_free(void *ptr);

/**
 * Returns the build version.
 * Summary: Returns the build version.
 *
 * @return Unix timestamp for the current build.
 */
uint64_t kc_lng_version(void);

#ifdef __cplusplus
}
#endif

#endif
