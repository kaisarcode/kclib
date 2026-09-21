/**
 * test.c - libhnsw public API tests.
 * Summary: Exercises the normalized HNSW lifecycle and search contract.
 */

#include "libhnsw.h"

#include <stdio.h>
#include <string.h>

typedef int (*case_fn)(void);

static int test_case_total = 0;
static int test_case_current = 0;

static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

static int expect(int condition) {
    return condition ? 0 : 1;
}

static int run_case(case_fn fn) {
    test_case_current++;
    return fn();
}

static kc_hnsw_t *open_index(size_t dimension, int metric) {
    kc_hnsw_options_t options = kc_hnsw_options_default();
    kc_hnsw_t *ctx = NULL;

    options.dimension = dimension;
    options.metric = metric;
    if (kc_hnsw_open(&ctx, &options) != KC_HNSW_OK) {
        return NULL;
    }
    return ctx;
}

static int case_kc_hnsw_open(void) {
    const char *name = "kc_hnsw_open";
    const char *detail = "validates options and manages index lifetime";
    kc_hnsw_options_t options = kc_hnsw_options_default();
    kc_hnsw_t *ctx = (kc_hnsw_t *)1;
    int fail = 0;

    fail |= expect(options.dimension == 0);
    fail |= expect(options.metric == KC_HNSW_METRIC_COSINE);
    fail |= expect(kc_hnsw_open(&ctx, &options) == KC_HNSW_EINVAL);
    fail |= expect(ctx == NULL);
    options.dimension = 2;
    fail |= expect(kc_hnsw_open(&ctx, &options) == KC_HNSW_OK);
    fail |= expect(ctx != NULL);
    kc_hnsw_close(ctx);
    kc_hnsw_close(NULL);
    case_result(fail, name, detail);
    return fail;
}

static int case_kc_hnsw_reserve(void) {
    const char *name = "kc_hnsw_reserve";
    const char *detail = "rejects invalid index arguments";
    kc_hnsw_options_t options = kc_hnsw_options_default();
    kc_hnsw_t *ctx = NULL;
    int fail = 0;

    options.dimension = 2;
    options.metric = 99;
    fail |= expect(kc_hnsw_open(&ctx, &options) == KC_HNSW_EINVAL && ctx == NULL);
    fail |= expect(kc_hnsw_open(NULL, &options) == KC_HNSW_EINVAL);
    fail |= expect(kc_hnsw_reserve(NULL, 1) == KC_HNSW_EINVAL);
    fail |= expect(kc_hnsw_add(NULL, "x", (const float[]){1.0f}) == KC_HNSW_EINVAL);
    case_result(fail, name, detail);
    return fail;
}

static int case_kc_hnsw_build(void) {
    const char *name = "kc_hnsw_build";
    const char *detail = "enforces mutation and build state";
    const float first[] = {1.0f, 0.0f};
    const float second[] = {0.0f, 1.0f};
    kc_hnsw_result_t *results = NULL;
    kc_hnsw_t *ctx = open_index(2, KC_HNSW_METRIC_COSINE);
    size_t count = 0;
    int fail = 0;

    if (ctx == NULL) {
        case_result(1, name, detail);
        return 1;
    }
    fail |= expect(kc_hnsw_reserve(ctx, 4) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(ctx, "first", first) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(ctx, "second", second) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_count(ctx) == 2);
    fail |= expect(kc_hnsw_dimension(ctx) == 2);
    fail |= expect(kc_hnsw_metric(ctx) == KC_HNSW_METRIC_COSINE);
    fail |= expect(kc_hnsw_search(ctx, first, 1, -1.0, &results, &count) == KC_HNSW_ESTATE);
    fail |= expect(results == NULL && count == 0);
    fail |= expect(kc_hnsw_build(ctx) == KC_HNSW_OK);
    kc_hnsw_close(ctx);
    case_result(fail, name, detail);
    return fail;
}

static int case_kc_hnsw_free(void) {
    const char *name = "kc_hnsw_free";
    const char *detail = "releases cosine search results";
    const float left[] = {1.0f, 0.0f};
    const float right[] = {0.0f, 1.0f};
    kc_hnsw_result_t *results = NULL;
    kc_hnsw_t *ctx = open_index(2, KC_HNSW_METRIC_COSINE);
    size_t count = 0;
    int fail = 0;

    if (ctx == NULL) {
        case_result(1, name, detail);
        return 1;
    }
    fail |= expect(kc_hnsw_add(ctx, "left", left) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(ctx, "right", right) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_build(ctx) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_search(ctx, left, 2, 0.5, &results, &count) == KC_HNSW_OK);
    fail |= expect(count == 1 && results != NULL);
    if (results != NULL) fail |= expect(strcmp(results[0].id, "left") == 0);
    kc_hnsw_free(results);
    kc_hnsw_free(NULL);
    kc_hnsw_close(ctx);
    case_result(fail, name, detail);
    return fail;
}

static int case_kc_hnsw_search(void) {
    const char *name = "kc_hnsw_search";
    const char *detail = "applies L2 thresholds and clears invalid outputs";
    const float near[] = {0.0f, 0.0f};
    const float far[] = {4.0f, 0.0f};
    kc_hnsw_result_t *results = (kc_hnsw_result_t *)1;
    kc_hnsw_t *ctx = open_index(2, KC_HNSW_METRIC_L2);
    size_t count = 99;
    int fail = 0;

    if (ctx == NULL) {
        case_result(1, name, detail);
        return 1;
    }
    fail |= expect(kc_hnsw_add(ctx, "near", near) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(ctx, "far", far) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_build(ctx) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_search(ctx, near, 2, 0.1, &results, &count) == KC_HNSW_OK);
    fail |= expect(count == 1 && results != NULL);
    if (results != NULL) fail |= expect(strcmp(results[0].id, "near") == 0);
    kc_hnsw_free(results);
    results = (kc_hnsw_result_t *)1;
    count = 99;
    fail |= expect(kc_hnsw_search(ctx, NULL, 1, 1.0, &results, &count) == KC_HNSW_EINVAL);
    fail |= expect(results == NULL && count == 0);
    kc_hnsw_close(ctx);
    case_result(fail, name, detail);
    return fail;
}

static int case_kc_hnsw_metric_from_string(void) {
    const char *name = "kc_hnsw_metric_from_string";
    const char *detail = "converts metrics and status values";
    int fail = 0;

    fail |= expect(kc_hnsw_metric_from_string("cosine") == KC_HNSW_METRIC_COSINE);
    fail |= expect(kc_hnsw_metric_from_string("inner_product") == KC_HNSW_METRIC_INNER_PRODUCT);
    fail |= expect(kc_hnsw_metric_from_string("euclidean") == KC_HNSW_METRIC_L2);
    fail |= expect(kc_hnsw_metric_from_string("bad") == 0);
    fail |= expect(strcmp(kc_hnsw_metric_to_string(KC_HNSW_METRIC_L2), "l2") == 0);
    fail |= expect(kc_hnsw_metric_to_string(0) == NULL);
    fail |= expect(strcmp(kc_hnsw_strerror(KC_HNSW_ESTOP), "stopped") == 0);
    uint64_t version = kc_hnsw_version();
    (void)version;
    case_result(fail, name, detail);
    return fail;
}

static int case_kc_hnsw_stop(void) {
    const char *name = "kc_hnsw_stop";
    const char *detail = "stops build and search operations";
    const float values[] = {1.0f, 0.0f};
    kc_hnsw_result_t *results = (kc_hnsw_result_t *)1;
    kc_hnsw_t *ctx = open_index(2, KC_HNSW_METRIC_COSINE);
    size_t count = 99;
    int fail = 0;

    if (ctx == NULL) {
        case_result(1, name, detail);
        return 1;
    }
    fail |= expect(kc_hnsw_stop(NULL) == KC_HNSW_EINVAL);
    fail |= expect(kc_hnsw_add(ctx, "one", values) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_stop(ctx) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_build(ctx) == KC_HNSW_ESTOP);
    fail |= expect(kc_hnsw_search(ctx, values, 1, -1.0, &results, &count) == KC_HNSW_ESTOP);
    fail |= expect(results == NULL && count == 0);
    kc_hnsw_close(ctx);
    case_result(fail, name, detail);
    return fail;
}

static int case_all(void) {
    int rc = 0;

    test_case_total = 7;
    test_case_current = 0;
    rc += run_case(case_kc_hnsw_open);
    rc += run_case(case_kc_hnsw_reserve);
    rc += run_case(case_kc_hnsw_build);
    rc += run_case(case_kc_hnsw_free);
    rc += run_case(case_kc_hnsw_search);
    rc += run_case(case_kc_hnsw_metric_from_string);
    rc += run_case(case_kc_hnsw_stop);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_hnsw_open") == 0) return case_kc_hnsw_open();
    if (strcmp(argv[1], "kc_hnsw_reserve") == 0) return case_kc_hnsw_reserve();
    if (strcmp(argv[1], "kc_hnsw_build") == 0) return case_kc_hnsw_build();
    if (strcmp(argv[1], "kc_hnsw_free") == 0) return case_kc_hnsw_free();
    if (strcmp(argv[1], "kc_hnsw_search") == 0) return case_kc_hnsw_search();
    if (strcmp(argv[1], "kc_hnsw_metric_from_string") == 0) return case_kc_hnsw_metric_from_string();
    if (strcmp(argv[1], "kc_hnsw_stop") == 0) return case_kc_hnsw_stop();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
