/**
 * test.c - liblng public API tests.
 * Summary: Tests each public liblng function through one C case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "liblng.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Test case description.
 * @param detail Additional detail string.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

/**
 * Runs one test case with counter tracking.
 * @param rc Destination accumulator.
 * @param fn Test case function.
 * @return None.
 */
static void run_case(int *rc, int (*fn)(void)) {
    test_case_current++;
    *rc += fn();
}

/**
 * Sets or clears one environment variable.
 * @param name Variable name.
 * @param value Variable value, or NULL to clear.
 * @return 0 on success, 1 on failure.
 */
static int set_env_value(const char *name, const char *value) {
#ifdef _WIN32
    return _putenv_s(name, value != NULL ? value : "") == 0 ? 0 : 1;
#else
    if (value == NULL) return unsetenv(name) == 0 ? 0 : 1;
    return setenv(name, value, 1) == 0 ? 0 : 1;
#endif
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
 * Verifies a string result.
 * @param name Check description.
 * @param expected Expected string.
 * @param actual Actual string.
 * @return 0 on success, 1 on failure.
 */
static int expect_string(const char *name, const char *expected, const char *actual) {
    if (actual == NULL || strcmp(expected, actual) != 0) {
        printf("[FAIL] %s: expected '%s', got '%s'\n", name, expected,
            actual != NULL ? actual : "NULL");
        return 1;
    }
    return 0;
}

/**
 * Tests kc_lng_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_lng_version(void) {
    const char *name = "kc_lng_version";
    const char *detail = "returns non-zero build timestamp";
    uint64_t v = kc_lng_version();
    int fail = expect_true("version non-zero", v != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_lng_options_default.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_lng_options_default(void) {
    const char *name = "kc_lng_options_default";
    const char *detail = "returns default threshold and limit";
    kc_lng_options_t opts = kc_lng_options_default();
    int fail = expect_true("threshold is 0.001",
        opts.threshold >= 0.001 - 1e-9 && opts.threshold <= 0.001 + 1e-9);
    fail += expect_int("limit is 1", 1, opts.limit);
    kc_lng_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_lng_options_load_env.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_lng_options_load_env(void) {
    const char *name = "kc_lng_options_load_env";
    const char *detail = "loads environment overrides";
    kc_lng_options_t opts = kc_lng_options_default();
    int fail = expect_int("set threshold env", 0,
        set_env_value("KC_LNG_THRESHOLD", "0.25"));
    fail += expect_int("set limit env", 0,
        set_env_value("KC_LNG_LIMIT", "3"));
    kc_lng_options_load_env(&opts);
    fail += expect_true("env threshold applied",
        opts.threshold >= 0.25 - 1e-9 && opts.threshold <= 0.25 + 1e-9);
    fail += expect_int("env limit applied", 3, opts.limit);
    fail += expect_int("set bad threshold env", 0,
        set_env_value("KC_LNG_THRESHOLD", "bad"));
    kc_lng_options_load_env(&opts);
    fail += expect_true("bad threshold ignored",
        opts.threshold >= 0.25 - 1e-9 && opts.threshold <= 0.25 + 1e-9);
    kc_lng_options_load_env(NULL);
    set_env_value("KC_LNG_THRESHOLD", NULL);
    set_env_value("KC_LNG_LIMIT", NULL);
    kc_lng_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_lng_options_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_lng_options_free(void) {
    const char *name = "kc_lng_options_free";
    const char *detail = "clears resources";
    kc_lng_options_t opts = kc_lng_options_default();
    kc_lng_options_free(&opts);
    kc_lng_options_free(NULL);
    int fail = 0;
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_lng_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_lng_open(void) {
    const char *name = "kc_lng_open";
    const char *detail = "validates arguments and allocates context";
    kc_lng_options_t opts = kc_lng_options_default();
    kc_lng_t *ctx = NULL;
    int fail = expect_int("open NULL out returns ERROR",
        KC_LNG_ERROR, kc_lng_open(NULL, &opts));
    fail += expect_int("open NULL opts returns ERROR",
        KC_LNG_ERROR, kc_lng_open(&ctx, NULL));
    fail += expect_int("open returns OK", KC_LNG_OK, kc_lng_open(&ctx, &opts));
    fail += expect_true("open creates context", ctx != NULL);
    kc_lng_close(ctx);
    kc_lng_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_lng_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_lng_close(void) {
    const char *name = "kc_lng_close";
    const char *detail = "releases context";
    kc_lng_options_t opts = kc_lng_options_default();
    kc_lng_t *ctx = NULL;
    int fail = expect_int("close NULL returns OK",
        KC_LNG_OK, kc_lng_close(NULL));
    fail += expect_int("open returns OK", KC_LNG_OK, kc_lng_open(&ctx, &opts));
    fail += expect_int("close returns OK", KC_LNG_OK, kc_lng_close(ctx));
    kc_lng_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_lng_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_lng_stop(void) {
    const char *name = "kc_lng_stop";
    const char *detail = "sets stop flag on context";
    kc_lng_options_t opts = kc_lng_options_default();
    kc_lng_t *ctx = NULL;
    int fail = expect_int("stop NULL returns ERROR",
        KC_LNG_ERROR, kc_lng_stop(NULL));
    fail += expect_int("open returns OK", KC_LNG_OK, kc_lng_open(&ctx, &opts));
    fail += expect_int("stop requested starts false", 0,
        kc_lng_stop_requested(ctx));
    fail += expect_int("stop returns OK", KC_LNG_OK, kc_lng_stop(ctx));
    fail += expect_int("stop is idempotent", KC_LNG_OK, kc_lng_stop(ctx));
    fail += expect_int("stop requested becomes true", 1,
        kc_lng_stop_requested(ctx));
    fail += expect_int("stop_requested NULL returns 0", 0,
        kc_lng_stop_requested(NULL));
    kc_lng_close(ctx);
    kc_lng_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_lng_init.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_lng_init(void) {
    const char *name = "kc_lng_init";
    const char *detail = "initializes profiles once";
    int fail = expect_int("init first returns OK", KC_LNG_OK, kc_lng_init());
    fail += expect_int("init second returns OK", KC_LNG_OK, kc_lng_init());
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_lng_detect.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_lng_detect(void) {
    const char *name = "kc_lng_detect";
    const char *detail = "detects best matching language";
    int fail = expect_true("detect NULL returns NULL", kc_lng_detect(NULL) == NULL);
    fail += expect_string("detect english", "en",
        kc_lng_detect("hello world this is english text"));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_lng_detect_top.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_lng_detect_top(void) {
    const char *name = "kc_lng_detect_top";
    const char *detail = "returns ranked results";
    kc_lng_result_t out[3];
    int fail = 0;
    int n;

    n = kc_lng_detect_top(NULL, out, 1, 0.001);
    fail += expect_int("detect_top NULL text returns 0", 0, n);
    n = kc_lng_detect_top("hello", NULL, 1, 0.001);
    fail += expect_int("detect_top NULL out returns 0", 0, n);
    n = kc_lng_detect_top("hello", out, 0, 0.001);
    fail += expect_int("detect_top zero max returns 0", 0, n);
    n = kc_lng_detect_top("hello world this is english text", out, 3, 0.001);
    fail += expect_true("detect_top writes results", n >= 1);
    fail += expect_string("detect_top first language", "en", out[0].code);
    fail += expect_true("detect_top score positive", out[0].score > 0.0);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_lng_detect_ctx.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_lng_detect_ctx(void) {
    const char *name = "kc_lng_detect_ctx";
    const char *detail = "observes stop state";
    kc_lng_options_t opts = kc_lng_options_default();
    kc_lng_t *ctx = NULL;
    kc_lng_t *second = NULL;
    kc_lng_result_t out[2];
    int fail = expect_int("detect_ctx NULL ctx returns 0", 0,
        kc_lng_detect_ctx(NULL, "hello", out, 1, 0.001));
    fail += expect_int("open returns OK", KC_LNG_OK, kc_lng_open(&ctx, &opts));
    fail += expect_int("open second returns OK", KC_LNG_OK,
        kc_lng_open(&second, &opts));
    fail += expect_int("detect_ctx NULL text returns 0", 0,
        kc_lng_detect_ctx(second, NULL, out, 1, 0.001));
    fail += expect_int("detect_ctx NULL out returns 0", 0,
        kc_lng_detect_ctx(second, "hello", NULL, 1, 0.001));
    fail += expect_int("detect_ctx zero max returns 0", 0,
        kc_lng_detect_ctx(second, "hello", out, 0, 0.001));
    fail += expect_int("stop first returns OK", KC_LNG_OK, kc_lng_stop(ctx));
    fail += expect_int("stopped context returns none", 0,
        kc_lng_detect_ctx(ctx, "hello world", out, 1, 0.001));
    fail += expect_int("second context still detects", 1,
        kc_lng_detect_ctx(second, "hello world this is english text", out, 1, 0.001));
    fail += expect_string("second context language", "en", out[0].code);
    kc_lng_close(second);
    kc_lng_close(ctx);
    kc_lng_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 11;
    test_case_current = 0;
    run_case(&rc, case_kc_lng_version);
    run_case(&rc, case_kc_lng_options_default);
    run_case(&rc, case_kc_lng_options_load_env);
    run_case(&rc, case_kc_lng_options_free);
    run_case(&rc, case_kc_lng_open);
    run_case(&rc, case_kc_lng_close);
    run_case(&rc, case_kc_lng_stop);
    run_case(&rc, case_kc_lng_init);
    run_case(&rc, case_kc_lng_detect);
    run_case(&rc, case_kc_lng_detect_top);
    run_case(&rc, case_kc_lng_detect_ctx);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one named test case.
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
    if (strcmp(argv[1], "kc_lng_version") == 0) return case_kc_lng_version();
    if (strcmp(argv[1], "kc_lng_options_default") == 0) return case_kc_lng_options_default();
    if (strcmp(argv[1], "kc_lng_options_load_env") == 0) return case_kc_lng_options_load_env();
    if (strcmp(argv[1], "kc_lng_options_free") == 0) return case_kc_lng_options_free();
    if (strcmp(argv[1], "kc_lng_open") == 0) return case_kc_lng_open();
    if (strcmp(argv[1], "kc_lng_close") == 0) return case_kc_lng_close();
    if (strcmp(argv[1], "kc_lng_stop") == 0) return case_kc_lng_stop();
    if (strcmp(argv[1], "kc_lng_init") == 0) return case_kc_lng_init();
    if (strcmp(argv[1], "kc_lng_detect") == 0) return case_kc_lng_detect();
    if (strcmp(argv[1], "kc_lng_detect_top") == 0) return case_kc_lng_detect_top();
    if (strcmp(argv[1], "kc_lng_detect_ctx") == 0) return case_kc_lng_detect_ctx();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
