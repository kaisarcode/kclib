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

#define KC_INIT_OK      0
#define KC_INIT_ERROR  -1
#define KC_INIT_ESTOP  -3

/**
 * Configuration for one init context.
 * @param dir Metadata directory path.
 * @param backend Backend name or NULL for auto detection.
 * @return No return value.
 */
typedef struct {
    char *dir;
    char *backend;
} kc_init_options_t;

typedef void (*kc_init_list_cb)(const char *key, const char *user, const char *cmd, void *userdata);

/**
 * Create default init options.
 * @return Default-initialized options.
 */
kc_init_options_t kc_init_options_default(void);

/**
 * Load init options from environment variables.
 * @param opts Options to update.
 * @return None.
 */
void kc_init_options_load_env(kc_init_options_t *opts);

/**
 * Free dynamically allocated resources within init options.
 * @param opts Options to clean up.
 * @return None.
 */
void kc_init_options_free(kc_init_options_t *opts);

/**
 * Initialize a new init context.
 * @param options Init context options.
 * @return Context pointer or NULL on failure.
 */
kc_init_t *kc_init_open(const kc_init_options_t *options);

/**
 * Release an init context.
 * @param ctx Context pointer.
 * @return None.
 */
void kc_init_close(kc_init_t *ctx);

/**
 * Request stop for a specific init context.
 * @param ctx Context handle.
 * @return KC_INIT_OK on success, KC_INIT_ERROR on failure.
 */
int kc_init_stop(kc_init_t *ctx);

/**
 * Return the resolved metadata directory for an init context.
 * @param ctx Context pointer.
 * @return Metadata directory path, or NULL on invalid input.
 */
const char *kc_init_path(kc_init_t *ctx);

/**
 * Register or replace a named startup command.
 * @param ctx Context pointer.
 * @param key Registration key name.
 * @param cmd Shell command string to run at startup.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on failure.
 */
int kc_init_update(kc_init_t *ctx, const char *key, const char *cmd);

/**
 * Execute the registered command for a key immediately.
 * @param ctx Context pointer.
 * @param key Registration key name.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on failure.
 */
int kc_init_exec(kc_init_t *ctx, const char *key);

/**
 * List registered startup entries.
 * Calls cb(key, user, cmd, userdata) per entry.
 * @param ctx Context pointer.
 * @param key Optional registration key name, or NULL for all.
 * @param cb Callback invoked per entry, or NULL.
 * @param userdata Opaque pointer passed to cb.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on failure.
 */
int kc_init_list(kc_init_t *ctx, const char *key, kc_init_list_cb cb, void *userdata);

/**
 * Remove a named startup registration.
 * @param ctx Context pointer.
 * @param key Registration key name.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on failure.
 */
int kc_init_delete(kc_init_t *ctx, const char *key);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_init_version(void);

/**
 * Return the last error message from an init context.
 * @param ctx Context pointer.
 * @return Static error string, or NULL if no error.
 */
const char *kc_init_error(kc_init_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
