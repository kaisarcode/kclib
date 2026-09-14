/**
 * test.c - libdmn public API tests.
 * Summary: Tests each public libdmn function through one CTest case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libdmn.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#define getpid _getpid
#define mkdir_one(path) _mkdir(path)
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define mkdir_one(path) mkdir(path, 0700)
#endif

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

/**
 * Prints a skipped test case line.
 * @param name Public API function under test.
 * @param detail Why the case is skipped.
 * @return None.
 */
#ifndef _WIN32
static void case_skip(const char *name, const char *detail) {
    printf("[%d/%d] [SKIP] %s: %s\n", test_case_current, test_case_total,
        name, detail);
}
#endif

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
 * Describes list callback observations.
 */
typedef struct {
    const char *expected_key;
    int count;
    int exact_count;
    char first_sock[512];
} list_state_t;

/**
 * Records one list callback entry.
 * @param key Daemon key name.
 * @param sock Socket or pipe path.
 * @param userdata List state pointer.
 * @return None.
 */
static void record_list(const char *key, const char *sock, void *userdata) {
    list_state_t *state;

    state = (list_state_t *)userdata;
    if (state == NULL || key == NULL || sock == NULL) return;
    state->count++;
    if (state->count == 1) {
        snprintf(state->first_sock, sizeof(state->first_sock), "%s", sock);
    }
    if (state->expected_key != NULL && strcmp(key, state->expected_key) == 0) {
        state->exact_count++;
    }
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
 * Verifies one string result.
 * @param name Check description.
 * @param expected Expected string.
 * @param actual Actual string.
 * @return 0 on success, 1 on failure.
 */
static int expect_string(const char *name, const char *expected, const char *actual) {
    if (actual == NULL || strcmp(expected, actual) != 0) {
        printf("[FAIL] %s: expected '%s', got '%s'\n", name, expected,
            actual != NULL ? actual : "NULL");
        return 1;
    }
    return 0;
}

/**
 * Sleeps briefly to let daemon processes reach readiness.
 * @return None.
 */
static void short_sleep(void) {
#ifdef _WIN32
    Sleep(200);
#else
    struct timespec ts;

    ts.tv_sec = 0;
    ts.tv_nsec = 200000000L;
    nanosleep(&ts, NULL);
#endif
}

/**
 * Copies one string into caller-owned memory.
 * @param text Input string.
 * @return Allocated copy, or NULL on failure.
 */
static char *copy_string(const char *text) {
    char *copy;
    size_t length;

    length = strlen(text) + 1;
    copy = (char *)malloc(length);
    if (copy == NULL) return NULL;
    memcpy(copy, text, length);
    return copy;
}

/**
 * Sets or clears a process environment variable.
 * @param name Variable name.
 * @param value Variable value, or NULL to clear.
 * @return 0 on success, 1 on failure.
 */
static int set_env_value(const char *name, const char *value) {
#ifdef _WIN32
    return _putenv_s(name, value != NULL ? value : "") == 0 ? 0 : 1;
#else
    if (value == NULL) return unsetenv(name) == 0 ? 0 : 1;
    return setenv(name, value, 1) == 0 ? 0 : 1;
#endif
}

/**
 * Creates a unique runtime directory path for one test case.
 * @param out Output buffer.
 * @param cap Output buffer capacity.
 * @param name Case name.
 * @return 0 on success, 1 on failure.
 */
static int make_runtime_dir(char *out, size_t cap, const char *name) {
    const char *base;

#ifdef _WIN32
    char tmp[MAX_PATH];

    if (GetTempPathA((DWORD)sizeof(tmp), tmp) == 0) return 1;
    base = tmp;
    if ((size_t)snprintf(out, cap, "%skc-dmn-test-%ld-%s",
            base, (long)getpid(), name) >= cap) return 1;
#else
    base = getenv("TMPDIR");
    if (base == NULL || base[0] == '\0') base = "/tmp";
    if ((size_t)snprintf(out, cap, "%s/kc-dmn-test-%ld-%s",
            base, (long)getpid(), name) >= cap) return 1;
#endif
    (void)mkdir_one(out);
    return 0;
}

/**
 * Opens a context using an isolated runtime directory.
 * @param out Destination context pointer.
 * @param dir Runtime directory output.
 * @param cap Runtime directory capacity.
 * @param name Case name.
 * @return 0 on success, 1 on failure.
 */
static int open_context(kc_dmn_t **out, char *dir, size_t cap, const char *name) {
    kc_dmn_options_t opts;

    if (make_runtime_dir(dir, cap, name) != 0) return 1;
    opts = kc_dmn_options_default();
    opts.dir = copy_string(dir);
    if (opts.dir == NULL) return 1;
    if (kc_dmn_open(out, &opts) != KC_DMN_OK) {
        kc_dmn_options_free(&opts);
        return 1;
    }
    kc_dmn_options_free(&opts);
    return 0;
}

#ifndef _WIN32
/**
 * Relays one input string through a daemon while capturing stdout.
 * @param ctx Context pointer.
 * @param key Daemon key.
 * @param input Input text.
 * @param out Output buffer.
 * @param cap Output buffer capacity.
 * @return KC_DMN_OK on success, or KC_DMN_ERROR on failure.
 */
static int relay_capture(kc_dmn_t *ctx, const char *key, const char *input, char *out, size_t cap) {
    int handle = -1;
    size_t input_len;
    int n;

    if (kc_dmn_connect(ctx, key, &handle) != KC_DMN_OK) return KC_DMN_ERROR;
    input_len = strlen(input);
    if (input_len > 0 && kc_dmn_send(handle, input, input_len) != KC_DMN_OK) {
        kc_dmn_disconnect(handle);
        return KC_DMN_ERROR;
    }
    n = kc_dmn_recv(handle, out, cap - 1);
    if (n < 0) n = 0;
    out[n] = '\0';
    kc_dmn_disconnect(handle);
    return KC_DMN_OK;
}
#endif

/**
 * Tests kc_dmn_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_dmn_version(void) {
    const char *name = "kc_dmn_version";
    const char *detail = "version returns build timestamp";
    int fail = expect_true("version returns non-zero build timestamp", kc_dmn_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_dmn_options_default.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_dmn_options_default(void) {
    const char *name = "kc_dmn_options_default";
    const char *detail = "default options initialize correctly";
    kc_dmn_options_t opts;

    opts = kc_dmn_options_default();
    int fail = expect_true("default options set dir to NULL", opts.dir == NULL);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_dmn_options_load_env.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_dmn_options_load_env(void) {
    const char *name = "kc_dmn_options_load_env";
    const char *detail = "options load from environment";
    kc_dmn_options_t opts;
    int fail;

    fail = 0;
    opts = kc_dmn_options_default();
    set_env_value("KC_DMN_DIR", NULL);
    kc_dmn_options_load_env(&opts);
    fail += expect_true("load_env leaves dir NULL when KC_DMN_DIR unset", opts.dir == NULL);
    set_env_value("KC_DMN_DIR", "/tmp/kc-dmn-env-test");
    kc_dmn_options_load_env(&opts);
    fail += expect_true("load_env sets dir", opts.dir != NULL);
    fail += expect_string("load_env reads KC_DMN_DIR", "/tmp/kc-dmn-env-test", opts.dir);
    set_env_value("KC_DMN_DIR", "/tmp/kc-dmn-env-test-2");
    kc_dmn_options_load_env(&opts);
    fail += expect_string("load_env replaces previous dir", "/tmp/kc-dmn-env-test-2", opts.dir);
    set_env_value("KC_DMN_DIR", NULL);
    kc_dmn_options_load_env(NULL);
    fail += expect_true("load_env(NULL) does not crash", 1);
    kc_dmn_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_dmn_options_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_dmn_options_free(void) {
    const char *name = "kc_dmn_options_free";
    const char *detail = "options free clears resources";
    kc_dmn_options_t opts;
    int fail;

    fail = 0;
    opts = kc_dmn_options_default();
    opts.dir = copy_string("/tmp/owned");
    fail += expect_true("copy option dir", opts.dir != NULL);
    kc_dmn_options_free(&opts);
    fail += expect_true("options_free clears dir", opts.dir == NULL);
    kc_dmn_options_free(&opts);
    fail += expect_true("options_free is idempotent", opts.dir == NULL);
    kc_dmn_options_free(NULL);
    fail += expect_true("options_free(NULL) does not crash", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_dmn_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_dmn_open(void) {
    const char *name = "kc_dmn_open";
    const char *detail = "open validates and allocates context";
    kc_dmn_options_t opts;
    kc_dmn_t *ctx;
    int fail;

    fail = 0;
    ctx = NULL;
    opts = kc_dmn_options_default();
    fail += expect_int("open(NULL, opts) returns ERROR", KC_DMN_ERROR,
        kc_dmn_open(NULL, &opts));
    fail += expect_int("open(out, NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_open(&ctx, NULL));
    fail += expect_true("open error leaves context NULL", ctx == NULL);
    if (open_context(&ctx, NULL, 0, "open") == 0) {
        fail += expect_true("open creates valid context", ctx != NULL);
        kc_dmn_close(ctx);
    }
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_dmn_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_dmn_close(void) {
    const char *name = "kc_dmn_close";
    const char *detail = "close releases context";
    kc_dmn_t *ctx;
    char dir[512];
    int fail;

    fail = 0;
    kc_dmn_close(NULL);
    fail += expect_true("close(NULL) does not crash", 1);
    if (open_context(&ctx, dir, sizeof(dir), "close") != 0) return 1;
    kc_dmn_close(ctx);
    fail += expect_true("close releases context", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_dmn_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_dmn_stop(void) {
    const char *name = "kc_dmn_stop";
    const char *detail = "stop sets flag on context";
    kc_dmn_t *ctx;
    kc_dmn_t *other;
    char dir_a[512];
    char dir_b[512];
    int fail;

    fail = 0;
    fail += expect_int("stop(NULL) returns ERROR", KC_DMN_ERROR, kc_dmn_stop(NULL));
    if (open_context(&ctx, dir_a, sizeof(dir_a), "stop-a") != 0) return 1;
    if (open_context(&other, dir_b, sizeof(dir_b), "stop-b") != 0) {
        kc_dmn_close(ctx);
        return 1;
    }
    fail += expect_int("stop context succeeds", KC_DMN_OK, kc_dmn_stop(ctx));
    fail += expect_int("stop is idempotent", KC_DMN_OK, kc_dmn_stop(ctx));
    fail += expect_int("other context still operates", KC_DMN_OK,
        kc_dmn_delete(other, "missing"));
    fail += expect_int("stop other context", KC_DMN_OK, kc_dmn_stop(other));
    kc_dmn_close(ctx);
    kc_dmn_close(other);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_dmn_path.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_dmn_path(void) {
    const char *name = "kc_dmn_path";
    const char *detail = "path returns configured directory";
    kc_dmn_t *ctx;
    char dir[512];
    int fail;

    fail = 0;
    fail += expect_true("path(NULL) returns NULL", kc_dmn_path(NULL) == NULL);
    if (open_context(&ctx, dir, sizeof(dir), "path") != 0) return 1;
    fail += expect_string("path returns configured dir", dir, kc_dmn_path(ctx));
    kc_dmn_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_dmn_update.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_dmn_update(void) {
    const char *name = "kc_dmn_update";
    const char *detail = "update registers and replaces daemons";
    kc_dmn_t *ctx;
    char dir[512];
    int fail;

    fail = 0;
    fail += expect_int("update(NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_update(NULL, "key", "cat"));
    if (open_context(&ctx, dir, sizeof(dir), "update") != 0) return 1;
    fail += expect_int("update(ctx, NULL, cmd) returns ERROR", KC_DMN_ERROR,
        kc_dmn_update(ctx, NULL, "cat"));
    fail += expect_int("update(ctx, key, NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_update(ctx, "key", NULL));
    fail += expect_int("update key returns OK", KC_DMN_OK,
        kc_dmn_update(ctx, "update", "cat"));
    short_sleep();
    fail += expect_int("replace key returns OK", KC_DMN_OK,
        kc_dmn_update(ctx, "update", "echo replaced"));
    short_sleep();
    fail += expect_int("update still registered after replace", KC_DMN_OK,
        kc_dmn_delete(ctx, "update"));
    kc_dmn_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_dmn_delete.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_dmn_delete(void) {
    const char *name = "kc_dmn_delete";
    const char *detail = "delete removes daemons from registry";
    kc_dmn_t *ctx;
    list_state_t state;
    char dir[512];
    int fail;

    fail = 0;
    fail += expect_int("delete(NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_delete(NULL, "key"));
    if (open_context(&ctx, dir, sizeof(dir), "delete") != 0) return 1;
    fail += expect_int("delete(ctx, NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_delete(ctx, NULL));
    fail += expect_int("delete missing key returns OK", KC_DMN_OK,
        kc_dmn_delete(ctx, "missing"));
    fail += expect_int("update key for delete test", KC_DMN_OK,
        kc_dmn_update(ctx, "delme", "cat"));
    short_sleep();
    fail += expect_int("delete existing key returns OK", KC_DMN_OK,
        kc_dmn_delete(ctx, "delme"));
    memset(&state, 0, sizeof(state));
    state.expected_key = "delme";
    fail += expect_int("list deleted key returns OK", KC_DMN_OK,
        kc_dmn_list(ctx, "delme", record_list, &state));
    fail += expect_int("deleted key is not listed", 0, state.exact_count);
    kc_dmn_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_dmn_list.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_dmn_list(void) {
    const char *name = "kc_dmn_list";
    const char *detail = "list queries registered daemons";
    kc_dmn_t *ctx;
    list_state_t state;
    char dir[512];
    int fail;

    fail = 0;
    fail += expect_int("list(NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_list(NULL, NULL, NULL, NULL));
    if (open_context(&ctx, dir, sizeof(dir), "list") != 0) return 1;
    fail += expect_int("list missing key returns OK", KC_DMN_OK,
        kc_dmn_list(ctx, "missing", NULL, NULL));
    fail += expect_int("list with NULL callback returns OK", KC_DMN_OK,
        kc_dmn_list(ctx, NULL, NULL, NULL));
    fail += expect_int("update key for list test", KC_DMN_OK,
        kc_dmn_update(ctx, "listed", "cat"));
    {
        int tries;
        for (tries = 0; tries < 10; tries++) {
            memset(&state, 0, sizeof(state));
            state.expected_key = "listed";
            if (kc_dmn_list(ctx, "listed", record_list, &state) == KC_DMN_OK && state.exact_count >= 1) break;
            short_sleep();
        }
    }
    memset(&state, 0, sizeof(state));
    state.expected_key = "listed";
    fail += expect_int("list exact key returns OK", KC_DMN_OK,
        kc_dmn_list(ctx, "listed", record_list, &state));
    fail += expect_int("list exact key invokes callback", 1, state.exact_count);
    fail += expect_true("list callback provides socket", state.first_sock[0] != '\0');
    memset(&state, 0, sizeof(state));
    state.expected_key = "listed";
    fail += expect_int("list all returns OK", KC_DMN_OK,
        kc_dmn_list(ctx, NULL, record_list, &state));
    fail += expect_true("list all includes listed key", state.exact_count >= 1);
    fail += expect_int("delete listed key", KC_DMN_OK,
        kc_dmn_delete(ctx, "listed"));
    kc_dmn_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_dmn_connect.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_dmn_connect(void) {
    const char *name = "kc_dmn_connect";
    const char *detail = "connect acquires daemon handle";
#ifdef _WIN32
    kc_dmn_t *ctx;
    char dir[512];
    int handle;
    int fail;

    fail = 0;
    fail += expect_int("connect(NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_connect(NULL, "key", &handle));
    if (open_context(&ctx, dir, sizeof(dir), "connect") != 0) return 1;
    fail += expect_int("connect(ctx, NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_connect(ctx, NULL, &handle));
    fail += expect_int("connect(ctx, key, NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_connect(ctx, "key", NULL));
    fail += expect_int("connect missing key returns ERROR", KC_DMN_ERROR,
        kc_dmn_connect(ctx, "missing", &handle));
    kc_dmn_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
#else
    kc_dmn_t *ctx;
    char dir[512];
    char out[512];
    int handle;
    int fail;

    fail = 0;
    fail += expect_int("connect(NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_connect(NULL, "key", &handle));
    if (open_context(&ctx, dir, sizeof(dir), "connect") != 0) return 1;
    fail += expect_int("connect(ctx, NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_connect(ctx, NULL, &handle));
    fail += expect_int("connect(ctx, key, NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_connect(ctx, "key", NULL));
    fail += expect_int("connect missing key returns ERROR", KC_DMN_ERROR,
        kc_dmn_connect(ctx, "missing", &handle));
    fail += expect_int("update connect daemon returns OK", KC_DMN_OK,
        kc_dmn_update(ctx, "connect", "while IFS= read -r l; do printf '%s\\004' \"$l\"; done"));
    short_sleep();
    memset(out, 0, sizeof(out));
    fail += expect_int("connect existing daemon returns OK", KC_DMN_OK,
        kc_dmn_connect(ctx, "connect", &handle));
    fail += expect_int("connect returns valid handle", 1, handle >= 0);
    fail += expect_int("disconnect releases handle", KC_DMN_OK,
        kc_dmn_disconnect(handle));
    fail += expect_int("delete connect daemon returns OK", KC_DMN_OK,
        kc_dmn_delete(ctx, "connect"));
    kc_dmn_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
#endif
}

/**
 * Tests kc_dmn_send.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_dmn_send(void) {
    const char *name = "kc_dmn_send";
    const char *detail = "send writes data to daemon";
#ifdef _WIN32
    int fail;

    fail = 0;
    fail += expect_int("send bad handle returns ERROR", KC_DMN_ERROR,
        kc_dmn_send(-1, "x", 1));
    fail += expect_int("send NULL data returns ERROR", KC_DMN_ERROR,
        kc_dmn_send(0, NULL, 1));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
#else
    kc_dmn_t *ctx;
    char dir[512];
    int handle;
    int fail;

    fail = 0;
    fail += expect_int("send bad handle returns ERROR", KC_DMN_ERROR,
        kc_dmn_send(-1, "x", 1));
    fail += expect_int("send NULL data returns ERROR", KC_DMN_ERROR,
        kc_dmn_send(0, NULL, 1));
    if (open_context(&ctx, dir, sizeof(dir), "send") != 0) return 1;
    fail += expect_int("update send daemon returns OK", KC_DMN_OK,
        kc_dmn_update(ctx, "send", "while IFS= read -r l; do printf '%s\\004' \"$l\"; done"));
    short_sleep();
    fail += expect_int("connect send daemon returns OK", KC_DMN_OK,
        kc_dmn_connect(ctx, "send", &handle));
    fail += expect_int("send zero length returns OK", KC_DMN_OK,
        kc_dmn_send(handle, NULL, 0));
    fail += expect_int("send data returns OK", KC_DMN_OK,
        kc_dmn_send(handle, "hello\n", 6));
    fail += expect_int("disconnect releases handle", KC_DMN_OK,
        kc_dmn_disconnect(handle));
    fail += expect_int("delete send daemon returns OK", KC_DMN_OK,
        kc_dmn_delete(ctx, "send"));
    kc_dmn_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
#endif
}

/**
 * Tests kc_dmn_recv.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_dmn_recv(void) {
    const char *name = "kc_dmn_recv";
    const char *detail = "recv reads data from daemon";
#ifdef _WIN32
    char buf[64];
    int fail;

    fail = 0;
    fail += expect_int("recv bad handle returns -1", -1,
        kc_dmn_recv(-1, buf, sizeof(buf)));
    fail += expect_int("recv NULL buffer returns -1", -1,
        kc_dmn_recv(0, NULL, sizeof(buf)));
    fail += expect_int("recv zero capacity returns -1", -1,
        kc_dmn_recv(0, buf, 0));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
#else
    kc_dmn_t *ctx;
    char dir[512];
    char out[512];
    int fail;

    fail = 0;
    fail += expect_int("recv bad handle returns -1", -1,
        kc_dmn_recv(-1, out, sizeof(out)));
    fail += expect_int("recv NULL buffer returns -1", -1,
        kc_dmn_recv(0, NULL, sizeof(out)));
    fail += expect_int("recv zero capacity returns -1", -1,
        kc_dmn_recv(0, NULL, 0));
    if (open_context(&ctx, dir, sizeof(dir), "recv") != 0) return 1;
    fail += expect_int("update recv daemon returns OK", KC_DMN_OK,
        kc_dmn_update(ctx, "recv", "while IFS= read -r l; do printf '%s\\004' \"$l\"; done"));
    short_sleep();
    memset(out, 0, sizeof(out));
    fail += expect_int("relay first input returns OK", KC_DMN_OK,
        relay_capture(ctx, "recv", "alpha\n", out, sizeof(out)));
    fail += expect_string("recv first output matches", "alpha\004", out);
    memset(out, 0, sizeof(out));
    fail += expect_int("relay second input returns OK", KC_DMN_OK,
        relay_capture(ctx, "recv", "beta\n", out, sizeof(out)));
    fail += expect_string("recv second output matches", "beta\004", out);
    fail += expect_int("delete recv daemon returns OK", KC_DMN_OK,
        kc_dmn_delete(ctx, "recv"));
    kc_dmn_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
#endif
}

/**
 * Tests kc_dmn_disconnect.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_dmn_disconnect(void) {
    const char *name = "kc_dmn_disconnect";
    const char *detail = "disconnect releases daemon handle";
#ifdef _WIN32
    int fail;

    fail = 0;
    fail += expect_int("disconnect bad handle returns ERROR", KC_DMN_ERROR,
        kc_dmn_disconnect(-1));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
#else
    kc_dmn_t *ctx;
    char dir[512];
    int handle;
    int fail;

    fail = 0;
    fail += expect_int("disconnect bad handle returns ERROR", KC_DMN_ERROR,
        kc_dmn_disconnect(-1));
    if (open_context(&ctx, dir, sizeof(dir), "disconnect") != 0) return 1;
    fail += expect_int("update disconnect daemon returns OK", KC_DMN_OK,
        kc_dmn_update(ctx, "disconnect", "while IFS= read -r l; do printf '%s\\004' \"$l\"; done"));
    short_sleep();
    fail += expect_int("connect disconnect daemon returns OK", KC_DMN_OK,
        kc_dmn_connect(ctx, "disconnect", &handle));
    fail += expect_int("disconnect valid handle returns OK", KC_DMN_OK,
        kc_dmn_disconnect(handle));
    fail += expect_int("disconnect released handle returns ERROR", KC_DMN_ERROR,
        kc_dmn_disconnect(handle));
    fail += expect_int("delete disconnect daemon returns OK", KC_DMN_OK,
        kc_dmn_delete(ctx, "disconnect"));
    kc_dmn_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
#endif
}

/**
 * Tests kc_dmn_relay.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_dmn_relay(void) {
    const char *name = "kc_dmn_relay";
    const char *detail = "relay rejects bad input";
    kc_dmn_t *ctx;
    char dir[512];
    int fail;

    fail = 0;
    fail += expect_int("relay(NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_relay(NULL, "key"));
    if (open_context(&ctx, dir, sizeof(dir), "relay") != 0) return 1;
    fail += expect_int("relay(ctx, NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_relay(ctx, NULL));
    fail += expect_int("relay missing key returns ERROR", KC_DMN_ERROR,
        kc_dmn_relay(ctx, "missing"));
    kc_dmn_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_dmn_signal (daemon signal).
 * @return 0 on success, 1 on failure.
 */
static int case_kc_dmn_signal(void) {
    const char *name = "kc_dmn_signal";
    const char *detail = "signal sends POSIX signal to daemon";
#ifdef _WIN32
    kc_dmn_t *ctx;
    char dir[512];
    int fail;

    fail = 0;
    fail += expect_int("signal(NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_signal(NULL, "key", 10));
    if (open_context(&ctx, dir, sizeof(dir), "daemon-signal") != 0) return 1;
    fail += expect_int("signal(ctx, NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_signal(ctx, NULL, 10));
    fail += expect_int("signal missing key returns ERROR", KC_DMN_ERROR,
        kc_dmn_signal(ctx, "missing", 10));
    kc_dmn_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
#else
    kc_dmn_t *ctx;
    char dir[512];
    char sigfile[512];
    char cmd[1024];
    char out[512];
    int fail;

    fail = 0;
    fail += expect_int("signal(NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_signal(NULL, "key", SIGUSR1));
    if (open_context(&ctx, dir, sizeof(dir), "daemon-signal") != 0) return 1;
    fail += expect_int("signal(ctx, NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_signal(ctx, NULL, SIGUSR1));
    fail += expect_int("signal missing key returns ERROR", KC_DMN_ERROR,
        kc_dmn_signal(ctx, "missing", SIGUSR1));
    if ((size_t)snprintf(sigfile, sizeof(sigfile),
            "%s/signal-flag", dir) >= sizeof(sigfile)) {
        kc_dmn_close(ctx);
        return 1;
    }
    snprintf(cmd, sizeof(cmd),
        "trap 'touch %s' USR1; while true; do IFS= read -r l; if [ $? -eq 0 ]; then printf '%%s\\004' \"$l\"; fi; done",
        sigfile);
    fail += expect_int("update signal daemon returns OK", KC_DMN_OK,
        kc_dmn_update(ctx, "signal", cmd));
    short_sleep();
    fail += expect_int("send signal returns OK", KC_DMN_OK,
        kc_dmn_signal(ctx, "signal", SIGUSR1));
    short_sleep();
    fail += expect_true("signal created flag file", access(sigfile, F_OK) == 0);
    memset(out, 0, sizeof(out));
    fail += expect_int("daemon works after signal", KC_DMN_OK,
        relay_capture(ctx, "signal", "hello\n", out, sizeof(out)));
    fail += expect_string("signal daemon relay output matches", "hello\004", out);
    fail += expect_int("delete signal daemon returns OK", KC_DMN_OK,
        kc_dmn_delete(ctx, "signal"));
    kc_dmn_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
#endif
}

/**
 * Tests kc_dmn_serve.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_dmn_serve(void) {
    const char *name = "kc_dmn_serve";
    const char *detail = "serve provides Windows named pipe relay";
#ifdef _WIN32
    char exe[MAX_PATH];
    char pipe_name[128];
    char cmdline[1024];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE pipe;
    DWORD got;
    char buf[128];
    int i;
    int fail;

    fail = 0;
    fail += expect_int("serve(NULL, cmd) returns ERROR", KC_DMN_ERROR,
        kc_dmn_serve(NULL, "more"));
    fail += expect_int("serve(pipe, NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_serve("\\\\.\\pipe\\kc-dmn-test-serve", NULL));
    if (GetModuleFileNameA(NULL, exe, sizeof(exe)) == 0) return 1;
    snprintf(pipe_name, sizeof(pipe_name),
        "\\\\.\\pipe\\kc-dmn-test-serve-%ld", (long)getpid());
    snprintf(cmdline, sizeof(cmdline),
        "\"%s\" serve-child \"%s\" \"echo served\"", exe, pipe_name);
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
            CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        case_result(1, name, detail);
        return 1;
    }
    CloseHandle(pi.hThread);
    pipe = INVALID_HANDLE_VALUE;
    for (i = 0; i < 20; i++) {
        pipe = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE,
            0, NULL, OPEN_EXISTING, 0, NULL);
        if (pipe != INVALID_HANDLE_VALUE) break;
        Sleep(100);
    }
    fail += expect_true("connect to serve child pipe", pipe != INVALID_HANDLE_VALUE);
    if (pipe != INVALID_HANDLE_VALUE) {
        memset(buf, 0, sizeof(buf));
        got = 0;
        ReadFile(pipe, buf, sizeof(buf) - 1, &got, NULL);
        buf[got] = '\0';
        fail += expect_true("serve child command output is relayed",
            strstr(buf, "served") != NULL);
        CloseHandle(pipe);
    }
    TerminateProcess(pi.hProcess, 0);
    WaitForSingleObject(pi.hProcess, 2000);
    CloseHandle(pi.hProcess);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
#else
    case_skip(name, detail);
    return 0;
#endif
}

/**
 * Tests two contexts coexist with isolated state.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_dmn_multictx(void) {
    const char *name = "two contexts coexist with isolated state";
    const char *detail = "two contexts coexist with isolated state";
    kc_dmn_t *first;
    kc_dmn_t *second;
    char dir_a[512];
    char dir_b[512];
    int fail;

    fail = 0;
    if (open_context(&first, dir_a, sizeof(dir_a), "multi-a") != 0) return 1;
    if (open_context(&second, dir_b, sizeof(dir_b), "multi-b") != 0) {
        kc_dmn_close(first);
        return 1;
    }
    fail += expect_int("stop first context returns OK", KC_DMN_OK,
        kc_dmn_stop(first));
    fail += expect_int("delete missing second context key returns OK", KC_DMN_OK,
        kc_dmn_delete(second, "missing"));
    fail += expect_int("stop second context returns OK", KC_DMN_OK,
        kc_dmn_stop(second));
    fail += expect_int("stop first context again returns OK", KC_DMN_OK,
        kc_dmn_stop(first));
    kc_dmn_close(first);
    kc_dmn_close(second);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, 1 on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 19;
    test_case_current = 0;
    run_case(&rc, case_kc_dmn_version);
    run_case(&rc, case_kc_dmn_options_default);
    run_case(&rc, case_kc_dmn_options_load_env);
    run_case(&rc, case_kc_dmn_options_free);
    run_case(&rc, case_kc_dmn_open);
    run_case(&rc, case_kc_dmn_close);
    run_case(&rc, case_kc_dmn_stop);
    run_case(&rc, case_kc_dmn_path);
    run_case(&rc, case_kc_dmn_update);
    run_case(&rc, case_kc_dmn_delete);
    run_case(&rc, case_kc_dmn_list);
    run_case(&rc, case_kc_dmn_connect);
    run_case(&rc, case_kc_dmn_send);
    run_case(&rc, case_kc_dmn_recv);
    run_case(&rc, case_kc_dmn_disconnect);
    run_case(&rc, case_kc_dmn_relay);
    run_case(&rc, case_kc_dmn_signal);
    run_case(&rc, case_kc_dmn_serve);
    run_case(&rc, case_kc_dmn_multictx);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one libdmn public API test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 or 2 on failure.
 */
int main(int argc, char **argv) {
#ifdef _WIN32
    if (argc == 4 && strcmp(argv[1], "serve-child") == 0) {
        return kc_dmn_serve(argv[2], argv[3]) == KC_DMN_OK ? 0 : 1;
    }
#endif
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_dmn_version") == 0) return case_kc_dmn_version();
    if (strcmp(argv[1], "kc_dmn_options_default") == 0) return case_kc_dmn_options_default();
    if (strcmp(argv[1], "kc_dmn_options_load_env") == 0) return case_kc_dmn_options_load_env();
    if (strcmp(argv[1], "kc_dmn_options_free") == 0) return case_kc_dmn_options_free();
    if (strcmp(argv[1], "kc_dmn_open") == 0) return case_kc_dmn_open();
    if (strcmp(argv[1], "kc_dmn_close") == 0) return case_kc_dmn_close();
    if (strcmp(argv[1], "kc_dmn_stop") == 0) return case_kc_dmn_stop();
    if (strcmp(argv[1], "kc_dmn_path") == 0) return case_kc_dmn_path();
    if (strcmp(argv[1], "kc_dmn_update") == 0) return case_kc_dmn_update();
    if (strcmp(argv[1], "kc_dmn_delete") == 0) return case_kc_dmn_delete();
    if (strcmp(argv[1], "kc_dmn_list") == 0) return case_kc_dmn_list();
    if (strcmp(argv[1], "kc_dmn_connect") == 0) return case_kc_dmn_connect();
    if (strcmp(argv[1], "kc_dmn_send") == 0) return case_kc_dmn_send();
    if (strcmp(argv[1], "kc_dmn_recv") == 0) return case_kc_dmn_recv();
    if (strcmp(argv[1], "kc_dmn_disconnect") == 0) return case_kc_dmn_disconnect();
    if (strcmp(argv[1], "kc_dmn_relay") == 0) return case_kc_dmn_relay();
    if (strcmp(argv[1], "kc_dmn_signal") == 0) return case_kc_dmn_signal();
    if (strcmp(argv[1], "kc_dmn_serve") == 0) return case_kc_dmn_serve();
    if (strcmp(argv[1], "kc_dmn_multictx") == 0) return case_kc_dmn_multictx();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
