/**
 * min.h - Asset Minifier
 * Summary: Public API for the min library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_MIN_H
#define KC_MIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KC_MIN_MODE_CSS   1
#define KC_MIN_MODE_JS    2
#define KC_MIN_MODE_HTML  3

/**
 * Minify a null-terminated source string using the requested mode.
 * @param mode One of KC_MIN_MODE_CSS, KC_MIN_MODE_JS, or KC_MIN_MODE_HTML.
 * @param input Borrowed null-terminated input source.
 * @return Owned null-terminated minified string, or NULL on invalid input or
 * allocation failure.
 */
char *kc_min_minify(int mode, const char *input);

/**
 * Release memory allocated by the min library.
 * @param ptr Owned pointer returned by the API, or NULL.
 * @return None.
 */
void kc_min_free(void *ptr);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_min_version(void);

#ifdef __cplusplus
}
#endif

#endif
