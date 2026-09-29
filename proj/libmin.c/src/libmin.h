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

/**
 * Minify CSS conservatively.
 * @param input Borrowed null-terminated CSS source.
 * @return Owned null-terminated minified string, or NULL on invalid input or
 * allocation failure.
 */
char *kc_min_css(const char *input);

/**
 * Minify JavaScript conservatively.
 * @param input Borrowed null-terminated JavaScript source.
 * @return Owned null-terminated minified string, or NULL on invalid input or
 * allocation failure.
 */
char *kc_min_js(const char *input);

/**
 * Minify HTML conservatively.
 * @param input Borrowed null-terminated HTML source.
 * @return Owned null-terminated minified string, or NULL on invalid input or
 * allocation failure.
 */
char *kc_min_html(const char *input);

/**
 * Minify generic txt by collapsing whitespace runs to one space and trimming
 * leading and trailing whitespace.
 * @param input Borrowed null-terminated text source.
 * @return Owned null-terminated minified string, or NULL on invalid input or
 * allocation failure.
 */
char *kc_min_txt(const char *input);

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
