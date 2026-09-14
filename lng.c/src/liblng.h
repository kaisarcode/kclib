/**
 * lng.h - Summary of the functionality
 * Summary: Public API for lng language detection.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_LNG_H
#define KC_LNG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KC_LNG_OK      0
#define KC_LNG_EINIT   1
#define KC_LNG_EINVAL  2
#define KC_LNG_ERROR  -1

typedef struct kc_lng kc_lng_t;

typedef struct {
    double threshold;
    int limit;
} kc_lng_options_t;

/**
 * Builds default options with threshold 0.001 and limit 1.
 * @return Default-initialized options.
 */
kc_lng_options_t kc_lng_options_default(void);

/**
 * Overrides options fields from KC_LNG_* environment variables.
 * @return None.
 */
void kc_lng_options_load_env(kc_lng_options_t *opts);

/**
 * Releases resources held by options.
 * @return None.
 */
void kc_lng_options_free(kc_lng_options_t *opts);

/**
 * Creates a new language detection context.
 * @param out Destination for the context pointer.
 * @param opts Configuration options.
 * @return KC_LNG_OK on success, or KC_LNG_ERROR on failure.
 */
int kc_lng_open(kc_lng_t **out, const kc_lng_options_t *opts);

/**
 * Releases a language detection context.
 * @param ctx Context pointer (NULL safe).
 * @return KC_LNG_OK.
 */
int kc_lng_close(kc_lng_t *ctx);

/**
 * Requests clean termination for one language detection context.
 * @param ctx Context pointer.
 * @return KC_LNG_OK on success, or KC_LNG_ERROR on failure.
 */
int kc_lng_stop(kc_lng_t *ctx);

/**
 * Returns whether stop has been requested on the context.
 * @param ctx Context pointer.
 * @return 1 when stop was requested, otherwise 0.
 */
int kc_lng_stop_requested(kc_lng_t *ctx);

typedef struct {
    const char *code;
    double score;
} kc_lng_result_t;

/**
 * Initializes internal language profiles via once-control.
 * @return KC_LNG_OK on success, or KC_LNG_EINIT on failure.
 */
int kc_lng_init(void);

/**
 * Detects the best matching language for input text.
 * @return Best matching language code, or NULL when unavailable.
 */
const char *kc_lng_detect(const char *text);

/**
 * Detects and ranks language matches for the given text.
 * @return Number of results written to out.
 */
int kc_lng_detect_top(
    const char *text,
    kc_lng_result_t *out,
    int max_results,
    double threshold
);

/**
 * Detects language using a specific context and observes stop state.
 * @param ctx Context pointer.
 * @param text Input text to analyze.
 * @param out Caller-provided output buffer.
 * @param max_results Maximum number of results.
 * @param threshold Minimum score threshold.
 * @return Number of results written to out.
 */
int kc_lng_detect_ctx(kc_lng_t *ctx, const char *text, kc_lng_result_t *out, int max_results, double threshold);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_lng_version(void);

#ifdef __cplusplus
}
#endif

#endif
