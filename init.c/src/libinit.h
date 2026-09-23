/**
 * libinit.h - Persistent Startup Registration
 * Summary: Public API for the init library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_INIT_H
#define KC_INIT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_init kc_init_t;

typedef struct {
    const char *dir;
    const char *backend;
} kc_init_options_t;

typedef struct {
    const char *key;
    const char *user;
    const char *cmd;
} kc_init_entry_t;

#define KC_INIT_OK          0
#define KC_INIT_NOT_FOUND   1
#define KC_INIT_ERROR      -1

/**
 * Initialize a startup registry context.
 * NULL options use the default metadata directory and automatic backend
 * detection. NULL option fields select their individual defaults.
 * @param out Output location for the caller-owned context.
 * @param options Startup registry options, or NULL.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on failure.
 */
int kc_init_open(
    kc_init_t **out,
    const kc_init_options_t *options
);

/**
 * Register or replace a named startup command.
 * @param init Startup registry context.
 * @param key Registration key name.
 * @param cmd One-line shell command to run at startup.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on failure.
 */
int kc_init_set(
    kc_init_t *init,
    const char *key,
    const char *cmd
);

/**
 * Execute a registered command immediately.
 * @param init Startup registry context.
 * @param key Registration key name.
 * @return KC_INIT_OK on success, KC_INIT_NOT_FOUND when absent,
 *         or KC_INIT_ERROR on failure.
 */
int kc_init_exec(
    kc_init_t *init,
    const char *key
);

/**
 * List startup registrations.
 * A NULL key returns all entries. A missing specific key succeeds with an
 * empty result. The returned array and all strings inside it share one
 * allocation released with kc_init_free().
 * @param init Startup registry context.
 * @param key Optional registration key name, or NULL for all.
 * @param out_entries Receives the allocated entry array, or NULL when empty.
 * @param out_count Receives the number of entries.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on failure.
 */
int kc_init_list(
    kc_init_t *init,
    const char *key,
    kc_init_entry_t **out_entries,
    size_t *out_count
);

/**
 * Remove a named startup registration.
 * Missing registrations remain a successful no-op.
 * @param init Startup registry context.
 * @param key Registration key name.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on failure.
 */
int kc_init_delete(
    kc_init_t *init,
    const char *key
);

/**
 * Return the resolved metadata directory.
 * @param init Startup registry context.
 * @return Borrowed metadata directory path, or NULL on invalid input.
 */
const char *kc_init_path(
    const kc_init_t *init
);

/**
 * Return the last context error message.
 * @param init Startup registry context.
 * @return Borrowed error text, or NULL when no error text is set.
 */
const char *kc_init_error(
    const kc_init_t *init
);

/**
 * Release memory returned by the init library.
 * @param ptr Allocation returned by the init library, or NULL.
 * @return None.
 */
void kc_init_free(void *ptr);

/**
 * Release a startup registry context.
 * @param init Startup registry context, or NULL.
 * @return None.
 */
void kc_init_close(kc_init_t *init);

/**
 * Return the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_init_version(void);

#ifdef __cplusplus
}
#endif

#endif
