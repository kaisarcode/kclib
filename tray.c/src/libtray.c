/**
 * libtray.c - Native system tray implementation.
 * Summary: Provides a tray icon with a native context menu.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#ifndef __APPLE__
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "libtray.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <Foundation/Foundation.h>
#include <objc/runtime.h>
#else
#include <gtk/gtk.h>
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define KC_TRAY_WINDOW_CLASS L"kcTrayWindow"
#define KC_TRAY_MESSAGE      (WM_APP + 1)
#define KC_TRAY_STOP_MESSAGE (WM_APP + 2)
#define KC_TRAY_ICON_ID      1
#endif

#ifndef KC_TRAY_BUILD_VERSION
#define KC_TRAY_BUILD_VERSION 0ULL
#endif

struct kc_tray_options {
    char *icon;
    char *tooltip;
};

struct kc_tray {
    char *icon;
    char *tooltip;
    kc_tray_callback_t callback;
    void *userdata;
    kc_tray_item_t *items;
    int count;
    int running;
    int stop_requested;
    int closing;
    char error[256];
#if defined(_WIN32)
    HWND hwnd;
    HICON native_icon;
#elif defined(__APPLE__)
    void *ns_status_item;
#else
    GtkStatusIcon *status_icon;
    GMainLoop *loop;
#endif
};

/**
 * Set an error message on the context.
 * @param ctx Tray context.
 * @param fmt Printf-style format string.
 * @param ... Format arguments.
 * @return None.
 */
static void kc_tray_set_error(kc_tray_t *ctx, const char *fmt, ...) {
    va_list ap;

    if (!ctx || !fmt) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(ctx->error, sizeof(ctx->error), fmt, ap);
    va_end(ap);
    ctx->error[sizeof(ctx->error) - 1] = '\0';
}

/**
 * Copy one string into fresh heap memory.
 * @param text Source string that must be NULL-terminated.
 * @return Owned copy, or NULL when text is NULL or memory is exhausted.
 */
static char *kc_tray_strdup(const char *text) {
    size_t length;
    char *copy;

    if (!text) {
        return NULL;
    }
    length = strlen(text);
    copy = (char *)malloc(length + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, length + 1);
    return copy;
}

/**
 * Release the current tray item array and its copied strings.
 * @param ctx Tray context.
 * @return None.
 */
static void kc_tray_menu_items_free(kc_tray_t *ctx) {
    int i;

    if (!ctx || !ctx->items) {
        return;
    }
    for (i = 0; i < ctx->count; i++) {
        free((void *)ctx->items[i].label);
        free((void *)ctx->items[i].action);
    }
    free(ctx->items);
    ctx->items = NULL;
    ctx->count = 0;
}

/**
 * Validate one tray menu item definition.
 * @param item Item definition to validate.
 * @return 1 when valid, 0 when invalid.
 */
static int kc_tray_item_valid(const kc_tray_item_t *item) {
    if (!item) {
        return 0;
    }
    if (item->label == NULL && item->action != NULL) {
        return 0;
    }
    if (item->label != NULL && item->action == NULL) {
        return 0;
    }
    if (item->label && item->label[0] == '\0') {
        return 0;
    }
    if (item->action && item->action[0] == '\0') {
        return 0;
    }
    return 1;
}

/**
 * Copy an item array into context-owned storage.
 * The active menu is replaced only after the full copy succeeds.
 * @param ctx Tray context.
 * @param items Caller item array.
 * @param count Number of items.
 * @return KC_TRAY_OK on success, KC_TRAY_ERROR on allocation failure.
 */
static int kc_tray_menu_copy(kc_tray_t *ctx, const kc_tray_item_t *items, int count) {
    kc_tray_item_t *copy;
    int i;

    copy = (kc_tray_item_t *)calloc((size_t)count, sizeof(kc_tray_item_t));
    if (!copy) {
        return KC_TRAY_ERROR;
    }
    for (i = 0; i < count; i++) {
        if (items[i].label) {
            copy[i].label = kc_tray_strdup(items[i].label);
            if (!copy[i].label) {
                goto failure;
            }
        }
        if (items[i].action) {
            copy[i].action = kc_tray_strdup(items[i].action);
            if (!copy[i].action) {
                goto failure;
            }
        }
    }

    kc_tray_menu_items_free(ctx);
    ctx->items = copy;
    ctx->count = count;
    return KC_TRAY_OK;

failure:
    for (i = 0; i < count; i++) {
        free((void *)copy[i].label);
        free((void *)copy[i].action);
    }
    free(copy);
    return KC_TRAY_ERROR;
}

/**
 * Deliver one menu activation to the context callback.
 * @param ctx Tray context.
 * @param action Action name of the activated item.
 * @return None.
 */
static void kc_tray_deliver(kc_tray_t *ctx, const char *action) {
    if (!ctx || !ctx->callback || !action) {
        return;
    }
    ctx->callback(ctx->userdata, action);
}

#if defined(_WIN32)

/**
 * Convert one UTF-8 string to a fresh UTF-16 buffer.
 * @param text UTF-8 source string.
 * @return Owned UTF-16 copy, or NULL on invalid input or allocation failure.
 */
static wchar_t *kc_tray_utf16_from_utf8(const char *text) {
    int length;
    wchar_t *buffer;

    if (!text) {
        return NULL;
    }
    length = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (length <= 0) {
        return NULL;
    }
    buffer = (wchar_t *)malloc((size_t)length * sizeof(wchar_t));
    if (!buffer) {
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8, 0, text, -1, buffer, length);
    return buffer;
}

/**
 * Show the context menu for the tray icon and deliver the selection.
 * @param ctx Tray context.
 * @param hwnd Message window that owns the notification icon.
 * @return None.
 */
static void kc_tray_windows_popup(kc_tray_t *ctx, HWND hwnd) {
    HMENU menu;
    POINT point;
    UINT selection;
    int i;

    menu = CreatePopupMenu();
    if (!menu) {
        return;
    }
    for (i = 0; i < ctx->count; i++) {
        if (!ctx->items[i].label) {
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        } else {
            wchar_t *label = kc_tray_utf16_from_utf8(ctx->items[i].label);
            if (!label) {
                DestroyMenu(menu);
                return;
            }
            AppendMenuW(menu, MF_STRING, (UINT_PTR)(i + 1), label);
            free(label);
        }
    }
    if (!GetCursorPos(&point)) {
        DestroyMenu(menu);
        return;
    }
    SetForegroundWindow(hwnd);
    selection = TrackPopupMenu(menu,
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        point.x, point.y, 0, hwnd, NULL);
    DestroyMenu(menu);

    if (selection && selection <= (UINT)ctx->count) {
        kc_tray_deliver(ctx, ctx->items[selection - 1].action);
    }
}

/**
 * Message procedure for the tray message window.
 * @param hwnd Message window handle.
 * @param msg Window message.
 * @param wparam Message payload.
 * @param lparam Message payload.
 * @return Message result.
 */
static LRESULT CALLBACK kc_tray_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    kc_tray_t *ctx;

    if (msg == WM_CREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        return 0;
    }

    ctx = (kc_tray_t *)(intptr_t)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!ctx) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    if (msg == KC_TRAY_MESSAGE) {
        if (wparam == KC_TRAY_ICON_ID) {
            if (lparam == WM_LBUTTONUP || lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) {
                kc_tray_windows_popup(ctx, hwnd);
            }
        }
        return 0;
    }
    if (msg == KC_TRAY_STOP_MESSAGE) {
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/**
 * Create the Windows tray icon owned by a hidden message window.
 * @param ctx Tray context.
 * @return KC_TRAY_OK on success, KC_TRAY_ERROR on failure.
 */
static int kc_tray_windows_init(kc_tray_t *ctx) {
    HINSTANCE instance;
    WNDCLASSW window_class;
    NOTIFYICONDATAW nid;
    HWND hwnd;
    HICON icon;

    instance = GetModuleHandleW(NULL);
    memset(&window_class, 0, sizeof(window_class));
    window_class.lpfnWndProc = kc_tray_wnd_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = KC_TRAY_WINDOW_CLASS;
    if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        kc_tray_set_error(ctx, "window class registration failed");
        return KC_TRAY_ERROR;
    }

    hwnd = CreateWindowExW(0, KC_TRAY_WINDOW_CLASS, L"kc_tray", 0,
        0, 0, 0, 0, HWND_MESSAGE, NULL, instance, ctx);
    if (!hwnd) {
        kc_tray_set_error(ctx, "tray window creation failed");
        return KC_TRAY_ERROR;
    }
    ctx->hwnd = hwnd;

    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = KC_TRAY_ICON_ID;
    nid.uFlags = NIF_MESSAGE;
    nid.uCallbackMessage = KC_TRAY_MESSAGE;

    if (ctx->tooltip) {
        wchar_t *tip = kc_tray_utf16_from_utf8(ctx->tooltip);
        if (tip) {
            wcsncpy(nid.szTip, tip, 127);
            nid.szTip[127] = L'\0';
            nid.uFlags |= NIF_TIP;
            free(tip);
        }
    }

    if (ctx->icon) {
        wchar_t *path = kc_tray_utf16_from_utf8(ctx->icon);
        if (path) {
            icon = (HICON)LoadImageW(NULL, path, IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
            free(path);
            if (icon) {
                nid.hIcon = icon;
                nid.uFlags |= NIF_ICON;
                ctx->native_icon = icon;
            }
        }
    }
    if (!(nid.uFlags & NIF_ICON)) {
        nid.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
        nid.uFlags |= NIF_ICON;
    }

    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        kc_tray_set_error(ctx, "notification icon addition failed");
        return KC_TRAY_ERROR;
    }
    return KC_TRAY_OK;
}

/**
 * Remove the Windows tray icon and destroy the message window.
 * @param ctx Tray context.
 * @return None.
 */
static void kc_tray_windows_release(kc_tray_t *ctx) {
    NOTIFYICONDATAW nid;

    if (!ctx || !ctx->hwnd) {
        return;
    }
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = ctx->hwnd;
    nid.uID = KC_TRAY_ICON_ID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    DestroyWindow(ctx->hwnd);
    ctx->hwnd = NULL;
    if (ctx->native_icon) {
        DestroyIcon(ctx->native_icon);
        ctx->native_icon = NULL;
    }
}

#elif defined(__APPLE__)

static void kc_tray_macos_rebuild_menu(kc_tray_t *ctx);

/**
 * Target object that receives status-item menu actions.
 */
@interface KCTrayStatusItemTarget : NSObject
@property (nonatomic, assign) kc_tray_t *ctx;
- (void)kc_tray_item_action:(id)sender;
@end

@implementation KCTrayStatusItemTarget

- (void)kc_tray_item_action:(id)sender {
    NSMenuItem *item;
    NSInteger index;
    const char *action;

    if (!self.ctx || ![sender isKindOfClass:[NSMenuItem class]]) {
        return;
    }
    item = (NSMenuItem *)sender;
    index = item.tag;
    @autoreleasepool {
        if (index < 0 || index >= self.ctx->count || !self.ctx->items) {
            return;
        }
        action = self.ctx->items[index].action;
        kc_tray_deliver(self.ctx, action);
    }
}

@end

/**
 * Rebuild the NSStatusItem menu from the active item array.
 * @param ctx Tray context.
 * @return None.
 */
static void kc_tray_macos_rebuild_menu(kc_tray_t *ctx) {
    NSMenu *menu;
    int i;

    if (!ctx || !ctx->ns_status_item) {
        return;
    }

    @autoreleasepool {
        NSStatusItem *status_item = (__bridge NSStatusItem *)ctx->ns_status_item;
        id target = objc_getAssociatedObject(status_item, "kc_tray_target");
        menu = [[[NSMenu alloc] init] autorelease];

        if (ctx->count > 0 && ctx->items) {
            for (i = 0; i < ctx->count; i++) {
                if (!ctx->items[i].label) {
                    [menu addItem:[NSMenuItem separatorItem]];
                } else {
                    NSString *label = [NSString stringWithUTF8String:ctx->items[i].label];
                    NSMenuItem *item = [[[NSMenuItem alloc] initWithTitle:label
                        action:@selector(kc_tray_item_action:)
                        keyEquivalent:@""] autorelease];
                    item.target = target;
                    item.tag = i;
                    [menu addItem:item];
                }
            }
        }

        status_item.menu = menu;
    }
}

/**
 * Create the macOS tray status item.
 * @param ctx Tray context.
 * @return KC_TRAY_OK on success, KC_TRAY_ERROR on failure.
 */
static int kc_tray_macos_init(kc_tray_t *ctx) {
    @autoreleasepool {
        NSStatusItem *status_item;

        [NSApplication sharedApplication];
        status_item = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];

        if (ctx->icon) {
            NSString *icon_str = [NSString stringWithUTF8String:ctx->icon];
            NSImage *image = nil;
            if (strchr(ctx->icon, '/')) {
                image = [[[NSImage alloc] initWithContentsOfFile:icon_str] autorelease];
            } else {
                image = [NSImage imageNamed:icon_str];
            }
            if (image) {
                [image setSize:NSMakeSize(18, 18)];
                status_item.button.image = image;
            }
        }
        if (!status_item.button.image) {
            NSImage *default_image = [NSImage imageNamed:NSImageNameApplicationIcon];
            if (default_image) {
                [default_image setSize:NSMakeSize(18, 18)];
                status_item.button.image = default_image;
            }
        }

        if (ctx->tooltip) {
            [[status_item button] setToolTip:[NSString stringWithUTF8String:ctx->tooltip]];
        }

        KCTrayStatusItemTarget *target = [[[KCTrayStatusItemTarget alloc] init] autorelease];
        target.ctx = ctx;
        objc_setAssociatedObject(status_item, "kc_tray_target", target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        ctx->ns_status_item = (void *)CFBridgingRetain(status_item);
        kc_tray_macos_rebuild_menu(ctx);
    }

    return KC_TRAY_OK;
}

/**
 * Remove the macOS tray status item.
 * @param ctx Tray context.
 * @return None.
 */
static void kc_tray_macos_release(kc_tray_t *ctx) {
    if (!ctx || !ctx->ns_status_item) {
        return;
    }

    @autoreleasepool {
        NSStatusItem *status_item = (__bridge NSStatusItem *)ctx->ns_status_item;
        objc_setAssociatedObject(status_item, "kc_tray_target", nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        status_item.menu = nil;
        [[NSStatusBar systemStatusBar] removeStatusItem:status_item];
        CFRelease(ctx->ns_status_item);
        ctx->ns_status_item = NULL;
    }
}

#else

/**
 * Release one transient GTK context menu after dismissal.
 * @param menu GTK menu that finished selection.
 * @param userdata Unused.
 * @return None.
 */
static void kc_tray_linux_menu_done(GtkWidget *menu, gpointer userdata) {
    (void)userdata;
    gtk_widget_destroy(menu);
}

/**
 * Deliver the action attached to one activated menu item.
 * @param item Activated menu item.
 * @param userdata Tray context.
 * @return None.
 */
static void kc_tray_linux_deliver(GtkWidget *item, gpointer userdata) {
    kc_tray_t *ctx = (kc_tray_t *)userdata;
    const char *action;

    if (!ctx) {
        return;
    }
    action = (const char *)g_object_get_data(G_OBJECT(item), "kc-tray-action");
    kc_tray_deliver(ctx, action);
}

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

/**
 * Build and show the context menu for the status icon.
 * @param icon Status icon.
 * @param button Mouse button.
 * @param activate_time Event time.
 * @param userdata Tray context.
 * @return None.
 */
static void kc_tray_linux_popup_menu(GtkStatusIcon *icon, guint button, guint activate_time, gpointer userdata) {
    kc_tray_t *ctx = (kc_tray_t *)userdata;
    GtkWidget *menu;
    GtkWidget *item;
    int i;

    (void)icon;

    if (!ctx) {
        return;
    }
    menu = gtk_menu_new();
    for (i = 0; i < ctx->count; i++) {
        if (!ctx->items[i].label) {
            item = gtk_separator_menu_item_new();
        } else {
            item = gtk_menu_item_new_with_label(ctx->items[i].label);
            g_object_set_data(G_OBJECT(item), "kc-tray-action",
                (gpointer)ctx->items[i].action);
            g_signal_connect(item, "activate", G_CALLBACK(kc_tray_linux_deliver), ctx);
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    g_signal_connect(menu, "selection-done", G_CALLBACK(kc_tray_linux_menu_done), NULL);
    gtk_widget_show_all(menu);
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, gtk_status_icon_position_menu,
        icon, button, activate_time);
}

/**
 * Forward primary activation to the same popup menu path.
 * @param icon Status icon.
 * @param userdata Tray context.
 * @return None.
 */
static void kc_tray_linux_activate(GtkStatusIcon *icon, gpointer userdata) {
    kc_tray_linux_popup_menu(icon, 0, gtk_get_current_event_time(), userdata);
}

/**
 * Create the Linux system-tray icon.
 * @param ctx Tray context.
 * @return KC_TRAY_OK on success, KC_TRAY_ERROR on failure.
 */
static int kc_tray_linux_init(kc_tray_t *ctx) {
    GtkStatusIcon *icon;

    if (!gtk_init_check(NULL, NULL)) {
        kc_tray_set_error(ctx, "GTK initialization failed");
        return KC_TRAY_ERROR;
    }

    icon = NULL;
    if (ctx->icon) {
        if (strchr(ctx->icon, '/')) {
            icon = gtk_status_icon_new_from_file(ctx->icon);
        } else {
            icon = gtk_status_icon_new_from_icon_name(ctx->icon);
        }
    }
    if (!icon) {
        icon = gtk_status_icon_new_from_icon_name("emblem-system");
    }
    if (!icon) {
        kc_tray_set_error(ctx, "status icon creation failed");
        return KC_TRAY_ERROR;
    }

    g_signal_connect(icon, "popup-menu", G_CALLBACK(kc_tray_linux_popup_menu), ctx);
    g_signal_connect(icon, "activate", G_CALLBACK(kc_tray_linux_activate), ctx);
    if (ctx->tooltip) {
        gtk_status_icon_set_tooltip_text(icon, ctx->tooltip);
    }
    gtk_status_icon_set_visible(icon, TRUE);
    ctx->status_icon = icon;
    return KC_TRAY_OK;
}

/**
 * Remove the Linux tray icon.
 * @param ctx Tray context.
 * @return None.
 */
static void kc_tray_linux_release(kc_tray_t *ctx) {
    GtkStatusIcon *icon;

    if (!ctx || !ctx->status_icon) {
        return;
    }
    icon = ctx->status_icon;
    ctx->status_icon = NULL;
    gtk_status_icon_set_visible(icon, FALSE);
    g_signal_handlers_disconnect_by_data(icon, ctx);
    g_object_unref(icon);
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#endif

/**
 * Create one owned platform tray icon from the copied options.
 * @param ctx Tray context.
 * @return KC_TRAY_OK on success, KC_TRAY_ERROR on failure.
 */
static int kc_tray_platform_init(kc_tray_t *ctx) {
#if defined(_WIN32)
    return kc_tray_windows_init(ctx);
#elif defined(__APPLE__)
    return kc_tray_macos_init(ctx);
#else
    return kc_tray_linux_init(ctx);
#endif
}

/**
 * Release the platform tray icon.
 * @param ctx Tray context.
 * @return None.
 */
static void kc_tray_platform_release(kc_tray_t *ctx) {
#if defined(_WIN32)
    kc_tray_windows_release(ctx);
#elif defined(__APPLE__)
    kc_tray_macos_release(ctx);
#else
    kc_tray_linux_release(ctx);
#endif
}

/**
 * Rebuild the active platform menu after a menu replacement or clear.
 * Windows and Linux rebuild the menu at popup time, so only macOS needs
 * to recreate its NSStatusItem menu.
 * @param ctx Tray context.
 * @return None.
 */
static void kc_tray_platform_menu_rebuild(kc_tray_t *ctx) {
#if defined(__APPLE__)
    kc_tray_macos_rebuild_menu(ctx);
#else
    (void)ctx;
#endif
}

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_tray_version(void) {
    return (uint64_t)KC_TRAY_BUILD_VERSION;
}

/**
 * Return default options for the library (caller owns, must free).
 * @return Opaque options handle, or NULL on failure.
 */
kc_tray_options_t kc_tray_options_default(void) {
    return (kc_tray_options_t)calloc(1, sizeof(struct kc_tray_options));
}

/**
 * Set an option value by key.
 * @param opts Options handle from kc_tray_options_default.
 * @param key Option key, "icon" or "tooltip".
 * @param value Option value, or NULL to clear.
 * @return KC_TRAY_OK on success, KC_TRAY_ERROR on unknown key.
 */
int kc_tray_options_set(kc_tray_options_t opts, const char *key, const char *value) {
    struct kc_tray_options *o = (struct kc_tray_options *)opts;
    char **field;

    if (!o || !key) {
        return KC_TRAY_ERROR;
    }
    if (strcmp(key, "icon") == 0) {
        field = &o->icon;
    } else if (strcmp(key, "tooltip") == 0) {
        field = &o->tooltip;
    } else {
        return KC_TRAY_ERROR;
    }

    if (!value) {
        free(*field);
        *field = NULL;
        return KC_TRAY_OK;
    }
    free(*field);
    *field = kc_tray_strdup(value);
    return *field ? KC_TRAY_OK : KC_TRAY_ERROR;
}

/**
 * Release resources owned by an options handle.
 * @param opts Options handle from kc_tray_options_default, or NULL.
 * @return None.
 */
void kc_tray_options_free(kc_tray_options_t opts) {
    struct kc_tray_options *o = (struct kc_tray_options *)opts;

    if (!o) {
        return;
    }
    free(o->icon);
    free(o->tooltip);
    free(o);
}

/**
 * Initialize a new tray context.
 * @param ctx_out Destination context pointer.
 * @param opts Opaque options handle from kc_tray_options_default.
 * @param callback Menu activation callback, or NULL.
 * @param userdata Caller data passed to the callback.
 * @return KC_TRAY_OK on success, KC_TRAY_ERROR on failure.
 */
int kc_tray_open(kc_tray_t **ctx_out, kc_tray_options_t opts, kc_tray_callback_t callback, void *userdata) {
    struct kc_tray_options *o;
    kc_tray_t *ctx;

    if (ctx_out) {
        *ctx_out = NULL;
    }
    if (!ctx_out || !opts) {
        return KC_TRAY_ERROR;
    }
    o = (struct kc_tray_options *)opts;

    ctx = (kc_tray_t *)calloc(1, sizeof(kc_tray_t));
    if (!ctx) {
        return KC_TRAY_ERROR;
    }
    ctx->callback = callback;
    ctx->userdata = userdata;

    if (o->icon) {
        ctx->icon = kc_tray_strdup(o->icon);
        if (!ctx->icon) {
            free(ctx);
            return KC_TRAY_ERROR;
        }
    }
    if (o->tooltip) {
        ctx->tooltip = kc_tray_strdup(o->tooltip);
        if (!ctx->tooltip) {
            free(ctx->icon);
            free(ctx);
            return KC_TRAY_ERROR;
        }
    }

    if (kc_tray_platform_init(ctx) != KC_TRAY_OK) {
        kc_tray_close(ctx);
        return KC_TRAY_ERROR;
    }

    *ctx_out = ctx;
    return KC_TRAY_OK;
}

/**
 * Replace the tray menu with a copied item array.
 * @param ctx Context pointer.
 * @param items Array of menu items, or NULL to clear.
 * @param count Number of items.
 * @return KC_TRAY_OK on success, KC_TRAY_ERROR on invalid input.
 */
int kc_tray_set_menu(kc_tray_t *ctx, const kc_tray_item_t *items, int count) {
    int i;

    if (!ctx) {
        return KC_TRAY_ERROR;
    }
    if (count < 0) {
        return KC_TRAY_ERROR;
    }
    if (count == 0) {
        kc_tray_menu_items_free(ctx);
        kc_tray_platform_menu_rebuild(ctx);
        return KC_TRAY_OK;
    }
    if (!items) {
        return KC_TRAY_ERROR;
    }
    for (i = 0; i < count; i++) {
        if (!kc_tray_item_valid(&items[i])) {
            return KC_TRAY_ERROR;
        }
    }
    if (kc_tray_menu_copy(ctx, items, count) != KC_TRAY_OK) {
        return KC_TRAY_ERROR;
    }
    kc_tray_platform_menu_rebuild(ctx);
    return KC_TRAY_OK;
}

/**
 * Enter the platform event loop until kc_tray_stop is requested.
 * @param ctx Context pointer.
 * @return KC_TRAY_OK on success, KC_TRAY_ERROR on failure.
 */
int kc_tray_run(kc_tray_t *ctx) {
    if (!ctx || ctx->closing) {
        return KC_TRAY_ERROR;
    }
    ctx->running = 1;
#if defined(_WIN32)
    if (!ctx->hwnd) {
        ctx->running = 0;
        kc_tray_set_error(ctx, "tray not initialized");
        return KC_TRAY_ERROR;
    }
    while (!ctx->stop_requested) {
        MSG message;
        BOOL result = GetMessageW(&message, NULL, 0, 0);
        if (result <= 0) {
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
#elif defined(__APPLE__)
    if (!ctx->ns_status_item) {
        ctx->running = 0;
        kc_tray_set_error(ctx, "tray not initialized");
        return KC_TRAY_ERROR;
    }
    while (!ctx->stop_requested) {
        NSEvent *event;
        @autoreleasepool {
            event = [NSApp nextEventMatchingMask:NSEventMaskAny
                untilDate:[NSDate distantFuture]
                inMode:NSDefaultRunLoopMode
                dequeue:YES];
            if (event) {
                [NSApp sendEvent:event];
            }
        }
    }
#else
    if (!ctx->status_icon) {
        ctx->running = 0;
        kc_tray_set_error(ctx, "tray not initialized");
        return KC_TRAY_ERROR;
    }
    if (!ctx->stop_requested) {
        ctx->loop = g_main_loop_new(NULL, FALSE);
        if (!ctx->loop) {
            ctx->running = 0;
            kc_tray_set_error(ctx, "main loop creation failed");
            return KC_TRAY_ERROR;
        }
        g_main_loop_run(ctx->loop);
        g_main_loop_unref(ctx->loop);
        ctx->loop = NULL;
    }
#endif
    ctx->running = 0;
    return KC_TRAY_OK;
}

/**
 * Request stop for a specific tray context.
 * @param ctx Context pointer.
 * @return KC_TRAY_OK on success, KC_TRAY_ERROR on failure.
 */
int kc_tray_stop(kc_tray_t *ctx) {
    if (!ctx) {
        return KC_TRAY_ERROR;
    }
    ctx->stop_requested = 1;
#if defined(_WIN32)
    if (ctx->running && ctx->hwnd) {
        PostMessageW(ctx->hwnd, KC_TRAY_STOP_MESSAGE, 0, 0);
    }
#elif defined(__APPLE__)
    if (ctx->running && NSApp) {
        NSEvent *wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
            location:NSZeroPoint modifierFlags:0 timestamp:0
            windowNumber:0 context:nil subtype:0 data1:0 data2:0];
        if (wake) {
            [NSApp postEvent:wake atStart:NO];
        }
    }
#else
    if (ctx->running && ctx->loop) {
        g_main_loop_quit(ctx->loop);
    }
#endif
    return KC_TRAY_OK;
}

/**
 * Release a tray context and all copied resources.
 * @param ctx Context pointer, or NULL.
 * @return KC_TRAY_OK.
 */
int kc_tray_close(kc_tray_t *ctx) {
    if (!ctx) {
        return KC_TRAY_OK;
    }
    if (!ctx->closing) {
        ctx->closing = 1;
        kc_tray_platform_release(ctx);
    }
    kc_tray_menu_items_free(ctx);
    free(ctx->icon);
    free(ctx->tooltip);
    free(ctx);
    return KC_TRAY_OK;
}

/**
 * Get the last error message from a context.
 * @param ctx Context pointer.
 * @return Error string, or NULL if no error.
 */
const char *kc_tray_get_error(const kc_tray_t *ctx) {
    if (!ctx || !ctx->error[0]) {
        return NULL;
    }
    return ctx->error;
}
