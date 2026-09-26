/** libtray.c - Native tray backends. License: GPL-3.0. */
#include "libtray.h"
#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#elif defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#else
#include <gtk/gtk.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#ifndef KC_TRAY_BUILD_VERSION
#define KC_TRAY_BUILD_VERSION 0ULL
#endif

struct kc_tray_item {
    kc_tray_t *tray;
    struct kc_tray_item *next;
    char *text;
    kc_tray_item_callback_t callback;
    void *userdata;
    unsigned id;
    int removed;
#if defined(__APPLE__)
    NSMenuItem *native;
#elif !defined(_WIN32)
    GtkWidget *native;
#endif
};
struct kc_tray {
    char *icon;
    char *tooltip;
    char error[256];
    kc_tray_item_t *items;
    kc_tray_item_t *tail;
    kc_tray_item_t *active;
    unsigned next_id;
    int closing;
    int free_on_exit;
#if defined(_WIN32)
    HANDLE thread;
    HANDLE ready;
    HWND window;
    HICON native_icon;
    HMENU menu;
    DWORD worker_id;
    int started;
#elif defined(__APPLE__)
    NSStatusItem *status;
    NSMenu *menu;
    id target;
#else
    GThread *thread;
    GMainContext *context;
    GMainLoop *loop;
    GMutex mutex;
    GCond cond;
    int started;
    GtkStatusIcon *status;
    GtkWidget *menu;
#endif
};

enum kc_op_kind { OP_INIT, OP_ICON, OP_TOOLTIP, OP_ADD, OP_TEXT, OP_REMOVE, OP_CLOSE };
typedef struct kc_op {
    kc_tray_t *tray;
    kc_tray_item_t *item;
    const char *value;
    enum kc_op_kind kind;
    int result;
#if !defined(_WIN32) && !defined(__APPLE__)
    GMutex mutex;
    GCond cond;
    int done;
#endif
} kc_op_t;

static char *kc_copy(const char *s) {
    size_t n;
    char *p;
    if (!s) return NULL;
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}
static void kc_error(kc_tray_t *t, const char *fmt, ...) {
    va_list ap;
    if (!t) return;
    va_start(ap, fmt);
    vsnprintf(t->error, sizeof(t->error), fmt, ap);
    va_end(ap);
}
static void kc_item_free(kc_tray_item_t *item) {
#if defined(__APPLE__)
    [item->native release];
#endif
    free(item->text);
    free(item);
}
static void kc_free(kc_tray_t *t) {
    kc_tray_item_t *p, *next;
    for (p = t->items; p; p = next) {
        next = p->next;
        kc_item_free(p);
    }
    free(t->icon);
    free(t->tooltip);
#if !defined(_WIN32) && !defined(__APPLE__)
    g_cond_clear(&t->cond);
    g_mutex_clear(&t->mutex);
#endif
    free(t);
}

#if defined(_WIN32)
#define KC_MSG_ICON (WM_APP + 40)
#define KC_MSG_OP (WM_APP + 41)
static wchar_t *kc_wide(const char *s) {
    int n;
    wchar_t *p;
    if (!s) return NULL;
    n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (!n) return NULL;
    p = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (p && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, p, n)) {
        free(p); return NULL;
    }
    return p;
}
static void kc_nid(kc_tray_t *t, NOTIFYICONDATAW *nid) {
    memset(nid, 0, sizeof(*nid));
    nid->cbSize = sizeof(*nid);
    nid->hWnd = t->window;
    nid->uID = 1;
}
static int kc_native_icon(kc_tray_t *t, const char *s) {
    NOTIFYICONDATAW nid;
    HICON icon = NULL;
    wchar_t *path = kc_wide(s);
    if (s) {
        if (!path) { kc_error(t, "invalid icon path"); return -1; }
        icon = (HICON)LoadImageW(NULL, path, IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
        free(path);
        if (!icon) { kc_error(t, "icon loading failed"); return -1; }
    }
    kc_nid(t, &nid);
    nid.uFlags = NIF_ICON;
    nid.hIcon = icon ? icon : LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        if (icon) DestroyIcon(icon);
        kc_error(t, "notification icon update failed"); return -1;
    }
    if (t->native_icon) DestroyIcon(t->native_icon);
    t->native_icon = icon;
    return 0;
}
static int kc_native_tip(kc_tray_t *t, const char *s) {
    NOTIFYICONDATAW nid;
    wchar_t *wide = kc_wide(s);
    if (s && !wide) { kc_error(t, "invalid tooltip"); return -1; }
    if (wide && wcslen(wide) >= sizeof(nid.szTip) / sizeof(wchar_t)) {
        free(wide); kc_error(t, "tooltip is too long"); return -1;
    }
    kc_nid(t, &nid);
    nid.uFlags = NIF_TIP;
    if (wide) wcsncpy(nid.szTip, wide, sizeof(nid.szTip) / sizeof(wchar_t) - 1);
    free(wide);
    if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        kc_error(t, "tooltip update failed"); return -1;
    }
    return 0;
}
static int kc_native_add(kc_tray_item_t *item) {
    kc_tray_t *t = item->tray;
    wchar_t *wide = kc_wide(item->text);
    BOOL ok;
    if (item->text && !wide) { kc_error(t, "invalid item text"); return -1; }
    ok = AppendMenuW(t->menu, item->text ? MF_STRING : MF_SEPARATOR,
                     item->id, wide);
    free(wide);
    if (!ok) { kc_error(t, "menu insertion failed"); return -1; }
    return 0;
}
static int kc_native_text(kc_tray_item_t *item, const char *s) {
    MENUITEMINFOW info;
    wchar_t *wide = kc_wide(s);
    BOOL ok;
    if (!wide) { kc_error(item->tray, "invalid item text"); return -1; }
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info); info.fMask = MIIM_STRING; info.dwTypeData = wide;
    ok = SetMenuItemInfoW(item->tray->menu, item->id, FALSE, &info);
    free(wide);
    if (!ok) { kc_error(item->tray, "menu update failed"); return -1; }
    return 0;
}
static void kc_native_remove(kc_tray_item_t *item) {
    unsigned position = 0;
    kc_tray_item_t *p;
    for (p = item->tray->items; p && p != item; p = p->next) position++;
    if (p) DeleteMenu(item->tray->menu, position, MF_BYPOSITION);
}
#elif defined(__APPLE__)
@interface KCTrayTarget : NSObject { @public kc_tray_t *tray; }
- (void)activate:(id)sender;
@end
@implementation KCTrayTarget
- (void)activate:(id)sender {
    kc_tray_item_t *item = (kc_tray_item_t *)[(NSMenuItem *)sender representedObject].pointerValue;
    kc_tray_t *owner = tray;
    [self retain];
    if (tray && !tray->closing && item && !item->removed && item->callback) {
        tray->active = item;
        item->callback(item, item->userdata);
        /* The public pointer ceases to exist immediately after its callback. */
        if (item->removed) kc_item_free(item);
        if (owner->active == item) owner->active = NULL;
    }
    [self release];
    if (owner && owner->free_on_exit) kc_free(owner);
}
@end
static int kc_native_icon(kc_tray_t *t, const char *s) {
    NSImage *image = nil;
    if (s) {
        NSString *name = [NSString stringWithUTF8String:s];
        if (!name) { kc_error(t, "invalid icon path"); return -1; }
        image = strchr(s, '/') ? [[[NSImage alloc] initWithContentsOfFile:name] autorelease]
                               : [NSImage imageNamed:name];
        if (!image) { kc_error(t, "icon loading failed"); return -1; }
        image = [[image copy] autorelease];
        [image setSize:NSMakeSize(18, 18)];
    } else image = [NSImage imageNamed:NSImageNameApplicationIcon];
    t->status.button.image = image;
    return 0;
}
static int kc_native_tip(kc_tray_t *t, const char *s) {
    NSString *tip = s ? [NSString stringWithUTF8String:s] : nil;
    if (s && !tip) { kc_error(t, "invalid tooltip"); return -1; }
    t->status.button.toolTip = tip;
    return 0;
}
static int kc_native_add(kc_tray_item_t *item) {
    kc_tray_t *t = item->tray;
    NSMenuItem *native;
    if (!item->text) native = [NSMenuItem separatorItem];
    else {
        NSString *title = [NSString stringWithUTF8String:item->text];
        if (!title) { kc_error(t, "invalid item text"); return -1; }
        native = [[[NSMenuItem alloc] initWithTitle:title action:@selector(activate:)
                                         keyEquivalent:@""] autorelease];
        native.target = t->target;
        native.representedObject = [NSValue valueWithPointer:item];
    }
    [t->menu addItem:native];
    item->native = [native retain];
    return 0;
}
static int kc_native_text(kc_tray_item_t *item, const char *s) {
    NSString *title = [NSString stringWithUTF8String:s];
    if (!title) { kc_error(item->tray, "invalid item text"); return -1; }
    item->native.title = title;
    return 0;
}
static void kc_native_remove(kc_tray_item_t *item) {
    [item->tray->menu removeItem:item->native];
    [item->native release]; item->native = nil;
}
static int kc_native_init(kc_tray_t *t) {
    [NSApplication sharedApplication];
    t->status = [[[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength] retain];
    if (!t->status) { kc_error(t, "status item creation failed"); return -1; }
    t->menu = [[NSMenu alloc] init];
    t->target = [[KCTrayTarget alloc] init];
    ((KCTrayTarget *)t->target)->tray = t;
    t->status.menu = t->menu;
    return 0;
}
#else
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
static int kc_native_icon(kc_tray_t *t, const char *s) {
    const char *previous = t->icon;
    if (!s) { gtk_status_icon_set_from_icon_name(t->status, "emblem-system"); return 0; }
    if (strchr(s, '/')) gtk_status_icon_set_from_file(t->status, s);
    else gtk_status_icon_set_from_icon_name(t->status, s);
    if (!gtk_status_icon_get_storage_type(t->status) ||
        (strchr(s, '/') && !gtk_status_icon_get_pixbuf(t->status))) {
        if (previous) {
            if (strchr(previous, '/')) gtk_status_icon_set_from_file(t->status, previous);
            else gtk_status_icon_set_from_icon_name(t->status, previous);
        } else gtk_status_icon_set_from_icon_name(t->status, "emblem-system");
        kc_error(t, "icon loading failed"); return -1;
    }
    return 0;
}
static int kc_native_tip(kc_tray_t *t, const char *s) {
    gtk_status_icon_set_tooltip_text(t->status, s);
    return 0;
}
static gboolean kc_gtk_free_later(gpointer data);
static void kc_gtk_activate(GtkWidget *widget, gpointer data) {
    kc_tray_item_t *item = (kc_tray_item_t *)data;
    kc_tray_t *t = item->tray;
    (void)widget;
    if (t->closing || item->removed || !item->callback) return;
    t->active = item;
    item->callback(item, item->userdata);
    if (item->removed) kc_item_free(item);
    t->active = NULL;
    if (t->free_on_exit) {
        GSource *source = g_idle_source_new();
        g_source_set_callback(source, (GSourceFunc)kc_gtk_free_later, t, NULL);
        g_source_attach(source, t->context);
        g_source_unref(source);
    }
}
static void kc_gtk_popup(GtkStatusIcon *icon, guint button, guint time, gpointer data) {
    kc_tray_t *t = (kc_tray_t *)data;
    if (!t->closing) gtk_menu_popup(GTK_MENU(t->menu), NULL, NULL,
        gtk_status_icon_position_menu, icon, button, time);
}
static void kc_gtk_click(GtkStatusIcon *icon, gpointer data) {
    kc_gtk_popup(icon, 0, gtk_get_current_event_time(), data);
}
static int kc_native_add(kc_tray_item_t *item) {
    GtkWidget *native = item->text ? gtk_menu_item_new_with_label(item->text)
                                   : gtk_separator_menu_item_new();
    if (!native) { kc_error(item->tray, "menu insertion failed"); return -1; }
    if (item->text) g_signal_connect(native, "activate", G_CALLBACK(kc_gtk_activate), item);
    gtk_menu_shell_append(GTK_MENU_SHELL(item->tray->menu), native);
    gtk_widget_show(native);
    item->native = native;
    return 0;
}
static int kc_native_text(kc_tray_item_t *item, const char *s) {
    gtk_menu_item_set_label(GTK_MENU_ITEM(item->native), s);
    return 0;
}
static void kc_native_remove(kc_tray_item_t *item) {
    g_signal_handlers_disconnect_by_data(item->native, item);
    gtk_widget_destroy(item->native);
    item->native = NULL;
}
static int kc_native_init(kc_tray_t *t) {
    t->status = gtk_status_icon_new_from_icon_name("emblem-system");
    t->menu = gtk_menu_new();
    if (!t->status || !t->menu) { kc_error(t, "GTK tray creation failed"); return -1; }
    g_signal_connect(t->status, "popup-menu", G_CALLBACK(kc_gtk_popup), t);
    g_signal_connect(t->status, "activate", G_CALLBACK(kc_gtk_click), t);
    gtk_status_icon_set_visible(t->status, TRUE);
    return 0;
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#endif

static void kc_native_close(kc_tray_t *t) {
#if defined(_WIN32)
    NOTIFYICONDATAW nid;
    if (t->window) {
        kc_nid(t, &nid); Shell_NotifyIconW(NIM_DELETE, &nid);
        DestroyWindow(t->window); t->window = NULL;
    }
    if (t->menu) DestroyMenu(t->menu);
    if (t->native_icon) DestroyIcon(t->native_icon);
#elif defined(__APPLE__)
    if (t->status) {
        ((KCTrayTarget *)t->target)->tray = NULL;
        t->status.menu = nil;
        [[NSStatusBar systemStatusBar] removeStatusItem:t->status];
        [t->status release]; t->status = nil;
    }
    [t->menu release]; [t->target release];
#else
    if (t->status) {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        gtk_status_icon_set_visible(t->status, FALSE);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        g_signal_handlers_disconnect_by_data(t->status, t);
        g_object_unref(t->status); t->status = NULL;
    }
    if (t->menu) { gtk_widget_destroy(t->menu); t->menu = NULL; }
#endif
}
static void kc_unlink(kc_tray_item_t *item) {
    kc_tray_t *t = item->tray;
    kc_tray_item_t **p = &t->items;
    while (*p && *p != item) p = &(*p)->next;
    if (*p) *p = item->next;
    if (t->tail == item) {
        t->tail = NULL;
        for (kc_tray_item_t *q = t->items; q; q = q->next) t->tail = q;
    }
    item->next = NULL;
}
static int kc_execute(kc_op_t *op) {
    kc_tray_t *t = op->tray;
    kc_tray_item_t *item = op->item;
    char *copy;
    int result;
    if (t->closing) return -1;
    t->error[0] = 0;
    switch (op->kind) {
    case OP_INIT:
#if !defined(_WIN32) && !defined(__APPLE__)
        result = kc_native_init(t);
        if (result) kc_native_close(t);
        return result;
#else
        return -1;
#endif
    case OP_ICON:
    case OP_TOOLTIP:
    case OP_TEXT:
        if (op->kind == OP_TEXT && (!op->value || !*op->value || !item || item->removed)) {
            kc_error(t, "invalid item text"); return -1;
        }
        copy = kc_copy(op->value);
        if (op->value && !copy) { kc_error(t, "allocation failed"); return -1; }
        result = op->kind == OP_ICON ? kc_native_icon(t, op->value) :
                 op->kind == OP_TOOLTIP ? kc_native_tip(t, op->value) :
                 kc_native_text(item, op->value);
        if (result) { free(copy); return -1; }
        if (op->kind == OP_ICON) { free(t->icon); t->icon = copy; }
        else if (op->kind == OP_TOOLTIP) { free(t->tooltip); t->tooltip = copy; }
        else { free(item->text); item->text = copy; }
        return 0;
    case OP_ADD:
        if (kc_native_add(item)) return -1;
        item->tray = t;
        if (t->tail) t->tail->next = item;
        else t->items = item;
        t->tail = item;
        return 0;
    case OP_REMOVE:
        if (!item || item->removed) return -1;
        item->removed = 1;
        kc_native_remove(item);
        kc_unlink(item);
        if (t->active != item) kc_item_free(item);
        return 0;
    case OP_CLOSE:
        t->closing = 1;
        kc_native_close(t);
#if defined(_WIN32)
        PostQuitMessage(0);
#endif
        return 0;
    }
    return -1;
}

#if defined(_WIN32)
static LRESULT CALLBACK kc_window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    kc_tray_t *t;
    if (msg == WM_NCCREATE) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
            (LONG_PTR)((CREATESTRUCTW *)lp)->lpCreateParams);
    }
    t = (kc_tray_t *)(intptr_t)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (t && msg == KC_MSG_OP) {
        kc_op_t *op = (kc_op_t *)(intptr_t)lp;
        op->result = kc_execute(op);
        return 0;
    }
    if (t && msg == KC_MSG_ICON && wp == 1 && !t->closing &&
        (lp == WM_LBUTTONUP || lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU)) {
        POINT pt;
        UINT id;
        if (!GetCursorPos(&pt)) return 0;
        SetForegroundWindow(hwnd);
        id = TrackPopupMenu(t->menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                            pt.x, pt.y, 0, hwnd, NULL);
        if (id && !t->closing) {
            kc_tray_item_t *item;
            for (item = t->items; item && item->id != id; item = item->next) {}
            if (item && item->callback) {
                t->active = item;
                item->callback(item, item->userdata);
                if (item->removed) kc_item_free(item);
                t->active = NULL;
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
static DWORD WINAPI kc_windows_worker(LPVOID data) {
    kc_tray_t *t = (kc_tray_t *)data;
    WNDCLASSW wc;
    NOTIFYICONDATAW nid;
    MSG msg;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = kc_window_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"KCTrayWindowV2";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) goto fail;
    t->menu = CreatePopupMenu();
    t->window = CreateWindowExW(0, wc.lpszClassName, L"tray", 0, 0, 0, 0, 0,
                               HWND_MESSAGE, NULL, wc.hInstance, t);
    if (!t->menu || !t->window) goto fail;
    kc_nid(t, &nid);
    nid.uFlags = NIF_ICON | NIF_MESSAGE;
    nid.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    nid.uCallbackMessage = KC_MSG_ICON;
    if (!Shell_NotifyIconW(NIM_ADD, &nid)) goto fail;
    t->started = 1;
    SetEvent(t->ready);
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    if (t->free_on_exit) {
        CloseHandle(t->thread);
        kc_free(t);
    }
    return 0;
fail:
    kc_error(t, "Windows tray initialization failed");
    kc_native_close(t);
    SetEvent(t->ready);
    return 1;
}
static int kc_dispatch(kc_op_t *op) {
    kc_tray_t *t = op->tray;
    if (t->closing) return -1;
    if (GetCurrentThreadId() == t->worker_id) return kc_execute(op);
    SendMessageW(t->window, KC_MSG_OP, 0, (LPARAM)op);
    return op->result;
}
#elif defined(__APPLE__)
static int kc_dispatch(kc_op_t *op) {
    if (![NSThread isMainThread]) { kc_error(op->tray, "AppKit requires the main thread"); return -1; }
    @autoreleasepool { return kc_execute(op); }
}
#else
static gsize kc_gtk_once;
static GMainContext *kc_gtk_context;
static GThread *kc_gtk_thread;
static GMutex kc_gtk_mutex;
static GCond kc_gtk_cond;
static int kc_gtk_ready, kc_gtk_started;
static gboolean kc_gtk_free_later(gpointer data) {
    kc_free((kc_tray_t *)data);
    return G_SOURCE_REMOVE;
}
static gboolean kc_gtk_dispatch(gpointer data) {
    kc_op_t *op = (kc_op_t *)data;
    int result = kc_execute(op);
    g_mutex_lock(&op->mutex);
    op->result = result;
    op->done = 1;
    g_cond_signal(&op->cond);
    g_mutex_unlock(&op->mutex);
    return G_SOURCE_REMOVE;
}
static int kc_dispatch(kc_op_t *op) {
    kc_tray_t *t = op->tray;
    int result;
    GSource *source;
    if (t->closing) return -1;
    if (g_thread_self() == t->thread) return kc_execute(op);
    g_mutex_init(&op->mutex); g_cond_init(&op->cond);
    op->done = 0;
    g_mutex_lock(&op->mutex);
    source = g_idle_source_new();
    g_source_set_callback(source, kc_gtk_dispatch, op, NULL);
    g_source_attach(source, t->context);
    g_source_unref(source);
    while (!op->done) g_cond_wait(&op->cond, &op->mutex);
    result = op->result;
    g_mutex_unlock(&op->mutex);
    g_cond_clear(&op->cond); g_mutex_clear(&op->mutex);
    return result;
}
static gpointer kc_linux_worker(gpointer data) {
    GMainLoop *loop;
    (void)data;
    kc_gtk_started = gtk_init_check(NULL, NULL);
    g_mutex_lock(&kc_gtk_mutex);
    kc_gtk_ready = 1;
    g_cond_signal(&kc_gtk_cond);
    g_mutex_unlock(&kc_gtk_mutex);
    if (!kc_gtk_started) return NULL;
    loop = g_main_loop_new(kc_gtk_context, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return NULL;
}
static int kc_gtk_service(void) {
    if (g_once_init_enter(&kc_gtk_once)) {
        g_mutex_init(&kc_gtk_mutex);
        g_cond_init(&kc_gtk_cond);
        kc_gtk_context = g_main_context_default();
        g_mutex_lock(&kc_gtk_mutex);
        kc_gtk_thread = g_thread_new("kc-tray", kc_linux_worker, NULL);
        if (kc_gtk_thread) {
            while (!kc_gtk_ready) g_cond_wait(&kc_gtk_cond, &kc_gtk_mutex);
        }
        g_mutex_unlock(&kc_gtk_mutex);
        g_once_init_leave(&kc_gtk_once, 1);
    }
    return kc_gtk_started ? 0 : -1;
}
#endif

uint64_t kc_tray_version(void) { return (uint64_t)KC_TRAY_BUILD_VERSION; }
int kc_tray_open(kc_tray_t **out, const kc_tray_options_t *options) {
    kc_tray_t *t;
    if (out) *out = NULL;
    if (!out) return -1;
#if defined(__APPLE__)
    if (![NSThread isMainThread]) return -1;
#endif
    t = (kc_tray_t *)calloc(1, sizeof(*t));
    if (!t) return -1;
#if defined(_WIN32)
    t->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!t->ready) { free(t); return -1; }
    t->thread = CreateThread(NULL, 0, kc_windows_worker, t, 0, &t->worker_id);
    if (!t->thread) { CloseHandle(t->ready); free(t); return -1; }
    WaitForSingleObject(t->ready, INFINITE);
    CloseHandle(t->ready); t->ready = NULL;
    if (!t->started) { WaitForSingleObject(t->thread, INFINITE); CloseHandle(t->thread); kc_free(t); return -1; }
#elif defined(__APPLE__)
    @autoreleasepool {
        if (kc_native_init(t)) { kc_native_close(t); kc_free(t); return -1; }
    }
#else
    g_mutex_init(&t->mutex); g_cond_init(&t->cond);
    if (kc_gtk_service()) { kc_free(t); return -1; }
    t->context = kc_gtk_context;
    t->thread = kc_gtk_thread;
    {
        kc_op_t init = {0};
        init.tray = t; init.kind = OP_INIT;
        if (kc_dispatch(&init)) { kc_free(t); return -1; }
    }
#endif
    *out = t;
    if (options && ((options->icon && kc_tray_set_icon(t, options->icon)) ||
                    (options->tooltip && kc_tray_set_tooltip(t, options->tooltip)))) {
        kc_tray_close(t); *out = NULL; return -1;
    }
    return 0;
}
int kc_tray_set_icon(kc_tray_t *t, const char *s) {
    kc_op_t op = {0};
    if (!t) return -1;
    op.tray = t; op.kind = OP_ICON; op.value = s;
    return kc_dispatch(&op);
}
const char *kc_tray_get_icon(const kc_tray_t *t) { return t ? t->icon : NULL; }
int kc_tray_set_tooltip(kc_tray_t *t, const char *s) {
    kc_op_t op = {0};
    if (!t) return -1;
    op.tray = t; op.kind = OP_TOOLTIP; op.value = s;
    return kc_dispatch(&op);
}
const char *kc_tray_get_tooltip(const kc_tray_t *t) { return t ? t->tooltip : NULL; }
static int kc_add(kc_tray_t *t, kc_tray_item_t **out, const char *text,
                  kc_tray_item_callback_t cb, void *userdata) {
    kc_tray_item_t *item;
    kc_op_t op = {0};
    if (out) *out = NULL;
    if (!t || !out || (text && !*text) || t->closing) return -1;
    item = (kc_tray_item_t *)calloc(1, sizeof(*item));
    if (!item) { kc_error(t, "allocation failed"); return -1; }
    item->tray = t; item->text = kc_copy(text);
    if (text && !item->text) { kc_item_free(item); kc_error(t, "allocation failed"); return -1; }
    item->callback = cb; item->userdata = userdata;
    item->id = ++t->next_id;
    op.tray = t; op.item = item; op.kind = OP_ADD;
    if (kc_dispatch(&op)) { kc_item_free(item); return -1; }
    *out = item;
    return 0;
}
int kc_tray_add_item(kc_tray_t *t, kc_tray_item_t **out, const char *text,
                     kc_tray_item_callback_t cb, void *userdata) {
    if (!text || !*text) { if (out) *out = NULL; if (t) kc_error(t, "invalid item text"); return -1; }
    return kc_add(t, out, text, cb, userdata);
}
int kc_tray_add_separator(kc_tray_t *t, kc_tray_item_t **out) {
    return kc_add(t, out, NULL, NULL, NULL);
}
int kc_tray_item_set_text(kc_tray_item_t *item, const char *text) {
    kc_op_t op = {0};
    if (!item || !item->text) return -1;
    op.tray = item->tray; op.item = item; op.value = text; op.kind = OP_TEXT;
    return kc_dispatch(&op);
}
const char *kc_tray_item_get_text(const kc_tray_item_t *item) {
    return item ? item->text : NULL;
}
void kc_tray_item_remove(kc_tray_item_t *item) {
    kc_op_t op = {0};
    if (!item) return;
    op.tray = item->tray; op.item = item; op.kind = OP_REMOVE;
    (void)kc_dispatch(&op);
}
const char *kc_tray_get_error(const kc_tray_t *t) {
    return t && t->error[0] ? t->error : NULL;
}
void kc_tray_close(kc_tray_t *t) {
    kc_op_t op = {0};
    int in_callback;
    if (!t) return;
    in_callback = t->active != NULL;
    if (in_callback) t->free_on_exit = 1;
    op.tray = t; op.kind = OP_CLOSE;
    (void)kc_dispatch(&op);
    if (in_callback) return; /* UI worker releases after the callback unwinds. */
#if defined(_WIN32)
    WaitForSingleObject(t->thread, INFINITE);
    CloseHandle(t->thread);
#endif
    kc_free(t);
}
