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
#define KC_WCH_ESTOP  -3

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

/**
 * Open a file watcher on the given path.
 * @param out Output pointer for watcher context.
 * @param path File or directory to watch.
 * @param opts Watcher options.
 * @return KC_WCH_OK on success, or KC_WCH_ERROR on failure.
 */
int kc_wch_open(kc_wch_t **out, const char *path, const kc_wch_options_t *opts);

/**
 * Poll for the next file change event.
 * @param w Watcher context.
 * @param ev Output event structure.
 * @param timeout_ms Max wait in milliseconds (-1 = infinite, 0 = no wait).
 * @return 1 on event, 0 on timeout, -1 on error.
 */
int kc_wch_poll(kc_wch_t *w, kc_wch_event_t *ev, int timeout_ms);

/**
 * Close a file watcher and release all resources.
 * @param w Watcher context (NULL safe).
 * @return KC_WCH_OK on success, or KC_WCH_ERROR on failure.
 */
int kc_wch_close(kc_wch_t *w);

/**
 * Create an options struct initialized with default values.
 * @return Default-initialized options.
 */
kc_wch_options_t kc_wch_options_default(void);

/**
 * Load configuration from environment variables.
 * @param opts Options to update.
 * @return None.
 */
void kc_wch_options_load_env(kc_wch_options_t *opts);

/**
 * Free dynamically allocated resources within an options struct.
 * @param opts Options to clean up.
 * @return None.
 */
void kc_wch_options_free(kc_wch_options_t *opts);

/**
 * Request stop for a specific wch context.
 * @param w Watcher context.
 * @return KC_WCH_OK on success, or KC_WCH_ERROR on failure.
 */
int kc_wch_stop(kc_wch_t *w);

/**
 * Retrieves the library build version as a Unix timestamp.
 * @return Build version timestamp.
 */
uint64_t kc_wch_version(void);

#ifdef __cplusplus
}
#endif

#endif
