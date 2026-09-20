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

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

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
 * Creates an empty directory for an isolated watcher test.
 * @param path Destination path buffer.
 * @param path_size Size of the destination buffer.
 * @return 1 on success, 0 on failure.
 */
static int make_test_dir(char *path, size_t path_size) {
#ifdef _WIN32
    char temp_path[MAX_PATH];

    if (path_size < MAX_PATH) return 0;
    if (GetTempPathA(sizeof(temp_path), temp_path) == 0) return 0;
    if (GetTempFileNameA(temp_path, "wch", 0, path) == 0) return 0;
    DeleteFileA(path);
    return CreateDirectoryA(path, NULL) != 0;
#else
    if (path_size < sizeof("/tmp/kc_wch_test_XXXXXX")) return 0;
    snprintf(path, path_size, "%s", "/tmp/kc_wch_test_XXXXXX");
    return mkdtemp(path) != NULL;
#endif
}

/**
 * Removes an empty isolated watcher test directory.
 * @param path Directory path.
 * @return None.
 */
static void remove_test_dir(const char *path) {
#ifdef _WIN32
    RemoveDirectoryA(path);
#else
    rmdir(path);
#endif
}

/**
 * Tests kc_wch_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wch_open(void) {
    const char *name = "kc_wch_open";
    const char *detail = "validates and allocates context";
    kc_wch_t *w;
    int fail = 0;

    w = NULL;
    fail += expect_int("open rejects NULL out", KC_WCH_ERROR,
        kc_wch_open(NULL, "/tmp", 0));
    fail += expect_int("open rejects NULL path", KC_WCH_ERROR,
        kc_wch_open(&w, NULL, 0));
    fail += expect_true("open leaves output NULL after NULL path", w == NULL);
    fail += expect_int("open rejects empty path", KC_WCH_ERROR,
        kc_wch_open(&w, "", 0));
    fail += expect_true("open leaves output NULL after empty path", w == NULL);
    fail += expect_int("open rejects nonexistent parent", KC_WCH_ERROR,
        kc_wch_open(&w, "/tmp/kc_wch_missing_parent_xyz/target", 0));
    fail += expect_true("open leaves output NULL after missing parent", w == NULL);
    fail += expect_int("open valid path", KC_WCH_OK,
        kc_wch_open(&w, "/tmp", 0));
    fail += expect_true("open sets output", w != NULL);
    kc_wch_close(w);
    w = NULL;
    fail += expect_int("open accepts recursive mode", KC_WCH_OK,
        kc_wch_open(&w, "/tmp", 1));
    fail += expect_true("recursive open sets output", w != NULL);
    kc_wch_close(w);
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
    kc_wch_t *w;
    int fail = 0;

    w = NULL;
    kc_wch_close(NULL);
    fail += expect_int("open context for close", KC_WCH_OK, kc_wch_open(&w, "/tmp", 0));
    fail += expect_true("open context for close sets output", w != NULL);
    kc_wch_close(w);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wch_poll.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_wch_poll(void) {
    const char *name = "kc_wch_poll";
    const char *detail = "reports timeout, events, and invalid arguments";
    kc_wch_t *w;
    kc_wch_event_t ev;
    char directory[512];
    char event_path[512];
#ifndef _WIN32
    FILE *file;
#endif
    int fail = 0;

    w = NULL;
    directory[0] = '\0';
    event_path[0] = '\0';
    fail += expect_int("poll rejects NULL context", KC_WCH_ERROR, kc_wch_poll(NULL, &ev, 0));
    fail += expect_true("create isolated poll directory", make_test_dir(directory, sizeof(directory)));
    fail += expect_int("open context for poll", KC_WCH_OK, kc_wch_open(&w, directory, 0));
    fail += expect_int("poll rejects NULL event", KC_WCH_ERROR, kc_wch_poll(w, NULL, 0));
    fail += expect_int("poll timeout=0 returns timeout", KC_WCH_TIMEOUT, kc_wch_poll(w, &ev, 0));
    fail += expect_int("timeout resets event type", -1, ev.type);
    fail += expect_true("timeout resets event path", ev.path == NULL);
    if (w != NULL && directory[0] != '\0') {
        snprintf(event_path, sizeof(event_path), "%s/event", directory);
#ifdef _WIN32
    HANDLE file_handle = CreateFileA(event_path, GENERIC_WRITE, 0, NULL,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        fail += expect_true("create watched file", file_handle != INVALID_HANDLE_VALUE);
        if (file_handle != INVALID_HANDLE_VALUE) CloseHandle(file_handle);
#else
        file = fopen(event_path, "w");
        fail += expect_true("create watched file", file != NULL);
        if (file != NULL) fclose(file);
#endif
        int event_rc = kc_wch_poll(w, &ev, 1000);
#ifdef _WIN32
        if (GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version") != NULL) {
            fail += expect_true("Wine poll reports event or timeout",
                event_rc == KC_WCH_EVENT || event_rc == KC_WCH_TIMEOUT);
        } else {
            fail += expect_int("poll reports real event", KC_WCH_EVENT, event_rc);
        }
#else
        fail += expect_int("poll reports real event", KC_WCH_EVENT, event_rc);
#endif
        if (event_rc == KC_WCH_EVENT) {
            fail += expect_true("real event sets path", ev.path != NULL);
        }
    }
    kc_wch_close(w);
    if (event_path[0] != '\0') remove(event_path);
    if (directory[0] != '\0') remove_test_dir(directory);
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
    kc_wch_t *a;
    kc_wch_t *b;
    kc_wch_event_t ev;
    char a_directory[512];
    char b_directory[512];
    int fail = 0;

    a = NULL;
    b = NULL;
    a_directory[0] = '\0';
    b_directory[0] = '\0';
    fail += expect_true("create first isolated directory",
        make_test_dir(a_directory, sizeof(a_directory)));
    fail += expect_true("create second isolated directory",
        make_test_dir(b_directory, sizeof(b_directory)));
    fail += expect_int("open first context", KC_WCH_OK, kc_wch_open(&a, a_directory, 0));
    fail += expect_int("open second context", KC_WCH_OK, kc_wch_open(&b, b_directory, 0));
    fail += expect_int("poll first context", KC_WCH_TIMEOUT, kc_wch_poll(a, &ev, 0));
    fail += expect_int("poll second context", KC_WCH_TIMEOUT, kc_wch_poll(b, &ev, 0));
    kc_wch_close(a);
    fail += expect_int("poll second context after first closes", KC_WCH_TIMEOUT,
        kc_wch_poll(b, &ev, 0));
    kc_wch_close(b);
    if (a_directory[0] != '\0') remove_test_dir(a_directory);
    if (b_directory[0] != '\0') remove_test_dir(b_directory);
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
    test_case_total = 5;
    test_case_current = 0;
    run_case(&rc, case_kc_wch_open);
    run_case(&rc, case_kc_wch_close);
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
    if (strcmp(argv[1], "kc_wch_open") == 0) return case_kc_wch_open();
    if (strcmp(argv[1], "kc_wch_close") == 0) return case_kc_wch_close();
    if (strcmp(argv[1], "kc_wch_poll") == 0) return case_kc_wch_poll();
    if (strcmp(argv[1], "kc_wch_multictx") == 0) return case_kc_wch_multictx();
    if (strcmp(argv[1], "kc_wch_version") == 0) return case_kc_wch_version();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
