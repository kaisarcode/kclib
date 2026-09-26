/**
 * libtray.h - Native system tray objects.
 * Author: KaisarCode
 * License: GPL-3.0
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

typedef void (*kc_tray_item_callback_t)(kc_tray_item_t *item, void *userdata);

uint64_t kc_tray_version(void);

/** Open a persistent tray, copying non-NULL initial strings. Returns promptly.
 * NULL options are equivalent to { NULL, NULL }. On failure *out is NULL.
 * Windows and Linux own their native event processing internally. On macOS,
 * call from the main thread of an application running its AppKit event loop;
 * subsequent operations on macOS also require that main thread.
 */
int kc_tray_open(kc_tray_t **out, const kc_tray_options_t *options);

/** NULL clears the native property; the strings are copied. Returns ERROR on
 * invalid icon resources or native failure, leaving the old property intact.
 */
int kc_tray_set_icon(kc_tray_t *tray, const char *icon);
/** Borrowed; invalidated by set_icon or close. NULL means unset. */
const char *kc_tray_get_icon(const kc_tray_t *tray);
int kc_tray_set_tooltip(kc_tray_t *tray, const char *tooltip);
/** Borrowed; invalidated by set_tooltip or close. NULL means unset. */
const char *kc_tray_get_tooltip(const kc_tray_t *tray);

/** Create a persistent child. Callback/userdata are retained, not owned; they
 * must remain valid until item removal or tray close. NULL callback is valid.
 * The callback receives this exact child, and may remove it or close its tray.
 */
int kc_tray_add_item(kc_tray_t *tray, kc_tray_item_t **out,
                     const char *text, kc_tray_item_callback_t callback,
                     void *userdata);
int kc_tray_add_separator(kc_tray_t *tray, kc_tray_item_t **out);
/** Update the native label. NULL and empty text are invalid. */
int kc_tray_item_set_text(kc_tray_item_t *item, const char *text);
/** Borrowed; invalidated by set_text, remove or tray close. Separators return NULL. */
const char *kc_tray_item_get_text(const kc_tray_item_t *item);
/** Ends the child's public lifetime; safe from its own callback. NULL is a no-op. */
void kc_tray_item_remove(kc_tray_item_t *item);
/** Borrowed until next operation on this tray or close; NULL if no error. */
const char *kc_tray_get_error(const kc_tray_t *tray);
/** Ends the tray and all child lifetimes and prevents future callbacks.
 * Safe from a callback; NULL is a no-op. No pointer may be used after close.
 */
void kc_tray_close(kc_tray_t *tray);

#ifdef __cplusplus
}
#endif
#endif
