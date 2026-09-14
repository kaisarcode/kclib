/**
 * test.c - liblibr public API tests.
 * Summary: Tests each public liblibr function through one CTest case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "liblibr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Public API function under test.
 * @param detail Behavior verified by the case.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

typedef int (*case_fn)(void);

/**
 * Runs one test case with counter tracking.
 * @param rc Destination accumulator.
 * @param fn Test case function.
 * @return None.
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
 * Opens one context for tests.
 * @param out Destination context pointer.
 * @return 0 on success, 1 on failure.
 */
static int open_context(void **out) {
    kc_libr_options_t opts = kc_libr_options_default();
    if (!opts) return 1;
    if (kc_libr_open(out, opts) != KC_LIBR_OK) {
        kc_libr_options_free(opts);
        return 1;
    }
    kc_libr_options_free(opts);
    return 0;
}

/**
 * Tests kc_libr_options_default.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_libr_options_default(void) {
    const char *name = "kc_libr_options_default";
    const char *detail = "default options initialize correctly";
    kc_libr_options_t opts;

    opts = kc_libr_options_default();
    int fail = expect_true("default options returns non-NULL", opts != NULL);
    kc_libr_options_free(opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_libr_options_set.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_libr_options_set(void) {
    const char *name = "kc_libr_options_set";
    const char *detail = "options set works correctly";
    kc_libr_options_t opts = kc_libr_options_default();
    int fail = 0;

    if (!opts) return 1;

    fail += expect_int("options_set accepts valid key", KC_LIBR_OK,
        kc_libr_options_set(opts, "param", "test_value"));
    fail += expect_int("options_set rejects unknown key", KC_LIBR_ERROR,
        kc_libr_options_set(opts, "unknown", "value"));
    fail += expect_int("options_set accepts NULL value", KC_LIBR_OK,
        kc_libr_options_set(opts, "param", NULL));

    kc_libr_options_free(opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_libr_options_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_libr_options_free(void) {
    const char *name = "kc_libr_options_free";
    const char *detail = "options free clears resources";
    kc_libr_options_t opts;
    int fail = 0;

    fail += expect_true("options_free accepts NULL", 1);
    kc_libr_options_free(NULL);

    opts = kc_libr_options_default();
    fail += expect_true("default options non-NULL", opts != NULL);
    kc_libr_options_free(opts);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_libr_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_libr_version(void) {
    const char *name = "kc_libr_version";
    const char *detail = "version returns build timestamp";
    int fail = expect_true("version returns build value", kc_libr_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_libr_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_libr_open(void) {
    const char *name = "kc_libr_open";
    const char *detail = "open validates and allocates context";
    kc_libr_options_t opts;
    void *ctx = NULL;
    int fail = 0;

    fail += expect_int("open rejects NULL out", KC_LIBR_ERROR,
        kc_libr_open(NULL, NULL));
    fail += expect_int("open rejects NULL opts", KC_LIBR_ERROR,
        kc_libr_open(&ctx, NULL));
    fail += expect_true("open error clears output", ctx == NULL);

    opts = kc_libr_options_default();
    fail += expect_int("open creates context", KC_LIBR_OK,
        kc_libr_open(&ctx, opts));
    fail += expect_true("open sets output", ctx != NULL);
    kc_libr_options_free(opts);

    fail += expect_int("opened context still executes", KC_LIBR_OK,
        kc_libr_exec(ctx, "input"));
    fail += expect_int("close opened context", KC_LIBR_OK, kc_libr_close(ctx));

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_libr_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_libr_close(void) {
    const char *name = "kc_libr_close";
    const char *detail = "close releases context";
    void *ctx;
    int fail = 0;

    fail += expect_int("close accepts NULL", KC_LIBR_OK, kc_libr_close(NULL));
    if (open_context(&ctx) != 0) return 1;
    fail += expect_int("close releases context", KC_LIBR_OK, kc_libr_close(ctx));

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_libr_exec.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_libr_exec(void) {
    const char *name = "kc_libr_exec";
    const char *detail = "exec validates and processes input";
    void *ctx;
    int fail = 0;

    fail += expect_int("exec rejects NULL ctx", KC_LIBR_ERROR,
        kc_libr_exec(NULL, "input"));
    if (open_context(&ctx) != 0) return 1;
    fail += expect_int("exec rejects NULL input", KC_LIBR_ERROR,
        kc_libr_exec(ctx, NULL));
    fail += expect_int("exec accepts empty input", KC_LIBR_OK,
        kc_libr_exec(ctx, ""));
    fail += expect_int("exec accepts normal input", KC_LIBR_OK,
        kc_libr_exec(ctx, "input"));
    fail += expect_int("exec remains usable after stop", KC_LIBR_OK,
        kc_libr_stop(ctx));
    fail += expect_int("exec after stop returns OK", KC_LIBR_OK,
        kc_libr_exec(ctx, "post-stop"));
    fail += expect_int("close context", KC_LIBR_OK, kc_libr_close(ctx));

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_libr_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_libr_stop(void) {
    const char *name = "kc_libr_stop";
    const char *detail = "stop sets flag on context";
    void *ctx;
    void *other;
    int fail = 0;

    fail += expect_int("stop rejects NULL", KC_LIBR_ERROR, kc_libr_stop(NULL));
    if (open_context(&ctx) != 0) return 1;
    if (open_context(&other) != 0) {
        kc_libr_close(ctx);
        return 1;
    }
    fail += expect_int("stop context succeeds", KC_LIBR_OK, kc_libr_stop(ctx));
    fail += expect_int("stop is idempotent", KC_LIBR_OK, kc_libr_stop(ctx));
    fail += expect_int("other context still executes", KC_LIBR_OK,
        kc_libr_exec(other, "input"));
    fail += expect_int("close stopped context", KC_LIBR_OK, kc_libr_close(ctx));
    fail += expect_int("close other context", KC_LIBR_OK, kc_libr_close(other));

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_libr_get_error.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_libr_get_error(void) {
    const char *name = "kc_libr_get_error";
    const char *detail = "get_error returns NULL when no error";
    void *ctx;
    int fail = 0;

    fail += expect_true("get_error returns NULL for NULL ctx",
        kc_libr_get_error(NULL) == NULL);
    if (open_context(&ctx) != 0) return 1;
    fail += expect_true("get_error returns NULL initially",
        kc_libr_get_error(ctx) == NULL);
    kc_libr_close(ctx);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, 1 on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 9;
    test_case_current = 0;
    run_case(&rc, case_kc_libr_options_default);
    run_case(&rc, case_kc_libr_options_set);
    run_case(&rc, case_kc_libr_options_free);
    run_case(&rc, case_kc_libr_version);
    run_case(&rc, case_kc_libr_open);
    run_case(&rc, case_kc_libr_close);
    run_case(&rc, case_kc_libr_exec);
    run_case(&rc, case_kc_libr_stop);
    run_case(&rc, case_kc_libr_get_error);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one liblibr public API test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 or 2 on failure.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_libr_options_default") == 0) return case_kc_libr_options_default();
    if (strcmp(argv[1], "kc_libr_options_set") == 0) return case_kc_libr_options_set();
    if (strcmp(argv[1], "kc_libr_options_free") == 0) return case_kc_libr_options_free();
    if (strcmp(argv[1], "kc_libr_version") == 0) return case_kc_libr_version();
    if (strcmp(argv[1], "kc_libr_open") == 0) return case_kc_libr_open();
    if (strcmp(argv[1], "kc_libr_close") == 0) return case_kc_libr_close();
    if (strcmp(argv[1], "kc_libr_exec") == 0) return case_kc_libr_exec();
    if (strcmp(argv[1], "kc_libr_stop") == 0) return case_kc_libr_stop();
    if (strcmp(argv[1], "kc_libr_get_error") == 0) return case_kc_libr_get_error();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
