/**
 * test.c - libhnsw public API tests.
 * Summary: Exercises the normalized HNSW lifecycle and search contract.
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libhnsw.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef int (*case_fn)(void);

#ifdef _WIN32
typedef HANDLE test_thread_t;
#else
typedef pthread_t test_thread_t;
#endif

typedef struct {
    const kc_hnsw_t *ctx;
    const float *query;
    kc_hnsw_result_t *results;
    size_t count;
    int rc;
} test_search_worker_t;

static int test_case_total = 0;
static int test_case_current = 0;

static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

static int expect(int condition) {
    return condition ? 0 : 1;
}

#ifdef _WIN32
static DWORD WINAPI test_search_worker_main(void *arg) {
#else
static void *test_search_worker_main(void *arg) {
#endif
    test_search_worker_t *worker = (test_search_worker_t *)arg;

    worker->rc = kc_hnsw_search(worker->ctx, worker->query, 1, -1.0,
        &worker->results, &worker->count);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static int test_thread_start(test_thread_t *thread, test_search_worker_t *worker) {
#ifdef _WIN32
    *thread = CreateThread(NULL, 0, test_search_worker_main, worker, 0, NULL);
    return *thread != NULL ? 0 : 1;
#else
    return pthread_create(thread, NULL, test_search_worker_main, worker) == 0 ? 0 : 1;
#endif
}

static int test_thread_join(test_thread_t thread) {
#ifdef _WIN32
    if (WaitForSingleObject(thread, INFINITE) != WAIT_OBJECT_0) return 1;
    CloseHandle(thread);
    return 0;
#else
    return pthread_join(thread, NULL) == 0 ? 0 : 1;
#endif
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
    const float third[] = {1.0f, 1.0f};
    kc_hnsw_result_t *results = (kc_hnsw_result_t *)1;
    kc_hnsw_t *ctx = open_index(2, KC_HNSW_METRIC_COSINE);
    test_search_worker_t workers[2];
    test_thread_t threads[2];
    size_t count = 99;
    int first_started;
    int second_started;
    int fail = 0;

    if (ctx == NULL) {
        case_result(1, name, detail);
        return 1;
    }
    fail |= expect(kc_hnsw_build(ctx) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_reserve(ctx, 4) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(ctx, "first", first) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(ctx, "second", second) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_count(ctx) == 2);
    fail |= expect(kc_hnsw_dimension(ctx) == 2);
    fail |= expect(kc_hnsw_metric(ctx) == KC_HNSW_METRIC_COSINE);
    fail |= expect(kc_hnsw_search(ctx, first, 1, -1.0, &results, &count) == KC_HNSW_ESTATE);
    fail |= expect(results == NULL && count == 0);
    fail |= expect(kc_hnsw_build(ctx) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(ctx, "third", third) == KC_HNSW_OK);
    results = (kc_hnsw_result_t *)1;
    count = 99;
    fail |= expect(kc_hnsw_search(ctx, first, 1, -1.0, &results, &count) == KC_HNSW_ESTATE);
    fail |= expect(results == NULL && count == 0);
    fail |= expect(kc_hnsw_build(ctx) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_search(ctx, first, 1, -1.0, &results, &count) == KC_HNSW_OK);
    fail |= expect(results != NULL && count == 1 && strcmp(results[0].id, "first") == 0);
    kc_hnsw_free(results);
    memset(workers, 0, sizeof(workers));
    workers[0].ctx = ctx;
    workers[0].query = first;
    workers[1].ctx = ctx;
    workers[1].query = first;
    first_started = test_thread_start(&threads[0], &workers[0]) == 0;
    second_started = test_thread_start(&threads[1], &workers[1]) == 0;
    fail |= expect(first_started);
    fail |= expect(second_started);
    if (first_started) fail |= expect(test_thread_join(threads[0]) == 0);
    if (second_started) fail |= expect(test_thread_join(threads[1]) == 0);
    fail |= expect(workers[0].rc == KC_HNSW_OK && workers[0].results != NULL && workers[0].count == 1);
    fail |= expect(workers[1].rc == KC_HNSW_OK && workers[1].results != NULL && workers[1].count == 1);
    kc_hnsw_free(workers[0].results);
    kc_hnsw_free(workers[1].results);
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
    const float nearest_values[] = {0.0f, 0.0f};
    const float middle[] = {1.0f, 0.0f};
    const float farthest_values[] = {4.0f, 0.0f};
    const float inner_low[] = {1.0f, 0.0f};
    const float inner_high[] = {3.0f, 0.0f};
    const float cosine_best[] = {2.0f, 0.0f};
    const float cosine_next[] = {1.0f, 1.0f};
    const float cosine_opposite[] = {-1.0f, 0.0f};
    const float zero[] = {0.0f, 0.0f};
    kc_hnsw_result_t *results = (kc_hnsw_result_t *)1;
    kc_hnsw_t *ctx = open_index(2, KC_HNSW_METRIC_L2);
    size_t count = 99;
    int fail = 0;

    if (ctx == NULL) {
        case_result(1, name, detail);
        return 1;
    }
    fail |= expect(kc_hnsw_add(ctx, "near", nearest_values) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(ctx, "middle", middle) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(ctx, "far", farthest_values) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_build(ctx) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_search(ctx, nearest_values, 3, 1.0, &results, &count) == KC_HNSW_OK);
    fail |= expect(count == 2 && results != NULL);
    if (results != NULL) {
        fail |= expect(strcmp(results[0].id, "near") == 0 && results[0].score == 0.0);
        fail |= expect(strcmp(results[1].id, "middle") == 0 && results[1].score == 1.0);
    }
    kc_hnsw_free(results);
    results = (kc_hnsw_result_t *)1;
    count = 99;
    fail |= expect(kc_hnsw_search(ctx, NULL, 1, 1.0, &results, &count) == KC_HNSW_EINVAL);
    fail |= expect(results == NULL && count == 0);
    count = 99;
    fail |= expect(kc_hnsw_search(ctx, nearest_values, 1, 1.0, NULL, &count) == KC_HNSW_EINVAL);
    fail |= expect(count == 0);
    results = (kc_hnsw_result_t *)1;
    fail |= expect(kc_hnsw_search(ctx, nearest_values, 1, 1.0, &results, NULL) == KC_HNSW_EINVAL);
    fail |= expect(results == NULL);
    results = (kc_hnsw_result_t *)1;
    count = 99;
    fail |= expect(kc_hnsw_search(ctx, nearest_values, 0, 1.0, &results, &count) == KC_HNSW_OK);
    fail |= expect(results == NULL && count == 0);
    results = (kc_hnsw_result_t *)1;
    count = 99;
    fail |= expect(kc_hnsw_search(ctx, nearest_values, 3, -1.0, &results, &count) == KC_HNSW_OK);
    fail |= expect(results == NULL && count == 0);
    kc_hnsw_close(ctx);

    ctx = open_index(2, KC_HNSW_METRIC_INNER_PRODUCT);
    if (ctx == NULL) {
        case_result(1, name, detail);
        return 1;
    }
    fail |= expect(kc_hnsw_add(ctx, "low", inner_low) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(ctx, "high", inner_high) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_build(ctx) == KC_HNSW_OK);
    results = NULL;
    count = 0;
    fail |= expect(kc_hnsw_search(ctx, inner_low, 2, 1.0, &results, &count) == KC_HNSW_OK);
    fail |= expect(results != NULL && count == 2);
    if (results != NULL) {
        fail |= expect(strcmp(results[0].id, "high") == 0 && results[0].score == 3.0);
        fail |= expect(strcmp(results[1].id, "low") == 0 && results[1].score == 1.0);
    }
    kc_hnsw_free(results);
    results = (kc_hnsw_result_t *)1;
    count = 99;
    fail |= expect(kc_hnsw_search(ctx, inner_low, 2, 2.0, &results, &count) == KC_HNSW_OK);
    fail |= expect(results != NULL && count == 1 && strcmp(results[0].id, "high") == 0);
    kc_hnsw_free(results);
    kc_hnsw_close(ctx);

    ctx = open_index(2, KC_HNSW_METRIC_COSINE);
    if (ctx == NULL) {
        case_result(1, name, detail);
        return 1;
    }
    fail |= expect(kc_hnsw_add(ctx, "best", cosine_best) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(ctx, "next", cosine_next) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(ctx, "opposite", cosine_opposite) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(ctx, "zero", zero) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_build(ctx) == KC_HNSW_OK);
    results = NULL;
    count = 0;
    fail |= expect(kc_hnsw_search(ctx, inner_low, 4, -1.0, &results, &count) == KC_HNSW_OK);
    fail |= expect(results != NULL && count == 4);
    if (results != NULL) {
        fail |= expect(strcmp(results[0].id, "best") == 0 && results[0].score == 1.0);
        fail |= expect(strcmp(results[1].id, "next") == 0 && results[1].score > 0.0);
    }
    kc_hnsw_free(results);
    results = (kc_hnsw_result_t *)1;
    count = 99;
    fail |= expect(kc_hnsw_search(ctx, inner_low, 4, 0.8, &results, &count) == KC_HNSW_OK);
    fail |= expect(results != NULL && count == 1 && strcmp(results[0].id, "best") == 0);
    kc_hnsw_free(results);
    results = NULL;
    count = 0;
    fail |= expect(kc_hnsw_search(ctx, zero, 4, 0.0, &results, &count) == KC_HNSW_OK);
    fail |= expect(results != NULL && count == 4);
    if (results != NULL) {
        for (size_t i = 0; i < count; i++) fail |= expect(results[i].score == 0.0);
    }
    kc_hnsw_free(results);
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
