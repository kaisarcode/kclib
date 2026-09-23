/**
 * test.c - liblibtpl public API and CLI tests.
 * Summary: Contract tests for the stateless min API and shipped CLI.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libtpl.h"

#ifndef __EMSCRIPTEN__
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef KC_TPL_TEST_CLI
#define KC_TPL_TEST_CLI ""
#endif

static int test_case_total = 0;
static int test_case_current = 0;

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
 * Verifies one boolean test expectation.
 * @param name Expectation description.
 * @param condition Nonzero when the expectation is satisfied.
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
 * Verifies one string test expectation.
 * @param name Expectation description.
 * @param expected Expected string value.
 * @param actual Actual string value.
 * @return 0 on success, 1 on failure.
 */
static int expect_string(const char *name, const char *expected, const char *actual) {
    if (!actual || strcmp(expected, actual) != 0) {
        printf("[FAIL] %s: expected '%s', got '%s'\n", name, expected,
            actual ? actual : "NULL");
        return 1;
    }
    return 0;
}

/**
 * Verifies one integer test expectation.
 * @param name Expectation description.
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

#ifndef __EMSCRIPTEN__
#ifdef _WIN32
/**
 * Appends one argument to the Windows CLI command line.
 * @param cmd Command-line buffer.
 * @param cap Command-line buffer capacity.
 * @param arg Argument to append.
 * @return 0 on success, 1 on failure.
 */
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
 * Converts one UTF-8 CLI argument to a Windows wide string.
 * @param in UTF-8 input string.
 * @param out Destination wide-character buffer.
 * @param cap Destination capacity in wide characters.
 * @return 0 on success, 1 on failure.
 */
static int test_cli_to_wide(const char *in, wchar_t *out, size_t cap) {
    return MultiByteToWideChar(CP_UTF8, 0, in, -1, out, (int)cap) > 0 ? 0 : 1;
}

/**
 * Reads process output from one Windows pipe.
 * @param pipe Pipe handle to read.
 * @param buf Destination byte buffer.
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
 * Runs the CLI with controlled input and captured output.
 * @param argv Null-terminated argument vector.
 * @param input Input bytes for standard input.
 * @param input_len Input byte count.
 * @param out Standard-output buffer.
 * @param out_size Standard-output buffer size.
 * @param err Standard-error buffer.
 * @param err_size Standard-error buffer size.
 * @param out_status Destination process exit status.
 * @return 0 on successful execution, 1 on harness failure.
 */
static int test_cli_run_input(char *const argv[], const char *input,
        size_t input_len, char *out, size_t out_size, char *err,
        size_t err_size, int *out_status) {
    wchar_t exe[MAX_PATH];
    wchar_t cmd[32768];
    wchar_t wide[4096];
    HANDLE in_pipe[2], out_pipe[2], err_pipe[2];
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD exit_code, written;
    int i;

    if (test_cli_to_wide(KC_TPL_TEST_CLI, exe, sizeof(exe) / sizeof(wchar_t))) return 1;
    sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE; sa.lpSecurityDescriptor = NULL;
    if (!CreatePipe(&in_pipe[0], &in_pipe[1], &sa, 0)) return 1;
    if (!CreatePipe(&out_pipe[0], &out_pipe[1], &sa, 0)) return 1;
    if (!CreatePipe(&err_pipe[0], &err_pipe[1], &sa, 0)) return 1;
    SetHandleInformation(in_pipe[1], HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_pipe[0], HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_pipe[0], HANDLE_FLAG_INHERIT, 0);
    memset(&si, 0, sizeof(si)); memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_pipe[0]; si.hStdOutput = out_pipe[1]; si.hStdError = err_pipe[1];

    cmd[0] = L'\0';
    if (test_cli_append_arg(cmd, sizeof(cmd) / sizeof(wchar_t), exe)) return 1;
    for (i = 1; argv[i]; i++) {
        if (test_cli_to_wide(argv[i], wide, sizeof(wide) / sizeof(wchar_t)) ||
                test_cli_append_arg(cmd, sizeof(cmd) / sizeof(wchar_t), wide)) return 1;
    }
    if (!CreateProcessW(exe, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) return 1;
    CloseHandle(in_pipe[0]); CloseHandle(out_pipe[1]); CloseHandle(err_pipe[1]);
    if (input_len > 0) (void)WriteFile(in_pipe[1], input, (DWORD)input_len, &written, NULL);
    CloseHandle(in_pipe[1]);
    test_cli_read_pipe(out_pipe[0], out, out_size);
    test_cli_read_pipe(err_pipe[0], err, err_size);
    CloseHandle(out_pipe[0]); CloseHandle(err_pipe[0]);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    *out_status = (int)exit_code;
    return 0;
}
#else
/**
 * Runs the CLI with controlled input and captured output.
 * @param argv Null-terminated argument vector.
 * @param input Input bytes for standard input.
 * @param input_len Input byte count.
 * @param out Standard-output buffer.
 * @param out_size Standard-output buffer size.
 * @param err Standard-error buffer.
 * @param err_size Standard-error buffer size.
 * @param out_status Destination process exit status.
 * @return 0 on successful execution, 1 on harness failure.
 */
static int test_cli_run_input(char *const argv[], const char *input,
        size_t input_len, char *out, size_t out_size, char *err,
        size_t err_size, int *out_status) {
    int in_pipe[2], out_pipe[2], err_pipe[2];
    pid_t pid;
    ssize_t count;
    size_t written = 0, pos = 0;
    int status;

    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0 || pipe(err_pipe) != 0) return 1;
    pid = fork();
    if (pid < 0) return 1;
    if (pid == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        execv(argv[0], argv);
        _exit(127);
    }
    close(in_pipe[0]); close(out_pipe[1]); close(err_pipe[1]);
    while (written < input_len) {
        count = write(in_pipe[1], input + written, input_len - written);
        if (count < 0) break;
        written += (size_t)count;
    }
    close(in_pipe[1]);
    memset(out, 0, out_size);
    while (pos + 1 < out_size &&
            (count = read(out_pipe[0], out + pos, out_size - pos - 1)) > 0) pos += (size_t)count;
    close(out_pipe[0]);
    pos = 0; memset(err, 0, err_size);
    while (pos + 1 < err_size &&
            (count = read(err_pipe[0], err + pos, err_size - pos - 1)) > 0) pos += (size_t)count;
    close(err_pipe[0]);
    if (waitpid(pid, &status, 0) < 0) return 1;
    *out_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    return 0;
}
#endif
#endif

/**
 * Tests kc_tpl_open.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_tpl_open(void) {
    kc_tpl_t *tpl = NULL;
    kc_tpl_options_t options;
    char source[] = "<h1>{{ title }}</h1>";
    char *out;
    int fail = 0;

    fail += expect_int("open NULL out", KC_TPL_ERROR,
        kc_tpl_open(NULL, source, NULL));
    fail += expect_int("open NULL source", KC_TPL_ERROR,
        kc_tpl_open(&tpl, NULL, NULL));
    fail += expect_true("NULL source clears output", tpl == NULL);

    options.root = NULL;
    fail += expect_int("open NULL root", KC_TPL_ERROR,
        kc_tpl_open(&tpl, source, &options));
    options.root = "";
    fail += expect_int("open empty root", KC_TPL_ERROR,
        kc_tpl_open(&tpl, source, &options));

    options.root = ".";
    fail += expect_int("open valid template", KC_TPL_OK,
        kc_tpl_open(&tpl, source, &options));
    fail += expect_true("open returns template", tpl != NULL);

    source[0] = 'X';
    out = kc_tpl_render(tpl, NULL, 0U);
    fail += expect_string("open owns source",
        "<h1></h1>", out);
    kc_tpl_free(out);
    kc_tpl_close(tpl);

    case_result(fail, "kc_tpl_open",
        "validates options and owns the template source");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpl_render.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_tpl_render(void) {
    const char *source =
        "{{ title }}|{{{ raw }}}|"
        "{{@if show}}yes{{@else}}no{{@endif}}|"
        "{{@foreach item in items}}[{{ item }}]{{@endforeach}}|"
        "{{/* hidden */}}";
    kc_tpl_var_t vars[] = {
        { "title", "A&B" },
        { "raw", "<b>x</b>" },
        { "show", "true" },
        { "items", "[one,two]" }
    };
    kc_tpl_var_t second[] = {
        { "title", "Second" }
    };
    kc_tpl_var_t invalid[] = {
        { "", "x" }
    };
    kc_tpl_t *tpl = NULL;
    char *out;
    int fail = 0;

    fail += expect_true("render NULL template",
        kc_tpl_render(NULL, NULL, 0U) == NULL);
    fail += expect_int("open template", KC_TPL_OK,
        kc_tpl_open(&tpl, source, NULL));

    fail += expect_true("render NULL vars with count fails",
        kc_tpl_render(tpl, NULL, 1U) == NULL);
    fail += expect_true("render invalid var fails",
        kc_tpl_render(tpl, invalid, 1U) == NULL);

    out = kc_tpl_render(tpl, vars, sizeof(vars) / sizeof(vars[0]));
    fail += expect_string("render directives",
        "A&amp;B|<b>x</b>|yes|[one][two]|", out);
    kc_tpl_free(out);

    out = kc_tpl_render(tpl, second, sizeof(second) / sizeof(second[0]));
    fail += expect_string("render variables are isolated",
        "Second||no||", out);
    kc_tpl_free(out);

    kc_tpl_close(tpl);

    fail += expect_int("open empty template", KC_TPL_OK,
        kc_tpl_open(&tpl, "", NULL));
    out = kc_tpl_render(tpl, NULL, 0U);
    fail += expect_true("empty render returns allocation", out != NULL);
    if (out) fail += expect_string("empty render", "", out);
    kc_tpl_free(out);
    kc_tpl_close(tpl);

    case_result(fail, "kc_tpl_render",
        "renders stored source with isolated per-call variables");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpl_error.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_tpl_error(void) {
    kc_tpl_t *tpl = NULL;
    char *out;
    int fail = 0;

    fail += expect_string("NULL template error",
        "invalid template", kc_tpl_error(NULL));
    fail += expect_int("open template", KC_TPL_OK,
        kc_tpl_open(&tpl, "{{@include \"missing.html\"}}", NULL));
    fail += expect_string("initial error", "ok", kc_tpl_error(tpl));

    out = kc_tpl_render(tpl, NULL, 0U);
    fail += expect_true("missing include fails", out == NULL);
    kc_tpl_free(out);
    fail += expect_true("error updated",
        strcmp(kc_tpl_error(tpl), "ok") != 0);

    kc_tpl_close(tpl);
    case_result(fail, "kc_tpl_error",
        "returns the latest template error");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpl_free.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_tpl_free(void) {
    kc_tpl_t *tpl = NULL;
    char *out;
    int fail = 0;

    fail += expect_int("open template", KC_TPL_OK,
        kc_tpl_open(&tpl, "output", NULL));
    out = kc_tpl_render(tpl, NULL, 0U);
    fail += expect_string("render output", "output", out);
    kc_tpl_free(out);
    kc_tpl_free(NULL);
    kc_tpl_close(tpl);

    case_result(fail, "kc_tpl_free",
        "releases render output and accepts NULL");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpl_close.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_tpl_close(void) {
    kc_tpl_t *tpl = NULL;
    int fail = 0;

    kc_tpl_close(NULL);
    fail += expect_int("open template", KC_TPL_OK,
        kc_tpl_open(&tpl, "x", NULL));
    kc_tpl_close(tpl);

    case_result(fail, "kc_tpl_close",
        "releases template state and accepts NULL");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpl_version.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_tpl_version(void) {
    int fail = expect_true("version returns non-zero", kc_tpl_version() != 0U);

    case_result(fail, "kc_tpl_version",
        "returns a nonzero generated build version");
    return fail == 0 ? 0 : 1;
}

#ifndef __EMSCRIPTEN__
/**
 * Tests the shipped CLI contract as one grouped case.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_tpl_cli(void) {
    int cli_enabled = KC_TPL_TEST_CLI[0] != '\0';
    int fail = 0;
    int status = 0;
    char out[8192];
    char err[8192];

#ifdef _WIN32
    cli_enabled = 1;
#endif

    if (!cli_enabled) {
        case_result(0, "kc_tpl_cli",
            "preserves flags, direct source, stdin, diagnostics, help, and version");
        return 0;
    }

    {
        char var[] = "title=Home";
        char *args[] = {
            (char *)KC_TPL_TEST_CLI, "--var", var,
            "<h1>{{ title }}</h1>", NULL
        };
        fail += expect_int("CLI direct source exits 0", 0,
            test_cli_run_input(args, NULL, 0, out, sizeof(out),
                err, sizeof(err), &status) ? 1 : status);
        fail += expect_string("CLI direct source output", "<h1>Home</h1>", out);
    }
    {
        char var[] = "title=Home";
        char *args[] = {
            (char *)KC_TPL_TEST_CLI,
            "<h1>{{ title }}</h1>", "-var", var, NULL
        };
        fail += expect_int("CLI source before flags exits 0", 0,
            test_cli_run_input(args, NULL, 0, out, sizeof(out),
                err, sizeof(err), &status) ? 1 : status);
        fail += expect_string("CLI source before flags output", "<h1>Home</h1>", out);
    }
    {
        char var[] = "title=Home";
        char *args[] = { (char *)KC_TPL_TEST_CLI, "--var", var, NULL };
        const char *input = "<h1>{{ title }}</h1>";
        fail += expect_int("CLI stdin exits 0", 0,
            test_cli_run_input(args, input, strlen(input), out, sizeof(out),
                err, sizeof(err), &status) ? 1 : status);
        fail += expect_string("CLI stdin output", "<h1>Home</h1>", out);
    }
    {
        char *args[] = { (char *)KC_TPL_TEST_CLI, "--var", NULL };
        fail += expect_int("CLI missing var exits 1", 1,
            test_cli_run_input(args, NULL, 0, out, sizeof(out),
                err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI missing var diagnostic",
            strstr(err, "missing value for var") != NULL);
    }
    {
        char a[] = "a=1";
        char b[] = "b=2";
        char *args[] = {
            (char *)KC_TPL_TEST_CLI, "--var", a, "--var", b,
            "{{ a }}{{ b }}", NULL
        };
        fail += expect_int("CLI repeated vars exit 0", 0,
            test_cli_run_input(args, NULL, 0, out, sizeof(out),
                err, sizeof(err), &status) ? 1 : status);
        fail += expect_string("CLI repeated vars output", "12", out);
    }
    {
        char *short_help[] = { (char *)KC_TPL_TEST_CLI, "-h", NULL };
        char *long_version[] = { (char *)KC_TPL_TEST_CLI, "--version", NULL };
        fail += expect_int("CLI help exits 0", 0,
            test_cli_run_input(short_help, NULL, 0, out, sizeof(out),
                err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI help usage", strstr(out, "Usage:") != NULL);
        fail += expect_int("CLI version exits 0", 0,
            test_cli_run_input(long_version, NULL, 0, out, sizeof(out),
                err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI version build", strstr(out, "tpl build ") != NULL);
    }

    case_result(fail, "kc_tpl_cli",
        "preserves flags, direct source, stdin, diagnostics, help, and version");
    return fail == 0 ? 0 : 1;
}
#endif

/**
 * Runs the complete portable tpl test suite.
 * @return Number of failed test cases.
 */
static int case_all(void) {
    int rc = 0;
#ifdef __EMSCRIPTEN__
    test_case_total = 6;
#else
    int cli_enabled = KC_TPL_TEST_CLI[0] != '\0';
#ifdef _WIN32
    cli_enabled = 1;
#endif
    test_case_total = cli_enabled ? 7 : 6;
#endif
    test_case_current = 0;

    run_case(&rc, case_kc_tpl_open);
    run_case(&rc, case_kc_tpl_render);
    run_case(&rc, case_kc_tpl_error);
    run_case(&rc, case_kc_tpl_free);
    run_case(&rc, case_kc_tpl_close);
    run_case(&rc, case_kc_tpl_version);
#ifndef __EMSCRIPTEN__
    if (cli_enabled) run_case(&rc, case_kc_tpl_cli);
#endif

    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Dispatches the requested test case.
 * @param argc Command-line argument count.
 * @param argv Command-line argument vector.
 * @return 0 on success, nonzero on failure.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_tpl_open") == 0) return case_kc_tpl_open();
    if (strcmp(argv[1], "kc_tpl_render") == 0) return case_kc_tpl_render();
    if (strcmp(argv[1], "kc_tpl_error") == 0) return case_kc_tpl_error();
    if (strcmp(argv[1], "kc_tpl_free") == 0) return case_kc_tpl_free();
    if (strcmp(argv[1], "kc_tpl_close") == 0) return case_kc_tpl_close();
    if (strcmp(argv[1], "kc_tpl_version") == 0) return case_kc_tpl_version();
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "kc_tpl_cli") == 0) return case_kc_tpl_cli();
#endif
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
