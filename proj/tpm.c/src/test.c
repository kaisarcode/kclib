/**
 * test.c - libtpm public API contract tests.
 * Summary: Validates the normalized tpm API and grouped CLI contract.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libtpm.h"

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

#ifndef KC_TPM_TEST_CLI
#define KC_TPM_TEST_CLI ""
#endif

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Canonical test case name.
 * @param detail Behavior verified by the case.
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
 * Verifies an integer result.
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
 * Verifies a true condition.
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
 * Allocates a repeated byte string.
 * @param byte Byte to repeat.
 * @param count Number of bytes.
 * @return Allocated string, or NULL on failure.
 */
static char *repeat_byte(char byte, size_t count) {
    char *text;

    text = (char *)malloc(count + 1);
    if (text == NULL) return NULL;
    memset(text, byte, count);
    text[count] = '\0';
    return text;
}

/**
 * Tests kc_tpm_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_open(void) {
    const char *name = "kc_tpm_open";
    const char *detail = "creates ready profiles with defaults and options";
    kc_tpm_t *tpm = NULL;
    kc_tpm_t *default_tpm = NULL;
    kc_tpm_t *explicit_tpm = NULL;
    kc_tpm_options_t options;
    char *large_text = NULL;
    double score = -1.0;
    double default_score = -1.0;
    double explicit_score = -1.0;
    int fail = 0;

    fail += expect_int("open rejects NULL out", KC_TPM_ERROR,
        kc_tpm_open(NULL, "abc", NULL));
    fail += expect_int("open rejects NULL map", KC_TPM_ERROR,
        kc_tpm_open(&tpm, NULL, NULL));
    fail += expect_true("failed open clears output", tpm == NULL);

    fail += expect_int("open uses default ngram size", KC_TPM_OK,
        kc_tpm_open(&default_tpm, "hello world hello world", NULL));
    fail += expect_true("default open returns profile", default_tpm != NULL);

    memset(&options, 0, sizeof(options));
    fail += expect_int("open accepts empty options", KC_TPM_OK,
        kc_tpm_open(&explicit_tpm, "hello world hello world", &options));
    fail += expect_true("explicit open returns profile", explicit_tpm != NULL);

    if (default_tpm != NULL && explicit_tpm != NULL) {
        fail += expect_int("default profile scores immediately", KC_TPM_OK,
            kc_tpm_score(default_tpm, "hello world", &default_score));
        fail += expect_int("explicit profile scores immediately", KC_TPM_OK,
            kc_tpm_score(explicit_tpm, "hello world", &explicit_score));
        fail += expect_true("NULL options equal omitted ngram option",
            default_score == explicit_score);
    }

    kc_tpm_close(default_tpm);
    kc_tpm_close(explicit_tpm);

    memset(&options, 0, sizeof(options));
    {
        static const int ngram_zero = 0;
        options.ngram_size = &ngram_zero;
    }
    tpm = (kc_tpm_t *)(uintptr_t)1;
    fail += expect_int("open rejects explicit n below range", KC_TPM_ERROR,
        kc_tpm_open(&tpm, "abc", &options));
    fail += expect_true("invalid option clears output", tpm == NULL);

    {
        static const int ngram_nine = 9;
        options.ngram_size = &ngram_nine;
    }
    tpm = (kc_tpm_t *)(uintptr_t)1;
    fail += expect_int("open rejects n above range", KC_TPM_ERROR,
        kc_tpm_open(&tpm, "abc", &options));
    fail += expect_true("invalid high option clears output", tpm == NULL);

    {
        static const int ngram_one = 1;
        options.ngram_size = &ngram_one;
    }
    fail += expect_int("open accepts n=1", KC_TPM_OK,
        kc_tpm_open(&tpm, "abc abc", &options));
    kc_tpm_close(tpm);

    {
        static const int ngram_eight = 8;
        options.ngram_size = &ngram_eight;
    }
    tpm = NULL;
    fail += expect_int("open accepts n=8", KC_TPM_OK,
        kc_tpm_open(&tpm, "abcdefgh abcdefgh", &options));
    kc_tpm_close(tpm);

    {
        static const int ngram_two = 2;
        options.ngram_size = &ngram_two;
    }
    tpm = NULL;
    fail += expect_int("open accepts empty profile", KC_TPM_OK,
        kc_tpm_open(&tpm, "", &options));
    if (tpm != NULL) {
        fail += expect_int("empty profile scores successfully", KC_TPM_OK,
            kc_tpm_score(tpm, "abc", &score));
        fail += expect_true("empty profile scores zero", score == 0.0);
    }
    kc_tpm_close(tpm);

    large_text = repeat_byte('a', 16385);
    fail += expect_true("allocate overflow text", large_text != NULL);
    if (large_text != NULL) {
        {
        static const int ngram_one = 1;
        options.ngram_size = &ngram_one;
    }
        tpm = (kc_tpm_t *)(uintptr_t)1;
        fail += expect_int("open reports raw gram overflow", KC_TPM_ERROR,
            kc_tpm_open(&tpm, large_text, &options));
        fail += expect_true("overflow open returns no profile", tpm == NULL);
    }

    free(large_text);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpm_score.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_score(void) {
    const char *name = "kc_tpm_score";
    const char *detail = "reuses one profile for bounded similarity scoring";
    kc_tpm_t *tpm = NULL;
    kc_tpm_t *normalized_tpm = NULL;
    static const int ngram_three = 3;
    kc_tpm_options_t options = { .ngram_size = &ngram_three };
    char *large_text = NULL;
    double matching_score = -1.0;
    double mismatching_score = -1.0;
    double normalized_score = -1.0;
    double plain_score = -1.0;
    double repeated_score = -1.0;
    double score = -1.0;
    int fail = 0;

    fail += expect_int("score rejects NULL ctx", KC_TPM_ERROR,
        kc_tpm_score(NULL, "abc", &score));

    fail += expect_int("open scoring profile", KC_TPM_OK,
        kc_tpm_open(&tpm, "hello world hello world english text", &options));
    if (tpm == NULL) {
        case_result(1, name, detail);
        return 1;
    }

    fail += expect_int("score rejects NULL input", KC_TPM_ERROR,
        kc_tpm_score(tpm, NULL, &score));
    fail += expect_int("score rejects NULL output", KC_TPM_ERROR,
        kc_tpm_score(tpm, "abc", NULL));

    fail += expect_int("matching score succeeds", KC_TPM_OK,
        kc_tpm_score(tpm, "hello world english text", &matching_score));
    fail += expect_int("mismatching score succeeds", KC_TPM_OK,
        kc_tpm_score(tpm, "zzzz qqqq xxxx yyyy", &mismatching_score));
    fail += expect_true("matching score is in range",
        matching_score >= 0.0 && matching_score <= 1.0);
    fail += expect_true("mismatching score is in range",
        mismatching_score >= 0.0 && mismatching_score <= 1.0);
    fail += expect_true("score ranks matching text higher",
        matching_score > mismatching_score);

    fail += expect_int("repeated score succeeds", KC_TPM_OK,
        kc_tpm_score(tpm, "hello world english text", &repeated_score));
    fail += expect_true("profile is reusable",
        repeated_score == matching_score);

    fail += expect_int("empty input succeeds", KC_TPM_OK,
        kc_tpm_score(tpm, "", &score));
    fail += expect_true("empty input scores zero", score == 0.0);

    fail += expect_int("open normalization profile", KC_TPM_OK,
        kc_tpm_open(&normalized_tpm,
            "  Hello\tWORLD\nhello   world  ", &options));
    if (normalized_tpm != NULL) {
        fail += expect_int("normalized score succeeds", KC_TPM_OK,
            kc_tpm_score(normalized_tpm, "hello world", &normalized_score));
        fail += expect_int("plain score succeeds", KC_TPM_OK,
            kc_tpm_score(normalized_tpm, "HELLO\tWORLD", &plain_score));
        fail += expect_true("score normalizes case and whitespace",
            plain_score == normalized_score);
    }

    {
        static const int ngram_one = 1;
        options.ngram_size = &ngram_one;
    }
    kc_tpm_close(tpm);
    tpm = NULL;
    fail += expect_int("open n=1 overflow profile", KC_TPM_OK,
        kc_tpm_open(&tpm, "aaaa", &options));
    large_text = repeat_byte('a', 16385);
    fail += expect_true("allocate score overflow text", large_text != NULL);
    if (tpm != NULL && large_text != NULL) {
        fail += expect_int("score reports raw gram overflow", KC_TPM_ERROR,
            kc_tpm_score(tpm, large_text, &score));
    }

    free(large_text);
    kc_tpm_close(normalized_tpm);
    kc_tpm_close(tpm);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpm_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_close(void) {
    const char *name = "kc_tpm_close";
    const char *detail = "releases context and accepts NULL";
    kc_tpm_t *tpm = NULL;
    int fail = 0;

    kc_tpm_close(NULL);
    fail += expect_int("open profile for close", KC_TPM_OK,
        kc_tpm_open(&tpm, "abc", NULL));
    kc_tpm_close(tpm);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpm_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_version(void) {
    const char *name = "kc_tpm_version";
    const char *detail = "returns a nonzero generated build version";
    int fail = expect_true("version returns nonzero", kc_tpm_version() != 0U);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

#ifndef __EMSCRIPTEN__

/**
 * Writes a temporary map file.
 * @param text File contents.
 * @param path Destination path buffer.
 * @param path_size Destination capacity.
 * @return 0 on success, 1 on failure.
 */
static int test_write_map(const char *text, char *path, size_t path_size) {
#ifdef _WIN32
    char dir[MAX_PATH];
    char tmp[MAX_PATH];
    FILE *file;

    if (GetTempPathA(MAX_PATH, dir) == 0) return 1;
    if (GetTempFileNameA(dir, "tpm", 0, tmp) == 0) return 1;
    if (strlen(tmp) + 1 > path_size) {
        DeleteFileA(tmp);
        return 1;
    }
    strcpy(path, tmp);
    file = fopen(path, "wb");
    if (file == NULL) {
        DeleteFileA(path);
        return 1;
    }
#else
    char pattern[] = "/tmp/tpm-test-XXXXXX";
    int fd;
    FILE *file;

    fd = mkstemp(pattern);
    if (fd < 0) return 1;
    if (strlen(pattern) + 1 > path_size) {
        close(fd);
        unlink(pattern);
        return 1;
    }
    strcpy(path, pattern);
    file = fdopen(fd, "wb");
    if (file == NULL) {
        close(fd);
        unlink(path);
        return 1;
    }
#endif

    if (text != NULL && fwrite(text, 1, strlen(text), file) != strlen(text)) {
        fclose(file);
#ifdef _WIN32
        DeleteFileA(path);
#else
        unlink(path);
#endif
        return 1;
    }
    if (fclose(file) != 0) {
#ifdef _WIN32
        DeleteFileA(path);
#else
        unlink(path);
#endif
        return 1;
    }
    return 0;
}

/**
 * Removes a temporary map file.
 * @param path File path.
 * @return None.
 */
static void test_remove_map(const char *path) {
#ifdef _WIN32
    DeleteFileA(path);
#else
    unlink(path);
#endif
}

/**
 * Checks that output is exactly one score line with six decimals.
 * @param text Captured stdout.
 * @return Non-zero when valid.
 */
static int test_is_score_line(const char *text) {
    const char *p = text;
    int before = 0;
    int after = 0;

    while (*p >= '0' && *p <= '9') {
        before++;
        p++;
    }
    if (before == 0 || *p++ != '.') return 0;
    while (*p >= '0' && *p <= '9') {
        after++;
        p++;
    }
    if (after != 6) return 0;
    if (*p == '\r') p++;
    if (*p++ != '\n') return 0;
    return *p == '\0';
}

#ifdef _WIN32
/**
 * Converts a Unix-style Wine path to a Win32 Z: path when needed.
 * @param input Input path.
 * @param output Output buffer.
 * @param output_size Output capacity.
 * @return 0 on success, 1 on failure.
 */
static int test_win_path(const char *input, char *output, size_t output_size) {
    size_t i;
    size_t offset = 0;

    if (input[0] == '/') {
        if (output_size < 4) return 1;
        output[0] = 'Z';
        output[1] = ':';
        offset = 2;
    }
    if (strlen(input) + offset + 1 > output_size) return 1;
    for (i = 0; input[i] != '\0'; i++) {
        output[offset + i] = input[i] == '/' ? '\\' : input[i];
    }
    output[offset + i] = '\0';
    return 0;
}

/**
 * Appends one quoted argument to a Win32 command line.
 * @param command Destination command line.
 * @param capacity Destination capacity.
 * @param arg Argument.
 * @return 0 on success, 1 on failure.
 */
static int test_win_append_arg(char *command, size_t capacity, const char *arg) {
    size_t used = strlen(command);
    size_t i;

    if (used != 0) {
        if (used + 1 >= capacity) return 1;
        command[used++] = ' ';
    }
    if (used + 1 >= capacity) return 1;
    command[used++] = '"';
    for (i = 0; arg[i] != '\0'; i++) {
        if (arg[i] == '"') {
            if (used + 2 >= capacity) return 1;
            command[used++] = '\\';
        }
        if (used + 1 >= capacity) return 1;
        command[used++] = arg[i];
    }
    if (used + 2 > capacity) return 1;
    command[used++] = '"';
    command[used] = '\0';
    return 0;
}

/**
 * Reads a Win32 pipe into a NUL-terminated buffer.
 * @param pipe Read handle.
 * @param buffer Destination.
 * @param size Destination capacity.
 * @return None.
 */
static void test_win_read_pipe(HANDLE pipe, char *buffer, size_t size) {
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

/**
 * Runs the tpm CLI with captured stdin/stdout/stderr.
 * @param argv NULL-terminated argument vector.
 * @param input Stdin bytes.
 * @param input_len Stdin byte count.
 * @param out Captured stdout.
 * @param out_size Stdout capacity.
 * @param err Captured stderr.
 * @param err_size Stderr capacity.
 * @param out_status Exit status destination.
 * @return 0 on execution success, 1 on harness failure.
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
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE in_pipe[2];
    HANDLE out_pipe[2];
    HANDLE err_pipe[2];
    DWORD written;
    DWORD exit_code;
    char exe[4096];
    char command[32768];
    int i;

    if (test_win_path(KC_TPM_TEST_CLI, exe, sizeof(exe))) return 1;

    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&in_pipe[0], &in_pipe[1], &sa, 0)) return 1;
    if (!CreatePipe(&out_pipe[0], &out_pipe[1], &sa, 0)) return 1;
    if (!CreatePipe(&err_pipe[0], &err_pipe[1], &sa, 0)) return 1;
    SetHandleInformation(in_pipe[1], HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_pipe[0], HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_pipe[0], HANDLE_FLAG_INHERIT, 0);

    command[0] = '\0';
    if (test_win_append_arg(command, sizeof(command), exe)) return 1;
    for (i = 1; argv[i] != NULL; i++) {
        if (test_win_append_arg(command, sizeof(command), argv[i])) return 1;
    }

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_pipe[0];
    si.hStdOutput = out_pipe[1];
    si.hStdError = err_pipe[1];
    memset(&pi, 0, sizeof(pi));

    if (!CreateProcessA(exe, command, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
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

    test_win_read_pipe(out_pipe[0], out, out_size);
    test_win_read_pipe(err_pipe[0], err, err_size);
    CloseHandle(out_pipe[0]);
    CloseHandle(err_pipe[0]);

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    *out_status = (int)exit_code;
    return 0;
#else
    int in_pipe[2];
    int out_pipe[2];
    int err_pipe[2];
    pid_t pid;
    ssize_t count;
    size_t written = 0;
    size_t pos = 0;
    int status;

    if (pipe(in_pipe) != 0) return 1;
    if (pipe(out_pipe) != 0) return 1;
    if (pipe(err_pipe) != 0) return 1;

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
        if (count <= 0) break;
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
#endif
}

/**
 * Tests the complete shipped tpm CLI contract as one grouped case.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_cli(void) {
    const char *name = "kc_tpm_cli";
    const char *detail = "help, version, arguments, stdin, scoring, and diagnostics";
    char map_path[4096] = "";
    char empty_map_path[4096] = "";
    char missing_path[4096] = "";
    char out[4096];
    char out_alt[4096];
    char err[4096];
    int status = 0;
    int fail = 0;

    if (KC_TPM_TEST_CLI[0] == '\0') {
        case_result(0, name, detail);
        return 0;
    }

    fail += expect_int("create normal map", 0,
        test_write_map("hello world hello world english text\n", map_path,
            sizeof(map_path)));
    fail += expect_int("create empty map", 0,
        test_write_map("", empty_map_path, sizeof(empty_map_path)));

#ifdef _WIN32
    snprintf(missing_path, sizeof(missing_path), "%s.missing", map_path);
#else
    snprintf(missing_path, sizeof(missing_path), "%s.missing", map_path);
#endif

    if (fail == 0) {
        char *help[] = { (char *)KC_TPM_TEST_CLI, "-h", NULL };
        char *long_help[] = { (char *)KC_TPM_TEST_CLI, "--help", NULL };
        char *version[] = { (char *)KC_TPM_TEST_CLI, "-v", NULL };
        char *long_version[] = { (char *)KC_TPM_TEST_CLI, "--version", NULL };
        char *missing_map[] = { (char *)KC_TPM_TEST_CLI, NULL };
        char *unknown[] = { (char *)KC_TPM_TEST_CLI, "--nope", NULL };
        char *missing_n[] = { (char *)KC_TPM_TEST_CLI, "-n", NULL };
        char *bad_n[] = { (char *)KC_TPM_TEST_CLI, "-n", "x", map_path, NULL };
        char *low_n[] = { (char *)KC_TPM_TEST_CLI, "-n", "0", map_path, NULL };
        char *high_n[] = { (char *)KC_TPM_TEST_CLI, "-n", "9", map_path, NULL };
        char *too_many[] = { (char *)KC_TPM_TEST_CLI, map_path, "extra", NULL };
        char *unreadable[] = { (char *)KC_TPM_TEST_CLI, missing_path, NULL };
        char *empty_map[] = { (char *)KC_TPM_TEST_CLI, empty_map_path, NULL };
        char *normal[] = { (char *)KC_TPM_TEST_CLI, map_path, NULL };
        char *before_n[] = {
            (char *)KC_TPM_TEST_CLI, "-n", "4", map_path, NULL
        };
        char *after_n[] = {
            (char *)KC_TPM_TEST_CLI, map_path, "-n", "4", NULL
        };
        char *n_one[] = {
            (char *)KC_TPM_TEST_CLI, "-n", "1", map_path, NULL
        };

        fail += expect_int("CLI -h exits 0", 0,
            test_cli_run_input(help, NULL, 0, out, sizeof(out), err,
                sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI -h prints usage", strstr(out, "Usage: tpm") != NULL);

        fail += expect_int("CLI --help exits 0", 0,
            test_cli_run_input(long_help, NULL, 0, out, sizeof(out), err,
                sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI --help prints usage", strstr(out, "Usage: tpm") != NULL);

        fail += expect_int("CLI -v exits 0", 0,
            test_cli_run_input(version, NULL, 0, out, sizeof(out), err,
                sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI -v prints build", strstr(out, "tpm build ") != NULL);

        fail += expect_int("CLI --version exits 0", 0,
            test_cli_run_input(long_version, NULL, 0, out, sizeof(out), err,
                sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI --version prints build",
            strstr(out, "tpm build ") != NULL);

        fail += expect_int("CLI missing map exits 1", 1,
            test_cli_run_input(missing_map, NULL, 0, out, sizeof(out), err,
                sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI missing map diagnostic",
            strstr(err, "missing map file") != NULL);

        fail += expect_int("CLI unknown option exits 1", 1,
            test_cli_run_input(unknown, NULL, 0, out, sizeof(out), err,
                sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI unknown option diagnostic",
            strstr(err, "unknown option") != NULL);

        fail += expect_int("CLI -n missing value exits 1", 1,
            test_cli_run_input(missing_n, NULL, 0, out, sizeof(out), err,
                sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI -n missing value diagnostic",
            strstr(err, "missing value for -n") != NULL);

        fail += expect_int("CLI -n nonnumeric exits 1", 1,
            test_cli_run_input(bad_n, NULL, 0, out, sizeof(out), err,
                sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI -n nonnumeric diagnostic",
            strstr(err, "invalid n-gram size") != NULL);

        fail += expect_int("CLI -n zero exits 1", 1,
            test_cli_run_input(low_n, NULL, 0, out, sizeof(out), err,
                sizeof(err), &status) ? 1 : status);
        fail += expect_int("CLI -n nine exits 1", 1,
            test_cli_run_input(high_n, NULL, 0, out, sizeof(out), err,
                sizeof(err), &status) ? 1 : status);

        fail += expect_int("CLI too many args exits 1", 1,
            test_cli_run_input(too_many, NULL, 0, out, sizeof(out), err,
                sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI too many args diagnostic",
            strstr(err, "too many arguments") != NULL);

        fail += expect_int("CLI unreadable map exits 1", 1,
            test_cli_run_input(unreadable, "hello", 5, out, sizeof(out), err,
                sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI unreadable map diagnostic",
            strstr(err, "failed to read map file") != NULL);

        fail += expect_int("CLI empty map exits 1", 1,
            test_cli_run_input(empty_map, "hello", 5, out, sizeof(out), err,
                sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI empty map profile diagnostic",
            strstr(err, "profile creation failed") != NULL);

        fail += expect_int("CLI empty stdin exits 0", 0,
            test_cli_run_input(normal, NULL, 0, out, sizeof(out), err,
                sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI empty stdin prints zero",
            strcmp(out, "0.000000\n") == 0 || strcmp(out, "0.000000\r\n") == 0);

        fail += expect_int("CLI normal score exits 0", 0,
            test_cli_run_input(normal, "hello world", 11, out, sizeof(out), err,
                sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI normal score is six decimals",
            test_is_score_line(out));
        fail += expect_true("CLI normal score has no stderr", err[0] == '\0');

        fail += expect_int("CLI -n before map exits 0", 0,
            test_cli_run_input(before_n, "zzzz qqqq", 9, out, sizeof(out), err,
                sizeof(err), &status) ? 1 : status);
        fail += expect_int("CLI -n after map exits 0", 0,
            test_cli_run_input(after_n, "zzzz qqqq", 9, out_alt,
                sizeof(out_alt), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI argument order preserves score",
            strcmp(out, out_alt) == 0);

        fail += expect_int("CLI n=1 exits 0", 0,
            test_cli_run_input(n_one, "zzzz qqqq", 9, out_alt,
                sizeof(out_alt), err, sizeof(err), &status) ? 1 : status);
        fail += expect_true("CLI n option affects scoring",
            strcmp(out, out_alt) != 0);
    }

    if (map_path[0] != '\0') test_remove_map(map_path);
    if (empty_map_path[0] != '\0') test_remove_map(empty_map_path);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}
#endif

/**
 * Runs all test cases in a single process.
 * @return 0 on success, nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;

#ifdef __EMSCRIPTEN__
    test_case_total = 4;
#else
    int cli_enabled = KC_TPM_TEST_CLI[0] != '\0';
    test_case_total = cli_enabled ? 5 : 4;
#endif

    test_case_current = 0;
    run_case(&rc, case_kc_tpm_open);
    run_case(&rc, case_kc_tpm_score);
    run_case(&rc, case_kc_tpm_close);
    run_case(&rc, case_kc_tpm_version);
#ifndef __EMSCRIPTEN__
    if (cli_enabled) {
        run_case(&rc, case_kc_tpm_cli);
    }
#endif
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one libtpm contract test case.
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
    if (strcmp(argv[1], "kc_tpm_open") == 0) return case_kc_tpm_open();
    if (strcmp(argv[1], "kc_tpm_score") == 0) return case_kc_tpm_score();
    if (strcmp(argv[1], "kc_tpm_close") == 0) return case_kc_tpm_close();
    if (strcmp(argv[1], "kc_tpm_version") == 0) return case_kc_tpm_version();
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "kc_tpm_cli") == 0) return case_kc_tpm_cli();
#endif
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
