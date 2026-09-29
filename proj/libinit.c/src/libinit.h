/**
 * libinit.h - Persistent Startup Registration
 * Summary: Public API for name-keyed startup registrations.
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
 * Get one persistent startup registration by name.
 * The returned entry and its strings share one allocation released with
 * kc_init_free().
 * @param name Registration name.
 * @param out_entry Receives the allocated entry, or NULL when absent.
 * @return KC_INIT_OK on success, KC_INIT_NOT_FOUND when absent,
 *         or KC_INIT_ERROR on failure.
 */
int kc_init_get(
    const char *name,
    kc_init_entry_t **out_entry
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
 * Release memory returned by the init library.
 * @param ptr Allocation returned by the init library, or NULL.
 * @return None.
 */
void kc_init_free(void *ptr);

/**
 * Return the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_init_version(void);

#ifdef __cplusplus
}
#endif

#endif
