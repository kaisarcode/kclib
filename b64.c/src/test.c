/**
 * test.c - Contract tests for the b64 library
 * Summary: Public API tests for encode and decode.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libb64.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef KC_B64_TEST_CLI
#define KC_B64_TEST_CLI ""
#endif

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Canonical test case name.
 * @param description Test case description.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *description) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, description);
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
 * Verifies one boolean condition.
 * @param name Check description.
 * @param cond Non-zero when the check passed.
 * @return 0 on success, 1 on failure.
 */
static int expect_true(const char *name, int cond) {
    if (!cond) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
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
 * Verifies one string result.
 * @param name Check description.
 * @param expected Expected string.
 * @param actual Actual string.
 * @return 0 on success, 1 on failure.
 */
static int expect_str(const char *name, const char *expected, const char *actual) {
    if (strcmp(expected, actual) != 0) {
        printf("[FAIL] %s: expected '%s', got '%s'\n", name, expected, actual);
        return 1;
    }
    return 0;
}

/**
 * Verifies stdout carries exactly the expected byte string.
 * @param name Check description.
 * @param out Captured child stdout.
 * @param expected Expected bytes.
 * @param expected_len Expected byte count.
 * @return 0 on success, 1 on failure.
 */
static int expect_bytes(const char *name, const char *out,
        const unsigned char *expected, size_t expected_len) {
    size_t len = strlen(out);
    size_t i;

    if (len != expected_len) {
        printf("[FAIL] %s: expected %zu bytes, got %zu\n",
            name, expected_len, len);
        return 1;
    }
    for (i = 0; i < expected_len; i++) {
        if ((unsigned char)out[i] != expected[i]) {
            printf("[FAIL] %s: byte %zu mismatch\n", name, i);
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
 * Runs the b64 CLI through CreateProcessW and captures its output.
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

    if (test_cli_to_wide(KC_B64_TEST_CLI, exe, sizeof(exe) / sizeof(wchar_t))) {
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
 * Runs the b64 CLI through a forked child and captures its output.
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
 * Tests encoding empty input.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_b64_encode_empty(void) {
    char *encoded = kc_b64_encode("", 0);
    int fail = 0;
    fail += expect_true("encode empty returns non-NULL", encoded != NULL);
    if (encoded) {
        fail += expect_str("encode empty returns empty string", "", encoded);
        kc_b64_free(encoded);
    }
    case_result(fail, "kc_b64_encode_empty", "encodes empty input");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests encoding "hello" string.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_b64_encode_hello(void) {
    const char *data = "hello";
    char *encoded = kc_b64_encode(data, strlen(data));
    int fail = 0;
    fail += expect_true("encode hello returns non-NULL", encoded != NULL);
    if (encoded) {
        fail += expect_str("encode hello matches expected", "aGVsbG8=", encoded);
        kc_b64_free(encoded);
    }
    case_result(fail, "kc_b64_encode_hello", "encodes hello to expected base64");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests encoding binary data with round-trip.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_b64_encode_binary(void) {
    const unsigned char data[] = {0, 1, 127, 128, 255};
    char *encoded = kc_b64_encode(data, sizeof(data));
    size_t decoded_len = 0;
    void *decoded;
    int fail = 0;
    fail += expect_true("encode binary returns non-NULL", encoded != NULL);
    if (encoded) {
        decoded = kc_b64_decode(encoded, &decoded_len);
        fail += expect_true("round-trip decode returns non-NULL", decoded != NULL);
        if (decoded) {
            fail += expect_int("round-trip length", (int)sizeof(data), (int)decoded_len);
            fail += expect_true("round-trip data matches", memcmp(data, decoded, sizeof(data)) == 0);
            kc_b64_free(decoded);
        }
        kc_b64_free(encoded);
    }
    case_result(fail, "kc_b64_encode_binary", "round-trips binary bytes through encode");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests decoding empty string.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_b64_decode_empty(void) {
    size_t out_size = 0;
    void *decoded = kc_b64_decode("", &out_size);
    int fail = 0;
    fail += expect_true("decode empty returns non-NULL", decoded != NULL);
    fail += expect_int("decode empty size", 0, (int)out_size);
    kc_b64_free(decoded);
    case_result(fail, "kc_b64_decode_empty", "decodes empty string");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests decoding "aGVsbG8=" to "hello".
 * @return 0 on success, 1 on failure.
 */
static int case_kc_b64_decode_hello(void) {
    size_t out_size = 0;
    void *decoded = kc_b64_decode("aGVsbG8=", &out_size);
    int fail = 0;
    fail += expect_true("decode hello returns non-NULL", decoded != NULL);
    if (decoded) {
        fail += expect_int("decode hello length", 5, (int)out_size);
        fail += expect_true("decode hello matches", memcmp(decoded, "hello", 5) == 0);
        kc_b64_free(decoded);
    }
    case_result(fail, "kc_b64_decode_hello", "decodes the expected hello bytes");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests encode/decode round-trip with various byte values.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_b64_roundtrip(void) {
    const unsigned char data[] = {0, 1, 127, 128, 255, 'A', 'B'};
    size_t data_len = sizeof(data);
    char *encoded;
    size_t decoded_len = 0;
    void *decoded;
    int fail = 0;

    encoded = kc_b64_encode(data, data_len);
    fail += expect_true("roundtrip encode returns non-NULL", encoded != NULL);

    if (encoded != NULL) {
        decoded = kc_b64_decode(encoded, &decoded_len);
        fail += expect_true("roundtrip decode returns non-NULL", decoded != NULL);
        fail += expect_int("roundtrip length", (int)data_len, (int)decoded_len);
        if (decoded != NULL) {
            fail += expect_true("roundtrip data matches", memcmp(data, decoded, data_len) == 0);
        }
        kc_b64_free(decoded);
    }
    kc_b64_free(encoded);
    case_result(fail, "kc_b64_roundtrip", "round-trips varied bytes through encode/decode");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests decoding invalid base64 string.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_b64_decode_invalid(void) {
    size_t out_size = 0;
    void *decoded = kc_b64_decode("invalid!", &out_size);
    int fail = 0;
    fail += expect_true("decode invalid returns NULL", decoded == NULL);
    case_result(fail, "kc_b64_decode_invalid", "rejects invalid base64 characters");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests decoding string with length not divisible by 4.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_b64_decode_bad_length(void) {
    size_t out_size = 0;
    void *decoded = kc_b64_decode("abc", &out_size);
    int fail = 0;
    fail += expect_true("decode bad length returns NULL", decoded == NULL);
    case_result(fail, "kc_b64_decode_bad_length", "rejects a length not divisible by four");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests NULL argument handling.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_b64_null_args(void) {
    int fail = 0;
    fail += expect_true("encode NULL data returns NULL", kc_b64_encode(NULL, 0) == NULL);
    fail += expect_true("decode NULL str returns NULL", kc_b64_decode(NULL, &(size_t){0}) == NULL);
    case_result(fail, "kc_b64_null_args", "rejects NULL arguments");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_b64_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_b64_free(void) {
    kc_b64_free(NULL);
    case_result(0, "kc_b64_free", "releases allocations and accepts NULL");
    return 0;
}

/**
 * Tests version function.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_b64_version(void) {
    int fail = 0;
    fail += expect_true("version returns non-zero", kc_b64_version() != 0);
    case_result(fail, "kc_b64_version", "returns a nonzero generated build version");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests the b64 CLI flags, verbs, diagnostics, and stdin framing.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_b64_cli(void) {
    int cli_enabled = KC_B64_TEST_CLI[0] != '\0';
    int fail = 0;

#ifdef _WIN32
    cli_enabled = 1;
#endif
    if (!cli_enabled) {
        case_result(fail, "kc_b64_cli",
            "help, version, encode/decode, stdin, binary output, and diagnostics");
        return 0;
    }

    {
        char out[4096];
        char err[4096];
        int status = 0;
        char *help[] = { (char *)KC_B64_TEST_CLI, "-h", (char *)NULL };
        char *long_help[] = { (char *)KC_B64_TEST_CLI, "--help", (char *)NULL };
        char *version[] = { (char *)KC_B64_TEST_CLI, "-v", (char *)NULL };
        char *long_version[] = { (char *)KC_B64_TEST_CLI, "--version", (char *)NULL };

        fail += expect_int("CLI -h exits 0", 0,
            test_cli_run_input(help, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI -h prints usage", strstr(out, "Usage:") != NULL);
        fail += expect_int("CLI --help exits 0", 0,
            test_cli_run_input(long_help, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI --help prints usage", strstr(out, "Usage:") != NULL);
        fail += expect_int("CLI -v exits 0", 0,
            test_cli_run_input(version, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI -v prints build", strstr(out, "b64 build") != NULL);
        fail += expect_int("CLI --version exits 0", 0,
            test_cli_run_input(long_version, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI --version prints build", strstr(out, "b64 build") != NULL);
    }

    {
        char out[4096];
        char err[4096];
        int status = 0;
#ifdef _WIN32
        static const unsigned char hello_encoded[] =
            { 'a', 'G', 'V', 's', 'b', 'G', '8', '=', '\r', '\n' };
        static const unsigned char bare_newline[] = { '\r', '\n' };
#else
        static const unsigned char hello_encoded[] =
            { 'a', 'G', 'V', 's', 'b', 'G', '8', '=', '\n' };
        static const unsigned char bare_newline[] = { '\n' };
#endif
        static const unsigned char hello[] = { 'h', 'e', 'l', 'l', 'o' };
        char *encode_cli[] = { (char *)KC_B64_TEST_CLI, "encode", (char *)NULL };
        char *encode_flag[] = { (char *)KC_B64_TEST_CLI, "--encode", (char *)NULL };
        char *encode_arg[] = { (char *)KC_B64_TEST_CLI, "encode", "hello", (char *)NULL };
        char *short_encode[] = { (char *)KC_B64_TEST_CLI, "-e", "hello", (char *)NULL };
        char *decode_cli[] = { (char *)KC_B64_TEST_CLI, "decode", (char *)NULL };
        char *decode_flag[] = { (char *)KC_B64_TEST_CLI, "--decode", (char *)NULL };
        char *decode_arg[] = { (char *)KC_B64_TEST_CLI, "decode", "aGVsbG8=", (char *)NULL };
        char *short_decode[] = { (char *)KC_B64_TEST_CLI, "-d", "aGVsbG8=", (char *)NULL };

        fail += expect_int("CLI encode stdin hello exits 0", 0,
            test_cli_run_input(encode_cli, "hello", 5,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_bytes("CLI encode stdin hello outputs base64", out,
            hello_encoded, sizeof(hello_encoded));

        fail += expect_int("CLI encode empty stdin exits 0", 0,
            test_cli_run_input(encode_cli, NULL, 0,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_bytes("CLI encode empty stdin outputs newline", out,
            bare_newline, sizeof(bare_newline));

        fail += expect_int("CLI --encode stdin hello exits 0", 0,
            test_cli_run_input(encode_flag, "hello", 5,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_bytes("CLI --encode stdin hello outputs base64", out,
            hello_encoded, sizeof(hello_encoded));

        fail += expect_int("CLI encode text precedence exits 0", 0,
            test_cli_run_input(encode_arg, "ignored", 7,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_bytes("CLI encode text precedence outputs base64", out,
            hello_encoded, sizeof(hello_encoded));

        fail += expect_int("CLI -e text exits 0", 0,
            test_cli_run_input(short_encode, NULL, 0,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_bytes("CLI -e text outputs base64", out,
            hello_encoded, sizeof(hello_encoded));

        fail += expect_int("CLI decode stdin trailing newline exits 0", 0,
            test_cli_run_input(decode_cli, "aGVsbG8=\n", 9,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_bytes("CLI decode stdin trailing newline outputs hello", out,
            hello, sizeof(hello));

        fail += expect_int("CLI decode stdin hello exits 0", 0,
            test_cli_run_input(decode_cli, "aGVsbG8=", 8,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_bytes("CLI decode stdin hello outputs hello", out,
            hello, sizeof(hello));

        fail += expect_int("CLI --decode stdin hello exits 0", 0,
            test_cli_run_input(decode_flag, "aGVsbG8=", 8,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_bytes("CLI --decode stdin hello outputs hello", out,
            hello, sizeof(hello));

        fail += expect_int("CLI decode text precedence exits 0", 0,
            test_cli_run_input(decode_arg, "!!!!", 4,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_bytes("CLI decode text precedence outputs hello", out,
            hello, sizeof(hello));

        fail += expect_int("CLI -d text exits 0", 0,
            test_cli_run_input(short_decode, NULL, 0,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_bytes("CLI -d text outputs hello", out,
            hello, sizeof(hello));
    }

    {
        char out[4096];
        char err[4096];
        int status = 0;
        static const char binary_input[] = "AH//";
        char *decode_cli[] = { (char *)KC_B64_TEST_CLI, "decode", (char *)NULL };
        char *decode_arg[] = { (char *)KC_B64_TEST_CLI, "decode", "AH//", (char *)NULL };

        fail += expect_int("CLI decode stdin binary exits 0", 0,
            test_cli_run_input(decode_cli, binary_input, sizeof(binary_input) - 1,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI decode stdin binary byte 0 is 0x00",
            (unsigned char)out[0] == 0x00);
        fail += expect_true("CLI decode stdin binary byte 1 is 0x7F",
            (unsigned char)out[1] == 0x7F);
        fail += expect_true("CLI decode stdin binary byte 2 is 0xFF",
            (unsigned char)out[2] == 0xFF);
        fail += expect_true("CLI decode stdin binary ends after 3 bytes",
            out[3] == '\0');

        fail += expect_int("CLI decode text binary exits 0", 0,
            test_cli_run_input(decode_arg, NULL, 0,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI decode text binary byte 0 is 0x00",
            (unsigned char)out[0] == 0x00);
        fail += expect_true("CLI decode text binary byte 1 is 0x7F",
            (unsigned char)out[1] == 0x7F);
        fail += expect_true("CLI decode text binary byte 2 is 0xFF",
            (unsigned char)out[2] == 0xFF);
        fail += expect_true("CLI decode text binary ends after 3 bytes",
            out[3] == '\0');
    }

    {
        char out[4096];
        char err[4096];
        int status = 0;
        char *decode_invalid[] = { (char *)KC_B64_TEST_CLI, "decode", "!!!!", (char *)NULL };
        char *decode_bad_length[] = { (char *)KC_B64_TEST_CLI, "decode", "abc", (char *)NULL };
        char *unknown_option[] = { (char *)KC_B64_TEST_CLI, "--nope", (char *)NULL };
        char *encode_extra_flag[] = { (char *)KC_B64_TEST_CLI, "encode", "-e", (char *)NULL };
        char *encode_extra_text[] = { (char *)KC_B64_TEST_CLI, "encode", "one", "two", (char *)NULL };
        char *missing_command[] = { (char *)KC_B64_TEST_CLI, (char *)NULL };
        char *unknown_command[] = { (char *)KC_B64_TEST_CLI, "nonsense", (char *)NULL };

        fail += expect_int("CLI decode invalid exits 1", 1,
            test_cli_run_input(decode_invalid, NULL, 0,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI decode invalid diagnostic",
            strstr(err, "decode error") != NULL);

        fail += expect_int("CLI decode bad length exits 1", 1,
            test_cli_run_input(decode_bad_length, NULL, 0,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI decode bad length diagnostic",
            strstr(err, "decode error") != NULL);

        fail += expect_int("CLI unknown option exits 1", 1,
            test_cli_run_input(unknown_option, NULL, 0,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI unknown option diagnostic",
            strstr(err, "unknown option") != NULL);

        fail += expect_int("CLI encode extra flag exits 1", 1,
            test_cli_run_input(encode_extra_flag, NULL, 0,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI encode extra flag diagnostic",
            strstr(err, "unexpected argument") != NULL);

        fail += expect_int("CLI encode extra text exits 1", 1,
            test_cli_run_input(encode_extra_text, NULL, 0,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI encode extra text diagnostic",
            strstr(err, "unexpected argument") != NULL);

        fail += expect_int("CLI missing command exits 1", 1,
            test_cli_run_input(missing_command, NULL, 0,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI missing command prints usage",
            strstr(out, "Usage:") != NULL);
        fail += expect_true("CLI missing command diagnostic",
            strstr(err, "missing command") != NULL);

        fail += expect_int("CLI unknown command exits 1", 1,
            test_cli_run_input(unknown_command, NULL, 0,
                out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI unknown command prints usage",
            strstr(out, "Usage:") != NULL);
        fail += expect_true("CLI unknown command diagnostic",
            strstr(err, "unknown command") != NULL);
    }

    case_result(fail, "kc_b64_cli",
        "help, version, encode/decode, stdin, binary output, and diagnostics");
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, 1 on failure.
 */
static int case_all(void) {
    int cli_enabled;
    int rc = 0;

    cli_enabled = KC_B64_TEST_CLI[0] != '\0';
#ifdef _WIN32
    cli_enabled = 1;
#endif
    test_case_total = cli_enabled ? 12 : 11;
    test_case_current = 0;
    run_case(&rc, case_kc_b64_encode_empty);
    run_case(&rc, case_kc_b64_encode_hello);
    run_case(&rc, case_kc_b64_encode_binary);
    run_case(&rc, case_kc_b64_decode_empty);
    run_case(&rc, case_kc_b64_decode_hello);
    run_case(&rc, case_kc_b64_roundtrip);
    run_case(&rc, case_kc_b64_decode_invalid);
    run_case(&rc, case_kc_b64_decode_bad_length);
    run_case(&rc, case_kc_b64_null_args);
    run_case(&rc, case_kc_b64_free);
    run_case(&rc, case_kc_b64_version);
    if (cli_enabled) {
        run_case(&rc, case_kc_b64_cli);
    }
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one b64 public API test case.
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
    if (strcmp(argv[1], "kc_b64_encode_empty") == 0) return case_kc_b64_encode_empty();
    if (strcmp(argv[1], "kc_b64_encode_hello") == 0) return case_kc_b64_encode_hello();
    if (strcmp(argv[1], "kc_b64_encode_binary") == 0) return case_kc_b64_encode_binary();
    if (strcmp(argv[1], "kc_b64_decode_empty") == 0) return case_kc_b64_decode_empty();
    if (strcmp(argv[1], "kc_b64_decode_hello") == 0) return case_kc_b64_decode_hello();
    if (strcmp(argv[1], "kc_b64_roundtrip") == 0) return case_kc_b64_roundtrip();
    if (strcmp(argv[1], "kc_b64_decode_invalid") == 0) return case_kc_b64_decode_invalid();
    if (strcmp(argv[1], "kc_b64_decode_bad_length") == 0) return case_kc_b64_decode_bad_length();
    if (strcmp(argv[1], "kc_b64_null_args") == 0) return case_kc_b64_null_args();
    if (strcmp(argv[1], "kc_b64_free") == 0) return case_kc_b64_free();
    if (strcmp(argv[1], "kc_b64_version") == 0) return case_kc_b64_version();
    if (strcmp(argv[1], "kc_b64_cli") == 0) return case_kc_b64_cli();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
