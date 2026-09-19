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
kc_libr_options_t *
kc_libr_options_default(void) {
    struct kc_libr_options *opts = (struct kc_libr_options *)calloc(1, sizeof(struct kc_libr_options));
    return opts;
}

/**
 * Set an option value by key.
 * @param opts Options handle from kc_libr_options_default.
 * @param key Option key (e.g., "param").
 * @param value Option value, or NULL to clear.
 * @return KC_LIBR_OK on success, KC_LIBR_ERROR on unknown key.
 */
int kc_libr_options_set(kc_libr_options_t *opts, const char *key, const char *value) {
    if (!opts || !key) return KC_LIBR_ERROR;

    if (strcmp(key, "param") == 0) {
        if (value == NULL) {
            free(opts->param);
            opts->param = NULL;
            return KC_LIBR_OK;
        }
        char *new_param = strdup(value);
        if (!new_param) {
            return KC_LIBR_ERROR;
        }
        free(opts->param);
        opts->param = new_param;
        return KC_LIBR_OK;
    }
    return KC_LIBR_ERROR;
}

/**
 * Release resources owned by options handle.
 * @param opts Options handle from kc_libr_options_default.
 * @return None.
 */
void kc_libr_options_free(kc_libr_options_t *opts) {
    if (!opts) return;
    free(opts->param);
    free(opts);
}

/**
 * Initialize a new libr context.
 * @param out Destination context pointer.
 * @param opts Opaque options handle from kc_libr_options_default.
 * @return KC_LIBR_OK on success, KC_LIBR_ERROR on failure.
 */
int kc_libr_open(
    kc_libr_t **out,
    const kc_libr_options_t *opts
) {
    if (!out || !opts) return KC_LIBR_ERROR;
    *out = NULL;

    kc_libr_t *ctx = (kc_libr_t *)calloc(1, sizeof(kc_libr_t));
    if (!ctx) return KC_LIBR_ERROR;

    ctx->opts.param = opts->param ? strdup(opts->param) : NULL;
    if (opts->param && !ctx->opts.param) {
        kc_libr_set_error(ctx, "out of memory");
        free(ctx);
        return KC_LIBR_ERROR;
    }

    *out = ctx;
    return KC_LIBR_OK;
}

/**
 * Release a libr context.
 * @param ctx Context pointer.
 * @return None.
 */
void kc_libr_close(kc_libr_t *ctx) {
    if (!ctx) return;

    free(ctx->opts.param);
    free(ctx);
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
int kc_libr_exec(kc_libr_t *ctx, const char *input) {
    if (!ctx || !input) {
        kc_libr_set_error(ctx, "invalid argument");
        return KC_LIBR_ERROR;
    }
    return KC_LIBR_OK;
}

/**
 * Request stop for a specific libr context.
 * @param ctx Context handle.
 * @return KC_LIBR_OK on success, KC_LIBR_ERROR on failure.
 */
int kc_libr_stop(kc_libr_t *ctx) {
    if (!ctx) return KC_LIBR_ERROR;
    ctx->stop_requested = 1;
    return KC_LIBR_OK;
}

/**
 * Check if stop was requested for a context.
 * @param ctx Context pointer.
 * @return 1 if stop was requested, 0 otherwise.
 */
int kc_libr_stop_requested(const kc_libr_t *ctx) {
    if (!ctx) return 0;
    return ctx->stop_requested ? 1 : 0;
}

/**
 * Get the last error message from a context.
 * @param ctx Context pointer.
 * @return Error string, or NULL if no error.
 */
const char *kc_libr_get_error(const kc_libr_t *ctx) {
    if (!ctx || !ctx->error[0]) return NULL;
    return ctx->error;
}
