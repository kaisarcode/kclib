/**
 * test.c - libwch public API contract tests.
 * Summary: Validates the asynchronous file watcher API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libwch.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif

static int test_case_total = 0;
static int test_case_current = 0;

typedef struct {
    atomic_int count;
    int type;
    char path[512];
} event_state_t;

/**
 * Prints one canonical test-case result line.
 * @param fail Nonzero when the case failed.
 * @param name Canonical test-case name.
 * @param description Human-readable test-case description.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *description) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, description);
}

typedef int (*case_fn)(void);

/**
 * Executes one test case and accumulates its result.
 * @param rc Aggregate failed-case count.
 * @param fn Test-case function to execute.
 * @return None.
 */
static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

/**
 * Verifies one integer expectation.
 * @param name Expectation description.
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
 * Verifies one boolean expectation.
 * @param name Expectation description.
 * @param condition Nonzero when satisfied.
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
 * Sleeps for a small number of milliseconds.
 * @param ms Milliseconds to sleep.
 * @return None.
 */
static void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

/**
 * Creates an empty isolated directory.
 * @param path Destination path buffer.
 * @param path_size Destination buffer size.
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
 * Removes one isolated directory.
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
 * Records one callback event.
 * @param event Watcher event.
 * @param userdata Event-state pointer.
 * @return None.
 */
static void record_event(const kc_wch_event_t *event, void *userdata) {
    event_state_t *state = (event_state_t *)userdata;

    if (atomic_load(&state->count) != 0) return;
    state->type = event->type;
    snprintf(state->path, sizeof(state->path), "%s", event->path);
    atomic_store(&state->count, 1);
}

/**
 * Waits until at least one callback event arrives.
 * @param state Event-state pointer.
 * @return 1 when an event arrived, otherwise 0.
 */
static int wait_event(event_state_t *state) {
    int i;

    for (i = 0; i < 100; i++) {
        if (atomic_load(&state->count) > 0) return 1;
        sleep_ms(10);
    }
    return 0;
}

/**
 * Tests kc_wch_open.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_wch_open(void) {
    char directory[512];
    kc_wch_options_t options = { 1 };
    kc_wch_t *w = NULL;
    int fail = 0;

    fail += expect_int("open rejects NULL out", KC_WCH_ERROR,
        kc_wch_open(NULL, ".", NULL));
    fail += expect_int("open rejects NULL path", KC_WCH_ERROR,
        kc_wch_open(&w, NULL, NULL));
    fail += expect_int("open rejects empty path", KC_WCH_ERROR,
        kc_wch_open(&w, "", NULL));

    if (!make_test_dir(directory, sizeof(directory))) return 1;
    fail += expect_int("open default options", KC_WCH_OK,
        kc_wch_open(&w, directory, NULL));
    fail += expect_true("open returns watcher", w != NULL);
    kc_wch_close(w);
    w = NULL;

    fail += expect_int("open recursive options", KC_WCH_OK,
        kc_wch_open(&w, directory, &options));
    fail += expect_true("recursive open returns watcher", w != NULL);
    kc_wch_close(w);
    remove_test_dir(directory);

    case_result(fail, "kc_wch_open",
        "opens watchers with optional recursive configuration");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wch_on.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_wch_on(void) {
    char directory[512];
    char event_path[520];
    kc_wch_t *w = NULL;
    event_state_t state;
    int fail = 0;
#ifdef _WIN32
    HANDLE file;
#else
    FILE *file;
#endif

    atomic_init(&state.count, 0);
    state.type = -1;
    state.path[0] = '\0';

    if (!make_test_dir(directory, sizeof(directory))) return 1;
    if (kc_wch_open(&w, directory, NULL) != KC_WCH_OK) {
        remove_test_dir(directory);
        return 1;
    }

    fail += expect_int("on rejects NULL watcher", KC_WCH_ERROR,
        kc_wch_on(NULL, record_event, &state));
    fail += expect_int("on rejects NULL handler", KC_WCH_ERROR,
        kc_wch_on(w, NULL, &state));
    fail += expect_int("on starts watcher", KC_WCH_OK,
        kc_wch_on(w, record_event, &state));
    fail += expect_int("on rejects second handler", KC_WCH_ERROR,
        kc_wch_on(w, record_event, &state));

    snprintf(event_path, sizeof(event_path), "%s/event", directory);
#ifdef _WIN32
    file = CreateFileA(event_path, GENERIC_WRITE, 0, NULL,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    fail += expect_true("create watched file", file != INVALID_HANDLE_VALUE);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
#else
    file = fopen(event_path, "w");
    fail += expect_true("create watched file", file != NULL);
    if (file != NULL) fclose(file);
#endif

    fail += expect_true("callback receives event", wait_event(&state));
    if (atomic_load(&state.count) > 0) {
        fail += expect_true("callback receives path",
            strstr(state.path, "event") != NULL);
        fail += expect_true("callback receives normalized type",
            state.type == KC_WCH_ADD || state.type == KC_WCH_UPD);
    }

    kc_wch_close(w);
    remove(event_path);
    remove_test_dir(directory);

    case_result(fail, "kc_wch_on",
        "starts background observation and delivers normalized events");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wch_close.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_wch_close(void) {
    char directory[512];
    kc_wch_t *w = NULL;
    int fail = 0;

    kc_wch_close(NULL);
    if (!make_test_dir(directory, sizeof(directory))) return 1;

    fail += expect_int("open watcher for close", KC_WCH_OK,
        kc_wch_open(&w, directory, NULL));
    kc_wch_close(w);
    remove_test_dir(directory);

    case_result(fail, "kc_wch_close",
        "releases idle watcher instances and accepts NULL");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_wch_version.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_wch_version(void) {
    int fail = expect_true("version nonzero", kc_wch_version() != 0U);

    case_result(fail, "kc_wch_version",
        "returns a nonzero generated build version");
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all public contract cases.
 * @return Failed case count.
 */
static int case_all(void) {
    int rc = 0;

    test_case_total = 4;
    test_case_current = 0;

    run_case(&rc, case_kc_wch_open);
    run_case(&rc, case_kc_wch_on);
    run_case(&rc, case_kc_wch_close);
    run_case(&rc, case_kc_wch_version);

    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Dispatches one named test case.
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
    if (strcmp(argv[1], "kc_wch_open") == 0) return case_kc_wch_open();
    if (strcmp(argv[1], "kc_wch_on") == 0) return case_kc_wch_on();
    if (strcmp(argv[1], "kc_wch_close") == 0) return case_kc_wch_close();
    if (strcmp(argv[1], "kc_wch_version") == 0) return case_kc_wch_version();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
