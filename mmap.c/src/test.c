/**
 * test.c - libmmap public API and CLI tests.
 * Summary: Contract tests for the file-backed mmap value API and shipped CLI.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libmmap.h"

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

#ifndef KC_MMAP_TEST_CLI
#define KC_MMAP_TEST_CLI ""
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

    if (test_cli_to_wide(KC_MMAP_TEST_CLI, exe, sizeof(exe) / sizeof(wchar_t))) return 1;
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
 * Creates a unique temporary file path.
 * @param out Destination buffer.
 * @param cap Destination buffer size.
 * @param suffix Test-specific suffix.
 * @return 0 on success, 1 on failure.
 */
static int temp_path(char *out, size_t cap, const char *suffix) {
#ifdef _WIN32
    char base[MAX_PATH];
    DWORD len = GetTempPathA((DWORD)sizeof(base), base);
    if (len == 0 || len >= sizeof(base)) return 1;
    return (size_t)snprintf(out, cap, "%skc-mmap-%lu-%s.bin",
        base, (unsigned long)GetCurrentProcessId(), suffix) >= cap;
#else
    const char *base = getenv("TMPDIR");
    if (!base || !base[0]) base = "/tmp";
    return (size_t)snprintf(out, cap, "%s/kc-mmap-%ld-%s.bin",
        base, (long)getpid(), suffix) >= cap;
#endif
}

/**
 * Writes exact bytes to a file.
 * @param path Destination path.
 * @param data Source bytes.
 * @param size Byte count.
 * @return 0 on success, 1 on failure.
 */
static int write_file(const char *path, const void *data, size_t size) {
    FILE *file = fopen(path, "wb");

    if (!file) return 1;
    if (size > 0U && fwrite(data, 1U, size, file) != size) {
        fclose(file);
        return 1;
    }
    return fclose(file) == 0 ? 0 : 1;
}

/**
 * Checks whether a path exists as a readable file.
 * @param path File path.
 * @return Nonzero when the file exists.
 */
static int file_exists(const char *path) {
    FILE *file = fopen(path, "rb");

    if (!file) return 0;
    fclose(file);
    return 1;
}

/**
 * Tests kc_mmap_open.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_mmap_open(void) {
    char missing[512];
    char existing[512];
    kc_mmap_t *map = NULL;
    const void *data = NULL;
    size_t size = 0U;
    int fail = 0;

    if (temp_path(missing, sizeof(missing), "open-missing") ||
        temp_path(existing, sizeof(existing), "open-existing")) return 1;
    remove(missing);
    remove(existing);
    if (write_file(existing, "AB", 2U)) return 1;

    fail += expect_int("open NULL out", KC_MMAP_ERROR,
        kc_mmap_open(NULL, existing));
    fail += expect_int("open NULL path", KC_MMAP_ERROR,
        kc_mmap_open(&map, NULL));
    fail += expect_int("open empty path", KC_MMAP_ERROR,
        kc_mmap_open(&map, ""));

    fail += expect_int("open missing path", KC_MMAP_OK,
        kc_mmap_open(&map, missing));
    fail += expect_int("missing path get", KC_MMAP_NOT_FOUND,
        kc_mmap_get(map, &data, &size));
    kc_mmap_close(map);
    map = NULL;

    fail += expect_int("open existing path", KC_MMAP_OK,
        kc_mmap_open(&map, existing));
    fail += expect_int("existing path get", KC_MMAP_OK,
        kc_mmap_get(map, &data, &size));
    fail += expect_true("existing data matches",
        size == 2U && data != NULL && memcmp(data, "AB", 2U) == 0);

    kc_mmap_close(map);
    remove(existing);
    case_result(fail, "kc_mmap_open",
        "opens existing and missing file-backed values");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mmap_get.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_mmap_get(void) {
    char path[512];
    kc_mmap_t *map = NULL;
    const void *data = (const void *)1;
    size_t size = 99U;
    int fail = 0;

    if (temp_path(path, sizeof(path), "get")) return 1;
    remove(path);
    if (write_file(path, "", 0U)) return 1;

    fail += expect_int("get NULL map", KC_MMAP_ERROR,
        kc_mmap_get(NULL, &data, &size));
    fail += expect_true("get NULL map clears outputs",
        data == NULL && size == 0U);

    fail += expect_int("open empty file", KC_MMAP_OK,
        kc_mmap_open(&map, path));
    fail += expect_int("get empty value", KC_MMAP_OK,
        kc_mmap_get(map, &data, &size));
    fail += expect_true("empty value has zero transport size",
        size == 0U);

    fail += expect_int("get NULL data output", KC_MMAP_ERROR,
        kc_mmap_get(map, NULL, &size));
    fail += expect_int("get NULL size output", KC_MMAP_ERROR,
        kc_mmap_get(map, &data, NULL));

    kc_mmap_close(map);
    remove(path);
    case_result(fail, "kc_mmap_get",
        "distinguishes an empty value from no value");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mmap_set.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_mmap_set(void) {
    char path[512];
    kc_mmap_t *map = NULL;
    const void *data = NULL;
    size_t size = 0U;
    int fail = 0;

    if (temp_path(path, sizeof(path), "set")) return 1;
    remove(path);

    fail += expect_int("open missing path", KC_MMAP_OK,
        kc_mmap_open(&map, path));
    fail += expect_int("set A", KC_MMAP_OK,
        kc_mmap_set(map, "A", 1U));
    fail += expect_int("get A", KC_MMAP_OK,
        kc_mmap_get(map, &data, &size));
    fail += expect_true("A visible before save",
        size == 1U && memcmp(data, "A", 1U) == 0);
    fail += expect_true("set does not save implicitly", !file_exists(path));

    fail += expect_int("replace with AB", KC_MMAP_OK,
        kc_mmap_set(map, "AB", 2U));
    fail += expect_int("get AB", KC_MMAP_OK,
        kc_mmap_get(map, &data, &size));
    fail += expect_true("replacement visible",
        size == 2U && memcmp(data, "AB", 2U) == 0);

    fail += expect_int("set NULL", KC_MMAP_OK,
        kc_mmap_set(map, NULL, 0U));
    fail += expect_int("get after NULL set", KC_MMAP_NOT_FOUND,
        kc_mmap_get(map, &data, &size));
    fail += expect_int("NULL with nonzero size fails", KC_MMAP_ERROR,
        kc_mmap_set(map, NULL, 1U));

    kc_mmap_close(map);
    remove(path);
    case_result(fail, "kc_mmap_set",
        "replaces the in-memory value without implicit persistence");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mmap_save.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_mmap_save(void) {
    char path[512];
    kc_mmap_t *map = NULL;
    kc_mmap_t *verify = NULL;
    const void *data = NULL;
    size_t size = 0U;
    int fail = 0;

    if (temp_path(path, sizeof(path), "save")) return 1;
    remove(path);

    fail += expect_int("open missing path", KC_MMAP_OK,
        kc_mmap_open(&map, path));
    fail += expect_int("save missing untouched value", KC_MMAP_ERROR,
        kc_mmap_save(map));

    fail += expect_int("set AB", KC_MMAP_OK,
        kc_mmap_set(map, "AB", 2U));
    fail += expect_int("save AB", KC_MMAP_OK, kc_mmap_save(map));
    fail += expect_true("save creates file", file_exists(path));

    fail += expect_int("open saved value", KC_MMAP_OK,
        kc_mmap_open(&verify, path));
    fail += expect_int("get saved value", KC_MMAP_OK,
        kc_mmap_get(verify, &data, &size));
    fail += expect_true("saved bytes match",
        size == 2U && memcmp(data, "AB", 2U) == 0);
    kc_mmap_close(verify);
    verify = NULL;

    fail += expect_int("set zero-byte string", KC_MMAP_OK,
        kc_mmap_set(map, "", 0U));
    fail += expect_int("save zero-byte string", KC_MMAP_OK, kc_mmap_save(map));
    fail += expect_int("open zero-byte saved value", KC_MMAP_OK,
        kc_mmap_open(&verify, path));
    fail += expect_int("get zero-byte saved value", KC_MMAP_OK,
        kc_mmap_get(verify, &data, &size));
    fail += expect_true("zero-byte file remains a value", size == 0U);
    kc_mmap_close(verify);
    verify = NULL;

    fail += expect_int("set null", KC_MMAP_OK,
        kc_mmap_set(map, NULL, 0U));
    fail += expect_int("get null before save", KC_MMAP_NOT_FOUND,
        kc_mmap_get(map, &data, &size));
    fail += expect_int("save null", KC_MMAP_OK, kc_mmap_save(map));
    fail += expect_true("saving null removes file", !file_exists(path));
    fail += expect_int("save invalidated null instance", KC_MMAP_ERROR,
        kc_mmap_save(map));

    kc_mmap_close(map);
    case_result(fail, "kc_mmap_save",
        "persists values and turns saved null into deletion");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mmap_del.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_mmap_del(void) {
    char path[512];
    kc_mmap_t *map = NULL;
    const void *data = NULL;
    size_t size = 0U;
    int fail = 0;

    if (temp_path(path, sizeof(path), "del")) return 1;
    remove(path);
    if (write_file(path, "AB", 2U)) return 1;

    fail += expect_int("open existing value", KC_MMAP_OK,
        kc_mmap_open(&map, path));
    fail += expect_int("delete value", KC_MMAP_OK, kc_mmap_del(map));
    fail += expect_true("delete removes backing file", !file_exists(path));
    fail += expect_int("get after delete fails", KC_MMAP_ERROR,
        kc_mmap_get(map, &data, &size));
    fail += expect_int("set after delete fails", KC_MMAP_ERROR,
        kc_mmap_set(map, "A", 1U));
    fail += expect_int("save after delete fails", KC_MMAP_ERROR,
        kc_mmap_save(map));
    fail += expect_int("delete after delete fails", KC_MMAP_ERROR,
        kc_mmap_del(map));
    kc_mmap_close(map);

    map = NULL;
    fail += expect_int("reopen deleted path", KC_MMAP_OK,
        kc_mmap_open(&map, path));
    fail += expect_int("reopened deleted path has no value",
        KC_MMAP_NOT_FOUND, kc_mmap_get(map, &data, &size));
    fail += expect_int("delete missing backing file", KC_MMAP_OK,
        kc_mmap_del(map));
    kc_mmap_close(map);

    case_result(fail, "kc_mmap_del",
        "deletes persistence and invalidates the instance");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mmap_close.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_mmap_close(void) {
    char path[512];
    kc_mmap_t *map = NULL;
    int fail = 0;

    if (temp_path(path, sizeof(path), "close")) return 1;
    remove(path);

    kc_mmap_close(NULL);
    fail += expect_int("open value", KC_MMAP_OK, kc_mmap_open(&map, path));
    kc_mmap_close(map);

    case_result(fail, "kc_mmap_close",
        "releases instances and accepts NULL");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mmap_version.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_mmap_version(void) {
    int fail = expect_true("version nonzero", kc_mmap_version() != 0U);

    case_result(fail, "kc_mmap_version",
        "returns a nonzero generated build version");
    return fail == 0 ? 0 : 1;
}

#ifndef __EMSCRIPTEN__
/**
 * Tests the shipped CLI contract as one grouped case.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_mmap_cli(void) {
    char path[512];
    char *set_direct[] = {
        (char *)KC_MMAP_TEST_CLI, path, "--set", "AB", NULL
    };
    char *set_stdin[] = {
        (char *)KC_MMAP_TEST_CLI, path, "-set", NULL
    };
    char *get_args[] = {
        (char *)KC_MMAP_TEST_CLI, path, "--get", NULL
    };
    char *del_args[] = {
        (char *)KC_MMAP_TEST_CLI, path, "-del", NULL
    };
    char *help_args[] = { (char *)KC_MMAP_TEST_CLI, "--help", NULL };
    char *version_args[] = { (char *)KC_MMAP_TEST_CLI, "-v", NULL };
    char *bad_args[] = {
        (char *)KC_MMAP_TEST_CLI, path, "--nope", NULL
    };
    char *bad_get_value[] = {
        (char *)KC_MMAP_TEST_CLI, path, "--get", "x", NULL
    };
    char out[8192];
    char err[8192];
    int status = 0;
    int fail = 0;

    if (KC_MMAP_TEST_CLI[0] == '\0') {
        case_result(0, "kc_mmap_cli",
            "preserves path-first set/get/del, stdin, direct values, help, and version");
        return 0;
    }

    if (temp_path(path, sizeof(path), "cli")) return 1;
    remove(path);

    fail += expect_int("CLI direct set exits 0", 0,
        test_cli_run_input(set_direct, NULL, 0U, out, sizeof(out),
            err, sizeof(err), &status) ? 1 : status);
    fail += expect_int("CLI get direct value exits 0", 0,
        test_cli_run_input(get_args, NULL, 0U, out, sizeof(out),
            err, sizeof(err), &status) ? 1 : status);
    fail += expect_string("CLI get direct value output", "AB", out);

    fail += expect_int("CLI stdin set exits 0", 0,
        test_cli_run_input(set_stdin, "CD", 2U, out, sizeof(out),
            err, sizeof(err), &status) ? 1 : status);
    fail += expect_int("CLI get stdin value exits 0", 0,
        test_cli_run_input(get_args, NULL, 0U, out, sizeof(out),
            err, sizeof(err), &status) ? 1 : status);
    fail += expect_string("CLI get stdin value output", "CD", out);

    fail += expect_int("CLI del exits 0", 0,
        test_cli_run_input(del_args, NULL, 0U, out, sizeof(out),
            err, sizeof(err), &status) ? 1 : status);
    fail += expect_int("CLI get missing exits 1", 1,
        test_cli_run_input(get_args, NULL, 0U, out, sizeof(out),
            err, sizeof(err), &status) ? 1 : status);
    fail += expect_true("CLI missing diagnostic",
        strstr(err, "value not found") != NULL);

    fail += expect_int("CLI help exits 0", 0,
        test_cli_run_input(help_args, NULL, 0U, out, sizeof(out),
            err, sizeof(err), &status) ? 1 : status);
    fail += expect_true("CLI help path-first usage",
        strstr(out, "mmap <path> -set|--set [value]") != NULL);
    fail += expect_int("CLI version exits 0", 0,
        test_cli_run_input(version_args, NULL, 0U, out, sizeof(out),
            err, sizeof(err), &status) ? 1 : status);
    fail += expect_true("CLI version output",
        strstr(out, "mmap build ") != NULL);
    fail += expect_int("CLI unknown option exits 1", 1,
        test_cli_run_input(bad_args, NULL, 0U, out, sizeof(out),
            err, sizeof(err), &status) ? 1 : status);
    fail += expect_int("CLI get value exits 1", 1,
        test_cli_run_input(bad_get_value, NULL, 0U, out, sizeof(out),
            err, sizeof(err), &status) ? 1 : status);

    remove(path);
    case_result(fail, "kc_mmap_cli",
        "preserves path-first set/get/del, stdin, direct values, help, and version");
    return fail == 0 ? 0 : 1;
}
#endif

/**
 * Runs all public contract cases.
 * @return Failed case count.
 */
static int case_all(void) {
    int rc = 0;
    int cli_enabled = KC_MMAP_TEST_CLI[0] != '\0';

    test_case_total = cli_enabled ? 8 : 7;
    test_case_current = 0;

    run_case(&rc, case_kc_mmap_open);
    run_case(&rc, case_kc_mmap_get);
    run_case(&rc, case_kc_mmap_set);
    run_case(&rc, case_kc_mmap_save);
    run_case(&rc, case_kc_mmap_del);
    run_case(&rc, case_kc_mmap_close);
    run_case(&rc, case_kc_mmap_version);
#ifndef __EMSCRIPTEN__
    if (cli_enabled) run_case(&rc, case_kc_mmap_cli);
#endif

    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Dispatches one named test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process exit status.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_mmap_open") == 0) return case_kc_mmap_open();
    if (strcmp(argv[1], "kc_mmap_get") == 0) return case_kc_mmap_get();
    if (strcmp(argv[1], "kc_mmap_set") == 0) return case_kc_mmap_set();
    if (strcmp(argv[1], "kc_mmap_save") == 0) return case_kc_mmap_save();
    if (strcmp(argv[1], "kc_mmap_del") == 0) return case_kc_mmap_del();
    if (strcmp(argv[1], "kc_mmap_close") == 0) return case_kc_mmap_close();
    if (strcmp(argv[1], "kc_mmap_version") == 0) return case_kc_mmap_version();
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "kc_mmap_cli") == 0) return case_kc_mmap_cli();
#endif
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
