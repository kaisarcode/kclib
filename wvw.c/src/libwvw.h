/**
 * libwvw.h - Native WebView window.
 * Summary: Public API for persistent native WebView windows.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_WVW_H
#define KC_WVW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_wvw kc_wvw_t;

#define KC_WVW_OK 0
#define KC_WVW_ERROR -1

#define KC_WVW_BRIDGE_EVENT_NAME "nativebridge"
#define KC_WVW_TITLE_MAX 4096
#define KC_WVW_SIZE_MAX 16384

typedef int (*kc_wvw_bridge_callback_t)(
    kc_wvw_t *wvw,
    const char *method,
    const char *params_json,
    const char **out_result_json,
    void *userdata
);

typedef struct {
    const char *url;
    const char *title;
    const char *background;
    const int *width;
    const int *height;
    const int *posx;
    const int *posy;
    const int *fullscreen;
    const int *borderless;
    const int *always_on_top;
    const int *click_through;
    const int *no_focus;
} kc_wvw_options_t;

typedef struct {
    const char *const *methods;
    size_t method_count;
    kc_wvw_bridge_callback_t callback;
    void *userdata;
    int allow_file;
    int allow_data;
    int allow_localhost;
} kc_wvw_bridge_options_t;

/**
 * Open one operational native WebView window without a caller-visible event loop.
 * Strings and scalar option values are copied before return. URL is required.
 * Omitted width and height use 1280x720; omitted positions use native placement.
 * Omitted boolean options are false.
 * @param out Output WebView handle, set to NULL on failure.
 * @param options Initial window and WebView options.
 * @return KC_WVW_OK on success, or KC_WVW_ERROR on failure.
 */
int kc_wvw_open(kc_wvw_t **out, const kc_wvw_options_t *options);

/**
 * Return the last contextual error.
 * @param wvw WebView handle.
 * @return Borrowed error string, or NULL when no error is available.
 */
const char *kc_wvw_get_error(const kc_wvw_t *wvw);

/**
 * Navigate the current WebView to a URL.
 * @param wvw WebView handle.
 * @param url Destination URL.
 * @return KC_WVW_OK on success, or KC_WVW_ERROR on failure.
 */
int kc_wvw_navigate(kc_wvw_t *wvw, const char *url);

/**
 * Add trusted JavaScript for document-start execution.
 * @param wvw WebView handle.
 * @param javascript JavaScript source.
 * @return KC_WVW_OK on success, or KC_WVW_ERROR on failure.
 */
int kc_wvw_add_init_script(kc_wvw_t *wvw, const char *javascript);

/**
 * Enable one native bridge with a fixed method whitelist.
 * The method list, callback and userdata are retained logically until close;
 * method names are copied by the library.
 * @param wvw WebView handle.
 * @param options Bridge configuration.
 * @return KC_WVW_OK on success, or KC_WVW_ERROR on failure.
 */
int kc_wvw_enable_bridge(kc_wvw_t *wvw, const kc_wvw_bridge_options_t *options);

/**
 * Deliver one JSON value as a nativebridge event detail.
 * @param wvw WebView handle.
 * @param json Serialized JSON value.
 * @return KC_WVW_OK on success, or KC_WVW_ERROR on failure.
 */
int kc_wvw_post_bridge_event(kc_wvw_t *wvw, const char *json);

/** Hide the native window. */
int kc_wvw_hide(kc_wvw_t *wvw);

/** Show the native window. */
int kc_wvw_show(kc_wvw_t *wvw);

/** Minimize the native window. */
int kc_wvw_minimize(kc_wvw_t *wvw);

/** Maximize the native window. */
int kc_wvw_maximize(kc_wvw_t *wvw);

/** Restore the native window from minimized or maximized state. */
int kc_wvw_restore(kc_wvw_t *wvw);

/**
 * Set the native window title.
 * @param wvw WebView handle.
 * @param title UTF-8 title.
 * @return KC_WVW_OK on success, or KC_WVW_ERROR on failure.
 */
int kc_wvw_set_title(kc_wvw_t *wvw, const char *title);

/**
 * Return the configured native window title.
 * @param wvw WebView handle.
 * @return Borrowed title, invalidated by set_title or close, or NULL.
 */
const char *kc_wvw_get_title(const kc_wvw_t *wvw);

/**
 * Resize the native window content area.
 * @param wvw WebView handle.
 * @param width Width in pixels.
 * @param height Height in pixels.
 * @return KC_WVW_OK on success, or KC_WVW_ERROR on failure.
 */
int kc_wvw_set_size(kc_wvw_t *wvw, int width, int height);

/**
 * Return the current native window content size.
 * @param wvw WebView handle.
 * @param out_width Output width.
 * @param out_height Output height.
 * @return KC_WVW_OK on success, or KC_WVW_ERROR on failure.
 */
int kc_wvw_get_size(const kc_wvw_t *wvw, int *out_width, int *out_height);

/**
 * Move the native window.
 * @param wvw WebView handle.
 * @param x X position in screen coordinates.
 * @param y Y position in screen coordinates.
 * @return KC_WVW_OK on success, or KC_WVW_ERROR on failure.
 */
int kc_wvw_set_position(kc_wvw_t *wvw, int x, int y);

/**
 * Return the current native window position.
 * @param wvw WebView handle.
 * @param out_x Output x position.
 * @param out_y Output y position.
 * @return KC_WVW_OK on success, or KC_WVW_ERROR on failure.
 */
int kc_wvw_get_position(const kc_wvw_t *wvw, int *out_x, int *out_y);

/** Return nonzero when the native window is visible. */
int kc_wvw_is_visible(const kc_wvw_t *wvw);

/** Return nonzero when the native window is minimized. */
int kc_wvw_is_minimized(const kc_wvw_t *wvw);

/** Return nonzero when the native window is maximized. */
int kc_wvw_is_maximized(const kc_wvw_t *wvw);

/** Return nonzero when the native window is fullscreen. */
int kc_wvw_is_fullscreen(const kc_wvw_t *wvw);

/**
 * Close the WebView and end its public lifetime. NULL is safe.
 * @param wvw WebView handle, or NULL.
 * @return None.
 */
void kc_wvw_close(kc_wvw_t *wvw);

/**
 * Return the generated build version.
 * @return Build version value.
 */
uint64_t kc_wvw_version(void);

#ifdef __cplusplus
}
#endif

#endif
