/**
 * wch.h - Resident File Watcher
 * Summary: Public API for named resident filesystem watchers.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_WCH_H
#define KC_WCH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_wch kc_wch_t;

typedef struct {
    const char *path;
    const char *cmd;
    int recursive;
} kc_wch_options_t;

typedef struct {
    const char *name;
    const char *path;
    const char *cmd;
    int recursive;
    int running;
} kc_wch_entry_t;

typedef void (*kc_wch_handler_t)(
    const char *path,
    void *userdata
);

#define KC_WCH_OK          0
#define KC_WCH_NOT_FOUND   1
#define KC_WCH_ERROR      -1

/**
 * Create or replace one named resident watcher.
 * @param name Watcher name.
 * @param options Creation options.
 * @return KC_WCH_OK on success, or KC_WCH_ERROR on failure.
 */
int kc_wch_create(
    const char *name,
    const kc_wch_options_t *options
);

/**
 * Open one local handle bound to a watcher name.
 * @param out Output watcher handle.
 * @param name Watcher name.
 * @return KC_WCH_OK on success, or KC_WCH_ERROR on failure.
 */
int kc_wch_open(
    kc_wch_t **out,
    const char *name
);

/**
 * List registered watchers in the active runtime directory.
 * Returned entries share one allocation released with kc_wch_free().
 * @param out_entries Output entry array.
 * @param out_count Output entry count.
 * @return KC_WCH_OK on success, or KC_WCH_ERROR on failure.
 */
int kc_wch_list(
    kc_wch_entry_t **out_entries,
    size_t *out_count
);

/**
 * Delete one named resident watcher.
 * @param name Watcher name.
 * @return KC_WCH_OK on success, or KC_WCH_ERROR on failure.
 */
int kc_wch_delete(
    const char *name
);

/**
 * Replace the watched path.
 * @param w Watcher handle.
 * @param path File or directory path.
 * @return KC_WCH_OK, KC_WCH_NOT_FOUND, or KC_WCH_ERROR.
 */
int kc_wch_set_path(
    kc_wch_t *w,
    const char *path
);

/**
 * Return the configured watched path.
 * @param w Watcher handle.
 * @return Borrowed path string, or NULL when unavailable.
 */
const char *kc_wch_get_path(
    const kc_wch_t *w
);

/**
 * Replace the persistent event command.
 * @param w Watcher handle.
 * @param cmd Event command.
 * @return KC_WCH_OK, KC_WCH_NOT_FOUND, or KC_WCH_ERROR.
 */
int kc_wch_set_cmd(
    kc_wch_t *w,
    const char *cmd
);

/**
 * Return the configured event command.
 * @param w Watcher handle.
 * @return Borrowed command string, or NULL when unavailable.
 */
const char *kc_wch_get_cmd(
    const kc_wch_t *w
);

/**
 * Replace recursive observation mode.
 * @param w Watcher handle.
 * @param recursive Nonzero enables recursive observation.
 * @return KC_WCH_OK, KC_WCH_NOT_FOUND, or KC_WCH_ERROR.
 */
int kc_wch_set_recursive(
    kc_wch_t *w,
    int recursive
);

/**
 * Return recursive observation mode.
 * @param w Watcher handle.
 * @return Nonzero when recursive, otherwise zero.
 */
int kc_wch_get_recursive(
    const kc_wch_t *w
);

/**
 * Register or clear one temporary event subscription.
 * Supported events are add, upd, and del.
 * @param w Watcher handle.
 * @param event Event name.
 * @param handler Event handler, or NULL to clear it.
 * @param userdata Opaque handler data.
 * @return KC_WCH_OK on success, or KC_WCH_ERROR on failure.
 */
int kc_wch_on(
    kc_wch_t *w,
    const char *event,
    kc_wch_handler_t handler,
    void *userdata
);

/**
 * Release memory returned by wch.
 * @param ptr Owned allocation, or NULL.
 * @return None.
 */
void kc_wch_free(void *ptr);

/**
 * Close and release one local watcher handle.
 * This does not delete the resident watcher.
 * @param w Watcher handle, or NULL.
 * @return None.
 */
void kc_wch_close(kc_wch_t *w);

/**
 * Return the generated build version.
 * @return Build version value.
 */
uint64_t kc_wch_version(void);

#ifdef __cplusplus
}
#endif

#endif
