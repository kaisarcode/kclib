/**
 * test.c - libhnsw public API tests.
 * Summary: Contract tests for the in-memory approximate-neighbor index.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libhnsw.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef int (*case_fn)(void);

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Print one grouped test result.
 * @param fail Non-zero when the case failed.
 * @param name Test case name.
 * @param detail Behavior covered by the case.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

/**
 * Convert one boolean expectation into a failure count.
 * @param condition Non-zero when the expectation passed.
 * @return Zero on success, or one on failure.
 */
static int expect(int condition) {
    return condition ? 0 : 1;
}

/**
 * Run one grouped test case.
 * @param fn Test case function.
 * @return Test failure count.
 */
static int run_case(case_fn fn) {
    test_case_current++;
    return fn();
}

/**
 * Open one index for a test using the library defaults.
 * @param dimension Vector dimension.
 * @return Open index, or NULL on failure.
 */
static kc_hnsw_t *open_index(size_t dimension) {
    kc_hnsw_options_t options = {0};
    kc_hnsw_t *hnsw = NULL;

    options.dimension = dimension;
    if (kc_hnsw_open(&hnsw, &options) != KC_HNSW_OK) return NULL;
    return hnsw;
}

/**
 * Open one index for a test with one explicit metric.
 * @param dimension Vector dimension.
 * @param metric Metric constant.
 * @return Open index, or NULL on failure.
 */
static kc_hnsw_t *open_index_metric(size_t dimension, int metric) {
    kc_hnsw_options_t options = {0};
    kc_hnsw_t *hnsw = NULL;

    options.dimension = dimension;
    options.metric = &metric;
    if (kc_hnsw_open(&hnsw, &options) != KC_HNSW_OK) return NULL;
    return hnsw;
}

/**
 * Test index opening, defaults, and required options.
 * @return Test failure count.
 */
static int case_kc_hnsw_open(void) {
    const char *name = "kc_hnsw_open";
    const char *detail = "applies defaults and validates explicit values";
    kc_hnsw_options_t options = {0};
    kc_hnsw_t *hnsw = (kc_hnsw_t *)1;
    int metric;
    int max_connections;
    int build_effort;
    int search_effort;
    int fail = 0;

    options.dimension = 2;
    fail |= expect(kc_hnsw_open(&hnsw, &options) == KC_HNSW_OK);
    fail |= expect(hnsw != NULL);
    fail |= expect(kc_hnsw_dimension(hnsw) == 2);
    fail |= expect(kc_hnsw_metric(hnsw) == KC_HNSW_METRIC_COSINE);
    kc_hnsw_close(hnsw);

    hnsw = (kc_hnsw_t *)1;
    memset(&options, 0, sizeof(options));
    fail |= expect(kc_hnsw_open(&hnsw, &options) == KC_HNSW_EINVAL);
    fail |= expect(hnsw == NULL);

    memset(&options, 0, sizeof(options));
    options.dimension = 2;
    metric = 0;
    options.metric = &metric;
    fail |= expect(kc_hnsw_open(&hnsw, &options) == KC_HNSW_EINVAL);
    fail |= expect(hnsw == NULL);

    metric = 99;
    fail |= expect(kc_hnsw_open(&hnsw, &options) == KC_HNSW_EINVAL);
    fail |= expect(hnsw == NULL);

    memset(&options, 0, sizeof(options));
    options.dimension = 2;
    max_connections = 0;
    options.max_connections = &max_connections;
    fail |= expect(kc_hnsw_open(&hnsw, &options) == KC_HNSW_EINVAL);
    fail |= expect(hnsw == NULL);

    memset(&options, 0, sizeof(options));
    options.dimension = 2;
    build_effort = 0;
    options.build_effort = &build_effort;
    fail |= expect(kc_hnsw_open(&hnsw, &options) == KC_HNSW_EINVAL);
    fail |= expect(hnsw == NULL);

    memset(&options, 0, sizeof(options));
    options.dimension = 2;
    search_effort = 0;
    options.search_effort = &search_effort;
    fail |= expect(kc_hnsw_open(&hnsw, &options) == KC_HNSW_EINVAL);
    fail |= expect(hnsw == NULL);

    memset(&options, 0, sizeof(options));
    options.dimension = 2;
    metric = KC_HNSW_METRIC_L2;
    max_connections = 8;
    build_effort = 32;
    search_effort = 32;
    options.metric = &metric;
    options.max_connections = &max_connections;
    options.build_effort = &build_effort;
    options.search_effort = &search_effort;
    fail |= expect(kc_hnsw_open(&hnsw, &options) == KC_HNSW_OK);
    fail |= expect(hnsw != NULL);
    fail |= expect(kc_hnsw_metric(hnsw) == KC_HNSW_METRIC_L2);
    kc_hnsw_close(hnsw);

    fail |= expect(kc_hnsw_open(NULL, &options) == KC_HNSW_EINVAL);
    hnsw = (kc_hnsw_t *)1;
    fail |= expect(kc_hnsw_open(&hnsw, NULL) == KC_HNSW_EINVAL);
    fail |= expect(hnsw == NULL);
    kc_hnsw_close(NULL);

    case_result(fail, name, detail);
    return fail;
}

/**
 * Test vector ownership and explicit build lifecycle.
 * @return Test failure count.
 */
static int case_kc_hnsw_add_build(void) {
    const char *name = "kc_hnsw_add_build";
    const char *detail = "owns vectors and requires rebuild after mutation";
    const float a[] = {1.0f, 0.0f};
    const float b[] = {0.0f, 1.0f};
    const float c[] = {1.0f, 1.0f};
    kc_hnsw_result_t *results = (kc_hnsw_result_t *)1;
    kc_hnsw_t *hnsw = open_index(2);
    size_t count = 99;
    int fail = 0;

    if (hnsw == NULL) {
        case_result(1, name, detail);
        return 1;
    }

    fail |= expect(kc_hnsw_count(hnsw) == 0);
    fail |= expect(kc_hnsw_add(hnsw, "a", a) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(hnsw, "b", b) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_count(hnsw) == 2);
    fail |= expect(kc_hnsw_add(hnsw, "", a) == KC_HNSW_EINVAL);
    fail |= expect(kc_hnsw_add(hnsw, NULL, a) == KC_HNSW_EINVAL);
    fail |= expect(kc_hnsw_add(hnsw, "bad", NULL) == KC_HNSW_EINVAL);

    fail |= expect(kc_hnsw_search(
        hnsw, a, 1, -1.0, &results, &count) == KC_HNSW_ESTATE);
    fail |= expect(results == NULL && count == 0);

    fail |= expect(kc_hnsw_build(hnsw) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_search(
        hnsw, a, 1, -1.0, &results, &count) == KC_HNSW_OK);
    fail |= expect(results != NULL && count == 1);
    if (results != NULL) fail |= expect(strcmp(results[0].id, "a") == 0);
    kc_hnsw_free(results);

    fail |= expect(kc_hnsw_add(hnsw, "c", c) == KC_HNSW_OK);
    results = (kc_hnsw_result_t *)1;
    count = 99;
    fail |= expect(kc_hnsw_search(
        hnsw, a, 1, -1.0, &results, &count) == KC_HNSW_ESTATE);
    fail |= expect(results == NULL && count == 0);
    fail |= expect(kc_hnsw_build(hnsw) == KC_HNSW_OK);

    kc_hnsw_close(hnsw);
    case_result(fail, name, detail);
    return fail;
}

/**
 * Test metrics, thresholds, and ranked search results.
 * @return Test failure count.
 */
static int case_kc_hnsw_search(void) {
    const char *name = "kc_hnsw_search";
    const char *detail = "supports cosine, inner product, L2, top-K, and thresholds";
    const float x[] = {1.0f, 0.0f};
    const float y[] = {0.0f, 1.0f};
    const float xy[] = {1.0f, 1.0f};
    const float origin[] = {0.0f, 0.0f};
    const float unit[] = {1.0f, 0.0f};
    const float distant[] = {4.0f, 0.0f};
    kc_hnsw_result_t *results = NULL;
    kc_hnsw_t *hnsw;
    size_t count = 0;
    int fail = 0;

    hnsw = open_index(2);
    if (hnsw == NULL) return 1;
    fail |= expect(kc_hnsw_add(hnsw, "x", x) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(hnsw, "y", y) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(hnsw, "xy", xy) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_build(hnsw) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_search(
        hnsw, x, 3, 0.8, &results, &count) == KC_HNSW_OK);
    fail |= expect(results != NULL && count == 1);
    if (results != NULL) {
        fail |= expect(strcmp(results[0].id, "x") == 0);
        fail |= expect(fabs(results[0].score - 1.0) < 0.000001);
    }
    kc_hnsw_free(results);
    kc_hnsw_close(hnsw);

    hnsw = open_index_metric(2, KC_HNSW_METRIC_INNER_PRODUCT);
    if (hnsw == NULL) return 1;
    fail |= expect(kc_hnsw_add(hnsw, "x", x) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(hnsw, "far", distant) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_build(hnsw) == KC_HNSW_OK);
    results = NULL;
    count = 0;
    fail |= expect(kc_hnsw_search(
        hnsw, x, 2, 2.0, &results, &count) == KC_HNSW_OK);
    fail |= expect(results != NULL && count == 1);
    if (results != NULL) {
        fail |= expect(strcmp(results[0].id, "far") == 0);
        fail |= expect(fabs(results[0].score - 4.0) < 0.000001);
    }
    kc_hnsw_free(results);
    kc_hnsw_close(hnsw);

    hnsw = open_index_metric(2, KC_HNSW_METRIC_L2);
    if (hnsw == NULL) return 1;
    fail |= expect(kc_hnsw_add(hnsw, "origin", origin) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(hnsw, "unit", unit) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(hnsw, "far", distant) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_build(hnsw) == KC_HNSW_OK);
    results = NULL;
    count = 0;
    fail |= expect(kc_hnsw_search(
        hnsw, origin, 3, 1.0, &results, &count) == KC_HNSW_OK);
    fail |= expect(results != NULL && count == 2);
    if (results != NULL && count == 2) {
        fail |= expect(strcmp(results[0].id, "origin") == 0);
        fail |= expect(strcmp(results[1].id, "unit") == 0);
    }
    kc_hnsw_free(results);
    kc_hnsw_close(hnsw);

    case_result(fail, name, detail);
    return fail;
}

/**
 * Test invalid arguments, output clearing, and empty indexes.
 * @return Test failure count.
 */
static int case_kc_hnsw_contract(void) {
    const char *name = "kc_hnsw_contract";
    const char *detail = "clears outputs and handles empty indexes and invalid arguments";
    const float q[] = {1.0f, 0.0f};
    kc_hnsw_result_t *results = (kc_hnsw_result_t *)1;
    kc_hnsw_t *hnsw = open_index(2);
    size_t count = 99;
    int fail = 0;

    if (hnsw == NULL) return 1;

    fail |= expect(kc_hnsw_build(hnsw) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_search(
        hnsw, q, 5, -1.0, &results, &count) == KC_HNSW_OK);
    fail |= expect(results == NULL && count == 0);

    results = (kc_hnsw_result_t *)1;
    count = 99;
    fail |= expect(kc_hnsw_search(
        hnsw, NULL, 1, -1.0, &results, &count) == KC_HNSW_EINVAL);
    fail |= expect(results == NULL && count == 0);

    count = 99;
    fail |= expect(kc_hnsw_search(
        hnsw, q, 1, -1.0, NULL, &count) == KC_HNSW_EINVAL);
    fail |= expect(count == 0);

    results = (kc_hnsw_result_t *)1;
    fail |= expect(kc_hnsw_search(
        hnsw, q, 1, -1.0, &results, NULL) == KC_HNSW_EINVAL);
    fail |= expect(results == NULL);

    results = (kc_hnsw_result_t *)1;
    count = 99;
    fail |= expect(kc_hnsw_search(
        hnsw, q, 0, -1.0, &results, &count) == KC_HNSW_OK);
    fail |= expect(results == NULL && count == 0);

    fail |= expect(kc_hnsw_dimension(NULL) == 0);
    fail |= expect(kc_hnsw_metric(NULL) == 0);
    fail |= expect(kc_hnsw_count(NULL) == 0);
    fail |= expect(strcmp(kc_hnsw_strerror(KC_HNSW_OK), "ok") == 0);
    fail |= expect(strcmp(kc_hnsw_strerror(KC_HNSW_ESTATE),
        "invalid state") == 0);
    kc_hnsw_free(NULL);
    kc_hnsw_close(hnsw);

    case_result(fail, name, detail);
    return fail;
}

typedef struct {
    const kc_hnsw_t *hnsw;
    const float *query;
    kc_hnsw_result_t *results;
    size_t count;
    int rc;
} search_worker_t;

#ifdef _WIN32
/**
 * Execute one concurrent search worker.
 * @param arg Worker state.
 * @return Windows thread exit code.
 */
static DWORD WINAPI search_worker_main(void *arg) {
#else
/**
 * Execute one concurrent search worker.
 * @param arg Worker state.
 * @return NULL when the worker finishes.
 */
static void *search_worker_main(void *arg) {
#endif
    search_worker_t *worker = (search_worker_t *)arg;
    worker->rc = kc_hnsw_search(worker->hnsw, worker->query, 1, -1.0,
        &worker->results, &worker->count);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/**
 * Test concurrent searches on one built index.
 * @return Test failure count.
 */
static int case_kc_hnsw_concurrency(void) {
    const char *name = "kc_hnsw_concurrency";
    const char *detail = "supports concurrent searches after build";
    const float x[] = {1.0f, 0.0f};
    const float y[] = {0.0f, 1.0f};
    search_worker_t workers[2];
    kc_hnsw_t *hnsw = open_index(2);
    int fail = 0;

#ifdef _WIN32
    HANDLE threads[2];
#else
    pthread_t threads[2];
#endif

    if (hnsw == NULL) return 1;
    fail |= expect(kc_hnsw_add(hnsw, "x", x) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(hnsw, "y", y) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_build(hnsw) == KC_HNSW_OK);

    memset(workers, 0, sizeof(workers));
    workers[0].hnsw = hnsw;
    workers[0].query = x;
    workers[1].hnsw = hnsw;
    workers[1].query = y;

#ifdef _WIN32
    threads[0] = CreateThread(NULL, 0, search_worker_main, &workers[0], 0, NULL);
    threads[1] = CreateThread(NULL, 0, search_worker_main, &workers[1], 0, NULL);
    fail |= expect(threads[0] != NULL && threads[1] != NULL);
    if (threads[0]) {
        fail |= expect(WaitForSingleObject(threads[0], INFINITE) == WAIT_OBJECT_0);
        CloseHandle(threads[0]);
    }
    if (threads[1]) {
        fail |= expect(WaitForSingleObject(threads[1], INFINITE) == WAIT_OBJECT_0);
        CloseHandle(threads[1]);
    }
#else
    fail |= expect(pthread_create(&threads[0], NULL,
        search_worker_main, &workers[0]) == 0);
    fail |= expect(pthread_create(&threads[1], NULL,
        search_worker_main, &workers[1]) == 0);
    fail |= expect(pthread_join(threads[0], NULL) == 0);
    fail |= expect(pthread_join(threads[1], NULL) == 0);
#endif

    fail |= expect(workers[0].rc == KC_HNSW_OK);
    fail |= expect(workers[1].rc == KC_HNSW_OK);
    fail |= expect(workers[0].count == 1 && workers[0].results != NULL);
    fail |= expect(workers[1].count == 1 && workers[1].results != NULL);
    kc_hnsw_free(workers[0].results);
    kc_hnsw_free(workers[1].results);
    kc_hnsw_close(hnsw);

    case_result(fail, name, detail);
    return fail;
}

/**
 * Test the generated build version.
 * @return Test failure count.
 */
static int case_kc_hnsw_version(void) {
    const char *name = "kc_hnsw_version";
    const char *detail = "returns the generated build version";
    int fail = expect(kc_hnsw_version() != 0U);

    case_result(fail, name, detail);
    return fail;
}

/**
 * Run all grouped HNSW contract tests.
 * @return Total failed case count.
 */
static int case_all(void) {
    int rc = 0;

    test_case_total = 6;
    test_case_current = 0;
    rc += run_case(case_kc_hnsw_open);
    rc += run_case(case_kc_hnsw_add_build);
    rc += run_case(case_kc_hnsw_search);
    rc += run_case(case_kc_hnsw_contract);
    rc += run_case(case_kc_hnsw_concurrency);
    rc += run_case(case_kc_hnsw_version);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Dispatch one grouped HNSW test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit status.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_hnsw_open") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_hnsw_open();
    }
    if (strcmp(argv[1], "kc_hnsw_add_build") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_hnsw_add_build();
    }
    if (strcmp(argv[1], "kc_hnsw_search") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_hnsw_search();
    }
    if (strcmp(argv[1], "kc_hnsw_contract") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_hnsw_contract();
    }
    if (strcmp(argv[1], "kc_hnsw_concurrency") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_hnsw_concurrency();
    }
    if (strcmp(argv[1], "kc_hnsw_version") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_hnsw_version();
    }
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
