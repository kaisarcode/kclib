/**
 * init.h - Persistent Startup Registration
 * Summary: Public API for the init library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_INIT_H
#define KC_INIT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_init kc_init_t;
typedef struct kc_init_options kc_init_options_t;

#define KC_INIT_OK      0
#define KC_INIT_ERROR  -1

/**
 * Handle one synchronously listed startup entry.
 * The callback is invoked only during kc_init_list() and is not retained.
 * The key, user, and cmd strings are borrowed and valid only during the
 * callback invocation. Copy them if a longer lifetime is required. Userdata
 * is passed through unchanged and is not retained.
 * @param key Borrowed registration key name.
 * @param user Borrowed registration user name.
 * @param cmd Borrowed shell command string.
 * @param userdata Opaque caller-owned pointer.
 * @return None.
 */
typedef void (*kc_init_list_cb)(
    const char *key,
    const char *user,
    const char *cmd,
    void *userdata
);

/**
 * Create default init options.
 * @return Caller-owned default options, or NULL on allocation failure.
 */
kc_init_options_t *kc_init_options_default(void);

/**
 * Set one init option.
 * Supported keys are "dir" and "backend". A NULL value resets that option
 * to its default.
 * @param opts Options to update.
 * @param key Option key.
 * @param value Option value, or NULL to reset it.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on failure.
 */
int kc_init_options_set(
    kc_init_options_t *opts,
    const char *key,
    const char *value
);

/**
 * Free init options.
 * @param opts Options to free, or NULL.
 * @return None.
 */
void kc_init_options_free(kc_init_options_t *opts);

/**
 * Initialize a new init context.
 * @param out Output location for the caller-owned context.
 * @param opts Init context options, or NULL for defaults.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on failure.
 */
int kc_init_open(
    kc_init_t **out,
    const kc_init_options_t *opts
);

/**
 * Release an init context.
 * @param ctx Context pointer.
 * @return None.
 */
void kc_init_close(kc_init_t *ctx);

/**
 * Return the resolved metadata directory for an init context.
 * The returned string is borrowed context-owned storage. The caller must not
 * free or modify it, and it remains valid until the context is closed.
 * @param ctx Context pointer.
 * @return Borrowed metadata directory path, or NULL on invalid input.
 */
const char *kc_init_path(const kc_init_t *ctx);

/**
 * Register or replace a named startup command.
 * @param ctx Context pointer.
 * @param key Registration key name.
 * @param cmd Shell command string to run at startup.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on failure.
 */
int kc_init_update(
    kc_init_t *ctx,
    const char *key,
    const char *cmd
);

/**
 * Execute the registered command for a key immediately.
 * @param ctx Context pointer.
 * @param key Registration key name.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on failure.
 */
int kc_init_exec(
    kc_init_t *ctx,
    const char *key
);

/**
 * List registered startup entries.
 * Calls cb(key, user, cmd, userdata) synchronously per entry. The callback and
 * userdata are not retained. Callback strings are borrowed and valid only
 * during each invocation.
 * @param ctx Context pointer.
 * @param key Optional registration key name, or NULL for all.
 * @param cb Callback invoked per entry, or NULL.
 * @param userdata Opaque pointer passed to cb.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on failure.
 */
int kc_init_list(
    kc_init_t *ctx,
    const char *key,
    kc_init_list_cb cb,
    void *userdata
);

/**
 * Remove a named startup registration.
 * @param ctx Context pointer.
 * @param key Registration key name.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on failure.
 */
int kc_init_delete(
    kc_init_t *ctx,
    const char *key
);

/**
 * Return the last error message from an init context.
 * The returned string is borrowed context-owned storage. The caller must not
 * free or modify it, and it remains valid until the context is closed. A later
 * operation on the same context may replace its contents.
 * @param ctx Context pointer.
 * @return Borrowed error string, or NULL if there is no current error text.
 */
const char *kc_init_get_error(const kc_init_t *ctx);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_init_version(void);

#ifdef __cplusplus
}
#endif

#endif
