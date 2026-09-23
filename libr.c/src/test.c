/**
 * test.c - liblibr public API and CLI contract tests.
 * Summary: Validates the minimal reusable API and grouped CLI contract.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "liblibr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __EMSCRIPTEN__
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif
#endif

#ifndef KC_LIBR_TEST_CLI
#define KC_LIBR_TEST_CLI ""
#endif

static int test_case_total = 0;
static int test_case_current = 0;

static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

static void run_case(int *rc, int (*fn)(void)) {
    test_case_current++;
    *rc += fn();
}

static int expect_true(const char *name, int condition) {
    if (!condition) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

static int case_kc_libr_greet(void) {
    const char *name = "kc_libr_greet";
    const char *detail = "returns an owned greeting for the supplied name";
    char *greeting;
    int fail = 0;

    fail += expect_true("greet rejects NULL", kc_libr_greet(NULL) == NULL);

    greeting = kc_libr_greet("John");
    fail += expect_true("greet allocates output", greeting != NULL);
    if (greeting != NULL) {
        fail += expect_true("greet returns expected text",
            strcmp(greeting, "Hello John!") == 0);
    }
    kc_libr_free(greeting);
    kc_libr_free(NULL);

    greeting = kc_libr_greet("");
    fail += expect_true("greet accepts empty name", greeting != NULL);
    if (greeting != NULL) {
        fail += expect_true("empty name keeps greeting format",
            strcmp(greeting, "Hello !") == 0);
    }
    kc_libr_free(greeting);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_kc_libr_version(void) {
    const char *name = "kc_libr_version";
    const char *detail = "returns a nonzero generated build version";
    int fail = expect_true("version is nonzero", kc_libr_version() != 0U);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

#ifndef __EMSCRIPTEN__

#ifdef _WIN32
static int test_to_wide(const char *input, wchar_t *output, size_t capacity) {
    return MultiByteToWideChar(
        CP_UTF8, 0, input, -1, output, (int)capacity
    ) > 0 ? 0 : 1;
}

static int test_append_arg(
    wchar_t *command,
    size_t capacity,
    const wchar_t *arg
) {
    size_t used = wcslen(command);
    size_t len = wcslen(arg);

    if (used != 0) {
        if (used + 1 >= capacity) return 1;
        command[used++] = L' ';
    }
    if (used + len + 2 >= capacity) return 1;

    command[used++] = L'"';
    memcpy(command + used, arg, len * sizeof(wchar_t));
    used += len;
    command[used++] = L'"';
    command[used] = L'\0';
    return 0;
}

static void test_read_pipe(HANDLE pipe, char *buffer, size_t size) {
    DWORD count;
    size_t used = 0;

    while (used + 1 < size &&
            ReadFile(pipe, buffer + used, (DWORD)(size - used - 1),
                &count, NULL) && count > 0) {
        used += count;
    }
    buffer[used] = '\0';
}
#endif

static int test_cli_run(
    char *const argv[],
    char *out,
    size_t out_size,
    char *err,
    size_t err_size,
    int *out_status
) {
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    HANDLE out_pipe[2];
    HANDLE err_pipe[2];
    wchar_t exe[MAX_PATH];
    wchar_t command[32768];
    wchar_t wide[4096];
    DWORD exit_code;
    int i;

    if (test_to_wide(KC_LIBR_TEST_CLI, exe, MAX_PATH)) return 1;

    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&out_pipe[0], &out_pipe[1], &sa, 0)) return 1;
    if (!CreatePipe(&err_pipe[0], &err_pipe[1], &sa, 0)) return 1;
    SetHandleInformation(out_pipe[0], HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_pipe[0], HANDLE_FLAG_INHERIT, 0);

    command[0] = L'\0';
    if (test_append_arg(command, 32768, exe)) return 1;
    for (i = 1; argv[i] != NULL; i++) {
        if (test_to_wide(argv[i], wide, 4096)) return 1;
        if (test_append_arg(command, 32768, wide)) return 1;
    }

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = out_pipe[1];
    si.hStdError = err_pipe[1];
    memset(&pi, 0, sizeof(pi));

    if (!CreateProcessW(exe, command, NULL, NULL, TRUE, 0,
            NULL, NULL, &si, &pi)) {
        return 1;
    }

    CloseHandle(out_pipe[1]);
    CloseHandle(err_pipe[1]);
    test_read_pipe(out_pipe[0], out, out_size);
    test_read_pipe(err_pipe[0], err, err_size);
    CloseHandle(out_pipe[0]);
    CloseHandle(err_pipe[0]);

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    *out_status = (int)exit_code;
    return 0;
#else
    int out_pipe[2];
    int err_pipe[2];
    pid_t pid;
    ssize_t count;
    size_t pos;
    int status;

    if (pipe(out_pipe) != 0) return 1;
    if (pipe(err_pipe) != 0) return 1;

    pid = fork();
    if (pid < 0) return 1;
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

    pos = 0;
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
#endif
}

static int case_kc_libr_cli(void) {
    const char *name = "kc_libr_cli";
    const char *detail = "default/name greeting, help, version, and diagnostics";
    char out[4096];
    char err[4096];
    int status = 0;
    int fail = 0;
    char *plain[] = { (char *)KC_LIBR_TEST_CLI, NULL };
    char *short_name[] = {
        (char *)KC_LIBR_TEST_CLI, "-n", "John", NULL
    };
    char *long_name[] = {
        (char *)KC_LIBR_TEST_CLI, "--name", "Jane", NULL
    };
    char *help[] = { (char *)KC_LIBR_TEST_CLI, "-h", NULL };
    char *long_help[] = { (char *)KC_LIBR_TEST_CLI, "--help", NULL };
    char *version[] = { (char *)KC_LIBR_TEST_CLI, "-v", NULL };
    char *long_version[] = { (char *)KC_LIBR_TEST_CLI, "--version", NULL };
    char *missing_name[] = { (char *)KC_LIBR_TEST_CLI, "-n", NULL };
    char *unknown[] = { (char *)KC_LIBR_TEST_CLI, "--nope", NULL };

    if (KC_LIBR_TEST_CLI[0] == '\0') {
        case_result(0, name, detail);
        return 0;
    }

    fail += test_cli_run(plain, out, sizeof(out), err, sizeof(err), &status);
    fail += expect_true("CLI default exits 0", status == 0);
    fail += expect_true("CLI default greets World",
        strcmp(out, "Hello World!\n") == 0 ||
        strcmp(out, "Hello World!\r\n") == 0);

    fail += test_cli_run(short_name, out, sizeof(out), err, sizeof(err), &status);
    fail += expect_true("CLI -n exits 0", status == 0);
    fail += expect_true("CLI -n greets name",
        strcmp(out, "Hello John!\n") == 0 ||
        strcmp(out, "Hello John!\r\n") == 0);

    fail += test_cli_run(long_name, out, sizeof(out), err, sizeof(err), &status);
    fail += expect_true("CLI --name exits 0", status == 0);
    fail += expect_true("CLI --name greets name",
        strcmp(out, "Hello Jane!\n") == 0 ||
        strcmp(out, "Hello Jane!\r\n") == 0);

    fail += test_cli_run(help, out, sizeof(out), err, sizeof(err), &status);
    fail += expect_true("CLI -h exits 0", status == 0);
    fail += expect_true("CLI -h prints usage", strstr(out, "Usage:") != NULL);

    fail += test_cli_run(long_help, out, sizeof(out), err, sizeof(err), &status);
    fail += expect_true("CLI --help exits 0", status == 0);
    fail += expect_true("CLI --help prints usage", strstr(out, "Usage:") != NULL);

    fail += test_cli_run(version, out, sizeof(out), err, sizeof(err), &status);
    fail += expect_true("CLI -v exits 0", status == 0);
    fail += expect_true("CLI -v prints build", strstr(out, "libr build") != NULL);

    fail += test_cli_run(long_version, out, sizeof(out), err, sizeof(err), &status);
    fail += expect_true("CLI --version exits 0", status == 0);
    fail += expect_true("CLI --version prints build",
        strstr(out, "libr build") != NULL);

    fail += test_cli_run(missing_name, out, sizeof(out), err, sizeof(err), &status);
    fail += expect_true("CLI missing name exits 1", status == 1);
    fail += expect_true("CLI missing name diagnostic",
        strstr(err, "missing value for -n") != NULL);

    fail += test_cli_run(unknown, out, sizeof(out), err, sizeof(err), &status);
    fail += expect_true("CLI unknown option exits 1", status == 1);
    fail += expect_true("CLI unknown option diagnostic",
        strstr(err, "unknown option") != NULL);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}
#endif

static int case_all(void) {
    int rc = 0;

#ifdef __EMSCRIPTEN__
    test_case_total = 2;
#else
    int cli_enabled = KC_LIBR_TEST_CLI[0] != '\0';
    test_case_total = cli_enabled ? 3 : 2;
#endif

    test_case_current = 0;
    run_case(&rc, case_kc_libr_greet);
    run_case(&rc, case_kc_libr_version);
#ifndef __EMSCRIPTEN__
    if (cli_enabled) {
        run_case(&rc, case_kc_libr_cli);
    }
#endif

    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_libr_greet") == 0) return case_kc_libr_greet();
    if (strcmp(argv[1], "kc_libr_version") == 0) return case_kc_libr_version();
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "kc_libr_cli") == 0) return case_kc_libr_cli();
#endif
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
