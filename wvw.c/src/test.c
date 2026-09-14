/**
 * test.c - libwvw public API tests.
 * Summary: Exercises the libwvw public contract through 9 grouped functional
 *          cases (version, options, context lifecycle, navigation, bridge,
 *          visibility, window state, title, and size).
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libwvw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static int test_case_total = 0;
static int test_case_current = 0;
static int test_grouped = 0;

/**
 * Prints a test case result line.
 * Suppressed for grouped internal cases so only one result line is printed
 * per externally registered case.
 * @param fail Non-zero when the case failed.
 * @param name Public API function under test.
 * @param detail Behavior verified by the case.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    if (test_grouped) return;
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

/**
 * Opens a WebView context, retrying to tolerate flaky backends.
 * @param ctx Receives the opened context.
 * @param opts Options for the new context.
 * @return 1 on success, 0 on failure.
 */
static int open_webview(kc_wvw_t **ctx, kc_wvw_options_t *opts) {
    for (int tries = 0; tries < 3; tries++) {
        *ctx = NULL;
        if (kc_wvw_open(ctx, opts) == KC_WVW_OK) {
            return 1;
        }
        if (tries < 2) {
#ifdef _WIN32
            Sleep(600 * (tries + 1));
#else
            usleep(600 * (tries + 1) * 1000);
#endif
        }
    }
    return 0;
}

typedef int (*case_fn)(void);

/**
 * Runs one test case function and accumulates the result count.
 * @param rc Pointer to result counter (incremented by return value of fn).
 * @param fn Test case function to execute.
 * @return void
 */
static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

/**
 * Verifies one integer result.
 * @param name Check description.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return 0 on success, 1 on failure.
 */
static int expect_int(const char *name, int expected, int actual) {
    if (expected != actual) {
        printf("[FAIL] %s: expected %d, got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

/**
 * Verifies one boolean condition.
 * @param name Check description.
 * @param condition Non-zero when the check passed.
 * @return 0 on success, 1 on failure.
 */
static int expect_true(const char *name, int condition) {
    if (!condition) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

/**
 * Creates one temporary HTML file and returns its file URL.
 * @return Malloc'd file URL string, or NULL on failure.
 */
static char *make_test_url(void) {
    char path[2048];
    char url[2200];
    FILE *f;
#ifdef _WIN32
    char dir[MAX_PATH];
    size_t k;
    if (!GetTempPathA((DWORD)sizeof(dir), dir) || strlen(dir) == 0) {
        return NULL;
    }
    snprintf(path, sizeof(path), "%swvw_test_file.html", dir);
    if ((int)strlen(path) > 1 && path[1] == ':') {
        for (k = 0; path[k]; k++) {
            if (path[k] == '\\') {
                path[k] = '/';
            }
        }
    }
#else
    snprintf(path, sizeof(path), "/tmp/wvw_test_file.html");
#endif
    f = fopen(path, "w");
    if (!f) {
        return NULL;
    }
    fprintf(f, "<html><head><title>wvw test</title></head><body>test</body></html>");
    fclose(f);
#ifdef _WIN32
    snprintf(url, sizeof(url), "file:///%s", path);
#else
    snprintf(url, sizeof(url), "file://%s", path);
#endif
    return strdup(url);
}

/**
 * Tests kc_wvw_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_version(void) {
    const char *name = "kc_wvw_version";
    const char *detail = "returns build timestamp";
    int fail = 0;
    kc_wvw_version();
    fail += expect_true("version does not crash", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_options_default.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_options_default(void) {
    const char *name = "kc_wvw_options_default";
    const char *detail = "initializes defaults";
    int fail = 0;
    kc_wvw_options_default();
    fail += expect_true("options_default does not crash", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_options_load_env.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_options_load_env(void) {
    const char *name = "kc_wvw_options_load_env";
    const char *detail = "loads environment values";
    int fail = 0;
    kc_wvw_options_t opts = {0};
    kc_wvw_options_load_env(&opts);
    kc_wvw_options_load_env(NULL);
    fail += expect_true("load_env does not crash", 1);
    kc_wvw_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_options_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_options_free(void) {
    const char *name = "kc_wvw_options_free";
    const char *detail = "clears owned resources";
    int fail = 0;
    kc_wvw_options_t opts = {0};
    kc_wvw_options_free(&opts);
    kc_wvw_options_free(NULL);
    fail += expect_true("options_free does not crash", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_open error paths.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_open(void) {
    const char *name = "kc_wvw_open";
    const char *detail = "rejects invalid inputs";
    int fail = 0;
    kc_wvw_t *ctx = NULL;
    kc_wvw_options_t opts;

    opts = kc_wvw_options_default();
    fail += expect_int("open(NULL, opts) returns ERROR", KC_WVW_ERROR, kc_wvw_open(NULL, &opts));
    fail += expect_int("open(out, NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_open(&ctx, NULL));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_get_error.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_get_error(void) {
    const char *name = "kc_wvw_get_error";
    const char *detail = "accepts null context";
    int fail = 0;

    fail += expect_true("get_error(NULL) returns NULL",
        kc_wvw_get_error(NULL) == NULL);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_close(void) {
    const char *name = "kc_wvw_close";
    const char *detail = "accepts null context";
    int fail = 0;

    fail += expect_int("close(NULL) returns OK", KC_WVW_OK, kc_wvw_close(NULL));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_stop(void) {
    const char *name = "kc_wvw_stop";
    const char *detail = "rejects null context";
    int fail = 0;

    fail += expect_int("stop(NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_stop(NULL));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_loop.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_loop(void) {
    const char *name = "kc_wvw_loop";
    const char *detail = "rejects null context";
    int fail = 0;

    fail += expect_int("loop(NULL) returns ERROR", KC_WVW_ERROR,
        kc_wvw_loop(NULL));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_navigate.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_navigate(void) {
    const char *name = "kc_wvw_navigate";
    const char *detail = "rejects null context";
    int fail = 0;

    fail += expect_int("navigate(NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_navigate(NULL, "http://localhost"));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_enable_bridge.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_enable_bridge(void) {
    const char *name = "kc_wvw_enable_bridge";
    const char *detail = "rejects null context";
    int fail = 0;
    kc_wvw_bridge_options_t opts = {0};

    fail += expect_int("enable_bridge(NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_enable_bridge(NULL, &opts));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests bridge initialization with an empty method registry.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_bridge_empty_registry(void) {
    const char *name = "kc_wvw_bridge_empty_registry";
    const char *detail = "accepts empty method registry";
    int fail = 0;
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bopts;
    kc_wvw_t *ctx = NULL;

    opts = kc_wvw_options_default();
    free(opts.url);
    opts.url = make_test_url();
    if (!open_webview(&ctx, &opts)) {
        kc_wvw_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    memset(&bopts, 0, sizeof(bopts));
    bopts.methods = NULL;
    bopts.method_count = 0;
    bopts.callback = NULL;
    bopts.userdata = NULL;
    bopts.allow_file = 1;
    fail += expect_int("enable_bridge(empty) returns OK", KC_WVW_OK, kc_wvw_enable_bridge(ctx, &bopts));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests negative method_count is rejected.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_bridge_negative_method_count(void) {
    const char *name = "kc_wvw_bridge_negative_method_count";
    const char *detail = "rejects negative method count";
    int fail = 0;
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bopts;
    kc_wvw_t *ctx = NULL;
    const char *methods[] = { "test" };

    opts = kc_wvw_options_default();
    free(opts.url);
    opts.url = make_test_url();
    if (!open_webview(&ctx, &opts)) {
        kc_wvw_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    memset(&bopts, 0, sizeof(bopts));
    bopts.methods = methods;
    bopts.method_count = -1;
    bopts.callback = (kc_wvw_bridge_callback_t)1;
    fail += expect_int("negative method_count rejected", KC_WVW_ERROR,
        kc_wvw_enable_bridge(ctx, &bopts));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests positive method_count without methods is rejected.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_bridge_positive_without_methods(void) {
    const char *name = "kc_wvw_bridge_positive_without_methods";
    const char *detail = "rejects count without methods";
    int fail = 0;
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bopts;
    kc_wvw_t *ctx = NULL;

    opts = kc_wvw_options_default();
    free(opts.url);
    opts.url = make_test_url();
    if (!open_webview(&ctx, &opts)) {
        kc_wvw_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    memset(&bopts, 0, sizeof(bopts));
    bopts.methods = NULL;
    bopts.method_count = 1;
    bopts.callback = (kc_wvw_bridge_callback_t)1;
    fail += expect_int("positive count without methods rejected", KC_WVW_ERROR,
        kc_wvw_enable_bridge(ctx, &bopts));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests positive method_count without callback is rejected.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_bridge_positive_without_callback(void) {
    const char *name = "kc_wvw_bridge_positive_without_callback";
    const char *detail = "rejects methods without callback";
    int fail = 0;
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bopts;
    kc_wvw_t *ctx = NULL;
    const char *methods[] = { "test" };

    opts = kc_wvw_options_default();
    free(opts.url);
    opts.url = make_test_url();
    if (!open_webview(&ctx, &opts)) {
        kc_wvw_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    memset(&bopts, 0, sizeof(bopts));
    bopts.methods = methods;
    bopts.method_count = 1;
    bopts.callback = NULL;
    fail += expect_int("positive count without callback rejected", KC_WVW_ERROR,
        kc_wvw_enable_bridge(ctx, &bopts));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_post_bridge_event.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_post_bridge_event(void) {
    const char *name = "kc_wvw_post_bridge_event";
    const char *detail = "rejects null context";
    int fail = 0;

    fail += expect_int("post_bridge_event(NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_post_bridge_event(NULL, "{}"));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_hide.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_hide(void) {
    const char *name = "kc_wvw_hide";
    const char *detail = "rejects null context";
    int fail = 0;

    fail += expect_int("hide(NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_hide(NULL));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_show.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_show(void) {
    const char *name = "kc_wvw_show";
    const char *detail = "rejects null context";
    int fail = 0;

    fail += expect_int("show(NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_show(NULL));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_minimize.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_minimize(void) {
    const char *name = "kc_wvw_minimize";
    const char *detail = "rejects null context";
    int fail = 0;

    fail += expect_int("minimize(NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_minimize(NULL));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_maximize.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_maximize(void) {
    const char *name = "kc_wvw_maximize";
    const char *detail = "rejects null context";
    int fail = 0;

    fail += expect_int("maximize(NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_maximize(NULL));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_restore.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_restore(void) {
    const char *name = "kc_wvw_restore";
    const char *detail = "rejects null context";
    int fail = 0;

    fail += expect_int("restore(NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_restore(NULL));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_set_title.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_set_title(void) {
    const char *name = "kc_wvw_set_title";
    const char *detail = "rejects invalid context and null title";
    int fail = 0;
    kc_wvw_options_t opts;
    kc_wvw_t *ctx = NULL;

    fail += expect_int("set_title(NULL, str) returns ERROR", KC_WVW_ERROR, kc_wvw_set_title(NULL, "test"));

    opts = kc_wvw_options_default();
    free(opts.url);
    opts.url = make_test_url();
    if (!open_webview(&ctx, &opts)) {
        kc_wvw_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    fail += expect_int("set_title(ctx, NULL) returns ERROR", KC_WVW_ERROR, kc_wvw_set_title(ctx, NULL));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests KC_WVW_TITLE_MAX enforcement on a live context.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_set_title_bounds(void) {
    const char *name = "kc_wvw_set_title_bounds";
    const char *detail = "enforces KC_WVW_TITLE_MAX";
    int fail = 0;
    kc_wvw_options_t opts;
    kc_wvw_t *ctx = NULL;
    char *long_title;
    char *max_title;
    int i;

    opts = kc_wvw_options_default();
    free(opts.url);
    opts.url = make_test_url();
    if (!open_webview(&ctx, &opts)) {
        kc_wvw_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    long_title = (char *)malloc((size_t)KC_WVW_TITLE_MAX + 2);
    max_title = (char *)malloc((size_t)KC_WVW_TITLE_MAX + 1);
    if (long_title && max_title) {
        for (i = 0; i < KC_WVW_TITLE_MAX; i++) {
            long_title[i] = 't';
            max_title[i] = 't';
        }
        long_title[KC_WVW_TITLE_MAX] = 't';
        long_title[KC_WVW_TITLE_MAX + 1] = '\0';
        max_title[KC_WVW_TITLE_MAX] = '\0';
        fail += expect_int("set_title valid short title returns OK",
            KC_WVW_OK, kc_wvw_set_title(ctx, "test title"));
        fail += expect_int("set_title at KC_WVW_TITLE_MAX returns OK",
            KC_WVW_OK, kc_wvw_set_title(ctx, max_title));
        fail += expect_int("set_title over KC_WVW_TITLE_MAX returns ERROR",
            KC_WVW_ERROR, kc_wvw_set_title(ctx, long_title));
    } else {
        fail += 1;
        printf("[FAIL] bound test title allocation failed\n");
    }
    free(long_title);
    free(max_title);
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_set_size.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_set_size(void) {
    const char *name = "kc_wvw_set_size";
    const char *detail = "rejects null context";
    int fail = 0;

    fail += expect_int("set_size(NULL, 100, 100) returns ERROR", KC_WVW_ERROR, kc_wvw_set_size(NULL, 100, 100));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests KC_WVW_SIZE_MAX enforcement on a live context.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_set_size_bounds(void) {
    const char *name = "kc_wvw_set_size_bounds";
    const char *detail = "enforces KC_WVW_SIZE_MAX";
    int fail = 0;
    kc_wvw_options_t opts;
    kc_wvw_t *ctx = NULL;

    opts = kc_wvw_options_default();
    free(opts.url);
    opts.url = make_test_url();
    if (!open_webview(&ctx, &opts)) {
        kc_wvw_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    fail += expect_int("set_size valid size returns OK",
        KC_WVW_OK, kc_wvw_set_size(ctx, 100, 100));
    fail += expect_int("set_size zero width returns ERROR",
        KC_WVW_ERROR, kc_wvw_set_size(ctx, 0, 100));
    fail += expect_int("set_size width over KC_WVW_SIZE_MAX returns ERROR",
        KC_WVW_ERROR, kc_wvw_set_size(ctx, KC_WVW_SIZE_MAX + 1, 100));
    fail += expect_int("set_size height over KC_WVW_SIZE_MAX returns ERROR",
        KC_WVW_ERROR, kc_wvw_set_size(ctx, 100, KC_WVW_SIZE_MAX + 1));
    fail += expect_int("set_size width at KC_WVW_SIZE_MAX returns OK",
        KC_WVW_OK, kc_wvw_set_size(ctx, KC_WVW_SIZE_MAX, 100));
    fail += expect_int("set_size height at KC_WVW_SIZE_MAX returns OK",
        KC_WVW_OK, kc_wvw_set_size(ctx, 100, KC_WVW_SIZE_MAX));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wvw_get_state.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_get_state(void) {
    const char *name = "kc_wvw_get_state";
    const char *detail = "rejects null context";
    int fail = 0;
    kc_wvw_window_state_t state;
    fail += expect_int("get_state(NULL, &state) returns ERROR", KC_WVW_ERROR, kc_wvw_get_state(NULL, &state));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests empty registry does not trust every URL via navigate.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_bridge_empty_registry_rejects_remote(void) {
    const char *name = "kc_wvw_bridge_empty_registry_rejects_remote";
    const char *detail = "rejects untrusted remote navigation";
    int fail = 0;
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bopts;
    kc_wvw_t *ctx = NULL;

    opts = kc_wvw_options_default();
    free(opts.url);
    opts.url = make_test_url();
    if (!open_webview(&ctx, &opts)) {
        kc_wvw_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    memset(&bopts, 0, sizeof(bopts));
    bopts.methods = NULL;
    bopts.method_count = 0;
    bopts.callback = NULL;
    bopts.allow_file = 1;
    if (kc_wvw_enable_bridge(ctx, &bopts) != KC_WVW_OK) {
        kc_wvw_close(ctx);
        kc_wvw_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    fail += expect_int("navigate to same file origin succeeds",
        KC_WVW_OK, kc_wvw_navigate(ctx, opts.url));
    fail += expect_int("navigate to unrelated remote URL rejected",
        KC_WVW_ERROR, kc_wvw_navigate(ctx, "https://evil.example.com/"));
    fail += expect_int("navigate to data: URL rejected",
        KC_WVW_ERROR, kc_wvw_navigate(ctx, "data:text/html,<h1>hi</h1>"));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests scheme allowances are enforced via enable_bridge.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_bridge_scheme_allowances(void) {
    const char *name = "kc_wvw_bridge_scheme_allowances";
    const char *detail = "enforces scheme allowances";
    int fail = 0;
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bopts;
    kc_wvw_t *ctx = NULL;

    opts = kc_wvw_options_default();
    free(opts.url);
    opts.url = make_test_url();
    if (!open_webview(&ctx, &opts)) {
        kc_wvw_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    memset(&bopts, 0, sizeof(bopts));
    bopts.methods = NULL;
    bopts.method_count = 0;
    bopts.callback = NULL;
    bopts.allow_file = 1;
    fail += expect_int("file:// initial URL accepted when allow_file=1",
        KC_WVW_OK, kc_wvw_enable_bridge(ctx, &bopts));
    fail += expect_int("navigate to data: blocked when allow_data=0",
        KC_WVW_ERROR, kc_wvw_navigate(ctx, "data:text/html,<h1>test</h1>"));
    fail += expect_int("navigate to remote blocked when not same-origin",
        KC_WVW_ERROR, kc_wvw_navigate(ctx, "https://evil.example.com/"));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests navigate rejects untrusted URLs with bridge enabled.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_navigate_rejects_untrusted(void) {
    const char *name = "kc_wvw_navigate_rejects_untrusted";
    const char *detail = "rejects untrusted URLs with bridge enabled";
    int fail = 0;
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bopts;
    kc_wvw_t *ctx = NULL;

    opts = kc_wvw_options_default();
    free(opts.url);
    opts.url = make_test_url();
    if (!open_webview(&ctx, &opts)) {
        kc_wvw_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    memset(&bopts, 0, sizeof(bopts));
    bopts.methods = NULL;
    bopts.method_count = 0;
    bopts.callback = NULL;
    bopts.allow_file = 1;
    if (kc_wvw_enable_bridge(ctx, &bopts) != KC_WVW_OK) {
        kc_wvw_close(ctx);
        kc_wvw_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    fail += expect_int("navigate to same file origin succeeds",
        KC_WVW_OK, kc_wvw_navigate(ctx, opts.url));
    fail += expect_int("navigate to remote URL rejected",
        KC_WVW_ERROR, kc_wvw_navigate(ctx, "https://evil.example.com/"));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests localhost bridge trust matches exact authority, not suffix text.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_bridge_localhost_authority(void) {
    const char *name = "kc_wvw_bridge_localhost_authority";
    const char *detail = "trusts exact localhost authority, rejects lookalikes";
    int fail = 0;
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bopts;
    kc_wvw_t *ctx = NULL;

    opts = kc_wvw_options_default();
    free(opts.url);
    opts.url = make_test_url();
    if (!open_webview(&ctx, &opts)) {
        kc_wvw_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    memset(&bopts, 0, sizeof(bopts));
    bopts.methods = NULL;
    bopts.method_count = 0;
    bopts.callback = NULL;
    bopts.allow_file = 1;
    bopts.allow_localhost = 1;
    if (kc_wvw_enable_bridge(ctx, &bopts) != KC_WVW_OK) {
        kc_wvw_close(ctx);
        kc_wvw_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    fail += expect_int("exact localhost host accepted",
        KC_WVW_OK, kc_wvw_navigate(ctx, "http://localhost/"));
    fail += expect_int("exact localhost with port accepted",
        KC_WVW_OK, kc_wvw_navigate(ctx, "http://localhost:8080/path"));
    fail += expect_int("exact https localhost accepted",
        KC_WVW_OK, kc_wvw_navigate(ctx, "https://localhost/"));
    fail += expect_int("empty localhost port rejected",
        KC_WVW_ERROR, kc_wvw_navigate(ctx, "http://localhost:"));
    fail += expect_int("non-numeric localhost port rejected",
        KC_WVW_ERROR, kc_wvw_navigate(ctx, "http://localhost:notaport"));
    fail += expect_int("mixed localhost port rejected",
        KC_WVW_ERROR, kc_wvw_navigate(ctx, "http://localhost:80x"));
    fail += expect_int("remote userinfo host rejected",
        KC_WVW_ERROR, kc_wvw_navigate(ctx, "http://localhost@evil.example/"));
    fail += expect_int("localhost userinfo host rejected",
        KC_WVW_ERROR, kc_wvw_navigate(ctx, "http://evil.example@localhost/"));
    fail += expect_int("localhost subdomain lookalike rejected",
        KC_WVW_ERROR, kc_wvw_navigate(ctx, "http://localhost.evil.com/"));
    fail += expect_int("name-prefix lookalike rejected",
        KC_WVW_ERROR, kc_wvw_navigate(ctx, "http://evil-localhost.com/"));
    fail += expect_int("localhost suffix lookalike rejected",
        KC_WVW_ERROR, kc_wvw_navigate(ctx, "http://localhost.evil/"));
    fail += expect_int("loopback IP not treated as localhost host",
        KC_WVW_ERROR, kc_wvw_navigate(ctx, "http://127.0.0.1/"));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests method_count is preserved for empty registry.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_bridge_empty_registry_method_count(void) {
    const char *name = "kc_wvw_bridge_empty_registry_method_count";
    const char *detail = "preserves an empty method registry";
    int fail = 0;
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bopts;
    kc_wvw_t *ctx = NULL;

    opts = kc_wvw_options_default();
    free(opts.url);
    opts.url = make_test_url();
    if (!open_webview(&ctx, &opts)) {
        kc_wvw_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    memset(&bopts, 0, sizeof(bopts));
    bopts.methods = NULL;
    bopts.method_count = 0;
    bopts.callback = NULL;
    bopts.allow_file = 1;
    if (kc_wvw_enable_bridge(ctx, &bopts) != KC_WVW_OK) {
        kc_wvw_close(ctx);
        kc_wvw_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    kc_wvw_window_state_t state;
    fail += expect_int("get_state succeeds with empty bridge",
        KC_WVW_OK, kc_wvw_get_state(ctx, &state));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests minimize then restore returns window to visible normal state.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_restore_restores_minimized(void) {
    const char *name = "kc_wvw_restore_restores_minimized";
    const char *detail = "restores a minimized window";
    int fail = 0;
    kc_wvw_options_t opts;
    kc_wvw_t *ctx = NULL;
    kc_wvw_window_state_t state;

    opts = kc_wvw_options_default();
    free(opts.url);
    opts.url = make_test_url();
    if (!open_webview(&ctx, &opts)) {
        kc_wvw_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    fail += expect_int("minimize returns OK", KC_WVW_OK, kc_wvw_minimize(ctx));
    fail += expect_int("restore returns OK", KC_WVW_OK, kc_wvw_restore(ctx));
    memset(&state, 0, sizeof(state));
    if (kc_wvw_get_state(ctx, &state) == KC_WVW_OK) {
        fail += expect_int("get_state after restore reports visible", 1, state.visible);
        fail += expect_int("get_state after restore reports not minimized", 0, state.minimized);
    } else {
        fail += 1;
        printf("[FAIL] get_state after restore returned ERROR\n");
    }
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests hide then show returns window to visible state.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_show_reveals_hidden(void) {
    const char *name = "kc_wvw_show_reveals_hidden";
    const char *detail = "reveals a hidden window";
    int fail = 0;
    kc_wvw_options_t opts;
    kc_wvw_t *ctx = NULL;
    kc_wvw_window_state_t state;

    opts = kc_wvw_options_default();
    free(opts.url);
    opts.url = make_test_url();
    if (!open_webview(&ctx, &opts)) {
        kc_wvw_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    fail += expect_int("hide returns OK", KC_WVW_OK, kc_wvw_hide(ctx));
    memset(&state, 0, sizeof(state));
    if (kc_wvw_get_state(ctx, &state) == KC_WVW_OK) {
        fail += expect_int("get_state after hide reports not visible", 0, state.visible);
    } else {
        fail += 1;
        printf("[FAIL] get_state after hide returned ERROR\n");
    }
    fail += expect_int("show returns OK", KC_WVW_OK, kc_wvw_show(ctx));
    memset(&state, 0, sizeof(state));
    if (kc_wvw_get_state(ctx, &state) == KC_WVW_OK) {
        fail += expect_int("get_state after show reports visible", 1, state.visible);
    } else {
        fail += 1;
        printf("[FAIL] get_state after show returned ERROR\n");
    }
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests show on already-visible window does not change state.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_show_no_hide_visible(void) {
    const char *name = "kc_wvw_show_no_hide_visible";
    const char *detail = "keeps a visible window shown";
    int fail = 0;
    kc_wvw_options_t opts;
    kc_wvw_t *ctx = NULL;
    kc_wvw_window_state_t state;

    opts = kc_wvw_options_default();
    free(opts.url);
    opts.url = make_test_url();
    if (!open_webview(&ctx, &opts)) {
        kc_wvw_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    fail += expect_int("show on visible window returns OK", KC_WVW_OK, kc_wvw_show(ctx));
    memset(&state, 0, sizeof(state));
    if (kc_wvw_get_state(ctx, &state) == KC_WVW_OK) {
        fail += expect_int("get_state after show reports visible", 1, state.visible);
    } else {
        fail += 1;
        printf("[FAIL] get_state after show returned ERROR\n");
    }
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests no_focus option is stored correctly in options struct.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_options_no_focus(void) {
    const char *name = "kc_wvw_options_no_focus";
    const char *detail = "stores and clears no-focus state";
    int fail = 0;
    kc_wvw_options_t opts;

    opts = kc_wvw_options_default();
    fail += expect_int("default no_focus is 0", 0, opts.no_focus);
    opts.no_focus = 1;
    fail += expect_int("set no_focus is 1", 1, opts.no_focus);
    opts.no_focus = 0;
    fail += expect_int("cleared no_focus is 0", 0, opts.no_focus);
    kc_wvw_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Dummy bridge callback that accepts any allowed method.
 * @param ctx Window context.
 * @param method Method name.
 * @param params_json Parameters JSON.
 * @param result_json Output result JSON.
 * @param userdata User data.
 * @return KC_WVW_OK.
 */
static int dummy_bridge_callback(kc_wvw_t *ctx, const char *method, const char *params_json, char **result_json, void *userdata) {
    (void)ctx; (void)method; (void)params_json; (void)userdata;
    if (result_json) *result_json = NULL;
    return KC_WVW_OK;
}

/**
 * Tests bridge options accept custom method whitelist.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_bridge_custom_options(void) {
    const char *name = "kc_wvw_bridge_custom_options";
    const char *detail = "accepts a custom method whitelist";
    int fail = 0;
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bopts;
    kc_wvw_t *ctx = NULL;
    const char *methods[] = { "customMethod" };

    opts = kc_wvw_options_default();
    free(opts.url);
    opts.url = make_test_url();
    if (!open_webview(&ctx, &opts)) {
        kc_wvw_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    memset(&bopts, 0, sizeof(bopts));
    bopts.methods = methods;
    bopts.method_count = 1;
    bopts.callback = dummy_bridge_callback;
    bopts.userdata = NULL;
    bopts.allow_file = 1;
    fail += expect_int("enable_bridge with custom methods returns OK",
        KC_WVW_OK, kc_wvw_enable_bridge(ctx, &bopts));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests bridge options reject NativeBridge top-level properties.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_bridge_reserved_methods(void) {
    const char *name = "kc_wvw_bridge_reserved_methods";
    const char *detail = "rejects reserved NativeBridge property names";
    int fail = 0;
    int i;
    kc_wvw_options_t opts;
    kc_wvw_bridge_options_t bopts;
    kc_wvw_t *ctx = NULL;
    const char *builtin_methods[] = {
        "minimize", "maximize", "restore", "close",
        "setTitle", "setSize", "getState"
    };
    const char *direct_method[] = { "directMethod" };
    const char *window_custom_method[] = { "window" };

    opts = kc_wvw_options_default();
    free(opts.url);
    opts.url = make_test_url();
    if (!open_webview(&ctx, &opts)) {
        kc_wvw_options_free(&opts);
        case_result(1, name, detail);
        return 1;
    }
    memset(&bopts, 0, sizeof(bopts));
    bopts.method_count = 1;
    bopts.callback = dummy_bridge_callback;
    bopts.allow_file = 1;
    for (i = 0; i < (int)(sizeof(builtin_methods) / sizeof(builtin_methods[0])); i++) {
        bopts.methods = &builtin_methods[i];
        fail += expect_int("reserved built-in method rejected", KC_WVW_ERROR,
            kc_wvw_enable_bridge(ctx, &bopts));
    }
    bopts.methods = direct_method;
    fail += expect_int("direct method accepted", KC_WVW_OK,
        kc_wvw_enable_bridge(ctx, &bopts));
    bopts.methods = window_custom_method;
    fail += expect_int("custom window method accepted", KC_WVW_OK,
        kc_wvw_enable_bridge(ctx, &bopts));
    kc_wvw_close(ctx);
    kc_wvw_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Groups the options contract cases into one top-level case.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_options(void) {
    int fail = 0;
    int grouped = test_grouped;

    test_grouped = 1;
    fail += case_kc_wvw_options_default();
    fail += case_kc_wvw_options_load_env();
    fail += case_kc_wvw_options_free();
    fail += case_kc_wvw_options_no_focus();
    test_grouped = grouped;
    case_result(fail, "kc_wvw_options",
        "defaults, env loading, free, no-focus");
    return fail == 0 ? 0 : 1;
}

/**
 * Groups the context lifecycle and error cases into one top-level case.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_context(void) {
    int fail = 0;
    int grouped = test_grouped;

    test_grouped = 1;
    fail += case_kc_wvw_open();
    fail += case_kc_wvw_get_error();
    fail += case_kc_wvw_close();
    fail += case_kc_wvw_stop();
    fail += case_kc_wvw_loop();
    test_grouped = grouped;
    case_result(fail, "kc_wvw_context",
        "context lifecycle and error surface");
    return fail == 0 ? 0 : 1;
}

/**
 * Groups the navigation trust and scheme allowance cases into one case.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_navigation(void) {
    int fail = 0;
    int grouped = test_grouped;

    test_grouped = 1;
    fail += case_kc_wvw_navigate();
    fail += case_kc_wvw_bridge_empty_registry_rejects_remote();
    fail += case_kc_wvw_bridge_scheme_allowances();
    fail += case_kc_wvw_navigate_rejects_untrusted();
    fail += case_kc_wvw_bridge_localhost_authority();
    test_grouped = grouped;
    case_result(fail, "kc_wvw_navigation",
        "trusted navigation and scheme limits");
    return fail == 0 ? 0 : 1;
}

/**
 * Groups the bridge enablement, registry, and transport cases into one case.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_bridge(void) {
    int fail = 0;
    int grouped = test_grouped;

    test_grouped = 1;
    fail += case_kc_wvw_enable_bridge();
    fail += case_kc_wvw_bridge_empty_registry();
    fail += case_kc_wvw_bridge_negative_method_count();
    fail += case_kc_wvw_bridge_positive_without_methods();
    fail += case_kc_wvw_bridge_positive_without_callback();
    fail += case_kc_wvw_post_bridge_event();
    fail += case_kc_wvw_bridge_empty_registry_method_count();
    fail += case_kc_wvw_bridge_custom_options();
    fail += case_kc_wvw_bridge_reserved_methods();
    test_grouped = grouped;
    case_result(fail, "kc_wvw_bridge",
        "enablement, registry, reserved names");
    return fail == 0 ? 0 : 1;
}

/**
 * Groups the window visibility cases into one top-level case.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_visibility(void) {
    int fail = 0;
    int grouped = test_grouped;

    test_grouped = 1;
    fail += case_kc_wvw_hide();
    fail += case_kc_wvw_show();
    fail += case_kc_wvw_show_reveals_hidden();
    fail += case_kc_wvw_show_no_hide_visible();
    test_grouped = grouped;
    case_result(fail, "kc_wvw_visibility",
        "hide and show visibility");
    return fail == 0 ? 0 : 1;
}

/**
 * Groups the window state transition and query cases into one top-level case.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_window_state(void) {
    int fail = 0;
    int grouped = test_grouped;

    test_grouped = 1;
    fail += case_kc_wvw_get_state();
    fail += case_kc_wvw_minimize();
    fail += case_kc_wvw_maximize();
    fail += case_kc_wvw_restore();
    fail += case_kc_wvw_restore_restores_minimized();
    test_grouped = grouped;
    case_result(fail, "kc_wvw_window_state",
        "minimize, restore, and window state query");
    return fail == 0 ? 0 : 1;
}

/**
 * Groups the window title cases into one top-level case.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_title(void) {
    int fail = 0;
    int grouped = test_grouped;

    test_grouped = 1;
    fail += case_kc_wvw_set_title();
    fail += case_kc_wvw_set_title_bounds();
    test_grouped = grouped;
    case_result(fail, "kc_wvw_title",
        "title bounds and KC_WVW_TITLE_MAX");
    return fail == 0 ? 0 : 1;
}

/**
 * Groups the window size cases into one top-level case.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wvw_size(void) {
    int fail = 0;
    int grouped = test_grouped;

    test_grouped = 1;
    fail += case_kc_wvw_set_size();
    fail += case_kc_wvw_set_size_bounds();
    test_grouped = grouped;
    case_result(fail, "kc_wvw_size",
        "size bounds and KC_WVW_SIZE_MAX");
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases via case_all().
 * @return 0 on success.
 */
static int case_all(void) {
    int rc = 0;
    test_case_current = 0;
    test_case_total = 9;
    run_case(&rc, case_kc_wvw_version);
    run_case(&rc, case_kc_wvw_options);
    run_case(&rc, case_kc_wvw_context);
    run_case(&rc, case_kc_wvw_navigation);
    run_case(&rc, case_kc_wvw_bridge);
    run_case(&rc, case_kc_wvw_visibility);
    run_case(&rc, case_kc_wvw_window_state);
    run_case(&rc, case_kc_wvw_title);
    run_case(&rc, case_kc_wvw_size);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Dispatches one named test case.
 * @param name Test case or group name.
 * @return 0 on success, 1 on failure, 2 for an unknown case.
 */
static int dispatch_case(const char *name) {
    if (strcmp(name, "all") == 0) return case_all();
    if (strcmp(name, "kc_wvw_version") == 0) { test_case_current = 1; test_case_total = 1; return case_kc_wvw_version(); }
    if (strcmp(name, "kc_wvw_options") == 0) { test_case_current = 1; test_case_total = 1; return case_kc_wvw_options(); }
    if (strcmp(name, "kc_wvw_context") == 0) { test_case_current = 1; test_case_total = 1; return case_kc_wvw_context(); }
    if (strcmp(name, "kc_wvw_navigation") == 0) { test_case_current = 1; test_case_total = 1; return case_kc_wvw_navigation(); }
    if (strcmp(name, "kc_wvw_bridge") == 0) { test_case_current = 1; test_case_total = 1; return case_kc_wvw_bridge(); }
    if (strcmp(name, "kc_wvw_visibility") == 0) { test_case_current = 1; test_case_total = 1; return case_kc_wvw_visibility(); }
    if (strcmp(name, "kc_wvw_window_state") == 0) { test_case_current = 1; test_case_total = 1; return case_kc_wvw_window_state(); }
    if (strcmp(name, "kc_wvw_title") == 0) { test_case_current = 1; test_case_total = 1; return case_kc_wvw_title(); }
    if (strcmp(name, "kc_wvw_size") == 0) { test_case_current = 1; test_case_total = 1; return case_kc_wvw_size(); }
    fprintf(stderr, "unknown test case: %s\n", name);
    return 2;
}

/**
 * Runs one libwvw public API test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 or 2 on failure.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    return dispatch_case(argv[1]);
}
