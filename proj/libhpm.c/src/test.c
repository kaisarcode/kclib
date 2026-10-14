/**
 * test.c - libhpm public API contract tests.
 * Summary: Validates direct path establishment, datagrams, and lifecycle.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libhpm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
typedef HANDLE test_thread_t;
#else
#include <pthread.h>
#include <unistd.h>
typedef pthread_t test_thread_t;
#endif

static int test_case_total = 0;
static int test_case_current = 0;

typedef struct {
    kc_hpm_endpoint_t peer;
    kc_hpm_options_t options;
    kc_hpm_path_t *path;
    int status;
} test_side_t;

/**
 * Prints a test case result.
 * @param fail Failure flag.
 * @param name Test case name.
 * @param detail Test case detail.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail)
{
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

static void run_case(int *rc, int (*fn)(void))
{
    test_case_current++;
    *rc += fn();
}

/**
 * Checks a condition and reports failure.
 * @param name Test name.
 * @param condition Condition to check.
 * @return 0 on pass, 1 on fail.
 */
static int expect_true(const char *name, int condition)
{
    if (!condition) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

#ifdef _WIN32
/**
 * Thread function for connect test (Windows).
 * @param arg Test side data.
 * @return 0.
 */
static DWORD WINAPI test_connect_thread(LPVOID arg)
#else
/**
 * Thread function for connect test (POSIX).
 * @param arg Test side data.
 * @return NULL.
 */
static void *test_connect_thread(void *arg)
#endif
{
    test_side_t *side = (test_side_t *)arg;
    side->status = kc_hpm_connect(&side->path, &side->options);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/**
 * Returns a base port for testing derived from process ID.
 * @return Base port number.
 */
static unsigned short test_base_port(void)
{
#ifdef _WIN32
    unsigned long pid = GetCurrentProcessId();
#else
    unsigned long pid = (unsigned long)getpid();
#endif
    return (unsigned short)(35000U + (pid % 12000U));
}

/**
 * Establishes a pair of connected paths for testing.
 * @param a_path Output path A.
 * @param b_path Output path B.
 * @return 0 on success, 1 on failure.
 */
static int test_pair(kc_hpm_path_t **a_path, kc_hpm_path_t **b_path)
{
    unsigned short base = test_base_port();
    unsigned attempt;

    *a_path = NULL;
    *b_path = NULL;
    for (attempt = 0; attempt < 8U; ++attempt) {
        test_side_t a;
        test_side_t b;
        test_thread_t a_thread;
        test_thread_t b_thread;
        unsigned short a_port = (unsigned short)(base + attempt * 2U);
        unsigned short b_port = (unsigned short)(a_port + 1U);
        int started_a = 0;
        int started_b = 0;

        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        a.peer.host = "127.0.0.1";
        a.peer.port = b_port;
        a.options.host = "127.0.0.1";
        a.options.port = a_port;
        a.options.peers = &a.peer;
        a.options.peer_count = 1;
        a.options.timeout_ms = 1500;
        b.peer.host = "127.0.0.1";
        b.peer.port = a_port;
        b.options.host = "127.0.0.1";
        b.options.port = b_port;
        b.options.peers = &b.peer;
        b.options.peer_count = 1;
        b.options.timeout_ms = 1500;

#ifdef _WIN32
        b_thread = CreateThread(NULL, 0, test_connect_thread, &b, 0, NULL);
        started_b = b_thread != NULL;
        a_thread = CreateThread(NULL, 0, test_connect_thread, &a, 0, NULL);
        started_a = a_thread != NULL;
        if (started_a) WaitForSingleObject(a_thread, INFINITE);
        if (started_b) WaitForSingleObject(b_thread, INFINITE);
        if (started_a) CloseHandle(a_thread);
        if (started_b) CloseHandle(b_thread);
#else
        started_b = pthread_create(&b_thread, NULL, test_connect_thread, &b) == 0;
        started_a = pthread_create(&a_thread, NULL, test_connect_thread, &a) == 0;
        if (started_a) pthread_join(a_thread, NULL);
        if (started_b) pthread_join(b_thread, NULL);
#endif
        if (started_a && started_b && a.status == KC_HPM_OK &&
            b.status == KC_HPM_OK && a.path && b.path) {
            *a_path = a.path;
            *b_path = b.path;
            return 0;
        }
        kc_hpm_close(a.path);
        kc_hpm_close(b.path);
    }
    return 1;
}

/**
 * Test case for kc_hpm_connect.
 * @return 0 on pass, 1 on fail.
 */
static int case_kc_hpm_connect(void)
{
    const char *name = "kc_hpm_connect";
    const char *detail = "establishes a direct UDP path from supplied endpoints";
    kc_hpm_path_t *a = NULL;
    kc_hpm_path_t *b = NULL;
    kc_hpm_options_t invalid;
    int fail = 0;

    memset(&invalid, 0, sizeof(invalid));
    fail += expect_true("rejects NULL destination",
        kc_hpm_connect(NULL, &invalid) == KC_HPM_EINVAL);
    fail += expect_true("rejects NULL options",
        kc_hpm_connect(&a, NULL) == KC_HPM_EINVAL);
    fail += expect_true("rejects empty peer set",
        kc_hpm_connect(&a, &invalid) == KC_HPM_EINVAL);
    fail += expect_true("loopback pair establishes", test_pair(&a, &b) == 0);
    if (a && b) {
        fail += expect_true("A reports local host", kc_hpm_local_host(a) != NULL);
        fail += expect_true("A reports local port", kc_hpm_local_port(a) != 0);
        fail += expect_true("A reports peer host", kc_hpm_peer_host(a) != NULL);
        fail += expect_true("A reports peer port", kc_hpm_peer_port(a) != 0);
    }
    kc_hpm_close(a);
    kc_hpm_close(b);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Test case for kc_hpm_datagrams.
 * @return 0 on pass, 1 on fail.
 */
static int case_kc_hpm_datagrams(void)
{
    const char *name = "kc_hpm_datagrams";
    const char *detail = "sends and receives UDP datagrams through the path";
    kc_hpm_path_t *a = NULL;
    kc_hpm_path_t *b = NULL;
    void *data = NULL;
    size_t size = 0;
    int fail = 0;

    fail += expect_true("loopback pair establishes", test_pair(&a, &b) == 0);
    if (a && b) {
        fail += expect_true("send succeeds",
            kc_hpm_send(a, "hello", 5) == KC_HPM_OK);
        fail += expect_true("receive succeeds",
            kc_hpm_receive(b, &data, &size, 1000) == KC_HPM_OK);
        fail += expect_true("received size matches", size == 5);
        fail += expect_true("received bytes match",
            data && size == 5 && memcmp(data, "hello", 5) == 0);
        kc_hpm_free(data);
        data = NULL;
        size = 0;
        fail += expect_true("receive timeout is observable",
            kc_hpm_receive(b, &data, &size, 20) == KC_HPM_ETIMEOUT);
        fail += expect_true("timeout clears output", data == NULL && size == 0);
    }
    kc_hpm_free(data);
    kc_hpm_close(a);
    kc_hpm_close(b);
    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Test case for kc_hpm_lifecycle.
 * @return 0 on pass, 1 on fail.
 */
static int case_kc_hpm_lifecycle(void)
{
    const char *name = "kc_hpm_lifecycle";
    const char *detail = "reports errors, version, and accepts NULL cleanup";
    int fail = 0;

    kc_hpm_close(NULL);
    kc_hpm_free(NULL);
    fail += expect_true("version is nonzero", kc_hpm_version() != 0U);
    fail += expect_true("known status has message",
        strcmp(kc_hpm_strerror(KC_HPM_ETIMEOUT), "connection timed out") == 0);
    fail += expect_true("unknown status has message",
        kc_hpm_strerror(-999) != NULL);
    fail += expect_true("closed path rejects send",
        kc_hpm_send(NULL, "x", 1) == KC_HPM_ECLOSED);

    case_result(fail, name, detail);
    return fail ? 1 : 0;
}

/**
 * Runs all test cases.
 * @return Number of failures.
 */
static int case_all(void)
{
    int rc = 0;
    test_case_total = 3;
    test_case_current = 0;
    run_case(&rc, case_kc_hpm_connect);
    run_case(&rc, case_kc_hpm_datagrams);
    run_case(&rc, case_kc_hpm_lifecycle);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Test program entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit status.
 */
int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_hpm_connect") == 0) return case_kc_hpm_connect();
    if (strcmp(argv[1], "kc_hpm_datagrams") == 0) return case_kc_hpm_datagrams();
    if (strcmp(argv[1], "kc_hpm_lifecycle") == 0) return case_kc_hpm_lifecycle();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
