/**
 * libwvw.c - Core implementation for the wvw library.
 * Summary: Implements native WebView backends.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifdef _WIN32

#define CINTERFACE
#define COBJMACROS
#include "libwvw.h"
#include "parson.h"

#include <WebView2.h>
#include <shlobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#define KC_WVW_CLOSE_MESSAGE (WM_APP + 1)
#define KC_WVW_BRIDGE_MAX_MESSAGE 65536

typedef HRESULT (STDAPICALLTYPE *kc_wvw_create_environment_fn)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions *, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *);
typedef HRESULT (STDAPICALLTYPE *kc_wvw_get_version_fn)(PCWSTR, LPWSTR *);

typedef enum {
    KC_WVW_INIT_PENDING,
    KC_WVW_INIT_READY,
    KC_WVW_INIT_FAILED,
} kc_wvw_init_state_t;

typedef struct {
    char *url;
    char *title;
    char *background;
    int width, height, posx, posy;
    int has_posx, has_posy;
    int fullscreen, borderless, always_on_top, click_through, no_focus;
} kc_wvw_config_t;
typedef struct {
    int width;
    int height;
    int minimized;
    int maximized;
    int fullscreen;
    int visible;
} kc_wvw_window_state_t;


typedef enum {
    KC_WVW_OP_NAVIGATE, KC_WVW_OP_ADD_INIT_SCRIPT, KC_WVW_OP_ENABLE_BRIDGE,
    KC_WVW_OP_POST_BRIDGE_EVENT, KC_WVW_OP_HIDE, KC_WVW_OP_SHOW,
    KC_WVW_OP_MINIMIZE, KC_WVW_OP_MAXIMIZE, KC_WVW_OP_RESTORE,
    KC_WVW_OP_SET_TITLE, KC_WVW_OP_GET_TITLE, KC_WVW_OP_SET_SIZE,
    KC_WVW_OP_GET_SIZE, KC_WVW_OP_IS_VISIBLE, KC_WVW_OP_IS_MINIMIZED,
    KC_WVW_OP_IS_MAXIMIZED, KC_WVW_OP_IS_FULLSCREEN
} kc_wvw_op_kind_t;

typedef struct {
    kc_wvw_op_kind_t kind;
    const char *text;
    const kc_wvw_bridge_options_t *bridge;
    int a, b;
    int *out_a, *out_b;
    const char *out_text;
    int result;
} kc_wvw_op_t;

static int kc_wvw_execute_op(kc_wvw_t *ctx, kc_wvw_op_t *op);

typedef struct {
    char *url;
    char *title;
    char *background;
    int width, height, posx, posy;
    int has_posx, has_posy;
    int fullscreen, borderless, always_on_top, click_through, no_focus;
} kc_wvw_config_t;
typedef struct {
    int width;
    int height;
    int minimized;
    int maximized;
    int fullscreen;
    int visible;
} kc_wvw_window_state_t;


typedef enum {
    KC_WVW_OP_NAVIGATE, KC_WVW_OP_ADD_INIT_SCRIPT, KC_WVW_OP_ENABLE_BRIDGE,
    KC_WVW_OP_POST_BRIDGE_EVENT, KC_WVW_OP_HIDE, KC_WVW_OP_SHOW,
    KC_WVW_OP_MINIMIZE, KC_WVW_OP_MAXIMIZE, KC_WVW_OP_RESTORE,
    KC_WVW_OP_SET_TITLE, KC_WVW_OP_GET_TITLE, KC_WVW_OP_SET_SIZE,
    KC_WVW_OP_GET_SIZE, KC_WVW_OP_IS_VISIBLE, KC_WVW_OP_IS_MINIMIZED,
    KC_WVW_OP_IS_MAXIMIZED, KC_WVW_OP_IS_FULLSCREEN
} kc_wvw_op_kind_t;

typedef struct {
    kc_wvw_op_kind_t kind;
    const char *text;
    const kc_wvw_bridge_options_t *bridge;
    int a, b;
    int *out_a, *out_b;
    const char *out_text;
    int result;
} kc_wvw_op_t;

static int kc_wvw_execute_op(kc_wvw_t *ctx, kc_wvw_op_t *op);

typedef struct {
    char **methods;
    size_t method_count;
    kc_wvw_bridge_callback_t callback;
    void *userdata;
    int allow_file;
    int allow_data;
    int allow_localhost;
    int enabled;
} kc_wvw_bridge_state_t;

struct kc_wvw {
    kc_wvw_config_t opts;
    volatile LONG ref_count;
    int running, closing, com_initialized, started, free_on_exit;
    DWORD worker_id;
    HANDLE thread, ready, closed_event;
    HWND hwnd;
    HBRUSH background_brush;
    HINSTANCE hinstance;
    HMODULE loader;
    ICoreWebView2Environment *environment;
    ICoreWebView2Controller *controller;
    ICoreWebView2 *webview;
    kc_wvw_init_state_t init_state;
    char *pending_url;
    kc_wvw_bridge_state_t bridge;
    char error[256];
};

static int kc_wvw_bridge_url_trusted(kc_wvw_t *ctx, kc_wvw_bridge_state_t *bridge, const char *url);
static void kc_wvw_bridge_state_free(kc_wvw_bridge_state_t *bridge);
static int kc_wvw_bridge_state_copy(kc_wvw_bridge_state_t *dst, const kc_wvw_bridge_options_t *src);
static char *kc_wvw_bridge_bootstrap_script(kc_wvw_bridge_state_t *bridge, const char *sender_expr, const char *receiver_setup);
static int kc_wvw_bridge_post_json(kc_wvw_t *ctx, const char *json);
static char *kc_wvw_bridge_dispatch_request(kc_wvw_t *ctx, const char *json);
static void kc_wvw_context_add_ref(kc_wvw_t *ctx);
static void kc_wvw_context_release(kc_wvw_t *ctx);
static void kc_wvw_context_destroy(kc_wvw_t *ctx) {
    if (!ctx) return;
    free(ctx->pending_url);
    kc_wvw_bridge_state_free(&ctx->bridge);
    kc_wvw_config_free(&ctx->opts);
    if (ctx->closed_event) CloseHandle(ctx->closed_event);
    free(ctx);
}

/**
 * Retains one context while an asynchronous operation may use it.
 * @param ctx Window context.
 * @return None.
 */
static void kc_wvw_context_add_ref(kc_wvw_t *ctx) {
    if (ctx) {
        InterlockedIncrement(&ctx->ref_count);
    }
}

/**
 * Releases all context-owned native and heap resources.
 * @param ctx Window context.
 * @return None.
 */
static void kc_wvw_context_destroy(kc_wvw_t *ctx) {
    if (!ctx) {
        return;
    }
    if (ctx->controller) {
        ICoreWebView2Controller_Close(ctx->controller);
    }
    if (ctx->webview) {
        ICoreWebView2_Release(ctx->webview);
    }
    if (ctx->controller) {
        ICoreWebView2Controller_Release(ctx->controller);
    }
    if (ctx->environment) {
        ICoreWebView2Environment_Release(ctx->environment);
    }
    if (ctx->hwnd && IsWindow(ctx->hwnd)) {
        DestroyWindow(ctx->hwnd);
    }
    if (ctx->loader) {
        FreeLibrary(ctx->loader);
    }
    if (ctx->background_brush) {
        DeleteObject(ctx->background_brush);
    }
    free(ctx->pending_url);
    kc_wvw_bridge_state_free(&ctx->bridge);
    kc_wvw_config_free(&ctx->opts);
    if (ctx->com_initialized) {
        CoUninitialize();
    }
    free(ctx);
}

/**
 * Releases one context reference and destroys it after the final release.
 * @param ctx Window context.
 * @return None.
 */
static void kc_wvw_context_release(kc_wvw_t *ctx) {
    if (ctx && InterlockedDecrement(&ctx->ref_count) == 0) {
        kc_wvw_context_destroy(ctx);
    }
}

#ifndef KC_WVW_BUILD_VERSION
#define KC_WVW_BUILD_VERSION 0
#endif

typedef struct kc_wvw_environment_handler {
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler iface;
    volatile LONG ref_count;
    kc_wvw_t *ctx;
} kc_wvw_environment_handler_t;

typedef struct kc_wvw_controller_handler {
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler iface;
    volatile LONG ref_count;
    kc_wvw_t *ctx;
} kc_wvw_controller_handler_t;

typedef struct kc_wvw_script_handler {
    ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler iface;
    volatile LONG ref_count;
} kc_wvw_script_handler_t;

typedef struct kc_wvw_navigation_handler {
    ICoreWebView2NavigationStartingEventHandler iface;
    volatile LONG ref_count;
    kc_wvw_t *ctx;
} kc_wvw_navigation_handler_t;

typedef struct kc_wvw_message_handler {
    ICoreWebView2WebMessageReceivedEventHandler iface;
    volatile LONG ref_count;
    kc_wvw_t *ctx;
} kc_wvw_message_handler_t;

static HRESULT STDMETHODCALLTYPE kc_wvw_environment_query_interface(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *iface, REFIID riid, void **object);
static ULONG STDMETHODCALLTYPE kc_wvw_environment_add_ref(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *iface);
static ULONG STDMETHODCALLTYPE kc_wvw_environment_release(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *iface);
static HRESULT STDMETHODCALLTYPE kc_wvw_environment_invoke(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *iface, HRESULT error_code, ICoreWebView2Environment *result);
static HRESULT STDMETHODCALLTYPE kc_wvw_controller_query_interface(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *iface, REFIID riid, void **object);
static ULONG STDMETHODCALLTYPE kc_wvw_controller_add_ref(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *iface);
static ULONG STDMETHODCALLTYPE kc_wvw_controller_release(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *iface);
static HRESULT STDMETHODCALLTYPE kc_wvw_controller_invoke(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *iface, HRESULT error_code, ICoreWebView2Controller *result);
static HRESULT STDMETHODCALLTYPE kc_wvw_script_query_interface(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *iface, REFIID riid, void **object);
static ULONG STDMETHODCALLTYPE kc_wvw_script_add_ref(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *iface);
static ULONG STDMETHODCALLTYPE kc_wvw_script_release(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *iface);
static HRESULT STDMETHODCALLTYPE kc_wvw_script_invoke(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *iface, HRESULT error_code, PCWSTR result);
static HRESULT STDMETHODCALLTYPE kc_wvw_navigation_query_interface(ICoreWebView2NavigationStartingEventHandler *iface, REFIID riid, void **object);
static ULONG STDMETHODCALLTYPE kc_wvw_navigation_add_ref(ICoreWebView2NavigationStartingEventHandler *iface);
static ULONG STDMETHODCALLTYPE kc_wvw_navigation_release(ICoreWebView2NavigationStartingEventHandler *iface);
static HRESULT STDMETHODCALLTYPE kc_wvw_navigation_invoke(ICoreWebView2NavigationStartingEventHandler *iface, ICoreWebView2 *sender, ICoreWebView2NavigationStartingEventArgs *args);
static HRESULT STDMETHODCALLTYPE kc_wvw_message_query_interface(ICoreWebView2WebMessageReceivedEventHandler *iface, REFIID riid, void **object);
static ULONG STDMETHODCALLTYPE kc_wvw_message_add_ref(ICoreWebView2WebMessageReceivedEventHandler *iface);
static ULONG STDMETHODCALLTYPE kc_wvw_message_release(ICoreWebView2WebMessageReceivedEventHandler *iface);
static HRESULT STDMETHODCALLTYPE kc_wvw_message_invoke(ICoreWebView2WebMessageReceivedEventHandler *iface, ICoreWebView2 *sender, ICoreWebView2WebMessageReceivedEventArgs *args);
static wchar_t *kc_wvw_utf16_from_utf8(const char *text);
static int kc_wvw_parse_background_color(const char *text, BYTE *out_a, BYTE *out_r, BYTE *out_g, BYTE *out_b);

static ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl kc_wvw_environment_vtbl = {
    kc_wvw_environment_query_interface,
    kc_wvw_environment_add_ref,
    kc_wvw_environment_release,
    kc_wvw_environment_invoke
};

static ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl kc_wvw_controller_vtbl = {
    kc_wvw_controller_query_interface,
    kc_wvw_controller_add_ref,
    kc_wvw_controller_release,
    kc_wvw_controller_invoke
};

static ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandlerVtbl kc_wvw_script_vtbl = {
    kc_wvw_script_query_interface,
    kc_wvw_script_add_ref,
    kc_wvw_script_release,
    kc_wvw_script_invoke
};

static ICoreWebView2NavigationStartingEventHandlerVtbl kc_wvw_navigation_vtbl = {
    kc_wvw_navigation_query_interface,
    kc_wvw_navigation_add_ref,
    kc_wvw_navigation_release,
    kc_wvw_navigation_invoke
};

static ICoreWebView2WebMessageReceivedEventHandlerVtbl kc_wvw_message_vtbl = {
    kc_wvw_message_query_interface,
    kc_wvw_message_add_ref,
    kc_wvw_message_release,
    kc_wvw_message_invoke
};

/**
 * Detects whether the process is running under Wine.
 * @return Non-zero under Wine, otherwise zero.
 */
static int kc_wvw_is_wine(void) {
    HMODULE ntdll;

    ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll && GetProcAddress(ntdll, "wine_get_version");
}

/**
 * Allocates a WebView2 environment completion handler.
 * @param ctx Window context.
 * @return Newly allocated handler or NULL.
 */
static kc_wvw_environment_handler_t *kc_wvw_environment_handler_new(kc_wvw_t *ctx) {
    kc_wvw_environment_handler_t *handler;

    if (!ctx) {
        return NULL;
    }
    handler = (kc_wvw_environment_handler_t *)calloc(1, sizeof(*handler));
    if (!handler) {
        return NULL;
    }

    handler->iface.lpVtbl = &kc_wvw_environment_vtbl;
    handler->ref_count = 1;
    handler->ctx = ctx;
    kc_wvw_context_add_ref(ctx);
    return handler;
}

/**
 * Allocates a WebView2 controller completion handler.
 * @param ctx Window context.
 * @return Newly allocated handler or NULL.
 */
static kc_wvw_controller_handler_t *kc_wvw_controller_handler_new(kc_wvw_t *ctx) {
    kc_wvw_controller_handler_t *handler;

    if (!ctx) {
        return NULL;
    }
    handler = (kc_wvw_controller_handler_t *)calloc(1, sizeof(*handler));
    if (!handler) {
        return NULL;
    }

    handler->iface.lpVtbl = &kc_wvw_controller_vtbl;
    handler->ref_count = 1;
    handler->ctx = ctx;
    kc_wvw_context_add_ref(ctx);
    return handler;
}

/**
 * Allocates one script completion handler.
 * @return Newly allocated handler or NULL.
 */
static kc_wvw_script_handler_t *kc_wvw_script_handler_new(void) {
    kc_wvw_script_handler_t *handler;

    handler = (kc_wvw_script_handler_t *)calloc(1, sizeof(*handler));
    if (!handler) {
        return NULL;
    }

    handler->iface.lpVtbl = &kc_wvw_script_vtbl;
    handler->ref_count = 1;
    return handler;
}

/**
 * Allocates one navigation event handler.
 * @param ctx Window context.
 * @return Newly allocated handler or NULL.
 */
static kc_wvw_navigation_handler_t *kc_wvw_navigation_handler_new(kc_wvw_t *ctx) {
    kc_wvw_navigation_handler_t *handler;

    handler = (kc_wvw_navigation_handler_t *)calloc(1, sizeof(*handler));
    if (!handler) {
        return NULL;
    }

    handler->iface.lpVtbl = &kc_wvw_navigation_vtbl;
    handler->ref_count = 1;
    handler->ctx = ctx;
    return handler;
}

/**
 * Allocates one web message event handler.
 * @param ctx Window context.
 * @return Newly allocated handler or NULL.
 */
static kc_wvw_message_handler_t *kc_wvw_message_handler_new(kc_wvw_t *ctx) {
    kc_wvw_message_handler_t *handler;

    handler = (kc_wvw_message_handler_t *)calloc(1, sizeof(*handler));
    if (!handler) {
        return NULL;
    }

    handler->iface.lpVtbl = &kc_wvw_message_vtbl;
    handler->ref_count = 1;
    handler->ctx = ctx;
    return handler;
}

/**
 * Returns a requested interface from the environment handler.
 * @param iface COM handler interface.
 * @param riid Requested interface identifier.
 * @param object Destination pointer.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_environment_query_interface(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *iface, REFIID riid, void **object) {
    if (!object) {
        return E_POINTER;
    }

    *object = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)) {
        *object = iface;
        kc_wvw_environment_add_ref(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

/**
 * Increments the environment handler reference count.
 * @param iface COM handler interface.
 * @return New reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_environment_add_ref(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *iface) {
    kc_wvw_environment_handler_t *handler = (kc_wvw_environment_handler_t *)iface;

    return (ULONG)InterlockedIncrement(&handler->ref_count);
}

/**
 * Decrements the environment handler reference count.
 * @param iface COM handler interface.
 * @return New reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_environment_release(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *iface) {
    kc_wvw_environment_handler_t *handler = (kc_wvw_environment_handler_t *)iface;
    ULONG next;

    next = (ULONG)InterlockedDecrement(&handler->ref_count);
    if (next == 0) {
        kc_wvw_context_release(handler->ctx);
        free(handler);
    }
    return next;
}

/**
 * Returns a requested interface from the controller handler.
 * @param iface COM handler interface.
 * @param riid Requested interface identifier.
 * @param object Destination pointer.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_controller_query_interface(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *iface, REFIID riid, void **object) {
    if (!object) {
        return E_POINTER;
    }

    *object = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)) {
        *object = iface;
        kc_wvw_controller_add_ref(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

/**
 * Increments the controller handler reference count.
 * @param iface COM handler interface.
 * @return New reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_controller_add_ref(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *iface) {
    kc_wvw_controller_handler_t *handler = (kc_wvw_controller_handler_t *)iface;

    return (ULONG)InterlockedIncrement(&handler->ref_count);
}

/**
 * Decrements the controller handler reference count.
 * @param iface COM handler interface.
 * @return New reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_controller_release(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *iface) {
    kc_wvw_controller_handler_t *handler = (kc_wvw_controller_handler_t *)iface;
    ULONG next;

    next = (ULONG)InterlockedDecrement(&handler->ref_count);
    if (next == 0) {
        kc_wvw_context_release(handler->ctx);
        free(handler);
    }
    return next;
}

/**
 * Returns a requested interface from the script handler.
 * @param iface COM handler interface.
 * @param riid Requested interface identifier.
 * @param object Destination pointer.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_script_query_interface(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *iface, REFIID riid, void **object) {
    if (!object) {
        return E_POINTER;
    }

    *object = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler)) {
        *object = iface;
        kc_wvw_script_add_ref(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

/**
 * Increments the script handler reference count.
 * @param iface COM handler interface.
 * @return New reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_script_add_ref(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *iface) {
    kc_wvw_script_handler_t *handler = (kc_wvw_script_handler_t *)iface;

    return (ULONG)InterlockedIncrement(&handler->ref_count);
}

/**
 * Decrements the script handler reference count.
 * @param iface COM handler interface.
 * @return New reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_script_release(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *iface) {
    kc_wvw_script_handler_t *handler = (kc_wvw_script_handler_t *)iface;
    ULONG next;

    next = (ULONG)InterlockedDecrement(&handler->ref_count);
    if (next == 0) {
        free(handler);
    }
    return next;
}

/**
 * Completes one injected startup script registration.
 * @param iface COM handler interface.
 * @param error_code Completion status.
 * @param result Script identifier.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_script_invoke(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *iface, HRESULT error_code, PCWSTR result) {
    (void)iface;
    (void)error_code;
    (void)result;
    return S_OK;
}

/**
 * Returns a requested interface from the navigation handler.
 * @param iface COM handler interface.
 * @param riid Requested interface identifier.
 * @param object Destination pointer.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_navigation_query_interface(ICoreWebView2NavigationStartingEventHandler *iface, REFIID riid, void **object) {
    if (!object) {
        return E_POINTER;
    }

    *object = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ICoreWebView2NavigationStartingEventHandler)) {
        *object = iface;
        kc_wvw_navigation_add_ref(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

/**
 * Increments the navigation handler reference count.
 * @param iface COM handler interface.
 * @return New reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_navigation_add_ref(ICoreWebView2NavigationStartingEventHandler *iface) {
    kc_wvw_navigation_handler_t *handler = (kc_wvw_navigation_handler_t *)iface;

    return (ULONG)InterlockedIncrement(&handler->ref_count);
}

/**
 * Decrements the navigation handler reference count.
 * @param iface COM handler interface.
 * @return New reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_navigation_release(ICoreWebView2NavigationStartingEventHandler *iface) {
    kc_wvw_navigation_handler_t *handler = (kc_wvw_navigation_handler_t *)iface;
    ULONG next;

    next = (ULONG)InterlockedDecrement(&handler->ref_count);
    if (next == 0) {
        free(handler);
    }
    return next;
}

/**
 * Returns a requested interface from the message handler.
 * @param iface COM handler interface.
 * @param riid Requested interface identifier.
 * @param object Destination pointer.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_message_query_interface(ICoreWebView2WebMessageReceivedEventHandler *iface, REFIID riid, void **object) {
    if (!object) {
        return E_POINTER;
    }

    *object = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ICoreWebView2WebMessageReceivedEventHandler)) {
        *object = iface;
        kc_wvw_message_add_ref(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

/**
 * Increments the message handler reference count.
 * @param iface COM handler interface.
 * @return New reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_message_add_ref(ICoreWebView2WebMessageReceivedEventHandler *iface) {
    kc_wvw_message_handler_t *handler = (kc_wvw_message_handler_t *)iface;

    return (ULONG)InterlockedIncrement(&handler->ref_count);
}

/**
 * Decrements the message handler reference count.
 * @param iface COM handler interface.
 * @return New reference count.
 */
static ULONG STDMETHODCALLTYPE kc_wvw_message_release(ICoreWebView2WebMessageReceivedEventHandler *iface) {
    kc_wvw_message_handler_t *handler = (kc_wvw_message_handler_t *)iface;
    ULONG next;

    next = (ULONG)InterlockedDecrement(&handler->ref_count);
    if (next == 0) {
        free(handler);
    }
    return next;
}

/**
 * Returns the browser arguments for the WebView2 environment.
 * @return Argument string or NULL.
 */
static const char *kc_wvw_browser_args(void) {
    return getenv("KC_WVW_BROWSER_ARGS");
}

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_wvw_version(void) {
    return (uint64_t)KC_WVW_BUILD_VERSION;
}

/**
 * Duplicate one C string.
 * @param text Source string.
 * @return Newly allocated duplicate or NULL.
 */
static char *kc_wvw_strdup(const char *text) {
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

static void kc_wvw_config_free(kc_wvw_config_t *config) {
    if (!config) return;
    free(config->url); free(config->title); free(config->background);
    memset(config, 0, sizeof(*config));
}

static int kc_wvw_config_copy(kc_wvw_config_t *config, const kc_wvw_options_t *options) {
    if (!config || !options || !options->url || !options->url[0]) return KC_WVW_ERROR;
    memset(config, 0, sizeof(*config));
    config->url = kc_wvw_strdup(options->url);
    config->title = kc_wvw_strdup(options->title ? options->title : "wvw");
    config->background = options->background ? kc_wvw_strdup(options->background) : NULL;
    if (!config->url || !config->title || (options->background && !config->background)) {
        kc_wvw_config_free(config); return KC_WVW_ERROR;
    }
    config->width = options->width ? *options->width : 1280;
    config->height = options->height ? *options->height : 720;
    config->has_posx = options->posx != NULL; config->has_posy = options->posy != NULL;
    config->posx = options->posx ? *options->posx : 0; config->posy = options->posy ? *options->posy : 0;
    config->fullscreen = options->fullscreen ? !!*options->fullscreen : 0;
    config->borderless = options->borderless ? !!*options->borderless : 0;
    config->always_on_top = options->always_on_top ? !!*options->always_on_top : 0;
    config->click_through = options->click_through ? !!*options->click_through : 0;
    config->no_focus = options->no_focus ? !!*options->no_focus : 0;
    if (config->width <= 0 || config->height <= 0 ||
        config->width > KC_WVW_SIZE_MAX || config->height > KC_WVW_SIZE_MAX ||
        strlen(config->title) > KC_WVW_TITLE_MAX) {
        kc_wvw_config_free(config); return KC_WVW_ERROR;
    }
    return KC_WVW_OK;
}


/**
 * Return whether the configured background requests a transparent host surface.
 * @param text Background color in RRGGBB or AARRGGBB format.
 * @return Non-zero when the host should enter transparent mode, otherwise zero.
 */
static int kc_wvw_background_transparent(const char *text) {
    BYTE a;
    BYTE r;
    BYTE g;
    BYTE b;

    if (!text) {
        return 0;
    }
    if (kc_wvw_parse_background_color(text, &a, &r, &g, &b) != KC_WVW_OK) {
        return 0;
    }

    (void)r;
    (void)g;
    (void)b;
    return a == 0x00;
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} kc_wvw_text_buf_t;

/**
 * Initialize one text buffer.
 * @param buf Buffer state.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_text_buf_init(kc_wvw_text_buf_t *buf) {
    if (!buf) {
        return KC_WVW_ERROR;
    }

    buf->data = (char *)malloc(256);
    if (!buf->data) {
        return KC_WVW_ERROR;
    }

    buf->data[0] = '\0';
    buf->len = 0;
    buf->cap = 256;
    return KC_WVW_OK;
}

/**
 * Append one byte range into the text buffer.
 * @param buf Buffer state.
 * @param text Source byte range.
 * @param len Source length.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_text_buf_append_n(kc_wvw_text_buf_t *buf, const char *text, size_t len) {
    char *next;
    size_t cap;

    if (!buf || !buf->data || (!text && len != 0)) {
        return KC_WVW_ERROR;
    }

    if (buf->len + len + 1 > buf->cap) {
        cap = buf->cap;
        while (buf->len + len + 1 > cap) {
            cap *= 2;
        }
        next = (char *)realloc(buf->data, cap);
        if (!next) {
            return KC_WVW_ERROR;
        }
        buf->data = next;
        buf->cap = cap;
    }

    if (len > 0) {
        memcpy(buf->data + buf->len, text, len);
        buf->len += len;
    }
    buf->data[buf->len] = '\0';
    return KC_WVW_OK;
}

/**
 * Append one C string into the text buffer.
 * @param buf Buffer state.
 * @param text Source text.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_text_buf_append(kc_wvw_text_buf_t *buf, const char *text) {
    if (!text) {
        return KC_WVW_ERROR;
    }

    return kc_wvw_text_buf_append_n(buf, text, strlen(text));
}

/**
 * Return whether one bridge method name is safe for JS property injection.
 * @param method Candidate method name.
 * @return Non-zero when the identifier is accepted, otherwise zero.
 */
static int kc_wvw_bridge_method_valid(const char *method) {
    size_t i;

    static const char *const reserved[] = {
        "minimize", "maximize", "restore", "close",
        "setTitle", "setSize", "getState"
    };

    if (!method || !method[0]) {
        return 0;
    }

    for (i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++) {
        if (strcmp(method, reserved[i]) == 0) {
            return 0;
        }
    }

    if (!((method[0] >= 'A' && method[0] <= 'Z') || (method[0] >= 'a' && method[0] <= 'z') || method[0] == '_' || method[0] == '$')) {
        return 0;
    }

    for (i = 1; method[i]; i++) {
        if (!((method[i] >= 'A' && method[i] <= 'Z') || (method[i] >= 'a' && method[i] <= 'z') || (method[i] >= '0' && method[i] <= '9') || method[i] == '_' || method[i] == '$')) {
            return 0;
        }
    }

    return 1;
}

/**
 * Extract the origin scheme and host from a URL string.
 * @param url Candidate URL.
 * @return Newly allocated origin string or NULL.
 */
static char *kc_wvw_extract_origin(const char *url) {
    const char *p;
    const char *q;
    const char *end;
    const char *port;
    size_t scheme_len;
    size_t authority_len;
    char *out;

    if (!url) {
        return NULL;
    }
    p = strstr(url, "://");
    if (!p || p == url) {
        return NULL;
    }
    if (!((url[0] >= 'A' && url[0] <= 'Z') || (url[0] >= 'a' && url[0] <= 'z'))) {
        return NULL;
    }
    for (q = url + 1; q < p; q++) {
        if (!(((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') ||
            (*q >= '0' && *q <= '9') || *q == '+' || *q == '-' || *q == '.'))) {
            return NULL;
        }
    }
    scheme_len = (size_t)(p - url) + 3;
    q = p + 3;
    end = q;
    while (*end && *end != '/' && *end != '?' && *end != '#') {
        if (*end == '@' || *end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') {
            return NULL;
        }
        end++;
    }
    if (end == q) {
        return NULL;
    }
    port = NULL;
    for (p = q; p < end; p++) {
        if (*p == ':') {
            if (port) {
                return NULL;
            }
            port = p + 1;
        }
    }
    if (port) {
        if (port == end || port == q) {
            return NULL;
        }
        for (p = port; p < end; p++) {
            if (*p < '0' || *p > '9') {
                return NULL;
            }
        }
    }
    authority_len = (size_t)(end - q);
    out = (char *)malloc(scheme_len + authority_len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, url, scheme_len + authority_len);
    out[scheme_len + authority_len] = '\0';
    return out;
}

/**
 * Search for an origin string within a space-separated list of origins.
 * @param origin Target origin.
 * @param list Space-separated list of origins.
 * @return Non-zero if found, otherwise zero.
 */
static int kc_wvw_is_origin_in_list(const char *origin, const char *list) {
    const char *p;
    const char *next;
    size_t len;
    size_t origin_len;

    if (!origin || !list) {
        return 0;
    }
    origin_len = strlen(origin);
    p = list;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (!*p) {
            break;
        }
        next = p;
        while (*next && *next != ' ' && *next != '\t' && *next != '\r' && *next != '\n') {
            next++;
        }
        len = (size_t)(next - p);
        if (len == origin_len && strncmp(p, origin, len) == 0) {
            return 1;
        }
        p = next;
    }
    return 0;
}

/**
 * Return whether a URL hostname is exactly "localhost".
 * @param url Candidate URL.
 * @return Non-zero only when the authority host is exactly localhost.
 */
static int kc_wvw_url_is_localhost(const char *url) {
    const char *p;
    const char *host;
    const char *port;
    static const char local[] = "localhost";
    int i;

    if (!url) {
        return 0;
    }
    if (strncmp(url, "http://", 7) == 0) {
        p = url + 7;
    } else if (strncmp(url, "https://", 8) == 0) {
        p = url + 8;
    } else {
        return 0;
    }
    host = p;
    while (*host && *host != '@') {
        host++;
    }
    if (*host == '@') {
        return 0;
    }
    host = p;
    while (*host && *host != ':' && *host != '/' && *host != '?' && *host != '#') {
        host++;
    }
    if ((size_t)(host - p) != 9) {
        return 0;
    }
    for (i = 0; i < 9; i++) {
        char c = p[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c + ('a' - 'A'));
        }
        if (c != local[i]) {
            return 0;
        }
    }
    if (*host == ':') {
        port = host + 1;
        if (*port == '\0' || *port == '/' || *port == '?' || *port == '#') {
            return 0;
        }
        for (; *port; port++) {
            if (*port < '0' || *port > '9') {
                if (*port == '/' || *port == '?' || *port == '#') {
                    break;
                }
                return 0;
            }
        }
    }
    return 1;
}

/**
 * Return whether one URL is trusted for the current bridge policy.
 * @param ctx Window context.
 * @param bridge Bridge state.
 * @param url Candidate URL.
 * @return Non-zero when the URL is accepted, otherwise zero.
 */
static int kc_wvw_bridge_url_trusted(kc_wvw_t *ctx, kc_wvw_bridge_state_t *bridge, const char *url) {
    char *url_origin;
    char *init_origin;
    const char *env_trusted;
    int trusted = 0;

    if (!bridge || !url) {
        return 0;
    }

    if (bridge->allow_file && strncmp(url, "file://", 7) == 0) {
        return 1;
    }
    if (bridge->allow_data && strncmp(url, "data:", 5) == 0) {
        return 1;
    }
    if (bridge->allow_localhost && kc_wvw_url_is_localhost(url)) {
        return 1;
    }

    url_origin = kc_wvw_extract_origin(url);
    if (!url_origin) {
        return 0;
    }

    env_trusted = getenv("TRUSTED_ORIGINS");
    if (env_trusted && kc_wvw_is_origin_in_list(url_origin, env_trusted)) {
        trusted = 1;
    }

    if (!trusted && ctx && ctx->opts.url) {
        init_origin = kc_wvw_extract_origin(ctx->opts.url);
        if (init_origin) {
            if (strcmp(url_origin, init_origin) == 0) {
                trusted = 1;
            }
            free(init_origin);
        }
    }

    free(url_origin);
    return trusted;
}

/**
 * Release one copied bridge configuration.
 * @param bridge Bridge state.
 * @return None.
 */
static void kc_wvw_bridge_state_free(kc_wvw_bridge_state_t *bridge) {
    int i;

    if (!bridge) {
        return;
    }

    if (bridge->methods) {
        for (i = 0; i < bridge->method_count; i++) {
            free(bridge->methods[i]);
        }
        free(bridge->methods);
    }

    memset(bridge, 0, sizeof(*bridge));
}

/**
 * Copy one bridge configuration into the context state.
 * @param dst Destination bridge state.
 * @param src Source bridge options.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_bridge_state_copy(kc_wvw_bridge_state_t *dst, const kc_wvw_bridge_options_t *src) {
    int i;

    if (!dst || !src || src->method_count < 0) {
        return KC_WVW_ERROR;
    }

    if (src->method_count > 0 && (!src->methods || !src->callback)) {
        return KC_WVW_ERROR;
    }

    if (src->method_count > 0) {
        dst->methods = (char **)calloc((size_t)src->method_count, sizeof(char *));
        if (!dst->methods) {
            return KC_WVW_ERROR;
        }
    }

    dst->method_count = src->method_count;
    for (i = 0; i < src->method_count; i++) {
        if (!kc_wvw_bridge_method_valid(src->methods[i])) {
            kc_wvw_bridge_state_free(dst);
            return KC_WVW_ERROR;
        }
        dst->methods[i] = kc_wvw_strdup(src->methods[i]);
        if (!dst->methods[i]) {
            kc_wvw_bridge_state_free(dst);
            return KC_WVW_ERROR;
        }
    }

    dst->callback = src->callback;
    dst->userdata = src->userdata;
    dst->allow_file = src->allow_file;
    dst->allow_data = src->allow_data;
    dst->allow_localhost = src->allow_localhost;

    return KC_WVW_OK;
}

/**
 * Return whether one method belongs to the current whitelist.
 * @param bridge Bridge state.
 * @param method Candidate method name.
 * @return Non-zero when the method is accepted, otherwise zero.
 */
static int kc_wvw_bridge_method_allowed(kc_wvw_bridge_state_t *bridge, const char *method) {
    int i;

    if (!bridge || !method) {
        return 0;
    }

    for (i = 0; i < bridge->method_count; i++) {
        if (strcmp(bridge->methods[i], method) == 0) {
            return 1;
        }
    }

    return 0;
}

/**
 * Extract one string field from a bridge request.
 * @param json Source JSON payload.
 * @param key Field name.
 * @return Newly allocated string value or NULL.
 */
static char *kc_wvw_bridge_get_string(const char *json, const char *key) {
    JSON_Value *root;
    JSON_Object *obj;
    const char *val;
    char *copy;

    if (!json || !key) return NULL;

    root = json_parse_string(json);
    if (!root || json_value_get_type(root) != JSONObject) {
        json_value_free(root);
        return NULL;
    }
    obj = json_value_get_object(root);
    val = json_object_get_string(obj, key);
    copy = val ? kc_wvw_strdup(val) : NULL;
    json_value_free(root);
    return copy;
}

/**
 * Extract the params payload from one bridge request.
 * @param json Source JSON payload.
 * @return Newly allocated JSON fragment or NULL.
 */
static char *kc_wvw_bridge_get_params(const char *json) {
    JSON_Value *root;
    JSON_Object *obj;
    JSON_Value *params_val;
    char *serialized;

    if (!json) return NULL;

    root = json_parse_string(json);
    if (!root || json_value_get_type(root) != JSONObject) {
        json_value_free(root);
        return NULL;
    }
    obj = json_value_get_object(root);
    params_val = json_object_get_value(obj, "params");
    if (!params_val) {
        json_value_free(root);
        return kc_wvw_strdup("null");
    }
    serialized = json_serialize_to_string(params_val);
    json_value_free(root);
    return serialized;
}

/**
 * Build one bridge response payload.
 * @param id Request identifier.
 * @param ok Success flag.
 * @param body Serialized result or error object.
 * @return Newly allocated response payload or NULL.
 */
static char *kc_wvw_bridge_wrap_response(const char *id, int ok, const char *body) {
    JSON_Value *root;
    JSON_Value *body_val;
    char *out;

    if (!id || !body) return NULL;

    root = json_value_init_object();
    if (!root) return NULL;

    json_object_set_string(json_value_get_object(root), "id", id);
    json_object_set_boolean(json_value_get_object(root), "ok", ok ? 1 : 0);

    body_val = json_parse_string(body);
    if (!body_val) {
        json_value_free(root);
        return NULL;
    }
    json_object_set_value(json_value_get_object(root),
        ok ? "result" : "error", body_val);

    out = json_serialize_to_string(root);
    json_value_free(root);
    return out;
}

/**
 * Build one JSON error object.
 * @param code Error code string.
 * @param message Error message string.
 * @return Newly allocated JSON error object or NULL.
 */
static char *kc_wvw_bridge_error_object(const char *code, const char *message) {
    JSON_Value *root;
    char *out;

    if (!code || !message) return NULL;

    root = json_value_init_object();
    if (!root) return NULL;

    json_object_set_string(json_value_get_object(root), "code", code);
    json_object_set_string(json_value_get_object(root), "message", message);

    out = json_serialize_to_string(root);
    json_value_free(root);
    return out;
}

/**
 * Build the injected NativeBridge bootstrap script.
 * @param bridge Bridge state.
 * @param sender_expr JavaScript sender expression.
 * @param receiver_setup JavaScript receiver setup.
 * @return Newly allocated script text or NULL.
 */
static char *kc_wvw_bridge_bootstrap_script(kc_wvw_bridge_state_t *bridge, const char *sender_expr, const char *receiver_setup) {
    kc_wvw_text_buf_t buf;
    int i;

    if (!bridge || !sender_expr || !receiver_setup || kc_wvw_text_buf_init(&buf) != KC_WVW_OK) {
        return NULL;
    }

    if (kc_wvw_text_buf_append(&buf, "(function(){if(window.NativeBridge){return;}var __kcWvwPending={};var __kcWvwSeq=0;function __kcWvwReceive(msg){if(msg&&typeof msg.id==='string'){var p=__kcWvwPending[msg.id];if(p){delete __kcWvwPending[msg.id];if(msg.ok){p.resolve(msg.result!==undefined?msg.result:{ok:true});}else{p.reject(msg.error||{code:'INTERNAL_ERROR',message:'Bridge error'});}}return;}window.dispatchEvent(new CustomEvent('") != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, KC_WVW_BRIDGE_EVENT_NAME) != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, "',{detail:msg}));}window.__kcWvwReceive=__kcWvwReceive;window.NativeBridge={};function __kcWvwSend(method,params){return new Promise(function(resolve,reject){var id=String(++__kcWvwSeq);__kcWvwPending[id]={resolve:resolve,reject:reject};(") != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, sender_expr) != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, ")(JSON.stringify({id:id,method:method,params:params===undefined?null:params}));});}window.NativeBridge.minimize=function(){return __kcWvwSend('minimize');};window.NativeBridge.maximize=function(){return __kcWvwSend('maximize');};window.NativeBridge.restore=function(){return __kcWvwSend('restore');};window.NativeBridge.close=function(){return __kcWvwSend('close');};window.NativeBridge.setTitle=function(title){return __kcWvwSend('setTitle',{title:title});};window.NativeBridge.setSize=function(width,height){return __kcWvwSend('setSize',{width:width,height:height});};window.NativeBridge.getState=function(){return __kcWvwSend('getState');};") != KC_WVW_OK) {
        free(buf.data);
        return NULL;
    }

    for (i = 0; i < bridge->method_count; i++) {
        if (kc_wvw_text_buf_append(&buf, "window.NativeBridge.") != KC_WVW_OK ||
            kc_wvw_text_buf_append(&buf, bridge->methods[i]) != KC_WVW_OK ||
            kc_wvw_text_buf_append(&buf, "=function(params){return __kcWvwSend('") != KC_WVW_OK ||
            kc_wvw_text_buf_append(&buf, bridge->methods[i]) != KC_WVW_OK ||
            kc_wvw_text_buf_append(&buf, "',params);};") != KC_WVW_OK) {
            free(buf.data);
            return NULL;
        }
    }

    if (kc_wvw_text_buf_append(&buf, receiver_setup) != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, "}());") != KC_WVW_OK) {
        free(buf.data);
        return NULL;
    }

    return buf.data;
}

/**
 * Deliver one JSON payload into the WebView bridge runtime.
 * @param ctx Window context.
 * @param json JSON payload.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_bridge_post_json(kc_wvw_t *ctx, const char *json) {
    wchar_t *wide;
    HRESULT hr;

    if (!ctx || !ctx->webview || !json || strlen(json) > KC_WVW_BRIDGE_MAX_MESSAGE) {
        return KC_WVW_ERROR;
    }

    wide = kc_wvw_utf16_from_utf8(json);
    if (!wide) {
        return KC_WVW_ERROR;
    }

    hr = ICoreWebView2_PostWebMessageAsJson(ctx->webview, wide);
    free(wide);
    return FAILED(hr) ? KC_WVW_ERROR : KC_WVW_OK;
}

/**
 * Decide whether one navigation request stays inside the trusted WebView.
 * @param iface COM handler interface.
 * @param sender WebView sender.
 * @param args Navigation event args.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_navigation_invoke(ICoreWebView2NavigationStartingEventHandler *iface, ICoreWebView2 *sender, ICoreWebView2NavigationStartingEventArgs *args) {
    kc_wvw_navigation_handler_t *handler = (kc_wvw_navigation_handler_t *)iface;
    LPWSTR uri = NULL;
    char utf8[4096];

    (void)sender;
    if (!handler || !handler->ctx || !handler->ctx->bridge.enabled || !args) {
        return S_OK;
    }

    if (SUCCEEDED(ICoreWebView2NavigationStartingEventArgs_get_Uri(args, &uri)) && uri) {
        utf8[0] = '\0';
        WideCharToMultiByte(CP_UTF8, 0, uri, -1, utf8, sizeof(utf8), NULL, NULL);
        if (!kc_wvw_bridge_url_trusted(handler->ctx, &handler->ctx->bridge, utf8)) {
            ICoreWebView2NavigationStartingEventArgs_put_Cancel(args, TRUE);
        }
        CoTaskMemFree(uri);
    }

    return S_OK;
}

/**
 * Dispatch one WebView message into the current bridge callback.
 * @param iface COM handler interface.
 * @param sender WebView sender.
 * @param args Message event args.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_message_invoke(ICoreWebView2WebMessageReceivedEventHandler *iface, ICoreWebView2 *sender, ICoreWebView2WebMessageReceivedEventArgs *args) {
    kc_wvw_message_handler_t *handler = (kc_wvw_message_handler_t *)iface;
    LPWSTR message = NULL;
    char utf8[KC_WVW_BRIDGE_MAX_MESSAGE + 1];
    char *response;

    (void)sender;
    if (!handler || !handler->ctx || !args) {
        return S_OK;
    }

    if (FAILED(ICoreWebView2WebMessageReceivedEventArgs_TryGetWebMessageAsString(args, &message)) || !message) {
        return S_OK;
    }

    utf8[0] = '\0';
    WideCharToMultiByte(CP_UTF8, 0, message, -1, utf8, sizeof(utf8), NULL, NULL);
    response = kc_wvw_bridge_dispatch_request(handler->ctx, utf8);
    if (response) {
        kc_wvw_bridge_post_json(handler->ctx, response);
        free(response);
    }
    CoTaskMemFree(message);
    return S_OK;
}

/**
 * Parses one hexadecimal WebView background color.
 * @param text Color text in RRGGBB or AARRGGBB format.
 * @param out_a Destination alpha byte.
 * @param out_r Destination red byte.
 * @param out_g Destination green byte.
 * @param out_b Destination blue byte.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_parse_background_color(const char *text, BYTE *out_a, BYTE *out_r, BYTE *out_g, BYTE *out_b) {
    size_t length;
    unsigned int parts[4];

    if (!text || !out_a || !out_r || !out_g || !out_b) {
        return KC_WVW_ERROR;
    }

    length = strlen(text);
    if (length == 6) {
        if (sscanf(text, "%2x%2x%2x", &parts[1], &parts[2], &parts[3]) != 3) {
            return KC_WVW_ERROR;
        }
        parts[0] = 0xff;
    } else if (length == 8) {
        if (sscanf(text, "%2x%2x%2x%2x", &parts[0], &parts[1], &parts[2], &parts[3]) != 4) {
            return KC_WVW_ERROR;
        }
    } else {
        return KC_WVW_ERROR;
    }

    *out_a = (BYTE)parts[0];
    *out_r = (BYTE)parts[1];
    *out_g = (BYTE)parts[2];
    *out_b = (BYTE)parts[3];
    return KC_WVW_OK;
}

/**
 * Formats one parsed background color for the WebView2 environment variable.
 * @param text Color text in RRGGBB or AARRGGBB format.
 * @return Newly allocated AARRGGBB string or NULL.
 */
static char *kc_wvw_background_hex8(const char *text) {
    BYTE a;
    BYTE r;
    BYTE g;
    BYTE b;
    char *color;

    if (kc_wvw_parse_background_color(text, &a, &r, &g, &b) != KC_WVW_OK) {
        return NULL;
    }
    if (a != 0x00 && a != 0xff) {
        return NULL;
    }

    color = (char *)malloc(9);
    if (!color) {
        return NULL;
    }

    snprintf(color, 9, "%02X%02X%02X%02X", (unsigned int)a, (unsigned int)r, (unsigned int)g, (unsigned int)b);
    return color;
}

/**
 * Creates one native window background brush from the configured color.
 * @param text Color text in RRGGBB or AARRGGBB format.
 * @return Newly allocated brush or NULL.
 */
static HBRUSH kc_wvw_background_brush(const char *text) {
    BYTE a;
    BYTE r;
    BYTE g;
    BYTE b;

    if (!text || kc_wvw_parse_background_color(text, &a, &r, &g, &b) != KC_WVW_OK) {
        return NULL;
    }
    if (a == 0x00) {
        return NULL;
    }

    return CreateSolidBrush(RGB(r, g, b));
}

/**
 * Converts UTF-8 text to UTF-16 text.
 * @param text UTF-8 source text.
 * @return Newly allocated UTF-16 text or NULL.
 */
static wchar_t *kc_wvw_utf16_from_utf8(const char *text) {
    int length;
    wchar_t *wide;

    if (!text) {
        return NULL;
    }

    length = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (length <= 0) {
        return NULL;
    }

    wide = (wchar_t *)malloc((size_t)length * sizeof(wchar_t));
    if (!wide) {
        return NULL;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, length) <= 0) {
        free(wide);
        return NULL;
    }

    return wide;
}

/**
 * Joins two UTF-16 path segments.
 * @param left Left path segment.
 * @param right Right path segment.
 * @return Newly allocated path or NULL.
 */
static wchar_t *kc_wvw_join_path(const wchar_t *left, const wchar_t *right) {
    size_t left_len;
    size_t right_len;
    size_t slash;
    wchar_t *path;

    if (!left || !right) {
        return NULL;
    }

    left_len = wcslen(left);
    right_len = wcslen(right);
    slash = left_len > 0 && left[left_len - 1] != L'\\' ? 1 : 0;
    path = (wchar_t *)malloc((left_len + slash + right_len + 1) * sizeof(wchar_t));
    if (!path) {
        return NULL;
    }

    wcscpy(path, left);
    if (slash) {
        wcscat(path, L"\\");
    }
    wcscat(path, right);
    return path;
}

/**
 * Returns the executable directory.
 * @return Newly allocated directory path or NULL.
 */
static wchar_t *kc_wvw_executable_dir(void) {
    DWORD length;
    wchar_t *slash;
    wchar_t buffer[MAX_PATH];

    length = GetModuleFileNameW(NULL, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return NULL;
    }

    slash = wcsrchr(buffer, L'\\');
    if (slash) {
        *slash = L'\0';
    }
    return _wcsdup(buffer);
}

/**
 * Loads WebView2Loader.dll from beside the executable.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_load_loader(kc_wvw_t *ctx) {
    wchar_t *dir;
    wchar_t *dll_path;

    dir = kc_wvw_executable_dir();
    if (!dir) {
        return KC_WVW_ERROR;
    }

    dll_path = kc_wvw_join_path(dir, L"WebView2Loader.dll");
    free(dir);
    if (!dll_path) {
        return KC_WVW_ERROR;
    }

    ctx->loader = LoadLibraryW(dll_path);
    free(dll_path);
    if (!ctx->loader) {
        return KC_WVW_ERROR;
    }

    return KC_WVW_OK;
}

/**
 * Creates the persistent WebView2 user-data directory.
 * @return Newly allocated directory path or NULL.
 */
static wchar_t *kc_wvw_user_data_dir(void) {
    PWSTR local_app_data = NULL;
    wchar_t *kaisar_dir;
    wchar_t *wvw_dir;
    wchar_t *webview_dir;

    if (FAILED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &local_app_data))) {
        return NULL;
    }

    kaisar_dir = kc_wvw_join_path(local_app_data, L"KaisarCode");
    CoTaskMemFree(local_app_data);
    if (!kaisar_dir) {
        return NULL;
    }

    wvw_dir = kc_wvw_join_path(kaisar_dir, L"wvw");
    if (!wvw_dir) {
        free(kaisar_dir);
        return NULL;
    }

    webview_dir = kc_wvw_join_path(wvw_dir, L"WebView2");
    if (!webview_dir) {
        free(kaisar_dir);
        free(wvw_dir);
        return NULL;
    }

    CreateDirectoryW(kaisar_dir, NULL);
    CreateDirectoryW(wvw_dir, NULL);
    CreateDirectoryW(webview_dir, NULL);
    free(kaisar_dir);
    free(wvw_dir);
    return webview_dir;
}

/**
 * Updates the WebView2 controller to fill the client area.
 * @param ctx Window context.
 * @return None.
 */
static void kc_wvw_update_bounds(kc_wvw_t *ctx) {
    RECT bounds;

    if (!ctx || !ctx->controller || !ctx->hwnd) {
        return;
    }

    GetClientRect(ctx->hwnd, &bounds);
    ICoreWebView2Controller_put_Bounds(ctx->controller, bounds);
}

/**
 * Apply topmost and click-through host modes on Windows.
 * @param ctx Window context.
 * @return None.
 */
static void kc_wvw_windows_apply_window_modes(kc_wvw_t *ctx) {
    LONG_PTR ex_style;

    if (!ctx || !ctx->hwnd) {
        return;
    }

    if (ctx->opts.always_on_top) {
        SetWindowPos(ctx->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
    if (ctx->opts.click_through) {
        ex_style = GetWindowLongPtrW(ctx->hwnd, GWL_EXSTYLE);
        ex_style |= WS_EX_TRANSPARENT;
        if (kc_wvw_background_transparent(ctx->opts.background)) {
            ex_style |= WS_EX_LAYERED;
        }
        if (ctx->opts.no_focus) {
            ex_style |= WS_EX_NOACTIVATE;
        }
        SetWindowLongPtrW(ctx->hwnd, GWL_EXSTYLE, ex_style);
        return;
    }
    if (ctx->opts.no_focus) {
        ex_style = GetWindowLongPtrW(ctx->hwnd, GWL_EXSTYLE);
        ex_style |= WS_EX_NOACTIVATE;
        SetWindowLongPtrW(ctx->hwnd, GWL_EXSTYLE, ex_style);
    }
}

/**
 * Applies application-surface WebView settings.
 * @param ctx Window context.
 * @return None.
 */
static void kc_wvw_apply_settings(kc_wvw_t *ctx) {
    ICoreWebView2Settings *settings = NULL;
    ICoreWebView2Settings3 *settings3 = NULL;

    if (!ctx || !ctx->webview) {
        return;
    }

    if (FAILED(ICoreWebView2_get_Settings(ctx->webview, &settings)) || !settings) {
        return;
    }

    ICoreWebView2Settings_put_IsStatusBarEnabled(settings, FALSE);
    ICoreWebView2Settings_put_IsZoomControlEnabled(settings, FALSE);
#ifdef NDEBUG
    ICoreWebView2Settings_put_AreDevToolsEnabled(settings, FALSE);
    ICoreWebView2Settings_put_AreDefaultContextMenusEnabled(settings, FALSE);
#endif
    if (SUCCEEDED(ICoreWebView2Settings_QueryInterface(settings, &IID_ICoreWebView2Settings3, (void **)&settings3)) && settings3) {
        ICoreWebView2Settings3_put_AreBrowserAcceleratorKeysEnabled(settings3, FALSE);
        ICoreWebView2Settings3_Release(settings3);
    }
    ICoreWebView2Settings_Release(settings);
}

/**
 * Applies one configured default background color to WebView2.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_apply_background_color(kc_wvw_t *ctx) {
    ICoreWebView2Controller2 *controller2 = NULL;
    COREWEBVIEW2_COLOR color;
    HRESULT hr;

    if (!ctx || !ctx->controller || !ctx->opts.background) {
        return KC_WVW_OK;
    }
    if (kc_wvw_parse_background_color(ctx->opts.background, &color.A, &color.R, &color.G, &color.B) != KC_WVW_OK) {
        return KC_WVW_ERROR;
    }
    if (color.A != 0x00 && color.A != 0xff) {
        return KC_WVW_ERROR;
    }

    hr = ICoreWebView2Controller_QueryInterface(ctx->controller, &IID_ICoreWebView2Controller2, (void **)&controller2);
    if (FAILED(hr) || !controller2) {
        return KC_WVW_OK;
    }

    hr = ICoreWebView2Controller2_put_DefaultBackgroundColor(controller2, color);
    ICoreWebView2Controller2_Release(controller2);
    return FAILED(hr) ? KC_WVW_ERROR : KC_WVW_OK;
}

/**
 * Install the configured bridge into the current WebView.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_windows_install_bridge(kc_wvw_t *ctx) {
    kc_wvw_script_handler_t *script_handler;
    kc_wvw_navigation_handler_t *navigation_handler;
    kc_wvw_message_handler_t *message_handler;
    EventRegistrationToken token;
    wchar_t *script_wide;
    char *script;
    HRESULT hr;

    if (!ctx || !ctx->webview || !ctx->bridge.enabled) {
        return KC_WVW_OK;
    }

    script = kc_wvw_bridge_bootstrap_script(&ctx->bridge, "window.chrome.webview.postMessage", "window.chrome.webview.addEventListener('message',function(e){__kcWvwReceive(e.data);});");
    if (!script) {
        return KC_WVW_ERROR;
    }

    script_wide = kc_wvw_utf16_from_utf8(script);
    free(script);
    if (!script_wide) {
        return KC_WVW_ERROR;
    }

    script_handler = kc_wvw_script_handler_new();
    if (!script_handler) {
        free(script_wide);
        return KC_WVW_ERROR;
    }

    hr = ICoreWebView2_AddScriptToExecuteOnDocumentCreated(ctx->webview, script_wide, (ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *)script_handler);
    ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler_Release((ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *)script_handler);
    free(script_wide);
    if (FAILED(hr)) {
        return KC_WVW_ERROR;
    }

    navigation_handler = kc_wvw_navigation_handler_new(ctx);
    if (!navigation_handler) {
        return KC_WVW_ERROR;
    }
    hr = ICoreWebView2_add_NavigationStarting(ctx->webview, (ICoreWebView2NavigationStartingEventHandler *)navigation_handler, &token);
    ICoreWebView2NavigationStartingEventHandler_Release((ICoreWebView2NavigationStartingEventHandler *)navigation_handler);
    if (FAILED(hr)) {
        return KC_WVW_ERROR;
    }

    message_handler = kc_wvw_message_handler_new(ctx);
    if (!message_handler) {
        return KC_WVW_ERROR;
    }
    hr = ICoreWebView2_add_WebMessageReceived(ctx->webview, (ICoreWebView2WebMessageReceivedEventHandler *)message_handler, &token);
    ICoreWebView2WebMessageReceivedEventHandler_Release((ICoreWebView2WebMessageReceivedEventHandler *)message_handler);
    if (FAILED(hr)) {
        return KC_WVW_ERROR;
    }

    return KC_WVW_OK;
}

/**
 * Navigates to the pending URL when the WebView is ready.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_flush_pending_navigation(kc_wvw_t *ctx) {
    wchar_t *target;
    HRESULT hr;

    if (!ctx || !ctx->webview || !ctx->pending_url) {
        return KC_WVW_OK;
    }

    target = kc_wvw_utf16_from_utf8(ctx->pending_url);
    if (!target) {
        return KC_WVW_ERROR;
    }

    hr = ICoreWebView2_Navigate(ctx->webview, target);
    free(target);
    return FAILED(hr) ? KC_WVW_ERROR : KC_WVW_OK;
}

/**
 * Performs UI-thread shutdown for the host window.
 * @param ctx Window context.
 * @return None.
 */
static void kc_wvw_request_close(kc_wvw_t *ctx) {
    if (!ctx || ctx->closing) {
        return;
    }

    ctx->closing = 1; ctx->running = 0;
    if (ctx->hwnd && IsWindow(ctx->hwnd)) DestroyWindow(ctx->hwnd);
    PostQuitMessage(0);
}

/**
 * Processes messages while WebView2 initializes asynchronously.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_wait_for_ready(kc_wvw_t *ctx) {
    MSG message;

    while (ctx->init_state == KC_WVW_INIT_PENDING && !ctx->closing) {
        BOOL rc = GetMessageW(&message, NULL, 0, 0);
        if (rc <= 0) {
            ctx->init_state = KC_WVW_INIT_FAILED;
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return ctx->init_state == KC_WVW_INIT_READY ? KC_WVW_OK : KC_WVW_ERROR;
}

/**
 * Handles native Win32 window messages.
 * @param hwnd Window handle.
 * @param msg Message identifier.
 * @param wparam Message parameter.
 * @param lparam Message parameter.
 * @return Window procedure result.
 */
static LRESULT CALLBACK kc_wvw_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    kc_wvw_t *ctx = (kc_wvw_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    case WM_SIZE:
        kc_wvw_update_bounds(ctx);
        return 0;
    case WM_ERASEBKGND:
        if (ctx && ctx->background_brush) {
            RECT rect;
            HDC dc = (HDC)wparam;
            GetClientRect(hwnd, &rect);
            FillRect(dc, &rect, ctx->background_brush);
            return 1;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_SETFOCUS:
        if (ctx && ctx->opts.no_focus) {
            return 0;
        }
        if (ctx && ctx->controller) {
            ICoreWebView2Controller_MoveFocus(ctx->controller, COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        }
        return 0;
    case WM_SETCURSOR:
        if (kc_wvw_is_wine()) {
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return TRUE;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_CLOSE:
        kc_wvw_request_close(ctx);
        return 0;
    case KC_WVW_CLOSE_MESSAGE:
        kc_wvw_request_close(ctx);
        return 0;
    case (WM_APP + 2): {
        kc_wvw_op_t *op = (kc_wvw_op_t *)lparam;
        if (op) op->result = kc_wvw_execute_op(ctx, op);
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

/**
 * Creates the native Win32 host window.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_create_window(kc_wvw_t *ctx) {
    WNDCLASSW klass;
    DWORD style;
    DWORD ex_style;
    RECT rect;
    wchar_t *title;
    const wchar_t class_name[] = L"kc_wvw_window";

    memset(&klass, 0, sizeof(klass));
    klass.lpfnWndProc = kc_wvw_window_proc;
    klass.hInstance = ctx->hinstance;
    klass.lpszClassName = class_name;
    klass.hCursor = LoadCursor(NULL, IDC_ARROW);
    klass.hbrBackground = NULL;
    RegisterClassW(&klass);

    style = WS_OVERLAPPEDWINDOW;
    ex_style = 0;
    if (ctx->opts.borderless || ctx->opts.fullscreen) {
        style = WS_POPUP | WS_VISIBLE;
    }
    if (kc_wvw_background_transparent(ctx->opts.background)) {
        ex_style |= WS_EX_LAYERED;
    }
    if (ctx->opts.no_focus) {
        ex_style |= WS_EX_NOACTIVATE;
    }

    rect.left = (ctx->opts.has_posx) ? ctx->opts.posx : CW_USEDEFAULT;
    rect.top = (ctx->opts.has_posy) ? ctx->opts.posy : CW_USEDEFAULT;
    if (rect.left != CW_USEDEFAULT) {
        int screen_w = GetSystemMetrics(SM_CXSCREEN);
        int win_w = rect.right;
        if (rect.left < 0) rect.left = 0;
        else if (rect.left > screen_w - win_w) rect.left = screen_w - win_w;
    }
    if (rect.top != CW_USEDEFAULT) {
        int screen_h = GetSystemMetrics(SM_CYSCREEN);
        int win_h = rect.bottom;
        if (rect.top < 0) rect.top = 0;
        else if (rect.top > screen_h - win_h) rect.top = screen_h - win_h;
    }
    rect.right = ctx->opts.width;
    rect.bottom = ctx->opts.height;
    AdjustWindowRectEx(&rect, style, FALSE, ex_style);

    if (ctx->opts.fullscreen) {
        rect.left = (ctx->opts.has_posx) ? ctx->opts.posx : 0;
        rect.top = (ctx->opts.has_posy) ? ctx->opts.posy : 0;
        rect.right = GetSystemMetrics(SM_CXSCREEN);
        rect.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    title = kc_wvw_utf16_from_utf8(ctx->opts.title ? ctx->opts.title : "wvw");
    ctx->hwnd = CreateWindowExW(
        ex_style,
        class_name,
        title ? title : L"wvw",
        style,
        rect.left,
        rect.top,
        rect.right - rect.left,
        rect.bottom - rect.top,
        NULL,
        NULL,
        ctx->hinstance,
        ctx
    );
    free(title);

    if (!ctx->hwnd) {
        return KC_WVW_ERROR;
    }
    if (kc_wvw_background_transparent(ctx->opts.background)) {
        SetLayeredWindowAttributes(ctx->hwnd, 0, 255, LWA_ALPHA);
    }
    kc_wvw_windows_apply_window_modes(ctx);

    ShowWindow(ctx->hwnd, SW_SHOW);
    UpdateWindow(ctx->hwnd);
    return KC_WVW_OK;
}

/**
 * Starts asynchronous WebView2 initialization.
 * @param ctx Window context.
 * @return KC_WVW_OK on start or KC_WVW_ERROR on failure.
 */
static int kc_wvw_start_webview(kc_wvw_t *ctx) {
    kc_wvw_create_environment_fn create_environment;
    kc_wvw_get_version_fn get_version;
    FARPROC create_environment_proc;
    FARPROC get_version_proc;
    kc_wvw_environment_handler_t *handler;
    wchar_t *user_data_dir;
    const char *browser_args;
    char *background;
    LPWSTR version = NULL;
    HRESULT hr;

    create_environment_proc = GetProcAddress(ctx->loader, "CreateCoreWebView2EnvironmentWithOptions");
    get_version_proc = GetProcAddress(ctx->loader, "GetAvailableCoreWebView2BrowserVersionString");
    memcpy(&create_environment, &create_environment_proc, sizeof(create_environment));
    memcpy(&get_version, &get_version_proc, sizeof(get_version));
    if (!create_environment || !get_version) {
        return KC_WVW_ERROR;
    }

    hr = get_version(NULL, &version);
    if (FAILED(hr) || !version) {
        return KC_WVW_ERROR;
    }
    CoTaskMemFree(version);

    user_data_dir = kc_wvw_user_data_dir();
    if (!user_data_dir) {
        return KC_WVW_ERROR;
    }

    browser_args = kc_wvw_browser_args();
    if (browser_args && *browser_args && !getenv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS")) {
        SetEnvironmentVariableA("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", browser_args);
    }
    background = NULL;
    if (ctx->opts.background && *ctx->opts.background) {
        background = kc_wvw_background_hex8(ctx->opts.background);
        if (!background) {
            free(user_data_dir);
            return KC_WVW_ERROR;
        }
        SetEnvironmentVariableA("WEBVIEW2_DEFAULT_BACKGROUND_COLOR", background);
        free(background);
    }

    handler = kc_wvw_environment_handler_new(ctx);
    if (!handler) {
        free(user_data_dir);
        return KC_WVW_ERROR;
    }
    hr = create_environment(NULL, user_data_dir, NULL, (ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *)handler);
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler_Release((ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *)handler);
    free(user_data_dir);
    if (FAILED(hr)) {
        return KC_WVW_ERROR;
    }

    return KC_WVW_OK;
}

/**
 * Receives the created WebView2 environment.
 * @param error_code Completion status.
 * @param result Created environment.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_environment_invoke(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *iface, HRESULT error_code, ICoreWebView2Environment *result) {
    kc_wvw_environment_handler_t *env_handler = (kc_wvw_environment_handler_t *)iface;
    kc_wvw_controller_handler_t *handler;
    HRESULT hr;
    kc_wvw_t *ctx = env_handler->ctx;

    if (!ctx || ctx->closing || FAILED(error_code) || !result) {
        if (ctx) {
            ctx->init_state = KC_WVW_INIT_FAILED;
        }
        return S_OK;
    }

    ctx->environment = result;
    ICoreWebView2Environment_AddRef(ctx->environment);
    handler = kc_wvw_controller_handler_new(ctx);
    if (!handler) {
        ctx->init_state = KC_WVW_INIT_FAILED;
        return S_OK;
    }
    hr = ICoreWebView2Environment_CreateCoreWebView2Controller(ctx->environment, ctx->hwnd, (ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *)handler);
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler_Release((ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *)handler);
    if (FAILED(hr)) {
        ctx->init_state = KC_WVW_INIT_FAILED;
    }
    return S_OK;
}

/**
 * Receives the created WebView2 controller.
 * @param error_code Completion status.
 * @param result Created controller.
 * @return HRESULT success code.
 */
static HRESULT STDMETHODCALLTYPE kc_wvw_controller_invoke(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *iface, HRESULT error_code, ICoreWebView2Controller *result) {
    kc_wvw_controller_handler_t *handler = (kc_wvw_controller_handler_t *)iface;
    kc_wvw_t *ctx = handler->ctx;

    if (!ctx || ctx->closing || FAILED(error_code) || !result) {
        if (ctx) {
            ctx->init_state = KC_WVW_INIT_FAILED;
        }
        return S_OK;
    }

    ctx->controller = result;
    ICoreWebView2Controller_AddRef(ctx->controller);
    if (FAILED(ICoreWebView2Controller_get_CoreWebView2(ctx->controller, &ctx->webview)) || !ctx->webview) {
        ctx->init_state = KC_WVW_INIT_FAILED;
        return S_OK;
    }

    kc_wvw_update_bounds(ctx);
    if (kc_wvw_apply_background_color(ctx) != KC_WVW_OK) {
        ctx->init_state = KC_WVW_INIT_FAILED;
        return S_OK;
    }
    kc_wvw_apply_settings(ctx);
    if (kc_wvw_windows_install_bridge(ctx) != KC_WVW_OK) {
        ctx->init_state = KC_WVW_INIT_FAILED;
        return S_OK;
    }
    if (kc_wvw_flush_pending_navigation(ctx) != KC_WVW_OK) {
        ctx->init_state = KC_WVW_INIT_FAILED;
        return S_OK;
    }

    ctx->init_state = KC_WVW_INIT_READY;
    return S_OK;
}











static DWORD WINAPI kc_wvw_windows_worker(LPVOID data) {
    kc_wvw_t *ctx=(kc_wvw_t *)data; HRESULT hr; MSG message;
    ctx->worker_id=GetCurrentThreadId(); ctx->init_state=KC_WVW_INIT_PENDING;
    hr=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE){kc_wvw_set_error(ctx,"COM initialization failed");SetEvent(ctx->ready);SetEvent(ctx->closed_event);return 1;}
    ctx->com_initialized=SUCCEEDED(hr);ctx->hinstance=GetModuleHandleW(NULL);ctx->background_brush=kc_wvw_background_brush(ctx->opts.background);
    ctx->pending_url=kc_wvw_strdup(ctx->opts.url);
    if(!ctx->pending_url){kc_wvw_set_error(ctx,"memory allocation failed");SetEvent(ctx->ready);SetEvent(ctx->closed_event);return 1;}
    if(kc_wvw_load_loader(ctx)!=KC_WVW_OK){kc_wvw_set_error(ctx,"WebView2Loader.dll not found");SetEvent(ctx->ready);SetEvent(ctx->closed_event);return 1;}
    if(kc_wvw_create_window(ctx)!=KC_WVW_OK){kc_wvw_set_error(ctx,"window creation failed");SetEvent(ctx->ready);SetEvent(ctx->closed_event);return 1;}
    if(kc_wvw_start_webview(ctx)!=KC_WVW_OK||kc_wvw_wait_for_ready(ctx)!=KC_WVW_OK){kc_wvw_set_error(ctx,"WebView2 initialization failed");SetEvent(ctx->ready);SetEvent(ctx->closed_event);return 1;}
    ctx->started=1;ctx->running=1;SetEvent(ctx->ready);
    while(ctx->running&&GetMessageW(&message,NULL,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
    ctx->running=0;
    if(ctx->controller){ICoreWebView2Controller_Close(ctx->controller);ICoreWebView2Controller_Release(ctx->controller);ctx->controller=NULL;}
    if(ctx->webview){ICoreWebView2_Release(ctx->webview);ctx->webview=NULL;}
    if(ctx->environment){ICoreWebView2Environment_Release(ctx->environment);ctx->environment=NULL;}
    if(ctx->hwnd&&IsWindow(ctx->hwnd))DestroyWindow(ctx->hwnd);ctx->hwnd=NULL;
    if(ctx->loader){FreeLibrary(ctx->loader);ctx->loader=NULL;}if(ctx->background_brush){DeleteObject(ctx->background_brush);ctx->background_brush=NULL;}
    if(ctx->com_initialized){CoUninitialize();ctx->com_initialized=0;}SetEvent(ctx->closed_event);
    if(ctx->free_on_exit){if(ctx->thread)CloseHandle(ctx->thread);ctx->thread=NULL;kc_wvw_context_release(ctx);}return 0;
}
static int kc_wvw_dispatch_op(kc_wvw_t *ctx,kc_wvw_op_t *op){
    if(!ctx||!op||ctx->closing||!ctx->started)return KC_WVW_ERROR;
    if(GetCurrentThreadId()==ctx->worker_id)return kc_wvw_execute_op(ctx,op);
    if(!ctx->hwnd||!IsWindow(ctx->hwnd))return KC_WVW_ERROR;
    SendMessageW(ctx->hwnd,WM_APP+2,0,(LPARAM)op);return op->result;
}
int kc_wvw_open(kc_wvw_t **out,const kc_wvw_options_t *options){
    kc_wvw_t *ctx;if(out)*out=NULL;if(!out||!options)return KC_WVW_ERROR;
    ctx=(kc_wvw_t *)calloc(1,sizeof(*ctx));if(!ctx)return KC_WVW_ERROR;ctx->ref_count=1;
    if(kc_wvw_config_copy(&ctx->opts,options)!=KC_WVW_OK){free(ctx);return KC_WVW_ERROR;}
    ctx->ready=CreateEventW(NULL,TRUE,FALSE,NULL);ctx->closed_event=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!ctx->ready||!ctx->closed_event){if(ctx->ready)CloseHandle(ctx->ready);if(ctx->closed_event)CloseHandle(ctx->closed_event);kc_wvw_config_free(&ctx->opts);free(ctx);return KC_WVW_ERROR;}
    ctx->thread=CreateThread(NULL,0,kc_wvw_windows_worker,ctx,0,NULL);
    if(!ctx->thread){CloseHandle(ctx->ready);CloseHandle(ctx->closed_event);kc_wvw_config_free(&ctx->opts);free(ctx);return KC_WVW_ERROR;}
    *out=ctx;WaitForSingleObject(ctx->ready,INFINITE);CloseHandle(ctx->ready);ctx->ready=NULL;return ctx->started?KC_WVW_OK:KC_WVW_ERROR;
}


/**
 * Return the last context error.
 * @param ctx Window context.
 * @return Borrowed error string, or NULL when no error is available.
 */
const char *kc_wvw_get_error(const kc_wvw_t *ctx) {
    if (!ctx || !ctx->error[0]) {
        return NULL;
    }
    return ctx->error;
}





int kc_wvw_cli_wait(kc_wvw_t *ctx){if(!ctx||!ctx->closed_event)return KC_WVW_ERROR;WaitForSingleObject(ctx->closed_event,INFINITE);return KC_WVW_OK;}
void kc_wvw_close(kc_wvw_t *ctx){if(!ctx)return;if(ctx->worker_id&&GetCurrentThreadId()==ctx->worker_id){ctx->free_on_exit=1;kc_wvw_request_close(ctx);return;}if(ctx->thread){if(ctx->hwnd&&IsWindow(ctx->hwnd))PostMessageW(ctx->hwnd,KC_WVW_CLOSE_MESSAGE,0,0);WaitForSingleObject(ctx->thread,INFINITE);CloseHandle(ctx->thread);ctx->thread=NULL;}kc_wvw_context_release(ctx);}





/**
 * Navigate the current WebView to a new URL.
 * @param ctx Window context.
 * @param url Destination URL.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_navigate_impl(kc_wvw_t *ctx, const char *url) {
    char *next_url;
    wchar_t *target;
    HRESULT hr;

    if (!ctx || !url) {
        return KC_WVW_ERROR;
    }
    if (ctx->bridge.enabled && !kc_wvw_bridge_url_trusted(ctx, &ctx->bridge, url)) {
        return KC_WVW_ERROR;
    }

    next_url = kc_wvw_strdup(url);
    if (!next_url) {
        return KC_WVW_ERROR;
    }
    free(ctx->pending_url);
    ctx->pending_url = next_url;

    if (!ctx->webview) {
        return KC_WVW_OK;
    }

    target = kc_wvw_utf16_from_utf8(ctx->pending_url);
    if (!target) {
        return KC_WVW_ERROR;
    }

    hr = ICoreWebView2_Navigate(ctx->webview, target);
    free(target);
    return FAILED(hr) ? KC_WVW_ERROR : KC_WVW_OK;
}

/**
 * Add trusted JavaScript for document-start execution in one WebView.
 * @param ctx Window context.
 * @param javascript Source text to install.
 * @return KC_WVW_OK on installation or KC_WVW_ERROR on failure.
 */
static int kc_wvw_add_init_script_impl(kc_wvw_t *ctx, const char *javascript) {
    wchar_t *wide;
    HRESULT hr;
    kc_wvw_script_handler_t *handler;

    if (!ctx || !ctx->webview || !javascript) {
        return KC_WVW_ERROR;
    }

    wide = kc_wvw_utf16_from_utf8(javascript);
    if (!wide) {
        return KC_WVW_ERROR;
    }
    handler = kc_wvw_script_handler_new();
    if (!handler) {
        free(wide);
        return KC_WVW_ERROR;
    }
    hr = ICoreWebView2_AddScriptToExecuteOnDocumentCreated(ctx->webview, wide, (ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *)handler);
    ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler_Release((ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *)handler);
    free(wide);
    return FAILED(hr) ? KC_WVW_ERROR : KC_WVW_OK;
}

/**
 * Enable one native bridge with a fixed method whitelist.
 * @param ctx Window context.
 * @param opts Bridge configuration options.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_enable_bridge_impl(kc_wvw_t *ctx, const kc_wvw_bridge_options_t *opts) {
    kc_wvw_bridge_state_t bridge;

    if (!ctx || !opts) {
        return KC_WVW_ERROR;
    }

    memset(&bridge, 0, sizeof(bridge));
    if (kc_wvw_bridge_state_copy(&bridge, opts) != KC_WVW_OK) {
        return KC_WVW_ERROR;
    }
    bridge.enabled = 1;
    if (!kc_wvw_bridge_url_trusted(ctx, &bridge, ctx->pending_url ? ctx->pending_url : ctx->opts.url)) {
        kc_wvw_bridge_state_free(&bridge);
        return KC_WVW_ERROR;
    }

    kc_wvw_bridge_state_free(&ctx->bridge);
    ctx->bridge = bridge;
    if (ctx->webview) {
        if (kc_wvw_windows_install_bridge(ctx) != KC_WVW_OK) {
            kc_wvw_bridge_state_free(&ctx->bridge);
            return KC_WVW_ERROR;
        }
    }
    return KC_WVW_OK;
}

/**
 * Deliver one native bridge event into the current WebView.
 * @param ctx Window context.
 * @param json JSON payload to dispatch as the event detail.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_post_bridge_event_impl(kc_wvw_t *ctx, const char *json) {
    return kc_wvw_bridge_post_json(ctx, json);
}

/**
 * Hide the window.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_hide_impl(kc_wvw_t *ctx) {
    if (!ctx || !ctx->hwnd) {
        return KC_WVW_ERROR;
    }
    ShowWindow(ctx->hwnd, SW_HIDE);
    return KC_WVW_OK;
}

/**
 * Show the window.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_show_impl(kc_wvw_t *ctx) {
    if (!ctx || !ctx->hwnd) {
        return KC_WVW_ERROR;
    }
    ShowWindow(ctx->hwnd, SW_SHOW);
    return KC_WVW_OK;
}

/**
 * Minimize (iconify) the window.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_minimize_impl(kc_wvw_t *ctx) {
    if (!ctx || !ctx->hwnd) {
        return KC_WVW_ERROR;
    }
    ShowWindow(ctx->hwnd, SW_MINIMIZE);
    return KC_WVW_OK;
}

/**
 * Maximize the window.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_maximize_impl(kc_wvw_t *ctx) {
    if (!ctx || !ctx->hwnd) {
        return KC_WVW_ERROR;
    }
    ShowWindow(ctx->hwnd, SW_MAXIMIZE);
    return KC_WVW_OK;
}

/**
 * Restore the window from minimized or maximized state.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_restore_impl(kc_wvw_t *ctx) {
    if (!ctx || !ctx->hwnd) {
        return KC_WVW_ERROR;
    }
    ShowWindow(ctx->hwnd, SW_RESTORE);
    return KC_WVW_OK;
}

/**
 * Set the native window title.
 * @param ctx Window context.
 * @param title UTF-8 title string.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_set_title_impl(kc_wvw_t *ctx, const char *title) {
    wchar_t *wtitle;

    if (!ctx || !ctx->hwnd || !title) {
        return KC_WVW_ERROR;
    }
    if (strlen(title) > KC_WVW_TITLE_MAX) {
        return KC_WVW_ERROR;
    }

    wtitle = kc_wvw_utf16_from_utf8(title);
    if (!wtitle) {
        return KC_WVW_ERROR;
    }

    SetWindowTextW(ctx->hwnd,wtitle);free(wtitle);{char *copy=kc_wvw_strdup(title);if(!copy)return KC_WVW_ERROR;free(ctx->opts.title);ctx->opts.title=copy;}return KC_WVW_OK;
}

/**
 * Resize the native window content area.
 * @param ctx Window context.
 * @param width Width in pixels.
 * @param height Height in pixels.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_set_size_impl(kc_wvw_t *ctx, int width, int height) {
    DWORD style;
    RECT rect;

    if (!ctx || !ctx->hwnd || width <= 0 || height <= 0) {
        return KC_WVW_ERROR;
    }
    if (width > KC_WVW_SIZE_MAX || height > KC_WVW_SIZE_MAX) {
        return KC_WVW_ERROR;
    }

    style = (DWORD)GetWindowLongPtrW(ctx->hwnd, GWL_STYLE);
    rect.left = 0;
    rect.top = 0;
    rect.right = width;
    rect.bottom = height;
    AdjustWindowRectEx(&rect, style, FALSE, 0);
    SetWindowPos(ctx->hwnd, NULL, 0, 0,
        rect.right - rect.left, rect.bottom - rect.top,
        SWP_NOMOVE | SWP_NOZORDER);
    return KC_WVW_OK;
}

/**
 * Query the current window state.
 * @param ctx Window context.
 * @param state Destination state structure.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_get_state_impl(kc_wvw_t *ctx, kc_wvw_window_state_t *state) {
    RECT rect;
    LONG style;

    if (!ctx || !ctx->hwnd || !state) {
        return KC_WVW_ERROR;
    }

    memset(state, 0, sizeof(*state));
    style = GetWindowLongPtrW(ctx->hwnd, GWL_STYLE);
    state->maximized = !!(style & WS_MAXIMIZE);
    state->minimized = !!(style & WS_MINIMIZE);
    state->fullscreen = !!(style & WS_POPUP);
    state->visible = IsWindowVisible(ctx->hwnd);
    if (GetClientRect(ctx->hwnd, &rect)) {
        state->width = rect.right - rect.left;
        state->height = rect.bottom - rect.top;
    }
    return KC_WVW_OK;
}

/**
 * Extract a string parameter from a bridge JSON params object.
 * @param params JSON params string.
 * @param key Parameter key.
 * @return Newly allocated string or NULL.
 */
static char *kc_wvw_bridge_param_string(const char *params, const char *key) {
    JSON_Value *root;
    JSON_Object *obj;
    const char *val;
    char *copy;

    if (!params || !key) return NULL;

    root = json_parse_string(params);
    if (!root || json_value_get_type(root) != JSONObject) {
        json_value_free(root);
        return NULL;
    }
    obj = json_value_get_object(root);
    val = json_object_get_string(obj, key);
    copy = val ? kc_wvw_strdup(val) : NULL;
    json_value_free(root);
    return copy;
}

/**
 * Extract an integer parameter from a bridge JSON params object.
 * @param params JSON params string.
 * @param key Parameter key.
 * @param out Destination for the parsed integer.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_bridge_param_int(const char *params, const char *key, int *out) {
    JSON_Value *root;
    JSON_Object *obj;
    JSON_Value *v;
    double d;

    if (!params || !key || !out) return KC_WVW_ERROR;

    root = json_parse_string(params);
    if (!root || json_value_get_type(root) != JSONObject) {
        json_value_free(root);
        return KC_WVW_ERROR;
    }
    obj = json_value_get_object(root);
    v = json_object_get_value(obj, key);
    if (!v || json_value_get_type(v) != JSONNumber) {
        json_value_free(root);
        return KC_WVW_ERROR;
    }
    d = json_value_get_number(v);
    if (d < (double)INT32_MIN || d > (double)INT32_MAX || d != (double)(int64_t)d) {
        json_value_free(root);
        return KC_WVW_ERROR;
    }
    *out = (int)d;
    json_value_free(root);
    return KC_WVW_OK;
}

/**
 * Return whether a string holds one complete valid JSON value.
 * @param json Candidate result JSON.
 * @return Non-zero when json parses to one JSON value with no trailing data.
 */
static size_t kc_wvw_json_first_value_end(const char *s, size_t i) {
    switch (s[i]) {
        case '{': {
            i++;
            if (s[i] == '}') return i + 1;
            for (;;) {
                while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') i++;
                if (s[i] != '"') return (size_t)-1;
                i++;
                for (;;) {
                    if (s[i] == '\0' || s[i] == '\n' || s[i] == '\r') return (size_t)-1;
                    if (s[i] == '\\') {
                        if (s[i + 1] == '\0') return (size_t)-1;
                        i += 2;
                        continue;
                    }
                    if (s[i] == '"') { i++; break; }
                    i++;
                }
                while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') i++;
                if (s[i] == ':') i++; else return (size_t)-1;
                while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') i++;
                i = kc_wvw_json_first_value_end(s, i);
                if (i == (size_t)-1) return (size_t)-1;
                while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') i++;
                if (s[i] == ',') { i++; continue; }
                if (s[i] == '}') return i + 1;
                return (size_t)-1;
            }
        }
        case '[': {
            i++;
            if (s[i] == ']') return i + 1;
            for (;;) {
                while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') i++;
                i = kc_wvw_json_first_value_end(s, i);
                if (i == (size_t)-1) return (size_t)-1;
                while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') i++;
                if (s[i] == ',') { i++; continue; }
                if (s[i] == ']') return i + 1;
                return (size_t)-1;
            }
        }
        case '"': {
            i++;
            for (;;) {
                if (s[i] == '\0' || s[i] == '\n' || s[i] == '\r') return (size_t)-1;
                if (s[i] == '\\') {
                    if (s[i + 1] == '\0') return (size_t)-1;
                    i += 2;
                    continue;
                }
                if (s[i] == '"') return i + 1;
                i++;
            }
        }
        case 't':
            if (strncmp(s + i, "true", 4) == 0) return i + 4;
            break;
        case 'f':
            if (strncmp(s + i, "false", 5) == 0) return i + 5;
            break;
        case 'n':
            if (strncmp(s + i, "null", 4) == 0) return i + 4;
            break;
        default: {
            size_t j = i;
            if (s[j] == '-') j++;
            if (s[j] >= '0' && s[j] <= '9') {
                while (s[j] >= '0' && s[j] <= '9') j++;
                if (s[j] == '.') {
                    j++;
                    while (s[j] >= '0' && s[j] <= '9') j++;
                }
                if (s[j] == 'e' || s[j] == 'E') {
                    j++;
                    if (s[j] == '+' || s[j] == '-') j++;
                    while (s[j] >= '0' && s[j] <= '9') j++;
                }
                return j;
            }
            break;
        }
    }
    return (size_t)-1;
}

/**
 * Validate that one JSON value occupies the complete input.
 * Leading whitespace is allowed, but trailing non-whitespace data is not.
 * @param json Candidate JSON text.
 * @return Non-zero when exactly one complete JSON value is present.
 */
static int kc_wvw_json_valid_value(const char *json) {
    JSON_Value *v;
    size_t i = 0;
    size_t end;

    if (!json) {
        return 0;
    }
    v = json_parse_string(json);
    if (!v) {
        return 0;
    }
    json_value_free(v);
    while (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r') i++;
    if (json[i] == '\0') {
        return 0;
    }
    end = kc_wvw_json_first_value_end(json, i);
    if (end == (size_t)-1) {
        return 0;
    }
    while (json[end] == ' ' || json[end] == '\t' || json[end] == '\n' || json[end] == '\r') end++;
    return json[end] == '\0';
}

/**
 * Dispatch one bridge request into the application callback or
 * built-in handler.
 * @param ctx Window context.
 * @param json Request payload.
 * @return Newly allocated response payload or NULL.
 */
static char *kc_wvw_bridge_dispatch_request(kc_wvw_t *ctx, const char *json) {
    char *id;
    char *method;
    char *params;
    char *result = NULL;
    const char *callback_result = NULL;
    char *error;
    char *response;
    int rc;

    if (!ctx || !json || strlen(json) > KC_WVW_BRIDGE_MAX_MESSAGE) {
        error = kc_wvw_bridge_error_object("INVALID_REQUEST", "Bridge request is invalid.");
        response = error ? kc_wvw_bridge_wrap_response("0", 0, error) : NULL;
        free(error);
        return response;
    }

    id = kc_wvw_bridge_get_string(json, "id");
    method = kc_wvw_bridge_get_string(json, "method");
    params = kc_wvw_bridge_get_params(json);
    if (!id || !method || !params) {
        free(id);
        free(method);
        free(params);
        error = kc_wvw_bridge_error_object("INVALID_REQUEST", "Bridge request is malformed.");
        response = error ? kc_wvw_bridge_wrap_response("0", 0, error) : NULL;
        free(error);
        return response;
    }

    if (strcmp(method, "minimize") == 0) {
        rc = kc_wvw_minimize(ctx);
        if (rc == KC_WVW_OK) {
            result = kc_wvw_strdup("{\"ok\":true}");
            response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
        } else {
            result = NULL;
            error = kc_wvw_bridge_error_object("WINDOW_CLOSED", "Window is not available.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
        }
        free(id); free(method); free(params); free(result);
        return response;
    }
    if (strcmp(method, "maximize") == 0) {
        rc = kc_wvw_maximize(ctx);
        if (rc == KC_WVW_OK) {
            result = kc_wvw_strdup("{\"ok\":true}");
            response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
        } else {
            result = NULL;
            error = kc_wvw_bridge_error_object("WINDOW_CLOSED", "Window is not available.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
        }
        free(id); free(method); free(params); free(result);
        return response;
    }
    if (strcmp(method, "restore") == 0) {
        rc = kc_wvw_restore(ctx);
        if (rc == KC_WVW_OK) {
            result = kc_wvw_strdup("{\"ok\":true}");
            response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
        } else {
            result = NULL;
            error = kc_wvw_bridge_error_object("WINDOW_CLOSED", "Window is not available.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
        }
        free(id); free(method); free(params); free(result);
        return response;
    }
    if (strcmp(method, "close") == 0) {
        kc_wvw_request_close(ctx);
        result = kc_wvw_strdup("{\"ok\":true}");
        response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
        free(id); free(method); free(params); free(result);
        return response;
    }
    if (strcmp(method, "setTitle") == 0) {
        char *title = kc_wvw_bridge_param_string(params, "title");
        if (!title) {
            error = kc_wvw_bridge_error_object("INVALID_ARGUMENT", "title must be a string.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
            free(id); free(method); free(params);
            return response;
        }
        if (strlen(title) > KC_WVW_TITLE_MAX) {
            free(title);
            error = kc_wvw_bridge_error_object("INVALID_ARGUMENT", "title exceeds maximum length.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
            free(id); free(method); free(params);
            return response;
        }
        rc = kc_wvw_set_title(ctx, title);
        free(title);
        if (rc == KC_WVW_OK) {
            result = kc_wvw_strdup("{\"ok\":true}");
            response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
        } else {
            result = NULL;
            error = kc_wvw_bridge_error_object("WINDOW_CLOSED", "Window is not available.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
        }
        free(id); free(method); free(params); free(result);
        return response;
    }
    if (strcmp(method, "setSize") == 0) {
        int width = 0, height = 0;
        if (kc_wvw_bridge_param_int(params, "width", &width) != KC_WVW_OK ||
            kc_wvw_bridge_param_int(params, "height", &height) != KC_WVW_OK) {
            error = kc_wvw_bridge_error_object("INVALID_ARGUMENT", "width and height must be integers.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
            free(id); free(method); free(params);
            return response;
        }
        if (width <= 0 || height <= 0 || width > KC_WVW_SIZE_MAX || height > KC_WVW_SIZE_MAX) {
            error = kc_wvw_bridge_error_object("INVALID_ARGUMENT", "width and height must be positive integers within bounds.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
            free(id); free(method); free(params);
            return response;
        }
        rc = kc_wvw_set_size(ctx, width, height);
        if (rc == KC_WVW_OK) {
            result = kc_wvw_strdup("{\"ok\":true}");
            response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
        } else {
            result = NULL;
            error = kc_wvw_bridge_error_object("WINDOW_CLOSED", "Window is not available.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
        }
        free(id); free(method); free(params); free(result);
        return response;
    }
    if (strcmp(method, "getState") == 0) {
        kc_wvw_window_state_t st;
        JSON_Value *rv;
        JSON_Object *ro;
        JSON_Value *sv;
        JSON_Object *so;
        rc = kc_wvw_get_state(ctx, &st);
        if (rc == KC_WVW_OK) {
            rv = json_value_init_object();
            ro = json_value_get_object(rv);
            json_object_set_boolean(ro, "ok", 1);
            sv = json_value_init_object();
            so = json_value_get_object(sv);
            json_object_set_number(so, "width", st.width);
            json_object_set_number(so, "height", st.height);
            json_object_set_boolean(so, "minimized", st.minimized);
            json_object_set_boolean(so, "maximized", st.maximized);
            json_object_set_boolean(so, "fullscreen", st.fullscreen);
            json_object_set_boolean(so, "visible", st.visible);
            json_object_set_value(ro, "state", sv);
            result = json_serialize_to_string(rv);
            json_value_free(rv);
            response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
        } else {
            result = NULL;
            error = kc_wvw_bridge_error_object("WINDOW_CLOSED", "Window is not available.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
        }
        free(id); free(method); free(params); free(result);
        return response;
    }

    if (!kc_wvw_bridge_method_allowed(&ctx->bridge, method)) {
        error = kc_wvw_bridge_error_object("METHOD_NOT_FOUND", "Bridge method is not allowed.");
        response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
        free(id);
        free(method);
        free(params);
        free(error);
        return response;
    }

    if(!ctx->bridge.callback){error=kc_wvw_bridge_error_object("METHOD_NOT_FOUND","No bridge callback registered.");response=error?kc_wvw_bridge_wrap_response(id,0,error):NULL;free(id);free(method);free(params);free(error);return response;}
    rc=ctx->bridge.callback(ctx,method,params,&callback_result,ctx->bridge.userdata);
    if(rc==KC_WVW_OK){if(!callback_result)callback_result="null";if(kc_wvw_json_valid_value(callback_result))response=kc_wvw_bridge_wrap_response(id,1,callback_result);else{error=kc_wvw_bridge_error_object("INVALID_RESPONSE","Bridge callback returned invalid JSON.");response=error?kc_wvw_bridge_wrap_response(id,0,error):NULL;free(error);}}
    else{if(callback_result&&kc_wvw_json_valid_value(callback_result))response=kc_wvw_bridge_wrap_response(id,0,callback_result);else{error=kc_wvw_bridge_error_object("OPERATION_FAILED","Bridge callback failed.");response=error?kc_wvw_bridge_wrap_response(id,0,error):NULL;free(error);}}
    free(id);free(method);free(params);return response;
}

#else

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <Foundation/Foundation.h>
#import <WebKit/WebKit.h>
#include <objc/runtime.h>
#else
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#endif

#include "libwvw.h"
#include "parson.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KC_WVW_BRIDGE_MAX_MESSAGE 65536

typedef struct {
    char **methods;
    size_t method_count;
    kc_wvw_bridge_callback_t callback;
    void *userdata;
    int allow_file;
    int allow_data;
    int allow_localhost;
    int enabled;
} kc_wvw_bridge_state_t;

#if defined(__APPLE__)

struct kc_wvw {
    kc_wvw_config_t opts;
    int running, closing, closed, cli_waiting;
    void *ns_window;
    void *ns_webview;
    void *ns_script_handler;
    void *ns_nav_delegate;
    void *ns_window_delegate;
    kc_wvw_bridge_state_t bridge;
    char error[256];
};

#else

struct kc_wvw {
    kc_wvw_config_t opts;
    int running, closed;
    GMainContext *context;
    GThread *thread;
    GMutex mutex;
    GCond cond;
    GtkWidget *window;
    WebKitWebView *web_view;
    kc_wvw_bridge_state_t bridge;
    char error[256];
};

#endif

static int kc_wvw_bridge_url_trusted(kc_wvw_t *ctx, kc_wvw_bridge_state_t *bridge, const char *url);
static void kc_wvw_bridge_state_free(kc_wvw_bridge_state_t *bridge);
static int kc_wvw_bridge_state_copy(kc_wvw_bridge_state_t *dst, const kc_wvw_bridge_options_t *src);
static char *kc_wvw_bridge_bootstrap_script(kc_wvw_bridge_state_t *bridge);
static int kc_wvw_bridge_post_json(kc_wvw_t *ctx, const char *json);
static char *kc_wvw_bridge_dispatch_request(kc_wvw_t *ctx, const char *json);

/**
 * Sets an error message on the context.
 * @param ctx Window context.
 * @param fmt Printf-style format string.
 * @param ... Format arguments.
 * @return None.
 */
static void kc_wvw_set_error(kc_wvw_t *ctx, const char *fmt, ...) {
    va_list ap;
    if (!ctx || !fmt) return;
    va_start(ap, fmt);
    vsnprintf(ctx->error, sizeof(ctx->error), fmt, ap);
    va_end(ap);
    ctx->error[sizeof(ctx->error) - 1] = '\0';
}

#ifndef KC_WVW_BUILD_VERSION
#define KC_WVW_BUILD_VERSION 0
#endif

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_wvw_version(void) {
    return (uint64_t)KC_WVW_BUILD_VERSION;
}

/**
 * Duplicate one C string.
 * @param text Source string.
 * @return Newly allocated duplicate or NULL.
 */
static char *kc_wvw_strdup(const char *text) {
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

static void kc_wvw_config_free(kc_wvw_config_t *config) {
    if (!config) return;
    free(config->url); free(config->title); free(config->background);
    memset(config, 0, sizeof(*config));
}

static int kc_wvw_config_copy(kc_wvw_config_t *config, const kc_wvw_options_t *options) {
    if (!config || !options || !options->url || !options->url[0]) return KC_WVW_ERROR;
    memset(config, 0, sizeof(*config));
    config->url = kc_wvw_strdup(options->url);
    config->title = kc_wvw_strdup(options->title ? options->title : "wvw");
    config->background = options->background ? kc_wvw_strdup(options->background) : NULL;
    if (!config->url || !config->title || (options->background && !config->background)) {
        kc_wvw_config_free(config); return KC_WVW_ERROR;
    }
    config->width = options->width ? *options->width : 1280;
    config->height = options->height ? *options->height : 720;
    config->has_posx = options->posx != NULL; config->has_posy = options->posy != NULL;
    config->posx = options->posx ? *options->posx : 0; config->posy = options->posy ? *options->posy : 0;
    config->fullscreen = options->fullscreen ? !!*options->fullscreen : 0;
    config->borderless = options->borderless ? !!*options->borderless : 0;
    config->always_on_top = options->always_on_top ? !!*options->always_on_top : 0;
    config->click_through = options->click_through ? !!*options->click_through : 0;
    config->no_focus = options->no_focus ? !!*options->no_focus : 0;
    if (config->width <= 0 || config->height <= 0 ||
        config->width > KC_WVW_SIZE_MAX || config->height > KC_WVW_SIZE_MAX ||
        strlen(config->title) > KC_WVW_TITLE_MAX) {
        kc_wvw_config_free(config); return KC_WVW_ERROR;
    }
    return KC_WVW_OK;
}


typedef struct {
    char *data;
    size_t len;
    size_t cap;
} kc_wvw_text_buf_t;

/**
 * Initialize one text buffer.
 * @param buf Buffer state.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_text_buf_init(kc_wvw_text_buf_t *buf) {
    if (!buf) {
        return KC_WVW_ERROR;
    }

    buf->data = (char *)malloc(256);
    if (!buf->data) {
        return KC_WVW_ERROR;
    }

    buf->data[0] = '\0';
    buf->len = 0;
    buf->cap = 256;
    return KC_WVW_OK;
}

/**
 * Append one byte range into the text buffer.
 * @param buf Buffer state.
 * @param text Source byte range.
 * @param len Source length.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_text_buf_append_n(kc_wvw_text_buf_t *buf, const char *text, size_t len) {
    char *next;
    size_t cap;

    if (!buf || !buf->data || (!text && len != 0)) {
        return KC_WVW_ERROR;
    }

    if (buf->len + len + 1 > buf->cap) {
        cap = buf->cap;
        while (buf->len + len + 1 > cap) {
            cap *= 2;
        }
        next = (char *)realloc(buf->data, cap);
        if (!next) {
            return KC_WVW_ERROR;
        }
        buf->data = next;
        buf->cap = cap;
    }

    if (len > 0) {
        memcpy(buf->data + buf->len, text, len);
        buf->len += len;
    }
    buf->data[buf->len] = '\0';
    return KC_WVW_OK;
}

/**
 * Append one C string into the text buffer.
 * @param buf Buffer state.
 * @param text Source text.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_text_buf_append(kc_wvw_text_buf_t *buf, const char *text) {
    if (!text) {
        return KC_WVW_ERROR;
    }

    return kc_wvw_text_buf_append_n(buf, text, strlen(text));
}

/**
 * Return whether one bridge method name is safe for JS property injection.
 * @param method Candidate method name.
 * @return Non-zero when the identifier is accepted, otherwise zero.
 */
static int kc_wvw_bridge_method_valid(const char *method) {
    size_t i;

    static const char *const reserved[] = {
        "minimize", "maximize", "restore", "close",
        "setTitle", "setSize", "getState"
    };

    if (!method || !method[0]) {
        return 0;
    }

    for (i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++) {
        if (strcmp(method, reserved[i]) == 0) {
            return 0;
        }
    }

    if (!((method[0] >= 'A' && method[0] <= 'Z') || (method[0] >= 'a' && method[0] <= 'z') || method[0] == '_' || method[0] == '$')) {
        return 0;
    }

    for (i = 1; method[i]; i++) {
        if (!((method[i] >= 'A' && method[i] <= 'Z') || (method[i] >= 'a' && method[i] <= 'z') || (method[i] >= '0' && method[i] <= '9') || method[i] == '_' || method[i] == '$')) {
            return 0;
        }
    }

    return 1;
}

/**
 * Extract the origin scheme and host from a URL string.
 * @param url Candidate URL.
 * @return Newly allocated origin string or NULL.
 */
static char *kc_wvw_extract_origin(const char *url) {
    const char *p;
    const char *q;
    const char *end;
    const char *port;
    size_t scheme_len;
    size_t authority_len;
    char *out;

    if (!url) {
        return NULL;
    }
    p = strstr(url, "://");
    if (!p || p == url) {
        return NULL;
    }
    if (!((url[0] >= 'A' && url[0] <= 'Z') || (url[0] >= 'a' && url[0] <= 'z'))) {
        return NULL;
    }
    for (q = url + 1; q < p; q++) {
        if (!(((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') ||
            (*q >= '0' && *q <= '9') || *q == '+' || *q == '-' || *q == '.'))) {
            return NULL;
        }
    }
    scheme_len = (size_t)(p - url) + 3;
    q = p + 3;
    end = q;
    while (*end && *end != '/' && *end != '?' && *end != '#') {
        if (*end == '@' || *end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') {
            return NULL;
        }
        end++;
    }
    if (end == q) {
        return NULL;
    }
    port = NULL;
    for (p = q; p < end; p++) {
        if (*p == ':') {
            if (port) {
                return NULL;
            }
            port = p + 1;
        }
    }
    if (port) {
        if (port == end || port == q) {
            return NULL;
        }
        for (p = port; p < end; p++) {
            if (*p < '0' || *p > '9') {
                return NULL;
            }
        }
    }
    authority_len = (size_t)(end - q);
    out = (char *)malloc(scheme_len + authority_len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, url, scheme_len + authority_len);
    out[scheme_len + authority_len] = '\0';
    return out;
}

/**
 * Search for an origin string within a space-separated list of origins.
 * @param origin Target origin.
 * @param list Space-separated list of origins.
 * @return Non-zero if found, otherwise zero.
 */
static int kc_wvw_is_origin_in_list(const char *origin, const char *list) {
    const char *p;
    const char *next;
    size_t len;
    size_t origin_len;

    if (!origin || !list) {
        return 0;
    }
    origin_len = strlen(origin);
    p = list;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (!*p) {
            break;
        }
        next = p;
        while (*next && *next != ' ' && *next != '\t' && *next != '\r' && *next != '\n') {
            next++;
        }
        len = (size_t)(next - p);
        if (len == origin_len && strncmp(p, origin, len) == 0) {
            return 1;
        }
        p = next;
    }
    return 0;
}

/**
 * Return whether a URL hostname is exactly "localhost".
 * @param url Candidate URL.
 * @return Non-zero only when the authority host is exactly localhost.
 */
static int kc_wvw_url_is_localhost(const char *url) {
    const char *p;
    const char *host;
    const char *port;
    static const char local[] = "localhost";
    int i;

    if (!url) {
        return 0;
    }
    if (strncmp(url, "http://", 7) == 0) {
        p = url + 7;
    } else if (strncmp(url, "https://", 8) == 0) {
        p = url + 8;
    } else {
        return 0;
    }
    host = p;
    while (*host && *host != '@') {
        host++;
    }
    if (*host == '@') {
        return 0;
    }
    host = p;
    while (*host && *host != ':' && *host != '/' && *host != '?' && *host != '#') {
        host++;
    }
    if ((size_t)(host - p) != 9) {
        return 0;
    }
    for (i = 0; i < 9; i++) {
        char c = p[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c + ('a' - 'A'));
        }
        if (c != local[i]) {
            return 0;
        }
    }
    if (*host == ':') {
        port = host + 1;
        if (*port == '\0' || *port == '/' || *port == '?' || *port == '#') {
            return 0;
        }
        for (; *port; port++) {
            if (*port < '0' || *port > '9') {
                if (*port == '/' || *port == '?' || *port == '#') {
                    break;
                }
                return 0;
            }
        }
    }
    return 1;
}

/**
 * Return whether one URL is trusted for the current bridge policy.
 * @param ctx Window context.
 * @param bridge Bridge state.
 * @param url Candidate URL.
 * @return Non-zero when the URL is accepted, otherwise zero.
 */
static int kc_wvw_bridge_url_trusted(kc_wvw_t *ctx, kc_wvw_bridge_state_t *bridge, const char *url) {
    char *url_origin;
    char *init_origin;
    const char *env_trusted;
    int trusted = 0;

    if (!bridge || !url) {
        return 0;
    }

    if (bridge->allow_file && strncmp(url, "file://", 7) == 0) {
        return 1;
    }
    if (bridge->allow_data && strncmp(url, "data:", 5) == 0) {
        return 1;
    }
    if (bridge->allow_localhost && kc_wvw_url_is_localhost(url)) {
        return 1;
    }

    url_origin = kc_wvw_extract_origin(url);
    if (!url_origin) {
        return 0;
    }

    env_trusted = getenv("TRUSTED_ORIGINS");
    if (env_trusted && kc_wvw_is_origin_in_list(url_origin, env_trusted)) {
        trusted = 1;
    }

    if (!trusted && ctx && ctx->opts.url) {
        init_origin = kc_wvw_extract_origin(ctx->opts.url);
        if (init_origin) {
            if (strcmp(url_origin, init_origin) == 0) {
                trusted = 1;
            }
            free(init_origin);
        }
    }

    free(url_origin);
    return trusted;
}

/**
 * Release one copied bridge configuration.
 * @param bridge Bridge state.
 * @return None.
 */
static void kc_wvw_bridge_state_free(kc_wvw_bridge_state_t *bridge) {
    int i;

    if (!bridge) {
        return;
    }

    if (bridge->methods) {
        for (i = 0; i < bridge->method_count; i++) {
            free(bridge->methods[i]);
        }
        free(bridge->methods);
    }

    memset(bridge, 0, sizeof(*bridge));
}

/**
 * Copy one bridge configuration into the context state.
 * @param dst Destination bridge state.
 * @param src Source bridge options.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_bridge_state_copy(kc_wvw_bridge_state_t *dst, const kc_wvw_bridge_options_t *src) {
    int i;

    if (!dst || !src || src->method_count < 0) {
        return KC_WVW_ERROR;
    }

    if (src->method_count > 0 && (!src->methods || !src->callback)) {
        return KC_WVW_ERROR;
    }

    if (src->method_count > 0) {
        dst->methods = (char **)calloc((size_t)src->method_count, sizeof(char *));
        if (!dst->methods) {
            return KC_WVW_ERROR;
        }
    }

    dst->method_count = src->method_count;
    for (i = 0; i < src->method_count; i++) {
        if (!kc_wvw_bridge_method_valid(src->methods[i])) {
            kc_wvw_bridge_state_free(dst);
            return KC_WVW_ERROR;
        }
        dst->methods[i] = kc_wvw_strdup(src->methods[i]);
        if (!dst->methods[i]) {
            kc_wvw_bridge_state_free(dst);
            return KC_WVW_ERROR;
        }
    }

    dst->callback = src->callback;
    dst->userdata = src->userdata;
    dst->allow_file = src->allow_file;
    dst->allow_data = src->allow_data;
    dst->allow_localhost = src->allow_localhost;

    return KC_WVW_OK;
}

/**
 * Return whether one method belongs to the current whitelist.
 * @param bridge Bridge state.
 * @param method Candidate method name.
 * @return Non-zero when the method is accepted, otherwise zero.
 */
static int kc_wvw_bridge_method_allowed(kc_wvw_bridge_state_t *bridge, const char *method) {
    int i;

    if (!bridge || !method) {
        return 0;
    }

    for (i = 0; i < bridge->method_count; i++) {
        if (strcmp(bridge->methods[i], method) == 0) {
            return 1;
        }
    }

    return 0;
}

/**
 * Extract one string field from a bridge request.
 * @param json Source JSON payload.
 * @param key Field name.
 * @return Newly allocated string value or NULL.
 */
static char *kc_wvw_bridge_get_string(const char *json, const char *key) {
    JSON_Value *root;
    JSON_Object *obj;
    const char *val;
    char *copy;

    if (!json || !key) return NULL;

    root = json_parse_string(json);
    if (!root || json_value_get_type(root) != JSONObject) {
        json_value_free(root);
        return NULL;
    }
    obj = json_value_get_object(root);
    val = json_object_get_string(obj, key);
    copy = val ? kc_wvw_strdup(val) : NULL;
    json_value_free(root);
    return copy;
}

/**
 * Extract the params payload from one bridge request.
 * @param json Source JSON payload.
 * @return Newly allocated JSON fragment or NULL.
 */
static char *kc_wvw_bridge_get_params(const char *json) {
    JSON_Value *root;
    JSON_Object *obj;
    JSON_Value *params_val;
    char *serialized;

    if (!json) return NULL;

    root = json_parse_string(json);
    if (!root || json_value_get_type(root) != JSONObject) {
        json_value_free(root);
        return NULL;
    }
    obj = json_value_get_object(root);
    params_val = json_object_get_value(obj, "params");
    if (!params_val) {
        json_value_free(root);
        return kc_wvw_strdup("null");
    }
    serialized = json_serialize_to_string(params_val);
    json_value_free(root);
    return serialized;
}

/**
 * Build one bridge response payload.
 * @param id Request identifier.
 * @param ok Success flag.
 * @param body Serialized result or error object.
 * @return Newly allocated response payload or NULL.
 */
static char *kc_wvw_bridge_wrap_response(const char *id, int ok, const char *body) {
    JSON_Value *root;
    JSON_Value *body_val;
    char *out;

    if (!id || !body) return NULL;

    root = json_value_init_object();
    if (!root) return NULL;

    json_object_set_string(json_value_get_object(root), "id", id);
    json_object_set_boolean(json_value_get_object(root), "ok", ok ? 1 : 0);

    body_val = json_parse_string(body);
    if (!body_val) {
        json_value_free(root);
        return NULL;
    }
    json_object_set_value(json_value_get_object(root),
        ok ? "result" : "error", body_val);

    out = json_serialize_to_string(root);
    json_value_free(root);
    return out;
}

/**
 * Build one JSON error object.
 * @param code Error code string.
 * @param message Error message string.
 * @return Newly allocated JSON error object or NULL.
 */
static char *kc_wvw_bridge_error_object(const char *code, const char *message) {
    JSON_Value *root;
    char *out;

    if (!code || !message) return NULL;

    root = json_value_init_object();
    if (!root) return NULL;

    json_object_set_string(json_value_get_object(root), "code", code);
    json_object_set_string(json_value_get_object(root), "message", message);

    out = json_serialize_to_string(root);
    json_value_free(root);
    return out;
}

/**
 * Build the injected NativeBridge bootstrap script.
 * @param bridge Bridge state.
 * @return Newly allocated script text or NULL.
 */
static char *kc_wvw_bridge_bootstrap_script(kc_wvw_bridge_state_t *bridge) {
    kc_wvw_text_buf_t buf;
    int i;

    if (!bridge || kc_wvw_text_buf_init(&buf) != KC_WVW_OK) {
        return NULL;
    }

    if (kc_wvw_text_buf_append(&buf, "(function(){if(window.NativeBridge){return;}var __kcWvwPending={};var __kcWvwSeq=0;function __kcWvwReceive(msg){if(msg&&typeof msg.id==='string'){var p=__kcWvwPending[msg.id];if(p){delete __kcWvwPending[msg.id];if(msg.ok){p.resolve(msg.result!==undefined?msg.result:{ok:true});}else{p.reject(msg.error||{code:'INTERNAL_ERROR',message:'Bridge error'});}}return;}window.dispatchEvent(new CustomEvent('") != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, KC_WVW_BRIDGE_EVENT_NAME) != KC_WVW_OK ||
        kc_wvw_text_buf_append(&buf, "',{detail:msg}));}window.__kcWvwReceive=__kcWvwReceive;window.NativeBridge={};function __kcWvwSend(method,params){return new Promise(function(resolve,reject){var id=String(++__kcWvwSeq);__kcWvwPending[id]={resolve:resolve,reject:reject};window.webkit.messageHandlers.kc_wvw_native.postMessage(JSON.stringify({id:id,method:method,params:params===undefined?null:params}));});}window.NativeBridge.minimize=function(){return __kcWvwSend('minimize');};window.NativeBridge.maximize=function(){return __kcWvwSend('maximize');};window.NativeBridge.restore=function(){return __kcWvwSend('restore');};window.NativeBridge.close=function(){return __kcWvwSend('close');};window.NativeBridge.setTitle=function(title){return __kcWvwSend('setTitle',{title:title});};window.NativeBridge.setSize=function(width,height){return __kcWvwSend('setSize',{width:width,height:height});};window.NativeBridge.getState=function(){return __kcWvwSend('getState');};") != KC_WVW_OK) {
        free(buf.data);
        return NULL;
    }

    for (i = 0; i < bridge->method_count; i++) {
        if (kc_wvw_text_buf_append(&buf, "window.NativeBridge.") != KC_WVW_OK ||
            kc_wvw_text_buf_append(&buf, bridge->methods[i]) != KC_WVW_OK ||
            kc_wvw_text_buf_append(&buf, "=function(params){return __kcWvwSend('") != KC_WVW_OK ||
            kc_wvw_text_buf_append(&buf, bridge->methods[i]) != KC_WVW_OK ||
            kc_wvw_text_buf_append(&buf, "',params);};") != KC_WVW_OK) {
            free(buf.data);
            return NULL;
        }
    }

    if (kc_wvw_text_buf_append(&buf, "}());") != KC_WVW_OK) {
        free(buf.data);
        return NULL;
    }

    return buf.data;
}

/**
 * Escape one JSON payload for direct JavaScript evaluation.
 * @param json JSON payload.
 * @return Newly allocated escaped string or NULL.
 */
static char *kc_wvw_bridge_escape_js_string(const char *json) {
    kc_wvw_text_buf_t buf;
    size_t i;

    if (!json || kc_wvw_text_buf_init(&buf) != KC_WVW_OK) {
        return NULL;
    }

    for (i = 0; json[i]; i++) {
        unsigned char c = (unsigned char)json[i];
        if (c == '\\' || c == '\'') {
            if (kc_wvw_text_buf_append_n(&buf, "\\", 1) != KC_WVW_OK) {
                free(buf.data);
                return NULL;
            }
            if (kc_wvw_text_buf_append_n(&buf, &json[i], 1) != KC_WVW_OK) {
                free(buf.data);
                return NULL;
            }
        } else if (c == '\n') {
            if (kc_wvw_text_buf_append(&buf, "\\n") != KC_WVW_OK) {
                free(buf.data);
                return NULL;
            }
        } else if (c == '\r') {
            if (kc_wvw_text_buf_append(&buf, "\\r") != KC_WVW_OK) {
                free(buf.data);
                return NULL;
            }
        } else if (c == '\t') {
            if (kc_wvw_text_buf_append(&buf, "\\t") != KC_WVW_OK) {
                free(buf.data);
                return NULL;
            }
        } else if (c == 0x7F) {
            if (kc_wvw_text_buf_append(&buf, "\\u007f") != KC_WVW_OK) {
                free(buf.data);
                return NULL;
            }
        } else if (c < 0x20) {
            char hex[8];
            snprintf(hex, sizeof(hex), "\\u%04x", c);
            if (kc_wvw_text_buf_append(&buf, hex) != KC_WVW_OK) {
                free(buf.data);
                return NULL;
            }
        } else if (c == 0xE2 && (unsigned char)json[i + 1] == 0x80 &&
                ((unsigned char)json[i + 2] == 0xA8 || (unsigned char)json[i + 2] == 0xA9)) {
            if (kc_wvw_text_buf_append(&buf, (unsigned char)json[i + 2] == 0xA8 ? "\\u2028" : "\\u2029") != KC_WVW_OK) {
                free(buf.data);
                return NULL;
            }
            i += 2;
        } else {
            if (kc_wvw_text_buf_append_n(&buf, &json[i], 1) != KC_WVW_OK) {
                free(buf.data);
                return NULL;
            }
        }
    }

    return buf.data;
}










/**
 * Return the last context error.
 * @param ctx Window context.
 * @return Borrowed error string, or NULL when no error is available.
 */
const char *kc_wvw_get_error(const kc_wvw_t *ctx) {
    if (!ctx || !ctx->error[0]) {
        return NULL;
    }
    return ctx->error;
}

/**
 * Extract a string parameter from a bridge JSON params object.
 * @param params JSON params string.
 * @param key Parameter key.
 * @return Newly allocated string or NULL.
 */
static char *kc_wvw_bridge_param_string(const char *params, const char *key) {
    JSON_Value *root;
    JSON_Object *obj;
    const char *val;
    char *copy;

    if (!params || !key) return NULL;

    root = json_parse_string(params);
    if (!root || json_value_get_type(root) != JSONObject) {
        json_value_free(root);
        return NULL;
    }
    obj = json_value_get_object(root);
    val = json_object_get_string(obj, key);
    copy = val ? kc_wvw_strdup(val) : NULL;
    json_value_free(root);
    return copy;
}

/**
 * Extract an integer parameter from a bridge JSON params object.
 * @param params JSON params string.
 * @param key Parameter key.
 * @param out Destination for the parsed integer.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_bridge_param_int(const char *params, const char *key, int *out) {
    JSON_Value *root;
    JSON_Object *obj;
    JSON_Value *v;
    double d;

    if (!params || !key || !out) return KC_WVW_ERROR;

    root = json_parse_string(params);
    if (!root || json_value_get_type(root) != JSONObject) {
        json_value_free(root);
        return KC_WVW_ERROR;
    }
    obj = json_value_get_object(root);
    v = json_object_get_value(obj, key);
    if (!v || json_value_get_type(v) != JSONNumber) {
        json_value_free(root);
        return KC_WVW_ERROR;
    }
    d = json_value_get_number(v);
    if (d < (double)INT32_MIN || d > (double)INT32_MAX || d != (double)(int64_t)d) {
        json_value_free(root);
        return KC_WVW_ERROR;
    }
    *out = (int)d;
    json_value_free(root);
    return KC_WVW_OK;
}

/**
 * Return whether a string holds one complete valid JSON value.
 * @param json Candidate result JSON.
 * @return Non-zero when json parses to one JSON value with no trailing data.
 */
static size_t kc_wvw_json_first_value_end(const char *s, size_t i) {
    switch (s[i]) {
        case '{': {
            i++;
            if (s[i] == '}') return i + 1;
            for (;;) {
                while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') i++;
                if (s[i] != '"') return (size_t)-1;
                i++;
                for (;;) {
                    if (s[i] == '\0' || s[i] == '\n' || s[i] == '\r') return (size_t)-1;
                    if (s[i] == '\\') {
                        if (s[i + 1] == '\0') return (size_t)-1;
                        i += 2;
                        continue;
                    }
                    if (s[i] == '"') { i++; break; }
                    i++;
                }
                while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') i++;
                if (s[i] == ':') i++; else return (size_t)-1;
                while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') i++;
                i = kc_wvw_json_first_value_end(s, i);
                if (i == (size_t)-1) return (size_t)-1;
                while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') i++;
                if (s[i] == ',') { i++; continue; }
                if (s[i] == '}') return i + 1;
                return (size_t)-1;
            }
        }
        case '[': {
            i++;
            if (s[i] == ']') return i + 1;
            for (;;) {
                while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') i++;
                i = kc_wvw_json_first_value_end(s, i);
                if (i == (size_t)-1) return (size_t)-1;
                while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') i++;
                if (s[i] == ',') { i++; continue; }
                if (s[i] == ']') return i + 1;
                return (size_t)-1;
            }
        }
        case '"': {
            i++;
            for (;;) {
                if (s[i] == '\0' || s[i] == '\n' || s[i] == '\r') return (size_t)-1;
                if (s[i] == '\\') {
                    if (s[i + 1] == '\0') return (size_t)-1;
                    i += 2;
                    continue;
                }
                if (s[i] == '"') return i + 1;
                i++;
            }
        }
        case 't':
            if (strncmp(s + i, "true", 4) == 0) return i + 4;
            break;
        case 'f':
            if (strncmp(s + i, "false", 5) == 0) return i + 5;
            break;
        case 'n':
            if (strncmp(s + i, "null", 4) == 0) return i + 4;
            break;
        default: {
            size_t j = i;
            if (s[j] == '-') j++;
            if (s[j] >= '0' && s[j] <= '9') {
                while (s[j] >= '0' && s[j] <= '9') j++;
                if (s[j] == '.') {
                    j++;
                    while (s[j] >= '0' && s[j] <= '9') j++;
                }
                if (s[j] == 'e' || s[j] == 'E') {
                    j++;
                    if (s[j] == '+' || s[j] == '-') j++;
                    while (s[j] >= '0' && s[j] <= '9') j++;
                }
                return j;
            }
            break;
        }
    }
    return (size_t)-1;
}

/**
 * Validate that one JSON value occupies the complete input.
 * Leading whitespace is allowed, but trailing non-whitespace data is not.
 * @param json Candidate JSON text.
 * @return Non-zero when exactly one complete JSON value is present.
 */
static int kc_wvw_json_valid_value(const char *json) {
    JSON_Value *v;
    size_t i = 0;
    size_t end;

    if (!json) {
        return 0;
    }
    v = json_parse_string(json);
    if (!v) {
        return 0;
    }
    json_value_free(v);
    while (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r') i++;
    if (json[i] == '\0') {
        return 0;
    }
    end = kc_wvw_json_first_value_end(json, i);
    if (end == (size_t)-1) {
        return 0;
    }
    while (json[end] == ' ' || json[end] == '\t' || json[end] == '\n' || json[end] == '\r') end++;
    return json[end] == '\0';
}

/**
 * Dispatch one bridge request into the application callback or
 * built-in handler.
 * @param ctx Window context.
 * @param json Request payload.
 * @return Newly allocated response payload or NULL.
 */
static char *kc_wvw_bridge_dispatch_request(kc_wvw_t *ctx, const char *json) {
    char *id;
    char *method;
    char *params;
    char *result = NULL;
    const char *callback_result = NULL;
    char *error;
    char *response;
    int rc;

    if (!ctx || !json || strlen(json) > KC_WVW_BRIDGE_MAX_MESSAGE) {
        error = kc_wvw_bridge_error_object("INVALID_REQUEST", "Bridge request is invalid.");
        response = error ? kc_wvw_bridge_wrap_response("0", 0, error) : NULL;
        free(error);
        return response;
    }

    id = kc_wvw_bridge_get_string(json, "id");
    method = kc_wvw_bridge_get_string(json, "method");
    params = kc_wvw_bridge_get_params(json);
    if (!id || !method || !params) {
        free(id);
        free(method);
        free(params);
        error = kc_wvw_bridge_error_object("INVALID_REQUEST", "Bridge request is malformed.");
        response = error ? kc_wvw_bridge_wrap_response("0", 0, error) : NULL;
        free(error);
        return response;
    }

    if (strcmp(method, "minimize") == 0) {
        rc = kc_wvw_minimize(ctx);
        if (rc == KC_WVW_OK) {
            result = kc_wvw_strdup("{\"ok\":true}");
            response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
        } else {
            result = NULL;
            error = kc_wvw_bridge_error_object("WINDOW_CLOSED", "Window is not available.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
        }
        free(id); free(method); free(params); free(result);
        return response;
    }
    if (strcmp(method, "maximize") == 0) {
        rc = kc_wvw_maximize(ctx);
        if (rc == KC_WVW_OK) {
            result = kc_wvw_strdup("{\"ok\":true}");
            response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
        } else {
            result = NULL;
            error = kc_wvw_bridge_error_object("WINDOW_CLOSED", "Window is not available.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
        }
        free(id); free(method); free(params); free(result);
        return response;
    }
    if (strcmp(method, "restore") == 0) {
        rc = kc_wvw_restore(ctx);
        if (rc == KC_WVW_OK) {
            result = kc_wvw_strdup("{\"ok\":true}");
            response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
        } else {
            result = NULL;
            error = kc_wvw_bridge_error_object("WINDOW_CLOSED", "Window is not available.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
        }
        free(id); free(method); free(params); free(result);
        return response;
    }
    if (strcmp(method, "close") == 0) {
        kc_wvw_request_close(ctx);
        result = kc_wvw_strdup("{\"ok\":true}");
        response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
        free(id); free(method); free(params); free(result);
        return response;
    }
    if (strcmp(method, "setTitle") == 0) {
        char *title = kc_wvw_bridge_param_string(params, "title");
        if (!title) {
            error = kc_wvw_bridge_error_object("INVALID_ARGUMENT", "title must be a string.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
            free(id); free(method); free(params);
            return response;
        }
        if (strlen(title) > KC_WVW_TITLE_MAX) {
            free(title);
            error = kc_wvw_bridge_error_object("INVALID_ARGUMENT", "title exceeds maximum length.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
            free(id); free(method); free(params);
            return response;
        }
        rc = kc_wvw_set_title(ctx, title);
        free(title);
        if (rc == KC_WVW_OK) {
            result = kc_wvw_strdup("{\"ok\":true}");
            response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
        } else {
            result = NULL;
            error = kc_wvw_bridge_error_object("WINDOW_CLOSED", "Window is not available.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
        }
        free(id); free(method); free(params); free(result);
        return response;
    }
    if (strcmp(method, "setSize") == 0) {
        int width = 0, height = 0;
        if (kc_wvw_bridge_param_int(params, "width", &width) != KC_WVW_OK ||
            kc_wvw_bridge_param_int(params, "height", &height) != KC_WVW_OK) {
            error = kc_wvw_bridge_error_object("INVALID_ARGUMENT", "width and height must be integers.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
            free(id); free(method); free(params);
            return response;
        }
        if (width <= 0 || height <= 0 || width > KC_WVW_SIZE_MAX || height > KC_WVW_SIZE_MAX) {
            error = kc_wvw_bridge_error_object("INVALID_ARGUMENT", "width and height must be positive integers within bounds.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
            free(id); free(method); free(params);
            return response;
        }
        rc = kc_wvw_set_size(ctx, width, height);
        if (rc == KC_WVW_OK) {
            result = kc_wvw_strdup("{\"ok\":true}");
            response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
        } else {
            result = NULL;
            error = kc_wvw_bridge_error_object("WINDOW_CLOSED", "Window is not available.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
        }
        free(id); free(method); free(params); free(result);
        return response;
    }
    if (strcmp(method, "getState") == 0) {
        kc_wvw_window_state_t st;
        JSON_Value *rv;
        JSON_Object *ro;
        JSON_Value *sv;
        JSON_Object *so;
        rc = kc_wvw_get_state(ctx, &st);
        if (rc == KC_WVW_OK) {
            rv = json_value_init_object();
            ro = json_value_get_object(rv);
            json_object_set_boolean(ro, "ok", 1);
            sv = json_value_init_object();
            so = json_value_get_object(sv);
            json_object_set_number(so, "width", st.width);
            json_object_set_number(so, "height", st.height);
            json_object_set_boolean(so, "minimized", st.minimized);
            json_object_set_boolean(so, "maximized", st.maximized);
            json_object_set_boolean(so, "fullscreen", st.fullscreen);
            json_object_set_boolean(so, "visible", st.visible);
            json_object_set_value(ro, "state", sv);
            result = json_serialize_to_string(rv);
            json_value_free(rv);
            response = result ? kc_wvw_bridge_wrap_response(id, 1, result) : NULL;
        } else {
            result = NULL;
            error = kc_wvw_bridge_error_object("WINDOW_CLOSED", "Window is not available.");
            response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
            free(error);
        }
        free(id); free(method); free(params); free(result);
        return response;
    }

    if (!kc_wvw_bridge_method_allowed(&ctx->bridge, method)) {
        error = kc_wvw_bridge_error_object("METHOD_NOT_FOUND", "Bridge method is not allowed.");
        response = error ? kc_wvw_bridge_wrap_response(id, 0, error) : NULL;
        free(id);
        free(method);
        free(params);
        free(error);
        return response;
    }

    if(!ctx->bridge.callback){error=kc_wvw_bridge_error_object("METHOD_NOT_FOUND","No bridge callback registered.");response=error?kc_wvw_bridge_wrap_response(id,0,error):NULL;free(id);free(method);free(params);free(error);return response;}
    rc=ctx->bridge.callback(ctx,method,params,&callback_result,ctx->bridge.userdata);
    if(rc==KC_WVW_OK){if(!callback_result)callback_result="null";if(kc_wvw_json_valid_value(callback_result))response=kc_wvw_bridge_wrap_response(id,1,callback_result);else{error=kc_wvw_bridge_error_object("INVALID_RESPONSE","Bridge callback returned invalid JSON.");response=error?kc_wvw_bridge_wrap_response(id,0,error):NULL;free(error);}}
    else{if(callback_result&&kc_wvw_json_valid_value(callback_result))response=kc_wvw_bridge_wrap_response(id,0,callback_result);else{error=kc_wvw_bridge_error_object("OPERATION_FAILED","Bridge callback failed.");response=error?kc_wvw_bridge_wrap_response(id,0,error):NULL;free(error);}}
    free(id);free(method);free(params);return response;
}
#if defined(__APPLE__)
static void kc_wvw_request_close(kc_wvw_t *ctx){if(ctx&&ctx->ns_window){@autoreleasepool{[(__bridge NSWindow *)ctx->ns_window close];}}}
static int kc_wvw_macos_create_window(kc_wvw_t *ctx);

/**
 * Deliver one JSON payload into the WebView bridge runtime.
 * @param ctx Window context.
 * @param json JSON payload.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_bridge_post_json(kc_wvw_t *ctx, const char *json) {
    char *escaped;
    char *script;
    size_t cap;

    if (!ctx || !ctx->ns_webview || !json || strlen(json) > KC_WVW_BRIDGE_MAX_MESSAGE) {
        return KC_WVW_ERROR;
    }

    escaped = kc_wvw_bridge_escape_js_string(json);
    if (!escaped) {
        return KC_WVW_ERROR;
    }

    cap = strlen(escaped) + 64;
    script = (char *)malloc(cap);
    if (!script) {
        free(escaped);
        return KC_WVW_ERROR;
    }

    snprintf(script, cap, "window.__kcWvwReceive(JSON.parse('%s'));", escaped);

    @autoreleasepool {
        WKWebView *webView = (__bridge WKWebView *)ctx->ns_webview;
        NSString *nsScript = [NSString stringWithUTF8String:script];
        [webView evaluateJavaScript:nsScript completionHandler:nil];
    }

    free(escaped);
    free(script);
    return KC_WVW_OK;
}

/**
 * Parses one hexadecimal WebView background color.
 * @param text Color text in RRGGBB or AARRGGBB format.
 * @param out_a Destination alpha byte.
 * @param out_r Destination red byte.
 * @param out_g Destination green byte.
 * @param out_b Destination blue byte.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_parse_background_color(const char *text, CGFloat *out_r, CGFloat *out_g, CGFloat *out_b, CGFloat *out_a) {
    size_t length;
    unsigned int parts[4];

    if (!text || !out_r || !out_g || !out_b || !out_a) {
        return KC_WVW_ERROR;
    }

    length = strlen(text);
    if (length == 6) {
        if (sscanf(text, "%2x%2x%2x", &parts[1], &parts[2], &parts[3]) != 3) {
            return KC_WVW_ERROR;
        }
        parts[0] = 0xff;
    } else if (length == 8) {
        if (sscanf(text, "%2x%2x%2x%2x", &parts[0], &parts[1], &parts[2], &parts[3]) != 4) {
            return KC_WVW_ERROR;
        }
    } else {
        return KC_WVW_ERROR;
    }

    *out_r = (CGFloat)parts[1] / 255.0;
    *out_g = (CGFloat)parts[2] / 255.0;
    *out_b = (CGFloat)parts[3] / 255.0;
    *out_a = (CGFloat)parts[0] / 255.0;
    return KC_WVW_OK;
}

/**
 * Return whether the configured background requests a transparent host surface.
 * @param text Background color in RRGGBB or AARRGGBB format.
 * @return Non-zero when the host should enter transparent mode, otherwise zero.
 */
static int kc_wvw_background_transparent(const char *text) {
    CGFloat r, g, b, a;

    if (!text) {
        return 0;
    }
    if (kc_wvw_parse_background_color(text, &r, &g, &b, &a) != KC_WVW_OK) {
        return 0;
    }

    return a <= 0.0;
}

#pragma mark - ObjC bridge delegates

@interface KCWvwScriptMessageHandler : NSObject <WKScriptMessageHandler>
@property (nonatomic, assign) kc_wvw_t *ctx;
@end

@implementation KCWvwScriptMessageHandler
- (void)userContentController:(WKUserContentController *)userContentController
    didReceiveScriptMessage:(WKScriptMessage *)message {
    (void)userContentController;
    if (!self.ctx) {
        return;
    }

    NSString *body = message.body;
    if (![body isKindOfClass:[NSString class]]) {
        return;
    }

    const char *utf8 = [body UTF8String];
    if (!utf8 || strlen(utf8) > KC_WVW_BRIDGE_MAX_MESSAGE) {
        return;
    }

    char *response = kc_wvw_bridge_dispatch_request(self.ctx, utf8);
    if (response) {
        kc_wvw_bridge_post_json(self.ctx, response);
        free(response);
    }
}
@end

@interface KCWvwNavigationDelegate : NSObject <WKNavigationDelegate>
@property (nonatomic, assign) kc_wvw_t *ctx;
@end

@implementation KCWvwNavigationDelegate
- (void)webView:(WKWebView *)webView
    decidePolicyForNavigationAction:(WKNavigationAction *)navigationAction
    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
    (void)webView;
    if (!self.ctx || !self.ctx->bridge.enabled) {
        decisionHandler(WKNavigationActionPolicyAllow);
        return;
    }

    NSURL *url = navigationAction.request.URL;
    if (url) {
        const char *urlStr = [[url absoluteString] UTF8String];
        if (urlStr && !kc_wvw_bridge_url_trusted(self.ctx, &self.ctx->bridge, urlStr)) {
            decisionHandler(WKNavigationActionPolicyCancel);
            return;
        }
    }

    decisionHandler(WKNavigationActionPolicyAllow);
}
@end

@interface KCWvwWindowDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, assign) kc_wvw_t *ctx;
@end

@implementation KCWvwWindowDelegate
- (void)windowWillClose:(NSNotification *)notification {
    (void)notification;
    if (!self.ctx) {
        return;
    }
    self.ctx->running=0;self.ctx->closed=1;if(self.ctx->cli_waiting)[NSApp stop:nil];
}
@end


static int kc_wvw_dispatch_op(kc_wvw_t *ctx,kc_wvw_op_t *op){if(!ctx||!op||ctx->closed||![NSThread isMainThread])return KC_WVW_ERROR;return kc_wvw_execute_op(ctx,op);}
int kc_wvw_open(kc_wvw_t **out,const kc_wvw_options_t *options){
    kc_wvw_t *ctx;if(out)*out=NULL;if(!out||!options||![NSThread isMainThread])return KC_WVW_ERROR;
    ctx=(kc_wvw_t *)calloc(1,sizeof(*ctx));if(!ctx)return KC_WVW_ERROR;*out=ctx;
    if(kc_wvw_config_copy(&ctx->opts,options)!=KC_WVW_OK){free(ctx);*out=NULL;return KC_WVW_ERROR;}
    if(kc_wvw_macos_create_window(ctx)!=KC_WVW_OK){kc_wvw_set_error(ctx,"window creation failed");return KC_WVW_ERROR;}return KC_WVW_OK;
}


/**
 * Navigate the current WebView to a new URL.
 * @param ctx Window context.
 * @param url Destination URL.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_navigate_impl(kc_wvw_t *ctx, const char *url) {
    if (!ctx || !url) {
        return KC_WVW_ERROR;
    }

    if (ctx->bridge.enabled && !kc_wvw_bridge_url_trusted(ctx, &ctx->bridge, url)) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        WKWebView *webView = (__bridge WKWebView *)ctx->ns_webview;
        if (!webView) {
            return KC_WVW_ERROR;
        }
        NSString *nsUrl = [NSString stringWithUTF8String:url];
        NSURLRequest *request = [NSURLRequest requestWithURL:[NSURL URLWithString:nsUrl]];
        [webView loadRequest:request];
    }
    return KC_WVW_OK;
}

/**
 * Add trusted JavaScript for document-start execution in one WebView.
 * @param ctx Window context.
 * @param javascript Source text to install.
 * @return KC_WVW_OK on installation or KC_WVW_ERROR on failure.
 */
static int kc_wvw_add_init_script_impl(kc_wvw_t *ctx, const char *javascript) {
    if (!ctx || !javascript) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        WKWebView *webView = (__bridge WKWebView *)ctx->ns_webview;
        WKUserContentController *manager;
        WKUserScript *script;
        if (!webView) {
            return KC_WVW_ERROR;
        }
        manager = webView.configuration.userContentController;
        script = [[WKUserScript alloc] initWithSource:[NSString stringWithUTF8String:javascript]
            injectionTime:WKUserScriptInjectionTimeAtDocumentStart
            forMainFrameOnly:YES];
        if (!manager || !script) {
            return KC_WVW_ERROR;
        }
        [manager addUserScript:script];
        [script release];
    }
    return KC_WVW_OK;
}




/**
 * Install the Objective-C bridge handlers on the macOS WebView.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_macos_install_bridge(kc_wvw_t *ctx) {
    KCWvwScriptMessageHandler *handler;
    KCWvwNavigationDelegate *navDelegate;
    char *script;
    NSString *nsScript;

    if (!ctx || !ctx->ns_webview || !ctx->bridge.enabled) {
        return KC_WVW_OK;
    }

    script = kc_wvw_bridge_bootstrap_script(&ctx->bridge);
    if (!script) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        WKWebView *webView = (__bridge WKWebView *)ctx->ns_webview;
        WKUserContentController *manager = [webView configuration].userContentController;

        if (ctx->ns_script_handler || ctx->ns_nav_delegate) {
            [manager removeScriptMessageHandlerForName:@"kc_wvw_native"];
            [manager removeAllUserScripts];
            [webView setNavigationDelegate:nil];
            if (ctx->ns_script_handler) {
                KCWvwScriptMessageHandler *oldHandler =
                    (__bridge KCWvwScriptMessageHandler *)ctx->ns_script_handler;
                oldHandler.ctx = NULL;
            }
            if (ctx->ns_nav_delegate) {
                KCWvwNavigationDelegate *oldNav =
                    (__bridge KCWvwNavigationDelegate *)ctx->ns_nav_delegate;
                oldNav.ctx = NULL;
            }
            if (ctx->ns_script_handler) {
                CFRelease(ctx->ns_script_handler);
                ctx->ns_script_handler = NULL;
            }
            if (ctx->ns_nav_delegate) {
                CFRelease(ctx->ns_nav_delegate);
                ctx->ns_nav_delegate = NULL;
            }
        }

        handler = [[KCWvwScriptMessageHandler alloc] init];
        handler.ctx = ctx;
        [manager addScriptMessageHandler:handler name:@"kc_wvw_native"];

        nsScript = [NSString stringWithUTF8String:script];
        WKUserScript *userScript = [[WKUserScript alloc]
            initWithSource:nsScript
            injectionTime:WKUserScriptInjectionTimeAtDocumentStart
            forMainFrameOnly:YES];
        [manager addUserScript:userScript];

        navDelegate = [[KCWvwNavigationDelegate alloc] init];
        navDelegate.ctx = ctx;
        [webView setNavigationDelegate:navDelegate];
        [webView reload];

        ctx->ns_script_handler = (void *)CFBridgingRetain(handler);
        ctx->ns_nav_delegate = (void *)CFBridgingRetain(navDelegate);

        if (ctx->opts.borderless || ctx->opts.fullscreen) {
            [webView setValue:@(NO) forKey:@"drawsBackground"];
        }
    }

    free(script);
    return KC_WVW_OK;
}

/**
 * Enable one native bridge with a fixed method whitelist.
 * @param ctx Window context.
 * @param opts Bridge configuration options.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_enable_bridge_impl(kc_wvw_t *ctx, const kc_wvw_bridge_options_t *opts) {
    kc_wvw_bridge_state_t bridge;

    if (!ctx || !opts) {
        return KC_WVW_ERROR;
    }

    memset(&bridge, 0, sizeof(bridge));
    if (kc_wvw_bridge_state_copy(&bridge, opts) != KC_WVW_OK) {
        return KC_WVW_ERROR;
    }
    bridge.enabled = 1;
    if (!kc_wvw_bridge_url_trusted(ctx, &bridge, ctx->opts.url)) {
        kc_wvw_bridge_state_free(&bridge);
        return KC_WVW_ERROR;
    }

    kc_wvw_bridge_state_free(&ctx->bridge);
    ctx->bridge = bridge;
    return kc_wvw_macos_install_bridge(ctx);
}

/**
 * Deliver one native bridge event into the current WebView.
 * @param ctx Window context.
 * @param json JSON payload to dispatch as the event detail.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_post_bridge_event_impl(kc_wvw_t *ctx, const char *json) {
    return kc_wvw_bridge_post_json(ctx, json);
}

/**
 * Create the macOS window, content WebView, and window delegate.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_macos_create_window(kc_wvw_t *ctx) {
    @autoreleasepool {
        WKWebViewConfiguration *config = [[WKWebViewConfiguration alloc] init];
        WKWebView *webView = [[WKWebView alloc] initWithFrame:NSZeroRect configuration:config];

        CGFloat r = 1.0, g = 1.0, b = 1.0, a = 1.0;
        int transparent = 0;

        if (ctx->opts.background) {
            if (kc_wvw_parse_background_color(ctx->opts.background, &r, &g, &b, &a) != KC_WVW_OK) {
                return KC_WVW_ERROR;
            }
            transparent = kc_wvw_background_transparent(ctx->opts.background);
        }

        if (transparent) {
            [webView setValue:@(NO) forKey:@"drawsBackground"];
        } else {
            NSColor *bgColor = [NSColor colorWithCalibratedRed:r green:g blue:b alpha:a];
            [webView setValue:@(YES) forKey:@"drawsBackground"];
            [webView setValue:bgColor forKey:@"backgroundColor"];
        }

        NSString *urlStr = ctx->opts.url ? [NSString stringWithUTF8String:ctx->opts.url] : @"about:blank";
        NSURLRequest *request = [NSURLRequest requestWithURL:[NSURL URLWithString:urlStr]];
        [webView loadRequest:request];

        CGFloat originX = 0, originY = 0;
        if (ctx->opts.has_posx || ctx->opts.has_posy) {
            NSScreen *mainScreen = [NSScreen mainScreen];
            CGFloat screenWidth = mainScreen.visibleFrame.size.width;
            CGFloat screenHeight = mainScreen.visibleFrame.size.height;
            CGFloat width = (CGFloat)(ctx->opts.width > 0 ? ctx->opts.width : 800);
            CGFloat height = (CGFloat)(ctx->opts.height > 0 ? ctx->opts.height : 600);
            originX = ctx->opts.posx > 0 ? fmin((CGFloat)ctx->opts.posx, screenWidth - width) : 0;
            originY = ctx->opts.posy > 0 ? fmin((CGFloat)ctx->opts.posy, screenHeight - height) : 0;
        }
        NSRect frame = NSMakeRect(originX, originY,
            ctx->opts.width > 0 ? ctx->opts.width : 800,
            ctx->opts.height > 0 ? ctx->opts.height : 600);

        NSString *title = ctx->opts.title ? [NSString stringWithUTF8String:ctx->opts.title] : @"wvw";
        NSWindowStyleMask styleMask = NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;

        if (!ctx->opts.borderless && !ctx->opts.fullscreen) {
            styleMask |= NSWindowStyleMaskTitled;
        }

        NSWindow *window = [[NSWindow alloc]
            initWithContentRect:frame
            styleMask:styleMask
            backing:NSBackingStoreBuffered
            defer:NO];
        [window setTitle:title];

        if (transparent) {
            [window setBackgroundColor:[NSColor clearColor]];
            [window setOpaque:NO];
            [window setHasShadow:NO];
        }

        [window setContentView:webView];

        if (ctx->opts.fullscreen) {
            [window toggleFullScreen:nil];
        }
        if (ctx->opts.borderless) {
            [window setStyleMask:NSWindowStyleMaskBorderless];
        }
        if (ctx->opts.always_on_top) {
            [window setLevel:NSFloatingWindowLevel];
        }
        if (ctx->opts.click_through) {
            [window setIgnoresMouseEvents:YES];
        }

        KCWvwWindowDelegate *windowDelegate = [[KCWvwWindowDelegate alloc] init];
        windowDelegate.ctx = ctx;
        [window setDelegate:windowDelegate];
        ctx->ns_window_delegate = (void *)CFBridgingRetain(windowDelegate);

        if (ctx->opts.no_focus) {
            [window orderFront:nil];
        } else {
            [window makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
        }

        ctx->ns_window = (void *)CFBridgingRetain(window);
        ctx->ns_webview = (void *)CFBridgingRetain(webView);
        return KC_WVW_OK;
    }
}

/**
 * Show the window.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_show_impl(kc_wvw_t *ctx) {
    if (!ctx || !ctx->ns_window) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        NSWindow *window = (__bridge NSWindow *)ctx->ns_window;
        [window orderFront:nil];
    }
    return KC_WVW_OK;
}

/**
 * Hide the window.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_hide_impl(kc_wvw_t *ctx) {
    if (!ctx || !ctx->ns_window) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        NSWindow *window = (__bridge NSWindow *)ctx->ns_window;
        [window orderOut:nil];
    }
    return KC_WVW_OK;
}

/**
 * Minimize (iconify) the window.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_minimize_impl(kc_wvw_t *ctx) {
    if (!ctx || !ctx->ns_window) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        NSWindow *window = (__bridge NSWindow *)ctx->ns_window;
        [window miniaturize:nil];
    }
    return KC_WVW_OK;
}

/**
 * Maximize the window.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_maximize_impl(kc_wvw_t *ctx) {
    if (!ctx || !ctx->ns_window) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        NSWindow *window = (__bridge NSWindow *)ctx->ns_window;
        [window zoom:nil];
    }
    return KC_WVW_OK;
}

/**
 * Restore the window from minimized or maximized state.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_restore_impl(kc_wvw_t *ctx) {
    if (!ctx || !ctx->ns_window) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        NSWindow *window = (__bridge NSWindow *)ctx->ns_window;
        if ([window isMiniaturized]) {
            [window deminiaturize:nil];
        }
        if ([window isZoomed]) {
            [window zoom:nil];
        }
    }
    return KC_WVW_OK;
}

/**
 * Set the native window title.
 * @param ctx Window context.
 * @param title UTF-8 title string.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_set_title_impl(kc_wvw_t *ctx, const char *title) {
    if (!ctx || !ctx->ns_window || !title) {
        return KC_WVW_ERROR;
    }
    if (strlen(title) > KC_WVW_TITLE_MAX) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        NSWindow *window = (__bridge NSWindow *)ctx->ns_window;
        NSString *nsTitle = [NSString stringWithUTF8String:title];
        [window setTitle:nsTitle];
    }{char *copy=kc_wvw_strdup(title);if(!copy)return KC_WVW_ERROR;free(ctx->opts.title);ctx->opts.title=copy;}return KC_WVW_OK;
}

/**
 * Resize the native window content area.
 * @param ctx Window context.
 * @param width Width in pixels.
 * @param height Height in pixels.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_set_size_impl(kc_wvw_t *ctx, int width, int height) {
    if (!ctx || !ctx->ns_window || width <= 0 || height <= 0) {
        return KC_WVW_ERROR;
    }
    if (width > KC_WVW_SIZE_MAX || height > KC_WVW_SIZE_MAX) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        NSWindow *window = (__bridge NSWindow *)ctx->ns_window;
        NSRect frame = [window frame];
        NSSize contentSize = NSMakeSize((CGFloat)width, (CGFloat)height);
        [window setContentSize:contentSize];
        (void)frame;
    }
    return KC_WVW_OK;
}

/**
 * Query the current window state.
 * @param ctx Window context.
 * @param state Destination state structure.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_get_state_impl(kc_wvw_t *ctx, kc_wvw_window_state_t *state) {
    if (!ctx || !ctx->ns_window || !state) {
        return KC_WVW_ERROR;
    }

    @autoreleasepool {
        NSWindow *window = (__bridge NSWindow *)ctx->ns_window;
        NSRect contentRect = [[window contentView] frame];
        memset(state, 0, sizeof(*state));
        state->width = (int)contentRect.size.width;
        state->height = (int)contentRect.size.height;
        state->minimized = [window isMiniaturized] ? 1 : 0;
        state->maximized = [window isZoomed] ? 1 : 0;
        state->fullscreen = ([window styleMask] & NSWindowStyleMaskFullScreen) ? 1 : 0;
        state->visible = [window isVisible] ? 1 : 0;
    }
    return KC_WVW_OK;
}





int kc_wvw_cli_wait(kc_wvw_t *ctx){if(!ctx||![NSThread isMainThread])return KC_WVW_ERROR;if(ctx->closed)return KC_WVW_OK;ctx->cli_waiting=1;@autoreleasepool{[NSApp run];}ctx->cli_waiting=0;return KC_WVW_OK;}
void kc_wvw_close(kc_wvw_t *ctx){if(!ctx)return;if(ctx->ns_webview){@autoreleasepool{WKWebView *webView=(__bridge WKWebView *)ctx->ns_webview;[[webView configuration].userContentController removeScriptMessageHandlerForName:@"kc_wvw_native"];[webView setNavigationDelegate:nil];}}if(ctx->ns_window){@autoreleasepool{NSWindow *window=(__bridge NSWindow *)ctx->ns_window;[window setDelegate:nil];[window close];}CFRelease(ctx->ns_window);ctx->ns_window=NULL;}if(ctx->ns_script_handler){CFRelease(ctx->ns_script_handler);ctx->ns_script_handler=NULL;}if(ctx->ns_nav_delegate){CFRelease(ctx->ns_nav_delegate);ctx->ns_nav_delegate=NULL;}if(ctx->ns_window_delegate){CFRelease(ctx->ns_window_delegate);ctx->ns_window_delegate=NULL;}if(ctx->ns_webview){CFRelease(ctx->ns_webview);ctx->ns_webview=NULL;}kc_wvw_bridge_state_free(&ctx->bridge);kc_wvw_config_free(&ctx->opts);free(ctx);}


#else
/**
 * Deliver one JSON payload into the WebView bridge runtime.
 * @param ctx Window context.
 * @param json JSON payload.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_bridge_post_json(kc_wvw_t *ctx, const char *json) {
    char *escaped;
    char *script;
    size_t cap;

    if (!ctx || !ctx->web_view || !json || strlen(json) > KC_WVW_BRIDGE_MAX_MESSAGE) {
        return KC_WVW_ERROR;
    }

    escaped = kc_wvw_bridge_escape_js_string(json);
    if (!escaped) {
        return KC_WVW_ERROR;
    }

    cap = strlen(escaped) + 64;
    script = (char *)malloc(cap);
    if (!script) {
        free(escaped);
        return KC_WVW_ERROR;
    }

    snprintf(script, cap, "window.__kcWvwReceive(JSON.parse('%s'));", escaped);
    webkit_web_view_evaluate_javascript(ctx->web_view, script, -1, NULL, NULL, NULL, NULL, NULL);
    free(escaped);
    free(script);
    return KC_WVW_OK;
}

/**
 * Prepare one GTK host window for transparent compositing.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_linux_prepare_transparent_window(kc_wvw_t *ctx) {
    GdkScreen *screen;
    GdkVisual *visual;

    if (!ctx || !ctx->window) {
        return KC_WVW_ERROR;
    }

    screen = gtk_widget_get_screen(ctx->window);
    if (!screen) {
        return KC_WVW_ERROR;
    }

    visual = gdk_screen_get_rgba_visual(screen);
    if (!visual) {
        return KC_WVW_OK;
    }

    gtk_widget_set_visual(ctx->window, visual);
    gtk_widget_set_app_paintable(ctx->window, TRUE);
    return KC_WVW_OK;
}

/**
 * Apply topmost and click-through host modes on Linux.
 * @param ctx Window context.
 * @return None.
 */
static void kc_wvw_linux_apply_window_modes(kc_wvw_t *ctx) {
    cairo_region_t *region;
    GdkWindow *window;
    GdkWindow *web_window;

    if (!ctx || !ctx->window) {
        return;
    }

    if (ctx->opts.always_on_top) {
        gtk_window_set_keep_above(GTK_WINDOW(ctx->window), TRUE);
    }
    if (ctx->opts.no_focus) {
        gtk_window_set_accept_focus(GTK_WINDOW(ctx->window), FALSE);
        gtk_window_set_focus_on_map(GTK_WINDOW(ctx->window), FALSE);
    }
    if (ctx->opts.click_through) {
        window = gtk_widget_get_window(ctx->window);
        web_window = ctx->web_view ? gtk_widget_get_window(GTK_WIDGET(ctx->web_view)) : NULL;
        if (window) {
            gdk_window_set_pass_through(window, TRUE);
            region = cairo_region_create();
            if (region) {
                gdk_window_input_shape_combine_region(window, region, 0, 0);
                cairo_region_destroy(region);
            }
        }
        if (web_window) {
            gdk_window_set_pass_through(web_window, TRUE);
        }
    }
}

/**
 * Parses one hexadecimal WebView background color.
 * @param text Color text in RRGGBB or AARRGGBB format.
 * @param out_rgba Destination GTK RGBA value.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_parse_background_color(const char *text, GdkRGBA *out_rgba) {
    size_t length;
    unsigned int parts[4];

    if (!text || !out_rgba) {
        return KC_WVW_ERROR;
    }

    length = strlen(text);
    if (length == 6) {
        if (sscanf(text, "%2x%2x%2x", &parts[1], &parts[2], &parts[3]) != 3) {
            return KC_WVW_ERROR;
        }
        parts[0] = 0xff;
    } else if (length == 8) {
        if (sscanf(text, "%2x%2x%2x%2x", &parts[0], &parts[1], &parts[2], &parts[3]) != 4) {
            return KC_WVW_ERROR;
        }
    } else {
        return KC_WVW_ERROR;
    }

    out_rgba->red = (gdouble)parts[1] / 255.0;
    out_rgba->green = (gdouble)parts[2] / 255.0;
    out_rgba->blue = (gdouble)parts[3] / 255.0;
    out_rgba->alpha = (gdouble)parts[0] / 255.0;
    return KC_WVW_OK;
}

/**
 * Return whether the configured background requests a transparent host surface.
 * @param text Background color in RRGGBB or AARRGGBB format.
 * @return Non-zero when the host should enter transparent mode, otherwise zero.
 */
static int kc_wvw_background_transparent(const char *text) {
    GdkRGBA rgba;

    if (!text) {
        return 0;
    }
    if (kc_wvw_parse_background_color(text, &rgba) != KC_WVW_OK) {
        return 0;
    }

    return rgba.alpha <= 0.0;
}

/**
 * Close the GTK main loop when the native window is destroyed.
 * @param widget GTK widget.
 * @param userdata Window context.
 * @return None.
 */
static void kc_wvw_linux_destroy(GtkWidget *widget, gpointer userdata) {
    kc_wvw_t *ctx = (kc_wvw_t *)userdata;
    int was_running = 0;

    (void)widget;

    if (ctx) {
        was_running = ctx->running;
        ctx->running = 0;
        ctx->window = NULL;
        ctx->web_view = NULL;
    }
    if (was_running && gtk_main_level() > 0) {
        gtk_main_quit();
    }
}

/**
 * Handle one message received from the injected NativeBridge runtime.
 * @param manager User content manager.
 * @param js_result JavaScript result.
 * @param user_data Window context.
 * @return None.
 */
static void kc_wvw_linux_bridge_message(WebKitUserContentManager *manager, WebKitJavascriptResult *js_result, gpointer user_data) {
    JSCValue *value;
    char *request;
    char *response;
    kc_wvw_t *ctx;

    (void)manager;
    ctx = (kc_wvw_t *)user_data;
    if (!ctx) {
        return;
    }

    value = webkit_javascript_result_get_js_value(js_result);
    request = value ? jsc_value_to_string(value) : NULL;
    if (!request) {
        return;
    }

    response = kc_wvw_bridge_dispatch_request(ctx, request);
    if (response) {
        kc_wvw_bridge_post_json(ctx, response);
        free(response);
    }
    g_free(request);
}

/**
 * Decide whether one navigation request stays inside the trusted WebView.
 * @param web_view Web view.
 * @param decision Policy decision.
 * @param type Policy decision type.
 * @param user_data Window context.
 * @return TRUE when the navigation is handled, otherwise FALSE.
 */
static gboolean kc_wvw_linux_bridge_policy(WebKitWebView *web_view, WebKitPolicyDecision *decision, WebKitPolicyDecisionType type, gpointer user_data) {
    WebKitNavigationPolicyDecision *nav;
    WebKitNavigationAction *action;
    WebKitURIRequest *request;
    const gchar *uri;
    kc_wvw_t *ctx;

    (void)web_view;
    ctx = (kc_wvw_t *)user_data;
    if (!ctx || !ctx->bridge.enabled || type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
        return FALSE;
    }

    nav = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
    action = webkit_navigation_policy_decision_get_navigation_action(nav);
    request = webkit_navigation_action_get_request(action);
    uri = request ? webkit_uri_request_get_uri(request) : NULL;
    if (uri && !kc_wvw_bridge_url_trusted(ctx, &ctx->bridge, uri)) {
        webkit_policy_decision_ignore(decision);
        return TRUE;
    }

    return FALSE;
}

/**
 * Install the configured bridge into the current WebView.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_linux_install_bridge(kc_wvw_t *ctx) {
    WebKitUserContentManager *manager;
    char *script;

    if (!ctx || !ctx->web_view || !ctx->bridge.enabled) {
        return KC_WVW_OK;
    }

    manager = webkit_web_view_get_user_content_manager(ctx->web_view);
    if (!manager) {
        return KC_WVW_ERROR;
    }

    script = kc_wvw_bridge_bootstrap_script(&ctx->bridge);
    if (!script) {
        return KC_WVW_ERROR;
    }

    webkit_user_content_manager_unregister_script_message_handler(manager, "kc_wvw_native");
    webkit_user_content_manager_register_script_message_handler(manager, "kc_wvw_native");
    g_signal_connect(manager, "script-message-received::kc_wvw_native", G_CALLBACK(kc_wvw_linux_bridge_message), ctx);
    webkit_user_content_manager_add_script(manager, webkit_user_script_new(script, WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, NULL, NULL));
    g_signal_connect(ctx->web_view, "decide-policy", G_CALLBACK(kc_wvw_linux_bridge_policy), ctx);
    free(script);
    return KC_WVW_OK;
}

/**
 * Create the native GTK window and WebView.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_linux_create_window(kc_wvw_t *ctx) {
    GdkRGBA background;

    ctx->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    if (!ctx->window) {
        return KC_WVW_ERROR;
    }

    gtk_window_set_default_size(GTK_WINDOW(ctx->window), ctx->opts.width, ctx->opts.height);
    gtk_window_set_title(GTK_WINDOW(ctx->window), ctx->opts.title ? ctx->opts.title : "wvw");
    if (ctx->opts.has_posx || ctx->opts.has_posy) {
        GdkDisplay *display = gdk_display_get_default();
        GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
        GdkRectangle geometry;
        gdk_monitor_get_geometry(monitor, &geometry);
        int x = ctx->opts.posx > 0 ? ctx->opts.posx : 0;
        int y = ctx->opts.posy > 0 ? ctx->opts.posy : 0;
        if (x > geometry.width - ctx->opts.width) x = geometry.width - ctx->opts.width;
        if (y > geometry.height - ctx->opts.height) y = geometry.height - ctx->opts.height;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        gtk_window_move(GTK_WINDOW(ctx->window), x, y);
    }
    if (ctx->opts.borderless || ctx->opts.fullscreen) {
        gtk_window_set_decorated(GTK_WINDOW(ctx->window), FALSE);
    }
    if (ctx->opts.fullscreen) {
        gtk_window_fullscreen(GTK_WINDOW(ctx->window));
    }

    ctx->web_view = WEBKIT_WEB_VIEW(webkit_web_view_new());
    if (!ctx->web_view) {
        return KC_WVW_ERROR;
    }
    if (kc_wvw_background_transparent(ctx->opts.background) && kc_wvw_linux_prepare_transparent_window(ctx) != KC_WVW_OK) {
        return KC_WVW_ERROR;
    }
    if (ctx->opts.background) {
        if (kc_wvw_parse_background_color(ctx->opts.background, &background) != KC_WVW_OK) {
            return KC_WVW_ERROR;
        }
        webkit_web_view_set_background_color(ctx->web_view, &background);
    }

    gtk_container_add(GTK_CONTAINER(ctx->window), GTK_WIDGET(ctx->web_view));
    g_signal_connect(ctx->window, "destroy", G_CALLBACK(kc_wvw_linux_destroy), ctx);
    gtk_widget_show_all(ctx->window);
    kc_wvw_linux_apply_window_modes(ctx);
    return KC_WVW_OK;
}


typedef struct {kc_wvw_t *ctx;kc_wvw_op_t *op;GMutex mutex;GCond cond;int done;} kc_wvw_linux_call_t;
static gsize kc_wvw_gtk_once;static GMainContext *kc_wvw_gtk_context;static GThread *kc_wvw_gtk_thread;static GMutex kc_wvw_gtk_mutex;static GCond kc_wvw_gtk_cond;static int kc_wvw_gtk_ready,kc_wvw_gtk_started;
static gpointer kc_wvw_linux_worker(gpointer data){GMainLoop *loop;(void)data;kc_wvw_gtk_started=gtk_init_check(NULL,NULL);g_mutex_lock(&kc_wvw_gtk_mutex);kc_wvw_gtk_ready=1;g_cond_signal(&kc_wvw_gtk_cond);g_mutex_unlock(&kc_wvw_gtk_mutex);if(!kc_wvw_gtk_started)return NULL;loop=g_main_loop_new(kc_wvw_gtk_context,FALSE);g_main_loop_run(loop);g_main_loop_unref(loop);return NULL;}
static int kc_wvw_linux_service(void){if(g_once_init_enter(&kc_wvw_gtk_once)){g_mutex_init(&kc_wvw_gtk_mutex);g_cond_init(&kc_wvw_gtk_cond);kc_wvw_gtk_context=g_main_context_default();g_mutex_lock(&kc_wvw_gtk_mutex);kc_wvw_gtk_thread=g_thread_new("kc-wvw",kc_wvw_linux_worker,NULL);if(kc_wvw_gtk_thread)while(!kc_wvw_gtk_ready)g_cond_wait(&kc_wvw_gtk_cond,&kc_wvw_gtk_mutex);g_mutex_unlock(&kc_wvw_gtk_mutex);g_once_init_leave(&kc_wvw_gtk_once,1);}return kc_wvw_gtk_started?KC_WVW_OK:KC_WVW_ERROR;}
static gboolean kc_wvw_linux_dispatch_cb(gpointer data){kc_wvw_linux_call_t *call=(kc_wvw_linux_call_t *)data;int rc=kc_wvw_execute_op(call->ctx,call->op);g_mutex_lock(&call->mutex);call->op->result=rc;call->done=1;g_cond_signal(&call->cond);g_mutex_unlock(&call->mutex);return G_SOURCE_REMOVE;}
static int kc_wvw_dispatch_op(kc_wvw_t *ctx,kc_wvw_op_t *op){kc_wvw_linux_call_t call;GSource *source;if(!ctx||!op||ctx->closed||!ctx->context)return KC_WVW_ERROR;if(g_thread_self()==ctx->thread)return kc_wvw_execute_op(ctx,op);memset(&call,0,sizeof(call));call.ctx=ctx;call.op=op;g_mutex_init(&call.mutex);g_cond_init(&call.cond);g_mutex_lock(&call.mutex);source=g_idle_source_new();g_source_set_callback(source,kc_wvw_linux_dispatch_cb,&call,NULL);g_source_attach(source,ctx->context);g_source_unref(source);while(!call.done)g_cond_wait(&call.cond,&call.mutex);g_mutex_unlock(&call.mutex);g_cond_clear(&call.cond);g_mutex_clear(&call.mutex);return op->result;}
typedef struct {kc_wvw_t *ctx;GMutex mutex;GCond cond;int done,result;} kc_wvw_linux_init_t;
static gboolean kc_wvw_linux_open_cb(gpointer data){kc_wvw_linux_init_t *call=(kc_wvw_linux_init_t *)data;int rc=kc_wvw_linux_create_window(call->ctx);if(rc==KC_WVW_OK)rc=kc_wvw_navigate_impl(call->ctx,call->ctx->opts.url);g_mutex_lock(&call->mutex);call->result=rc;call->done=1;g_cond_signal(&call->cond);g_mutex_unlock(&call->mutex);return G_SOURCE_REMOVE;}
static int kc_wvw_linux_open_dispatch(kc_wvw_t *ctx){kc_wvw_linux_init_t call;GSource *source;memset(&call,0,sizeof(call));call.ctx=ctx;g_mutex_init(&call.mutex);g_cond_init(&call.cond);g_mutex_lock(&call.mutex);source=g_idle_source_new();g_source_set_callback(source,kc_wvw_linux_open_cb,&call,NULL);g_source_attach(source,ctx->context);g_source_unref(source);while(!call.done)g_cond_wait(&call.cond,&call.mutex);g_mutex_unlock(&call.mutex);g_cond_clear(&call.cond);g_mutex_clear(&call.mutex);return call.result;}
static gboolean kc_wvw_linux_close_cb(gpointer data){kc_wvw_request_close((kc_wvw_t *)data);return G_SOURCE_REMOVE;}
static void kc_wvw_linux_close_dispatch(kc_wvw_t *ctx){GSource *source;if(!ctx||!ctx->context)return;source=g_idle_source_new();g_source_set_callback(source,kc_wvw_linux_close_cb,ctx,NULL);g_source_attach(source,ctx->context);g_source_unref(source);g_mutex_lock(&ctx->mutex);while(!ctx->closed)g_cond_wait(&ctx->cond,&ctx->mutex);g_mutex_unlock(&ctx->mutex);}
int kc_wvw_open(kc_wvw_t **out,const kc_wvw_options_t *options){kc_wvw_t *ctx;if(out)*out=NULL;if(!out||!options)return KC_WVW_ERROR;ctx=(kc_wvw_t *)calloc(1,sizeof(*ctx));if(!ctx)return KC_WVW_ERROR;if(kc_wvw_config_copy(&ctx->opts,options)!=KC_WVW_OK){free(ctx);return KC_WVW_ERROR;}g_mutex_init(&ctx->mutex);g_cond_init(&ctx->cond);if(kc_wvw_linux_service()!=KC_WVW_OK){kc_wvw_set_error(ctx,"GTK initialization failed");*out=ctx;return KC_WVW_ERROR;}ctx->context=kc_wvw_gtk_context;ctx->thread=kc_wvw_gtk_thread;*out=ctx;if(kc_wvw_linux_open_dispatch(ctx)!=KC_WVW_OK){kc_wvw_set_error(ctx,"window creation failed");return KC_WVW_ERROR;}ctx->running=1;return KC_WVW_OK;}






int kc_wvw_cli_wait(kc_wvw_t *ctx){if(!ctx)return KC_WVW_ERROR;g_mutex_lock(&ctx->mutex);while(!ctx->closed)g_cond_wait(&ctx->cond,&ctx->mutex);g_mutex_unlock(&ctx->mutex);return KC_WVW_OK;}
void kc_wvw_close(kc_wvw_t *ctx){if(!ctx)return;if(!ctx->closed)kc_wvw_linux_close_dispatch(ctx);kc_wvw_bridge_state_free(&ctx->bridge);kc_wvw_config_free(&ctx->opts);g_cond_clear(&ctx->cond);g_mutex_clear(&ctx->mutex);free(ctx);}





/**
 * Navigate the current WebView to a new URL.
 * @param ctx Window context.
 * @param url Destination URL.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_navigate_impl(kc_wvw_t *ctx, const char *url) {
    if (!ctx || !url) {
        return KC_WVW_ERROR;
    }
    if (ctx->bridge.enabled && !kc_wvw_bridge_url_trusted(ctx, &ctx->bridge, url)) {
        return KC_WVW_ERROR;
    }

    webkit_web_view_load_uri(ctx->web_view, url);
    return KC_WVW_OK;
}

/**
 * Add trusted JavaScript for document-start execution in one WebView.
 * @param ctx Window context.
 * @param javascript Source text to install.
 * @return KC_WVW_OK on installation or KC_WVW_ERROR on failure.
 */
static int kc_wvw_add_init_script_impl(kc_wvw_t *ctx, const char *javascript) {
    WebKitUserContentManager *manager;

    if (!ctx || !ctx->web_view || !javascript) {
        return KC_WVW_ERROR;
    }

    manager = webkit_web_view_get_user_content_manager(ctx->web_view);
    if (!manager) {
        return KC_WVW_ERROR;
    }
    webkit_user_content_manager_add_script(manager, webkit_user_script_new(javascript, WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, NULL, NULL));
    return KC_WVW_OK;
}

/**
 * Enable one native bridge with a fixed method whitelist.
 * @param ctx Window context.
 * @param opts Bridge configuration options.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_enable_bridge_impl(kc_wvw_t *ctx, const kc_wvw_bridge_options_t *opts) {
    kc_wvw_bridge_state_t bridge;

    if (!ctx || !opts) {
        return KC_WVW_ERROR;
    }

    memset(&bridge, 0, sizeof(bridge));
    if (kc_wvw_bridge_state_copy(&bridge, opts) != KC_WVW_OK) {
        return KC_WVW_ERROR;
    }
    bridge.enabled = 1;
    if (!kc_wvw_bridge_url_trusted(ctx, &bridge, ctx->opts.url)) {
        kc_wvw_bridge_state_free(&bridge);
        return KC_WVW_ERROR;
    }

    kc_wvw_bridge_state_free(&ctx->bridge);
    ctx->bridge = bridge;
    return kc_wvw_linux_install_bridge(ctx);
}

/**
 * Deliver one native bridge event into the current WebView.
 * @param ctx Window context.
 * @param json JSON payload to dispatch as the event detail.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_post_bridge_event_impl(kc_wvw_t *ctx, const char *json) {
    return kc_wvw_bridge_post_json(ctx, json);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

/**
 * Hide the window.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_hide_impl(kc_wvw_t *ctx) {
    if (!ctx || !ctx->window) {
        return KC_WVW_ERROR;
    }
    gtk_widget_hide(ctx->window);
    return KC_WVW_OK;
}

/**
 * Show the window.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_show_impl(kc_wvw_t *ctx) {
    if (!ctx || !ctx->window) {
        return KC_WVW_ERROR;
    }
    gtk_widget_show_all(ctx->window);
    gtk_window_present(GTK_WINDOW(ctx->window));
    return KC_WVW_OK;
}

/**
 * Minimize (iconify) the window.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_minimize_impl(kc_wvw_t *ctx) {
    if (!ctx || !ctx->window) {
        return KC_WVW_ERROR;
    }
    gtk_window_iconify(GTK_WINDOW(ctx->window));
    return KC_WVW_OK;
}

/**
 * Maximize the window.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_maximize_impl(kc_wvw_t *ctx) {
    if (!ctx || !ctx->window) {
        return KC_WVW_ERROR;
    }
    gtk_window_maximize(GTK_WINDOW(ctx->window));
    return KC_WVW_OK;
}

/**
 * Restore the window from minimized or maximized state.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_restore_impl(kc_wvw_t *ctx) {
    GdkWindow *gdk_window;

    if (!ctx || !ctx->window) {
        return KC_WVW_ERROR;
    }

    gdk_window = gtk_widget_get_window(ctx->window);
    if (gdk_window) {
        GdkWindowState ws = gdk_window_get_state(gdk_window);
        if (ws & GDK_WINDOW_STATE_MAXIMIZED) {
            gtk_window_unmaximize(GTK_WINDOW(ctx->window));
        }
        if (ws & GDK_WINDOW_STATE_ICONIFIED) {
            gtk_window_deiconify(GTK_WINDOW(ctx->window));
        }
    }
    return KC_WVW_OK;
}

/**
 * Set the native window title.
 * @param ctx Window context.
 * @param title UTF-8 title string.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_set_title_impl(kc_wvw_t *ctx, const char *title) {
    if (!ctx || !ctx->window || !title) {
        return KC_WVW_ERROR;
    }
    if (strlen(title) > KC_WVW_TITLE_MAX) {
        return KC_WVW_ERROR;
    }
    gtk_window_set_title(GTK_WINDOW(ctx->window),title);{char *copy=kc_wvw_strdup(title);if(!copy)return KC_WVW_ERROR;free(ctx->opts.title);ctx->opts.title=copy;}return KC_WVW_OK;
}

/**
 * Resize the native window content area.
 * @param ctx Window context.
 * @param width Width in pixels.
 * @param height Height in pixels.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_set_size_impl(kc_wvw_t *ctx, int width, int height) {
    if (!ctx || !ctx->window || width <= 0 || height <= 0) {
        return KC_WVW_ERROR;
    }
    if (width > KC_WVW_SIZE_MAX || height > KC_WVW_SIZE_MAX) {
        return KC_WVW_ERROR;
    }
    gtk_window_resize(GTK_WINDOW(ctx->window), width, height);
    return KC_WVW_OK;
}

/**
 * Query the current window state.
 * @param ctx Window context.
 * @param state Destination state structure.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
static int kc_wvw_get_state_impl(kc_wvw_t *ctx, kc_wvw_window_state_t *state) {
    int width, height;
    GdkWindow *gdk_window;

    if (!ctx || !ctx->window || !state) {
        return KC_WVW_ERROR;
    }

    memset(state, 0, sizeof(*state));
    gtk_window_get_size(GTK_WINDOW(ctx->window), &width, &height);
    state->width = width;
    state->height = height;
    gdk_window = gtk_widget_get_window(ctx->window);
    if (gdk_window) {
        GdkWindowState ws = gdk_window_get_state(gdk_window);
        state->minimized = !!(ws & GDK_WINDOW_STATE_ICONIFIED);
        state->maximized = !!(ws & GDK_WINDOW_STATE_MAXIMIZED);
        state->fullscreen = !!(ws & GDK_WINDOW_STATE_FULLSCREEN);
    }
    state->visible = gtk_widget_get_visible(ctx->window);
    return KC_WVW_OK;
}

#pragma GCC diagnostic pop

#endif

#endif

static int kc_wvw_execute_op(kc_wvw_t *ctx,kc_wvw_op_t *op){
    kc_wvw_window_state_t state;if(!ctx||!op)return KC_WVW_ERROR;
    switch(op->kind){
    case KC_WVW_OP_NAVIGATE:return kc_wvw_navigate_impl(ctx,op->text);
    case KC_WVW_OP_ADD_INIT_SCRIPT:return kc_wvw_add_init_script_impl(ctx,op->text);
    case KC_WVW_OP_ENABLE_BRIDGE:return kc_wvw_enable_bridge_impl(ctx,op->bridge);
    case KC_WVW_OP_POST_BRIDGE_EVENT:return kc_wvw_post_bridge_event_impl(ctx,op->text);
    case KC_WVW_OP_HIDE:return kc_wvw_hide_impl(ctx);case KC_WVW_OP_SHOW:return kc_wvw_show_impl(ctx);
    case KC_WVW_OP_MINIMIZE:return kc_wvw_minimize_impl(ctx);case KC_WVW_OP_MAXIMIZE:return kc_wvw_maximize_impl(ctx);case KC_WVW_OP_RESTORE:return kc_wvw_restore_impl(ctx);
    case KC_WVW_OP_SET_TITLE:return kc_wvw_set_title_impl(ctx,op->text);case KC_WVW_OP_GET_TITLE:op->out_text=ctx->opts.title;return op->out_text?KC_WVW_OK:KC_WVW_ERROR;
    case KC_WVW_OP_SET_SIZE:return kc_wvw_set_size_impl(ctx,op->a,op->b);
    case KC_WVW_OP_GET_SIZE:if(!op->out_a||!op->out_b||kc_wvw_get_state_impl(ctx,&state)!=KC_WVW_OK)return KC_WVW_ERROR;*op->out_a=state.width;*op->out_b=state.height;return KC_WVW_OK;
    case KC_WVW_OP_IS_VISIBLE:case KC_WVW_OP_IS_MINIMIZED:case KC_WVW_OP_IS_MAXIMIZED:case KC_WVW_OP_IS_FULLSCREEN:
        if(kc_wvw_get_state_impl(ctx,&state)!=KC_WVW_OK)return KC_WVW_ERROR;
        op->a=op->kind==KC_WVW_OP_IS_VISIBLE?state.visible:op->kind==KC_WVW_OP_IS_MINIMIZED?state.minimized:op->kind==KC_WVW_OP_IS_MAXIMIZED?state.maximized:state.fullscreen;return KC_WVW_OK;
    default:return KC_WVW_ERROR;}
}
int kc_wvw_navigate(kc_wvw_t *ctx,const char *url){kc_wvw_op_t op={0};op.kind=KC_WVW_OP_NAVIGATE;op.text=url;return kc_wvw_dispatch_op(ctx,&op);}
int kc_wvw_add_init_script(kc_wvw_t *ctx,const char *js){kc_wvw_op_t op={0};op.kind=KC_WVW_OP_ADD_INIT_SCRIPT;op.text=js;return kc_wvw_dispatch_op(ctx,&op);}
int kc_wvw_enable_bridge(kc_wvw_t *ctx,const kc_wvw_bridge_options_t *o){kc_wvw_op_t op={0};op.kind=KC_WVW_OP_ENABLE_BRIDGE;op.bridge=o;return kc_wvw_dispatch_op(ctx,&op);}
int kc_wvw_post_bridge_event(kc_wvw_t *ctx,const char *json){kc_wvw_op_t op={0};op.kind=KC_WVW_OP_POST_BRIDGE_EVENT;op.text=json;return kc_wvw_dispatch_op(ctx,&op);}
int kc_wvw_hide(kc_wvw_t *ctx){kc_wvw_op_t op={0};op.kind=KC_WVW_OP_HIDE;return kc_wvw_dispatch_op(ctx,&op);}int kc_wvw_show(kc_wvw_t *ctx){kc_wvw_op_t op={0};op.kind=KC_WVW_OP_SHOW;return kc_wvw_dispatch_op(ctx,&op);}
int kc_wvw_minimize(kc_wvw_t *ctx){kc_wvw_op_t op={0};op.kind=KC_WVW_OP_MINIMIZE;return kc_wvw_dispatch_op(ctx,&op);}int kc_wvw_maximize(kc_wvw_t *ctx){kc_wvw_op_t op={0};op.kind=KC_WVW_OP_MAXIMIZE;return kc_wvw_dispatch_op(ctx,&op);}int kc_wvw_restore(kc_wvw_t *ctx){kc_wvw_op_t op={0};op.kind=KC_WVW_OP_RESTORE;return kc_wvw_dispatch_op(ctx,&op);}
int kc_wvw_set_title(kc_wvw_t *ctx,const char *title){kc_wvw_op_t op={0};op.kind=KC_WVW_OP_SET_TITLE;op.text=title;return kc_wvw_dispatch_op(ctx,&op);}
const char *kc_wvw_get_title(const kc_wvw_t *ctx){kc_wvw_op_t op={0};op.kind=KC_WVW_OP_GET_TITLE;if(kc_wvw_dispatch_op((kc_wvw_t *)ctx,&op)!=KC_WVW_OK)return NULL;return op.out_text;}
int kc_wvw_set_size(kc_wvw_t *ctx,int w,int h){kc_wvw_op_t op={0};op.kind=KC_WVW_OP_SET_SIZE;op.a=w;op.b=h;return kc_wvw_dispatch_op(ctx,&op);}
int kc_wvw_get_size(const kc_wvw_t *ctx,int *w,int *h){kc_wvw_op_t op={0};op.kind=KC_WVW_OP_GET_SIZE;op.out_a=w;op.out_b=h;return kc_wvw_dispatch_op((kc_wvw_t *)ctx,&op);}
static int kc_wvw_bool_query(const kc_wvw_t *ctx,kc_wvw_op_kind_t k){kc_wvw_op_t op={0};op.kind=k;if(kc_wvw_dispatch_op((kc_wvw_t *)ctx,&op)!=KC_WVW_OK)return 0;return !!op.a;}
int kc_wvw_is_visible(const kc_wvw_t *ctx){return kc_wvw_bool_query(ctx,KC_WVW_OP_IS_VISIBLE);}int kc_wvw_is_minimized(const kc_wvw_t *ctx){return kc_wvw_bool_query(ctx,KC_WVW_OP_IS_MINIMIZED);}int kc_wvw_is_maximized(const kc_wvw_t *ctx){return kc_wvw_bool_query(ctx,KC_WVW_OP_IS_MAXIMIZED);}int kc_wvw_is_fullscreen(const kc_wvw_t *ctx){return kc_wvw_bool_query(ctx,KC_WVW_OP_IS_FULLSCREEN);}
