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
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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

static int test_case_total;
static int test_case_current;

/**
 * Prints one test case result.
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
 * Runs one test case.
 * @param rc Destination accumulator.
 * @param fn Test case function.
 * @return None.
 */
static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

/**
 * Verifies that an integer result matches the expected value.
 * @param name Description of the assertion.
 * @param expected Expected integer value.
 * @param actual Actual integer value.
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
 * @param name Description of the assertion.
 * @param condition Boolean condition to verify.
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
 * @param name Description of the assertion.
 * @param expected Expected string value.
 * @param actual Actual string value.
 * @return 0 on success, 1 on failure.
 */
static int expect_string(const char *name, const char *expected,
    const char *actual) {
    if (!actual || strcmp(expected, actual) != 0) {
        printf("[FAIL] %s: expected '%s', got '%s'\n", name, expected,
            actual ? actual : "NULL");
        return 1;
    }
    return 0;
}

typedef struct {
    const char *expected_key;
    int count;
    int exact_count;
    char first_sock[512];
} list_state_t;

/**
 * Records a synchronous list callback.
 * @param key Borrowed registration key.
 * @param sock Borrowed endpoint value.
 * @param userdata List state supplied to the callback.
 * @return None.
 */
static void record_list(const char *key, const char *sock, void *userdata) {
    list_state_t *state = (list_state_t *)userdata;
    if (!state || !key || !sock) return;
    state->count++;
    if (state->count == 1)
        snprintf(state->first_sock, sizeof(state->first_sock), "%s", sock);
    if (state->expected_key && strcmp(key, state->expected_key) == 0)
        state->exact_count++;
}

/** Sleeps briefly for daemon readiness.
 * @return None.
 */
static void short_sleep(void) {
#ifdef _WIN32
    Sleep(200);
#else
    struct timespec ts = {0, 200000000L};
    nanosleep(&ts, NULL);
#endif
}

/**
 * Makes an isolated runtime directory.
 * @param out Destination path buffer.
 * @param cap Capacity of the destination buffer.
 * @param name Test-specific directory name.
 * @return 0 on success, 1 on failure.
 */
static int make_runtime_dir(char *out, size_t cap, const char *name) {
    size_t base_len;
    unsigned long suffix = 1;
#ifdef _WIN32
    char tmp[MAX_PATH];
    if (GetTempPathA((DWORD)sizeof(tmp), tmp) == 0) return 1;
    if ((size_t)snprintf(out, cap, "%skc-dmn-test-%ld-%s", tmp,
            (long)getpid(), name) >= cap) return 1;
#else
    const char *base = getenv("TMPDIR");
    if (!base || !base[0]) base = "/tmp";
    if ((size_t)snprintf(out, cap, "%s/kc-dmn-test-%ld-%s", base,
        (long)getpid(), name) >= cap) return 1;
#endif
    base_len = strlen(out);
    if (mkdir_one(out) == 0) return 0;
    if (errno != EEXIST) return 1;
    for (;;) {
        if ((size_t)snprintf(out + base_len, cap - base_len, "-%lu",
                suffix) >= cap - base_len)
            return 1;
        if (mkdir_one(out) == 0) return 0;
        if (errno != EEXIST) return 1;
        suffix++;
    }
}

/**
 * Opens a context through the public options API.
 * @param out Destination context pointer.
 * @param dir Destination runtime directory buffer.
 * @param cap Capacity of the directory buffer.
 * @param name Test-specific directory name.
 * @return 0 on success.
 */
static int open_context(kc_dmn_t **out, char *dir, size_t cap,
    const char *name) {
    kc_dmn_options_t *opts;
    int rc;
    if (make_runtime_dir(dir, cap, name) != 0) return 1;
    opts = kc_dmn_options_default();
    if (!opts) return 1;
    if (kc_dmn_options_set(opts, "dir", dir) != KC_DMN_OK) {
        kc_dmn_options_free(opts);
        return 1;
    }
    rc = kc_dmn_open(out, opts);
    kc_dmn_options_free(opts);
    return rc == KC_DMN_OK ? 0 : 1;
}

/**
 * Performs one public connection exchange.
 * @param ctx Context owning the registration.
 * @param key Registration key to connect to.
 * @param input Bytes to send to the daemon.
 * @param out Destination response buffer.
 * @param cap Capacity of the response buffer.
 * @return API status.
 */
static int relay_capture(kc_dmn_t *ctx, const char *key, const char *input,
    char *out, size_t cap) {
    kc_dmn_conn_t *conn = NULL;
    void *data = NULL;
    size_t data_size = 0;
    int rc;
    if (kc_dmn_connect(ctx, key, &conn) != KC_DMN_OK) return KC_DMN_ERROR;
    if (kc_dmn_send(conn, input, strlen(input)) != KC_DMN_OK) {
        kc_dmn_disconnect(conn);
        return KC_DMN_ERROR;
    }
    rc = kc_dmn_recv(conn, cap - 1, &data, &data_size);
    if (rc == KC_DMN_OK && data_size < cap) {
        memcpy(out, data, data_size);
        out[data_size] = '\0';
    }
    kc_dmn_free(data);
    kc_dmn_disconnect(conn);
    return rc;
}

#ifdef _WIN32

/** Joins command arguments for the private Windows serve entrypoint.
 * @param out Destination command buffer.
 * @param cap Destination capacity.
 * @param argv Argument vector.
 * @param first First argument to join.
 * @param argc Argument count.
 * @return None.
 */
static void test_join_args(char *out, size_t cap, char **argv, int first,
    int argc) {
    size_t used = 0;
    int i;

    if (!cap) return;
    out[0] = '\0';
    for (i = first; i < argc; i++) {
        int written = snprintf(out + used, cap - used, "%s%s",
            used ? " " : "", argv[i]);
        if (written < 0 || (size_t)written >= cap - used) {
            out[0] = '\0';
            return;
        }
        used += (size_t)written;
    }
}

/** Runs the private Windows named-pipe test daemon entrypoint.
 * @param pipename Named Pipe path.
 * @param cmd Command string.
 * @return 0 on success, 1 on failure.
 */
static int test_serve_win32(const char *pipename, const char *cmd) {
    SECURITY_ATTRIBUTES sa;
    char cmdstr[8192];
    char buf[8192];

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    if ((size_t)snprintf(cmdstr, sizeof(cmdstr), "cmd.exe /c %s", cmd)
            >= sizeof(cmdstr))
        return 1;

    while (1) {
        HANDLE h = CreateNamedPipeA(pipename, PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, sizeof(buf), sizeof(buf), 0, NULL);
        HANDLE in_rd, in_wr, out_rd, out_wr;
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        DWORD br, avail, exit_code;

        if (h == INVALID_HANDLE_VALUE) {
            Sleep(100);
            continue;
        }
        if (!ConnectNamedPipe(h, NULL) &&
                GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(h);
            continue;
        }
        if (!CreatePipe(&in_rd, &in_wr, &sa, 0) ||
                !CreatePipe(&out_rd, &out_wr, &sa, 0)) {
            DisconnectNamedPipe(h);
            CloseHandle(h);
            continue;
        }
        SetHandleInformation(in_wr, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(out_rd, HANDLE_FLAG_INHERIT, 0);
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = in_rd;
        si.hStdOutput = out_wr;
        si.hStdError = out_wr;
        memset(&pi, 0, sizeof(pi));
        if (!CreateProcessA(NULL, cmdstr, NULL, NULL, TRUE,
                CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(in_rd);
            CloseHandle(in_wr);
            CloseHandle(out_rd);
            CloseHandle(out_wr);
            DisconnectNamedPipe(h);
            CloseHandle(h);
            continue;
        }
        CloseHandle(in_rd);
        CloseHandle(out_wr);
        CloseHandle(pi.hThread);
        while (1) {
            if (PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL) && avail > 0) {
                DWORD rd = avail < (DWORD)sizeof(buf) ?
                    avail : (DWORD)sizeof(buf);
                if (ReadFile(h, buf, rd, &br, NULL) && br > 0) {
                    DWORD off = 0, written;
                    while (off < br && WriteFile(in_wr, buf + off,
                            br - off, &written, NULL) && written > 0)
                        off += written;
                }
            }
            if (PeekNamedPipe(out_rd, NULL, 0, NULL, &avail, NULL) &&
                    avail > 0) {
                DWORD rd = avail < (DWORD)sizeof(buf) ?
                    avail : (DWORD)sizeof(buf);
                if (ReadFile(out_rd, buf, rd, &br, NULL) && br > 0) {
                    DWORD off = 0, written;
                    while (off < br && WriteFile(h, buf + off,
                            br - off, &written, NULL) && written > 0)
                        off += written;
                }
            }
            if (!GetExitCodeProcess(pi.hProcess, &exit_code) ||
                    exit_code != STILL_ACTIVE)
                break;
            if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL) &&
                    GetLastError() == ERROR_BROKEN_PIPE)
                break;
            Sleep(5);
        }
        CloseHandle(in_wr);
        CloseHandle(out_rd);
        CloseHandle(pi.hProcess);
        DisconnectNamedPipe(h);
        CloseHandle(h);
    }
}

#endif

/**
 * Tests kc_dmn_version.
 * @return 0 on success, 1 on failure.
 */
static int case_version(void) {
    int fail = expect_true("version returns build timestamp",
        kc_dmn_version() != 0U);
    case_result(fail, "kc_dmn_version", "version returns build timestamp");
    return fail != 0;
}

/**
 * Tests options allocation, setting, reset, ownership, and freeing.
 * @return 0 on success, 1 on failure.
 */
static int case_options(void) {
    kc_dmn_options_t *opts = kc_dmn_options_default();
    kc_dmn_t *ctx = NULL;
    char dir[512];
    int fail = 0;
    fail += expect_true("options_default returns an object", opts != NULL);
    fail += expect_int("options_set(NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_options_set(NULL, "dir", "/tmp/invalid"));
    fail += expect_int("options_set unknown key returns ERROR", KC_DMN_ERROR,
        kc_dmn_options_set(opts, "unknown", "/tmp/invalid"));
    fail += expect_int("options_set NULL key returns ERROR", KC_DMN_ERROR,
        kc_dmn_options_set(opts, NULL, "/tmp/invalid"));
    if (opts) {
        fail += expect_int("options_set dir returns OK", KC_DMN_OK,
            kc_dmn_options_set(opts, "dir", "/tmp/kc-dmn-option-owned"));
        fail += expect_int("options_set reset returns OK", KC_DMN_OK,
            kc_dmn_options_set(opts, "dir", NULL));
        if (make_runtime_dir(dir, sizeof(dir), "options") == 0) {
            fail += expect_int("options_set owned dir returns OK", KC_DMN_OK,
                kc_dmn_options_set(opts, "dir", dir));
            fail += expect_int("open copies options", KC_DMN_OK,
                kc_dmn_open(&ctx, opts));
            kc_dmn_options_free(opts);
            opts = NULL;
            fail += expect_string("context retains copied path", dir,
                kc_dmn_path(ctx));
            kc_dmn_close(ctx);
        } else {
            fail++;
        }
    }
    kc_dmn_options_free(opts);
    kc_dmn_options_free(NULL);
    case_result(fail, "kc_dmn_options_default/set/free",
        "options are opaque, copied, resettable, and NULL-safe");
    return fail != 0;
}

/**
 * Tests open output handling and kc_dmn_get_error.
 * @return 0 on success, 1 on failure.
 */
static int case_open_error(void) {
    kc_dmn_t *ctx = NULL;
    kc_dmn_t *failed = (kc_dmn_t *)(uintptr_t)1;
    kc_dmn_options_t *opts = NULL;
    char long_dir[1024];
    const char *error;
    int fail = 0;
    fail += expect_int("open(NULL, NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_open(NULL, NULL));

    fail += expect_int("open(NULL options) returns OK", KC_DMN_OK,
        kc_dmn_open(&ctx, NULL));
    if (ctx) {
        fail += expect_true("automatic path is available",
            kc_dmn_path(ctx) != NULL && kc_dmn_path(ctx)[0] != '\0');
        fail += expect_true("fresh error is NULL",
            kc_dmn_get_error(ctx) == NULL);
        fail += expect_int("invalid update returns ERROR", KC_DMN_ERROR,
            kc_dmn_update(ctx, NULL, "cat"));
        error = kc_dmn_get_error(ctx);
        if (error)
            fail += expect_true("recorded error is non-empty", error[0] != '\0');
        kc_dmn_close(ctx);
    } else {
        fail++;
    }

    memset(long_dir, 'x', sizeof(long_dir) - 1);
    long_dir[sizeof(long_dir) - 1] = '\0';
    opts = kc_dmn_options_default();
    fail += expect_true("options are available for failed open", opts != NULL);
    if (opts) {
        fail += expect_int("overlong dir is accepted by options", KC_DMN_OK,
            kc_dmn_options_set(opts, "dir", long_dir));
        fail += expect_int("overlong open returns ERROR", KC_DMN_ERROR,
            kc_dmn_open(&failed, opts));
        fail += expect_true("failed open output is NULL", failed == NULL);
        kc_dmn_options_free(opts);
    }
    fail += expect_true("get_error(NULL) returns NULL",
        kc_dmn_get_error(NULL) == NULL);
    case_result(fail, "kc_dmn_open/get_error", "open output and error access");
    return fail != 0;
}

/**
 * Tests kc_dmn_close.
 * @return 0 on success, 1 on failure.
 */
static int case_close(void) {
    kc_dmn_t *ctx;
    char dir[512];
    kc_dmn_close(NULL);
    if (open_context(&ctx, dir, sizeof(dir), "close") != 0) return 1;
    kc_dmn_close(ctx);
    case_result(0, "kc_dmn_close", "close releases context and is NULL-safe");
    return 0;
}

/**
 * Tests kc_dmn_path.
 * @return 0 on success, 1 on failure.
 */
static int case_path(void) {
    kc_dmn_t *ctx;
    char dir[512];
    int fail = expect_true("path(NULL) returns NULL", kc_dmn_path(NULL) == NULL);
    if (open_context(&ctx, dir, sizeof(dir), "path") != 0) return 1;
    fail += expect_string("path returns configured directory", dir,
        kc_dmn_path(ctx));
    kc_dmn_close(ctx);
    case_result(fail, "kc_dmn_path", "path returns configured directory");
    return fail != 0;
}

/**
 * Tests kc_dmn_update.
 * @return 0 on success, 1 on failure.
 */
static int case_update(void) {
    kc_dmn_t *ctx;
    char dir[512];
    int fail = expect_int("update(NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_update(NULL, "key", "cat"));
    if (open_context(&ctx, dir, sizeof(dir), "update") != 0) return 1;
    fail += expect_int("update NULL key returns ERROR", KC_DMN_ERROR,
        kc_dmn_update(ctx, NULL, "cat"));
    fail += expect_int("update NULL command returns ERROR", KC_DMN_ERROR,
        kc_dmn_update(ctx, "key", NULL));
    fail += expect_int("update key returns OK", KC_DMN_OK,
        kc_dmn_update(ctx, "update", "cat"));
    short_sleep();
    fail += expect_int("replace key returns OK", KC_DMN_OK,
        kc_dmn_update(ctx, "update", "echo replaced"));
    short_sleep();
    fail += expect_int("delete updated key returns OK", KC_DMN_OK,
        kc_dmn_delete(ctx, "update"));
    kc_dmn_close(ctx);
    case_result(fail, "kc_dmn_update", "registers and replaces daemons");
    return fail != 0;
}

/**
 * Tests kc_dmn_delete.
 * @return 0 on success, 1 on failure.
 */
static int case_delete(void) {
    kc_dmn_t *ctx;
    char dir[512];
    int fail = expect_int("delete(NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_delete(NULL, "key"));
    if (open_context(&ctx, dir, sizeof(dir), "delete") != 0) return 1;
    fail += expect_int("delete NULL key returns ERROR", KC_DMN_ERROR,
        kc_dmn_delete(ctx, NULL));
    fail += expect_int("delete missing key returns OK", KC_DMN_OK,
        kc_dmn_delete(ctx, "missing"));
    fail += expect_int("update delete test key returns OK", KC_DMN_OK,
        kc_dmn_update(ctx, "delme", "cat"));
    short_sleep();
    fail += expect_int("delete existing key returns OK", KC_DMN_OK,
        kc_dmn_delete(ctx, "delme"));
    kc_dmn_close(ctx);
    case_result(fail, "kc_dmn_delete", "deletes daemon registrations");
    return fail != 0;
}

/**
 * Tests kc_dmn_list and its synchronous borrowed callback values.
 * @return 0 on success, 1 on failure.
 */
static int case_list(void) {
    kc_dmn_t *ctx;
    list_state_t state;
    char dir[512];
    int fail = expect_int("list(NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_list(NULL, NULL, NULL, NULL));
    if (open_context(&ctx, dir, sizeof(dir), "list") != 0) return 1;
    fail += expect_int("list missing key returns OK", KC_DMN_OK,
        kc_dmn_list(ctx, "missing", NULL, NULL));
    fail += expect_int("list NULL callback returns OK", KC_DMN_OK,
        kc_dmn_list(ctx, NULL, NULL, NULL));
    fail += expect_int("update list test key returns OK", KC_DMN_OK,
        kc_dmn_update(ctx, "listed", "cat"));
    short_sleep();
    memset(&state, 0, sizeof(state));
    state.expected_key = "listed";
    fail += expect_int("list exact key returns OK", KC_DMN_OK,
        kc_dmn_list(ctx, "listed", record_list, &state));
    fail += expect_true("list invokes callback", state.exact_count >= 1);
    fail += expect_true("list callback provides endpoint",
        state.first_sock[0] != '\0');
    memset(&state, 0, sizeof(state));
    state.expected_key = "listed";
    fail += expect_int("list all returns OK", KC_DMN_OK,
        kc_dmn_list(ctx, NULL, record_list, &state));
    fail += expect_true("list all includes key", state.exact_count >= 1);
    kc_dmn_delete(ctx, "listed");
    kc_dmn_close(ctx);
    case_result(fail, "kc_dmn_list", "lists registrations synchronously");
    return fail != 0;
}

/**
 * Tests kc_dmn_connect and opaque ownership.
 * @return 0 on success, 1 on failure.
 */
static int case_connect(void) {
    kc_dmn_t *ctx;
    kc_dmn_conn_t *conn = (kc_dmn_conn_t *)(uintptr_t)1;
    char dir[512];
    int fail = expect_int("connect(NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_connect(NULL, "key", &conn));
    fail += expect_true("connect error output is NULL", conn == NULL);
    if (open_context(&ctx, dir, sizeof(dir), "connect") != 0) return 1;
    fail += expect_int("connect NULL key returns ERROR", KC_DMN_ERROR,
        kc_dmn_connect(ctx, NULL, &conn));
    fail += expect_int("connect NULL output returns ERROR", KC_DMN_ERROR,
        kc_dmn_connect(ctx, "key", NULL));
    fail += expect_int("connect missing key returns ERROR", KC_DMN_ERROR,
        kc_dmn_connect(ctx, "missing", &conn));
    fail += expect_true("missing connection output is NULL", conn == NULL);
    kc_dmn_close(ctx);
    case_result(fail, "kc_dmn_connect", "returns owned opaque connections");
    return fail != 0;
}

/**
 * Tests kc_dmn_send.
 * @return 0 on success, 1 on failure.
 */
static int case_send(void) {
    kc_dmn_t *ctx;
    kc_dmn_conn_t *conn = NULL;
    char dir[512];
    int fail = expect_int("send NULL connection returns ERROR", KC_DMN_ERROR,
        kc_dmn_send(NULL, "x", 1));
    fail += expect_int("send NULL data with size returns ERROR", KC_DMN_ERROR,
        kc_dmn_send(NULL, NULL, 1));
    if (open_context(&ctx, dir, sizeof(dir), "send") != 0) return 1;
#ifdef _WIN32
    fail += expect_int("update send daemon returns OK", KC_DMN_OK,
        kc_dmn_update(ctx, "send", "echo send"));
#else
    fail += expect_int("update send daemon returns OK", KC_DMN_OK,
        kc_dmn_update(ctx, "send", "while IFS= read -r l; do printf '%s\\004' \"$l\"; done"));
#endif
    short_sleep();
    fail += expect_int("connect send daemon returns OK", KC_DMN_OK,
        kc_dmn_connect(ctx, "send", &conn));
    fail += expect_int("send zero bytes returns OK", KC_DMN_OK,
        kc_dmn_send(conn, NULL, 0));
    fail += expect_int("send data returns OK", KC_DMN_OK,
        kc_dmn_send(conn, "hello\n", 6));
    kc_dmn_disconnect(conn);
    kc_dmn_delete(ctx, "send");
    kc_dmn_close(ctx);
    case_result(fail, "kc_dmn_send", "sends data through owned connection");
    return fail != 0;
}

/**
 * Tests recv buffers, reset outputs, exact bytes, and deterministic EOF.
 * @return 0 on success, 1 on failure.
 */
static int case_recv(void) {
    kc_dmn_t *ctx;
    kc_dmn_conn_t *conn = NULL;
    void *data = (void *)(uintptr_t)1;
    size_t data_size = 99;
    char dir[512];
    char out[64];
    int fail = 0;
    fail += expect_int("recv invalid connection returns ERROR", KC_DMN_ERROR,
        kc_dmn_recv(NULL, sizeof(out), &data, &data_size));
    fail += expect_true("invalid recv resets data", data == NULL);
    fail += expect_true("invalid recv resets size", data_size == 0);
    if (open_context(&ctx, dir, sizeof(dir), "recv") != 0) return 1;
#ifdef _WIN32
    fail += expect_int("update recv daemon returns OK", KC_DMN_OK,
        kc_dmn_update(ctx, "recv", "echo alpha"));
#else
    fail += expect_int("update recv daemon returns OK", KC_DMN_OK,
        kc_dmn_update(ctx, "recv", "while IFS= read -r l; do printf '%s\\004' \"$l\"; done"));
#endif
    short_sleep();
    fail += expect_int("connect recv daemon returns OK", KC_DMN_OK,
        kc_dmn_connect(ctx, "recv", &conn));
    fail += expect_int("send recv request returns OK", KC_DMN_OK,
        kc_dmn_send(conn, "alpha\n", 6));
    data = (void *)(uintptr_t)1;
    data_size = 99;
    fail += expect_int("recv response returns OK", KC_DMN_OK,
        kc_dmn_recv(conn, sizeof(out), &data, &data_size));
    fail += expect_true("recv returns owned data", data != NULL);
#ifdef _WIN32
    fail += expect_int("recv returns exact 7-byte count", 7, (int)data_size);
#else
    fail += expect_int("recv returns exact 6-byte count", 6, (int)data_size);
#endif
    if (data && data_size < sizeof(out)) {
        memcpy(out, data, data_size);
        out[data_size] = '\0';
#ifdef _WIN32
        fail += expect_string("recv preserves command response", "alpha\r\n", out);
#else
        fail += expect_string("recv preserves binary response", "alpha\004", out);
#endif
    }
    kc_dmn_free(data);
    kc_dmn_disconnect(conn);
    kc_dmn_delete(ctx, "recv");
    conn = NULL;
    fail += expect_int("update EOF daemon returns OK", KC_DMN_OK,
        kc_dmn_update(ctx, "eof", "exit 0"));
    short_sleep();
    fail += expect_int("connect EOF daemon returns OK", KC_DMN_OK,
        kc_dmn_connect(ctx, "eof", &conn));
    data = (void *)(uintptr_t)1;
    data_size = 99;
    if (conn) {
        int rc = kc_dmn_recv(conn, sizeof(out), &data, &data_size);
        fail += expect_int("closed daemon returns EOF", KC_DMN_EOF, rc);
        fail += expect_true("EOF resets data", data == NULL);
        fail += expect_true("EOF resets size", data_size == 0);
    }
    kc_dmn_free(data);
    kc_dmn_disconnect(conn);
    kc_dmn_delete(ctx, "eof");
    kc_dmn_close(ctx);
    case_result(fail, "kc_dmn_recv", "owned buffers, reset outputs, and EOF");
    return fail != 0;
}

/**
 * Tests NULL-safe connection release without stale handle reuse.
 * @return 0 on success, 1 on failure.
 */
static int case_disconnect(void) {
    kc_dmn_disconnect(NULL);
    case_result(0, "kc_dmn_disconnect", "opaque connection release is NULL-safe");
    return 0;
}

/**
 * Tests NULL-safe API buffer release.
 * @return 0 on success, 1 on failure.
 */
static int case_free(void) {
    kc_dmn_free(NULL);
    case_result(0, "kc_dmn_free", "API-owned buffer release is NULL-safe");
    return 0;
}

/**
 * Tests kc_dmn_signal.
 * @return 0 on success, 1 on failure.
 */
static int case_signal(void) {
    kc_dmn_t *ctx;
    char dir[512];
    int fail = expect_int("signal(NULL) returns ERROR", KC_DMN_ERROR,
        kc_dmn_signal(NULL, "key", 10));
    if (open_context(&ctx, dir, sizeof(dir), "signal") != 0) return 1;
    fail += expect_int("signal NULL key returns ERROR", KC_DMN_ERROR,
        kc_dmn_signal(ctx, NULL, 10));
    fail += expect_int("signal missing key returns ERROR", KC_DMN_ERROR,
        kc_dmn_signal(ctx, "missing", 10));
    kc_dmn_close(ctx);
    case_result(fail, "kc_dmn_signal", "signals managed daemons");
    return fail != 0;
}

/**
 * Tests a genuine public daemon exchange.
 * @return 0 on success, 1 on failure.
 */
static int case_integration(void) {
    kc_dmn_t *ctx;
    char dir[512];
    char out[64] = {0};
    int fail;
    if (open_context(&ctx, dir, sizeof(dir), "integration") != 0) return 1;
#ifdef _WIN32
    fail = expect_int("update integration daemon returns OK", KC_DMN_OK,
        kc_dmn_update(ctx, "integration", "echo hello"));
#else
    fail = expect_int("update integration daemon returns OK", KC_DMN_OK,
        kc_dmn_update(ctx, "integration", "while IFS= read -r l; do printf '%s\\004' \"$l\"; done"));
#endif
    short_sleep();
    fail += expect_int("public exchange returns OK", KC_DMN_OK,
        relay_capture(ctx, "integration", "hello\n", out, sizeof(out)));
#ifdef _WIN32
    fail += expect_string("public exchange preserves command response", "hello\r\n", out);
#else
    fail += expect_string("public exchange preserves EOT", "hello\004", out);
#endif
    kc_dmn_delete(ctx, "integration");
    kc_dmn_close(ctx);
    case_result(fail, "public integration exchange", "daemon I/O through public API");
    return fail != 0;
}

/**
 * Runs all test cases.
 * @return 0 on success, 1 on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 15;
    test_case_current = 0;
    run_case(&rc, case_version);
    run_case(&rc, case_options);
    run_case(&rc, case_open_error);
    run_case(&rc, case_close);
    run_case(&rc, case_path);
    run_case(&rc, case_update);
    run_case(&rc, case_delete);
    run_case(&rc, case_list);
    run_case(&rc, case_connect);
    run_case(&rc, case_send);
    run_case(&rc, case_recv);
    run_case(&rc, case_disconnect);
    run_case(&rc, case_free);
    run_case(&rc, case_signal);
    run_case(&rc, case_integration);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one libdmn public API test case.
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument vector.
 * @return Process status.
 */
int main(int argc, char **argv) {
#ifdef _WIN32
    if (argc >= 4 && strcmp(argv[1], "--_serve") == 0) {
        char cmd[8192];
        test_join_args(cmd, sizeof(cmd), argv, 3, argc);
        if (!cmd[0]) return 1;
        return test_serve_win32(argv[2], cmd);
    }
#endif
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_dmn_version") == 0) return case_version();
    if (strcmp(argv[1], "kc_dmn_options") == 0) return case_options();
    if (strcmp(argv[1], "kc_dmn_open") == 0) return case_open_error();
    if (strcmp(argv[1], "kc_dmn_close") == 0) return case_close();
    if (strcmp(argv[1], "kc_dmn_path") == 0) return case_path();
    if (strcmp(argv[1], "kc_dmn_update") == 0) return case_update();
    if (strcmp(argv[1], "kc_dmn_delete") == 0) return case_delete();
    if (strcmp(argv[1], "kc_dmn_list") == 0) return case_list();
    if (strcmp(argv[1], "kc_dmn_connect") == 0) return case_connect();
    if (strcmp(argv[1], "kc_dmn_send") == 0) return case_send();
    if (strcmp(argv[1], "kc_dmn_recv") == 0) return case_recv();
    if (strcmp(argv[1], "kc_dmn_disconnect") == 0) return case_disconnect();
    if (strcmp(argv[1], "kc_dmn_free") == 0) return case_free();
    if (strcmp(argv[1], "kc_dmn_signal") == 0) return case_signal();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
