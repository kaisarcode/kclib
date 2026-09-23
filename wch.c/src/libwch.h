/**
 * wch.h - File and directory change notification.
 * Summary: Portable asynchronous file watcher emitting add, upd, and del events.
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

#define KC_WCH_ADD     0
#define KC_WCH_UPD     1
#define KC_WCH_DEL     2

typedef struct {
    int recursive;
} kc_wch_options_t;

typedef struct {
    int type;
    const char *path;
} kc_wch_event_t;

typedef void (*kc_wch_handler_t)(
    const kc_wch_event_t *event,
    void *userdata
);

/**
 * Opens one watcher instance.
 * @param out Output pointer for the new watcher.
 * @param path File or directory to watch.
 * @param options Optional watcher options; NULL uses defaults.
 * @return KC_WCH_OK on success, or KC_WCH_ERROR on failure.
 */
int kc_wch_open(
    kc_wch_t **out,
    const char *path,
    const kc_wch_options_t *options
);

/**
 * Registers the event handler and starts asynchronous observation.
 * @param w Watcher instance.
 * @param handler Event handler invoked from the watcher worker thread.
 * @param userdata Opaque caller value passed to the handler.
 * @return KC_WCH_OK on success, or KC_WCH_ERROR on failure.
 */
int kc_wch_on(
    kc_wch_t *w,
    kc_wch_handler_t handler,
    void *userdata
);

/**
 * Closes one watcher and releases its resources. NULL safe.
 * @param w Watcher instance.
 * @return None.
 */
void kc_wch_close(kc_wch_t *w);

/**
 * Returns the generated build version.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_wch_version(void);

#ifdef __cplusplus
}
#endif

#endif
