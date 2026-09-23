/**
 * test.c - libmdp public API tests.
 * Summary: Contract tests for the stateless mdp API and CLI.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libmdp.h"

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

#ifndef MDP_TEST_CLI
#define MDP_TEST_CLI ""
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
#endif

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
    HANDLE in_pipe[2];
    HANDLE out_pipe[2];
    HANDLE err_pipe[2];
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD exit_code;
    DWORD written;
    size_t n;
    int i;

    if (test_cli_to_wide(MDP_TEST_CLI, exe, sizeof(exe) / sizeof(wchar_t))) {
        return 1;
    }

    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;
    if (!CreatePipe(&in_pipe[0], &in_pipe[1], &sa, 0)) return 1;
    if (!CreatePipe(&out_pipe[0], &out_pipe[1], &sa, 0)) {
        CloseHandle(in_pipe[0]);
        CloseHandle(in_pipe[1]);
        return 1;
    }
    if (!CreatePipe(&err_pipe[0], &err_pipe[1], &sa, 0)) {
        CloseHandle(in_pipe[0]);
        CloseHandle(in_pipe[1]);
        CloseHandle(out_pipe[0]);
        CloseHandle(out_pipe[1]);
        return 1;
    }

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

    cmd[0] = L'"';
    cmd[1] = L'\0';
    if (test_cli_append_arg(cmd, sizeof(cmd) / sizeof(wchar_t), exe)) {
        CloseHandle(in_pipe[0]); CloseHandle(in_pipe[1]);
        CloseHandle(out_pipe[0]); CloseHandle(out_pipe[1]);
        CloseHandle(err_pipe[0]); CloseHandle(err_pipe[1]);
        return 1;
    }
    n = wcslen(cmd);
    cmd[n] = L'"';
    cmd[n + 1] = L'\0';

    for (i = 1; argv[i]; i++) {
        if (test_cli_to_wide(argv[i], wide, sizeof(wide) / sizeof(wchar_t)) ||
                test_cli_append_arg(cmd, sizeof(cmd) / sizeof(wchar_t), wide)) {
            CloseHandle(in_pipe[0]); CloseHandle(in_pipe[1]);
            CloseHandle(out_pipe[0]); CloseHandle(out_pipe[1]);
            CloseHandle(err_pipe[0]); CloseHandle(err_pipe[1]);
            return 1;
        }
    }

    if (!CreateProcessW(exe, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(in_pipe[0]); CloseHandle(in_pipe[1]);
        CloseHandle(out_pipe[0]); CloseHandle(out_pipe[1]);
        CloseHandle(err_pipe[0]); CloseHandle(err_pipe[1]);
        return 1;
    }

    CloseHandle(in_pipe[0]);
    CloseHandle(out_pipe[1]);
    CloseHandle(err_pipe[1]);
    written = 0;
    if (input_len > 0) {
        (void)WriteFile(in_pipe[1], input, (DWORD)input_len, &written, NULL);
    }
    CloseHandle(in_pipe[1]);
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
    int in_pipe[2];
    int out_pipe[2];
    int err_pipe[2];
    pid_t pid;
    ssize_t count;
    size_t written = 0;
    size_t pos = 0;
    int status;

    if (pipe(in_pipe) != 0) return 1;
    if (pipe(out_pipe) != 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        return 1;
    }
    if (pipe(err_pipe) != 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        return 1;
    }

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

    close(in_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);

    while (written < input_len) {
        count = write(in_pipe[1], input + written, input_len - written);
        if (count < 0) break;
        written += (size_t)count;
    }
    close(in_pipe[1]);

    memset(out, 0, out_size);
    while (pos + 1 < out_size &&
            (count = read(out_pipe[0], out + pos, out_size - pos - 1)) > 0) {
        pos += (size_t)count;
    }
    close(out_pipe[0]);

    pos = 0;
    memset(err, 0, err_size);
    while (pos + 1 < err_size &&
            (count = read(err_pipe[0], err + pos, err_size - pos - 1)) > 0) {
        pos += (size_t)count;
    }
    close(err_pipe[0]);

    if (waitpid(pid, &status, 0) < 0) return 1;
    *out_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    return 0;
}
#endif
#endif

/**
 * Tests document creation and one-time split ownership.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_mdp_open(void) {
    char input[] = "---\ntitle: Home\n---\n# Hello";
    kc_mdp_t *mdp = NULL;
    int fail = 0;

    fail += expect_int("open rejects NULL out", KC_MDP_ERROR,
        kc_mdp_open(NULL, input));
    fail += expect_int("open rejects NULL input", KC_MDP_ERROR,
        kc_mdp_open(&mdp, NULL));
    fail += expect_true("failed open clears output", mdp == NULL);

    fail += expect_int("open accepts document", KC_MDP_OK,
        kc_mdp_open(&mdp, input));
    fail += expect_true("open returns document", mdp != NULL);

    if (mdp != NULL) {
        input[4] = 'X';
        fail += expect_string("open owns split metadata",
            "title: Home", kc_mdp_meta(mdp));
        fail += expect_string("open owns split body",
            "# Hello", kc_mdp_body(mdp));
    }

    kc_mdp_close(mdp);
    case_result(fail, "kc_mdp_open",
        "creates one persistent document with an owned split");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests cached Markdown-to-HTML rendering.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_mdp_html(void) {
    const char *first;
    const char *second;
    kc_mdp_t *mdp = NULL;
    int fail = 0;

    fail += expect_true("html rejects NULL document",
        kc_mdp_html(NULL) == NULL);

    fail += expect_int("open composite document", KC_MDP_OK,
        kc_mdp_open(&mdp,
            "---\ntitle: Home\n---\n# Hello\n\n"
            "Text **bold** *italic* `code`.\n\n"
            "- one\n- two\n\n> quote\n\n"
            "```\n<a>\n```\n\n---\n\n"
            "<div>\n*raw*\n</div>\n"));

    first = mdp ? kc_mdp_html(mdp) : NULL;
    fail += expect_true("html composite returns non-NULL", first != NULL);
    if (first != NULL) {
        fail += expect_true("html renders heading",
            strstr(first, "<h1>Hello</h1>\n") != NULL);
        fail += expect_true("html renders inline markup",
            strstr(first,
                "<strong>bold</strong> <em>italic</em> <code>code</code>") != NULL);
        fail += expect_true("html renders list",
            strstr(first, "<ul>\n<li>one</li>\n<li>two</li>\n</ul>\n") != NULL);
        fail += expect_true("html renders blockquote",
            strstr(first,
                "<blockquote>\n<p>quote</p>\n</blockquote>\n") != NULL);
        fail += expect_true("html escapes fenced code",
            strstr(first, "<pre><code>&lt;a&gt;\n</code></pre>\n") != NULL);
        fail += expect_true("html renders horizontal rule",
            strstr(first, "<hr>\n") != NULL);
        fail += expect_true("html passes raw block",
            strstr(first, "<div>\n*raw*\n</div>\n") != NULL);
        fail += expect_true("html excludes frontmatter",
            strstr(first, "title: Home") == NULL);

        second = kc_mdp_html(mdp);
        fail += expect_true("html reuses cached result", second == first);
    }

    kc_mdp_close(mdp);

    mdp = NULL;
    fail += expect_int("open empty document", KC_MDP_OK,
        kc_mdp_open(&mdp, ""));
    first = mdp ? kc_mdp_html(mdp) : NULL;
    fail += expect_true("html empty returns non-NULL", first != NULL);
    if (first != NULL) {
        fail += expect_string("html empty returns empty string", "", first);
    }
    kc_mdp_close(mdp);

    case_result(fail, "kc_mdp_html",
        "renders once and caches the supported Markdown contract");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests body access through the persistent document.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_mdp_body(void) {
    kc_mdp_t *mdp = NULL;
    const char *first;
    const char *second;
    int fail = 0;

    fail += expect_true("body rejects NULL document",
        kc_mdp_body(NULL) == NULL);

    fail += expect_int("open LF frontmatter", KC_MDP_OK,
        kc_mdp_open(&mdp, "---\ntitle: Home\n---\n# Hello"));
    first = mdp ? kc_mdp_body(mdp) : NULL;
    second = mdp ? kc_mdp_body(mdp) : NULL;
    fail += expect_string("body LF strips frontmatter", "# Hello", first);
    fail += expect_true("body returns stable view", second == first);
    kc_mdp_close(mdp);

    mdp = NULL;
    fail += expect_int("open CRLF frontmatter", KC_MDP_OK,
        kc_mdp_open(&mdp, "---\r\ntitle: Home\r\n---\r\n# Hello"));
    fail += expect_string("body CRLF strips frontmatter",
        "# Hello", mdp ? kc_mdp_body(mdp) : NULL);
    kc_mdp_close(mdp);

    mdp = NULL;
    fail += expect_int("open unclosed frontmatter", KC_MDP_OK,
        kc_mdp_open(&mdp, "---\ntitle: Home\n# Hello"));
    fail += expect_string("body unclosed frontmatter preserves input",
        "---\ntitle: Home\n# Hello", mdp ? kc_mdp_body(mdp) : NULL);
    kc_mdp_close(mdp);

    mdp = NULL;
    fail += expect_int("open empty body", KC_MDP_OK,
        kc_mdp_open(&mdp, ""));
    fail += expect_string("body empty returns empty string",
        "", mdp ? kc_mdp_body(mdp) : NULL);
    kc_mdp_close(mdp);

    case_result(fail, "kc_mdp_body",
        "returns the persistent body view after one split");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests frontmatter access through the persistent document.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_mdp_meta(void) {
    kc_mdp_t *mdp = NULL;
    const char *first;
    const char *second;
    int fail = 0;

    fail += expect_true("meta rejects NULL document",
        kc_mdp_meta(NULL) == NULL);

    fail += expect_int("open LF metadata", KC_MDP_OK,
        kc_mdp_open(&mdp, "---\ntitle: Home\n---\n# Hello"));
    first = mdp ? kc_mdp_meta(mdp) : NULL;
    second = mdp ? kc_mdp_meta(mdp) : NULL;
    fail += expect_string("meta LF returns raw frontmatter",
        "title: Home", first);
    fail += expect_true("meta returns stable view", second == first);
    kc_mdp_close(mdp);

    mdp = NULL;
    fail += expect_int("open CRLF metadata", KC_MDP_OK,
        kc_mdp_open(&mdp, "---\r\ntitle: Home\r\n---\r\n# Hello"));
    fail += expect_string("meta CRLF returns raw frontmatter",
        "title: Home", mdp ? kc_mdp_meta(mdp) : NULL);
    kc_mdp_close(mdp);

    mdp = NULL;
    fail += expect_int("open document without metadata", KC_MDP_OK,
        kc_mdp_open(&mdp, "# Hello"));
    fail += expect_string("meta absent returns empty string",
        "", mdp ? kc_mdp_meta(mdp) : NULL);
    kc_mdp_close(mdp);

    mdp = NULL;
    fail += expect_int("open unclosed metadata", KC_MDP_OK,
        kc_mdp_open(&mdp, "---\ntitle: Home\n# Hello"));
    fail += expect_string("meta unclosed returns empty string",
        "", mdp ? kc_mdp_meta(mdp) : NULL);
    kc_mdp_close(mdp);

    case_result(fail, "kc_mdp_meta",
        "returns the persistent recognized frontmatter view");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests document release through the public API.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_mdp_close(void) {
    kc_mdp_t *mdp = NULL;
    int fail = 0;

    fail += expect_int("open document for close", KC_MDP_OK,
        kc_mdp_open(&mdp, "# Hello"));
    fail += expect_true("document allocated for close", mdp != NULL);
    if (mdp != NULL) {
        fail += expect_true("html can be cached before close",
            kc_mdp_html(mdp) != NULL);
    }

    kc_mdp_close(mdp);
    kc_mdp_close(NULL);

    case_result(fail, "kc_mdp_close",
        "releases document state and accepts NULL");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests the public build-version query.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_mdp_version(void) {
    int fail = 0;

    fail += expect_true("version returns non-zero", kc_mdp_version() != 0U);
    case_result(fail, "kc_mdp_version",
        "returns a nonzero generated build version");
    return fail == 0 ? 0 : 1;
}

#ifndef __EMSCRIPTEN__
/**
 * Tests the shipped CLI contract as one grouped case.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_mdp_cli(void) {
    int cli_enabled = MDP_TEST_CLI[0] != '\0';
    int fail = 0;
    char out[8192];
    char err[8192];
    int status = 0;

#ifdef _WIN32
    cli_enabled = 1;
#endif

    if (!cli_enabled) {
        case_result(0, "kc_mdp_cli",
            "preserves flags, aliases, stdin/stdout, diagnostics, help, and version");
        return 0;
    }

    {
        char *args[] = { (char *)MDP_TEST_CLI, (char *)NULL };
        fail += expect_int("CLI default exits 0", 0,
            test_cli_run_input(args, "# Hello", 7, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
#ifdef _WIN32
        fail += expect_string("CLI default renders HTML", "<h1>Hello</h1>\r\n", out);
#else
        fail += expect_string("CLI default renders HTML", "<h1>Hello</h1>\n", out);
#endif
        fail += expect_string("CLI default stderr empty", "", err);
    }

    {
        char *args[] = { (char *)MDP_TEST_CLI, "--body", (char *)NULL };
        const char *input = "---\ntitle: Home\n---\n# Hello";
        fail += expect_int("CLI --body exits 0", 0,
            test_cli_run_input(args, input, strlen(input), out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_string("CLI --body output", "# Hello", out);
    }

    {
        char *args[] = { (char *)MDP_TEST_CLI, "meta", (char *)NULL };
        const char *input = "---\ntitle: Home\n---\n# Hello";
        fail += expect_int("CLI meta alias exits 0", 0,
            test_cli_run_input(args, input, strlen(input), out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_string("CLI meta alias output", "title: Home", out);
    }

    {
        char *args[] = { (char *)MDP_TEST_CLI, "--meta", "--body", (char *)NULL };
        const char *input = "---\ntitle: Home\n---\n# Hello";
        fail += expect_int("CLI last mode wins exits 0", 0,
            test_cli_run_input(args, input, strlen(input), out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_string("CLI last mode wins", "# Hello", out);
    }

    {
        char *short_help[] = { (char *)MDP_TEST_CLI, "-h", (char *)NULL };
        char *long_help[] = { (char *)MDP_TEST_CLI, "--help", (char *)NULL };
        char *short_version[] = { (char *)MDP_TEST_CLI, "-v", (char *)NULL };
        char *long_version[] = { (char *)MDP_TEST_CLI, "--version", (char *)NULL };

        fail += expect_int("CLI -h exits 0", 0,
            test_cli_run_input(short_help, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI -h prints usage", strstr(out, "Usage:") != NULL);

        fail += expect_int("CLI --help exits 0", 0,
            test_cli_run_input(long_help, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI --help prints usage", strstr(out, "Usage:") != NULL);

        fail += expect_int("CLI -v exits 0", 0,
            test_cli_run_input(short_version, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI -v prints build", strstr(out, "mdp build ") != NULL);

        fail += expect_int("CLI --version exits 0", 0,
            test_cli_run_input(long_version, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI --version prints build", strstr(out, "mdp build ") != NULL);
    }

    {
        char *args[] = { (char *)MDP_TEST_CLI, "--nope", (char *)NULL };
        fail += expect_int("CLI invalid option exits 1", 1,
            test_cli_run_input(args, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_string("CLI invalid option stdout empty", "", out);
        fail += expect_true("CLI invalid option diagnostic",
            strstr(err, "mdp: unknown option '--nope'") != NULL);
    }

    case_result(fail, "kc_mdp_cli",
        "preserves flags, aliases, stdin/stdout, diagnostics, help, and version");
    return fail == 0 ? 0 : 1;
}
#endif

/**
 * Runs the complete portable mdp test suite.
 * @return Number of failed test cases.
 */
static int case_all(void) {
    int rc = 0;

#ifdef __EMSCRIPTEN__
    test_case_total = 6;
#else
    int cli_enabled = MDP_TEST_CLI[0] != '\0';
#ifdef _WIN32
    cli_enabled = 1;
#endif
    test_case_total = cli_enabled ? 7 : 6;
#endif

    test_case_current = 0;
    run_case(&rc, case_kc_mdp_open);
    run_case(&rc, case_kc_mdp_html);
    run_case(&rc, case_kc_mdp_body);
    run_case(&rc, case_kc_mdp_meta);
    run_case(&rc, case_kc_mdp_close);
    run_case(&rc, case_kc_mdp_version);
#ifndef __EMSCRIPTEN__
    if (cli_enabled) {
        run_case(&rc, case_kc_mdp_cli);
    }
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
    if (strcmp(argv[1], "kc_mdp_open") == 0) return case_kc_mdp_open();
    if (strcmp(argv[1], "kc_mdp_html") == 0) return case_kc_mdp_html();
    if (strcmp(argv[1], "kc_mdp_body") == 0) return case_kc_mdp_body();
    if (strcmp(argv[1], "kc_mdp_meta") == 0) return case_kc_mdp_meta();
    if (strcmp(argv[1], "kc_mdp_close") == 0) return case_kc_mdp_close();
    if (strcmp(argv[1], "kc_mdp_version") == 0) return case_kc_mdp_version();
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "kc_mdp_cli") == 0) return case_kc_mdp_cli();
#endif

    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
