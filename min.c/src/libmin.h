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

typedef struct kc_min kc_min_t;

#define KC_MIN_OK          0
#define KC_MIN_ERROR      -1

#define KC_MIN_MODE_NONE   0
#define KC_MIN_MODE_CSS    1
#define KC_MIN_MODE_JS     2
#define KC_MIN_MODE_HTML   3

/**
 * Initialize a new min context.
 * @param out Receives the context pointer on success.
 * @return KC_MIN_OK on success, or KC_MIN_ERROR on failure.
 */
int kc_min_open(kc_min_t **out);

/**
 * Release a min context.
 * @param ctx Context pointer.
 * @return None.
 */
void kc_min_close(kc_min_t *ctx);

/**
 * Select the minification mode for a context.
 * @param ctx Context pointer.
 * @param mode Minification mode.
 * @return KC_MIN_OK on success, or KC_MIN_ERROR on invalid input.
 */
int kc_min_set_mode(kc_min_t *ctx, int mode);

/**
 * Convert a mode name to a minification mode.
 * @param name Mode name.
 * @return Minification mode, or KC_MIN_MODE_NONE for invalid input.
 */
int kc_min_mode(const char *name);

/**
 * Execute minification using the selected context mode.
 * @param ctx Context pointer.
 * @param input Null-terminated input source.
 * @param output Receives the owned minified output string.
 * @return KC_MIN_OK on success, or KC_MIN_ERROR on failure.
 */
int kc_min_exec(kc_min_t *ctx, const char *input, char **output);

/**
 * Release a string allocated by the min library.
 * @param text Owned string returned by the API.
 * @return None.
 */
void kc_min_free(char *text);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_min_version(void);

#ifdef __cplusplus
}
#endif

#endif
