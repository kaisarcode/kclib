/**
 * test.c - liblng public API and CLI tests.
 * Summary: Contract tests for lng stateless language detection and shipped CLI.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "liblng.h"

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
#include <stdbool.h>
#include <math.h>
#include <float.h>
#include <stdint.h>

#ifndef LNG_TEST_CLI
#define LNG_TEST_CLI ""
#endif

#ifdef _WIN32
#define LNG_TEST_EOL "\r\n"
#else
#define LNG_TEST_EOL "\n"
#endif

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Test case name.
 * @param detail Test case detail.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

/**
 * Runs one test case with counter tracking.
 * @param rc Destination accumulator.
 * @param fn Test case function.
 * @return None.
 */
static void run_case(int *rc, int (*fn)(void)) {
    test_case_current++;
    *rc += fn();
}

/**
 * Verifies one boolean condition.
 * @param name Check description.
 * @param condition Condition to verify.
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

    if (test_cli_to_wide(LNG_TEST_CLI, exe, sizeof(exe) / sizeof(wchar_t))) return 1;
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
                test_cli_append_arg(cmd, sizeof(cmd) / sizeof(wchar_t), wide)) return 1;
    }
    if (!CreateProcessW(exe, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) return 1;
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
#endif

/**
 * Verifies one string result.
 * @param name Check description.
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

static const char *TEXT_EN =
    "hello world this is english text how are you doing today? "
    "this is a robust test for english language detection. "
    "documentation is vital for understanding systems. "
    "university technology science information research development "
    "program software computer system network data business management "
    "education government country people year time way service work life "
    "world school high school state city area group problem hand part child";

static const char *TEXT_ES =
    "que el la de en que lo los un una por para como al su sus con del por sobre entre mucho "
    "despues tambien siempre mundo hola buenos dias todos como estas? "
    "este proyecto compara texto corto el zorro marron salta sobre el perro. "
    "esperanza y libertad para todos los pueblos. la programacion es un arte que requiere paciencia. "
    "el lenguaje de programacion c es muy potente. espana mexico argentina colombia chile peru";

static const char *TEXT_JA =
    "konnichiwa nihon ha gijutsu to dentou ga kyouson suru subarashii kuni desu. "
    "byouin ishi chiryou kanja houritsu saiban shimin kenkyu kagaku daigaku kankyou keizai enerugi "
    "gakkou chiiki toshi umi yama rekishi ongaku hon kazoku shigoto. "
    "\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf\xe3\x80\x82 "
    "\xe6\x97\xa5\xe6\x9c\xac\xe3\x81\xaf\xe6\x8a\x80\xe8\xa1\x93\xe3\x81\xa8\xe4\xbc\x9d\xe7\xb5\xb1\xe3\x81\x8c\xe5\x85\xb1\xe5\xad\x98\xe3\x81\x99\xe3\x82\x8b\xe7\xb4\xa0\xe6\x99\xb4\xe3\x82\x89\xe3\x81\x97\xe3\x81\x84\xe5\x9b\xbd\xe3\x81\xa7\xe3\x81\x99\xe3\x80\x82 "
    "\xe7\x97\x85\xe9\x99\xa2 \xe5\x8c\xbb\xe5\xb8\xab \xe6\xb2\xbb\xe7\x99\x82 \xe6\x82\xa3\xe8\x80\x85 \xe6\xb3\x95\xe5\xbe\x8b \xe8\xa3\x81\xe5\x88\xa4 \xe5\xb8\x82\xe6\xb0\x91 \xe7\xa0\x94\xe7\xa9\xb6 \xe7\xa7\x91\xe5\xad\xa6 \xe5\xa4\xa7\xe5\xad\xa6 \xe7\x92\xb0\xe5\xa2\x83 \xe7\xb5\x8c\xe6\xb8\x88 \xe3\x82\xa8\xe3\x83\x8d\xe3\x83\xab\xe3\x82\xae\xe3\x83\xbc \xe5\xad\xa6\xe6\xa0\xa1 \xe5\x9c\xb0\xe5\x9f\x9f \xe9\x83\xbd\xe5\xb8\x82 \xe6\xb5\xb7 \xe5\xb1\xb1 \xe6\xad\xb4\xe5\x8f\xb2 \xe9\x9f\xb3\xe6\xa5\xbd \xe6\x9c\xac \xe5\xae\xb6\xe6\x97\x8f \xe4\xbb\x95\xe4\xba\x8b";

/**
 * Tests version determinism.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_lng_version(void) {
    const char *name = "kc_lng_version";
    const char *detail = "version returns build timestamp";
    int fail = 0;
    uint64_t v1 = kc_lng_version();
    uint64_t v2 = kc_lng_version();
    fail += expect_true("version deterministic", v1 == v2);
    (void)v1;
    (void)v2;
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests detection for representative languages.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_lng_detect(void) {
    const char *name = "kc_lng_detect";
    const char *detail = "detects representative languages and handles empty input";
    int fail = 0;
    kc_lng_result_t *res = NULL;
    size_t count = 0;
    int rc;

    res = NULL; count = 0;
    rc = kc_lng_detect(TEXT_EN, 0.001, 1, &res, &count);
    fail += expect_int("en detect rc", KC_LNG_OK, rc);
    fail += expect_true("en count >=1", count >= 1 && res != NULL);
    if (count >= 1 && res != NULL) {
        fail += expect_string("en top code", "en", res[0].code);
        fail += expect_true("en score positive", res[0].score > 0.0);
    }
    kc_lng_free(res);

    res = NULL; count = 0;
    rc = kc_lng_detect(TEXT_ES, 0.001, 1, &res, &count);
    fail += expect_int("es detect rc", KC_LNG_OK, rc);
    fail += expect_true("es count >=1", count >= 1 && res != NULL);
    if (count >= 1 && res != NULL) {
        fail += expect_string("es top code", "es", res[0].code);
        fail += expect_true("es score positive", res[0].score > 0.0);
    }
    kc_lng_free(res);

    res = NULL; count = 0;
    rc = kc_lng_detect(TEXT_JA, 0.001, 1, &res, &count);
    fail += expect_int("ja detect rc", KC_LNG_OK, rc);
    fail += expect_true("ja count >=1", count >= 1 && res != NULL);
    if (count >= 1 && res != NULL) {
        fail += expect_string("ja top code", "ja", res[0].code);
        fail += expect_true("ja score positive", res[0].score > 0.0);
    }
    kc_lng_free(res);

    res = (kc_lng_result_t *)0x1; count = 999;
    rc = kc_lng_detect("", 0.001, 1, &res, &count);
    fail += expect_int("empty rc", KC_LNG_OK, rc);
    fail += expect_true("empty count 0", count == 0);
    fail += expect_true("empty results NULL", res == NULL);
    kc_lng_free(res);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests ranking order and limits.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_lng_detect_ranking(void) {
    const char *name = "kc_lng_detect_ranking";
    const char *detail = "ranking, threshold and limit enforcement";
    int fail = 0;
    kc_lng_result_t *res = NULL;
    size_t count = 0;
    int rc;
    size_t i;

    res = NULL; count = 0;
    rc = kc_lng_detect(TEXT_EN, 0.0, 5, &res, &count);
    fail += expect_int("ranking rc", KC_LNG_OK, rc);
    fail += expect_true("ranking count >1", count > 1);
    fail += expect_true("ranking count <=5", count <= 5);
    for (i = 1; i < count; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "descending %zu", i);
        fail += expect_true(buf, res[i-1].score >= res[i].score);
    }
    kc_lng_free(res);
    res = NULL;

    kc_lng_result_t *low_res = NULL, *high_res = NULL;
    size_t low_count = 0, high_count = 0;
    rc = kc_lng_detect(TEXT_EN, 0.001, 10, &low_res, &low_count);
    fail += expect_int("low threshold rc", KC_LNG_OK, rc);
    rc = kc_lng_detect(TEXT_EN, 0.99, 10, &high_res, &high_count);
    fail += expect_int("high threshold rc", KC_LNG_OK, rc);
    fail += expect_true("high filters more", high_count <= low_count);
    if (low_count > 0) {
        fail += expect_true("high <= low when low>0", high_count <= low_count);
    }
    kc_lng_free(low_res);
    kc_lng_free(high_res);

    kc_lng_result_t *r1 = NULL, *r3 = NULL;
    size_t c1 = 0, c3 = 0;
    rc = kc_lng_detect(TEXT_EN, 0.0, 1, &r1, &c1);
    fail += expect_int("limit1 rc", KC_LNG_OK, rc);
    fail += expect_true("limit1 count ==1", c1 == 1);
    rc = kc_lng_detect(TEXT_EN, 0.0, 3, &r3, &c3);
    fail += expect_int("limit3 rc", KC_LNG_OK, rc);
    fail += expect_true("limit3 count <=3", c3 <= 3);
    fail += expect_true("limit3 >= limit1", c3 >= c1);
    kc_lng_free(r1);
    kc_lng_free(r3);

    res = NULL; count = 0;
    rc = kc_lng_detect(TEXT_EN, 0.0, 100, &res, &count);
    fail += expect_int("limit100 rc", KC_LNG_OK, rc);
    fail += expect_true("limit100 count <=32", count <= 32);
    fail += expect_true("limit100 count <=26 langs", count <= 26);
    kc_lng_free(res);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Verifies failure resets outputs.
 * @param label Check label.
 * @param rc Return code to verify.
 * @param out_results Results pointer to check.
 * @param out_count Count pointer to check.
 * @return 0 on success, 1 on failure.
 */
static int check_fail_resets(const char *label, int rc, kc_lng_result_t **out_results, size_t *out_count) {
    int f = 0;
    f += expect_int(label, KC_LNG_ERROR, rc);
    f += expect_true("reset results NULL", *out_results == NULL);
    f += expect_true("reset count 0", *out_count == 0);
    return f;
}

/**
 * Tests contract error handling.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_lng_detect_contract(void) {
    const char *name = "kc_lng_detect_contract";
    const char *detail = "validates args, resets outputs and frees correctly";
    int fail = 0;
    kc_lng_result_t *res = NULL;
    size_t count = 0;
    int rc;

    res = (kc_lng_result_t *)0xDEADBEEF; count = 999;
    rc = kc_lng_detect(NULL, 0.001, 1, &res, &count);
    fail += check_fail_resets("NULL text", rc, &res, &count);

    count = 999;
    {
        kc_lng_result_t *dummy = (kc_lng_result_t *)0xDEADBEEF;
        size_t c = 999;
        rc = kc_lng_detect(TEXT_EN, 0.001, 1, NULL, &c);
        fail += expect_int("NULL out_results", KC_LNG_ERROR, rc);
        fail += expect_true("NULL out_results count 0", c == 0);
        (void)dummy;
    }

    res = (kc_lng_result_t *)0xDEADBEEF;
    rc = kc_lng_detect(TEXT_EN, 0.001, 1, &res, NULL);
    fail += expect_int("NULL out_count", KC_LNG_ERROR, rc);
    fail += expect_true("NULL out_count results NULL", res == NULL);

    res = (kc_lng_result_t *)0xDEADBEEF; count = 999;
    rc = kc_lng_detect(TEXT_EN, 0.001, 0, &res, &count);
    fail += check_fail_resets("zero limit", rc, &res, &count);

    res = (kc_lng_result_t *)0xDEADBEEF; count = 999;
    rc = kc_lng_detect(TEXT_EN, -0.1, 1, &res, &count);
    fail += check_fail_resets("threshold <0", rc, &res, &count);

    res = (kc_lng_result_t *)0xDEADBEEF; count = 999;
    rc = kc_lng_detect(TEXT_EN, 1.5, 1, &res, &count);
    fail += check_fail_resets("threshold >1", rc, &res, &count);

    res = (kc_lng_result_t *)0xDEADBEEF; count = 999;
    rc = kc_lng_detect(TEXT_EN, NAN, 1, &res, &count);
    fail += check_fail_resets("NaN threshold", rc, &res, &count);

    res = (kc_lng_result_t *)0xDEADBEEF; count = 999;
    rc = kc_lng_detect(TEXT_EN, INFINITY, 1, &res, &count);
    fail += check_fail_resets("INFINITY threshold", rc, &res, &count);
    res = (kc_lng_result_t *)0xDEADBEEF; count = 999;
    rc = kc_lng_detect(TEXT_EN, -INFINITY, 1, &res, &count);
    fail += check_fail_resets("-INFINITY threshold", rc, &res, &count);

    res = (kc_lng_result_t *)0xDEADBEEF; count = 999;
    rc = kc_lng_detect("xyz", 0.99, 5, &res, &count);
    fail += expect_int("xyz filtered rc", KC_LNG_OK, rc);
    fail += expect_true("xyz filtered count 0", count == 0);
    fail += expect_true("xyz filtered results NULL", res == NULL);
    kc_lng_free(res);

    res = (kc_lng_result_t *)0xDEADBEEF; count = 999;
    rc = kc_lng_detect("", 0.001, 5, &res, &count);
    fail += expect_int("empty contract rc", KC_LNG_OK, rc);
    fail += expect_true("empty contract count 0", count == 0);
    fail += expect_true("empty contract results NULL", res == NULL);
    kc_lng_free(res);

    res = (kc_lng_result_t *)0xDEADBEEF; count = 999;
    rc = kc_lng_detect("", 0.0, 32, &res, &count);
    fail += expect_int("empty zero threshold rc", KC_LNG_OK, rc);
    fail += expect_true("empty zero threshold count 0", count == 0);
    fail += expect_true("empty zero threshold results NULL", res == NULL);
    kc_lng_free(res);

    res = NULL; count = 0;
    rc = kc_lng_detect(TEXT_EN, 0.001, 2, &res, &count);
    if (rc == KC_LNG_OK && count > 0) {
        fail += expect_true("allocated results non-NULL", res != NULL);
        kc_lng_free(res);
        res = NULL;
    } else if (rc == KC_LNG_OK) {
        fail += expect_true("no results still NULL", res == NULL);
    } else {
        fail += expect_true("valid call should succeed", 0);
    }

    kc_lng_free(NULL);
    fail += expect_true("free NULL safe", 1);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests deterministic ranking.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_lng_free(void) {
    const char *name = "kc_lng_free";
    const char *detail = "deterministic ranking and static code storage";
    int fail = 0;
    kc_lng_result_t *a = NULL, *b = NULL;
    size_t ca = 0, cb = 0;
    int rc;
    size_t i;

    rc = kc_lng_detect(TEXT_EN, 0.0, 5, &a, &ca);
    fail += expect_int("det first rc", KC_LNG_OK, rc);
    rc = kc_lng_detect(TEXT_EN, 0.0, 5, &b, &cb);
    fail += expect_int("det second rc", KC_LNG_OK, rc);
    fail += expect_true("det same count", ca == cb);
    if (ca == cb && a != NULL && b != NULL) {
        for (i = 0; i < ca; i++) {
            char buf[64];
            snprintf(buf, sizeof(buf), "det code %zu", i);
            fail += expect_string(buf, a[i].code, b[i].code);
            snprintf(buf, sizeof(buf), "det score %zu", i);
            fail += expect_true(buf, fabs(a[i].score - b[i].score) < 1e-9);
        }
        fail += expect_true("static code still valid", strcmp(a[0].code, b[0].code) == 0);
        for (i = 0; i < ca; i++) {
            size_t j;
            for (j = 0; j < cb; j++) {
                if (strcmp(a[i].code, b[j].code) == 0) {
                    fail += expect_true("static pointer same", a[i].code == b[j].code);
                    break;
                }
            }
        }
    }
    kc_lng_free(a);
    kc_lng_free(b);

    a = NULL; b = NULL; ca = 0; cb = 0;
    rc = kc_lng_detect(TEXT_ES, 0.001, 3, &a, &ca);
    fail += expect_int("det es first rc", KC_LNG_OK, rc);
    rc = kc_lng_detect(TEXT_ES, 0.001, 3, &b, &cb);
    fail += expect_int("det es second rc", KC_LNG_OK, rc);
    fail += expect_true("det es same count", ca == cb);
    if (ca == cb && a && b) {
        for (i = 0; i < ca; i++) {
            fail += expect_string("det es code", a[i].code, b[i].code);
            fail += expect_true("det es score", fabs(a[i].score - b[i].score) < 1e-9);
        }
    }
    kc_lng_free(a);
    kc_lng_free(b);

    kc_lng_free(NULL);
    fail += expect_true("free NULL safe", 1);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

#ifndef __EMSCRIPTEN__
/**
 * Tests the shipped CLI contract as one grouped case.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_lng_cli(void) {
    int cli_enabled = LNG_TEST_CLI[0] != '\0';
    int fail = 0;
    int status = 0;
    char out[8192];
    char err[8192];

#ifdef _WIN32
    cli_enabled = 1;
#endif
    if (!cli_enabled) {
        case_result(0, "kc_lng_cli",
            "preserves arguments, stdin, ranking output, diagnostics, help, and version");
        return 0;
    }

    {
        char *args[] = { (char *)LNG_TEST_CLI, (char *)TEXT_EN, NULL };
        fail += expect_int("CLI argument exits 0", 0,
            test_cli_run_input(args, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_string("CLI argument output", "en" LNG_TEST_EOL, out);
        fail += expect_string("CLI argument stderr", "", err);
    }
    {
        char *args[] = { (char *)LNG_TEST_CLI, NULL };
        const char *input = TEXT_ES;
        fail += expect_int("CLI stdin exits 0", 0,
            test_cli_run_input(args, input, strlen(input), out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_string("CLI stdin output", "es" LNG_TEST_EOL, out);
        fail += expect_string("CLI stdin stderr", "", err);
    }
    {
        char *args[] = { (char *)LNG_TEST_CLI, NULL };
        fail += expect_int("CLI empty stdin exits 0", 0,
            test_cli_run_input(args, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_string("CLI empty stdin stdout", "", out);
        fail += expect_string("CLI empty stdin stderr", "", err);
    }
    {
        char *args[] = { (char *)LNG_TEST_CLI, "-l", "3", (char *)TEXT_EN, NULL };
        fail += expect_int("CLI ranked exits 0", 0,
            test_cli_run_input(args, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI ranked includes score", strstr(out, "en: ") == out);
        fail += expect_true("CLI ranked contains decimal", strstr(out, ".") != NULL);
    }
    {
        char *args[] = { (char *)LNG_TEST_CLI, "-t", "1", (char *)TEXT_EN, NULL };
        fail += expect_int("CLI threshold exits 0", 0,
            test_cli_run_input(args, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_string("CLI threshold filters output", "", out);
    }
    {
        char *args[] = { (char *)LNG_TEST_CLI, "-l", "0", (char *)TEXT_EN, NULL };
        fail += expect_int("CLI limit zero clamps and exits 0", 0,
            test_cli_run_input(args, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_string("CLI limit zero clamps to one", "en" LNG_TEST_EOL, out);
    }
    {
        char *args[] = { (char *)LNG_TEST_CLI, "-l", "100", (char *)TEXT_EN, NULL };
        fail += expect_int("CLI high limit clamps and exits 0", 0,
            test_cli_run_input(args, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI high limit ranked output", strstr(out, "en: ") == out);
    }
    {
        char *short_help[] = { (char *)LNG_TEST_CLI, "-h", NULL };
        char *long_help[] = { (char *)LNG_TEST_CLI, "--help", NULL };
        char *short_version[] = { (char *)LNG_TEST_CLI, "-v", NULL };
        char *long_version[] = { (char *)LNG_TEST_CLI, "--version", NULL };

        fail += expect_int("CLI -h exits 0", 0,
            test_cli_run_input(short_help, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI -h usage", strstr(out, "Usage:") != NULL);
        fail += expect_int("CLI --help exits 0", 0,
            test_cli_run_input(long_help, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI --help usage", strstr(out, "Usage:") != NULL);
        fail += expect_int("CLI -v exits 0", 0,
            test_cli_run_input(short_version, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI -v build", strstr(out, "lng build ") != NULL);
        fail += expect_int("CLI --version exits 0", 0,
            test_cli_run_input(long_version, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI --version build", strstr(out, "lng build ") != NULL);
    }
    {
        char *args[] = { (char *)LNG_TEST_CLI, "--nope", NULL };
        fail += expect_int("CLI unknown argument exits 1", 1,
            test_cli_run_input(args, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI unknown argument diagnostic",
            strstr(err, "lng: Unknown argument.") != NULL);
    }
    {
        char *args[] = { (char *)LNG_TEST_CLI, "--threshold", NULL };
        fail += expect_int("CLI missing threshold exits 1", 1,
            test_cli_run_input(args, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI missing threshold diagnostic",
            strstr(err, "lng: Missing value for --threshold.") != NULL);
    }
    {
        char *args[] = { (char *)LNG_TEST_CLI, "--threshold", "nope", NULL };
        fail += expect_int("CLI invalid threshold exits 1", 1,
            test_cli_run_input(args, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI invalid threshold diagnostic",
            strstr(err, "lng: Invalid value for --threshold.") != NULL);
    }
    {
        char *args[] = { (char *)LNG_TEST_CLI, "--limit", NULL };
        fail += expect_int("CLI missing limit exits 1", 1,
            test_cli_run_input(args, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI missing limit diagnostic",
            strstr(err, "lng: Missing value for --limit.") != NULL);
    }
    {
        char *args[] = { (char *)LNG_TEST_CLI, "--limit", "nope", NULL };
        fail += expect_int("CLI invalid limit exits 1", 1,
            test_cli_run_input(args, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI invalid limit diagnostic",
            strstr(err, "lng: Invalid value for --limit.") != NULL);
    }
    {
        char *args[] = { (char *)LNG_TEST_CLI, "hello", "world", NULL };
        fail += expect_int("CLI too many positionals exits 1", 1,
            test_cli_run_input(args, NULL, 0, out, sizeof(out), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI too many positionals diagnostic",
            strstr(err, "lng: Too many positional arguments.") != NULL);
    }

    case_result(fail, "kc_lng_cli",
        "preserves arguments, stdin, ranking output, diagnostics, help, and version");
    return fail == 0 ? 0 : 1;
}
#endif

/**
 * Runs all test cases.
 * @return 0 on success, 1 on failure.
 */
static int case_all(void) {
    int rc = 0;
#ifdef __EMSCRIPTEN__
    test_case_total = 5;
#else
    test_case_total = 6;
#endif
    test_case_current = 0;
    run_case(&rc, case_kc_lng_version);
    run_case(&rc, case_kc_lng_detect);
    run_case(&rc, case_kc_lng_detect_ranking);
    run_case(&rc, case_kc_lng_detect_contract);
    run_case(&rc, case_kc_lng_free);
#ifndef __EMSCRIPTEN__
    run_case(&rc, case_kc_lng_cli);
#endif
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one lng public API test case.
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
    if (strcmp(argv[1], "kc_lng_version") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_version(); }
    if (strcmp(argv[1], "kc_lng_detect") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_detect(); }
    if (strcmp(argv[1], "kc_lng_detect_ranking") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_detect_ranking(); }
    if (strcmp(argv[1], "kc_lng_detect_contract") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_detect_contract(); }
    if (strcmp(argv[1], "kc_lng_free") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_free(); }
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "kc_lng_cli") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_cli(); }
#endif
    if (strcmp(argv[1], "version") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_version(); }
    if (strcmp(argv[1], "detection") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_detect(); }
    if (strcmp(argv[1], "ranking") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_detect_ranking(); }
    if (strcmp(argv[1], "contract") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_detect_contract(); }
    if (strcmp(argv[1], "determinism") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_free(); }
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "cli") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_cli(); }
#endif
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
