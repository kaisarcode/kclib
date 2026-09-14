/**
 * test.c - libhnsw public API contract tests.
 * Summary: Validates each exported libhnsw function through one dedicated test case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libhnsw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Test case description.
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
 * Verifies an integer result.
 * @param name Check name.
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
 * Verifies a true condition.
 * @param name Check name.
 * @param condition Condition expected to be true.
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
 * @param name Check name.
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
 * Tests kc_hnsw_options_default.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_hnsw_options_default(void) {
    const char *name = "kc_hnsw_options_default";
    const char *detail = "initializes correctly";
    kc_hnsw_options_t opts;
    int fail = 0;

    opts = kc_hnsw_options_default();
    fail = 0;
    fail += expect_int("default metric", KC_HNSW_METRIC_COSINE, opts.metric);
    fail += expect_int("default m", 16, opts.m);
    fail += expect_int("default ef_construction", 64, opts.ef_construction);
    fail += expect_int("default ef_search", 64, opts.ef_search);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_hnsw_options_load_env.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_hnsw_options_load_env(void) {
    const char *name = "kc_hnsw_options_load_env";
    const char *detail = "loads from environment";
    kc_hnsw_options_t opts;
    int fail = 0;

    opts = kc_hnsw_options_default();
    fail = 0;
    fail += expect_int("set env dim", 0, set_env_value("KC_HNSW_DIM", "5"));
    fail += expect_int("set env metric", 0, set_env_value("KC_HNSW_METRIC", "l2"));
    fail += expect_int("set env m", 0, set_env_value("KC_HNSW_M", "32"));
    fail += expect_int("set env constr", 0, set_env_value("KC_HNSW_EF_CONSTRUCTION", "128"));
    fail += expect_int("set env search", 0, set_env_value("KC_HNSW_EF_SEARCH", "256"));

    kc_hnsw_options_load_env(&opts);

    fail += expect_int("loaded dim", 5, (int)opts.dimension);
    fail += expect_int("loaded metric", KC_HNSW_METRIC_L2, opts.metric);
    fail += expect_int("loaded m", 32, opts.m);
    fail += expect_int("loaded ef_construction", 128, opts.ef_construction);
    fail += expect_int("loaded ef_search", 256, opts.ef_search);

    kc_hnsw_options_free(&opts);

    set_env_value("KC_HNSW_DIM", NULL);
    set_env_value("KC_HNSW_METRIC", NULL);
    set_env_value("KC_HNSW_M", NULL);
    set_env_value("KC_HNSW_EF_CONSTRUCTION", NULL);
    set_env_value("KC_HNSW_EF_SEARCH", NULL);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_hnsw_options_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_hnsw_options_free(void) {
    const char *name = "kc_hnsw_options_free";
    const char *detail = "clears resources";
    kc_hnsw_options_t opts;

    opts = kc_hnsw_options_default();
    kc_hnsw_options_free(&opts);
    kc_hnsw_options_free(NULL);
    case_result(0, name, detail);
    return 0;
}

/**
 * Tests kc_hnsw_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_hnsw_open(void) {
    const char *name = "kc_hnsw_open";
    const char *detail = "allocates context";
    kc_hnsw_options_t opts;
    int fail = 0;

    opts = kc_hnsw_options_default();
    opts.dimension = 3;
    fail = 0;
    fail += expect_true("open NULL returns NULL", kc_hnsw_open(NULL) == NULL);
    fail += expect_int("open valid context", KC_HNSW_OK, kc_hnsw_open(&opts) != NULL ? KC_HNSW_OK : KC_HNSW_EINVAL);
    kc_hnsw_t *ctx = kc_hnsw_open(&opts);
    fail += expect_true("open sets context", ctx != NULL);
    kc_hnsw_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_hnsw_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_hnsw_close(void) {
    const char *name = "kc_hnsw_close";
    const char *detail = "releases context";
    kc_hnsw_options_t opts;
    int fail = 0;

    opts = kc_hnsw_options_default();
    opts.dimension = 3;
    kc_hnsw_t *ctx = kc_hnsw_open(&opts);
    fail = 0;
    kc_hnsw_close(NULL);
    fail += expect_true("close NULL doesn't crash", 1);
    fail += expect_true("close context doesn't crash", 1);
    kc_hnsw_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_hnsw_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_hnsw_stop(void) {
    const char *name = "kc_hnsw_stop";
    const char *detail = "sets flag";
    kc_hnsw_options_t opts;
    int fail = 0;

    opts = kc_hnsw_options_default();
    opts.dimension = 3;
    kc_hnsw_t *ctx = kc_hnsw_open(&opts);
    fail = 0;
    fail += expect_int("stop NULL returns EINVAL", KC_HNSW_EINVAL, kc_hnsw_stop(NULL));
    fail += expect_int("stop_requested NULL", 0, kc_hnsw_stop_requested(NULL));
    fail += expect_true("open context", ctx != NULL);
    fail += expect_int("stop_requested initial", 0, kc_hnsw_stop_requested(ctx));
    fail += expect_int("stop context", KC_HNSW_OK, kc_hnsw_stop(ctx));
    fail += expect_int("stop_requested after stop", 1, kc_hnsw_stop_requested(ctx));
    kc_hnsw_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests that two contexts can coexist and stop is isolated.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_hnsw_multictx_stop(void) {
    const char *name = "kc_hnsw_multictx_stop";
    const char *detail = "two contexts coexist independently";
    kc_hnsw_options_t opts;
    int fail = 0;

    opts = kc_hnsw_options_default();
    opts.dimension = 3;
    kc_hnsw_t *a = kc_hnsw_open(&opts);
    kc_hnsw_t *b = kc_hnsw_open(&opts);
    fail = 0;

    fail += expect_true("open a", a != NULL);
    fail += expect_true("open b", b != NULL);

    fail += expect_int("stop NULL", KC_HNSW_EINVAL, kc_hnsw_stop(NULL));
    fail += expect_int("stop a", KC_HNSW_OK, kc_hnsw_stop(a));
    fail += expect_int("stop b", KC_HNSW_OK, kc_hnsw_stop(b));
    fail += expect_int("stop a again", KC_HNSW_OK, kc_hnsw_stop(a));

    kc_hnsw_close(a);
    kc_hnsw_close(b);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_hnsw_reserve.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_hnsw_reserve(void) {
    const char *name = "kc_hnsw_reserve";
    const char *detail = "preallocates capacity";
    kc_hnsw_options_t opts;
    int fail = 0;

    opts = kc_hnsw_options_default();
    opts.dimension = 3;
    kc_hnsw_t *ctx = kc_hnsw_open(&opts);
    fail = 0;
    fail += expect_true("open context", ctx != NULL);
    fail += expect_int("reserve 10", KC_HNSW_OK, kc_hnsw_reserve(ctx, 10));
    kc_hnsw_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_hnsw_add.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_hnsw_add(void) {
    const char *name = "kc_hnsw_add";
    const char *detail = "inserts vectors";
    kc_hnsw_options_t opts;
    int fail = 0;

    opts = kc_hnsw_options_default();
    opts.dimension = 3;
    opts.metric = KC_HNSW_METRIC_L2;

    kc_hnsw_t *ctx = kc_hnsw_open(&opts);
    fail = 0;
    fail += expect_true("open context", ctx != NULL);

    float v_red[3] = {1.0f, 0.0f, 0.0f};
    float v_green[3] = {0.0f, 1.0f, 0.0f};
    float v_blue[3] = {0.0f, 0.0f, 1.0f};
    float v_pink[3] = {0.8f, 0.2f, 0.0f};

    fail += expect_int("add red", KC_HNSW_OK, kc_hnsw_add(ctx, "red", v_red));
    fail += expect_int("add green", KC_HNSW_OK, kc_hnsw_add(ctx, "green", v_green));
    fail += expect_int("add blue", KC_HNSW_OK, kc_hnsw_add(ctx, "blue", v_blue));
    fail += expect_int("add pink", KC_HNSW_OK, kc_hnsw_add(ctx, "pink", v_pink));
    fail += expect_int("count 4", 4, (int)kc_hnsw_count(ctx));

    kc_hnsw_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_hnsw_build.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_hnsw_build(void) {
    const char *name = "kc_hnsw_build";
    const char *detail = "constructs graph";
    kc_hnsw_options_t opts;
    int fail = 0;

    opts = kc_hnsw_options_default();
    opts.dimension = 3;
    kc_hnsw_t *ctx = kc_hnsw_open(&opts);
    fail = 0;
    fail += expect_true("open context", ctx != NULL);

    float v_red[3] = {1.0f, 0.0f, 0.0f};
    float v_green[3] = {0.0f, 1.0f, 0.0f};
    float v_blue[3] = {0.0f, 0.0f, 1.0f};

    fail += expect_int("add red", KC_HNSW_OK, kc_hnsw_add(ctx, "red", v_red));
    fail += expect_int("add green", KC_HNSW_OK, kc_hnsw_add(ctx, "green", v_green));
    fail += expect_int("add blue", KC_HNSW_OK, kc_hnsw_add(ctx, "blue", v_blue));
    fail += expect_int("build", KC_HNSW_OK, kc_hnsw_build(ctx));

    kc_hnsw_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_hnsw_search.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_hnsw_search(void) {
    const char *name = "kc_hnsw_search";
    const char *detail = "finds nearest neighbors";
    kc_hnsw_options_t opts;
    int fail = 0;

    opts = kc_hnsw_options_default();
    opts.dimension = 3;
    opts.metric = KC_HNSW_METRIC_L2;

    kc_hnsw_t *ctx = kc_hnsw_open(&opts);
    fail = 0;
    fail += expect_true("open context", ctx != NULL);

    float v_red[3] = {1.0f, 0.0f, 0.0f};
    float v_green[3] = {0.0f, 1.0f, 0.0f};
    float v_blue[3] = {0.0f, 0.0f, 1.0f};
    float v_pink[3] = {0.8f, 0.2f, 0.0f};
    float query[3] = {0.7f, 0.1f, 0.0f};
    kc_hnsw_result_t results[4];

    fail += expect_int("add red", KC_HNSW_OK, kc_hnsw_add(ctx, "red", v_red));
    fail += expect_int("add green", KC_HNSW_OK, kc_hnsw_add(ctx, "green", v_green));
    fail += expect_int("add blue", KC_HNSW_OK, kc_hnsw_add(ctx, "blue", v_blue));
    fail += expect_int("add pink", KC_HNSW_OK, kc_hnsw_add(ctx, "pink", v_pink));
    fail += expect_int("build", KC_HNSW_OK, kc_hnsw_build(ctx));

    int search_rc = kc_hnsw_search(ctx, query, 2, 9999.0, results);
    fail += expect_int("search count", 2, search_rc);
    if (search_rc == 2) {
        fail += expect_string("first result", "pink", results[0].id);
        fail += expect_string("second result", "red", results[1].id);
    }

    kc_hnsw_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_hnsw_dimension.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_hnsw_dimension(void) {
    const char *name = "kc_hnsw_dimension";
    const char *detail = "returns configured dimension";
    kc_hnsw_options_t opts;
    int fail = 0;

    opts = kc_hnsw_options_default();
    opts.dimension = 3;
    kc_hnsw_t *ctx = kc_hnsw_open(&opts);
    fail = 0;
    fail += expect_int("dimension 3", 3, (int)kc_hnsw_dimension(ctx));
    kc_hnsw_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_hnsw_metric.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_hnsw_metric(void) {
    const char *name = "kc_hnsw_metric";
    const char *detail = "returns configured metric";
    kc_hnsw_options_t opts;
    int fail = 0;

    opts = kc_hnsw_options_default();
    opts.dimension = 3;
    opts.metric = KC_HNSW_METRIC_L2;
    kc_hnsw_t *ctx = kc_hnsw_open(&opts);
    fail = 0;
    fail += expect_int("metric L2", KC_HNSW_METRIC_L2, kc_hnsw_metric(ctx));
    kc_hnsw_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_hnsw_count.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_hnsw_count(void) {
    const char *name = "kc_hnsw_count";
    const char *detail = "returns vector count";
    kc_hnsw_options_t opts;
    int fail = 0;

    opts = kc_hnsw_options_default();
    opts.dimension = 3;
    kc_hnsw_t *ctx = kc_hnsw_open(&opts);
    fail = 0;
    fail += expect_int("count 0", 0, (int)kc_hnsw_count(ctx));
    float v[3] = {1.0f, 0.0f, 0.0f};
    fail += expect_int("add red", KC_HNSW_OK, kc_hnsw_add(ctx, "red", v));
    fail += expect_int("count 1", 1, (int)kc_hnsw_count(ctx));
    kc_hnsw_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_hnsw_metric_from_string.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_hnsw_metric_from_string(void) {
    const char *name = "kc_hnsw_metric_from_string";
    const char *detail = "parses names";
    int fail = 0;
    fail += expect_int("cosine", KC_HNSW_METRIC_COSINE, kc_hnsw_metric_from_string("cosine"));
    fail += expect_int("inner", KC_HNSW_METRIC_INNER_PRODUCT, kc_hnsw_metric_from_string("inner"));
    fail += expect_int("l2", KC_HNSW_METRIC_L2, kc_hnsw_metric_from_string("l2"));
    fail += expect_int("invalid", 0, kc_hnsw_metric_from_string("invalid"));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_hnsw_metric_to_string.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_hnsw_metric_to_string(void) {
    const char *name = "kc_hnsw_metric_to_string";
    const char *detail = "returns names";
    int fail = 0;
    fail += expect_string("cosine", "cosine", kc_hnsw_metric_to_string(KC_HNSW_METRIC_COSINE));
    fail += expect_string("inner", "inner", kc_hnsw_metric_to_string(KC_HNSW_METRIC_INNER_PRODUCT));
    fail += expect_string("l2", "l2", kc_hnsw_metric_to_string(KC_HNSW_METRIC_L2));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_hnsw_strerror.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_hnsw_strerror(void) {
    const char *name = "kc_hnsw_strerror";
    const char *detail = "returns messages";
    int fail = 0;
    fail += expect_string("ok", "ok", kc_hnsw_strerror(KC_HNSW_OK));
    fail += expect_string("einval", "invalid argument", kc_hnsw_strerror(KC_HNSW_EINVAL));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_hnsw_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_hnsw_version(void) {
    const char *name = "kc_hnsw_version";
    const char *detail = "returns non-zero build timestamp";
    int fail = expect_true("version set", kc_hnsw_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_hnsw_stop_requested.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_hnsw_stop_requested(void) {
    const char *name = "kc_hnsw_stop_requested";
    const char *detail = "reports flag";
    kc_hnsw_options_t opts;
    int fail = 0;

    opts = kc_hnsw_options_default();
    opts.dimension = 3;
    kc_hnsw_t *ctx = kc_hnsw_open(&opts);
    fail = 0;
    fail += expect_int("stop_requested initial", 0, kc_hnsw_stop_requested(ctx));
    fail += expect_int("stop context", KC_HNSW_OK, kc_hnsw_stop(ctx));
    fail += expect_int("stop_requested after stop", 1, kc_hnsw_stop_requested(ctx));
    kc_hnsw_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_hnsw_close with NULL.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_hnsw_close_null(void) {
    const char *name = "kc_hnsw_close_null";
    const char *detail = "accepts NULL";
    int fail = 0;
    kc_hnsw_close(NULL);
    fail += expect_true("close NULL doesn't crash", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_hnsw_close after open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_hnsw_close_open(void) {
    const char *name = "kc_hnsw_close_open";
    const char *detail = "releases opened context";
    kc_hnsw_options_t opts;
    int fail = 0;

    opts = kc_hnsw_options_default();
    opts.dimension = 3;
    kc_hnsw_t *ctx = kc_hnsw_open(&opts);
    fail = 0;
    fail += expect_true("open context", ctx != NULL);
    kc_hnsw_close(ctx);
    fail += expect_true("close context doesn't crash", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 21;
    test_case_current = 0;
    run_case(&rc, case_kc_hnsw_options_default);
    run_case(&rc, case_kc_hnsw_options_load_env);
    run_case(&rc, case_kc_hnsw_options_free);
    run_case(&rc, case_kc_hnsw_open);
    run_case(&rc, case_kc_hnsw_close);
    run_case(&rc, case_kc_hnsw_stop);
    run_case(&rc, case_kc_hnsw_stop_requested);
    run_case(&rc, case_kc_hnsw_multictx_stop);
    run_case(&rc, case_kc_hnsw_close_null);
    run_case(&rc, case_kc_hnsw_close_open);
    run_case(&rc, case_kc_hnsw_reserve);
    run_case(&rc, case_kc_hnsw_add);
    run_case(&rc, case_kc_hnsw_build);
    run_case(&rc, case_kc_hnsw_search);
    run_case(&rc, case_kc_hnsw_dimension);
    run_case(&rc, case_kc_hnsw_metric);
    run_case(&rc, case_kc_hnsw_count);
    run_case(&rc, case_kc_hnsw_metric_from_string);
    run_case(&rc, case_kc_hnsw_metric_to_string);
    run_case(&rc, case_kc_hnsw_strerror);
    run_case(&rc, case_kc_hnsw_version);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one named test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <case>\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_hnsw_options_default") == 0) return case_kc_hnsw_options_default();
    if (strcmp(argv[1], "kc_hnsw_options_load_env") == 0) return case_kc_hnsw_options_load_env();
    if (strcmp(argv[1], "kc_hnsw_options_free") == 0) return case_kc_hnsw_options_free();
    if (strcmp(argv[1], "kc_hnsw_open") == 0) return case_kc_hnsw_open();
    if (strcmp(argv[1], "kc_hnsw_close") == 0) return case_kc_hnsw_close();
    if (strcmp(argv[1], "kc_hnsw_stop") == 0) return case_kc_hnsw_stop();
    if (strcmp(argv[1], "kc_hnsw_stop_requested") == 0) return case_kc_hnsw_stop_requested();
    if (strcmp(argv[1], "kc_hnsw_close_null") == 0) return case_kc_hnsw_close_null();
    if (strcmp(argv[1], "kc_hnsw_close_open") == 0) return case_kc_hnsw_close_open();
    if (strcmp(argv[1], "kc_hnsw_reserve") == 0) return case_kc_hnsw_reserve();
    if (strcmp(argv[1], "kc_hnsw_add") == 0) return case_kc_hnsw_add();
    if (strcmp(argv[1], "kc_hnsw_build") == 0) return case_kc_hnsw_build();
    if (strcmp(argv[1], "kc_hnsw_search") == 0) return case_kc_hnsw_search();
    if (strcmp(argv[1], "kc_hnsw_dimension") == 0) return case_kc_hnsw_dimension();
    if (strcmp(argv[1], "kc_hnsw_metric") == 0) return case_kc_hnsw_metric();
    if (strcmp(argv[1], "kc_hnsw_count") == 0) return case_kc_hnsw_count();
    if (strcmp(argv[1], "kc_hnsw_metric_from_string") == 0) return case_kc_hnsw_metric_from_string();
    if (strcmp(argv[1], "kc_hnsw_metric_to_string") == 0) return case_kc_hnsw_metric_to_string();
    if (strcmp(argv[1], "kc_hnsw_strerror") == 0) return case_kc_hnsw_strerror();
    if (strcmp(argv[1], "kc_hnsw_version") == 0) return case_kc_hnsw_version();
    fprintf(stderr, "unknown case: %s\n", argv[1]);
    return 2;
}
