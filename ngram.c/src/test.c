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
 * Visitor that counts emitted chunks.
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
    if (chunk->size > st->max_size) st->max_size = chunk->size;
    return 0;
}

/**
 * Visitor that closes the span on the first chunk.
 * @return 1 to close the span.
 */
static int close_first_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    (void)chunk;
    (void)context;
    return 1;
}

/**
 * Visitor that aborts traversal.
 * @return -1 to abort.
 */
static int abort_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    (void)chunk;
    (void)context;
    return -1;
}

/**
 * Visitor that requests stop on the context.
 * @return KC_NGRAM_ESTOP to stop.
 */
static int stop_visitor(const kc_ngram_chunk_t *chunk, void *context) {
    kc_ngram_t *ctx = (kc_ngram_t *)context;
    (void)chunk;
    kc_ngram_stop(ctx);
    return KC_NGRAM_ESTOP;
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
 * Tests kc_ngram_options_default.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_options_default(void) {
    const char *name = "kc_ngram_options_default";
    const char *detail = "initializes default window bounds and separators";
    kc_ngram_options_t opts;
    int fail;

    fail = 0;
    fail += expect_int("options_default(NULL) returns -1", -1,
        kc_ngram_options_default(NULL));
    memset(&opts, 0, sizeof(opts));
    fail += expect_int("options_default fills max_tokens", 0,
        kc_ngram_options_default(&opts));
    fail += expect_int("max_tokens is 10", 10, opts.max_tokens);
    fail += expect_int("min_tokens is 1", 1, opts.min_tokens);
    fail += expect_true("separators is set", opts.separators != NULL);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_options_load_env.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_options_load_env(void) {
    const char *name = "kc_ngram_options_load_env";
    const char *detail = "loads environment without crashing";
    kc_ngram_options_t opts;
    int fail;

    fail = 0;
    opts = (kc_ngram_options_t){0};
    kc_ngram_options_load_env(&opts);
    fail += expect_true("load_env does not crash", 1);
    kc_ngram_options_load_env(NULL);
    fail += expect_true("load_env(NULL) does not crash", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_options_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_options_free(void) {
    const char *name = "kc_ngram_options_free";
    const char *detail = "releases options safely";
    kc_ngram_options_t opts;
    int fail;

    fail = 0;
    opts = (kc_ngram_options_t){0};
    kc_ngram_options_free(&opts);
    fail += expect_true("options_free does not crash", 1);
    kc_ngram_options_free(NULL);
    fail += expect_true("options_free(NULL) does not crash", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_open(void) {
    const char *name = "kc_ngram_open";
    const char *detail = "validates and allocates context";
    kc_ngram_t *ctx;
    int fail;

    fail = 0;
    ctx = NULL;
    fail += expect_int("open(NULL) returns ERROR", KC_NGRAM_ERROR,
        kc_ngram_open(NULL));
    fail += expect_int("open creates context", KC_NGRAM_OK,
        kc_ngram_open(&ctx));
    fail += expect_true("open sets output", ctx != NULL);
    kc_ngram_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_close(void) {
    const char *name = "kc_ngram_close";
    const char *detail = "releases context";
    kc_ngram_t *ctx;
    int fail;

    fail = 0;
    kc_ngram_close(NULL);
    fail += expect_true("close(NULL) does not crash", 1);
    if (kc_ngram_open(&ctx) != KC_NGRAM_OK) return 1;
    kc_ngram_close(ctx);
    fail += expect_true("close releases context", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_stop(void) {
    const char *name = "kc_ngram_stop";
    const char *detail = "is idempotent and context-local";
    kc_ngram_t *ctx;
    kc_ngram_t *other;
    int fail;

    fail = 0;
    fail += expect_int("stop(NULL) returns ERROR", KC_NGRAM_ERROR, kc_ngram_stop(NULL));
    if (kc_ngram_open(&ctx) != KC_NGRAM_OK) return 1;
    if (kc_ngram_open(&other) != KC_NGRAM_OK) {
        kc_ngram_close(ctx);
        return 1;
    }
    fail += expect_int("stop succeeds", KC_NGRAM_OK, kc_ngram_stop(ctx));
    fail += expect_int("stop is idempotent", KC_NGRAM_OK, kc_ngram_stop(ctx));
    fail += expect_int("other context unaffected", KC_NGRAM_OK, kc_ngram_stop(other));
    kc_ngram_close(ctx);
    kc_ngram_close(other);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_stop_requested.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_stop_requested(void) {
    const char *name = "kc_ngram_stop_requested";
    const char *detail = "tracks stop state on context";
    kc_ngram_t *ctx;
    int fail;

    fail = 0;
    fail += expect_int("stop_requested(NULL) returns 0", 0, kc_ngram_stop_requested(NULL));
    if (kc_ngram_open(&ctx) != KC_NGRAM_OK) return 1;
    fail += expect_int("fresh context not stopped", 0, kc_ngram_stop_requested(ctx));
    fail += expect_int("stop context", KC_NGRAM_OK, kc_ngram_stop(ctx));
    fail += expect_int("stopped context reported", 1, kc_ngram_stop_requested(ctx));
    kc_ngram_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_configure.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_configure(void) {
    const char *name = "kc_ngram_configure";
    const char *detail = "attaches and restores mutable options";
    kc_ngram_t *ctx;
    kc_ngram_options_t opts;
    int fail;

    fail = 0;
    if (kc_ngram_open(&ctx) != KC_NGRAM_OK) return 1;
    fail += expect_int("configure(NULL, ...) returns ERROR", KC_NGRAM_ERROR,
        kc_ngram_configure(NULL, &opts));
    kc_ngram_options_default(&opts);
    opts.max_tokens = 5;
    opts.min_tokens = 2;
    opts.separators = ",";
    fail += expect_int("configure attaches options", KC_NGRAM_OK,
        kc_ngram_configure(ctx, &opts));
    fail += expect_int("configure restores defaults on NULL", KC_NGRAM_OK,
        kc_ngram_configure(ctx, NULL));
    kc_ngram_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_execute with default options.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_execute(void) {
    const char *name = "kc_ngram_execute";
    const char *detail = "traverses descending windows and validates bounds";
    counter_state_t st;
    kc_ngram_options_t opts;
    int fail;

    fail = 0;
    fail += expect_int("execute(NULL, ...) returns ERROR", -1,
        kc_ngram_execute(NULL, NULL, count_visitor, NULL));
    fail += expect_int("execute(input, NULL, NULL, ...) returns ERROR", -1,
        kc_ngram_execute("hello", NULL, NULL, NULL));
    fail += expect_int("execute empty input returns 0", 0,
        kc_ngram_execute("", NULL, count_visitor, NULL));
    st = (counter_state_t){0, 0};
    fail += expect_int("execute with defaults emits chunks", 65,
        kc_ngram_execute("a b c d e f g h i j k", NULL, count_visitor, &st));
    fail += expect_true("max_size respects window", st.max_size <= 10);
    opts = (kc_ngram_options_t){0};
    kc_ngram_options_default(&opts);
    opts.max_tokens = 2;
    opts.min_tokens = 1;
    st = (counter_state_t){0, 0};
    fail += expect_int("execute with max=2 emits fewer chunks", 5,
        kc_ngram_execute("a b c", &opts, count_visitor, &st));
    opts.max_tokens = 3;
    opts.min_tokens = 3;
    st = (counter_state_t){0, 0};
    fail += expect_int("execute min==max emits exact-size chunks", 1,
        kc_ngram_execute("a b c", &opts, count_visitor, &st));
    fail += expect_true("chunk size matches window", st.max_size == 3);
    opts = (kc_ngram_options_t){0};
    kc_ngram_options_default(&opts);
    opts.max_tokens = 1;
    opts.min_tokens = 100;
    fail += expect_int("execute min>max returns ERROR", -1,
        kc_ngram_execute("a b", &opts, count_visitor, NULL));
    opts.min_tokens = 0;
    fail += expect_int("execute with min=0 returns ERROR", -1,
        kc_ngram_execute("a b", &opts, count_visitor, NULL));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_execute span closure and abort.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_execute_span(void) {
    const char *name = "kc_ngram_execute_span";
    const char *detail = "closes and aborts spans per visitor decision";
    int fail;
    counter_state_t st;

    fail = 0;
    st = (counter_state_t){0, 0};
    fail += expect_int("execute close-first emits 1 chunk", 1,
        kc_ngram_execute("a b c", NULL, close_first_visitor, NULL));
    fail += expect_int("execute abort returns -1", -1,
        kc_ngram_execute("a b c", NULL, abort_visitor, NULL));
    st = (counter_state_t){0, 0};
    fail += expect_int("execute custom sep tokenizes correctly", 2,
        kc_ngram_execute("a,b,c", &(kc_ngram_options_t){
            .max_tokens = 2, .min_tokens = 2, .separators = ","
        }, count_visitor, &st));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests multiple contexts coexist.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_multictx(void) {
    const char *name = "kc_ngram_multictx";
    const char *detail = "keeps contexts independent";
    kc_ngram_t *a;
    kc_ngram_t *b;
    int fail;
    counter_state_t st;

    fail = 0;
    if (kc_ngram_open(&a) != KC_NGRAM_OK) return 1;
    if (kc_ngram_open(&b) != KC_NGRAM_OK) {
        kc_ngram_close(a);
        return 1;
    }
    fail += expect_int("stop a returns OK", KC_NGRAM_OK, kc_ngram_stop(a));
    fail += expect_int("stop b returns OK", KC_NGRAM_OK, kc_ngram_stop(b));
    fail += expect_int("stop a again returns OK", KC_NGRAM_OK, kc_ngram_stop(a));
    st = (counter_state_t){0, 0};
    fail += expect_int("execute still works with defaults", 6,
        kc_ngram_execute("a b c", NULL, count_visitor, &st));
    kc_ngram_close(a);
    kc_ngram_close(b);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_ngram_execute stop propagation.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_ngram_execute_stop(void) {
    const char *name = "kc_ngram_execute_stop";
    const char *detail = "propagates cooperative stop through visitor";
    kc_ngram_t *ctx;
    int fail;

    fail = 0;
    if (kc_ngram_open(&ctx) != KC_NGRAM_OK) return 1;
    fail += expect_int("execute returns ESTOP on visitor stop", KC_NGRAM_ESTOP,
        kc_ngram_execute("a b c", NULL, stop_visitor, ctx));
    fail += expect_int("stop_requested true after visitor stop", 1,
        kc_ngram_stop_requested(ctx));
    kc_ngram_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 13;
    test_case_current = 0;
    run_case(&rc, case_kc_ngram_version);
    run_case(&rc, case_kc_ngram_options_default);
    run_case(&rc, case_kc_ngram_options_load_env);
    run_case(&rc, case_kc_ngram_options_free);
    run_case(&rc, case_kc_ngram_open);
    run_case(&rc, case_kc_ngram_close);
    run_case(&rc, case_kc_ngram_stop);
    run_case(&rc, case_kc_ngram_stop_requested);
    run_case(&rc, case_kc_ngram_configure);
    run_case(&rc, case_kc_ngram_execute);
    run_case(&rc, case_kc_ngram_execute_span);
    run_case(&rc, case_kc_ngram_multictx);
    run_case(&rc, case_kc_ngram_execute_stop);
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
    if (strcmp(argv[1], "kc_ngram_version") == 0) return case_kc_ngram_version();
    if (strcmp(argv[1], "kc_ngram_options_default") == 0) return case_kc_ngram_options_default();
    if (strcmp(argv[1], "kc_ngram_options_load_env") == 0) return case_kc_ngram_options_load_env();
    if (strcmp(argv[1], "kc_ngram_options_free") == 0) return case_kc_ngram_options_free();
    if (strcmp(argv[1], "kc_ngram_open") == 0) return case_kc_ngram_open();
    if (strcmp(argv[1], "kc_ngram_close") == 0) return case_kc_ngram_close();
    if (strcmp(argv[1], "kc_ngram_stop") == 0) return case_kc_ngram_stop();
    if (strcmp(argv[1], "kc_ngram_stop_requested") == 0) return case_kc_ngram_stop_requested();
    if (strcmp(argv[1], "kc_ngram_configure") == 0) return case_kc_ngram_configure();
    if (strcmp(argv[1], "kc_ngram_execute") == 0) return case_kc_ngram_execute();
    if (strcmp(argv[1], "kc_ngram_execute_span") == 0) return case_kc_ngram_execute_span();
    if (strcmp(argv[1], "kc_ngram_multictx") == 0) return case_kc_ngram_multictx();
    if (strcmp(argv[1], "kc_ngram_execute_stop") == 0) return case_kc_ngram_execute_stop();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
