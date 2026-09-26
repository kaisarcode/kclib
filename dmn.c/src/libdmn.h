/**
 * libdmn.h - Local Daemon Manager
 * Summary: Public API for the dmn library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_DMN_H
#define KC_DMN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_dmn kc_dmn_t;

typedef struct {
    const char *cmd;
    const void *eot;
    size_t eot_size;
} kc_dmn_options_t;

typedef struct {
    const char *name;
} kc_dmn_entry_t;

#define KC_DMN_OK          0
#define KC_DMN_NOT_FOUND   1
#define KC_DMN_ERROR      -1

/**
 * Create or replace a named daemon.
 * @param name Daemon name.
 * @param options Creation options.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_create(
    const char *name,
    const kc_dmn_options_t *options
);

/**
 * Open a local handle bound to one daemon name.
 * @param out Output daemon handle.
 * @param name Daemon name.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_open(
    kc_dmn_t **out,
    const char *name
);

/**
 * List daemon identities in the active runtime directory.
 * Returned entries share one allocation released with kc_dmn_free().
 * @param out_entries Output entry array.
 * @param out_count Output entry count.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_list(
    kc_dmn_entry_t **out_entries,
    size_t *out_count
);

/**
 * Delete one named daemon.
 * @param name Daemon name.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
int kc_dmn_delete(
    const char *name
);

/**
 * Replace the command of an opened daemon.
 * @param dmn Daemon handle.
 * @param cmd Daemon command.
 * @return KC_DMN_OK, KC_DMN_NOT_FOUND, or KC_DMN_ERROR.
 */
int kc_dmn_set_cmd(
    kc_dmn_t *dmn,
    const char *cmd
);

/**
 * Return the configured daemon command.
 * @param dmn Daemon handle.
 * @return Borrowed command string, or NULL when unavailable.
 */
const char *kc_dmn_get_cmd(
    const kc_dmn_t *dmn
);

/**
 * Replace the daemon EOT marker.
 * Passing NULL with an empty size restores the default EOT marker.
 * @param dmn Daemon handle.
 * @param eot EOT bytes, or NULL for the default marker.
 * @param eot_size EOT byte count.
 * @return KC_DMN_OK, KC_DMN_NOT_FOUND, or KC_DMN_ERROR.
 */
int kc_dmn_set_eot(
    kc_dmn_t *dmn,
    const void *eot,
    size_t eot_size
);

/**
 * Perform one complete daemon data exchange.
 * The returned response excludes the configured EOT marker.
 * @param dmn Daemon handle.
 * @param data Request bytes.
 * @param data_size Request byte count.
 * @param out_data Output response allocation.
 * @param out_size Output response byte count.
 * @return KC_DMN_OK, KC_DMN_NOT_FOUND, or KC_DMN_ERROR.
 */
int kc_dmn_send_data(
    kc_dmn_t *dmn,
    const void *data,
    size_t data_size,
    void **out_data,
    size_t *out_size
);

/**
 * Send one platform signal to an opened daemon.
 * @param dmn Daemon handle.
 * @param signal Signal value.
 * @return KC_DMN_OK, KC_DMN_NOT_FOUND, or KC_DMN_ERROR.
 */
int kc_dmn_send_signal(
    kc_dmn_t *dmn,
    int signal
);

/**
 * Release memory returned by dmn.
 * @param ptr Owned allocation, or NULL.
 * @return None.
 */
void kc_dmn_free(void *ptr);

/**
 * Close and release one local daemon handle.
 * This does not delete the registered daemon.
 * @param dmn Daemon handle, or NULL.
 * @return None.
 */
void kc_dmn_close(kc_dmn_t *dmn);

/**
 * Return the generated build version.
 * @return Build version value.
 */
uint64_t kc_dmn_version(void);

#ifdef __cplusplus
}
#endif

#endif
