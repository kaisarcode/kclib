/**
 * test.c - libwch public API contract tests.
 * Summary: Validates each exported libwch function through one dedicated test case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libwch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Test case name.
 * @param detail Behavior verified by the case.
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
 * Tests kc_wch_options_default.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wch_options_default(void) {
    const char *name = "kc_wch_options_default";
    const char *detail = "initializes correctly";
    kc_wch_options_t opts;
    int fail = 0;

    opts = kc_wch_options_default();
    fail += expect_int("options_default initializes recursive", 0, opts.recursive);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wch_options_load_env.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wch_options_load_env(void) {
    const char *name = "kc_wch_options_load_env";
    const char *detail = "loads from environment";
    kc_wch_options_t opts;
    int fail = 0;

    opts = kc_wch_options_default();
    opts.recursive = 1;
    kc_wch_options_load_env(&opts);
    fail += expect_int("load_env preserves unmapped options", 1, opts.recursive);
    kc_wch_options_load_env(NULL);
    fail += expect_true("load_env accepts NULL", 1);
    kc_wch_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wch_options_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wch_options_free(void) {
    const char *name = "kc_wch_options_free";
    const char *detail = "clears resources";
    kc_wch_options_t opts;
    int fail = 0;

    opts = kc_wch_options_default();
    opts.recursive = 1;
    kc_wch_options_free(&opts);
    fail += expect_int("free preserves plain options", 1, opts.recursive);
    kc_wch_options_free(NULL);
    fail += expect_true("free accepts NULL", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wch_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wch_open(void) {
    const char *name = "kc_wch_open";
    const char *detail = "validates and allocates context";
    kc_wch_options_t opts;
    kc_wch_t *w;
    int fail = 0;

    opts = kc_wch_options_default();
    w = NULL;
    fail += expect_int("open rejects NULL out", KC_WCH_ERROR,
        kc_wch_open(NULL, "/tmp", &opts));
    fail += expect_int("open rejects NULL path", KC_WCH_ERROR,
        kc_wch_open(&w, NULL, &opts));
    fail += expect_int("open rejects NULL opts", KC_WCH_ERROR,
        kc_wch_open(&w, "/tmp", NULL));
    fail += expect_true("open leaves output unchanged on error", w == NULL);
    fail += expect_int("open rejects missing path", KC_WCH_ERROR,
        kc_wch_open(&w, "", &opts));
    fail += expect_int("open rejects nonexistent path", KC_WCH_ERROR,
        kc_wch_open(&w, "/nonexistent/path/xyz", &opts));
    fail += expect_int("open valid path", KC_WCH_OK,
        kc_wch_open(&w, "/tmp", &opts));
    fail += expect_true("open sets output", w != NULL);
    if (w != NULL) fail += expect_int("close opened context", KC_WCH_OK, kc_wch_close(w));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wch_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wch_close(void) {
    const char *name = "kc_wch_close";
    const char *detail = "releases context";
    kc_wch_options_t opts;
    kc_wch_t *w;
    int fail = 0;

    opts = kc_wch_options_default();
    w = NULL;
    fail += expect_int("close rejects NULL", KC_WCH_OK, kc_wch_close(NULL));
    fail += expect_int("open context for close", KC_WCH_OK, kc_wch_open(&w, "/tmp", &opts));
    fail += expect_int("close releases context", KC_WCH_OK, kc_wch_close(w));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wch_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wch_stop(void) {
    const char *name = "kc_wch_stop";
    const char *detail = "sets flag and is idempotent";
    kc_wch_options_t opts;
    kc_wch_t *w;
    int fail = 0;

    opts = kc_wch_options_default();
    w = NULL;
    fail += expect_int("stop rejects NULL", KC_WCH_ERROR, kc_wch_stop(NULL));
    fail += expect_int("open context for stop", KC_WCH_OK, kc_wch_open(&w, "/tmp", &opts));
    fail += expect_int("stop returns OK", KC_WCH_OK, kc_wch_stop(w));
    fail += expect_int("stop is idempotent", KC_WCH_OK, kc_wch_stop(w));
    fail += expect_int("close stop context", KC_WCH_OK, kc_wch_close(w));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wch_poll.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wch_poll(void) {
    const char *name = "kc_wch_poll";
    const char *detail = "reports timeout and rejects null";
    kc_wch_options_t opts;
    kc_wch_t *w;
    kc_wch_event_t ev;
    int fail = 0;

    opts = kc_wch_options_default();
    w = NULL;
    fail += expect_int("poll rejects NULL ctx", -1, kc_wch_poll(NULL, &ev, 0));
    fail += expect_int("open context for poll", KC_WCH_OK, kc_wch_open(&w, "/tmp", &opts));
    fail += expect_int("poll timeout=0 returns 0", 0, kc_wch_poll(w, &ev, 0));
    fail += expect_int("close poll context", KC_WCH_OK, kc_wch_close(w));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests that two contexts coexist independently.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wch_multictx(void) {
    const char *name = "kc_wch_multictx";
    const char *detail = "two contexts coexist independently";
    kc_wch_options_t opts;
    kc_wch_t *a;
    kc_wch_t *b;
    kc_wch_event_t ev;
    int fail = 0;

    opts = kc_wch_options_default();
    a = NULL;
    b = NULL;
    fail += expect_int("open first context", KC_WCH_OK, kc_wch_open(&a, "/tmp", &opts));
    fail += expect_int("open second context", KC_WCH_OK, kc_wch_open(&b, "/tmp", &opts));
    fail += expect_int("stop a returns OK", KC_WCH_OK, kc_wch_stop(a));
    fail += expect_true("poll b still usable after stop a", kc_wch_poll(b, &ev, 0) == 0);
    fail += expect_int("stop b returns OK", KC_WCH_OK, kc_wch_stop(b));
    fail += expect_int("close a returns OK", KC_WCH_OK, kc_wch_close(a));
    fail += expect_int("close b returns OK", KC_WCH_OK, kc_wch_close(b));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wch_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wch_version(void) {
    const char *name = "kc_wch_version";
    const char *detail = "returns non-zero build timestamp";
    int fail = expect_true("version returns non-zero build timestamp", kc_wch_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 9;
    test_case_current = 0;
    run_case(&rc, case_kc_wch_options_default);
    run_case(&rc, case_kc_wch_options_load_env);
    run_case(&rc, case_kc_wch_options_free);
    run_case(&rc, case_kc_wch_open);
    run_case(&rc, case_kc_wch_close);
    run_case(&rc, case_kc_wch_stop);
    run_case(&rc, case_kc_wch_poll);
    run_case(&rc, case_kc_wch_multictx);
    run_case(&rc, case_kc_wch_version);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one libwch contract test case.
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
    if (strcmp(argv[1], "kc_wch_options_default") == 0) return case_kc_wch_options_default();
    if (strcmp(argv[1], "kc_wch_options_load_env") == 0) return case_kc_wch_options_load_env();
    if (strcmp(argv[1], "kc_wch_options_free") == 0) return case_kc_wch_options_free();
    if (strcmp(argv[1], "kc_wch_open") == 0) return case_kc_wch_open();
    if (strcmp(argv[1], "kc_wch_close") == 0) return case_kc_wch_close();
    if (strcmp(argv[1], "kc_wch_stop") == 0) return case_kc_wch_stop();
    if (strcmp(argv[1], "kc_wch_poll") == 0) return case_kc_wch_poll();
    if (strcmp(argv[1], "kc_wch_multictx") == 0) return case_kc_wch_multictx();
    if (strcmp(argv[1], "kc_wch_version") == 0) return case_kc_wch_version();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
