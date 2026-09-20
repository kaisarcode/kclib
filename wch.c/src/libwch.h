/**
 * wch.h - File and directory change notification
 * Summary: Portable file watcher emitting add, upd, and del events.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_WCH_H
#define KC_WCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_wch kc_wch_t;

#define KC_WCH_OK      0
#define KC_WCH_ERROR  -1

#define KC_WCH_EVENT    1
#define KC_WCH_TIMEOUT  0

#define KC_WCH_ADD     0
#define KC_WCH_UPD     1
#define KC_WCH_DEL     2

typedef struct {
    int type;
    const char *path;
} kc_wch_event_t;

/**
 * Open a file watcher on the given path.
 * @param out Output pointer for watcher context.
 * @param path File or directory to watch.
 * @param recursive Non-zero to watch directories recursively.
 * @return KC_WCH_OK on success, or KC_WCH_ERROR on failure.
 */
int kc_wch_open(kc_wch_t **out, const char *path, int recursive);

/**
 * Poll for the next file change event.
 * @param w Watcher context.
 * @param ev Caller-owned event output. Its path is context-owned, must not be
 * freed or modified, is valid only after KC_WCH_EVENT, and remains valid until
 * another event from the same watcher or kc_wch_close(). It may be overwritten
 * by a later event and is NULL on timeout or when reset before waiting.
 * @param timeout_ms Negative waits indefinitely, zero does not wait, and a
 * positive value waits up to that many milliseconds.
 * @return KC_WCH_EVENT on event, KC_WCH_TIMEOUT on timeout, or KC_WCH_ERROR
 * on error.
 */
int kc_wch_poll(kc_wch_t *w, kc_wch_event_t *ev, int timeout_ms);

/**
 * Close a file watcher and release all resources. NULL safe.
 * @param w Watcher context (NULL safe).
 * @return None.
 */
void kc_wch_close(kc_wch_t *w);

/**
 * Retrieves the library build version as a Unix timestamp.
 * @return Build version timestamp.
 */
uint64_t kc_wch_version(void);

#ifdef __cplusplus
}
#endif

#endif
