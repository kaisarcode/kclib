/**
 * wvw.h - Public API for the wvw library.
 * Summary: Provides a small native WebView wrapper.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_WVW_H
#define KC_WVW_H

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
#define KC_WVW_POSITION_AUTO -2147483647

typedef struct {
    int width;
    int height;
    int minimized;
    int maximized;
    int fullscreen;
    int visible;
} kc_wvw_window_state_t;

typedef int (*kc_wvw_bridge_callback_t)(
kc_wvw_t *ctx,
const char *method,
const char *params_json,
char **result_json,
void *userdata
);

typedef struct {
    char *url;
    char *title;
    char *background;
    int width;
    int height;
    int posx;
    int posy;
    int fullscreen;
    int borderless;
    int always_on_top;
    int click_through;
    int no_focus;
} kc_wvw_options_t;

typedef struct {
    const char **methods;
    int method_count;
    kc_wvw_bridge_callback_t callback;
    void *userdata;
    int allow_file;
    int allow_data;
    int allow_localhost;
} kc_wvw_bridge_options_t;

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_wvw_version(void);

/**
 * Create an options struct initialized with default values.
 * @return Default-initialized options.
 */
kc_wvw_options_t kc_wvw_options_default(void);

/**
 * Load configuration from environment variables.
 * @param opts Options to update.
 * @return None.
 */
void kc_wvw_options_load_env(kc_wvw_options_t *opts);

/**
 * Free dynamically allocated resources within an options struct.
 * @param opts Options to clean up.
 * @return None.
 */
void kc_wvw_options_free(kc_wvw_options_t *opts);

/**
 * Request stop for a specific wvw context.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_stop(kc_wvw_t *ctx);

/**
 * Create a new native WebView context.
 * @param ctx_out Destination context pointer.
 * @param opts Configuration options.
 * On failure, close a non-NULL destination context after reading its error.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_open(kc_wvw_t **ctx_out, kc_wvw_options_t *opts);

/**
 * Return the last context error.
 * @param ctx Window context.
 * @return Borrowed error string, or NULL when no error is available.
 */
const char *kc_wvw_get_error(const kc_wvw_t *ctx);

/**
 * Release a WebView context and its resources.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_close(kc_wvw_t *ctx);

/**
 * Start the native window event loop.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_loop(kc_wvw_t *ctx);

/**
 * Navigate the current WebView to a new URL.
 * @param ctx Window context.
 * @param url Destination URL.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_navigate(kc_wvw_t *ctx, const char *url);

/**
 * Add trusted JavaScript for document-start execution in one WebView.
 * @param ctx Window context.
 * @param javascript Source text to install.
 * @return KC_WVW_OK on installation or KC_WVW_ERROR on failure.
 */
int kc_wvw_add_init_script(kc_wvw_t *ctx, const char *javascript);

/**
 * Enable one native bridge with a fixed method whitelist.
 * @param ctx Window context.
 * @param opts Bridge configuration options.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_enable_bridge(kc_wvw_t *ctx, const kc_wvw_bridge_options_t *opts);

/**
 * Deliver one native bridge event into the current WebView.
 * @param ctx Window context.
 * @param json JSON payload to dispatch as the event detail.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_post_bridge_event(kc_wvw_t *ctx, const char *json);

/**
 * Hide the window.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_hide(kc_wvw_t *ctx);

/**
 * Show the window.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_show(kc_wvw_t *ctx);

/**
 * Minimize (iconify) the window.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_minimize(kc_wvw_t *ctx);

/**
 * Maximize the window.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_maximize(kc_wvw_t *ctx);

/**
 * Restore the window from minimized or maximized state.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_restore(kc_wvw_t *ctx);

/**
 * Set the native window title.
 * @param ctx Window context.
 * @param title UTF-8 title string.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_set_title(kc_wvw_t *ctx, const char *title);

/**
 * Resize the native window content area.
 * @param ctx Window context.
 * @param width Width in pixels.
 * @param height Height in pixels.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_set_size(kc_wvw_t *ctx, int width, int height);

/**
 * Query the current window state.
 * @param ctx Window context.
 * @param state Destination state structure.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_get_state(kc_wvw_t *ctx, kc_wvw_window_state_t *state);

#ifdef __cplusplus
}
#endif

#endif
