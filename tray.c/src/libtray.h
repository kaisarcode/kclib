/**
 * tray.h - Native system tray library.
 * Summary: Public API for the libtray library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_LIBTRAY_H
#define KC_LIBTRAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_tray kc_tray_t;

#define KC_TRAY_OK      0
#define KC_TRAY_ERROR  -1

/**
 * Opaque options handle.
 */
typedef void *kc_tray_options_t;

/**
 * One tray menu item.
 * A NULL label and NULL action define a separator.
 * A label without an action and an action without a label are invalid.
 * Labels and actions must be non-empty when present.
 */
typedef struct {
    const char *label;
    const char *action;
} kc_tray_item_t;

/**
 * Tray menu activation callback.
 * @param userdata Caller data borrowed at kc_tray_open.
 * @param action Action name of the activated item.
 * @return None.
 */
typedef void (*kc_tray_callback_t)(void *userdata, const char *action);

/**
 * Return default options for the library (caller owns, must free).
 * @return Opaque options handle, or NULL on failure.
 */
kc_tray_options_t kc_tray_options_default(void);

/**
 * Set an option value by key.
 * Supported keys are "icon" and "tooltip".
 * @param opts Options handle from kc_tray_options_default.
 * @param key Option key.
 * @param value Option value, or NULL to clear.
 * @return KC_TRAY_OK on success, KC_TRAY_ERROR on unknown key.
 */
int kc_tray_options_set(kc_tray_options_t opts, const char *key, const char *value);

/**
 * Release resources owned by an options handle.
 * @param opts Options handle from kc_tray_options_default, or NULL.
 * @return None.
 */
void kc_tray_options_free(kc_tray_options_t opts);

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_tray_version(void);

/**
 * Initialize a new tray context.
 * The icon and tooltip options are copied into context-owned storage.
 * The callback and userdata are borrowed for the lifetime of the context.
 * Each open creates an independent context where the native platform permits
 * independent instances.
 * @param ctx_out Destination context pointer.
 * @param opts Opaque options handle from kc_tray_options_default.
 * @param callback Menu activation callback, or NULL.
 * @param userdata Caller data passed to the callback, or NULL.
 * @return KC_TRAY_OK on success, KC_TRAY_ERROR on failure.
 */
int kc_tray_open(kc_tray_t **ctx_out, kc_tray_options_t opts, kc_tray_callback_t callback, void *userdata);

/**
 * Replace the tray menu with a copied item array.
 * All caller strings are copied. On failure the active menu is unchanged and
 * the partial copy is released. A NULL items array or a count of zero clears
 * the menu. A NULL items array with a positive count is invalid.
 * @param ctx Context pointer.
 * @param items Array of menu items, or NULL to clear.
 * @param count Number of items.
 * @return KC_TRAY_OK on success, KC_TRAY_ERROR on invalid input.
 */
int kc_tray_set_menu(kc_tray_t *ctx, const kc_tray_item_t *items, int count);

/**
 * Enter the platform event loop until kc_tray_stop is requested.
 *
 * Run owns the blocking event-loop execution for ctx. It honors a stop that
 * happened before run, in which case it returns promptly without entering the
 * loop. Repeated run calls after a stop return promptly. Run does not destroy
 * the context.
 *
 * On macOS the loop is context-local; on Linux each running context uses its
 * own loop object; on Windows the loop wakes through the context's own window.
 *
 * @param ctx Context pointer.
 * @return KC_TRAY_OK on success, KC_TRAY_ERROR on failure.
 */
int kc_tray_run(kc_tray_t *ctx);

/**
 * Request stop for a specific tray context.
 *
 * Stop requests termination of ctx's active or future run, wakes a blocked
 * native event loop promptly, and does not destroy the context. Repeated stop
 * calls are harmless. Stopping one context must not terminate another
 * independently running context on platforms that support such independence.
 *
 * @param ctx Context pointer.
 * @return KC_TRAY_OK on success, KC_TRAY_ERROR on failure.
 */
int kc_tray_stop(kc_tray_t *ctx);

/**
 * Release a tray context and all copied native resources.
 *
 * Close is the ownership-destruction boundary. The context must not be used
 * after close. Close releases native resources exactly once and is distinct
 * from stop, which only requests event-loop termination.
 *
 * @param ctx Context pointer, or NULL.
 * @return KC_TRAY_OK.
 */
int kc_tray_close(kc_tray_t *ctx);

/**
 * Get the last error message from a context.
 * @param ctx Context pointer.
 * @return Error string, or NULL if no error.
 */
const char *kc_tray_get_error(const kc_tray_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
