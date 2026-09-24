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
    const char *cmd;
} kc_init_options_t;

typedef struct {
    const char *name;
    const char *user;
    const char *cmd;
} kc_init_entry_t;

#define KC_INIT_OK          0
#define KC_INIT_NOT_FOUND   1
#define KC_INIT_ERROR      -1

/**
 * Create or replace one persistent startup registration.
 * The operating-system startup backend is selected internally.
 * @param name Registration name.
 * @param options Startup registration options.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on failure.
 */
int kc_init_create(
    const char *name,
    const kc_init_options_t *options
);

/**
 * Open one persistent startup registration.
 * @param out Output location for the caller-owned handle.
 * @param name Registration name.
 * @return KC_INIT_OK on success, KC_INIT_NOT_FOUND when absent,
 *         or KC_INIT_ERROR on failure.
 */
int kc_init_open(
    kc_init_t **out,
    const char *name
);

/**
 * List startup registrations in the active user namespace.
 * The returned array and all strings inside it share one allocation released
 * with kc_init_free().
 * @param out_entries Receives the allocated entry array, or NULL when empty.
 * @param out_count Receives the number of entries.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on failure.
 */
int kc_init_list(
    kc_init_entry_t **out_entries,
    size_t *out_count
);

/**
 * Remove one persistent startup registration.
 * Missing registrations are a successful no-op.
 * @param name Registration name.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on failure.
 */
int kc_init_delete(const char *name);

/**
 * Replace the command of one persistent startup registration.
 * @param init Startup entry handle.
 * @param cmd New one-line startup command.
 * @return KC_INIT_OK on success, or KC_INIT_ERROR on failure.
 */
int kc_init_set_cmd(
    kc_init_t *init,
    const char *cmd
);

/**
 * Return the command of one opened startup registration.
 * @param init Startup entry handle.
 * @return Borrowed command string, or NULL on invalid input.
 */
const char *kc_init_get_cmd(const kc_init_t *init);

/**
 * Return the recorded user of one opened startup registration.
 * @param init Startup entry handle.
 * @return Borrowed user string, or NULL on invalid input.
 */
const char *kc_init_get_user(const kc_init_t *init);

/**
 * Return the last handle error message.
 * @param init Startup entry handle.
 * @return Borrowed error text, or NULL when unset.
 */
const char *kc_init_error(const kc_init_t *init);

/**
 * Release memory returned by the init library.
 * @param ptr Allocation returned by the init library, or NULL.
 * @return None.
 */
void kc_init_free(void *ptr);

/**
 * Release one local startup entry handle.
 * @param init Startup entry handle, or NULL.
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
