/**
 * test.c - libhnsw public API and CLI tests.
 * Summary: Contract tests for the in-memory approximate-neighbor index.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libhnsw.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __EMSCRIPTEN__
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#endif

#ifndef KC_HNSW_TEST_CLI
#define KC_HNSW_TEST_CLI ""
#endif

typedef int (*case_fn)(void);

static int test_case_total = 0;
static int test_case_current = 0;

static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

static int expect(int condition) {
    return condition ? 0 : 1;
}

static int run_case(case_fn fn) {
    test_case_current++;
    return fn();
}

static kc_hnsw_t *open_index(size_t dimension, int metric) {
    kc_hnsw_options_t options;
    kc_hnsw_t *hnsw = NULL;

    memset(&options, 0, sizeof(options));
    options.dimension = dimension;
    options.metric = metric;
    if (kc_hnsw_open(&hnsw, &options) != KC_HNSW_OK) return NULL;
    return hnsw;
}

static int case_kc_hnsw_open(void) {
    const char *name = "kc_hnsw_open";
    const char *detail = "applies library defaults and validates required options";
    kc_hnsw_options_t defaults = kc_hnsw_options_default();
    kc_hnsw_options_t sparse;
    kc_hnsw_t *hnsw = (kc_hnsw_t *)1;
    int fail = 0;

    fail |= expect(defaults.dimension == 0);
    fail |= expect(defaults.metric == KC_HNSW_METRIC_COSINE);
    fail |= expect(defaults.max_connections == 16);
    fail |= expect(defaults.build_effort == 64);
    fail |= expect(defaults.search_effort == 64);

    memset(&sparse, 0, sizeof(sparse));
    sparse.dimension = 2;
    fail |= expect(kc_hnsw_open(&hnsw, &sparse) == KC_HNSW_OK);
    fail |= expect(hnsw != NULL);
    fail |= expect(kc_hnsw_dimension(hnsw) == 2);
    fail |= expect(kc_hnsw_metric(hnsw) == KC_HNSW_METRIC_COSINE);
    kc_hnsw_close(hnsw);

    hnsw = (kc_hnsw_t *)1;
    memset(&sparse, 0, sizeof(sparse));
    fail |= expect(kc_hnsw_open(&hnsw, &sparse) == KC_HNSW_EINVAL);
    fail |= expect(hnsw == NULL);

    sparse.dimension = 2;
    sparse.metric = 99;
    fail |= expect(kc_hnsw_open(&hnsw, &sparse) == KC_HNSW_EINVAL);
    fail |= expect(hnsw == NULL);
    fail |= expect(kc_hnsw_open(NULL, &sparse) == KC_HNSW_EINVAL);
    fail |= expect(kc_hnsw_open(&hnsw, NULL) == KC_HNSW_EINVAL);
    kc_hnsw_close(NULL);

    case_result(fail, name, detail);
    return fail;
}

static int case_kc_hnsw_add_build(void) {
    const char *name = "kc_hnsw_add_build";
    const char *detail = "owns vectors and requires rebuild after mutation";
    const float a[] = {1.0f, 0.0f};
    const float b[] = {0.0f, 1.0f};
    const float c[] = {1.0f, 1.0f};
    kc_hnsw_result_t *results = (kc_hnsw_result_t *)1;
    kc_hnsw_t *hnsw = open_index(2, 0);
    size_t count = 99;
    int fail = 0;

    if (hnsw == NULL) {
        case_result(1, name, detail);
        return 1;
    }

    fail |= expect(kc_hnsw_count(hnsw) == 0);
    fail |= expect(kc_hnsw_add(hnsw, "a", a) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(hnsw, "b", b) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_count(hnsw) == 2);
    fail |= expect(kc_hnsw_add(hnsw, "", a) == KC_HNSW_EINVAL);
    fail |= expect(kc_hnsw_add(hnsw, NULL, a) == KC_HNSW_EINVAL);
    fail |= expect(kc_hnsw_add(hnsw, "bad", NULL) == KC_HNSW_EINVAL);

    fail |= expect(kc_hnsw_search(
        hnsw, a, 1, -1.0, &results, &count) == KC_HNSW_ESTATE);
    fail |= expect(results == NULL && count == 0);

    fail |= expect(kc_hnsw_build(hnsw) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_search(
        hnsw, a, 1, -1.0, &results, &count) == KC_HNSW_OK);
    fail |= expect(results != NULL && count == 1);
    if (results != NULL) fail |= expect(strcmp(results[0].id, "a") == 0);
    kc_hnsw_free(results);

    fail |= expect(kc_hnsw_add(hnsw, "c", c) == KC_HNSW_OK);
    results = (kc_hnsw_result_t *)1;
    count = 99;
    fail |= expect(kc_hnsw_search(
        hnsw, a, 1, -1.0, &results, &count) == KC_HNSW_ESTATE);
    fail |= expect(results == NULL && count == 0);
    fail |= expect(kc_hnsw_build(hnsw) == KC_HNSW_OK);

    kc_hnsw_close(hnsw);
    case_result(fail, name, detail);
    return fail;
}

static int case_kc_hnsw_search(void) {
    const char *name = "kc_hnsw_search";
    const char *detail = "supports cosine, inner product, L2, top-K, and thresholds";
    const float x[] = {1.0f, 0.0f};
    const float y[] = {0.0f, 1.0f};
    const float xy[] = {1.0f, 1.0f};
    const float origin[] = {0.0f, 0.0f};
    const float unit[] = {1.0f, 0.0f};
    const float distant[] = {4.0f, 0.0f};
    kc_hnsw_result_t *results = NULL;
    kc_hnsw_t *hnsw;
    size_t count = 0;
    int fail = 0;

    hnsw = open_index(2, KC_HNSW_METRIC_COSINE);
    if (hnsw == NULL) return 1;
    fail |= expect(kc_hnsw_add(hnsw, "x", x) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(hnsw, "y", y) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(hnsw, "xy", xy) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_build(hnsw) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_search(
        hnsw, x, 3, 0.8, &results, &count) == KC_HNSW_OK);
    fail |= expect(results != NULL && count == 1);
    if (results != NULL) {
        fail |= expect(strcmp(results[0].id, "x") == 0);
        fail |= expect(fabs(results[0].score - 1.0) < 0.000001);
    }
    kc_hnsw_free(results);
    kc_hnsw_close(hnsw);

    hnsw = open_index(2, KC_HNSW_METRIC_INNER_PRODUCT);
    if (hnsw == NULL) return 1;
    fail |= expect(kc_hnsw_add(hnsw, "x", x) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(hnsw, "far", distant) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_build(hnsw) == KC_HNSW_OK);
    results = NULL;
    count = 0;
    fail |= expect(kc_hnsw_search(
        hnsw, x, 2, 2.0, &results, &count) == KC_HNSW_OK);
    fail |= expect(results != NULL && count == 1);
    if (results != NULL) {
        fail |= expect(strcmp(results[0].id, "far") == 0);
        fail |= expect(fabs(results[0].score - 4.0) < 0.000001);
    }
    kc_hnsw_free(results);
    kc_hnsw_close(hnsw);

    hnsw = open_index(2, KC_HNSW_METRIC_L2);
    if (hnsw == NULL) return 1;
    fail |= expect(kc_hnsw_add(hnsw, "origin", origin) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(hnsw, "unit", unit) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(hnsw, "far", distant) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_build(hnsw) == KC_HNSW_OK);
    results = NULL;
    count = 0;
    fail |= expect(kc_hnsw_search(
        hnsw, origin, 3, 1.0, &results, &count) == KC_HNSW_OK);
    fail |= expect(results != NULL && count == 2);
    if (results != NULL && count == 2) {
        fail |= expect(strcmp(results[0].id, "origin") == 0);
        fail |= expect(strcmp(results[1].id, "unit") == 0);
    }
    kc_hnsw_free(results);
    kc_hnsw_close(hnsw);

    case_result(fail, name, detail);
    return fail;
}

static int case_kc_hnsw_contract(void) {
    const char *name = "kc_hnsw_contract";
    const char *detail = "clears outputs and handles empty indexes and invalid arguments";
    const float q[] = {1.0f, 0.0f};
    kc_hnsw_result_t *results = (kc_hnsw_result_t *)1;
    kc_hnsw_t *hnsw = open_index(2, 0);
    size_t count = 99;
    int fail = 0;

    if (hnsw == NULL) return 1;

    fail |= expect(kc_hnsw_build(hnsw) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_search(
        hnsw, q, 5, -1.0, &results, &count) == KC_HNSW_OK);
    fail |= expect(results == NULL && count == 0);

    results = (kc_hnsw_result_t *)1;
    count = 99;
    fail |= expect(kc_hnsw_search(
        hnsw, NULL, 1, -1.0, &results, &count) == KC_HNSW_EINVAL);
    fail |= expect(results == NULL && count == 0);

    count = 99;
    fail |= expect(kc_hnsw_search(
        hnsw, q, 1, -1.0, NULL, &count) == KC_HNSW_EINVAL);
    fail |= expect(count == 0);

    results = (kc_hnsw_result_t *)1;
    fail |= expect(kc_hnsw_search(
        hnsw, q, 1, -1.0, &results, NULL) == KC_HNSW_EINVAL);
    fail |= expect(results == NULL);

    results = (kc_hnsw_result_t *)1;
    count = 99;
    fail |= expect(kc_hnsw_search(
        hnsw, q, 0, -1.0, &results, &count) == KC_HNSW_OK);
    fail |= expect(results == NULL && count == 0);

    fail |= expect(kc_hnsw_dimension(NULL) == 0);
    fail |= expect(kc_hnsw_metric(NULL) == 0);
    fail |= expect(kc_hnsw_count(NULL) == 0);
    fail |= expect(strcmp(kc_hnsw_strerror(KC_HNSW_OK), "ok") == 0);
    fail |= expect(strcmp(kc_hnsw_strerror(KC_HNSW_ESTATE),
        "invalid state") == 0);
    kc_hnsw_free(NULL);
    kc_hnsw_close(hnsw);

    case_result(fail, name, detail);
    return fail;
}

#ifndef __EMSCRIPTEN__
typedef struct {
    const kc_hnsw_t *hnsw;
    const float *query;
    kc_hnsw_result_t *results;
    size_t count;
    int rc;
} search_worker_t;

#ifdef _WIN32
static DWORD WINAPI search_worker_main(void *arg) {
#else
static void *search_worker_main(void *arg) {
#endif
    search_worker_t *worker = (search_worker_t *)arg;
    worker->rc = kc_hnsw_search(worker->hnsw, worker->query, 1, -1.0,
        &worker->results, &worker->count);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static int case_kc_hnsw_concurrency(void) {
    const char *name = "kc_hnsw_concurrency";
    const char *detail = "supports concurrent searches after build";
    const float x[] = {1.0f, 0.0f};
    const float y[] = {0.0f, 1.0f};
    search_worker_t workers[2];
    kc_hnsw_t *hnsw = open_index(2, 0);
    int fail = 0;

#ifdef _WIN32
    HANDLE threads[2];
#else
    pthread_t threads[2];
#endif

    if (hnsw == NULL) return 1;
    fail |= expect(kc_hnsw_add(hnsw, "x", x) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_add(hnsw, "y", y) == KC_HNSW_OK);
    fail |= expect(kc_hnsw_build(hnsw) == KC_HNSW_OK);

    memset(workers, 0, sizeof(workers));
    workers[0].hnsw = hnsw;
    workers[0].query = x;
    workers[1].hnsw = hnsw;
    workers[1].query = y;

#ifdef _WIN32
    threads[0] = CreateThread(NULL, 0, search_worker_main, &workers[0], 0, NULL);
    threads[1] = CreateThread(NULL, 0, search_worker_main, &workers[1], 0, NULL);
    fail |= expect(threads[0] != NULL && threads[1] != NULL);
    if (threads[0]) {
        fail |= expect(WaitForSingleObject(threads[0], INFINITE) == WAIT_OBJECT_0);
        CloseHandle(threads[0]);
    }
    if (threads[1]) {
        fail |= expect(WaitForSingleObject(threads[1], INFINITE) == WAIT_OBJECT_0);
        CloseHandle(threads[1]);
    }
#else
    fail |= expect(pthread_create(&threads[0], NULL,
        search_worker_main, &workers[0]) == 0);
    fail |= expect(pthread_create(&threads[1], NULL,
        search_worker_main, &workers[1]) == 0);
    fail |= expect(pthread_join(threads[0], NULL) == 0);
    fail |= expect(pthread_join(threads[1], NULL) == 0);
#endif

    fail |= expect(workers[0].rc == KC_HNSW_OK);
    fail |= expect(workers[1].rc == KC_HNSW_OK);
    fail |= expect(workers[0].count == 1 && workers[0].results != NULL);
    fail |= expect(workers[1].count == 1 && workers[1].results != NULL);
    kc_hnsw_free(workers[0].results);
    kc_hnsw_free(workers[1].results);
    kc_hnsw_close(hnsw);

    case_result(fail, name, detail);
    return fail;
}

#ifdef _WIN32
static int cli_to_wide(const char *in, wchar_t *out, size_t cap) {
    return MultiByteToWideChar(CP_UTF8, 0, in, -1, out, (int)cap) > 0 ? 0 : 1;
}

static int cli_append_arg(wchar_t *cmd, size_t cap, const wchar_t *arg) {
    size_t used = wcslen(cmd);
    size_t len = wcslen(arg);
    size_t i;
    int quote = len == 0 || wcschr(arg, L' ') != NULL ||
        wcschr(arg, L'\t') != NULL || wcschr(arg, L'"') != NULL;

    if (used != 0) cmd[used++] = L' ';
    if (quote) cmd[used++] = L'"';
    for (i = 0; i < len && used + 2 < cap; i++) {
        if (arg[i] == L'"') cmd[used++] = L'\\';
        cmd[used++] = arg[i];
    }
    if (quote) cmd[used++] = L'"';
    if (used >= cap) return 1;
    cmd[used] = L'\0';
    return 0;
}

static void cli_read_pipe(HANDLE pipe, char *buf, size_t size) {
    DWORD got;
    size_t used = 0;

    while (used + 1 < size &&
            ReadFile(pipe, buf + used, (DWORD)(size - used - 1), &got, NULL) &&
            got > 0) {
        used += got;
    }
    buf[used] = '\0';
}

static int cli_run(char *const argv[], const char *input,
        char *out, size_t out_size, char *err, size_t err_size, int *status) {
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    HANDLE in_pipe[2], out_pipe[2], err_pipe[2];
    wchar_t exe[MAX_PATH], cmd[32768], wide[4096];
    DWORD exit_code, written;
    int i;

    if (cli_to_wide(KC_HNSW_TEST_CLI, exe, MAX_PATH)) return 1;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;
    if (!CreatePipe(&in_pipe[0], &in_pipe[1], &sa, 0) ||
            !CreatePipe(&out_pipe[0], &out_pipe[1], &sa, 0) ||
            !CreatePipe(&err_pipe[0], &err_pipe[1], &sa, 0)) return 1;
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
    if (cli_append_arg(cmd, 32768, exe)) return 1;
    for (i = 1; argv[i]; i++) {
        if (cli_to_wide(argv[i], wide, 4096) ||
                cli_append_arg(cmd, 32768, wide)) return 1;
    }

    if (!CreateProcessW(exe, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        return 1;
    }
    CloseHandle(in_pipe[0]);
    CloseHandle(out_pipe[1]);
    CloseHandle(err_pipe[1]);
    if (input && input[0]) {
        (void)WriteFile(in_pipe[1], input, (DWORD)strlen(input), &written, NULL);
    }
    CloseHandle(in_pipe[1]);
    cli_read_pipe(out_pipe[0], out, out_size);
    cli_read_pipe(err_pipe[0], err, err_size);
    CloseHandle(out_pipe[0]);
    CloseHandle(err_pipe[0]);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    *status = (int)exit_code;
    return 0;
}

static int make_dataset(char *path, size_t size) {
    char dir[MAX_PATH];
    char tmp[MAX_PATH];
    FILE *file;

    if (!GetTempPathA(MAX_PATH, dir) ||
            !GetTempFileNameA(dir, "hns", 0, tmp)) return 1;
    if (strlen(tmp) + 1 > size) return 1;
    memcpy(path, tmp, strlen(tmp) + 1);
    file = fopen(path, "wb");
    if (!file) return 1;
    fputs("x 1 0\ny 0 1\nxy 1 1\n", file);
    fclose(file);
    return 0;
}
#else
static int cli_run(char *const argv[], const char *input,
        char *out, size_t out_size, char *err, size_t err_size, int *status) {
    int in_pipe[2], out_pipe[2], err_pipe[2];
    pid_t pid;
    ssize_t got;
    size_t pos;
    int wait_status;

    if (pipe(in_pipe) || pipe(out_pipe) || pipe(err_pipe)) return 1;
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
    if (input && input[0]) {
        size_t len = strlen(input);
        size_t written = 0;
        while (written < len &&
                (got = write(in_pipe[1], input + written, len - written)) > 0) {
            written += (size_t)got;
        }
    }
    close(in_pipe[1]);

    pos = 0;
    while (pos + 1 < out_size &&
            (got = read(out_pipe[0], out + pos, out_size - pos - 1)) > 0) {
        pos += (size_t)got;
    }
    out[pos] = '\0';
    close(out_pipe[0]);

    pos = 0;
    while (pos + 1 < err_size &&
            (got = read(err_pipe[0], err + pos, err_size - pos - 1)) > 0) {
        pos += (size_t)got;
    }
    err[pos] = '\0';
    close(err_pipe[0]);

    if (waitpid(pid, &wait_status, 0) < 0) return 1;
    *status = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : 1;
    return 0;
}

static int make_dataset(char *path, size_t size) {
    char tmp[] = "/tmp/hnsw-test-XXXXXX";
    int fd;
    FILE *file;

    if (sizeof(tmp) > size) return 1;
    fd = mkstemp(tmp);
    if (fd < 0) return 1;
    file = fdopen(fd, "w");
    if (!file) {
        close(fd);
        unlink(tmp);
        return 1;
    }
    fputs("x 1 0\ny 0 1\nxy 1 1\n", file);
    fclose(file);
    memcpy(path, tmp, sizeof(tmp));
    return 0;
}
#endif

static int case_kc_hnsw_cli(void) {
    const char *name = "kc_hnsw_cli";
    const char *detail = "preserves dataset, query, tuning, help, and version CLI behavior";
    char dataset[4096];
    char out[16384];
    char err[8192];
    int status = 0;
    int fail = 0;

    if (KC_HNSW_TEST_CLI[0] == '\0') {
        case_result(0, name, detail);
        return 0;
    }
    if (make_dataset(dataset, sizeof(dataset))) {
        case_result(1, name, detail);
        return 1;
    }

    {
        char *args[] = {
            (char *)KC_HNSW_TEST_CLI,
            "--dim", "2",
            "--input", dataset,
            "--query", "1 0",
            NULL
        };
        fail |= expect(cli_run(args, NULL, out, sizeof(out),
            err, sizeof(err), &status) == 0);
        fail |= expect(status == 0);
        fail |= expect(strstr(out, "x: 1.000000") != NULL);
        fail |= expect(err[0] == '\0');
    }
    {
        char *args[] = {
            (char *)KC_HNSW_TEST_CLI,
            "-d", "2",
            "-i", dataset,
            "-m", "l2",
            "-k", "1",
            "--max-conn", "8",
            "--build-effort", "32",
            "--search-effort", "32",
            NULL
        };
        fail |= expect(cli_run(args, "1 0\n", out, sizeof(out),
            err, sizeof(err), &status) == 0);
        fail |= expect(status == 0);
        fail |= expect(strcmp(out, "x: 0.000000\n") == 0 ||
            strcmp(out, "x: 0.000000\r\n") == 0);
        fail |= expect(err[0] == '\0');
    }
    {
        char *args[] = {(char *)KC_HNSW_TEST_CLI, "--help", NULL};
        fail |= expect(cli_run(args, NULL, out, sizeof(out),
            err, sizeof(err), &status) == 0);
        fail |= expect(status == 0 && strstr(out, "Usage:") != NULL);
    }
    {
        char *args[] = {(char *)KC_HNSW_TEST_CLI, "--version", NULL};
        fail |= expect(cli_run(args, NULL, out, sizeof(out),
            err, sizeof(err), &status) == 0);
        fail |= expect(status == 0 && strstr(out, "hnsw build ") != NULL);
    }
    {
        char *args[] = {
            (char *)KC_HNSW_TEST_CLI,
            "--dim", "3",
            "--input", dataset,
            "--query", "1 0 0",
            NULL
        };
        fail |= expect(cli_run(args, NULL, out, sizeof(out),
            err, sizeof(err), &status) == 0);
        fail |= expect(status != 0);
        fail |= expect(strstr(err, "invalid argument") != NULL);
    }
    {
        char *args[] = {
            (char *)KC_HNSW_TEST_CLI,
            "--dim", "2",
            "--input", dataset,
            "--query", "1 0",
            "--metric", "bad",
            NULL
        };
        fail |= expect(cli_run(args, NULL, out, sizeof(out),
            err, sizeof(err), &status) == 0);
        fail |= expect(status != 0);
        fail |= expect(strstr(err, "Unknown metric name") != NULL);
    }

    remove(dataset);
    case_result(fail, name, detail);
    return fail;
}
#endif

static int case_kc_hnsw_version(void) {
    const char *name = "kc_hnsw_version";
    const char *detail = "returns the generated build version";
    int fail = expect(kc_hnsw_version() != 0U);

    case_result(fail, name, detail);
    return fail;
}

static int case_all(void) {
    int rc = 0;

#ifdef __EMSCRIPTEN__
    test_case_total = 5;
#else
    test_case_total = 7;
#endif
    test_case_current = 0;
    rc += run_case(case_kc_hnsw_open);
    rc += run_case(case_kc_hnsw_add_build);
    rc += run_case(case_kc_hnsw_search);
    rc += run_case(case_kc_hnsw_contract);
#ifndef __EMSCRIPTEN__
    rc += run_case(case_kc_hnsw_concurrency);
    rc += run_case(case_kc_hnsw_cli);
#endif
    rc += run_case(case_kc_hnsw_version);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_hnsw_open") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_hnsw_open();
    }
    if (strcmp(argv[1], "kc_hnsw_add_build") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_hnsw_add_build();
    }
    if (strcmp(argv[1], "kc_hnsw_search") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_hnsw_search();
    }
    if (strcmp(argv[1], "kc_hnsw_contract") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_hnsw_contract();
    }
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "kc_hnsw_concurrency") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_hnsw_concurrency();
    }
    if (strcmp(argv[1], "kc_hnsw_cli") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_hnsw_cli();
    }
#endif
    if (strcmp(argv[1], "kc_hnsw_version") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_hnsw_version();
    }
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
