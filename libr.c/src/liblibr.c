/**
 * liblibr.c - Summary of the functionality
 * Summary: Core implementation for the libr library.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#include "liblibr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include <signal.h>

struct kc_libr_options {
    char *param;
};

struct kc_libr {
    struct kc_libr_options opts;
    volatile sig_atomic_t stop_requested;
    int state;
    char error[256];
};

static void kc_libr_set_error(kc_libr_t *ctx, const char *fmt, ...);

#ifndef KC_LIBR_BUILD_VERSION
#define KC_LIBR_BUILD_VERSION 0
#endif

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_libr_version(void) {
    return (uint64_t)KC_LIBR_BUILD_VERSION;
}

/**
 * Return default options for the library (caller owns, must free).
 * @return Opaque options handle, or NULL on failure.
 */
kc_libr_options_t kc_libr_options_default(void) {
    struct kc_libr_options *opts = (struct kc_libr_options *)calloc(1, sizeof(struct kc_libr_options));
    return (kc_libr_options_t)opts;
}

/**
 * Set an option value by key.
 * @param opts Options handle from kc_libr_options_default.
 * @param key Option key (e.g., "param").
 * @param value Option value, or NULL to clear.
 * @return KC_LIBR_OK on success, KC_LIBR_ERROR on unknown key.
 */
int kc_libr_options_set(kc_libr_options_t opts, const char *key, const char *value) {
    struct kc_libr_options *o = (struct kc_libr_options *)opts;
    if (!o || !key) return KC_LIBR_ERROR;

    if (strcmp(key, "param") == 0) {
        free(o->param);
        o->param = value ? strdup(value) : NULL;
        return KC_LIBR_OK;
    }
    return KC_LIBR_ERROR;
}

/**
 * Release resources owned by options handle.
 * @param opts Options handle from kc_libr_options_default.
 * @return None.
 */
void kc_libr_options_free(kc_libr_options_t opts) {
    struct kc_libr_options *o = (struct kc_libr_options *)opts;
    if (!o) return;
    free(o->param);
    free(o);
}

/**
 * Initialize a new libr context.
 * @param ctx_out Destination context pointer.
 * @param opts Opaque options handle from kc_libr_options_default.
 * @return KC_LIBR_OK on success, KC_LIBR_ERROR on failure.
 */
int kc_libr_open(void **ctx_out, kc_libr_options_t opts) {
    if (!ctx_out || !opts) return KC_LIBR_ERROR;
    *ctx_out = NULL;

    struct kc_libr_options *o = (struct kc_libr_options *)opts;

    kc_libr_t *ctx = (kc_libr_t *)calloc(1, sizeof(kc_libr_t));
    if (!ctx) return KC_LIBR_ERROR;

    ctx->opts = *o;
    ctx->opts.param = o->param ? strdup(o->param) : NULL;
    if (o->param && !ctx->opts.param) {
        kc_libr_set_error(ctx, "out of memory");
        free(ctx);
        return KC_LIBR_ERROR;
    }

    *ctx_out = ctx;
    return KC_LIBR_OK;
}

/**
 * Release a libr context.
 * @param ctx Context pointer.
 * @return KC_LIBR_OK.
 */
int kc_libr_close(void *ctx) {
    if (!ctx) return KC_LIBR_OK;

    kc_libr_t *c = (kc_libr_t *)ctx;
    free(c->opts.param);
    free(c);
    return KC_LIBR_OK;
}

/**
 * Set an error message on the context.
 * @param ctx Context pointer.
 * @param fmt printf-style format string.
 * @return None.
 */
static void kc_libr_set_error(kc_libr_t *ctx, const char *fmt, ...) {
    if (!ctx || !fmt) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ctx->error, sizeof(ctx->error), fmt, ap);
    va_end(ap);
    ctx->error[sizeof(ctx->error) - 1] = '\0';
}

/**
 * Execute a core libr operation.
 * @param ctx Context pointer.
 * @param input Operation input.
 * @return Status code.
 */
int kc_libr_exec(void *ctx, const char *input) {
    kc_libr_t *c = (kc_libr_t *)ctx;
    if (!c || !input) {
        kc_libr_set_error(c, "invalid argument");
        return KC_LIBR_ERROR;
    }
    return KC_LIBR_OK;
}

/**
 * Request stop for a specific libr context.
 * @param ctx Context handle.
 * @return KC_LIBR_OK on success, KC_LIBR_ERROR on failure.
 */
int kc_libr_stop(void *ctx) {
    kc_libr_t *c = (kc_libr_t *)ctx;
    if (!c) return KC_LIBR_ERROR;
    c->stop_requested = 1;
    return KC_LIBR_OK;
}

/**
 * Get the last error message from a context.
 * @param ctx Context pointer.
 * @return Error string, or NULL if no error.
 */
const char *kc_libr_get_error(const void *ctx) {
    const kc_libr_t *c = (const kc_libr_t *)ctx;
    if (!c || !c->error[0]) return NULL;
    return c->error;
}
