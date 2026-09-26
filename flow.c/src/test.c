/**
 * test.c - libflow public API and CLI contract tests.
 * Summary: Validates opened flow runtimes, overrides, execution, and the shipped CLI.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libflow.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef FLOW_TEST_CLI
#define FLOW_TEST_CLI ""
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
 * Verifies one boolean result.
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
 * Verifies one string result.
 * @param name Check name.
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
 * Verifies that output contains a substring anywhere in the buffer.
 * @param name Check name.
 * @param output Output buffer.
 * @param output_size Output size.
 * @param needle Expected substring.
 * @return 0 on success, 1 on failure.
 */
static int expect_output_contains(const char *name,
const char *output, size_t output_size, const char *needle)
{
    size_t needle_len;
    size_t i;

    if (output == NULL || output_size == 0) {
        printf("[FAIL] %s: expected output containing '%s', got empty\n",
            name, needle);
        return 1;
    }
    needle_len = strlen(needle);
    if (output_size < needle_len) {
        printf("[FAIL] %s: expected output containing '%s', got '%.*s'\n",
            name, needle, (int)output_size, output);
        return 1;
    }
    for (i = 0; i + needle_len <= output_size; i++) {
        if (memcmp(output + i, needle, needle_len) == 0) {
            return 0;
        }
    }
    printf("[FAIL] %s: expected output containing '%s', got '%.*s'\n",
        name, needle, (int)output_size, output);
    return 1;
}

/**
 * Creates one temporary directory for generated flow fixtures.
 * @param out Buffer to receive the path.
 * @param cap Buffer capacity.
 * @return 0 on success, 1 on failure.
 */
static int make_temp_dir(char *out, size_t cap) {
    const char *base;
    int r;

#ifdef _WIN32
    base = getenv("TEMP");
    if (base == NULL || *base == '\0') base = ".";
    {
        int i;
        for (i = 0; i < 100; i++) {
            r = snprintf(out, cap, "%s/flow-test-%ld-%d", base,
                (long)_getpid(), i);
            if (r < 0 || (size_t)r >= cap) return 1;
            if (_mkdir(out) == 0) return 0;
            if (errno != EEXIST) return 1;
        }
    }
    return 1;
#else
    base = getenv("TMPDIR");
    if (base == NULL || *base == '\0') base = "/tmp";
    r = snprintf(out, cap, "%s/flow-test-XXXXXX", base);
    if (r < 0 || (size_t)r >= cap) return 1;
    if (mkdtemp(out) == NULL) return 1;
    return 0;
#endif
}

/**
 * Writes one fixture file into a directory.
 * @param dir Directory path.
 * @param name File name.
 * @param content File content.
 * @return 0 on success, 1 on failure.
 */
static int write_fixture(const char *dir, const char *name, const char *content) {
    char path[512];
    FILE *f;
    size_t len;

    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) return 1;
    f = fopen(path, "w");
    if (f == NULL) return 1;
    len = strlen(content);
    if (fwrite(content, 1, len, f) != len) {
        fclose(f);
        return 1;
    }
    fclose(f);
    return 0;
}

/**
 * Joins a directory and file name into a path buffer.
 * @param dir Directory path.
 * @param name File name.
 * @param out Output buffer.
 * @param cap Buffer capacity.
 * @return 0 on success, 1 on failure.
 */
static int join_path(const char *dir, const char *name, char *out, size_t cap) {
    int r;

    r = snprintf(out, cap, "%s/%s", dir, name);
    return (r < 0 || (size_t)r >= cap) ? 1 : 0;
}

/**
 * Writes generated runtime fixtures.
 * @param dir Temporary directory path.
 * @return 0 on success, 1 on failure.
 */
static int write_all_fixtures(const char *dir) {
#ifdef _WIN32
    if (write_fixture(dir, "stdin.flow",
        "flow.link=root\n"
        "node.root.exec=more\n") != 0)
        return 1;
    if (write_fixture(dir, "overlay.flow",
        "flow.id=overlay\n"
        "flow.link=default\n"
        "node.default.exec=echo default\n"
        "node.server.exec=echo <node.param.msg>\n"
        "node.install.exec=echo install\n") != 0)
        return 1;
    if (write_fixture(dir, "fanout.flow",
        "flow.id=fanout\n"
        "flow.param.greeting=Hi\n"
        "flow.link=root\n"
        "node.root.link=left\n"
        "node.root.link=right\n"
        "node.left.exec=echo <flow.param.greeting> Left\n"
        "node.right.exec=echo <flow.param.greeting> Right\n") != 0)
        return 1;
    if (write_fixture(dir, "cycle.flow",
        "flow.link=left\n"
        "node.left.link=right\n"
        "node.right.link=left\n") != 0)
        return 1;
#else
    if (write_fixture(dir, "stdin.flow",
        "flow.link=root\n"
        "node.root.exec=cat\n") != 0)
        return 1;
    if (write_fixture(dir, "overlay.flow",
        "flow.id=overlay\n"
        "flow.link=default\n"
        "node.default.exec=printf \"%s\" \"default\"\n"
        "node.server.exec=printf \"%s\" \"<node.param.msg>\"\n"
        "node.install.exec=printf \"%s\" \"install\"\n") != 0)
        return 1;
    if (write_fixture(dir, "fanout.flow",
        "flow.id=fanout\n"
        "flow.param.greeting=Hi\n"
        "flow.link=root\n"
        "node.root.link=left\n"
        "node.root.link=right\n"
        "node.left.exec=printf \"%s\" \"<flow.param.greeting> Left\"\n"
        "node.right.exec=printf \"%s\" \"<flow.param.greeting> Right\"\n") != 0)
        return 1;
    if (write_fixture(dir, "cycle.flow",
        "flow.link=left\n"
        "node.left.link=right\n"
        "node.right.link=left\n") != 0)
        return 1;
#endif
    if (write_fixture(dir, "empty.flow",
        "flow.link=pass\n"
        "node.pass.exec=\n") != 0)
        return 1;
    if (write_fixture(dir, "size.flow",
        "flow.id=sizefan\n"
        "flow.link=root\n"
        "node.root.link=left\n"
        "node.root.link=right\n"
        "node.left.exec=echo Left\n"
        "node.right.exec=echo Right\n") != 0)
        return 1;
    return 0;
}

#ifdef _WIN32
/**
 * Append one argument to a Windows command line.
 * @param cmd Command-line buffer.
 * @param cap Buffer capacity in wide characters.
 * @param arg Argument to append.
 * @return 0 on success, 1 on failure.
 */
static int cli_append_arg(wchar_t *cmd, size_t cap, const wchar_t *arg) {
    size_t used = wcslen(cmd);
    size_t len = wcslen(arg);
    int quote = len == 0 || wcschr(arg, L' ') != NULL ||
        wcschr(arg, L'\t') != NULL || wcschr(arg, L'"') != NULL;
    size_t i;

    if (used > 0) {
        if (used + 1 >= cap) return 1;
        cmd[used++] = L' ';
    }
    if (quote) {
        if (used + 1 >= cap) return 1;
        cmd[used++] = L'"';
    }
    for (i = 0; i < len; i++) {
        if (arg[i] == L'"') {
            if (used + 1 >= cap) return 1;
            cmd[used++] = L'\\';
        }
        if (used + 1 >= cap) return 1;
        cmd[used++] = arg[i];
    }
    if (quote) {
        if (used + 1 >= cap) return 1;
        cmd[used++] = L'"';
    }
    cmd[used] = L'\0';
    return 0;
}

/**
 * Convert UTF-8 text to one Windows wide string.
 * @param input UTF-8 source text.
 * @param output Wide destination buffer.
 * @param cap Destination capacity.
 * @return 0 on success, 1 on failure.
 */
static int cli_to_wide(const char *input, wchar_t *output, size_t cap) {
    return MultiByteToWideChar(CP_UTF8, 0, input, -1, output, (int)cap) > 0 ? 0 : 1;
}

/**
 * Read one Windows pipe into a text buffer.
 * @param pipe Pipe handle.
 * @param buffer Destination buffer.
 * @param size Destination size.
 * @return None.
 */
static void cli_read_pipe(HANDLE pipe, char *buffer, size_t size) {
    DWORD got;
    size_t used = 0;

    while (used + 1 < size &&
            ReadFile(pipe, buffer + used, (DWORD)(size - used - 1), &got, NULL) &&
            got > 0) {
        used += got;
    }
    buffer[used] = '\0';
}

/**
 * Run the flow CLI with captured standard streams.
 * @param argv Argument vector.
 * @param input Optional standard input text.
 * @param out Standard output buffer.
 * @param out_size Standard output capacity.
 * @param err Standard error buffer.
 * @param err_size Standard error capacity.
 * @param status Output process status.
 * @return 0 on launch success, 1 on failure.
 */
static int cli_run(
    char *const argv[],
    const char *input,
    char *out,
    size_t out_size,
    char *err,
    size_t err_size,
    int *status
) {
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    HANDLE in_pipe[2], out_pipe[2], err_pipe[2];
    wchar_t exe[MAX_PATH], cmd[32768], wide[4096];
    DWORD exit_code, written;
    int i;

    if (cli_to_wide(FLOW_TEST_CLI, exe, MAX_PATH)) return 1;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;
    if (!CreatePipe(&in_pipe[0], &in_pipe[1], &sa, 0) ||
            !CreatePipe(&out_pipe[0], &out_pipe[1], &sa, 0) ||
            !CreatePipe(&err_pipe[0], &err_pipe[1], &sa, 0)) return 1;
    SetHandleInformation(in_pipe[1], HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_pipe[0], HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_pipe[0], HANDLE_FLAG_INHERIT, 0);

    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_pipe[0];
    si.hStdOutput = out_pipe[1];
    si.hStdError = err_pipe[1];

    cmd[0] = L'\0';
    if (cli_append_arg(cmd, 32768, exe)) return 1;
    for (i = 1; argv[i] != NULL; i++) {
        if (cli_to_wide(argv[i], wide, 4096) ||
                cli_append_arg(cmd, 32768, wide)) return 1;
    }

    if (!CreateProcessW(exe, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        return 1;
    }
    CloseHandle(in_pipe[0]);
    CloseHandle(out_pipe[1]);
    CloseHandle(err_pipe[1]);
    if (input != NULL && input[0] != '\0') {
        (void)WriteFile(in_pipe[1], input, (DWORD)strlen(input), &written, NULL);
    }
    CloseHandle(in_pipe[1]);
    cli_read_pipe(out_pipe[0], out, out_size);
    cli_read_pipe(err_pipe[0], err, err_size);
    CloseHandle(out_pipe[0]);
    CloseHandle(err_pipe[0]);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    *status = (int)exit_code;
    return 0;
}
#else
/**
 * Run the flow CLI with captured standard streams.
 * @param argv Argument vector.
 * @param input Optional standard input text.
 * @param out Standard output buffer.
 * @param out_size Standard output capacity.
 * @param err Standard error buffer.
 * @param err_size Standard error capacity.
 * @param status Output process status.
 * @return 0 on launch success, 1 on failure.
 */
static int cli_run(
    char *const argv[],
    const char *input,
    char *out,
    size_t out_size,
    char *err,
    size_t err_size,
    int *status
) {
    int in_pipe[2], out_pipe[2], err_pipe[2];
    pid_t pid;
    ssize_t got;
    size_t pos;
    int wait_status;

    if (pipe(in_pipe) || pipe(out_pipe) || pipe(err_pipe)) return 1;
    pid = fork();
    if (pid < 0) return 1;
    if (pid == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        execv(argv[0], argv);
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);
    if (input != NULL && input[0] != '\0') {
        size_t len = strlen(input);
        size_t written = 0;
        while (written < len &&
                (got = write(in_pipe[1], input + written, len - written)) > 0) {
            written += (size_t)got;
        }
    }
    close(in_pipe[1]);

    pos = 0;
    while (pos + 1 < out_size &&
            (got = read(out_pipe[0], out + pos, out_size - pos - 1)) > 0) {
        pos += (size_t)got;
    }
    out[pos] = '\0';
    close(out_pipe[0]);

    pos = 0;
    while (pos + 1 < err_size &&
            (got = read(err_pipe[0], err + pos, err_size - pos - 1)) > 0) {
        pos += (size_t)got;
    }
    err[pos] = '\0';
    close(err_pipe[0]);

    if (waitpid(pid, &wait_status, 0) < 0) return 1;
    *status = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : 1;
    return 0;
}
#endif

typedef struct {
    void *data;
    size_t size;
    char error[256];
    int status;
    int calls;
    atomic_int done;
} test_run_result_t;

static void test_sleep_ms(unsigned milliseconds);

/**
 * Receive one terminal flow result for contract tests.
 * @param status Completion status.
 * @param data Owned successful output.
 * @param size Output byte count.
 * @param error Borrowed contextual error.
 * @param userdata Test result state.
 * @return None.
 */
static void test_run_handler(
    int status,
    void *data,
    size_t size,
    const char *error,
    void *userdata
) {
    test_run_result_t *result = (test_run_result_t *)userdata;

    result->status = status;
    result->data = data;
    result->size = size;
    result->calls++;
    if (error != NULL) {
        snprintf(result->error, sizeof(result->error), "%s", error);
    } else {
        result->error[0] = '\0';
    }
    atomic_store_explicit(&result->done, 1, memory_order_release);
}

/**
 * Wait for one test callback to complete.
 * @param result Test result state.
 * @return None.
 */
static void test_run_wait(test_run_result_t *result) {
    while (!atomic_load_explicit(&result->done, memory_order_acquire)) {
        test_sleep_ms(1U);
    }
}

/**
 * Run one flow synchronously through the public callback contract.
 * @param flow Opened flow.
 * @param entry Optional explicit entry.
 * @param input Optional input bytes.
 * @param input_size Input byte size.
 * @param out_data Receives owned output.
 * @param out_size Receives output size.
 * @return Run completion status.
 */
static int flow_run_sync(
    kc_flow_t *flow,
    const char *entry,
    const void *input,
    size_t input_size,
    void **out_data,
    size_t *out_size
) {
    kc_flow_run_t *run = NULL;
    test_run_result_t result;
    int rc;

    if (out_data) *out_data = NULL;
    if (out_size) *out_size = 0;
    if (!out_data || !out_size) return KC_FLOW_ERROR;

    memset(&result, 0, sizeof(result));
    atomic_init(&result.done, 0);
    rc = kc_flow_run(
        flow,
        &run,
        entry,
        input,
        input_size,
        test_run_handler,
        &result
    );
    if (rc != KC_FLOW_OK) return rc;

    test_run_wait(&result);
    if (result.status == KC_FLOW_OK) {
        *out_data = result.data;
        *out_size = result.size;
    } else {
        kc_flow_free(result.data);
    }
    rc = result.status;
    kc_flow_run_close(run);
    return rc;
}

/**
 * Sleep briefly so a started run can enter its current step.
 * @param milliseconds Delay in milliseconds.
 * @return None.
 */
static void test_sleep_ms(unsigned milliseconds) {
#ifdef _WIN32
    Sleep((DWORD)milliseconds);
#else
    struct timespec delay;
    delay.tv_sec = (time_t)(milliseconds / 1000U);
    delay.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
    (void)nanosleep(&delay, NULL);
#endif
}

/**
 * Tests kc_flow_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_open(void) {
    const char *name = "kc_flow_open";
    const char *detail = "opens one existing flow file and owns its runtime path";
    kc_flow_t *flow = NULL;
    char tmpdir[320];
    char path[640];
    int fail = 0;

    fail += expect_int("open NULL out", KC_FLOW_ERROR, kc_flow_open(NULL, "x.flow"));
    fail += expect_int("open NULL path", KC_FLOW_ERROR, kc_flow_open(&flow, NULL));
    fail += expect_int("open missing path", KC_FLOW_ERROR,
        kc_flow_open(&flow, "/nonexistent/path.flow"));
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_all_fixtures(tmpdir) != 0) return 1;
    if (join_path(tmpdir, "overlay.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("open existing flow", KC_FLOW_OK, kc_flow_open(&flow, path));
    fail += expect_true("open sets runtime", flow != NULL);
    kc_flow_close(flow);
    case_result(fail, name, detail);
    return fail != 0;
}

/**
 * Tests kc_flow_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_close(void) {
    const char *name = "kc_flow_close";
    const char *detail = "releases one opened runtime and accepts NULL";
    kc_flow_t *flow = NULL;
    char tmpdir[320];
    char path[640];
    int fail = 0;

    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_all_fixtures(tmpdir) != 0) return 1;
    if (join_path(tmpdir, "overlay.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("open before close", KC_FLOW_OK, kc_flow_open(&flow, path));
    kc_flow_close(NULL);
    kc_flow_close(flow);
    case_result(fail, name, detail);
    return fail != 0;
}

/**
 * Tests kc_flow_set.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_set(void) {
    const char *name = "kc_flow_set";
    const char *detail = "adds temporary ordered value overrides to the opened flow";
    kc_flow_t *flow = NULL;
    char tmpdir[320];
    char path[640];
    void *out = NULL;
    size_t out_size = 0;
    int fail = 0;

    fail += expect_int("set NULL runtime", KC_FLOW_ERROR,
        kc_flow_set(NULL, "flow.testkey", "second"));
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_fixture(tmpdir, "replace.flow",
        "flow.testkey=first\n"
        "flow.link=root\n"
        "node.root.exec=echo key=<flow.testkey>\n") != 0) return 1;
    if (join_path(tmpdir, "replace.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("open for set", KC_FLOW_OK, kc_flow_open(&flow, path));
    fail += expect_int("set NULL key", KC_FLOW_ERROR, kc_flow_set(flow, NULL, "x"));
    fail += expect_int("set NULL value", KC_FLOW_ERROR,
        kc_flow_set(flow, "flow.testkey", NULL));
    fail += expect_int("set invalid key", KC_FLOW_ERROR,
        kc_flow_set(flow, "bad key", "x"));
    fail += expect_int("set value override", KC_FLOW_OK,
        kc_flow_set(flow, "flow.testkey", "second"));
    fail += expect_int("run with set", KC_FLOW_OK,
        flow_run_sync(flow, NULL, NULL, 0, &out, &out_size));
    fail += expect_output_contains("set affects run", (const char *)out,
        out_size, "key=second");
    kc_flow_free(out);
    kc_flow_close(flow);
    case_result(fail, name, detail);
    return fail != 0;
}

/**
 * Tests kc_flow_unset.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_unset(void) {
    const char *name = "kc_flow_unset";
    const char *detail = "removes exact records before later overrides are applied";
    kc_flow_t *flow = NULL;
    char tmpdir[320];
    char path[640];
    void *out = NULL;
    size_t out_size = 0;
    int fail = 0;

    fail += expect_int("unset NULL runtime", KC_FLOW_ERROR,
        kc_flow_unset(NULL, "flow.link"));
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_all_fixtures(tmpdir) != 0) return 1;
    if (join_path(tmpdir, "overlay.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("open for unset", KC_FLOW_OK, kc_flow_open(&flow, path));
    fail += expect_int("unset NULL key", KC_FLOW_ERROR, kc_flow_unset(flow, NULL));
    fail += expect_int("unset invalid key", KC_FLOW_ERROR,
        kc_flow_unset(flow, "bad key"));
    fail += expect_int("unset entry", KC_FLOW_OK, kc_flow_unset(flow, "flow.link"));
    fail += expect_int("set replacement entry", KC_FLOW_OK,
        kc_flow_set(flow, "flow.link", "install"));
    fail += expect_int("run after unset/set", KC_FLOW_OK,
        flow_run_sync(flow, NULL, NULL, 0, &out, &out_size));
    fail += expect_output_contains("unset/set output", (const char *)out,
        out_size, "install");
    kc_flow_free(out);
    kc_flow_close(flow);
    case_result(fail, name, detail);
    return fail != 0;
}

/**
 * Tests ordered override semantics.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_override_order(void) {
    const char *name = "kc_flow_override_order";
    const char *detail = "preserves set/unset operation order on independent runtimes";
    kc_flow_t *a = NULL;
    kc_flow_t *b = NULL;
    char tmpdir[320];
    char path[640];
    void *out = NULL;
    size_t out_size = 0;
    int fail = 0;

    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_all_fixtures(tmpdir) != 0) return 1;
    if (join_path(tmpdir, "overlay.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("open runtime A", KC_FLOW_OK, kc_flow_open(&a, path));
    fail += expect_int("open runtime B", KC_FLOW_OK, kc_flow_open(&b, path));

    fail += expect_int("A unset entry", KC_FLOW_OK, kc_flow_unset(a, "flow.link"));
    fail += expect_int("A set message", KC_FLOW_OK,
        kc_flow_set(a, "node.server.param.msg", "Hello"));
    fail += expect_int("A set entry", KC_FLOW_OK,
        kc_flow_set(a, "flow.link", "server"));
    fail += expect_int("A run", KC_FLOW_OK,
        flow_run_sync(a, NULL, NULL, 0, &out, &out_size));
    fail += expect_output_contains("A output", (const char *)out, out_size, "Hello");
    kc_flow_free(out);
    out = NULL;
    out_size = 0;

    fail += expect_int("B set message", KC_FLOW_OK,
        kc_flow_set(b, "node.server.param.msg", "Hello"));
    fail += expect_int("B set entry", KC_FLOW_OK,
        kc_flow_set(b, "flow.link", "server"));
    fail += expect_int("B unset entry", KC_FLOW_OK,
        kc_flow_unset(b, "flow.link"));
    fail += expect_int("B run", KC_FLOW_OK,
        flow_run_sync(b, NULL, NULL, 0, &out, &out_size));
    fail += expect_true("B output empty", out == NULL && out_size == 0);

    kc_flow_close(b);
    kc_flow_close(a);
    case_result(fail, name, detail);
    return fail != 0;
}

/**
 * Tests kc_flow_run callback completion and kc_flow_run_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_run(void) {
    const char *name = "kc_flow_run";
    const char *detail = "starts independent runs with entries, input, fan-out, and owned bytes";
    kc_flow_t *flow = NULL;
    char tmpdir[320];
    char path[640];
    void *out = (void *)"sentinel";
    size_t out_size = 123;
    int fail = 0;

    fail += expect_int("run NULL flow", KC_FLOW_ERROR,
        flow_run_sync(NULL, NULL, NULL, 0, &out, &out_size));
    fail += expect_true("NULL runtime clears output", out == NULL && out_size == 0);

    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_all_fixtures(tmpdir) != 0) return 1;

    if (join_path(tmpdir, "fanout.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("open fanout", KC_FLOW_OK, kc_flow_open(&flow, path));
    {
        kc_flow_run_t *invalid_run = NULL;
        fail += expect_int("run requires handler", KC_FLOW_ERROR,
            kc_flow_run(
                flow,
                &invalid_run,
                NULL,
                NULL,
                0,
                NULL,
                NULL
            ));
        fail += expect_true("missing handler creates no run", invalid_run == NULL);
    }
    fail += expect_int("run declared fanout", KC_FLOW_OK,
        flow_run_sync(flow, NULL, NULL, 0, &out, &out_size));
    fail += expect_output_contains("fanout left", (const char *)out, out_size, "Hi Left");
    fail += expect_output_contains("fanout right", (const char *)out, out_size, "Hi Right");
    kc_flow_free(out);
    kc_flow_close(flow);
    flow = NULL;
    out = NULL;
    out_size = 0;

    if (join_path(tmpdir, "overlay.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("open overlay", KC_FLOW_OK, kc_flow_open(&flow, path));
    fail += expect_int("run explicit entry", KC_FLOW_OK,
        flow_run_sync(flow, "default", NULL, 0, &out, &out_size));
    fail += expect_output_contains("explicit entry output", (const char *)out,
        out_size, "default");
    kc_flow_free(out);
    out = NULL;
    out_size = 0;
    fail += expect_int("empty entry rejected", KC_FLOW_ERROR,
        flow_run_sync(flow, "", NULL, 0, &out, &out_size));
    fail += expect_true("empty entry clears output", out == NULL && out_size == 0);
    fail += expect_int("input size requires buffer", KC_FLOW_ERROR,
        flow_run_sync(flow, NULL, NULL, 5, &out, &out_size));
    kc_flow_close(flow);
    flow = NULL;

    if (join_path(tmpdir, "stdin.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("open stdin flow", KC_FLOW_OK, kc_flow_open(&flow, path));
    fail += expect_int("run stdin", KC_FLOW_OK,
        flow_run_sync(flow, NULL, "Pipe Input", 10, &out, &out_size));
    fail += expect_output_contains("stdin output", (const char *)out,
        out_size, "Pipe Input");
    kc_flow_free(out);
    kc_flow_close(flow);
    flow = NULL;
    out = NULL;
    out_size = 0;

    {
        kc_flow_run_t *run = NULL;
        test_run_result_t result;
        memset(&result, 0, sizeof(result));
        atomic_init(&result.done, 0);
        if (join_path(tmpdir, "stdin.flow", path, sizeof(path)) != 0) return 1;
        fail += expect_int("open flow for detached run", KC_FLOW_OK,
            kc_flow_open(&flow, path));
        fail += expect_int("start detached run", KC_FLOW_OK,
            kc_flow_run(
                flow,
                &run,
                NULL,
                "Snapshot",
                8,
                test_run_handler,
                &result
            ));
        kc_flow_close(flow);
        flow = NULL;
        test_run_wait(&result);
        fail += expect_int("detached run survives flow close", KC_FLOW_OK,
            result.status);
        fail += expect_int("detached run callback once", 1, result.calls);
        fail += expect_output_contains("detached run output",
            (const char *)result.data, result.size, "Snapshot");
        kc_flow_free(result.data);
        out = NULL;
        out_size = 0;
        kc_flow_run_close(run);
    }

    if (join_path(tmpdir, "empty.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("open empty flow", KC_FLOW_OK, kc_flow_open(&flow, path));
    fail += expect_int("run empty output", KC_FLOW_OK,
        flow_run_sync(flow, NULL, NULL, 0, &out, &out_size));
    fail += expect_true("empty output is NULL/0", out == NULL && out_size == 0);
    kc_flow_close(flow);
    flow = NULL;

    if (join_path(tmpdir, "size.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("open size flow", KC_FLOW_OK, kc_flow_open(&flow, path));
    fail += expect_int("run size fanout", KC_FLOW_OK,
        flow_run_sync(flow, NULL, NULL, 0, &out, &out_size));
    fail += expect_true("size output has bytes", out != NULL && out_size > 0);
    fail += expect_output_contains("size left", (const char *)out, out_size, "Left");
    fail += expect_output_contains("size right", (const char *)out, out_size, "Right");
    kc_flow_free(out);
    kc_flow_close(flow);
    flow = NULL;
    out = NULL;
    out_size = 0;

    if (join_path(tmpdir, "cycle.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("open cycle", KC_FLOW_OK, kc_flow_open(&flow, path));
    fail += expect_int("cycle rejected by run", KC_FLOW_ERROR,
        flow_run_sync(flow, NULL, NULL, 0, &out, &out_size));
    kc_flow_close(flow);

    case_result(fail, name, detail);
    return fail != 0;
}

/**
 * Tests terminal failure delivery through the run callback.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_run_failure(void) {
    const char *name = "kc_flow_run_failure";
    const char *detail = "delivers status, descriptive error, and no output exactly once";
    kc_flow_t *flow = NULL;
    kc_flow_run_t *run = NULL;
    test_run_result_t result;
    char tmpdir[320];
    char path[640];
    int fail = 0;

    memset(&result, 0, sizeof(result));
    atomic_init(&result.done, 0);
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_all_fixtures(tmpdir) != 0) return 1;
    if (join_path(tmpdir, "cycle.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("open cycle for failure", KC_FLOW_OK,
        kc_flow_open(&flow, path));
    fail += expect_int("start failing run", KC_FLOW_OK,
        kc_flow_run(
            flow,
            &run,
            NULL,
            NULL,
            0,
            test_run_handler,
            &result
        ));
    test_run_wait(&result);
    fail += expect_int("failure status", KC_FLOW_ERROR, result.status);
    fail += expect_int("failure callback once", 1, result.calls);
    fail += expect_true("failure error is descriptive", result.error[0] != '\0');
    fail += expect_true("failed run has no output",
        result.data == NULL && result.size == 0);
    kc_flow_run_close(run);
    kc_flow_close(flow);

    case_result(fail, name, detail);
    return fail != 0;
}

/**
 * Tests cooperative stop on an independent run.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_run_stop(void) {
    const char *name = "kc_flow_run_stop";
    const char *detail = "stops after the current step without starting the next step";
    kc_flow_t *flow = NULL;
    kc_flow_run_t *run = NULL;
    char tmpdir[320];
    char path[640];
    test_run_result_t result;
    int fail = 0;

    memset(&result, 0, sizeof(result));
    atomic_init(&result.done, 0);

    fail += expect_int("stop NULL run", KC_FLOW_ERROR, kc_flow_run_stop(NULL));
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
#ifdef _WIN32
    if (write_fixture(tmpdir, "stop.flow",
        "flow.link=first\n"
        "node.first.exec=ping -n 2 127.0.0.1 >nul\n"
        "node.first.link=second\n"
        "node.second.exec=echo second\n") != 0) return 1;
#else
    if (write_fixture(tmpdir, "stop.flow",
        "flow.link=first\n"
        "node.first.exec=sleep 1\n"
        "node.first.link=second\n"
        "node.second.exec=printf second\n") != 0) return 1;
#endif
    if (join_path(tmpdir, "stop.flow", path, sizeof(path)) != 0) return 1;
    fail += expect_int("open stop flow", KC_FLOW_OK, kc_flow_open(&flow, path));
    fail += expect_int("start non-blocking run", KC_FLOW_OK,
        kc_flow_run(
            flow,
            &run,
            NULL,
            NULL,
            0,
            test_run_handler,
            &result
        ));
    test_sleep_ms(100U);
    fail += expect_int("request cooperative stop", KC_FLOW_OK, kc_flow_run_stop(run));
    test_run_wait(&result);
    fail += expect_int("stopped run status", KC_FLOW_ESTOP, result.status);
    fail += expect_int("stopped run callback once", 1, result.calls);
    fail += expect_true("stopped run has no final output",
        result.data == NULL && result.size == 0);
    fail += expect_string("stopped run error", "flow stopped", result.error);
    kc_flow_run_close(run);
    kc_flow_close(flow);

    case_result(fail, name, detail);
    return fail != 0;
}

/**
 * Tests kc_flow_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_free(void) {
    const char *name = "kc_flow_free";
    const char *detail = "releases library output and accepts NULL";
    char *output = (char *)malloc(8);
    int fail = 0;

    if (output == NULL) return 1;
    memcpy(output, "owned", 6);
    kc_flow_free(output);
    kc_flow_free(NULL);
    case_result(fail, name, detail);
    return fail != 0;
}

/**
 * Tests kc_flow_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_version(void) {
    const char *name = "kc_flow_version";
    const char *detail = "returns the generated build version";
    int fail = expect_true("version is non-zero", kc_flow_version() != 0U);

    case_result(fail, name, detail);
    return fail != 0;
}

/**
 * Tests the complete shipped CLI contract.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_flow_cli(void) {
    const char *name = "kc_flow_cli";
    const char *detail = "preserves file, link, overrides, stdin, help, version, and errors";
    char tmpdir[320];
    char overlay[640];
    char stdin_path[640];
    char out[16384];
    char err[8192];
    int status = 0;
    int fail = 0;

    if (FLOW_TEST_CLI[0] == '\0') {
        case_result(1, name, detail);
        return 1;
    }
    if (make_temp_dir(tmpdir, sizeof(tmpdir)) != 0) return 1;
    if (write_all_fixtures(tmpdir) != 0) return 1;
    if (join_path(tmpdir, "overlay.flow", overlay, sizeof(overlay)) != 0) return 1;
    if (join_path(tmpdir, "stdin.flow", stdin_path, sizeof(stdin_path)) != 0) return 1;

    {
        char *args[] = {(char *)FLOW_TEST_CLI, overlay, NULL};
        fail += expect_int("CLI default launch", 0,
            cli_run(args, NULL, out, sizeof(out), err, sizeof(err), &status));
        fail += expect_int("CLI default status", 0, status);
        fail += expect_true("CLI default output", strstr(out, "default") != NULL);
        fail += expect_true("CLI default stderr empty", err[0] == '\0');
    }
    {
        char *args[] = {
            (char *)FLOW_TEST_CLI, overlay, "--link", "install", NULL
        };
        fail += expect_int("CLI link launch", 0,
            cli_run(args, NULL, out, sizeof(out), err, sizeof(err), &status));
        fail += expect_int("CLI link status", 0, status);
        fail += expect_true("CLI link output", strstr(out, "install") != NULL);
    }
    {
        char *args[] = {
            (char *)FLOW_TEST_CLI,
            overlay,
            "--unset", "flow.link",
            "--set", "flow.link=server",
            "--set", "node.server.param.msg=Hello",
            NULL
        };
        fail += expect_int("CLI overrides launch", 0,
            cli_run(args, NULL, out, sizeof(out), err, sizeof(err), &status));
        fail += expect_int("CLI overrides status", 0, status);
        fail += expect_true("CLI overrides output", strstr(out, "Hello") != NULL);
    }
    {
        char *args[] = {(char *)FLOW_TEST_CLI, stdin_path, NULL};
        fail += expect_int("CLI stdin launch", 0,
            cli_run(args, "Pipe Input", out, sizeof(out), err, sizeof(err), &status));
        fail += expect_int("CLI stdin status", 0, status);
        fail += expect_true("CLI stdin output", strstr(out, "Pipe Input") != NULL);
    }
    {
        char *args[] = {(char *)FLOW_TEST_CLI, "--help", NULL};
        fail += expect_int("CLI help launch", 0,
            cli_run(args, NULL, out, sizeof(out), err, sizeof(err), &status));
        fail += expect_int("CLI help status", 0, status);
        fail += expect_true("CLI help usage", strstr(out, "Usage:") != NULL);
        fail += expect_true("CLI help link", strstr(out, "--link") != NULL);
        fail += expect_true("CLI help set", strstr(out, "--set") != NULL);
        fail += expect_true("CLI help unset", strstr(out, "--unset") != NULL);
    }
    {
        char *args[] = {(char *)FLOW_TEST_CLI, "--version", NULL};
        fail += expect_int("CLI version launch", 0,
            cli_run(args, NULL, out, sizeof(out), err, sizeof(err), &status));
        fail += expect_int("CLI version status", 0, status);
        fail += expect_true("CLI version output", strstr(out, "flow build ") != NULL);
    }
    {
        char *args[] = {(char *)FLOW_TEST_CLI, "--unknown", NULL};
        fail += expect_int("CLI error launch", 0,
            cli_run(args, NULL, out, sizeof(out), err, sizeof(err), &status));
        fail += expect_true("CLI unknown fails", status != 0);
        fail += expect_true("CLI error text", strstr(err, "unknown option") != NULL);
    }

    case_result(fail, name, detail);
    return fail != 0;
}

/**
 * Runs all public API and CLI cases.
 * @return 0 on success, nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;

    test_case_total = 11;
    test_case_current = 0;
    run_case(&rc, case_kc_flow_open);
    run_case(&rc, case_kc_flow_close);
    run_case(&rc, case_kc_flow_set);
    run_case(&rc, case_kc_flow_unset);
    run_case(&rc, case_kc_flow_override_order);
    run_case(&rc, case_kc_flow_run);
    run_case(&rc, case_kc_flow_run_stop);
    run_case(&rc, case_kc_flow_run_failure);
    run_case(&rc, case_kc_flow_free);
    run_case(&rc, case_kc_flow_version);
    run_case(&rc, case_kc_flow_cli);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one public contract test case.
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
    if (strcmp(argv[1], "kc_flow_open") == 0) return case_kc_flow_open();
    if (strcmp(argv[1], "kc_flow_close") == 0) return case_kc_flow_close();
    if (strcmp(argv[1], "kc_flow_set") == 0) return case_kc_flow_set();
    if (strcmp(argv[1], "kc_flow_unset") == 0) return case_kc_flow_unset();
    if (strcmp(argv[1], "kc_flow_override_order") == 0) {
        return case_kc_flow_override_order();
    }
    if (strcmp(argv[1], "kc_flow_run") == 0) return case_kc_flow_run();
    if (strcmp(argv[1], "kc_flow_run_stop") == 0) return case_kc_flow_run_stop();
    if (strcmp(argv[1], "kc_flow_run_failure") == 0) return case_kc_flow_run_failure();
    if (strcmp(argv[1], "kc_flow_free") == 0) return case_kc_flow_free();
    if (strcmp(argv[1], "kc_flow_version") == 0) return case_kc_flow_version();
    if (strcmp(argv[1], "kc_flow_cli") == 0) return case_kc_flow_cli();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
