/**
 * test.c - liblibr public API and libr CLI tests.
 * Summary: Tests the surviving public liblibr function and the libr CLI
 * contract through one CTest case each.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "liblibr.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef KC_LIBR_TEST_CLI
#define KC_LIBR_TEST_CLI ""
#endif

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Contract under test.
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
 * Verifies stdout carries exactly a run of EOT delimiter bytes.
 * @param name Check description.
 * @param out Captured child stdout.
 * @param count Expected number of delimiter bytes.
 * @return 0 on success, 1 on failure.
 */
static int expect_eot_payload(const char *name, const char *out, size_t count) {
    size_t len = strlen(out);
    size_t i;

    if (len != count) {
        printf("[FAIL] %s: expected %zu EOT bytes, got %zu\n",
            name, count, len);
        return 1;
    }
    for (i = 0; i < count; i++) {
        if ((unsigned char)out[i] != 4) {
            printf("[FAIL] %s: byte %zu is not EOT\n", name, i);
            return 1;
        }
    }
    return 0;
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
 * Runs the libr CLI through CreateProcessW and captures its output.
 * @param argv CLI argument vector, NULL-terminated.
 * @param input Byte string piped to the child stdin.
 * @param input_len Length of the piped byte string.
 * @param out Output buffer.
 * @param out_size Output buffer size.
 * @param err Error buffer.
 * @param err_size Error buffer size.
 * @param out_status Receives the CLI exit status.
 * @return 0 on success, 1 on failure.
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

    if (test_cli_to_wide(KC_LIBR_TEST_CLI, exe, sizeof(exe) / sizeof(wchar_t))) {
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
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_pipe[0];
    si.hStdOutput = out_pipe[1];
    si.hStdError = err_pipe[1];

    cmd[0] = L'"';
    cmd[1] = L'\0';
    if (test_cli_append_arg(cmd, sizeof(cmd) / sizeof(wchar_t), exe)) {
        CloseHandle(in_pipe[0]);
        CloseHandle(in_pipe[1]);
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
            CloseHandle(in_pipe[0]);
            CloseHandle(in_pipe[1]);
            CloseHandle(out_pipe[0]);
            CloseHandle(out_pipe[1]);
            CloseHandle(err_pipe[0]);
            CloseHandle(err_pipe[1]);
            return 1;
        }
        if (test_cli_append_arg(cmd, sizeof(cmd) / sizeof(wchar_t), wide)) {
            CloseHandle(in_pipe[0]);
            CloseHandle(in_pipe[1]);
            CloseHandle(out_pipe[0]);
            CloseHandle(out_pipe[1]);
            CloseHandle(err_pipe[0]);
            CloseHandle(err_pipe[1]);
            return 1;
        }
    }

    if (!CreateProcessW(exe, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(in_pipe[0]);
        CloseHandle(in_pipe[1]);
        CloseHandle(out_pipe[0]);
        CloseHandle(out_pipe[1]);
        CloseHandle(err_pipe[0]);
        CloseHandle(err_pipe[1]);
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
 * Runs the libr CLI through a forked child and captures its output.
 * @param argv CLI argument vector, NULL-terminated.
 * @param input Byte string piped to the child stdin.
 * @param input_len Length of the piped byte string.
 * @param out Output buffer.
 * @param out_size Output buffer size.
 * @param err Error buffer.
 * @param err_size Error buffer size.
 * @param out_status Receives the CLI exit status.
 * @return 0 on success, 1 on failure.
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
        close(in_pipe[0]);
        close(in_pipe[1]);
        return 1;
    }
    if (pipe(err_pipe) != 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        return 1;
    }
    pid = fork();
    if (pid < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        return 1;
    }
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

    if (waitpid(pid, &status, 0) < 0) {
        return 1;
    }
    *out_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    return 0;
}
#endif

/**
 * Tests kc_libr_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_libr_version(void) {
    const char *name = "kc_libr_version";
    const char *detail = "returns a nonzero generated build version";
    int fail = expect_true("version is a nonzero generated build value", kc_libr_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests the libr CLI flags, verbs, params, diagnostics, and stdin framing.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_libr_cli(void) {
    const char *name = "kc_libr_cli";
    const char *detail = "help, version, set/get, params, diagnostics, and stdin framing";
    int cli_enabled = KC_LIBR_TEST_CLI[0] != '\0';
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
        char *help[] = { (char *)KC_LIBR_TEST_CLI, "-h", (char *)NULL };
        char *long_help[] = { (char *)KC_LIBR_TEST_CLI, "--help", (char *)NULL };
        char *version[] = { (char *)KC_LIBR_TEST_CLI, "-v", (char *)NULL };
        char *long_version[] = { (char *)KC_LIBR_TEST_CLI, "--version", (char *)NULL };
        char *set_pos[] = { (char *)KC_LIBR_TEST_CLI, "set", "example input", (char *)NULL };
        char *get_pos[] = { (char *)KC_LIBR_TEST_CLI, "get", "example input", (char *)NULL };
        char *param[] = { (char *)KC_LIBR_TEST_CLI, "set", "example input", "-p", "value", (char *)NULL };
        char *long_param[] = { (char *)KC_LIBR_TEST_CLI, "get", "example input", "--param", "value", (char *)NULL };
        char *no_param_value[] = { (char *)KC_LIBR_TEST_CLI, "set", "-p", (char *)NULL };
        char *no_long_param_value[] = { (char *)KC_LIBR_TEST_CLI, "set", "--param", (char *)NULL };
        char *unknown[] = { (char *)KC_LIBR_TEST_CLI, "set", "--nope", (char *)NULL };

        fail += expect_int("CLI -h exits 0", 0,
            test_cli_run_input(help, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI -h prints usage", strstr(out, "Usage:") != NULL);
        fail += expect_int("CLI --help exits 0", 0,
            test_cli_run_input(long_help, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI --help prints usage", strstr(out, "Usage:") != NULL);
        fail += expect_int("CLI -v exits 0", 0,
            test_cli_run_input(version, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI -v prints build", strstr(out, "libr build") != NULL);
        fail += expect_int("CLI --version exits 0", 0,
            test_cli_run_input(long_version, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI --version prints build", strstr(out, "libr build") != NULL);

        fail += expect_int("CLI set with positional input exits 0", 0,
            test_cli_run_input(set_pos, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_eot_payload("CLI set emits one EOT byte", out, 1);
        fail += expect_int("CLI get with positional input exits 0", 0,
            test_cli_run_input(get_pos, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_eot_payload("CLI get emits one EOT byte", out, 1);

        fail += expect_int("CLI -p with value exits 0", 0,
            test_cli_run_input(param, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_eot_payload("CLI -p keeps one EOT byte", out, 1);
        fail += expect_int("CLI --param with value exits 0", 0,
            test_cli_run_input(long_param, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_eot_payload("CLI --param keeps one EOT byte", out, 1);

        fail += expect_int("CLI -p without value exits 1", 1,
            test_cli_run_input(no_param_value, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI missing -p diagnostic",
            strstr(err, "missing value for -p") != NULL);
        fail += expect_int("CLI --param without value exits 1", 1,
            test_cli_run_input(no_long_param_value, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI missing --param diagnostic",
            strstr(err, "missing value for --param") != NULL);
        fail += expect_int("CLI unknown option exits 1", 1,
            test_cli_run_input(unknown, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI unknown option diagnostic",
            strstr(err, "unknown option") != NULL);
    }

    {
        char out[4096];
        char err[4096];
        int status = 0;
        static const char one_request[] = "hello\004";
        static const char multiple_requests[] = "first\004second\004";
        static const char eof_request[] = "tail";
        static const char empty_requests[] = "\004\004data\004";
        char *cli[] = { (char *)KC_LIBR_TEST_CLI, "set", (char *)NULL };

        fail += expect_int("CLI one stdin request exits 0", 0,
            test_cli_run_input(cli, one_request, sizeof(one_request) - 1,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_eot_payload("CLI one stdin request emits one EOT byte", out, 1);

        fail += expect_int("CLI multiple stdin requests exit 0", 0,
            test_cli_run_input(cli, multiple_requests, sizeof(multiple_requests) - 1,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_eot_payload("CLI two stdin requests emit two EOT bytes", out, 2);

        fail += expect_int("CLI EOF-terminated stdin request exits 0", 0,
            test_cli_run_input(cli, eof_request, sizeof(eof_request) - 1,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_eot_payload("CLI EOF-terminated request emits one EOT byte", out, 1);

        fail += expect_int("CLI empty delimited requests exit 0", 0,
            test_cli_run_input(cli, empty_requests, sizeof(empty_requests) - 1,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_eot_payload("CLI empty requests emit one EOT byte", out, 1);

        fail += expect_int("CLI empty stdin exits 0", 0,
            test_cli_run_input(cli, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI empty stdin produces no payload", out[0] == '\0');
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

    cli_enabled = KC_LIBR_TEST_CLI[0] != '\0';
#ifdef _WIN32
    cli_enabled = 1;
#endif
    test_case_total = cli_enabled ? 2 : 1;
    test_case_current = 0;
    run_case(&rc, case_kc_libr_version);
    if (cli_enabled) {
        run_case(&rc, case_kc_libr_cli);
    }
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one liblibr or libr CLI test case.
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
    if (strcmp(argv[1], "kc_libr_version") == 0) return case_kc_libr_version();
    if (strcmp(argv[1], "kc_libr_cli") == 0) return case_kc_libr_cli();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
