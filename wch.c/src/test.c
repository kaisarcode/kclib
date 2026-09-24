/**
 * test.c - libwch public API contract tests.
 * Summary: Tests named resident watcher lifecycle and subscriptions.
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
#include <direct.h>
#include <process.h>
#include <windows.h>
#define getpid _getpid
#define mkdir_one(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define mkdir_one(path) mkdir(path, 0700)
#endif

#ifndef WCH_TEST_CLI
#define WCH_TEST_CLI ""
#endif

static int test_case_total;
static int test_case_current;

typedef int (*case_fn)(void);

typedef struct {
    atomic_int count;
    char path[1024];
} event_state_t;

/**
 * Print one top-level test result.
 * @param fail Failure count.
 * @param name Canonical case name.
 * @param detail Case detail.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    printf(
        "[%d/%d] [%s] %s: %s\n",
        test_case_current,
        test_case_total,
        fail ? "FAIL" : "PASS",
        name,
        detail
    );
}

/**
 * Run one top-level case.
 * @param rc Failure accumulator.
 * @param fn Case function.
 * @return None.
 */
static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

/**
 * Verify one integer result.
 * @param label Check label.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return Failure count.
 */
static int expect_int(const char *label, int expected, int actual) {
    if (expected != actual) {
        printf(
            "[FAIL] %s: expected %d, got %d\n",
            label,
            expected,
            actual
        );
        return 1;
    }
    return 0;
}

/**
 * Verify one condition.
 * @param label Check label.
 * @param condition Condition.
 * @return Failure count.
 */
static int expect_true(const char *label, int condition) {
    if (!condition) {
        printf("[FAIL] %s\n", label);
        return 1;
    }
    return 0;
}

/**
 * Verify one string result.
 * @param label Check label.
 * @param expected Expected string.
 * @param actual Actual string.
 * @return Failure count.
 */
static int expect_string(
    const char *label,
    const char *expected,
    const char *actual
) {
    if (actual == NULL || strcmp(expected, actual) != 0) {
        printf(
            "[FAIL] %s: expected '%s', got '%s'\n",
            label,
            expected,
            actual != NULL ? actual : "NULL"
        );
        return 1;
    }
    return 0;
}

/**
 * Sleep briefly for resident startup.
 * @param ms Milliseconds to sleep.
 * @return None.
 */
static void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec delay;

    delay.tv_sec = ms / 1000;
    delay.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&delay, NULL);
#endif
}

/**
 * Select the runtime directory used by public wch operations.
 * @param dir Runtime directory path.
 * @return Zero on success, nonzero on failure.
 */
static int use_runtime_dir(const char *dir) {
#ifdef _WIN32
    return _putenv_s("KC_WCH_DIR", dir) == 0 ? 0 : 1;
#else
    return setenv("KC_WCH_DIR", dir, 1) == 0 ? 0 : 1;
#endif
}

/**
 * Create one isolated test directory.
 * @param out Output path buffer.
 * @param cap Output buffer capacity.
 * @param name Test-specific directory name.
 * @return Zero on success, nonzero on failure.
 */
static int make_test_dir(
    char *out,
    size_t cap,
    const char *name
) {
    unsigned long suffix = 0U;
    size_t base_length;

#ifdef _WIN32
    char temp[MAX_PATH];
    DWORD size = GetTempPathA((DWORD)sizeof(temp), temp);

    if (size == 0 || size >= (DWORD)sizeof(temp)) return 1;
    if ((size_t)snprintf(
            out,
            cap,
            "%skc-wch-%ld-%s",
            temp,
            (long)getpid(),
            name
        ) >= cap) {
        return 1;
    }
#else
    const char *base = getenv("TMPDIR");

    if (base == NULL || base[0] == '\0') base = "/tmp";
    if ((size_t)snprintf(
            out,
            cap,
            "%s/kc-wch-%ld-%s",
            base,
            (long)getpid(),
            name
        ) >= cap) {
        return 1;
    }
#endif

    base_length = strlen(out);
    while (mkdir_one(out) != 0) {
        suffix++;
        if ((size_t)snprintf(
                out + base_length,
                cap - base_length,
                "-%lu",
                suffix
            ) >= cap - base_length) {
            return 1;
        }
        if (suffix > 1000U) return 1;
    }
    return use_runtime_dir(out);
}

/**
 * Remove one test directory.
 * @param path Directory path.
 * @return None.
 */
static void remove_test_dir(const char *path) {
#ifdef _WIN32
    (void)RemoveDirectoryA(path);
#else
    (void)rmdir(path);
#endif
}

/**
 * Build one watched child directory.
 * @param out Output path buffer.
 * @param cap Output buffer capacity.
 * @param root Parent test directory.
 * @return Zero on success, nonzero on failure.
 */
static int make_watched_dir(
    char *out,
    size_t cap,
    const char *root
) {
#ifdef _WIN32
    if ((size_t)snprintf(out, cap, "%s\\watched", root) >= cap) return 1;
#else
    if ((size_t)snprintf(out, cap, "%s/watched", root) >= cap) return 1;
#endif
    return mkdir_one(out) == 0 ? 0 : 1;
}

/**
 * Create one resident test watcher.
 * @param name Watcher name.
 * @param dir Runtime directory.
 * @param path Watched path.
 * @return wch status code.
 */
static int create_watcher(
    const char *name,
    const char *dir,
    const char *path
) {
    kc_wch_options_t options;

    memset(&options, 0, sizeof(options));
    options.path = path;
    (void)dir;
    options.cmd = "echo";
    options.recursive = 0;
    return kc_wch_create(name, &options);
}

/**
 * Open one test watcher in a selected runtime directory.
 * @param out Output watcher handle.
 * @param name Watcher name.
 * @param dir Runtime directory.
 * @return wch status code.
 */
static int open_watcher(
    kc_wch_t **out,
    const char *name,
    const char *dir
) {
    (void)dir;
    return kc_wch_open(out, name);
}

/**
 * Record one subscription event.
 * @param path Event path.
 * @param userdata Event state.
 * @return None.
 */
static void record_event(const char *path, void *userdata) {
    event_state_t *state = (event_state_t *)userdata;

    if (state == NULL || path == NULL) return;
    if (atomic_load(&state->count) != 0) return;
    snprintf(state->path, sizeof(state->path), "%s", path);
    atomic_store(&state->count, 1);
}

/**
 * Wait for one subscription event.
 * @param state Event state.
 * @return Nonzero when an event arrives.
 */
static int wait_event(event_state_t *state) {
    int i;

    for (i = 0; i < 200; i++) {
        if (atomic_load(&state->count) > 0) return 1;
        sleep_ms(10);
    }
    return 0;
}

/**
 * Configure the Windows companion executable for tests.
 * @return None.
 */
static void configure_test_cli(void) {
#ifdef _WIN32
    if (WCH_TEST_CLI[0] != '\0') {
        (void)_putenv_s("KC_WCH_EXE", WCH_TEST_CLI);
    }
#endif
}

/**
 * Test kc_wch_create.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_wch_create(void) {
    char dir[1024];
    char watched[1024];
    int fail = 0;

    configure_test_cli();
    if (make_test_dir(dir, sizeof(dir), "create") != 0) return 1;
    if (make_watched_dir(watched, sizeof(watched), dir) != 0) return 1;

    fail += expect_int(
        "invalid name rejected",
        KC_WCH_ERROR,
        create_watcher("../bad", dir, watched)
    );
    fail += expect_int(
        "create resident watcher",
        KC_WCH_OK,
        create_watcher("created", dir, watched)
    );
    sleep_ms(200);
    fail += expect_int(
        "delete resident watcher",
        KC_WCH_OK,
        kc_wch_delete("created")
    );

    remove_test_dir(watched);
    remove_test_dir(dir);
    case_result(fail, "kc_wch_create", "creates a resident named watcher");
    return fail != 0;
}

/**
 * Test kc_wch_open.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_wch_open(void) {
    kc_wch_t *watcher = (kc_wch_t *)(uintptr_t)1;
    int fail = 0;

    fail += expect_int(
        "NULL output rejected",
        KC_WCH_ERROR,
        kc_wch_open(NULL, "watcher")
    );
    fail += expect_int(
        "invalid name rejected",
        KC_WCH_ERROR,
        kc_wch_open(&watcher, "../bad")
    );
    fail += expect_true("failed output cleared", watcher == NULL);
    fail += expect_int(
        "logical handle opens",
        KC_WCH_OK,
        kc_wch_open(&watcher, "watcher")
    );
    kc_wch_close(watcher);

    case_result(fail, "kc_wch_open", "opens a local named watcher handle");
    return fail != 0;
}

/**
 * Test kc_wch_list.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_wch_list(void) {
    char dir[1024];
    char watched[1024];
    kc_wch_entry_t *entries = NULL;
    size_t count = 0U;
    size_t i;
    int found = 0;
    int fail = 0;

    configure_test_cli();
    if (make_test_dir(dir, sizeof(dir), "list") != 0) return 1;
    if (make_watched_dir(watched, sizeof(watched), dir) != 0) return 1;
    fail += expect_int(
        "create listed watcher",
        KC_WCH_OK,
        create_watcher("listed", dir, watched)
    );
    sleep_ms(200);
    fail += expect_int(
        "list succeeds",
        KC_WCH_OK,
        kc_wch_list(&entries, &count)
    );
    for (i = 0U; i < count; i++) {
        if (strcmp(entries[i].name, "listed") == 0) {
            found = strcmp(entries[i].path, watched) == 0 &&
                strcmp(entries[i].cmd, "echo") == 0;
            break;
        }
    }
    fail += expect_true("list contains watcher metadata", found);

    kc_wch_free(entries);
    (void)kc_wch_delete("listed");
    remove_test_dir(watched);
    remove_test_dir(dir);
    case_result(fail, "kc_wch_list", "returns owned resident watcher entries");
    return fail != 0;
}

/**
 * Test kc_wch_delete.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_wch_delete(void) {
    char dir[1024];
    int fail = 0;

    if (make_test_dir(dir, sizeof(dir), "delete") != 0) return 1;
    fail += expect_int(
        "missing delete succeeds",
        KC_WCH_OK,
        kc_wch_delete("missing")
    );
    remove_test_dir(dir);

    case_result(fail, "kc_wch_delete", "deletes resident watcher state");
    return fail != 0;
}

/**
 * Test kc_wch_set_path and kc_wch_get_path.
 * @return Zero on success, nonzero on failure.
 */
static int case_wch_path(const char *case_name) {
    char dir[1024];
    char watched[1024];
    char watched_two[1024];
    kc_wch_t *watcher = NULL;
    int fail = 0;

    configure_test_cli();
    if (make_test_dir(dir, sizeof(dir), "path") != 0) return 1;
    if (make_watched_dir(watched, sizeof(watched), dir) != 0) return 1;
#ifdef _WIN32
    if (strlen(dir) + sizeof("\\watched-two") > sizeof(watched_two)) {
        return 1;
    }
    memcpy(watched_two, dir, strlen(dir));
    memcpy(
        watched_two + strlen(dir),
        "\\watched-two",
        sizeof("\\watched-two")
    );
#else
    if (strlen(dir) + sizeof("/watched-two") > sizeof(watched_two)) {
        return 1;
    }
    memcpy(watched_two, dir, strlen(dir));
    memcpy(
        watched_two + strlen(dir),
        "/watched-two",
        sizeof("/watched-two")
    );
#endif
    if (mkdir_one(watched_two) != 0) return 1;

    fail += expect_int(
        "create watcher",
        KC_WCH_OK,
        create_watcher("path", dir, watched)
    );
    sleep_ms(200);
    fail += expect_int(
        "open watcher",
        KC_WCH_OK,
        open_watcher(&watcher, "path", dir)
    );
    if (watcher != NULL) {
        fail += expect_string(
            "get initial path",
            watched,
            kc_wch_get_path(watcher)
        );
        fail += expect_int(
            "set path",
            KC_WCH_OK,
            kc_wch_set_path(watcher, watched_two)
        );
        fail += expect_string(
            "get updated path",
            watched_two,
            kc_wch_get_path(watcher)
        );
    }

    kc_wch_close(watcher);
    (void)kc_wch_delete("path");
    remove_test_dir(watched);
    remove_test_dir(watched_two);
    remove_test_dir(dir);
    case_result(fail, case_name, "sets and gets the watched path");
    return fail != 0;
}

/**
 * Test kc_wch_set_path.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_wch_set_path(void) {
    return case_wch_path("kc_wch_set_path");
}

/**
 * Test kc_wch_get_path.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_wch_get_path(void) {
    return case_wch_path("kc_wch_get_path");
}

/**
 * Test kc_wch_set_cmd and kc_wch_get_cmd.
 * @return Zero on success, nonzero on failure.
 */
static int case_wch_cmd(const char *case_name) {
    char dir[1024];
    char watched[1024];
    kc_wch_t *watcher = NULL;
    int fail = 0;

    configure_test_cli();
    if (make_test_dir(dir, sizeof(dir), "cmd") != 0) return 1;
    if (make_watched_dir(watched, sizeof(watched), dir) != 0) return 1;
    fail += expect_int(
        "create watcher",
        KC_WCH_OK,
        create_watcher("cmd", dir, watched)
    );
    sleep_ms(200);
    fail += expect_int(
        "open watcher",
        KC_WCH_OK,
        open_watcher(&watcher, "cmd", dir)
    );
    if (watcher != NULL) {
        fail += expect_string("get initial command", "echo", kc_wch_get_cmd(watcher));
        fail += expect_int(
            "set command",
            KC_WCH_OK,
            kc_wch_set_cmd(watcher, "echo changed")
        );
        fail += expect_string(
            "get updated command",
            "echo changed",
            kc_wch_get_cmd(watcher)
        );
    }

    kc_wch_close(watcher);
    (void)kc_wch_delete("cmd");
    remove_test_dir(watched);
    remove_test_dir(dir);
    case_result(fail, case_name, "sets and gets the event command");
    return fail != 0;
}

/**
 * Test kc_wch_set_cmd.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_wch_set_cmd(void) {
    return case_wch_cmd("kc_wch_set_cmd");
}

/**
 * Test kc_wch_get_cmd.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_wch_get_cmd(void) {
    return case_wch_cmd("kc_wch_get_cmd");
}

/**
 * Test recursive setters and getters.
 * @return Zero on success, nonzero on failure.
 */
static int case_wch_recursive(const char *case_name) {
    char dir[1024];
    char watched[1024];
    kc_wch_t *watcher = NULL;
    int fail = 0;

    configure_test_cli();
    if (make_test_dir(dir, sizeof(dir), "recursive") != 0) return 1;
    if (make_watched_dir(watched, sizeof(watched), dir) != 0) return 1;
    fail += expect_int(
        "create watcher",
        KC_WCH_OK,
        create_watcher("recursive", dir, watched)
    );
    sleep_ms(200);
    fail += expect_int(
        "open watcher",
        KC_WCH_OK,
        open_watcher(&watcher, "recursive", dir)
    );
    if (watcher != NULL) {
        fail += expect_true(
            "initial recursive false",
            kc_wch_get_recursive(watcher) == 0
        );
        fail += expect_int(
            "set recursive",
            KC_WCH_OK,
            kc_wch_set_recursive(watcher, 1)
        );
        fail += expect_true(
            "updated recursive true",
            kc_wch_get_recursive(watcher) != 0
        );
    }

    kc_wch_close(watcher);
    (void)kc_wch_delete("recursive");
    remove_test_dir(watched);
    remove_test_dir(dir);
    case_result(
        fail, case_name,
        "sets and gets recursive observation mode"
    );
    return fail != 0;
}

/**
 * Test kc_wch_set_recursive.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_wch_set_recursive(void) {
    return case_wch_recursive("kc_wch_set_recursive");
}

/**
 * Test kc_wch_get_recursive.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_wch_get_recursive(void) {
    return case_wch_recursive("kc_wch_get_recursive");
}

/**
 * Test kc_wch_on.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_wch_on(void) {
    char dir[1024];
    char watched[1024];
    char event_path[1200];
    kc_wch_t *watcher = NULL;
    event_state_t state;
    int fail = 0;
#ifdef _WIN32
    HANDLE file;
#else
    FILE *file;
#endif

    configure_test_cli();
    atomic_init(&state.count, 0);
    state.path[0] = '\0';

    if (make_test_dir(dir, sizeof(dir), "on") != 0) return 1;
    if (make_watched_dir(watched, sizeof(watched), dir) != 0) return 1;
    {
        kc_wch_options_t options;

        memset(&options, 0, sizeof(options));
        options.path = watched;
#ifdef _WIN32
        options.cmd = "echo > NUL";
#else
        options.cmd = "echo > /dev/null";
#endif
        options.recursive = 0;
        fail += expect_int(
            "create watcher",
            KC_WCH_OK,
            kc_wch_create("events", &options)
        );
    }
    sleep_ms(250);
    fail += expect_int(
        "open watcher",
        KC_WCH_OK,
        open_watcher(&watcher, "events", dir)
    );
    if (watcher != NULL) {
        fail += expect_int(
            "unknown event rejected",
            KC_WCH_ERROR,
            kc_wch_on(watcher, "unknown", record_event, &state)
        );
        fail += expect_int(
            "add subscription",
            KC_WCH_OK,
            kc_wch_on(watcher, "add", record_event, &state)
        );
    }

#ifdef _WIN32
    snprintf(event_path, sizeof(event_path), "%s\\event", watched);
    file = CreateFileA(
        event_path,
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    fail += expect_true("create watched file", file != INVALID_HANDLE_VALUE);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (GetProcAddress(
            GetModuleHandleA("ntdll.dll"),
            "wine_get_version"
        ) != NULL) {
        (void)wait_event(&state);
    } else {
        fail += expect_true("subscription receives event", wait_event(&state));
    }
#else
    snprintf(event_path, sizeof(event_path), "%s/event", watched);
    file = fopen(event_path, "w");
    fail += expect_true("create watched file", file != NULL);
    if (file != NULL) fclose(file);
    fail += expect_true("subscription receives event", wait_event(&state));
#endif

    if (atomic_load(&state.count) > 0) {
        fail += expect_true(
            "subscription path",
            strstr(state.path, "event") != NULL
        );
    }

    kc_wch_close(watcher);
    (void)kc_wch_delete("events");
    (void)remove(event_path);
    remove_test_dir(watched);
    remove_test_dir(dir);
    case_result(fail, "kc_wch_on", "subscribes to resident watcher events");
    return fail != 0;
}

/**
 * Test kc_wch_free.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_wch_free(void) {
    kc_wch_entry_t *entries = NULL;
    size_t count = 0U;
    int fail = expect_int(
        "empty default list succeeds",
        KC_WCH_OK,
        kc_wch_list(NULL, &entries, &count)
    );

    kc_wch_free(entries);
    kc_wch_free(NULL);
    case_result(fail, "kc_wch_free", "releases API-owned allocations");
    return fail != 0;
}

/**
 * Test kc_wch_close.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_wch_close(void) {
    kc_wch_t *watcher = NULL;
    int fail = expect_int(
        "open handle",
        KC_WCH_OK,
        kc_wch_open(&watcher, "close")
    );

    kc_wch_close(watcher);
    kc_wch_close(NULL);
    case_result(fail, "kc_wch_close", "releases local watcher handles");
    return fail != 0;
}

/**
 * Test kc_wch_version.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_wch_version(void) {
    int fail = expect_true("version nonzero", kc_wch_version() != 0U);

    case_result(fail, "kc_wch_version", "returns the generated build version");
    return fail != 0;
}

/**
 * Test the grouped CLI contract.
 * @return Zero on success, nonzero on failure.
 */
static int case_kc_wch_cli(void) {
    char command[4096];
    int fail = 0;
    int rc;

    if (WCH_TEST_CLI[0] == '\0') {
        case_result(1, "kc_wch_cli", "covers stable CLI parsing");
        return 1;
    }

#ifdef _WIN32
    snprintf(
        command,
        sizeof(command),
        "\"%s\" --help > NUL 2>&1",
        WCH_TEST_CLI
    );
#else
    snprintf(
        command,
        sizeof(command),
        "\"%s\" --help > /dev/null 2>&1",
        WCH_TEST_CLI
    );
#endif
    rc = system(command);
    fail += expect_true("CLI help succeeds", rc == 0);

#ifdef _WIN32
    snprintf(
        command,
        sizeof(command),
        "\"%s\" --version > NUL 2>&1",
        WCH_TEST_CLI
    );
#else
    snprintf(
        command,
        sizeof(command),
        "\"%s\" --version > /dev/null 2>&1",
        WCH_TEST_CLI
    );
#endif
    rc = system(command);
    fail += expect_true("CLI version succeeds", rc == 0);

    case_result(fail, "kc_wch_cli", "covers stable CLI parsing");
    return fail != 0;
}

/**
 * Run all public contract cases.
 * @return Failed case count.
 */
static int case_all(void) {
    int rc = 0;

    test_case_total = 15;
    test_case_current = 0;
    run_case(&rc, case_kc_wch_create);
    run_case(&rc, case_kc_wch_open);
    run_case(&rc, case_kc_wch_list);
    run_case(&rc, case_kc_wch_delete);
    run_case(&rc, case_kc_wch_set_path);
    run_case(&rc, case_kc_wch_get_path);
    run_case(&rc, case_kc_wch_set_cmd);
    run_case(&rc, case_kc_wch_get_cmd);
    run_case(&rc, case_kc_wch_set_recursive);
    run_case(&rc, case_kc_wch_get_recursive);
    run_case(&rc, case_kc_wch_on);
    run_case(&rc, case_kc_wch_free);
    run_case(&rc, case_kc_wch_close);
    run_case(&rc, case_kc_wch_version);
    run_case(&rc, case_kc_wch_cli);

    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Run one named contract test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(
            stderr,
            "test case: expected one argument, got %d\n",
            argc - 1
        );
        return 2;
    }

    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_wch_create") == 0) return case_kc_wch_create();
    if (strcmp(argv[1], "kc_wch_open") == 0) return case_kc_wch_open();
    if (strcmp(argv[1], "kc_wch_list") == 0) return case_kc_wch_list();
    if (strcmp(argv[1], "kc_wch_delete") == 0) return case_kc_wch_delete();
    if (strcmp(argv[1], "kc_wch_set_path") == 0) return case_kc_wch_set_path();
    if (strcmp(argv[1], "kc_wch_get_path") == 0) return case_kc_wch_get_path();
    if (strcmp(argv[1], "kc_wch_set_cmd") == 0) return case_kc_wch_set_cmd();
    if (strcmp(argv[1], "kc_wch_get_cmd") == 0) return case_kc_wch_get_cmd();
    if (strcmp(argv[1], "kc_wch_set_recursive") == 0) return case_kc_wch_set_recursive();
    if (strcmp(argv[1], "kc_wch_get_recursive") == 0) return case_kc_wch_get_recursive();
    if (strcmp(argv[1], "kc_wch_on") == 0) return case_kc_wch_on();
    if (strcmp(argv[1], "kc_wch_free") == 0) return case_kc_wch_free();
    if (strcmp(argv[1], "kc_wch_close") == 0) return case_kc_wch_close();
    if (strcmp(argv[1], "kc_wch_version") == 0) return case_kc_wch_version();
    if (strcmp(argv[1], "kc_wch_cli") == 0) return case_kc_wch_cli();

    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
