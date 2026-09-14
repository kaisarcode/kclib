/**
 * test.c - libtray public API tests.
 * Summary: Tests each public libtray function through one CTest case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libtray.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef KC_TRAY_TEST_CLI
#define KC_TRAY_TEST_CLI ""
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
 * Records the most recent delivered action.
 * @param userdata Unused.
 * @param action Delivered action name.
 * @return None.
 */
static void test_callback(void *userdata, const char *action) {
    (void)userdata;
    (void)action;
}

/**
 * Opens one tray context for tests.
 * @param ctx Receives the opened context.
 * @return 1 on success, 0 on failure.
 */
static int open_tray(kc_tray_t **ctx) {
    kc_tray_options_t opts;

    opts = kc_tray_options_default();
    if (!opts) {
        return 0;
    }
    if (kc_tray_open(ctx, opts, test_callback, NULL) != KC_TRAY_OK) {
        kc_tray_options_free(opts);
        return 0;
    }
    kc_tray_options_free(opts);
    return 1;
}

/**
 * Append one argument to a Windows command line with quoting.
 * @param cmd Destination wide command line.
 * @param cap Capacity in wchar_t units.
 * @param arg Argument to append.
 * @return 0 on success, 1 on failure.
 */
#ifdef _WIN32
static int test_cli_append_arg(wchar_t *cmd, size_t cap, const wchar_t *arg) {
    size_t n = wcslen(cmd);
    size_t len = wcslen(arg);
    int quote = len == 0 || wcschr(arg, L' ') != NULL || wcschr(arg, L'\t') != NULL ||
        wcschr(arg, L'"') != NULL;
    int i;

    if (n > 0) {
        if (n + 1 >= cap) return 1;
        cmd[n++] = L' ';
    }
    if (quote) {
        if (n + 1 >= cap) return 1;
        cmd[n++] = L'"';
        for (i = 0; i < (int)len; i++) {
            if (arg[i] == L'"') {
                if (n + 1 >= cap) return 1;
                cmd[n++] = L'\\';
            }
            if (n + 1 >= cap) return 1;
            cmd[n++] = arg[i];
        }
        if (n + 1 >= cap) return 1;
        cmd[n++] = L'"';
    } else {
        if (n + len >= cap) return 1;
        memcpy(cmd + n, arg, len * sizeof(wchar_t));
        n += len;
    }
    cmd[n] = L'\0';
    return 0;
}

/**
 * Convert one UTF-8 string to a wide buffer.
 * @param in UTF-8 input string.
 * @param out Wide output buffer.
 * @param cap Output buffer capacity in wchar_t units.
 * @return 0 on success, 1 on failure.
 */
static int test_cli_to_wide(const char *in, wchar_t *out, size_t cap) {
    return MultiByteToWideChar(CP_UTF8, 0, in, -1, out, (int)cap) > 0 ? 0 : 1;
}

/**
 * Read one inherited pipe into a NUL-terminated buffer.
 * @param pipe Pipe read handle.
 * @param buf Destination buffer.
 * @param size Destination buffer size.
 * @return 0 on success.
 */
static int test_cli_read_pipe(HANDLE pipe, char *buf, size_t size) {
    DWORD count;
    size_t used = 0;

    while (used + 1 < size &&
            ReadFile(pipe, buf + used, (DWORD)(size - used - 1), &count, NULL) &&
            count > 0) {
        used += count;
    }
    buf[used] = '\0';
    return 0;
}

/**
 * Runs the tray CLI through CreateProcessW and captures its output.
 * @param argv CLI argument vector, NULL-terminated.
 * @param out Output buffer.
 * @param out_size Output buffer size.
 * @param err Error buffer.
 * @param err_size Error buffer size.
 * @param out_status Receives the CLI exit status.
 * @return 0 on success, 1 on failure.
 */
static int test_cli_run(char *const argv[], char *out, size_t out_size,
        char *err, size_t err_size, int *out_status) {
    wchar_t exe[MAX_PATH];
    wchar_t cmd[32768];
    wchar_t wide[4096];
    HANDLE out_pipe[2];
    HANDLE err_pipe[2];
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD exit_code;
    size_t n;
    int i;

    (void)argv;

    n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return 1;
    while (n > 0 && exe[n - 1] != L'\\') n--;
    if (n >= MAX_PATH - 8) return 1;
    wcscpy(exe + n, L"tray.exe");

    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;
    if (!CreatePipe(&out_pipe[0], &out_pipe[1], &sa, 0)) return 1;
    if (!CreatePipe(&err_pipe[0], &err_pipe[1], &sa, 0)) {
        CloseHandle(out_pipe[0]);
        CloseHandle(out_pipe[1]);
        return 1;
    }
    SetHandleInformation(out_pipe[0], HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_pipe[0], HANDLE_FLAG_INHERIT, 0);

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = out_pipe[1];
    si.hStdError = err_pipe[1];

    cmd[0] = L'"';
    cmd[1] = L'\0';
    if (test_cli_append_arg(cmd, sizeof(cmd) / sizeof(wchar_t), exe)) {
        CloseHandle(out_pipe[0]);
        CloseHandle(out_pipe[1]);
        CloseHandle(err_pipe[0]);
        CloseHandle(err_pipe[1]);
        return 1;
    }
    n = wcslen(cmd);
    cmd[n] = L'"';
    cmd[n + 1] = L'\0';

    for (i = 0; argv[i]; i++) {
        if (i == 0) continue;
        if (test_cli_to_wide(argv[i], wide, sizeof(wide) / sizeof(wchar_t))) {
            CloseHandle(out_pipe[0]);
            CloseHandle(out_pipe[1]);
            CloseHandle(err_pipe[0]);
            CloseHandle(err_pipe[1]);
            return 1;
        }
        if (test_cli_append_arg(cmd, sizeof(cmd) / sizeof(wchar_t), wide)) {
            CloseHandle(out_pipe[0]);
            CloseHandle(out_pipe[1]);
            CloseHandle(err_pipe[0]);
            CloseHandle(err_pipe[1]);
            return 1;
        }
    }

    if (!CreateProcessW(exe, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(out_pipe[0]);
        CloseHandle(out_pipe[1]);
        CloseHandle(err_pipe[0]);
        CloseHandle(err_pipe[1]);
        return 1;
    }

    CloseHandle(out_pipe[1]);
    CloseHandle(err_pipe[1]);
    test_cli_read_pipe(out_pipe[0], out, out_size);
    test_cli_read_pipe(err_pipe[0], err, err_size);
    CloseHandle(out_pipe[0]);
    CloseHandle(err_pipe[0]);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    *out_status = (int)exit_code;
    return 0;
}
#else
/**
 * Runs the tray CLI and captures its output.
 * @param argv CLI argument vector, NULL-terminated.
 * @param out Output buffer.
 * @param out_size Output buffer size.
 * @param err Error buffer.
 * @param err_size Error buffer size.
 * @param out_status Receives the CLI exit status.
 * @return 0 on success, 1 on failure.
 */
static int test_cli_run(char *const argv[], char *out, size_t out_size,
        char *err, size_t err_size, int *out_status) {
    int out_pipe[2];
    int err_pipe[2];
    pid_t pid;
    ssize_t count;
    int status;

    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        return 1;
    }
    pid = fork();
    if (pid < 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        return 1;
    }
    if (pid == 0) {
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        execv(argv[0], argv);
        _exit(127);
    }

    close(out_pipe[1]);
    close(err_pipe[1]);
    memset(out, 0, out_size);
    memset(err, 0, err_size);
    while ((count = read(out_pipe[0], out, out_size - 1)) > 0) {
        out += count;
        out_size -= count;
    }
    close(out_pipe[0]);
    while ((count = read(err_pipe[0], err, err_size - 1)) > 0) {
        err += count;
        err_size -= count;
    }
    close(err_pipe[0]);

    if (waitpid(pid, &status, 0) < 0) {
        return 1;
    }
    *out_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    return 0;
}
#endif

/**
 * Tests kc_tray_options_default.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tray_options_default(void) {
    const char *name = "kc_tray_options_default";
    const char *detail = "default options initialize correctly";
    kc_tray_options_t opts;
    int fail;

    opts = kc_tray_options_default();
    fail = expect_true("default options returns non-NULL", opts != NULL);
    kc_tray_options_free(opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tray_options_set.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tray_options_set(void) {
    const char *name = "kc_tray_options_set";
    const char *detail = "options set works correctly";
    kc_tray_options_t opts;
    int fail = 0;

    opts = kc_tray_options_default();
    if (!opts) return 1;

    fail += expect_int("options_set accepts icon key", KC_TRAY_OK,
        kc_tray_options_set(opts, "icon", "network-server"));
    fail += expect_int("options_set accepts tooltip key", KC_TRAY_OK,
        kc_tray_options_set(opts, "tooltip", "tray tooltip"));
    fail += expect_int("options_set rejects unknown key", KC_TRAY_ERROR,
        kc_tray_options_set(opts, "unknown", "value"));
    fail += expect_int("options_set accepts NULL value", KC_TRAY_OK,
        kc_tray_options_set(opts, "tooltip", NULL));
    fail += expect_int("options_set rejects NULL opts", KC_TRAY_ERROR,
        kc_tray_options_set(NULL, "icon", "value"));
    fail += expect_int("options_set rejects NULL key", KC_TRAY_ERROR,
        kc_tray_options_set(opts, NULL, "value"));

    kc_tray_options_free(opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tray_options_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tray_options_free(void) {
    const char *name = "kc_tray_options_free";
    const char *detail = "options free clears resources";
    kc_tray_options_t opts;
    int fail = 0;

    fail += expect_true("options_free accepts NULL", 1);
    kc_tray_options_free(NULL);

    opts = kc_tray_options_default();
    fail += expect_true("default options non-NULL", opts != NULL);
    if (opts) {
        kc_tray_options_set(opts, "icon", "network-server");
        kc_tray_options_set(opts, "tooltip", "tray tooltip");
        kc_tray_options_free(opts);
    }

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tray_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tray_version(void) {
    const char *name = "kc_tray_version";
    const char *detail = "version returns build timestamp";
    int fail = expect_true("version returns build value", kc_tray_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tray_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tray_open(void) {
    const char *name = "kc_tray_open";
    const char *detail = "open validates and allocates context";
    kc_tray_t *ctx = NULL;
    int fail = 0;

    fail += expect_int("open rejects NULL out", KC_TRAY_ERROR,
        kc_tray_open(NULL, NULL, NULL, NULL));
    fail += expect_int("open rejects NULL opts", KC_TRAY_ERROR,
        kc_tray_open(&ctx, NULL, NULL, NULL));
    fail += expect_true("open error clears output", ctx == NULL);
    fail += expect_int("open rejects NULL out with opts", KC_TRAY_ERROR,
        kc_tray_open(NULL, NULL, NULL, NULL));

    fail += expect_true("open creates context", open_tray(&ctx));
    fail += expect_true("open sets output", ctx != NULL);
    fail += expect_int("close opened context", KC_TRAY_OK, kc_tray_close(ctx));

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tray_set_menu.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tray_set_menu(void) {
    const char *name = "kc_tray_set_menu";
    const char *detail = "set_menu copies items atomically";
    kc_tray_item_t items[3];
    kc_tray_item_t replaced[2];
    kc_tray_item_t invalid[1];

    memset(items, 0, sizeof(items));
    memset(replaced, 0, sizeof(replaced));
    memset(invalid, 0, sizeof(invalid));
    kc_tray_t *ctx;
    int fail = 0;

    fail += expect_int("set_menu rejects NULL ctx", KC_TRAY_ERROR,
        kc_tray_set_menu(NULL, items, 1));

    if (!open_tray(&ctx)) return 1;

    fail += expect_int("set_menu rejects negative count", KC_TRAY_ERROR,
        kc_tray_set_menu(ctx, items, -1));
    fail += expect_int("set_menu rejects NULL items with count", KC_TRAY_ERROR,
        kc_tray_set_menu(ctx, NULL, 1));
    fail += expect_int("set_menu clears with NULL items", KC_TRAY_OK,
        kc_tray_set_menu(ctx, NULL, 0));
    fail += expect_int("set_menu clears with zero count", KC_TRAY_OK,
        kc_tray_set_menu(ctx, items, 0));

    items[0].label = NULL;
    items[0].action = NULL;
    items[1].label = "One";
    items[1].action = "one";
    items[2].label = "Two";
    items[2].action = "two";
    fail += expect_int("set_menu accepts separators", KC_TRAY_OK,
        kc_tray_set_menu(ctx, items, 3));

    replaced[0].label = "New";
    replaced[0].action = "new";
    replaced[1].label = NULL;
    replaced[1].action = NULL;
    fail += expect_int("set_menu replaces the menu", KC_TRAY_OK,
        kc_tray_set_menu(ctx, replaced, 2));
    fail += expect_int("caller array is reusable after set_menu", KC_TRAY_OK,
        kc_tray_set_menu(ctx, items, 3));

    invalid[0].label = "Label only";
    invalid[0].action = NULL;
    fail += expect_int("set_menu rejects label without action", KC_TRAY_ERROR,
        kc_tray_set_menu(ctx, invalid, 1));
    invalid[0].label = NULL;
    invalid[0].action = "Action only";
    fail += expect_int("set_menu rejects action without label", KC_TRAY_ERROR,
        kc_tray_set_menu(ctx, invalid, 1));
    invalid[0].label = "";
    invalid[0].action = "";
    fail += expect_int("set_menu rejects empty strings", KC_TRAY_ERROR,
        kc_tray_set_menu(ctx, invalid, 1));

    fail += expect_int("close context", KC_TRAY_OK, kc_tray_close(ctx));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tray_run.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tray_run(void) {
    const char *name = "kc_tray_run";
    const char *detail = "run honors a stop request";
    kc_tray_t *ctx;
    int fail = 0;

    fail += expect_int("run rejects NULL", KC_TRAY_ERROR, kc_tray_run(NULL));
    if (!open_tray(&ctx)) return 1;

    fail += expect_int("static stop request", KC_TRAY_OK, kc_tray_stop(ctx));
    fail += expect_int("run returns after stop", KC_TRAY_OK, kc_tray_run(ctx));
    fail += expect_int("run returns after repeated stop", KC_TRAY_OK,
        kc_tray_run(ctx));
    fail += expect_int("close context", KC_TRAY_OK, kc_tray_close(ctx));

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tray_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tray_stop(void) {
    const char *name = "kc_tray_stop";
    const char *detail = "stop is context-local and idempotent";
    kc_tray_t *ctx;
    kc_tray_t *other;
    int fail = 0;

    fail += expect_int("stop rejects NULL", KC_TRAY_ERROR, kc_tray_stop(NULL));
    if (!open_tray(&ctx)) return 1;
    if (!open_tray(&other)) {
        kc_tray_close(ctx);
        return 1;
    }
    fail += expect_int("stop context succeeds", KC_TRAY_OK, kc_tray_stop(ctx));
    fail += expect_int("stop is idempotent", KC_TRAY_OK, kc_tray_stop(ctx));
    fail += expect_int("other context still accepts menu", KC_TRAY_OK,
        kc_tray_set_menu(other, NULL, 0));
    fail += expect_int("stopping one context does not stop the other run", KC_TRAY_OK,
        kc_tray_stop(other));
    fail += expect_int("other context run returns after its own stop", KC_TRAY_OK,
        kc_tray_run(other));
    fail += expect_int("close stopped context", KC_TRAY_OK, kc_tray_close(ctx));
    fail += expect_int("close other context", KC_TRAY_OK, kc_tray_close(other));

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tray_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tray_close(void) {
    const char *name = "kc_tray_close";
    const char *detail = "close releases context";
    kc_tray_t *ctx;
    int fail = 0;

    fail += expect_int("close accepts NULL", KC_TRAY_OK, kc_tray_close(NULL));
    if (!open_tray(&ctx)) return 1;
    fail += expect_int("close releases context", KC_TRAY_OK, kc_tray_close(ctx));

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tray_get_error.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tray_get_error(void) {
    const char *name = "kc_tray_get_error";
    const char *detail = "get_error returns NULL when no error";
    kc_tray_t *ctx;
    int fail = 0;

    fail += expect_true("get_error returns NULL for NULL ctx",
        kc_tray_get_error(NULL) == NULL);
    if (!open_tray(&ctx)) return 1;
    fail += expect_true("get_error returns NULL initially",
        kc_tray_get_error(ctx) == NULL);
    kc_tray_close(ctx);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests the tray CLI argument validation.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tray_cli(void) {
    const char *name = "tray CLI";
    const char *detail = "argument validation happens before opening";
    int cli_enabled = KC_TRAY_TEST_CLI[0] != '\0';
    int fail = 0;

#ifdef _WIN32
    cli_enabled = 1;
#endif
    if (!cli_enabled) {
        case_result(fail, name, detail);
        return 0;
    }

    {
        char out[4096];
        char err[4096];
        int status = 0;
        char *help[] = { (char *)KC_TRAY_TEST_CLI, "--help", (char *)NULL };
        char *version[] = { (char *)KC_TRAY_TEST_CLI, "-v", (char *)NULL };
        char *unknown[] = { (char *)KC_TRAY_TEST_CLI, "--nope", (char *)NULL };
        char *item_without_exec[] = { (char *)KC_TRAY_TEST_CLI, "--item", "Label", "action", (char *)NULL };
        char *empty_menu[] = { (char *)KC_TRAY_TEST_CLI, "--icon", "foo", (char *)NULL };

        fail += expect_int("CLI --help exits 0", 0,
            test_cli_run(help, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_int("CLI --version exits 0", 0,
            test_cli_run(version, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_int("CLI unknown option exits 1", 1,
            test_cli_run(unknown, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI unknown option diagnostic",
            strstr(err, "unknown option") != NULL);
        fail += expect_int("CLI --item without --exec exits 1", 1,
            test_cli_run(item_without_exec, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI missing --exec diagnostic",
            strstr(err, "missing --exec") != NULL);
        fail += expect_int("CLI empty menu exits 1", 1,
            test_cli_run(empty_menu, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI empty menu diagnostic",
            strstr(err, "no menu items") != NULL);
    }

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests the tray CLI process-execution configuration contract.
 * Verifies strict parse-time rejection of malformed execution
 * configurations; this runs headlessly because execution itself requires
 * menu activation in a desktop session.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tray_cli_exec(void) {
    const char *name = "tray CLI exec";
    const char *detail = "execution configuration is parsed strictly";
    int cli_enabled = KC_TRAY_TEST_CLI[0] != '\0';
    int fail = 0;

#ifdef _WIN32
    cli_enabled = 1;
#endif
    if (!cli_enabled) {
        case_result(fail, name, detail);
        return 0;
    }

    {
        char out[4096];
        char err[4096];
        int status = 0;
        char *reserved[] = { (char *)KC_TRAY_TEST_CLI, "--item", "Run", "kc:tray:quit",
            "--exec", "/bin/true", (char *)NULL };
        char *duplicate[] = { (char *)KC_TRAY_TEST_CLI,
            "--item", "One", "act", "--exec", "/bin/true",
            "--sep",
            "--item", "Two", "act", "--exec", "/bin/true", (char *)NULL };
        char *no_exec_value[] = { (char *)KC_TRAY_TEST_CLI,
            "--item", "One", "act", "--exec", (char *)NULL };
        char *no_arg_value[] = { (char *)KC_TRAY_TEST_CLI,
            "--item", "One", "act", "--exec", "/bin/true", "--arg", (char *)NULL };
        char *arg_before_exec[] = { (char *)KC_TRAY_TEST_CLI,
            "--item", "One", "act", "--arg", "x", (char *)NULL };

        fail += expect_int("CLI reserved quit action exits 1", 1,
            test_cli_run(reserved, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI reserved quit diagnostic",
            strstr(err, "reserved") != NULL);
        fail += expect_int("CLI duplicate action exits 1", 1,
            test_cli_run(duplicate, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI duplicate action diagnostic",
            strstr(err, "duplicate action") != NULL);
        fail += expect_int("CLI missing --exec value exits 1", 1,
            test_cli_run(no_exec_value, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI missing --exec value diagnostic",
            strstr(err, "missing value for --exec") != NULL);
        fail += expect_int("CLI missing --arg value exits 1", 1,
            test_cli_run(no_arg_value, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI missing --arg value diagnostic",
            strstr(err, "missing value for --arg") != NULL);
        fail += expect_int("CLI --arg before --exec exits 1", 1,
            test_cli_run(arg_before_exec, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI --arg before --exec diagnostic",
            strstr(err, "missing --exec") != NULL);
    }

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, 1 on failure.
 */
static int case_all(void) {
    int cli_enabled;
    int rc = 0;

    cli_enabled = KC_TRAY_TEST_CLI[0] != '\0';
#ifdef _WIN32
    cli_enabled = 1;
#endif
    test_case_total = cli_enabled ? 12 : 10;
    test_case_current = 0;
    run_case(&rc, case_kc_tray_options_default);
    run_case(&rc, case_kc_tray_options_set);
    run_case(&rc, case_kc_tray_options_free);
    run_case(&rc, case_kc_tray_version);
    run_case(&rc, case_kc_tray_open);
    run_case(&rc, case_kc_tray_set_menu);
    run_case(&rc, case_kc_tray_run);
    run_case(&rc, case_kc_tray_stop);
    run_case(&rc, case_kc_tray_close);
    run_case(&rc, case_kc_tray_get_error);
    if (cli_enabled) {
        run_case(&rc, case_kc_tray_cli);
        run_case(&rc, case_kc_tray_cli_exec);
    }
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one libtray public API test case.
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
    if (strcmp(argv[1], "kc_tray_options_default") == 0) return case_kc_tray_options_default();
    if (strcmp(argv[1], "kc_tray_options_set") == 0) return case_kc_tray_options_set();
    if (strcmp(argv[1], "kc_tray_options_free") == 0) return case_kc_tray_options_free();
    if (strcmp(argv[1], "kc_tray_version") == 0) return case_kc_tray_version();
    if (strcmp(argv[1], "kc_tray_open") == 0) return case_kc_tray_open();
    if (strcmp(argv[1], "kc_tray_set_menu") == 0) return case_kc_tray_set_menu();
    if (strcmp(argv[1], "kc_tray_run") == 0) return case_kc_tray_run();
    if (strcmp(argv[1], "kc_tray_stop") == 0) return case_kc_tray_stop();
    if (strcmp(argv[1], "kc_tray_close") == 0) return case_kc_tray_close();
    if (strcmp(argv[1], "kc_tray_get_error") == 0) return case_kc_tray_get_error();
    if (strcmp(argv[1], "kc_tray_cli") == 0) return case_kc_tray_cli();
    if (strcmp(argv[1], "kc_tray_cli_exec") == 0) return case_kc_tray_cli_exec();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
