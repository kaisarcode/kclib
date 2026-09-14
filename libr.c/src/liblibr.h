/**
 * libr.h - Summary of the functionality
 * Summary: Public API for the libr library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_LIBR_H
#define KC_LIBR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_libr kc_libr_t;

#define KC_LIBR_OK      0
#define KC_LIBR_ERROR  -1

/**
 * Opaque options handle.
 */
typedef void *kc_libr_options_t;

/**
 * Return default options for the library (caller owns, must free).
 * @return Opaque options handle, or NULL on failure.
 */
kc_libr_options_t kc_libr_options_default(void);

/**
 * Set an option value by key.
 * @param opts Options handle from kc_libr_options_default.
 * @param key Option key (e.g., "param").
 * @param value Option value, or NULL to clear.
 * @return KC_LIBR_OK on success, KC_LIBR_ERROR on unknown key.
 */
int kc_libr_options_set(kc_libr_options_t opts, const char *key, const char *value);

/**
 * Release resources owned by options handle.
 * @param opts Options handle from kc_libr_options_default.
 * @return None.
 */
void kc_libr_options_free(kc_libr_options_t opts);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_libr_version(void);

/**
 * Initialize a new libr context.
 * @param ctx_out Destination context pointer.
 * @param opts Opaque options handle from kc_libr_options_default.
 * @return KC_LIBR_OK on success, KC_LIBR_ERROR on failure.
 */
int kc_libr_open(void **ctx_out, kc_libr_options_t opts);

/**
 * Release a libr context.
 * @param ctx Context pointer.
 * @return KC_LIBR_OK.
 */
int kc_libr_close(void *ctx);

/**
 * Execute a core libr operation.
 * @param ctx Context pointer.
 * @param input Operation input.
 * @return Status code.
 */
int kc_libr_exec(void *ctx, const char *input);

/**
 * Request stop for a specific libr context.
 * @param ctx Context handle.
 * @return KC_LIBR_OK on success, KC_LIBR_ERROR on failure.
 */
int kc_libr_stop(void *ctx);

/**
 * Get the last error message from a context.
 * @param ctx Context pointer.
 * @return Error string, or NULL if no error.
 */
const char *kc_libr_get_error(const void *ctx);

#ifdef __cplusplus
}
#endif

#endif
