/**
 * test.c - libmin public API and CLI tests.
 * Summary: Contract tests for the stateless min API and shipped CLI.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libmin.h"

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

#ifndef MIN_TEST_CLI
#define MIN_TEST_CLI ""
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

#ifndef __EMSCRIPTEN__
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

    if (test_cli_to_wide(MIN_TEST_CLI, exe, sizeof(exe) / sizeof(wchar_t))) return 1;
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
 * Tests CSS minification.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_min_css(void) {
    char *out;
    int fail = 0;

    fail += expect_true("css NULL returns NULL", kc_min_css(NULL) == NULL);

    out = kc_min_css("");
    fail += expect_true("css empty returns allocation", out != NULL);
    if (out) fail += expect_string("css empty returns empty string", "", out);
    kc_min_free(out);

    out = kc_min_css("/* hi */ body{}");
    fail += expect_string("CSS comments removed", "body{}", out);
    kc_min_free(out);

    out = kc_min_css("a { margin: 0px 0% 0pt; }");
    fail += expect_string("CSS zero units removed", "a{margin:0 0 0}", out);
    kc_min_free(out);

    out = kc_min_css("a { width: calc(100% - 10px); }");
    fail += expect_string("CSS calc spacing preserved",
        "a{width:calc(100% - 10px)}", out);
    kc_min_free(out);

    case_result(fail, "kc_min_css",
        "minifies CSS with the conservative scanner contract");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests JavaScript minification.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_min_js(void) {
    char *out;
    int fail = 0;

    fail += expect_true("js NULL returns NULL", kc_min_js(NULL) == NULL);

    out = kc_min_js("");
    fail += expect_true("js empty returns allocation", out != NULL);
    if (out) fail += expect_string("js empty returns empty string", "", out);
    kc_min_free(out);

    out = kc_min_js("const x = 1; // comment");
    fail += expect_string("JS comment removed", "const x = 1;", out);
    kc_min_free(out);

    out = kc_min_js("const t = `a  b`;");
    fail += expect_string("JS template preserved", "const t = `a  b`;", out);
    kc_min_free(out);

    out = kc_min_js("var re = /a[bc]d/g;");
    fail += expect_string("JS regex preserved", "var re = /a[bc]d/g;", out);
    kc_min_free(out);

    out = kc_min_js("var d = a / b;");
    fail += expect_string("JS division preserved", "var d = a / b;", out);
    kc_min_free(out);

    case_result(fail, "kc_min_js",
        "minifies JavaScript while preserving templates, regex, and division");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests HTML minification.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_min_html(void) {
    char *out;
    int fail = 0;

    fail += expect_true("html NULL returns NULL", kc_min_html(NULL) == NULL);

    out = kc_min_html("");
    fail += expect_true("html empty returns allocation", out != NULL);
    if (out) fail += expect_string("html empty returns empty string", "", out);
    kc_min_free(out);

    out = kc_min_html("<!-- c --><p>hi</p>");
    fail += expect_string("HTML comment removed", "<p>hi</p>", out);
    kc_min_free(out);

    out = kc_min_html("<pre>  a  </pre>");
    fail += expect_string("HTML pre preserved", "<pre>  a  </pre>", out);
    kc_min_free(out);

    out = kc_min_html("<textarea>  x  </textarea>");
    fail += expect_string("HTML textarea preserved",
        "<textarea>  x  </textarea>", out);
    kc_min_free(out);

    case_result(fail, "kc_min_html",
        "minifies HTML while preserving verbatim regions");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests generic text minification.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_min_text(void) {
    char *out;
    int fail = 0;

    fail += expect_true("text NULL returns NULL", kc_min_text(NULL) == NULL);

    out = kc_min_text("");
    fail += expect_true("text empty returns allocation", out != NULL);
    if (out) fail += expect_string("text empty returns empty string", "", out);
    kc_min_free(out);

    out = kc_min_text("  hello   world\n\nfoo\tbar  ");
    fail += expect_string("text collapses whitespace",
        "hello world foo bar", out);
    kc_min_free(out);

    out = kc_min_text("a  <!-- x -->  b");
    fail += expect_string("text does not interpret syntax",
        "a <!-- x --> b", out);
    kc_min_free(out);

    case_result(fail, "kc_min_text",
        "collapses generic whitespace without interpreting syntax");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests ownership release through the public API.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_min_free(void) {
    char *out = kc_min_css("body { color: red; }");
    int fail = expect_true("minifier returns allocation", out != NULL);

    kc_min_free(out);
    kc_min_free(NULL);

    case_result(fail, "kc_min_free",
        "releases owned output and accepts NULL");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests the public build-version query.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_min_version(void) {
    int fail = expect_true("version returns non-zero", kc_min_version() != 0U);

    case_result(fail, "kc_min_version",
        "returns a nonzero generated build version");
    return fail == 0 ? 0 : 1;
}

#ifndef __EMSCRIPTEN__
/**
 * Tests the shipped CLI contract as one grouped case.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_min_cli(void) {
    int cli_enabled = MIN_TEST_CLI[0] != '\0';
    int fail = 0, status = 0;
    char out[8192], err[8192];

#ifdef _WIN32
    cli_enabled = 1;
#endif
    if (!cli_enabled) {
        case_result(0, "kc_min_cli", "preserves modes, stdin/stdout, diagnostics, help, and version");
        return 0;
    }

    {
        char *args[] = { (char *)MIN_TEST_CLI, "css", NULL };
        const char *input = "body { color: red; }";
        fail += expect_int("CLI css exits 0", 0,
            test_cli_run_input(args, input, strlen(input), out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_string("CLI css output", "body{color:red}", out);
        fail += expect_string("CLI css stderr", "", err);
    }
    {
        char *args[] = { (char *)MIN_TEST_CLI, "js", NULL };
        const char *input = "const x = 1; // comment";
        fail += expect_int("CLI js exits 0", 0,
            test_cli_run_input(args, input, strlen(input), out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_string("CLI js output", "const x = 1;", out);
    }
    {
        char *args[] = { (char *)MIN_TEST_CLI, "html", NULL };
        const char *input = "<!-- c --><p>hi</p>";
        fail += expect_int("CLI html exits 0", 0,
            test_cli_run_input(args, input, strlen(input), out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_string("CLI html output", "<p>hi</p>", out);
    }
    {
        char *args[] = { (char *)MIN_TEST_CLI, "text", NULL };
        const char *input = "  hello   world\nfoo\tbar  ";
        fail += expect_int("CLI text exits 0", 0,
            test_cli_run_input(args, input, strlen(input), out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_string("CLI text output", "hello world foo bar", out);
    }
    {
        char *args[] = { (char *)MIN_TEST_CLI, "css", NULL };
        fail += expect_int("CLI empty stdin exits 0", 0,
            test_cli_run_input(args, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_string("CLI empty stdin stdout", "", out);
    }
    {
        char *short_help[] = { (char *)MIN_TEST_CLI, "-h", NULL };
        char *long_help[] = { (char *)MIN_TEST_CLI, "--help", NULL };
        char *short_version[] = { (char *)MIN_TEST_CLI, "-v", NULL };
        char *long_version[] = { (char *)MIN_TEST_CLI, "--version", NULL };
        fail += expect_int("CLI -h exits 0", 0, test_cli_run_input(short_help, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI -h usage", strstr(out, "Usage:") != NULL);
        fail += expect_int("CLI --help exits 0", 0, test_cli_run_input(long_help, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI --help usage", strstr(out, "Usage:") != NULL);
        fail += expect_int("CLI -v exits 0", 0, test_cli_run_input(short_version, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI -v build", strstr(out, "min build ") != NULL);
        fail += expect_int("CLI --version exits 0", 0, test_cli_run_input(long_version, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI --version build", strstr(out, "min build ") != NULL);
    }
    {
        char *args[] = { (char *)MIN_TEST_CLI, NULL };
        fail += expect_int("CLI missing mode exits 1", 1, test_cli_run_input(args, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI missing mode diagnostic", strstr(err, "min: mode is required") != NULL);
    }
    {
        char *args[] = { (char *)MIN_TEST_CLI, "bogus", NULL };
        fail += expect_int("CLI invalid mode exits 1", 1, test_cli_run_input(args, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI invalid mode diagnostic", strstr(err, "min: invalid mode 'bogus'") != NULL);
    }
    {
        char *args[] = { (char *)MIN_TEST_CLI, "--nope", NULL };
        fail += expect_int("CLI invalid option exits 1", 1, test_cli_run_input(args, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI invalid option diagnostic", strstr(err, "min: unknown option '--nope'") != NULL);
    }
    {
        char *args[] = { (char *)MIN_TEST_CLI, "css", "extra", NULL };
        fail += expect_int("CLI extra argument exits 1", 1, test_cli_run_input(args, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI extra argument diagnostic", strstr(err, "min: unexpected argument 'extra'") != NULL);
    }

    case_result(fail, "kc_min_cli", "preserves modes, stdin/stdout, diagnostics, help, and version");
    return fail == 0 ? 0 : 1;
}
#endif

/**
 * Runs the complete portable min test suite.
 * @return Number of failed test cases.
 */
static int case_all(void) {
    int rc = 0;
#ifdef __EMSCRIPTEN__
    test_case_total = 6;
#else
    int cli_enabled = MIN_TEST_CLI[0] != '\0';
#ifdef _WIN32
    cli_enabled = 1;
#endif
    test_case_total = cli_enabled ? 7 : 6;
#endif
    test_case_current = 0;
    run_case(&rc, case_kc_min_css);
    run_case(&rc, case_kc_min_js);
    run_case(&rc, case_kc_min_html);
    run_case(&rc, case_kc_min_text);
    run_case(&rc, case_kc_min_free);
    run_case(&rc, case_kc_min_version);
#ifndef __EMSCRIPTEN__
    if (cli_enabled) run_case(&rc, case_kc_min_cli);
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
    if (strcmp(argv[1], "kc_min_css") == 0) return case_kc_min_css();
    if (strcmp(argv[1], "kc_min_js") == 0) return case_kc_min_js();
    if (strcmp(argv[1], "kc_min_html") == 0) return case_kc_min_html();
    if (strcmp(argv[1], "kc_min_text") == 0) return case_kc_min_text();
    if (strcmp(argv[1], "kc_min_free") == 0) return case_kc_min_free();
    if (strcmp(argv[1], "kc_min_version") == 0) return case_kc_min_version();
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "kc_min_cli") == 0) return case_kc_min_cli();
#endif
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
