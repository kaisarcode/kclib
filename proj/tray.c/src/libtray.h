/**
 * libtray.h - Native system tray objects.
 * Summary: Public API for persistent trays and their menu items.
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
typedef struct kc_tray_item kc_tray_item_t;

#define KC_TRAY_OK 0
#define KC_TRAY_ERROR -1

typedef struct {
    const char *icon;
    const char *tooltip;
} kc_tray_options_t;

typedef void (*kc_tray_item_callback_t)(
    kc_tray_item_t *item,
    void *userdata
);

/**
 * Return the generated build version.
 * @return Build version.
 */
uint64_t kc_tray_version(void);

/**
 * Open a persistent tray without blocking the caller's main thread.
 * Initial strings are copied; NULL options omit initial properties.
 * On macOS the caller must use the main thread and run the AppKit loop.
 * @param out Output tray handle, set to NULL on failure.
 * @param options Initial icon and tooltip, or NULL.
 * @return KC_TRAY_OK on success, or KC_TRAY_ERROR on failure.
 */
int kc_tray_open(
    kc_tray_t **out,
    const kc_tray_options_t *options
);

/**
 * Update the native icon, or restore its default with NULL.
 * @param tray Tray handle.
 * @param icon Icon file path or platform icon name, or NULL.
 * @return KC_TRAY_OK on success, or KC_TRAY_ERROR on failure.
 */
int kc_tray_set_icon(
    kc_tray_t *tray,
    const char *icon
);

/**
 * Return the configured icon string.
 * @param tray Tray handle.
 * @return Borrowed icon, or NULL when unset; invalidated by set or close.
 */
const char *kc_tray_get_icon(
    const kc_tray_t *tray
);

/**
 * Update the native tooltip, or clear it with NULL.
 * @param tray Tray handle.
 * @param tooltip Tooltip string, or NULL.
 * @return KC_TRAY_OK on success, or KC_TRAY_ERROR on failure.
 */
int kc_tray_set_tooltip(
    kc_tray_t *tray,
    const char *tooltip
);

/**
 * Return the configured tooltip string.
 * @param tray Tray handle.
 * @return Borrowed tooltip, or NULL; invalidated by set or close.
 */
const char *kc_tray_get_tooltip(
    const kc_tray_t *tray
);

/**
 * Add a persistent item owned by the tray.
 * Callback and userdata remain borrowed until item removal or tray close.
 * The callback receives the exact item and may remove it or close the tray.
 * @param tray Tray handle.
 * @param out Output item handle, set to NULL on failure.
 * @param text Nonempty item label.
 * @param callback Activation callback, or NULL.
 * @param userdata Caller data passed to the callback.
 * @return KC_TRAY_OK on success, or KC_TRAY_ERROR on failure.
 */
int kc_tray_add_item(
    kc_tray_t *tray,
    kc_tray_item_t **out,
    const char *text,
    kc_tray_item_callback_t callback,
    void *userdata
);

/**
 * Add a removable separator owned by the tray.
 * @param tray Tray handle.
 * @param out Output separator handle, set to NULL on failure.
 * @return KC_TRAY_OK on success, or KC_TRAY_ERROR on failure.
 */
int kc_tray_add_separator(
    kc_tray_t *tray,
    kc_tray_item_t **out
);

/**
 * Update an item's native label.
 * @param item Item handle; separators cannot have text.
 * @param text Nonempty replacement label.
 * @return KC_TRAY_OK on success, or KC_TRAY_ERROR on failure.
 */
int kc_tray_item_set_text(
    kc_tray_item_t *item,
    const char *text
);

/**
 * Return an item's label.
 * @param item Item handle.
 * @return Borrowed label, or NULL for separators; invalidated by mutation.
 */
const char *kc_tray_item_get_text(
    const kc_tray_item_t *item
);

/**
 * Remove one item and end its public lifetime, including from its callback.
 * @param item Item handle, or NULL.
 * @return None.
 */
void kc_tray_item_remove(
    kc_tray_item_t *item
);

/**
 * Return the last contextual error.
 * @param tray Tray handle.
 * @return Borrowed error until the next operation or close, or NULL.
 */
const char *kc_tray_get_error(
    const kc_tray_t *tray
);

/**
 * Close the tray and all items, including from an item callback.
 * Close prevents future callbacks and ends all public object lifetimes.
 * @param tray Tray handle, or NULL.
 * @return None.
 */
void kc_tray_close(
    kc_tray_t *tray
);

#ifdef __cplusplus
}
#endif

#endif
