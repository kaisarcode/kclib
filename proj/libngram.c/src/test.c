/**
 * test.c - libngram public API tests.
 * Summary: Contract tests for traversal.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libngram.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/**
 * Returns the inclusive token end index for a chunk.
 * @param chunk Chunk whose token range is inspected.
 * @return Inclusive token end index.
 */
static size_t chunk_end(const kc_ngram_chunk_t *chunk) {
    return chunk->token_start + chunk->token_count - 1U;
}

/**
 * Counts visited chunks and records the largest token count.
 * @param chunk Current chunk.
 * @param context Counter state.
 * @return 0 to continue traversal.
 */
static int count_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    counter_state_t *st = (counter_state_t *)context;

    st->count++;
    if (chunk->token_count > (size_t)st->max_size) {
        st->max_size = (int)chunk->token_count;
    }
    return 0;
}

/**
 * Records one visited token span in traversal order.
 * @param chunk Current chunk.
 * @param context Recorded-pairs state.
 * @return 0 to continue traversal.
 */
static int record_span_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    recorded_pairs_t *rec = (recorded_pairs_t *)context;

    if (rec->count < 32) {
        rec->items[rec->count].start = chunk->token_start;
        rec->items[rec->count].end = chunk_end(chunk);
        rec->count++;
    }
    return 0;
}

/**
 * Records one chunk value for field-contract checks.
 * @param chunk Current chunk.
 * @param context Chunk-record state.
 * @return 0 to continue traversal.
 */
static int record_chunk_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    chunk_records_t *rec = (chunk_records_t *)context;

    if (rec->count < 32) {
        rec->items[rec->count] = *chunk;
        rec->count++;
    }
    return 0;
}

/**
 * Records visits and closes the configured pivot span.
 * @param chunk Current chunk.
 * @param context Recorded-pairs state.
 * @return 1 for the pivot span, otherwise 0.
 */
static int close_pivot_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    recorded_pairs_t *rec = (recorded_pairs_t *)context;

    record_span_visitor(chunk, rec);
    if (chunk->token_start == 1U && chunk->token_count == 3U) {
        return 1;
    }
    return 0;
}

/**
 * Aborts traversal on the first callback.
 * @param chunk Current chunk.
 * @param context Unused caller context.
 * @return Negative value to abort traversal.
 */
static int abort_first_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    (void)chunk;
    (void)context;
    return -1;
}

/**
 * Aborts when traversal reaches the first one-token span.
 * @param chunk Current chunk.
 * @param context Unused caller context.
 * @return Negative value at the abort span, otherwise 0.
 */
static int abort_third_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    (void)context;
    if (chunk->token_start == 0U && chunk->token_count == 1U) {
        return -1;
    }
    return 0;
}

/**
 * Verifies one integer result.
 * @param name Check description.
 * @param expected Expected value.
 * @param actual Actual value.
 * @return 0 on success, or 1 on failure.
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
 * @param condition Non-zero when the check passes.
 * @return 0 on success, or 1 on failure.
 */
static int expect_true(const char *name, int condition) {
    if (!condition) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

/**
 * Verifies one recorded token span.
 * @param name Check description.
 * @param rec Recorded-pairs state.
 * @param index Record index to inspect.
 * @param start Expected inclusive token start.
 * @param end Expected inclusive token end.
 * @return 0 on success, or 1 on failure.
 */
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

/**
 * Verifies that a token span was not visited.
 * @param name Check description.
 * @param rec Recorded-pairs state.
 * @param start Token start that must be absent.
 * @param end Token end that must be absent.
 * @return 0 on success, or 1 on failure.
 */
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

/**
 * Prints one grouped test-case result line.
 * @param fail Non-zero when the case failed.
 * @param name Canonical case name.
 * @param detail Case behavior description.
 * @return No return value.
 */
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

#ifndef __EMSCRIPTEN__
static int case_ngram_cli(void);
#endif

/**
 * Runs one case and accumulates its result.
 * @param rc Failure accumulator.
 * @param fn Case function to execute.
 * @return No return value.
 */
static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

/**
 * Tests traversal argument and explicit-bound validation.
 * @return 0 on success, or 1 on failure.
 */
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

/**
 * Tests empty-input traversal behavior.
 * @return 0 on success, or 1 on failure.
 */
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

/**
 * Tests descending window traversal order.
 * @return 0 on success, or 1 on failure.
 */
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

/**
 * Tests the public chunk field and borrowing contract.
 * @return 0 on success, or 1 on failure.
 */
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

/**
 * Tests omitted defaults and explicit traversal bounds.
 * @return 0 on success, or 1 on failure.
 */
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

/**
 * Tests default and custom separator behavior.
 * @return 0 on success, or 1 on failure.
 */
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

/**
 * Tests span closure and contained-window suppression.
 * @return 0 on success, or 1 on failure.
 */
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

/**
 * Tests callback-driven traversal abort behavior.
 * @return 0 on success, or 1 on failure.
 */
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

/**
 * Tests the generated build version.
 * @return 0 on success, or 1 on failure.
 */
static int case_kc_ngram_version(void) {
    const char *name = "kc_ngram_version";
    const char *detail = "returns a non-zero build timestamp";
    int fail = expect_true(name, kc_ngram_version() != 0U);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all applicable ngram contract test cases.
 * @return 0 when all cases pass, otherwise non-zero.
 */
static int case_all(void) {
    int rc = 0;

    test_case_total = 10;
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
    run_case(&rc, case_ngram_cli);
#endif
    printf("%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Dispatches helper modes and named test cases.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 on test failure, or 2 on usage failure.
 */
int main(int argc, char **argv) {
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
    if (strcmp(argv[1], "ngram_cli") == 0) return case_ngram_cli();
#endif
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
#ifndef __EMSCRIPTEN__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef KC_CLI_PATH
#define KC_CLI_PATH "ngram"
#endif

static int test_total = 0;
static int test_failed = 0;

/**
 * Writes one text fixture file.
 * @param path Output file path.
 * @param text Text to write.
 * @return Nonzero on success.
 */
static int write_text_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    size_t size = strlen(text);
    int ok;

    if (!file) return 0;
    ok = fwrite(text, 1, size, file) == size;
    fclose(file);
    return ok;
}

/**
 * Executes the CLI and captures stdout.
 * @param args CLI arguments.
 * @param stdin_text Optional stdin content.
 * @param buffer Output buffer.
 * @param buffer_size Output buffer size.
 * @return Nonzero on success.
 */
static int capture_output(
    const char *args,
    const char *stdin_text,
    char *buffer,
    size_t buffer_size
)
{
    const char *stdin_path = "ngram-cli-stdin.txt";
    const char *output_path = "ngram-cli-output.txt";
    char command[4096];
    FILE *file;
    size_t size;

    if (stdin_text && !write_text_file(stdin_path, stdin_text)) return 0;

#ifdef _WIN32
    snprintf(command, sizeof(command),
        "\"\"%s\" %s %s> %s 2>NUL\"",
        KC_CLI_PATH,
        args ? args : "",
        stdin_text ? "< ngram-cli-stdin.txt " : "",
        output_path);
#else
    snprintf(command, sizeof(command),
        "\"%s\" %s %s> %s 2>/dev/null",
        KC_CLI_PATH,
        args ? args : "",
        stdin_text ? "< ngram-cli-stdin.txt " : "",
        output_path);
#endif

    if (system(command) != 0) {
        if (stdin_text) remove(stdin_path);
        remove(output_path);
        return 0;
    }
    if (stdin_text) remove(stdin_path);

    file = fopen(output_path, "rb");
    if (!file) return 0;
    size = fread(buffer, 1, buffer_size - 1, file);
    fclose(file);
    remove(output_path);
    buffer[size] = '\0';
    return 1;
}

/**
 * Executes the CLI with output discarded.
 * @param args Arguments appended after executable.
 * @return Child process status from system().
 */
static int run_cli(const char *args)
{
    char command[4096];

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"\"%s\" %s > NUL 2>&1\"",
        KC_CLI_PATH, args ? args : "");
#else
    snprintf(command, sizeof(command), "\"%s\" %s > /dev/null 2>&1",
        KC_CLI_PATH, args ? args : "");
#endif
    return system(command);
}

/**
 * Removes carriage returns from captured output.
 * @param text Mutable output string.
 * @return None.
 */
static void normalize_newlines(char *text)
{
    size_t read_at = 0;
    size_t write_at = 0;

    while (text[read_at] != '\0') {
        if (text[read_at] != '\r') {
            text[write_at++] = text[read_at];
        }
        read_at++;
    }
    text[write_at] = '\0';
}

/**
 * Prints one contract-test result.
 * @param name Test name.
 * @param detail Behavior under test.
 * @param ok Nonzero on success.
 * @return None.
 */
static void result(const char *name, const char *detail, int ok)
{
    test_total++;
    if (!ok) test_failed++;
    printf("[%d/11] [%s] %s: %s\n", test_total, ok ? "PASS" : "FAIL",
        name, detail);
}

/**
 * Runs all ngram CLI contract tests.
 * @return Zero when all tests pass.
 */
static int case_ngram_cli(void)
{
    const char *file_path = "ngram-cli-file.txt";
    char output[2048];
    char expected[2048];
    int fixture_ok;

#ifdef _WIN32
    const char *filter_command = "more";
#else
    const char *filter_command = "cat";
#endif

    result("help", "--help succeeds", run_cli("--help") == 0);
    result("version", "--version succeeds", run_cli("--version") == 0);
    result("unknown", "unknown option fails",
        run_cli("\"a b\" --unknown") != 0);

    strcpy(expected, "a b\nb c\n");
    result("literal", "literal text traverses requested window size",
        capture_output("\"a b c\" --max 2 --min 2", NULL,
            output, sizeof(output)) &&
        (normalize_newlines(output), strcmp(output, expected) == 0));

    result("stdin", "stdin provides text input",
        capture_output("--max 2 --min 2", "a b c",
            output, sizeof(output)) &&
        (normalize_newlines(output), strcmp(output, expected) == 0));

    fixture_ok = write_text_file(file_path, "a b c");
    result("input file", "-i reads text from file",
        fixture_ok && capture_output("-i ngram-cli-file.txt --max 2 --min 2",
            NULL, output, sizeof(output)) &&
        (normalize_newlines(output), strcmp(output, expected) == 0));

    strcpy(expected, "a,b\nb,c\n");
    result("separators", "custom separators delimit tokens",
        capture_output("\"a,b,c\" --max 2 --min 2 --separators \",\"",
            NULL, output, sizeof(output)) &&
        (normalize_newlines(output), strcmp(output, expected) == 0));

    snprintf(expected, sizeof(expected), "\"a b c\" --filter %s",
        filter_command);
    result("filter", "filter output closes the visited span",
        capture_output(expected, NULL, output, sizeof(output)) &&
        (normalize_newlines(output), strcmp(output, "a b c\n") == 0));

    result("bad min", "--min rejects zero",
        run_cli("\"a b\" --min 0") != 0);
    result("range", "nonzero --max cannot be smaller than --min",
        run_cli("\"a b c\" --max 1 --min 2") != 0);
    result("missing filter", "--filter requires a command",
        run_cli("\"a b\" --filter") != 0);

    remove(file_path);

    printf("%d passed, %d failed\n", test_total - test_failed, test_failed);
    return test_failed ? 1 : 0;
}
#endif
