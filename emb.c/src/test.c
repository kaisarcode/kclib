/**
 * test.c - libemb public API and CLI tests.
 * Summary: Contract tests for fixed-model embeddings and the shipped CLI.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libemb.h"

#ifndef __EMSCRIPTEN__
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EMB_TEST_CLI
#define EMB_TEST_CLI ""
#endif

#define EMB_EXPECTED_DIM 384

static int test_case_total = 0;
static int test_case_current = 0;

typedef int (*case_fn)(void);

/**
 * Prints one canonical test-case result line.
 * @param fail Nonzero when the case failed.
 * @param name Canonical test-case name.
 * @param detail Human-readable behavior description.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

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
 * Verifies one boolean condition.
 * @param name Check description.
 * @param condition Nonzero when the check passed.
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
 * Verifies two embedding vectors are close within tolerance.
 * @param name Check description.
 * @param a First vector.
 * @param b Second vector.
 * @param count Number of elements.
 * @param tolerance Maximum absolute difference.
 * @return 0 on success, 1 on failure.
 */
static int expect_vectors_close(
    const char *name,
    const float *a,
    const float *b,
    size_t count,
    float tolerance
) {
    size_t i;

    for (i = 0; i < count; i++) {
        if (fabsf(a[i] - b[i]) > tolerance) {
            printf("[FAIL] %s: vectors differ at %zu\n", name, i);
            return 1;
        }
    }
    return 0;
}

/**
 * Verifies two embedding vectors contain a measurable difference.
 * @param name Check description.
 * @param a First vector.
 * @param b Second vector.
 * @param count Number of elements.
 * @return 0 on success, 1 on failure.
 */
static int expect_vectors_distinct(
    const char *name,
    const float *a,
    const float *b,
    size_t count
) {
    size_t i;

    for (i = 0; i < count; i++) {
        if (fabsf(a[i] - b[i]) > 0.000001f) return 0;
    }
    printf("[FAIL] %s\n", name);
    return 1;
}

/**
 * Verifies one embedding vector has the fixed model shape and finite values.
 * @param name Check description.
 * @param vec Vector data.
 * @param count Element count.
 * @return 0 on success, 1 on failure.
 */
static int expect_vector_valid(const char *name, const float *vec, size_t count) {
    size_t i;
    int nonzero = 0;

    if (!vec || count != EMB_EXPECTED_DIM) {
        printf("[FAIL] %s: expected %d floats, got %zu\n",
            name, EMB_EXPECTED_DIM, count);
        return 1;
    }
    for (i = 0; i < count; i++) {
        if (!isfinite(vec[i])) {
            printf("[FAIL] %s: non-finite value at %zu\n", name, i);
            return 1;
        }
        if (vec[i] != 0.0f) nonzero = 1;
    }
    return expect_true(name, nonzero);
}

#ifndef __EMSCRIPTEN__
#ifdef _WIN32
/**
 * Appends one argument to a Windows command line.
 * @param cmd Command-line buffer.
 * @param cap Buffer capacity in wide characters.
 * @param arg Argument to append.
 * @return 0 on success, 1 on failure.
 */
static int test_cli_append_arg(wchar_t *cmd, size_t cap, const wchar_t *arg) {
    size_t n = wcslen(cmd);
    size_t len = wcslen(arg);
    int quote = len == 0 || wcschr(arg, L' ') != NULL ||
        wcschr(arg, L'\t') != NULL || wcschr(arg, L'"') != NULL;
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
 * @param in UTF-8 input.
 * @param out Destination buffer.
 * @param cap Destination capacity.
 * @return 0 on success, 1 on failure.
 */
static int test_cli_to_wide(const char *in, wchar_t *out, size_t cap) {
    return MultiByteToWideChar(CP_UTF8, 0, in, -1, out, (int)cap) > 0 ? 0 : 1;
}

/**
 * Reads all bytes from one Windows pipe.
 * @param pipe Pipe handle.
 * @param buf Destination buffer.
 * @param size Destination buffer size.
 * @return 0.
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
 * Runs the CLI with controlled stdin and captured stdout/stderr.
 * @param argv Null-terminated argument vector.
 * @param input Input bytes for stdin.
 * @param input_len Input byte count.
 * @param out Standard-output buffer.
 * @param out_size Standard-output buffer size.
 * @param err Standard-error buffer.
 * @param err_size Standard-error buffer size.
 * @param out_status Destination process status.
 * @return 0 on harness success, 1 on harness failure.
 */
static int test_cli_run_input(
    char *const argv[],
    const char *input,
    size_t input_len,
    char *out,
    size_t out_size,
    char *err,
    size_t err_size,
    int *out_status
) {
    wchar_t exe[MAX_PATH];
    wchar_t cmd[32768];
    wchar_t wide[4096];
    HANDLE in_pipe[2], out_pipe[2], err_pipe[2];
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD exit_code, written;
    int i;

    if (test_cli_to_wide(EMB_TEST_CLI, exe, sizeof(exe) / sizeof(wchar_t))) return 1;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;
    if (!CreatePipe(&in_pipe[0], &in_pipe[1], &sa, 0)) return 1;
    if (!CreatePipe(&out_pipe[0], &out_pipe[1], &sa, 0)) return 1;
    if (!CreatePipe(&err_pipe[0], &err_pipe[1], &sa, 0)) return 1;
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
    if (test_cli_append_arg(cmd, sizeof(cmd) / sizeof(wchar_t), exe)) return 1;
    for (i = 1; argv[i]; i++) {
        if (test_cli_to_wide(argv[i], wide, sizeof(wide) / sizeof(wchar_t)) ||
                test_cli_append_arg(cmd, sizeof(cmd) / sizeof(wchar_t), wide)) {
            return 1;
        }
    }
    if (!CreateProcessW(exe, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        return 1;
    }
    CloseHandle(in_pipe[0]);
    CloseHandle(out_pipe[1]);
    CloseHandle(err_pipe[1]);
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
 * Runs the CLI with controlled stdin and captured stdout/stderr.
 * @param argv Null-terminated argument vector.
 * @param input Input bytes for stdin.
 * @param input_len Input byte count.
 * @param out Standard-output buffer.
 * @param out_size Standard-output buffer size.
 * @param err Standard-error buffer.
 * @param err_size Standard-error buffer size.
 * @param out_status Destination process status.
 * @return 0 on harness success, 1 on harness failure.
 */
static int test_cli_run_input(
    char *const argv[],
    const char *input,
    size_t input_len,
    char *out,
    size_t out_size,
    char *err,
    size_t err_size,
    int *out_status
) {
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

    if (waitpid(pid, &status, 0) < 0) return 1;
    *out_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    return 0;
}
#endif

/**
 * Validates one CLI vector line and advances to the next line.
 * @param name Check description.
 * @param text Current output position.
 * @param next Destination for the next output position.
 * @return 0 on success, 1 on failure.
 */
static int expect_cli_vector_line(
    const char *name,
    const char *text,
    const char **next
) {
    const char *p = text;
    size_t count = 0;
    int nonzero = 0;

    while (*p != '\0' && *p != '\r' && *p != '\n') {
        char *end;
        float value = strtof(p, &end);

        if (end == p || !isfinite(value)) {
            printf("[FAIL] %s: invalid float at element %zu\n", name, count);
            return 1;
        }
        if (value != 0.0f) nonzero = 1;
        count++;
        p = end;
        if (*p == ' ') {
            p++;
        } else if (*p != '\0' && *p != '\r' && *p != '\n') {
            printf("[FAIL] %s: invalid vector separator\n", name);
            return 1;
        }
    }

    if (count != EMB_EXPECTED_DIM || !nonzero) {
        printf("[FAIL] %s: expected %d nonzero-capable floats, got %zu\n",
            name, EMB_EXPECTED_DIM, count);
        return 1;
    }
    if (*p == '\r') p++;
    if (*p == '\n') p++;
    *next = p;
    return 0;
}
#endif

/**
 * Tests the public build version.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_emb_version(void) {
    int fail = expect_true("version nonzero", kc_emb_version() != 0U);

    case_result(fail, "kc_emb_version", "returns the generated build version");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests the fixed embedding dimension exposed by the model.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_emb_dimension(void) {
    size_t dimension = kc_emb_dimension();
    int fail = 0;

    fail += expect_true("dimension available", dimension != 0U);
    fail += expect_true("dimension matches embedded model",
        dimension == EMB_EXPECTED_DIM);

    case_result(fail, "kc_emb_dimension",
        "returns the embedded model vector dimension");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests direct fixed-model embedding.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_emb_embed(void) {
    float *vec = NULL;
    float *empty = NULL;
    size_t count = 0;
    size_t empty_count = 0;
    int fail = 0;

    fail += expect_int("embed text returns OK", KC_EMB_OK,
        kc_emb_embed("The quick brown fox", &vec, &count));
    fail += expect_vector_valid("text vector valid", vec, count);
    fail += expect_true("text count matches model dimension",
        count == kc_emb_dimension());

    fail += expect_int("embed empty returns OK", KC_EMB_OK,
        kc_emb_embed("", &empty, &empty_count));
    fail += expect_vector_valid("empty vector valid", empty, empty_count);
    fail += expect_true("empty count matches model dimension",
        empty_count == kc_emb_dimension());

    kc_emb_free(vec);
    kc_emb_free(empty);
    case_result(fail, "kc_emb_embed",
        "embeds text directly with the fixed local model");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests direct embedding argument and ownership contracts.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_emb_contract(void) {
    float *vec = (float *)0x1;
    size_t count = 123;
    int fail = 0;

    fail += expect_int("NULL input errors", KC_EMB_ERROR,
        kc_emb_embed(NULL, &vec, &count));
    fail += expect_true("NULL input resets vector", vec == NULL);
    fail += expect_true("NULL input resets count", count == 0);

    count = 123;
    fail += expect_int("NULL out_data errors", KC_EMB_ERROR,
        kc_emb_embed("input", NULL, &count));
    fail += expect_true("NULL out_data resets count", count == 0);

    vec = (float *)0x1;
    fail += expect_int("NULL out_count errors", KC_EMB_ERROR,
        kc_emb_embed("input", &vec, NULL));
    fail += expect_true("NULL out_count resets vector", vec == NULL);

    kc_emb_free(NULL);
    fail += expect_true("free NULL safe", 1);

    case_result(fail, "kc_emb_contract",
        "validates arguments, output reset, and ownership");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests deterministic fixed-model output.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_emb_determinism(void) {
    float *a = NULL;
    float *b = NULL;
    float *c = NULL;
    size_t ca = 0, cb = 0, cc = 0;
    int fail = 0;

    fail += expect_int("first embed OK", KC_EMB_OK,
        kc_emb_embed("The quick brown fox", &a, &ca));
    fail += expect_int("second embed OK", KC_EMB_OK,
        kc_emb_embed("The quick brown fox", &b, &cb));
    fail += expect_int("different embed OK", KC_EMB_OK,
        kc_emb_embed("incident response runbook", &c, &cc));
    fail += expect_true("all counts fixed",
        ca == EMB_EXPECTED_DIM && cb == EMB_EXPECTED_DIM &&
        cc == EMB_EXPECTED_DIM);

    if (a && b && ca == cb) {
        fail += expect_vectors_close("same text deterministic",
            a, b, ca, 0.000001f);
    } else {
        fail += expect_true("determinism vectors available", 0);
    }
    if (a && c && ca == cc) {
        fail += expect_vectors_distinct("different text differs", a, c, ca);
    } else {
        fail += expect_true("distinct vectors available", 0);
    }

    kc_emb_free(a);
    kc_emb_free(b);
    kc_emb_free(c);
    case_result(fail, "kc_emb_determinism",
        "keeps fixed-model output deterministic and input-sensitive");
    return fail == 0 ? 0 : 1;
}

#ifndef __EMSCRIPTEN__
/**
 * Tests the shipped CLI contract as one grouped case.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_emb_cli(void) {
    int cli_enabled = EMB_TEST_CLI[0] != '\0';
    int fail = 0;
    int status = 0;
    char out[65536], err[8192], arg_out[65536];
    const char *next;

#ifdef _WIN32
    cli_enabled = 1;
#endif
    if (!cli_enabled) {
        case_result(0, "kc_emb_cli",
            "preserves argument/stdin input, vectors, help, and version");
        return 0;
    }

    {
        char *args[] = {
            (char *)EMB_TEST_CLI, "The", "quick", "brown", "fox", NULL
        };
        fail += expect_int("CLI argument exits 0", 0,
            test_cli_run_input(args, NULL, 0, out, sizeof(out),
                err, sizeof(err), &status) ? 1 : status);
        next = out;
        fail += expect_cli_vector_line("CLI argument vector", next, &next);
        fail += expect_true("CLI argument one line", *next == '\0');
        fail += expect_true("CLI argument stderr empty", err[0] == '\0');
        memcpy(arg_out, out, sizeof(arg_out));
        arg_out[sizeof(arg_out) - 1] = '\0';
    }
    {
        char *args[] = { (char *)EMB_TEST_CLI, NULL };
        const char *input = "The quick brown fox\n";
        fail += expect_int("CLI stdin exits 0", 0,
            test_cli_run_input(args, input, strlen(input), out, sizeof(out),
                err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI stdin equals argument output",
            strcmp(out, arg_out) == 0);
        fail += expect_true("CLI stdin stderr empty", err[0] == '\0');
    }
    {
        char *args[] = { (char *)EMB_TEST_CLI, NULL };
        const char *input =
            "The quick brown fox\nincident response runbook\n";
        fail += expect_int("CLI multiline stdin exits 0", 0,
            test_cli_run_input(args, input, strlen(input), out, sizeof(out),
                err, sizeof(err), &status) ? 1 : status);
        next = out;
        fail += expect_cli_vector_line("CLI stdin first vector", next, &next);
        fail += expect_cli_vector_line("CLI stdin second vector", next, &next);
        fail += expect_true("CLI stdin exactly two lines", *next == '\0');
    }
    {
        char *args[] = { (char *)EMB_TEST_CLI, NULL };
        const char *input = "\n";
        fail += expect_int("CLI empty line exits 0", 0,
            test_cli_run_input(args, input, strlen(input), out, sizeof(out),
                err, sizeof(err), &status) ? 1 : status);
        next = out;
        fail += expect_cli_vector_line("CLI empty line vector", next, &next);
        fail += expect_true("CLI empty line one vector", *next == '\0');
    }
    {
        char *args[] = { (char *)EMB_TEST_CLI, NULL };
        fail += expect_int("CLI empty stdin exits 0", 0,
            test_cli_run_input(args, NULL, 0, out, sizeof(out),
                err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI empty stdin stdout empty", out[0] == '\0');
        fail += expect_true("CLI empty stdin stderr empty", err[0] == '\0');
    }
    {
        char *short_dim[] = { (char *)EMB_TEST_CLI, "-d", NULL };
        char *long_dim[] = { (char *)EMB_TEST_CLI, "--dim", NULL };
        char *short_help[] = { (char *)EMB_TEST_CLI, "-h", NULL };
        char *long_help[] = { (char *)EMB_TEST_CLI, "--help", NULL };
        char *short_version[] = { (char *)EMB_TEST_CLI, "-v", NULL };
        char *long_version[] = { (char *)EMB_TEST_CLI, "--version", NULL };

        fail += expect_int("CLI -d exits 0", 0,
            test_cli_run_input(short_dim, NULL, 0, out, sizeof(out),
                err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI -d dimension", strcmp(out, "384\n") == 0 ||
            strcmp(out, "384\r\n") == 0);
        fail += expect_true("CLI -d stderr empty", err[0] == '\0');

        fail += expect_int("CLI --dim exits 0", 0,
            test_cli_run_input(long_dim, NULL, 0, out, sizeof(out),
                err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI --dim dimension",
            strcmp(out, "384\n") == 0 || strcmp(out, "384\r\n") == 0);
        fail += expect_true("CLI --dim stderr empty", err[0] == '\0');

        fail += expect_int("CLI -h exits 0", 0,
            test_cli_run_input(short_help, NULL, 0, out, sizeof(out),
                err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI -h usage", strstr(out, "Usage:") != NULL);
        fail += expect_true("CLI -h options", strstr(out, "Options:") != NULL);
        fail += expect_true("CLI -h dimension", strstr(out,
            "-d, --dim       Show embedded model vector dimension") != NULL);
        fail += expect_true("CLI -h help", strstr(out,
            "-h, --help      Show this help") != NULL);
        fail += expect_true("CLI -h version", strstr(out,
            "-v, --version   Show version") != NULL);

        fail += expect_int("CLI --help exits 0", 0,
            test_cli_run_input(long_help, NULL, 0, out, sizeof(out),
                err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI --help usage", strstr(out, "Usage:") != NULL);

        fail += expect_int("CLI -v exits 0", 0,
            test_cli_run_input(short_version, NULL, 0, out, sizeof(out),
                err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI -v build", strstr(out, "emb build ") != NULL);

        fail += expect_int("CLI --version exits 0", 0,
            test_cli_run_input(long_version, NULL, 0, out, sizeof(out),
                err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI --version build",
            strstr(out, "emb build ") != NULL);
    }
    {
        char *args[] = { (char *)EMB_TEST_CLI, "hello", "-h", NULL };
        fail += expect_int("CLI later -h is input and exits 0", 0,
            test_cli_run_input(args, NULL, 0, out, sizeof(out),
                err, sizeof(err), &status) ? 1 : status);
        next = out;
        fail += expect_cli_vector_line("CLI later -h vector", next, &next);
        fail += expect_true("CLI later -h one vector", *next == '\0');
    }

    case_result(fail, "kc_emb_cli",
        "preserves input, vectors, dimension, help, and version");
    return fail == 0 ? 0 : 1;
}
#endif

/**
 * Runs all reusable and platform-applicable test cases.
 * @return 0 on success, nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;

#ifdef __EMSCRIPTEN__
    test_case_total = 5;
#else
    test_case_total = 6;
#endif
    test_case_current = 0;
    run_case(&rc, case_kc_emb_version);
    run_case(&rc, case_kc_emb_dimension);
    run_case(&rc, case_kc_emb_embed);
    run_case(&rc, case_kc_emb_contract);
    run_case(&rc, case_kc_emb_determinism);
#ifndef __EMSCRIPTEN__
    run_case(&rc, case_kc_emb_cli);
#endif
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one emb contract test case.
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
    if (strcmp(argv[1], "kc_emb_version") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_emb_version();
    }
    if (strcmp(argv[1], "kc_emb_dimension") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_emb_dimension();
    }
    if (strcmp(argv[1], "kc_emb_embed") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_emb_embed();
    }
    if (strcmp(argv[1], "kc_emb_contract") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_emb_contract();
    }
    if (strcmp(argv[1], "kc_emb_determinism") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_emb_determinism();
    }
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "kc_emb_cli") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_emb_cli();
    }
#endif
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
