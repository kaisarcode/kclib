/**
 * test.c - libemb public API contract tests.
 * Summary: Validates each exported libemb function through one dedicated test case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libemb.h"

#include <math.h>
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
 * Opens one embedding context for a test case.
 * @param out Destination for the context pointer.
 * @return 0 on success, 1 on failure.
 */
static int open_context(kc_emb_t **out) {
    kc_emb_options_t opts;

    opts = kc_emb_options_default();
    if (kc_emb_open(out, &opts) != KC_EMB_OK) {
        kc_emb_options_free(&opts);
        return 1;
    }
    kc_emb_options_free(&opts);
    return 0;
}

/**
 * Verifies all vector values are finite and not all zero.
 * @param name Check description.
 * @param vec Vector data.
 * @param dim Vector dimension.
 * @return 0 on success, 1 on failure.
 */
static int expect_vector_valid(const char *name, const float *vec, int dim) {
    int i;
    int nonzero;

    nonzero = 0;
    for (i = 0; i < dim; i++) {
        if (!isfinite(vec[i])) {
            printf("[FAIL] %s: non-finite value at %d\n", name, i);
            return 1;
        }
        if (vec[i] != 0.0f) nonzero = 1;
    }
    return expect_true(name, nonzero);
}

/**
 * Verifies two vectors are measurably different.
 * @param name Check description.
 * @param a First vector.
 * @param b Second vector.
 * @param dim Vector dimension.
 * @return 0 on success, 1 on failure.
 */
static int expect_vectors_distinct(const char *name, const float *a, const float *b, int dim) {
    int i;

    for (i = 0; i < dim; i++) {
        if (fabsf(a[i] - b[i]) > 0.000001f) return expect_true(name, 1);
    }
    return expect_true(name, 0);
}

/**
 * Tests kc_emb_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_emb_version(void) {
    const char *name = "kc_emb_version";
    const char *detail = "version returns non-zero build timestamp";
    int fail = 0;

    fail += expect_true("version returns non-zero build timestamp", kc_emb_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_emb_options_default.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_emb_options_default(void) {
    const char *name = "kc_emb_options_default";
    const char *detail = "options_default initializes unused field to zero";
    kc_emb_options_t opts;
    int fail = 0;

    opts = kc_emb_options_default();
    fail += expect_int("options_default initializes unused field to zero", 0, opts._unused);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_emb_options_load_env.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_emb_options_load_env(void) {
    const char *name = "kc_emb_options_load_env";
    const char *detail = "options_load_env leaves unused unchanged and tolerates NULL";
    kc_emb_options_t opts;
    int fail = 0;

    fail = 0;
    opts = kc_emb_options_default();
    opts._unused = 7;
    kc_emb_options_load_env(&opts);
    fail += expect_int("load_env leaves unused field unchanged", 7, opts._unused);
    kc_emb_options_load_env(NULL);
    fail += expect_true("load_env(NULL) does not crash", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_emb_options_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_emb_options_free(void) {
    const char *name = "kc_emb_options_free";
    const char *detail = "options_free preserves unused and tolerates NULL";
    kc_emb_options_t opts;
    int fail = 0;

    fail = 0;
    opts = kc_emb_options_default();
    opts._unused = 7;
    kc_emb_options_free(&opts);
    fail += expect_int("options_free keeps unused field", 7, opts._unused);
    kc_emb_options_free(NULL);
    fail += expect_true("options_free(NULL) does not crash", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_emb_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_emb_open(void) {
    const char *name = "kc_emb_open";
    const char *detail = "open rejects bad args and opens valid context";
    kc_emb_options_t opts;
    kc_emb_t *ctx;
    int fail = 0;

    fail = 0;
    opts = kc_emb_options_default();
    ctx = NULL;
    fail += expect_int("open(NULL, opts) returns ERROR", KC_EMB_ERROR,
        kc_emb_open(NULL, &opts));
    fail += expect_int("open(out, NULL) returns ERROR", KC_EMB_ERROR,
        kc_emb_open(&ctx, NULL));
    fail += expect_true("open with NULL args leaves out as NULL", ctx == NULL);
    fail += expect_int("open(out, opts) returns OK", KC_EMB_OK,
        kc_emb_open(&ctx, &opts));
    fail += expect_true("open sets context", ctx != NULL);
    kc_emb_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_emb_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_emb_close(void) {
    const char *name = "kc_emb_close";
    const char *detail = "close frees context and tolerates NULL";
    kc_emb_t *ctx;
    int fail = 0;

    fail = 0;
    ctx = NULL;
    if (open_context(&ctx) != 0) {
        case_result(1, name, detail);
        return 1;
    }
    kc_emb_close(NULL);
    fail += expect_true("close(NULL) does not crash", 1);
    kc_emb_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_emb_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_emb_stop(void) {
    const char *name = "kc_emb_stop";
    const char *detail = "stop blocks future exec and tolerates NULL";
    kc_emb_t *ctx;
    float *vec;
    int dim;
    int fail = 0;

    fail = 0;
    ctx = NULL;
    vec = NULL;
    fail += expect_int("stop(NULL) returns ERROR", KC_EMB_ERROR, kc_emb_stop(NULL));
    if (open_context(&ctx) != 0) {
        case_result(1, name, detail);
        return 1;
    }
    dim = kc_emb_dim(ctx);
    vec = (float *)calloc((size_t)dim, sizeof(float));
    fail += expect_true("allocate vector", vec != NULL);
    if (vec == NULL) {
        kc_emb_close(ctx);
        case_result(1, name, detail);
        return 1;
    }
    fail += expect_int("exec before stop returns OK", KC_EMB_OK,
        kc_emb_exec(ctx, "pre stop input", vec));
    fail += expect_int("stop(ctx) returns OK", KC_EMB_OK, kc_emb_stop(ctx));
    fail += expect_int("stop(ctx) second call returns OK", KC_EMB_OK, kc_emb_stop(ctx));
    fail += expect_int("exec after stop returns ESTOP", KC_EMB_ESTOP,
        kc_emb_exec(ctx, "post stop input", vec));
    free(vec);
    kc_emb_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_emb_dim.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_emb_dim(void) {
    const char *name = "kc_emb_dim";
    const char *detail = "dim returns 384 for valid context and zero for NULL";
    kc_emb_t *ctx;
    int fail = 0;

    fail = 0;
    ctx = NULL;
    fail += expect_int("dim(NULL) returns zero", 0, kc_emb_dim(NULL));
    if (open_context(&ctx) != 0) {
        case_result(1, name, detail);
        return 1;
    }
    fail += expect_int("dim(ctx) returns 384", 384, kc_emb_dim(ctx));
    kc_emb_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_emb_exec.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_emb_exec(void) {
    const char *name = "kc_emb_exec";
    const char *detail = "exec produces valid distinct vectors for distinct inputs";
    kc_emb_t *ctx;
    float *first;
    float *second;
    int dim;
    int fail = 0;

    fail = 0;
    ctx = NULL;
    first = NULL;
    second = NULL;
    fail += expect_int("exec(NULL, input, out) returns ERROR", KC_EMB_ERROR,
        kc_emb_exec(NULL, "input", (float *)&fail));
    if (open_context(&ctx) != 0) {
        case_result(1, name, detail);
        return 1;
    }
    dim = kc_emb_dim(ctx);
    first = (float *)calloc((size_t)dim, sizeof(float));
    second = (float *)calloc((size_t)dim, sizeof(float));
    fail += expect_true("allocate first vector", first != NULL);
    fail += expect_true("allocate second vector", second != NULL);
    if (first == NULL || second == NULL) {
        free(first);
        free(second);
        kc_emb_close(ctx);
        case_result(1, name, detail);
        return 1;
    }
    fail += expect_int("exec(ctx, NULL, out) returns ERROR", KC_EMB_ERROR,
        kc_emb_exec(ctx, NULL, first));
    fail += expect_int("exec(ctx, input, NULL) returns ERROR", KC_EMB_ERROR,
        kc_emb_exec(ctx, "input", NULL));
    fail += expect_int("exec first input returns OK", KC_EMB_OK,
        kc_emb_exec(ctx, "incident response runbook", first));
    fail += expect_vector_valid("first vector is finite and nonzero", first, dim);
    fail += expect_int("exec second input returns OK", KC_EMB_OK,
        kc_emb_exec(ctx, "release rollback checklist", second));
    fail += expect_vector_valid("second vector is finite and nonzero", second, dim);
    fail += expect_vectors_distinct("distinct inputs produce distinct vectors", first, second, dim);
    fail += expect_int("exec empty input returns OK", KC_EMB_OK,
        kc_emb_exec(ctx, "", first));
    fail += expect_vector_valid("empty input vector is finite and nonzero", first, dim);
    free(first);
    free(second);
    kc_emb_close(ctx);
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

    run_case(&rc, case_kc_emb_version);
    run_case(&rc, case_kc_emb_options_default);
    run_case(&rc, case_kc_emb_options_load_env);
    run_case(&rc, case_kc_emb_options_free);
    run_case(&rc, case_kc_emb_open);
    run_case(&rc, case_kc_emb_close);
    run_case(&rc, case_kc_emb_stop);
    run_case(&rc, case_kc_emb_dim);
    run_case(&rc, case_kc_emb_exec);

    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one libemb contract test case.
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
    if (strcmp(argv[1], "kc_emb_version") == 0) return case_kc_emb_version();
    if (strcmp(argv[1], "kc_emb_options_default") == 0) return case_kc_emb_options_default();
    if (strcmp(argv[1], "kc_emb_options_load_env") == 0) return case_kc_emb_options_load_env();
    if (strcmp(argv[1], "kc_emb_options_free") == 0) return case_kc_emb_options_free();
    if (strcmp(argv[1], "kc_emb_open") == 0) return case_kc_emb_open();
    if (strcmp(argv[1], "kc_emb_close") == 0) return case_kc_emb_close();
    if (strcmp(argv[1], "kc_emb_stop") == 0) return case_kc_emb_stop();
    if (strcmp(argv[1], "kc_emb_dim") == 0) return case_kc_emb_dim();
    if (strcmp(argv[1], "kc_emb_exec") == 0) return case_kc_emb_exec();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
