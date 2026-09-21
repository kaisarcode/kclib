/**
 * test.c - libngram public API tests.
 * Summary: Tests each public libngram function through one CTest case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libngram.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Visitor that counts emitted chunks and tracks the largest chunk size.
 */
typedef struct {
    int count;
    int max_size;
} counter_state_t;

/**
 * Counts one emitted chunk.
 * @param chunk Current chunk.
 * @param context Counter state pointer.
 * @return 0 to continue.
 */
static int count_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    counter_state_t *st = (counter_state_t *)context;
    st->count++;
    if (chunk->size > (size_t)st->max_size) st->max_size = (int)chunk->size;
    return 0;
}

/**
 * One (start,end) token pair recorded in visit order.
 */
typedef struct {
    size_t start;
    size_t end;
} pair_record_t;

/**
 * Growing-state records of visited token pairs.
 */
typedef struct {
    pair_record_t items[16];
    int count;
} recorded_pairs_t;

/**
 * Records each visited (start,end) pair.
 * @param chunk Current chunk.
 * @param context Recorded pairs pointer.
 * @return 0 to continue.
 */
static int record_span_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    recorded_pairs_t *rec = (recorded_pairs_t *)context;
    if (rec->count < 16) {
        rec->items[rec->count].start = chunk->start;
        rec->items[rec->count].end = chunk->end;
        rec->count++;
    }
    return 0;
}

/**
 * Records full chunk copies for field and offset contracts.
 */
typedef struct {
    kc_ngram_chunk_t items[16];
    int count;
} chunk_records_t;

/**
 * Records one chunk by value.
 * @param chunk Current chunk.
 * @param context Chunk records pointer.
 * @return 0 to continue.
 */
static int record_chunk_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    chunk_records_t *rec = (chunk_records_t *)context;
    if (rec->count < 16) {
        rec->items[rec->count] = *chunk;
        rec->count++;
    }
    return 0;
}

/**
 * Records visits and closes the span on (start=1, end=3).
 * @return 1 on the pivot span, otherwise 0.
 */
static int close_pivot_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    recorded_pairs_t *rec = (recorded_pairs_t *)context;
    if (rec->count < 16) {
        rec->items[rec->count].start = chunk->start;
        rec->items[rec->count].end = chunk->end;
        rec->count++;
    }
    if (chunk->start == 1 && chunk->end == 3) {
        return 1;
    }
    return 0;
}

/**
 * Visitor that aborts traversal on the first visited chunk.
 * @return -1 to abort.
 */
static int abort_first_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    (void)chunk;
    (void)context;
    return -1;
}

/**
 * Visitor that aborts traversal on the third visit (start=0, end=0).
 * @return -1 on that span, otherwise 0.
 */
static int abort_third_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    (void)context;
    if (chunk->start == 0 && chunk->end == 0) {
        return -1;
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
 * Verifies one recorded (start,end) pair at an index.
 * @param name Check description.
 * @param rec Recorded pairs.
 * @param index Expected index.
 * @param start Expected start.
 * @param end Expected end.
 * @return 0 on success, 1 on failure.
 */
static int expect_pair(const char *name, const recorded_pairs_t *rec,
    int index, size_t start, size_t end) {
    if (index >= rec->count) {
        printf("[FAIL] %s: pair %d missing (got %d)\n", name, index, rec->count);
        return 1;
    }
    if (rec->items[index].start != start || rec->items[index].end != end) {
        printf("[FAIL] %s: pair %d expected (%zu,%zu), got (%zu,%zu)\n", name,
            index, start, end, rec->items[index].start, rec->items[index].end);
        return 1;
    }
    return 0;
}

/**
 * Verifies that a (start,end) pair was never visited.
 * @param name Check description.
 * @param rec Recorded pairs.
 * @param start Expected start.
 * @param end Expected end.
 * @return 0 on success, 1 on failure.
 */
static int expect_not_visited(const char *name, const recorded_pairs_t *rec,
    size_t start, size_t end) {
    int i;
    for (i = 0; i < rec->count; i++) {
        if (rec->items[i].start == start && rec->items[i].end == end) {
            printf("[FAIL] %s: did not expect visit (%zu,%zu)\n", name, start, end);
            return 1;
        }
    }
    return 0;
}

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Test case name.
 * @param detail Test behavior detail.
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
 * Tests kc_ngram_options_default.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_options_default(void) {
    const char *name = "kc_ngram_options_default";
    const char *detail = "returns default bounds and separators";
    kc_ngram_options_t opts;
    int fail;

    fail = 0;
    opts = kc_ngram_options_default();
    fail += expect_int("default max_tokens is 10", 10, (int)opts.max_tokens);
    fail += expect_int("default min_tokens is 1", 1, (int)opts.min_tokens);
    fail += expect_int("default separators string", 0,
        opts.separators != NULL ? strcmp(opts.separators, " \t\r\n") : -1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_execute input and option validation.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_execute_validation(void) {
    const char *name = "kc_ngram_execute_validation";
    const char *detail = "rejects invalid input, visitor, count, and bounds";
    kc_ngram_options_t opts;
    size_t count;
    int fail;

    fail = 0;

    count = 99;
    fail += expect_int("NULL input returns ERROR", KC_NGRAM_ERROR,
        kc_ngram_execute(NULL, NULL, count_visitor, NULL, &count));
    fail += expect_int("NULL input leaves count 0", 0, (int)count);

    count = 99;
    fail += expect_int("NULL visitor returns ERROR", KC_NGRAM_ERROR,
        kc_ngram_execute("a b", NULL, NULL, NULL, &count));
    fail += expect_int("NULL visitor leaves count 0", 0, (int)count);

    fail += expect_int("NULL out_count returns ERROR", KC_NGRAM_ERROR,
        kc_ngram_execute("a b", NULL, count_visitor, NULL, NULL));

    opts = (kc_ngram_options_t){.max_tokens = 10, .min_tokens = 0, .separators = " "};
    count = 99;
    fail += expect_int("min_tokens 0 returns ERROR", KC_NGRAM_ERROR,
        kc_ngram_execute("a b", &opts, count_visitor, NULL, &count));
    fail += expect_int("min_tokens 0 leaves count 0", 0, (int)count);

    opts = (kc_ngram_options_t){.max_tokens = 1, .min_tokens = 2, .separators = " "};
    count = 99;
    fail += expect_int("max < min returns ERROR", KC_NGRAM_ERROR,
        kc_ngram_execute("a b", &opts, count_visitor, NULL, &count));
    fail += expect_int("max < min leaves count 0", 0, (int)count);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_execute with empty input.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_execute_empty(void) {
    const char *name = "kc_ngram_execute_empty";
    const char *detail = "returns OK without visiting for empty input";
    counter_state_t st;
    size_t count;
    int fail;

    fail = 0;
    st = (counter_state_t){0, 0};
    count = 99;
    fail += expect_int("empty input returns OK", KC_NGRAM_OK,
        kc_ngram_execute("", NULL, count_visitor, &st, &count));
    fail += expect_int("empty input emits 0 chunks", 0, (int)count);
    fail += expect_int("empty input never visits", 0, st.count);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_execute traversal order.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_execute_order(void) {
    const char *name = "kc_ngram_execute_order";
    const char *detail = "descends from largest to smallest windows, left to right";
    static const size_t expected[10][2] = {
        {0, 3}, {0, 2}, {1, 3}, {0, 1}, {1, 2}, {2, 3},
        {0, 0}, {1, 1}, {2, 2}, {3, 3}
    };
    recorded_pairs_t rec;
    size_t count;
    int fail;
    int i;

    fail = 0;
    rec.count = 0;
    fail += expect_int("order traversal returns OK", KC_NGRAM_OK,
        kc_ngram_execute("a b c d", NULL, record_span_visitor, &rec, &count));
    fail += expect_int("order emits 10 chunks", 10, (int)count);
    fail += expect_int("order records 10 visits", 10, rec.count);
    for (i = 0; i < 10; i++) {
        fail += expect_pair("order sequence", &rec, i, expected[i][0], expected[i][1]);
    }
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_execute chunk field contract.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_execute_chunk(void) {
    const char *name = "kc_ngram_execute_chunk";
    const char *detail = "reports byte offsets, token indices, and sizes";
    const char *input = "a b c d";
    kc_ngram_options_t opts;
    chunk_records_t rec;
    const kc_ngram_chunk_t *target;
    size_t count;
    int fail;
    int i;

    fail = 0;
    rec.count = 0;
    opts = (kc_ngram_options_t){.max_tokens = 3, .min_tokens = 1, .separators = " "};
    fail += expect_int("chunk contract returns OK", KC_NGRAM_OK,
        kc_ngram_execute(input, &opts, record_chunk_visitor, &rec, &count));
    fail += expect_int("chunk contract emits 9 chunks", 9, rec.count);
    for (i = 0; i < rec.count; i++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "chunk %d borrows input", i);
        fail += expect_true(msg, rec.items[i].input == input);
    }

    target = NULL;
    for (i = 0; i < rec.count; i++) {
        if (rec.items[i].start == 1 && rec.items[i].end == 3) {
            target = &rec.items[i];
            break;
        }
    }
    fail += expect_true("chunk (1,3) present", target != NULL);
    if (target != NULL) {
        fail += expect_int("chunk (1,3) byte_start is 2", 2, (int)target->byte_start);
        fail += expect_int("chunk (1,3) byte_end is 7", 7, (int)target->byte_end);
        fail += expect_int("chunk (1,3) start is 1", 1, (int)target->start);
        fail += expect_int("chunk (1,3) end is 3", 3, (int)target->end);
        fail += expect_int("chunk (1,3) size is 3", 3, (int)target->size);
    }
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_execute min and max window bounds.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_execute_bounds(void) {
    const char *name = "kc_ngram_execute_bounds";
    const char *detail = "honors default, explicit, zero, and oversized bounds";
    kc_ngram_options_t opts;
    counter_state_t st;
    size_t count;
    int fail;

    fail = 0;

    st = (counter_state_t){0, 0};
    count = 99;
    fail += expect_int("defaults over 11 tokens returns OK", KC_NGRAM_OK,
        kc_ngram_execute("a b c d e f g h i j k", NULL, count_visitor, &st, &count));
    fail += expect_int("defaults over 11 tokens emit 65", 65, (int)count);

    opts = (kc_ngram_options_t){.max_tokens = 2, .min_tokens = 1, .separators = " "};
    st = (counter_state_t){0, 0};
    count = 99;
    fail += expect_int("max=2 over 3 tokens returns OK", KC_NGRAM_OK,
        kc_ngram_execute("a b c", &opts, count_visitor, &st, &count));
    fail += expect_int("max=2 over 3 tokens emits 5", 5, (int)count);
    fail += expect_true("max=2 keeps chunks within 2", st.max_size <= 2);

    opts = (kc_ngram_options_t){.max_tokens = 3, .min_tokens = 3, .separators = " "};
    st = (counter_state_t){0, 0};
    count = 99;
    fail += expect_int("min=max returns OK", KC_NGRAM_OK,
        kc_ngram_execute("a b c", &opts, count_visitor, &st, &count));
    fail += expect_int("min=max emits 1", 1, (int)count);
    fail += expect_int("min=max chunk size is 3", 3, st.max_size);

    opts = (kc_ngram_options_t){.max_tokens = 0, .min_tokens = 1, .separators = " "};
    st = (counter_state_t){0, 0};
    count = 99;
    fail += expect_int("max=0 uses all tokens returns OK", KC_NGRAM_OK,
        kc_ngram_execute("a b c", &opts, count_visitor, &st, &count));
    fail += expect_int("max=0 uses all tokens emits 6", 6, (int)count);

    opts = (kc_ngram_options_t){.max_tokens = 100, .min_tokens = 1, .separators = " "};
    st = (counter_state_t){0, 0};
    count = 99;
    fail += expect_int("over-large max clamps returns OK", KC_NGRAM_OK,
        kc_ngram_execute("a b c", &opts, count_visitor, &st, &count));
    fail += expect_int("over-large max clamps emits 6", 6, (int)count);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_execute with a custom separator set.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_execute_separators(void) {
    const char *name = "kc_ngram_execute_separators";
    const char *detail = "tokenizes on custom byte separators";
    kc_ngram_options_t opts;
    counter_state_t st;
    size_t count;
    int fail;

    fail = 0;
    opts = (kc_ngram_options_t){.max_tokens = 2, .min_tokens = 2, .separators = ","};
    st = (counter_state_t){0, 0};
    count = 99;
    fail += expect_int("comma separators returns OK", KC_NGRAM_OK,
        kc_ngram_execute("a,b,c", &opts, count_visitor, &st, &count));
    fail += expect_int("comma separators emit 2", 2, (int)count);
    fail += expect_int("comma separator chunk size is 2", 2, st.max_size);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_execute span closure.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_execute_closure(void) {
    const char *name = "kc_ngram_execute_closure";
    const char *detail = "closing one span suppresses fully contained windows";
    static const size_t expected[5][2] = {
        {0, 3}, {0, 2}, {1, 3}, {0, 1}, {0, 0}
    };
    static const size_t skipped[5][2] = {
        {1, 2}, {2, 3}, {1, 1}, {2, 2}, {3, 3}
    };
    recorded_pairs_t rec;
    size_t count;
    int fail;
    int i;

    fail = 0;
    rec.count = 0;
    fail += expect_int("closure returns OK", KC_NGRAM_OK,
        kc_ngram_execute("a b c d", NULL, close_pivot_visitor, &rec, &count));
    fail += expect_int("closure emits 5 chunks", 5, (int)count);
    fail += expect_int("closure records 5 visits", 5, rec.count);
    for (i = 0; i < 5; i++) {
        fail += expect_pair("closure order", &rec, i, expected[i][0], expected[i][1]);
    }
    for (i = 0; i < 5; i++) {
        fail += expect_not_visited("closure skips contained", &rec,
            skipped[i][0], skipped[i][1]);
    }
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_execute abort on negative visitor return.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_execute_abort(void) {
    const char *name = "kc_ngram_execute_abort";
    const char *detail = "negative visitor return aborts with EABORT";
    kc_ngram_options_t opts;
    size_t count;
    int fail;

    fail = 0;

    count = 99;
    fail += expect_int("abort on first returns EABORT", KC_NGRAM_EABORT,
        kc_ngram_execute("a b c", NULL, abort_first_visitor, NULL, &count));
    fail += expect_int("abort on first emits 0", 0, (int)count);

    opts = (kc_ngram_options_t){.max_tokens = 2, .min_tokens = 1, .separators = " "};
    count = 99;
    fail += expect_int("abort on third returns EABORT", KC_NGRAM_EABORT,
        kc_ngram_execute("a b c", &opts, abort_third_visitor, NULL, &count));
    fail += expect_int("abort on third emits 2", 2, (int)count);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_version(void) {
    const char *name = "kc_ngram_version";
    const char *detail = "returns a non-zero build timestamp";
    int fail = expect_true(name, kc_ngram_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 10;
    test_case_current = 0;
    run_case(&rc, case_kc_ngram_options_default);
    run_case(&rc, case_kc_ngram_execute_validation);
    run_case(&rc, case_kc_ngram_execute_empty);
    run_case(&rc, case_kc_ngram_execute_order);
    run_case(&rc, case_kc_ngram_execute_chunk);
    run_case(&rc, case_kc_ngram_execute_bounds);
    run_case(&rc, case_kc_ngram_execute_separators);
    run_case(&rc, case_kc_ngram_execute_closure);
    run_case(&rc, case_kc_ngram_execute_abort);
    run_case(&rc, case_kc_ngram_version);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one named test case.
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
    if (strcmp(argv[1], "kc_ngram_options_default") == 0) return case_kc_ngram_options_default();
    if (strcmp(argv[1], "kc_ngram_execute_validation") == 0) return case_kc_ngram_execute_validation();
    if (strcmp(argv[1], "kc_ngram_execute_empty") == 0) return case_kc_ngram_execute_empty();
    if (strcmp(argv[1], "kc_ngram_execute_order") == 0) return case_kc_ngram_execute_order();
    if (strcmp(argv[1], "kc_ngram_execute_chunk") == 0) return case_kc_ngram_execute_chunk();
    if (strcmp(argv[1], "kc_ngram_execute_bounds") == 0) return case_kc_ngram_execute_bounds();
    if (strcmp(argv[1], "kc_ngram_execute_separators") == 0) return case_kc_ngram_execute_separators();
    if (strcmp(argv[1], "kc_ngram_execute_closure") == 0) return case_kc_ngram_execute_closure();
    if (strcmp(argv[1], "kc_ngram_execute_abort") == 0) return case_kc_ngram_execute_abort();
    if (strcmp(argv[1], "kc_ngram_version") == 0) return case_kc_ngram_version();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
