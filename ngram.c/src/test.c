/**
 * test.c - libngram public API tests.
 * Summary: Contract tests for traversal and the shipped CLI.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libngram.h"

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

#ifndef KC_NGRAM_TEST_CLI
#define KC_NGRAM_TEST_CLI ""
#endif

typedef struct {
    int count;
    int max_size;
} counter_state_t;

typedef struct {
    size_t start;
    size_t end;
} pair_record_t;

typedef struct {
    pair_record_t items[32];
    int count;
} recorded_pairs_t;

typedef struct {
    kc_ngram_chunk_t items[32];
    int count;
} chunk_records_t;

static int test_case_total = 0;
static int test_case_current = 0;
#ifndef __EMSCRIPTEN__
static const char *test_self_path = NULL;
#endif

static size_t chunk_end(const kc_ngram_chunk_t *chunk) {
    return chunk->token_start + chunk->token_count - 1U;
}

static int count_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    counter_state_t *st = (counter_state_t *)context;

    st->count++;
    if (chunk->token_count > (size_t)st->max_size) {
        st->max_size = (int)chunk->token_count;
    }
    return 0;
}

static int record_span_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    recorded_pairs_t *rec = (recorded_pairs_t *)context;

    if (rec->count < 32) {
        rec->items[rec->count].start = chunk->token_start;
        rec->items[rec->count].end = chunk_end(chunk);
        rec->count++;
    }
    return 0;
}

static int record_chunk_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    chunk_records_t *rec = (chunk_records_t *)context;

    if (rec->count < 32) {
        rec->items[rec->count] = *chunk;
        rec->count++;
    }
    return 0;
}

static int close_pivot_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    recorded_pairs_t *rec = (recorded_pairs_t *)context;

    record_span_visitor(chunk, rec);
    if (chunk->token_start == 1U && chunk->token_count == 3U) {
        return 1;
    }
    return 0;
}

static int abort_first_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    (void)chunk;
    (void)context;
    return -1;
}

static int abort_third_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    (void)context;
    if (chunk->token_start == 0U && chunk->token_count == 1U) {
        return -1;
    }
    return 0;
}

static int expect_int(const char *name, int expected, int actual) {
    if (expected != actual) {
        printf("[FAIL] %s: expected %d, got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

static int expect_true(const char *name, int condition) {
    if (!condition) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

static int expect_str(const char *name, const char *expected, const char *actual) {
    if (strcmp(expected, actual) != 0) {
        printf("[FAIL] %s: expected '%s', got '%s'\n", name, expected, actual);
        return 1;
    }
    return 0;
}

static int expect_pair(
    const char *name,
    const recorded_pairs_t *rec,
    int index,
    size_t start,
    size_t end
) {
    if (index >= rec->count) {
        printf("[FAIL] %s: pair %d missing (got %d)\n", name, index, rec->count);
        return 1;
    }
    if (rec->items[index].start != start || rec->items[index].end != end) {
        printf(
            "[FAIL] %s: pair %d expected (%zu,%zu), got (%zu,%zu)\n",
            name,
            index,
            start,
            end,
            rec->items[index].start,
            rec->items[index].end
        );
        return 1;
    }
    return 0;
}

static int expect_not_visited(
    const char *name,
    const recorded_pairs_t *rec,
    size_t start,
    size_t end
) {
    int i;

    for (i = 0; i < rec->count; i++) {
        if (rec->items[i].start == start && rec->items[i].end == end) {
            printf("[FAIL] %s: did not expect visit (%zu,%zu)\n", name, start, end);
            return 1;
        }
    }
    return 0;
}

static void case_result(int fail, const char *name, const char *detail) {
    printf(
        "[%d/%d] [%s] %s: %s\n",
        test_case_current,
        test_case_total,
        fail ? "FAIL" : "PASS",
        name,
        detail
    );
}

typedef int (*case_fn)(void);

static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

#ifndef __EMSCRIPTEN__
#ifdef _WIN32
static int test_cli_append_arg(wchar_t *cmd, size_t cap, const wchar_t *arg) {
    size_t n = wcslen(cmd);
    size_t len = wcslen(arg);
    int quote =
        len == 0U ||
        wcschr(arg, L' ') != NULL ||
        wcschr(arg, L'\t') != NULL ||
        wcschr(arg, L'"') != NULL;
    int i;

    if (n > 0U) {
        if (n + 1U >= cap) return 1;
        cmd[n++] = L' ';
    }
    if (quote) {
        if (n + 1U >= cap) return 1;
        cmd[n++] = L'"';
        for (i = 0; i < (int)len; i++) {
            if (arg[i] == L'"') {
                if (n + 1U >= cap) return 1;
                cmd[n++] = L'\\';
            }
            if (n + 1U >= cap) return 1;
            cmd[n++] = arg[i];
        }
        if (n + 1U >= cap) return 1;
        cmd[n++] = L'"';
    } else {
        if (n + len >= cap) return 1;
        memcpy(cmd + n, arg, len * sizeof(wchar_t));
        n += len;
    }
    cmd[n] = L'\0';
    return 0;
}

static int test_cli_to_wide(const char *in, wchar_t *out, size_t cap) {
    return MultiByteToWideChar(CP_UTF8, 0, in, -1, out, (int)cap) > 0 ? 0 : 1;
}

static void test_cli_read_pipe(HANDLE pipe, char *buf, size_t size) {
    DWORD count;
    size_t used = 0U;

    while (
        used + 1U < size &&
        ReadFile(pipe, buf + used, (DWORD)(size - used - 1U), &count, NULL) &&
        count > 0U
    ) {
        used += count;
    }
    buf[used] = '\0';
}

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

    if (test_cli_to_wide(KC_NGRAM_TEST_CLI, exe, sizeof(exe) / sizeof(wchar_t))) {
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

    cmd[0] = L'\0';
    if (test_cli_append_arg(cmd, sizeof(cmd) / sizeof(wchar_t), exe)) {
        CloseHandle(in_pipe[0]);
        CloseHandle(in_pipe[1]);
        CloseHandle(out_pipe[0]);
        CloseHandle(out_pipe[1]);
        CloseHandle(err_pipe[0]);
        CloseHandle(err_pipe[1]);
        return 1;
    }

    for (i = 1; argv[i] != NULL; i++) {
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

    written = 0U;
    if (input_len > 0U) {
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
    int in_pipe[2];
    int out_pipe[2];
    int err_pipe[2];
    pid_t pid;
    ssize_t count;
    size_t written = 0U;
    size_t pos = 0U;
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
    while (
        pos + 1U < out_size &&
        (count = read(out_pipe[0], out + pos, out_size - pos - 1U)) > 0
    ) {
        pos += (size_t)count;
    }
    close(out_pipe[0]);

    pos = 0U;
    memset(err, 0, err_size);
    while (
        pos + 1U < err_size &&
        (count = read(err_pipe[0], err + pos, err_size - pos - 1U)) > 0
    ) {
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

static int helper_consume_stdin(void) {
    char buffer[256];

    while (fread(buffer, 1U, sizeof(buffer), stdin) > 0U) {
    }
    return ferror(stdin) ? 1 : 0;
}

static int helper_output(void) {
    if (helper_consume_stdin() != 0) {
        return 1;
    }
    fputs("close\n", stdout);
    return 0;
}

static int helper_silent(void) {
    return helper_consume_stdin();
}

static int helper_literal(int argc, char **argv) {
    if (helper_consume_stdin() != 0) {
        return 1;
    }
    if (argc == 3 && strcmp(argv[2], "x;echo nope") == 0) {
        fputs("close\n", stdout);
    }
    return 0;
}
#endif

static int case_kc_ngram_traverse_validation(void) {
    const char *name = "kc_ngram_traverse_validation";
    const char *detail = "rejects invalid input, visitor, and explicit bounds";
    kc_ngram_options_t opts;
    size_t max_tokens;
    size_t min_tokens;
    int fail = 0;

    fail += expect_int(
        "NULL input returns ERROR",
        KC_NGRAM_ERROR,
        kc_ngram_traverse(NULL, NULL, count_visitor, NULL)
    );
    fail += expect_int(
        "NULL visitor returns ERROR",
        KC_NGRAM_ERROR,
        kc_ngram_traverse("a b", NULL, NULL, NULL)
    );

    min_tokens = 0U;
    opts = (kc_ngram_options_t){0};
    opts.min_tokens = &min_tokens;
    fail += expect_int(
        "min_tokens 0 returns ERROR",
        KC_NGRAM_ERROR,
        kc_ngram_traverse("a b", &opts, count_visitor, NULL)
    );

    max_tokens = 1U;
    min_tokens = 2U;
    opts = (kc_ngram_options_t){0};
    opts.max_tokens = &max_tokens;
    opts.min_tokens = &min_tokens;
    fail += expect_int(
        "max < min returns ERROR",
        KC_NGRAM_ERROR,
        kc_ngram_traverse("a b", &opts, count_visitor, NULL)
    );

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_kc_ngram_traverse_empty(void) {
    const char *name = "kc_ngram_traverse_empty";
    const char *detail = "returns OK without visiting for empty input";
    counter_state_t st = {0, 0};
    int fail = 0;

    fail += expect_int(
        "empty input returns OK",
        KC_NGRAM_OK,
        kc_ngram_traverse("", NULL, count_visitor, &st)
    );
    fail += expect_int("empty input never visits", 0, st.count);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_kc_ngram_traverse_order(void) {
    const char *name = "kc_ngram_traverse_order";
    const char *detail = "descends from largest to smallest windows, left to right";
    static const size_t expected[10][2] = {
        {0U, 3U}, {0U, 2U}, {1U, 3U}, {0U, 1U}, {1U, 2U},
        {2U, 3U}, {0U, 0U}, {1U, 1U}, {2U, 2U}, {3U, 3U}
    };
    recorded_pairs_t rec;
    int fail = 0;
    int i;

    rec.count = 0;
    fail += expect_int(
        "order traversal returns OK",
        KC_NGRAM_OK,
        kc_ngram_traverse("a b c d", NULL, record_span_visitor, &rec)
    );
    fail += expect_int("order records 10 visits", 10, rec.count);

    for (i = 0; i < 10; i++) {
        fail += expect_pair(
            "order sequence",
            &rec,
            i,
            expected[i][0],
            expected[i][1]
        );
    }

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_kc_ngram_traverse_chunk(void) {
    const char *name = "kc_ngram_traverse_chunk";
    const char *detail = "reports borrowed byte spans and token position";
    const char *input = "a b c d";
    kc_ngram_options_t opts = {0};
    chunk_records_t rec;
    const kc_ngram_chunk_t *target = NULL;
    size_t max_tokens = 3U;
    size_t min_tokens = 1U;
    int fail = 0;
    int i;

    rec.count = 0;
    opts.max_tokens = &max_tokens;
    opts.min_tokens = &min_tokens;
    opts.separators = " ";

    fail += expect_int(
        "chunk contract returns OK",
        KC_NGRAM_OK,
        kc_ngram_traverse(input, &opts, record_chunk_visitor, &rec)
    );
    fail += expect_int("chunk contract emits 9 chunks", 9, rec.count);

    for (i = 0; i < rec.count; i++) {
        fail += expect_true(
            "chunk data stays within borrowed input",
            rec.items[i].data >= input &&
            rec.items[i].data < input + strlen(input)
        );
    }

    for (i = 0; i < rec.count; i++) {
        if (
            rec.items[i].token_start == 1U &&
            rec.items[i].token_count == 3U
        ) {
            target = &rec.items[i];
            break;
        }
    }

    fail += expect_true("chunk token_start=1 token_count=3 present", target != NULL);
    if (target != NULL) {
        fail += expect_true("chunk data points at byte 2", target->data == input + 2);
        fail += expect_int("chunk data_size is 5", 5, (int)target->data_size);
        fail += expect_int("chunk token_start is 1", 1, (int)target->token_start);
        fail += expect_int("chunk token_count is 3", 3, (int)target->token_count);
        fail += expect_true(
            "chunk data preserves internal separators",
            memcmp(target->data, "b c d", target->data_size) == 0
        );
    }

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_kc_ngram_traverse_bounds(void) {
    const char *name = "kc_ngram_traverse_bounds";
    const char *detail = "honors omitted defaults and explicit zero/min/max bounds";
    kc_ngram_options_t opts;
    counter_state_t st;
    size_t max_tokens;
    size_t min_tokens;
    int fail = 0;

    st = (counter_state_t){0, 0};
    fail += expect_int(
        "omitted defaults return OK",
        KC_NGRAM_OK,
        kc_ngram_traverse(
            "a b c d e f g h i j k",
            NULL,
            count_visitor,
            &st
        )
    );
    fail += expect_int("default max=10/min=1 emits 65", 65, st.count);
    fail += expect_int("default maximum chunk is 10", 10, st.max_size);

    max_tokens = 2U;
    opts = (kc_ngram_options_t){0};
    opts.max_tokens = &max_tokens;
    st = (counter_state_t){0, 0};
    fail += expect_int(
        "partial options max=2 return OK",
        KC_NGRAM_OK,
        kc_ngram_traverse("a b c", &opts, count_visitor, &st)
    );
    fail += expect_int("partial max=2 keeps default min and emits 5", 5, st.count);
    fail += expect_int("partial max=2 largest chunk is 2", 2, st.max_size);

    max_tokens = 3U;
    min_tokens = 3U;
    opts = (kc_ngram_options_t){0};
    opts.max_tokens = &max_tokens;
    opts.min_tokens = &min_tokens;
    st = (counter_state_t){0, 0};
    fail += expect_int(
        "min=max returns OK",
        KC_NGRAM_OK,
        kc_ngram_traverse("a b c", &opts, count_visitor, &st)
    );
    fail += expect_int("min=max emits 1", 1, st.count);
    fail += expect_int("min=max chunk size is 3", 3, st.max_size);

    max_tokens = 0U;
    opts = (kc_ngram_options_t){0};
    opts.max_tokens = &max_tokens;
    st = (counter_state_t){0, 0};
    fail += expect_int(
        "explicit max=0 returns OK",
        KC_NGRAM_OK,
        kc_ngram_traverse("a b c", &opts, count_visitor, &st)
    );
    fail += expect_int("explicit max=0 means all tokens", 6, st.count);
    fail += expect_int("explicit max=0 reaches 3-token chunk", 3, st.max_size);

    max_tokens = 100U;
    opts = (kc_ngram_options_t){0};
    opts.max_tokens = &max_tokens;
    st = (counter_state_t){0, 0};
    fail += expect_int(
        "oversized max returns OK",
        KC_NGRAM_OK,
        kc_ngram_traverse("a b c", &opts, count_visitor, &st)
    );
    fail += expect_int("oversized max clamps to token count", 6, st.count);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_kc_ngram_traverse_separators(void) {
    const char *name = "kc_ngram_traverse_separators";
    const char *detail = "uses the default separators when omitted and custom bytes when explicit";
    kc_ngram_options_t opts;
    counter_state_t st;
    size_t max_tokens = 2U;
    size_t min_tokens = 2U;
    int fail = 0;

    opts = (kc_ngram_options_t){0};
    opts.max_tokens = &max_tokens;
    opts.min_tokens = &min_tokens;
    opts.separators = ",";
    st = (counter_state_t){0, 0};

    fail += expect_int(
        "comma separators return OK",
        KC_NGRAM_OK,
        kc_ngram_traverse("a,b,c", &opts, count_visitor, &st)
    );
    fail += expect_int("comma separators emit 2", 2, st.count);
    fail += expect_int("comma separator chunk size is 2", 2, st.max_size);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_kc_ngram_traverse_closure(void) {
    const char *name = "kc_ngram_traverse_closure";
    const char *detail = "visitor return 1 closes a span and suppresses contained windows";
    static const size_t expected[5][2] = {
        {0U, 3U}, {0U, 2U}, {1U, 3U}, {0U, 1U}, {0U, 0U}
    };
    static const size_t skipped[5][2] = {
        {1U, 2U}, {2U, 3U}, {1U, 1U}, {2U, 2U}, {3U, 3U}
    };
    recorded_pairs_t rec;
    int fail = 0;
    int i;

    rec.count = 0;
    fail += expect_int(
        "closure returns OK",
        KC_NGRAM_OK,
        kc_ngram_traverse("a b c d", NULL, close_pivot_visitor, &rec)
    );
    fail += expect_int("closure records 5 visits", 5, rec.count);

    for (i = 0; i < 5; i++) {
        fail += expect_pair(
            "closure order",
            &rec,
            i,
            expected[i][0],
            expected[i][1]
        );
    }
    for (i = 0; i < 5; i++) {
        fail += expect_not_visited(
            "closure skips contained",
            &rec,
            skipped[i][0],
            skipped[i][1]
        );
    }

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_kc_ngram_traverse_abort(void) {
    const char *name = "kc_ngram_traverse_abort";
    const char *detail = "negative visitor return aborts immediately with EABORT";
    kc_ngram_options_t opts = {0};
    recorded_pairs_t rec;
    size_t max_tokens = 2U;
    int fail = 0;

    rec.count = 0;
    fail += expect_int(
        "abort on first returns EABORT",
        KC_NGRAM_EABORT,
        kc_ngram_traverse("a b c", NULL, abort_first_visitor, &rec)
    );
    fail += expect_int("abort on first records no completed visitor state", 0, rec.count);

    opts.max_tokens = &max_tokens;
    fail += expect_int(
        "abort on third returns EABORT",
        KC_NGRAM_EABORT,
        kc_ngram_traverse("a b c", &opts, abort_third_visitor, NULL)
    );

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_kc_ngram_version(void) {
    const char *name = "kc_ngram_version";
    const char *detail = "returns a non-zero build timestamp";
    int fail = expect_true(name, kc_ngram_version() != 0U);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

#ifndef __EMSCRIPTEN__
static int run_cli_expect(
    const char *check,
    char *const argv[],
    const char *input,
    const char *expected_out,
    int expected_status
) {
    char out[8192];
    char err[4096];
    int status = 0;
    int fail = 0;

    if (
        test_cli_run_input(
            argv,
            input,
            input != NULL ? strlen(input) : 0U,
            out,
            sizeof(out),
            err,
            sizeof(err),
            &status
        ) != 0
    ) {
        printf("[FAIL] %s: failed to execute CLI\n", check);
        return 1;
    }

    fail += expect_int(check, expected_status, status);
    if (expected_out != NULL) {
        fail += expect_str(check, expected_out, out);
    }
    return fail;
}

static int case_kc_ngram_cli(void) {
    const char *name = "kc_ngram_cli";
    const char *detail =
        "flags, aliases, defaults, stdin, bounds, separators, commands, diagnostics, help, and version";
    int cli_enabled = KC_NGRAM_TEST_CLI[0] != '\0';
    int fail = 0;
    char cmd_output[4096];
    char cmd_silent[4096];
    char cmd_literal[4096];

#ifdef _WIN32
    cli_enabled = 1;
#endif

    if (!cli_enabled) {
        case_result(fail, name, detail);
        return 0;
    }

    if (
        test_self_path == NULL ||
        snprintf(
            cmd_output,
            sizeof(cmd_output),
            "\"%s\" --helper-output",
            test_self_path
        ) >= (int)sizeof(cmd_output) ||
        snprintf(
            cmd_silent,
            sizeof(cmd_silent),
            "\"%s\" --helper-silent",
            test_self_path
        ) >= (int)sizeof(cmd_silent) ||
        snprintf(
            cmd_literal,
            sizeof(cmd_literal),
            "\"%s\" --helper-literal \"x;echo nope\"",
            test_self_path
        ) >= (int)sizeof(cmd_literal)
    ) {
        fail += expect_true("CLI helper command construction", 0);
        case_result(fail, name, detail);
        return 1;
    }

    {
        char out[8192];
        char err[4096];
        int status = 0;
        char *help[] = {(char *)KC_NGRAM_TEST_CLI, "-h", NULL};
        char *long_help[] = {(char *)KC_NGRAM_TEST_CLI, "--help", NULL};
        char *version[] = {(char *)KC_NGRAM_TEST_CLI, "-v", NULL};
        char *long_version[] = {(char *)KC_NGRAM_TEST_CLI, "--version", NULL};

        fail += expect_int(
            "CLI -h exits 0",
            0,
            test_cli_run_input(help, NULL, 0U, out, sizeof(out), err, sizeof(err), &status) ? 1 : status
        );
        fail += expect_true("CLI -h prints usage", strstr(out, "Usage:") != NULL);
        fail += expect_int(
            "CLI --help exits 0",
            0,
            test_cli_run_input(long_help, NULL, 0U, out, sizeof(out), err, sizeof(err), &status) ? 1 : status
        );
        fail += expect_true("CLI --help prints usage", strstr(out, "Usage:") != NULL);
        fail += expect_int(
            "CLI -v exits 0",
            0,
            test_cli_run_input(version, NULL, 0U, out, sizeof(out), err, sizeof(err), &status) ? 1 : status
        );
        fail += expect_true("CLI -v prints build", strstr(out, "ngram build ") != NULL);
        fail += expect_int(
            "CLI --version exits 0",
            0,
            test_cli_run_input(long_version, NULL, 0U, out, sizeof(out), err, sizeof(err), &status) ? 1 : status
        );
        fail += expect_true("CLI --version prints build", strstr(out, "ngram build ") != NULL);
    }

    {
        char *positional[] = {(char *)KC_NGRAM_TEST_CLI, "a b c", NULL};
        char *stdin_cli[] = {(char *)KC_NGRAM_TEST_CLI, NULL};
        char *max_long[] = {(char *)KC_NGRAM_TEST_CLI, "--max", "2", "a b c", NULL};
        char *max_short[] = {(char *)KC_NGRAM_TEST_CLI, "-max", "2", "a b c", NULL};
        char *min_long[] = {(char *)KC_NGRAM_TEST_CLI, "--min", "2", "a b c", NULL};
        char *min_short[] = {(char *)KC_NGRAM_TEST_CLI, "-min", "2", "a b c", NULL};
        char *sep_long[] = {(char *)KC_NGRAM_TEST_CLI, "--sep", ",", "a,b,c", NULL};
        char *sep_short[] = {(char *)KC_NGRAM_TEST_CLI, "-sep", ",", "a,b,c", NULL};
        char *max_zero[] = {(char *)KC_NGRAM_TEST_CLI, "--max", "0", "a b c", NULL};

        fail += run_cli_expect(
            "CLI positional defaults",
            positional,
            NULL,
            "a b c\na b\nb c\na\nb\nc\n",
            0
        );
        fail += run_cli_expect(
            "CLI stdin defaults",
            stdin_cli,
            "a b c",
            "a b c\na b\nb c\na\nb\nc\n",
            0
        );
        fail += run_cli_expect(
            "CLI --max",
            max_long,
            NULL,
            "a b\nb c\na\nb\nc\n",
            0
        );
        fail += run_cli_expect(
            "CLI -max",
            max_short,
            NULL,
            "a b\nb c\na\nb\nc\n",
            0
        );
        fail += run_cli_expect(
            "CLI --min",
            min_long,
            NULL,
            "a b c\na b\nb c\n",
            0
        );
        fail += run_cli_expect(
            "CLI -min",
            min_short,
            NULL,
            "a b c\na b\nb c\n",
            0
        );
        fail += run_cli_expect(
            "CLI --sep",
            sep_long,
            NULL,
            "a,b,c\na,b\nb,c\na\nb\nc\n",
            0
        );
        fail += run_cli_expect(
            "CLI -sep",
            sep_short,
            NULL,
            "a,b,c\na,b\nb,c\na\nb\nc\n",
            0
        );
        fail += run_cli_expect(
            "CLI explicit max zero",
            max_zero,
            NULL,
            "a b c\na b\nb c\na\nb\nc\n",
            0
        );
    }

    {
        char *silent[] = {
            (char *)KC_NGRAM_TEST_CLI,
            "--cmd",
            cmd_silent,
            "a b c",
            NULL
        };
        char *close[] = {
            (char *)KC_NGRAM_TEST_CLI,
            "-cmd",
            cmd_output,
            "a b c",
            NULL
        };
        char *literal[] = {
            (char *)KC_NGRAM_TEST_CLI,
            "--cmd",
            cmd_literal,
            "a b c",
            NULL
        };

        fail += run_cli_expect(
            "CLI command without stdout keeps spans open",
            silent,
            NULL,
            "a b c\na b\nb c\na\nb\nc\n",
            0
        );
        fail += run_cli_expect(
            "CLI command stdout closes span",
            close,
            NULL,
            "a b c\n",
            0
        );
        fail += run_cli_expect(
            "CLI command arguments are literal and not shell-evaluated",
            literal,
            NULL,
            "a b c\n",
            0
        );
    }

    {
        char out[8192];
        char err[4096];
        int status = 0;
        char *unknown[] = {(char *)KC_NGRAM_TEST_CLI, "--nope", NULL};
        char *missing_max[] = {(char *)KC_NGRAM_TEST_CLI, "--max", NULL};
        char *bad_min[] = {(char *)KC_NGRAM_TEST_CLI, "--min", "0", "a b", NULL};
        char *too_many[] = {(char *)KC_NGRAM_TEST_CLI, "a", "b", NULL};

        fail += expect_int(
            "CLI unknown argument exits 1",
            1,
            test_cli_run_input(unknown, NULL, 0U, out, sizeof(out), err, sizeof(err), &status) ? 0 : status
        );
        fail += expect_true("CLI unknown argument diagnostic", strstr(err, "Unknown argument.") != NULL);
        fail += expect_true("CLI unknown argument prints help", strstr(out, "Usage:") != NULL);

        fail += expect_int(
            "CLI missing max exits 1",
            1,
            test_cli_run_input(missing_max, NULL, 0U, out, sizeof(out), err, sizeof(err), &status) ? 0 : status
        );
        fail += expect_true("CLI missing max diagnostic", strstr(err, "Missing value for --max.") != NULL);

        fail += expect_int(
            "CLI min zero exits 1",
            1,
            test_cli_run_input(bad_min, NULL, 0U, out, sizeof(out), err, sizeof(err), &status) ? 0 : status
        );
        fail += expect_true("CLI min zero diagnostic", strstr(err, "Invalid value for --min.") != NULL);

        fail += expect_int(
            "CLI too many positional arguments exits 1",
            1,
            test_cli_run_input(too_many, NULL, 0U, out, sizeof(out), err, sizeof(err), &status) ? 0 : status
        );
        fail += expect_true("CLI too many diagnostic", strstr(err, "Too many positional arguments.") != NULL);
    }

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}
#endif

static int case_all(void) {
    int rc = 0;

#ifdef __EMSCRIPTEN__
    test_case_total = 8;
#else
    int cli_enabled = KC_NGRAM_TEST_CLI[0] != '\0';
#ifdef _WIN32
    cli_enabled = 1;
#endif
    test_case_total = cli_enabled ? 9 : 8;
#endif

    test_case_current = 0;
    run_case(&rc, case_kc_ngram_traverse_validation);
    run_case(&rc, case_kc_ngram_traverse_empty);
    run_case(&rc, case_kc_ngram_traverse_order);
    run_case(&rc, case_kc_ngram_traverse_chunk);
    run_case(&rc, case_kc_ngram_traverse_bounds);
    run_case(&rc, case_kc_ngram_traverse_separators);
    run_case(&rc, case_kc_ngram_traverse_closure);
    run_case(&rc, case_kc_ngram_traverse_abort);
    run_case(&rc, case_kc_ngram_version);
#ifndef __EMSCRIPTEN__
    if (cli_enabled) {
        run_case(&rc, case_kc_ngram_cli);
    }
#endif

    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

int main(int argc, char **argv) {
#ifndef __EMSCRIPTEN__
    test_self_path = argv[0];

    if (argc >= 2 && strcmp(argv[1], "--helper-output") == 0) {
        return helper_output();
    }
    if (argc >= 2 && strcmp(argv[1], "--helper-silent") == 0) {
        return helper_silent();
    }
    if (argc >= 2 && strcmp(argv[1], "--helper-literal") == 0) {
        return helper_literal(argc, argv);
    }
#endif

    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }

    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_ngram_traverse_validation") == 0) return case_kc_ngram_traverse_validation();
    if (strcmp(argv[1], "kc_ngram_traverse_empty") == 0) return case_kc_ngram_traverse_empty();
    if (strcmp(argv[1], "kc_ngram_traverse_order") == 0) return case_kc_ngram_traverse_order();
    if (strcmp(argv[1], "kc_ngram_traverse_chunk") == 0) return case_kc_ngram_traverse_chunk();
    if (strcmp(argv[1], "kc_ngram_traverse_bounds") == 0) return case_kc_ngram_traverse_bounds();
    if (strcmp(argv[1], "kc_ngram_traverse_separators") == 0) return case_kc_ngram_traverse_separators();
    if (strcmp(argv[1], "kc_ngram_traverse_closure") == 0) return case_kc_ngram_traverse_closure();
    if (strcmp(argv[1], "kc_ngram_traverse_abort") == 0) return case_kc_ngram_traverse_abort();
    if (strcmp(argv[1], "kc_ngram_version") == 0) return case_kc_ngram_version();
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "kc_ngram_cli") == 0) return case_kc_ngram_cli();
#endif

    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
