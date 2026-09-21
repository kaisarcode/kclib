/**
 * test.c - libemb public API tests.
 * Summary: Behavior-oriented contract tests for the emb public API.
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
    if (kc_emb_open(out) != KC_EMB_OK) {
        return 1;
    }
    return 0;
}

/**
 * Verifies all vector values are finite and not all zero.
 * @param name Check description.
 * @param vec Vector data.
 * @param dim Vector dimension.
 * @return 0 on success, 1 on failure.
 */
static int expect_vector_valid(const char *name, const float *vec, size_t dim) {
    size_t i;
    int nonzero;

    nonzero = 0;
    for (i = 0; i < dim; i++) {
        if (!isfinite(vec[i])) {
            printf("[FAIL] %s: non-finite value at %zu\n", name, i);
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
static int expect_vectors_distinct(const char *name, const float *a, const float *b, size_t dim) {
    size_t i;

    for (i = 0; i < dim; i++) {
        if (fabsf(a[i] - b[i]) > 0.000001f) return expect_true(name, 1);
    }
    return expect_true(name, 0);
}

/**
 * Verifies two vectors are close within tolerance.
 * @param name Check description.
 * @param a First vector.
 * @param b Second vector.
 * @param dim Vector dimension.
 * @param tol Tolerance.
 * @return 0 on success, 1 on failure.
 */
static int expect_vectors_close(const char *name, const float *a, const float *b, size_t dim, float tol) {
    size_t i;

    for (i = 0; i < dim; i++) {
        if (fabsf(a[i] - b[i]) > tol) {
            printf("[FAIL] %s: vectors differ at %zu: %f vs %f (diff %f > %f)\n", name, i, a[i], b[i], fabsf(a[i] - b[i]), tol);
            return 1;
        }
    }
    return expect_true(name, 1);
}

/**
 * Verifies version reports build timestamp.
 * @return 0 on success, 1 on failure.
 */
static int case_version(void) {
    const char *name = "kc_emb_version";
    const char *detail = "version reports build timestamp";
    int fail = 0;

    fail += expect_true("version returns non-zero build timestamp", kc_emb_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Verifies lifecycle and metadata contract.
 * @return 0 on success, 1 on failure.
 */
static int case_lifecycle_metadata(void) {
    const char *name = "lifecycle_metadata";
    const char *detail = "open close dim error lifecycle";
    kc_emb_t *ctx = NULL;
    kc_emb_t *sentinel = (kc_emb_t *)0xDEADBEEF;
    const char *err = NULL;
    size_t dim = 0;
    int fail = 0;

    fail += expect_int("open rejects NULL out", KC_EMB_ERROR, kc_emb_open(NULL));
    ctx = sentinel;
    fail += expect_int("open NULL leaves sentinel", KC_EMB_ERROR, kc_emb_open(NULL));
    fail += expect_true("sentinel not overwritten", ctx == sentinel);
    ctx = NULL;
    fail += expect_int("open creates context", KC_EMB_OK, kc_emb_open(&ctx));
    fail += expect_true("ctx non-NULL", ctx != NULL && ctx != sentinel);
    if (ctx != NULL) {
        err = kc_emb_get_error(ctx);
        fail += expect_true("fresh error not NULL", err != NULL);
        fail += expect_true("fresh error empty", err != NULL && err[0] == '\0');
        fail += expect_true("get_error NULL returns NULL", kc_emb_get_error(NULL) == NULL);
        fail += expect_true("dim NULL returns 0", kc_emb_dim(NULL) == 0);
        dim = kc_emb_dim(ctx);
        fail += expect_true("dim positive", dim > 0);
        fail += expect_int("dim is 384", 384, (int)dim);
        kc_emb_close(NULL);
        fail += expect_true("close NULL does not crash", 1);
        kc_emb_close(ctx);
        ctx = NULL;
        fail += expect_true("close succeeds", 1);
    }
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Verifies embedding behavior for inputs.
 * @return 0 on success, 1 on failure.
 */
static int case_embedding_behavior(void) {
    const char *name = "embedding_behavior";
    const char *detail = "exec produces valid distinct vectors";
    kc_emb_t *ctx = NULL;
    size_t dim = 0;
    float *vec = NULL;
    size_t count = 0;
    float *vec2 = NULL;
    size_t count2 = 0;
    float *empty = NULL;
    size_t empty_count = 0;
    int fail = 0;

    if (open_context(&ctx) != 0) {
        case_result(1, name, detail);
        return 1;
    }
    dim = kc_emb_dim(ctx);
    fail += expect_int("exec fox returns OK", KC_EMB_OK, kc_emb_exec(ctx, "The quick brown fox", &vec, &count));
    fail += expect_true("vec non-NULL", vec != NULL);
    fail += expect_true("count equals dim", count == dim);
    if (vec != NULL) fail += expect_vector_valid("fox vector valid", vec, dim);
    fail += expect_int("exec runbook returns OK", KC_EMB_OK, kc_emb_exec(ctx, "incident response runbook", &vec2, &count2));
    fail += expect_true("second vec non-NULL", vec2 != NULL);
    fail += expect_true("second count equals dim", count2 == dim);
    if (vec2 != NULL) fail += expect_vector_valid("runbook vector valid", vec2, dim);
    if (vec != NULL && vec2 != NULL) {
        fail += expect_vectors_distinct("distinct inputs distinct", vec, vec2, dim);
    } else {
        fail += expect_true("distinct requires vectors", 0);
    }
    fail += expect_int("exec empty returns OK", KC_EMB_OK, kc_emb_exec(ctx, "", &empty, &empty_count));
    fail += expect_true("empty vec non-NULL", empty != NULL);
    fail += expect_true("empty count equals dim", empty_count == dim);
    if (empty != NULL) fail += expect_vector_valid("empty vector valid", empty, dim);
    kc_emb_free(vec);
    kc_emb_free(vec2);
    kc_emb_free(empty);
    kc_emb_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Verifies output ownership and sanitization.
 * @return 0 on success, 1 on failure.
 */
static int case_output_contract(void) {
    const char *name = "output_contract";
    const char *detail = "exec ownership and sanitization";
    kc_emb_t *ctx = NULL;
    size_t dim = 0;
    float *vec = NULL;
    size_t count = 0;
    float *sentinel_vec = (float *)0xDEADBEEF;
    size_t sentinel_count = 0xDEADBEEF;
    int fail = 0;

    if (open_context(&ctx) != 0) {
        case_result(1, name, detail);
        return 1;
    }
    dim = kc_emb_dim(ctx);
    fail += expect_int("exec owned returns OK", KC_EMB_OK, kc_emb_exec(ctx, "The quick brown fox", &vec, &count));
    fail += expect_true("vec non-NULL owned", vec != NULL);
    fail += expect_true("count equals dim", count == dim);
    kc_emb_free(vec);
    vec = NULL;
    kc_emb_free(NULL);
    fail += expect_true("free NULL does not crash", 1);
    kc_emb_free(NULL);
    fail += expect_true("second free NULL safe", 1);
    vec = sentinel_vec;
    count = sentinel_count;
    fail += expect_int("exec NULL ctx error", KC_EMB_ERROR, kc_emb_exec(NULL, "input", &vec, &count));
    fail += expect_true("vec NULL on NULL ctx", vec == NULL);
    fail += expect_true("count 0 on NULL ctx", count == 0);
    vec = sentinel_vec;
    count = sentinel_count;
    fail += expect_int("exec NULL input error", KC_EMB_ERROR, kc_emb_exec(ctx, NULL, &vec, &count));
    fail += expect_true("vec NULL on NULL input", vec == NULL);
    fail += expect_true("count 0 on NULL input", count == 0);
    count = sentinel_count;
    fail += expect_int("exec NULL out_data error", KC_EMB_ERROR, kc_emb_exec(ctx, "input", NULL, &count));
    fail += expect_true("count 0 on NULL out_data", count == 0);
    vec = sentinel_vec;
    fail += expect_int("exec NULL out_count error", KC_EMB_ERROR, kc_emb_exec(ctx, "input", &vec, NULL));
    fail += expect_true("vec NULL on NULL out_count", vec == NULL);
    kc_emb_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Verifies determinism across calls and contexts.
 * @return 0 on success, 1 on failure.
 */
static int case_contexts_and_determinism(void) {
    const char *name = "contexts_and_determinism";
    const char *detail = "determinism across calls and contexts";
    kc_emb_t *ctx = NULL;
    kc_emb_t *ctx2 = NULL;
    size_t dim = 0;
    size_t dim2 = 0;
    float *a = NULL;
    float *b = NULL;
    float *c = NULL;
    float *d = NULL;
    size_t ca = 0;
    size_t cb = 0;
    size_t cc = 0;
    size_t cd = 0;
    int fail = 0;

    if (open_context(&ctx) != 0) {
        case_result(1, name, detail);
        return 1;
    }
    if (open_context(&ctx2) != 0) {
        kc_emb_close(ctx);
        case_result(1, name, detail);
        return 1;
    }
    dim = kc_emb_dim(ctx);
    dim2 = kc_emb_dim(ctx2);
    fail += expect_true("dims equal across contexts", dim == dim2);
    fail += expect_true("dim positive", dim > 0);
    fail += expect_int("exec a ok", KC_EMB_OK, kc_emb_exec(ctx, "The quick brown fox", &a, &ca));
    fail += expect_int("exec b ok", KC_EMB_OK, kc_emb_exec(ctx, "The quick brown fox", &b, &cb));
    fail += expect_true("counts equal dim", ca == dim && cb == dim);
    if (a != NULL && b != NULL) {
        fail += expect_vectors_close("same input same ctx close", a, b, dim, 1e-6f);
    } else {
        fail += expect_true("determinism requires vectors", 0);
    }
    fail += expect_int("exec c ctx2 ok", KC_EMB_OK, kc_emb_exec(ctx2, "The quick brown fox", &c, &cc));
    fail += expect_true("ctx2 count dim", cc == dim);
    if (a != NULL && c != NULL) {
        fail += expect_vectors_close("same input across ctx close", a, c, dim, 1e-6f);
    } else {
        fail += expect_true("cross-context requires vectors", 0);
    }
    fail += expect_int("exec distinct ok", KC_EMB_OK, kc_emb_exec(ctx, "incident response runbook", &d, &cd));
    if (a != NULL && d != NULL) {
        fail += expect_vectors_distinct("distinct still distinct", a, d, dim);
    } else {
        fail += expect_true("distinct requires vectors", 0);
    }
    kc_emb_free(a);
    kc_emb_free(b);
    kc_emb_free(c);
    kc_emb_free(d);
    kc_emb_close(ctx);
    kc_emb_close(ctx2);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Verifies error contract and recovery.
 * @return 0 on success, 1 on failure.
 */
static int case_error_contract(void) {
    const char *name = "error_contract";
    const char *detail = "error state after failure and success";
    kc_emb_t *ctx = NULL;
    const char *err = NULL;
    float *vec = (float *)0xDEADBEEF;
    size_t count = 0xDEADBEEF;
    int fail = 0;

    if (open_context(&ctx) != 0) {
        case_result(1, name, detail);
        return 1;
    }
    err = kc_emb_get_error(ctx);
    fail += expect_true("fresh error not NULL", err != NULL);
    fail += expect_true("fresh error empty", err != NULL && err[0] == '\0');
    vec = (float *)0xDEADBEEF;
    count = 0xDEADBEEF;
    fail += expect_int("exec NULL input error", KC_EMB_ERROR, kc_emb_exec(ctx, NULL, &vec, &count));
    fail += expect_true("vec NULL after failure", vec == NULL);
    fail += expect_true("count 0 after failure", count == 0);
    err = kc_emb_get_error(ctx);
    fail += expect_true("error non-empty after failure", err != NULL && strlen(err) > 0);
    count = 0xDEADBEEF;
    fail += expect_int("exec NULL out_data error", KC_EMB_ERROR, kc_emb_exec(ctx, "input", NULL, &count));
    fail += expect_true("count 0 after NULL out_data", count == 0);
    err = kc_emb_get_error(ctx);
    fail += expect_true("error non-empty after NULL out_data", err != NULL && strlen(err) > 0);
    vec = (float *)0xDEADBEEF;
    fail += expect_int("exec NULL out_count error", KC_EMB_ERROR, kc_emb_exec(ctx, "input", &vec, NULL));
    fail += expect_true("vec NULL after NULL out_count", vec == NULL);
    err = kc_emb_get_error(ctx);
    fail += expect_true("error non-empty after NULL out_count", err != NULL && strlen(err) > 0);
    vec = (float *)0xDEADBEEF;
    count = 0xDEADBEEF;
    fail += expect_int("exec success after failure", KC_EMB_OK, kc_emb_exec(ctx, "The quick brown fox", &vec, &count));
    fail += expect_true("vec non-NULL after success", vec != NULL);
    fail += expect_true("count equals dim after success", count == kc_emb_dim(ctx));
    err = kc_emb_get_error(ctx);
    fail += expect_true("error empty after success", err != NULL && err[0] == '\0');
    kc_emb_free(vec);
    vec = NULL;
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

    test_case_total = 6;
    test_case_current = 0;
    run_case(&rc, case_version);
    run_case(&rc, case_lifecycle_metadata);
    run_case(&rc, case_embedding_behavior);
    run_case(&rc, case_output_contract);
    run_case(&rc, case_contexts_and_determinism);
    run_case(&rc, case_error_contract);
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
    if (strcmp(argv[1], "version") == 0) return case_version();
    if (strcmp(argv[1], "lifecycle_metadata") == 0) return case_lifecycle_metadata();
    if (strcmp(argv[1], "embedding_behavior") == 0) return case_embedding_behavior();
    if (strcmp(argv[1], "output_contract") == 0) return case_output_contract();
    if (strcmp(argv[1], "contexts_and_determinism") == 0) return case_contexts_and_determinism();
    if (strcmp(argv[1], "error_contract") == 0) return case_error_contract();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
