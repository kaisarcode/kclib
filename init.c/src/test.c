/**
 * test.c - libinit public API tests.
 * Summary: Tests each public libinit function through one C case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#define _POSIX_C_SOURCE 200809L

#include "libinit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Test case description.
 * @param detail Additional detail string.
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
 * Verifies a pointer is not NULL.
 * @param name Check description.
 * @param ptr Pointer to check.
 * @return 0 on success, 1 on failure.
 */
static int expect_not_null(const char *name, const void *ptr) {
    if (!ptr) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

typedef struct {
    void *expected_userdata;
    int in_list;
    int called;
    int userdata_matches;
    int called_during_list;
    int strings_valid;
} list_callback_state_t;

/**
 * Records one synchronous list callback invocation.
 * @param key Borrowed registration key.
 * @param user Borrowed registration user.
 * @param cmd Borrowed registration command.
 * @param userdata Callback state pointer.
 * @return None.
 */
static void record_list_callback(
    const char *key,
    const char *user,
    const char *cmd,
    void *userdata
) {
    list_callback_state_t *state = (list_callback_state_t *)userdata;

    state->called++;
    state->userdata_matches = userdata == state->expected_userdata;
    state->called_during_list = state->in_list;
    state->strings_valid = key && user && cmd &&
        strcmp(key, "test-entry") == 0 && strcmp(cmd, "echo init-test") == 0;
}

/**
 * Tests kc_init_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_version(void) {
    const char *name = "kc_init_version";
    const char *detail = "returns non-zero build timestamp";
    uint64_t v = kc_init_version();
    int fail = expect_true("version non-zero", v != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_options_default.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_options_default(void) {
    const char *name = "kc_init_options_default";
    const char *detail = "creates caller-owned defaults";
    kc_init_options_t *opts = kc_init_options_default();
    int fail = expect_not_null("default options are non-NULL", opts);
    kc_init_options_free(opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_options_set.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_options_set(void) {
    const char *name = "kc_init_options_set";
    const char *detail = "sets and resets supported options";
    kc_init_options_t *opts = kc_init_options_default();
    int fail = expect_not_null("default options are non-NULL", opts);

    fail += expect_int("set rejects NULL options", KC_INIT_ERROR,
        kc_init_options_set(NULL, "dir", "."));
    if (opts) {
        fail += expect_int("set rejects NULL key", KC_INIT_ERROR,
            kc_init_options_set(opts, NULL, "."));
        fail += expect_int("set rejects unknown key", KC_INIT_ERROR,
            kc_init_options_set(opts, "unknown", "."));
        fail += expect_int("set dir succeeds", KC_INIT_OK,
            kc_init_options_set(opts, "dir", "."));
        fail += expect_int("reset dir succeeds", KC_INIT_OK,
            kc_init_options_set(opts, "dir", NULL));
        fail += expect_int("set backend succeeds", KC_INIT_OK,
            kc_init_options_set(opts, "backend", "none"));
        fail += expect_int("reset backend succeeds", KC_INIT_OK,
            kc_init_options_set(opts, "backend", NULL));
    }
    kc_init_options_free(opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_options_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_options_free(void) {
    const char *name = "kc_init_options_free";
    const char *detail = "releases options and accepts NULL";
    kc_init_options_t *opts = kc_init_options_default();
    int fail = expect_not_null("default options are non-NULL", opts);

    kc_init_options_free(opts);
    kc_init_options_free(NULL);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_open and kc_init_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_open(void) {
    const char *name = "kc_init_open";
    const char *detail = "opens defaults and copies explicit options";
    kc_init_options_t *opts;
    kc_init_t *ctx = (kc_init_t *)(uintptr_t)1;
    int fail = expect_int("open rejects NULL output", KC_INIT_ERROR,
        kc_init_open(NULL, NULL));

    fail += expect_int("open with defaults succeeds", KC_INIT_OK,
        kc_init_open(&ctx, NULL));
    fail += expect_not_null("default open creates context", ctx);
    kc_init_close(ctx);

    opts = kc_init_options_default();
    fail += expect_not_null("explicit options are non-NULL", opts);
    ctx = NULL;
    if (opts) {
        fail += expect_int("set explicit dir succeeds", KC_INIT_OK,
            kc_init_options_set(opts, "dir", "."));
        fail += expect_int("open with explicit options succeeds", KC_INIT_OK,
            kc_init_open(&ctx, opts));
    }
    kc_init_options_free(opts);
    fail += expect_not_null("explicit open creates context", ctx);
    if (ctx) {
        fail += expect_true("context copied explicit dir",
            strcmp(kc_init_path(ctx), ".") == 0);
    }
    kc_init_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_close(void) {
    const char *name = "kc_init_close";
    const char *detail = "releases a context and accepts NULL";
    kc_init_t *ctx = NULL;
    int fail = expect_int("open succeeds", KC_INIT_OK,
        kc_init_open(&ctx, NULL));

    fail += expect_not_null("open creates context", ctx);
    kc_init_close(ctx);
    kc_init_close(NULL);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_path.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_path(void) {
    const char *name = "kc_init_path";
    const char *detail = "returns borrowed const-compatible path";
    kc_init_options_t *opts = kc_init_options_default();
    kc_init_t *ctx = NULL;
    const kc_init_t *view;
    int fail = expect_true("path(NULL) returns NULL", kc_init_path(NULL) == NULL);

    fail += expect_not_null("options are non-NULL", opts);
    if (opts) {
        fail += expect_int("set dir succeeds", KC_INIT_OK,
            kc_init_options_set(opts, "dir", "."));
        fail += expect_int("open succeeds", KC_INIT_OK,
            kc_init_open(&ctx, opts));
    }
    view = ctx;
    if (view) {
        fail += expect_true("const context path matches explicit dir",
            strcmp(kc_init_path(view), ".") == 0);
    }
    kc_init_close(ctx);
    kc_init_options_free(opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_update.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_update(void) {
    const char *name = "kc_init_update";
    const char *detail = "rejects invalid updates without writing metadata";
    kc_init_t *ctx = NULL;
    int fail = expect_int("open succeeds", KC_INIT_OK,
        kc_init_open(&ctx, NULL));

    if (ctx) {
        fail += expect_int("update(ctx, NULL, cmd) returns ERROR",
            KC_INIT_ERROR, kc_init_update(ctx, NULL, "cmd"));
        fail += expect_int("update(ctx, key, NULL) returns ERROR",
            KC_INIT_ERROR, kc_init_update(ctx, "key", NULL));
        kc_init_close(ctx);
    }
    fail += expect_int("update(NULL) returns ERROR",
        KC_INIT_ERROR, kc_init_update(NULL, "key", "cmd"));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_exec.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_exec(void) {
    const char *name = "kc_init_exec";
    const char *detail = "reports a missing registered command";
    kc_init_options_t *opts = kc_init_options_default();
    kc_init_t *ctx = NULL;
    int fail = expect_not_null("options are non-NULL", opts);

    if (opts) {
        fail += expect_int("set local dir succeeds", KC_INIT_OK,
            kc_init_options_set(opts, "dir", "."));
        fail += expect_int("open succeeds", KC_INIT_OK,
            kc_init_open(&ctx, opts));
    }
    if (ctx) {
        fail += expect_int("exec missing key returns ERROR",
            KC_INIT_ERROR,
            kc_init_exec(ctx, "__kc_init_test_missing_entry__"));
        kc_init_close(ctx);
    }
    fail += expect_int("exec(NULL) returns ERROR",
        KC_INIT_ERROR, kc_init_exec(NULL, "key"));
    kc_init_options_free(opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_list.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_list(void) {
    const char *name = "kc_init_list";
    const char *detail = "invokes callback synchronously with userdata";
    kc_init_options_t *opts = kc_init_options_default();
    kc_init_t *ctx = NULL;
    list_callback_state_t state = {0};
    int fail = expect_not_null("options are non-NULL", opts);

    if (opts) {
        fail += expect_int("set local dir succeeds", KC_INIT_OK,
            kc_init_options_set(opts, "dir", "/tmp/tmp.wg2fMbTBE8"));
        fail += expect_int("open succeeds", KC_INIT_OK,
            kc_init_open(&ctx, opts));
    }
    if (ctx) {
        state.expected_userdata = &state;
        state.in_list = 1;
        fail += expect_int("list existing metadata succeeds", KC_INIT_OK,
            kc_init_list(ctx, "test-entry", record_list_callback,
                &state));
        state.in_list = 0;
        fail += expect_int("callback runs exactly once", 1, state.called);
        fail += expect_true("callback receives exact userdata",
            state.userdata_matches);
        fail += expect_true("callback runs during list call",
            state.called_during_list);
        fail += expect_true("callback strings are valid during invocation",
            state.strings_valid);
        fail += expect_int("NULL callback remains supported", KC_INIT_OK,
            kc_init_list(ctx, "test-entry", NULL, &state));
        fail += expect_int("list missing key returns ERROR",
            KC_INIT_ERROR,
            kc_init_list(ctx, "__kc_init_test_missing_entry__", NULL, NULL));
        kc_init_close(ctx);
    }
    fail += expect_int("list(NULL) returns ERROR",
        KC_INIT_ERROR, kc_init_list(NULL, NULL, NULL, NULL));
    kc_init_options_free(opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_delete.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_delete(void) {
    const char *name = "kc_init_delete";
    const char *detail = "rejects invalid deletes without mutation";
    kc_init_t *ctx = NULL;
    int fail = expect_int("open succeeds", KC_INIT_OK,
        kc_init_open(&ctx, NULL));

    if (ctx) {
        fail += expect_int("delete(ctx, NULL) returns ERROR",
            KC_INIT_ERROR, kc_init_delete(ctx, NULL));
        kc_init_close(ctx);
    }
    fail += expect_int("delete(NULL) returns ERROR",
        KC_INIT_ERROR, kc_init_delete(NULL, "key"));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_get_error.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_get_error(void) {
    const char *name = "kc_init_get_error";
    const char *detail = "reports context operation failures";
    kc_init_options_t *opts = kc_init_options_default();
    kc_init_t *ctx = NULL;
    int fail = expect_true("get_error(NULL) returns NULL",
        kc_init_get_error(NULL) == NULL);

    fail += expect_not_null("options are non-NULL", opts);
    if (opts) {
        fail += expect_int("set local dir succeeds", KC_INIT_OK,
            kc_init_options_set(opts, "dir", "."));
        fail += expect_int("open succeeds", KC_INIT_OK,
            kc_init_open(&ctx, opts));
    }
    if (ctx) {
        fail += expect_true("fresh context has no error",
            kc_init_get_error(ctx) == NULL);
        fail += expect_int("missing list entry returns ERROR", KC_INIT_ERROR,
            kc_init_list(ctx, "__kc_init_test_missing_entry__", NULL, NULL));
        fail += expect_not_null("failed operation sets error",
            kc_init_get_error(ctx));
        kc_init_close(ctx);
    }
    kc_init_options_free(opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests two contexts coexist independently.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_multictx(void) {
    const char *name = "kc_init_multictx";
    const char *detail = "two contexts coexist independently";
    kc_init_options_t *opts_a = kc_init_options_default();
    kc_init_options_t *opts_b = kc_init_options_default();
    kc_init_t *a = NULL;
    kc_init_t *b = NULL;
    int fail = expect_not_null("options a are non-NULL", opts_a);

    fail += expect_not_null("options b are non-NULL", opts_b);
    if (opts_a && opts_b) {
        fail += expect_int("set dir a succeeds", KC_INIT_OK,
            kc_init_options_set(opts_a, "dir", "."));
        fail += expect_int("set dir b succeeds", KC_INIT_OK,
            kc_init_options_set(opts_b, "dir", ".."));
        fail += expect_int("open a succeeds", KC_INIT_OK,
            kc_init_open(&a, opts_a));
        fail += expect_int("open b succeeds", KC_INIT_OK,
            kc_init_open(&b, opts_b));
    }
    kc_init_options_free(opts_a);
    kc_init_options_free(opts_b);
    if (a && b) {
        fail += expect_true("a retains its own path",
            strcmp(kc_init_path(a), ".") == 0);
        fail += expect_true("b retains its own path",
            strcmp(kc_init_path(b), "..") == 0);
        kc_init_close(a);
        a = NULL;
        fail += expect_true("b remains usable after closing a",
            strcmp(kc_init_path(b), "..") == 0);
        fail += expect_int("b can still list after closing a", KC_INIT_OK,
            kc_init_list(b, NULL, NULL, NULL));
        kc_init_close(b);
        b = NULL;
    }
    if (a) kc_init_close(a);
    if (b) kc_init_close(b);
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
    run_case(&rc, case_kc_init_version);
    run_case(&rc, case_kc_init_options_default);
    run_case(&rc, case_kc_init_options_set);
    run_case(&rc, case_kc_init_options_free);
    run_case(&rc, case_kc_init_open);
    run_case(&rc, case_kc_init_close);
    run_case(&rc, case_kc_init_path);
    run_case(&rc, case_kc_init_update);
    run_case(&rc, case_kc_init_exec);
    run_case(&rc, case_kc_init_list);
    run_case(&rc, case_kc_init_delete);
    run_case(&rc, case_kc_init_get_error);
    run_case(&rc, case_kc_init_multictx);
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
    if (strcmp(argv[1], "kc_init_version") == 0) return case_kc_init_version();
    if (strcmp(argv[1], "kc_init_options_default") == 0) return case_kc_init_options_default();
    if (strcmp(argv[1], "kc_init_options_set") == 0) return case_kc_init_options_set();
    if (strcmp(argv[1], "kc_init_options_free") == 0) return case_kc_init_options_free();
    if (strcmp(argv[1], "kc_init_open") == 0) return case_kc_init_open();
    if (strcmp(argv[1], "kc_init_close") == 0) return case_kc_init_close();
    if (strcmp(argv[1], "kc_init_path") == 0) return case_kc_init_path();
    if (strcmp(argv[1], "kc_init_update") == 0) return case_kc_init_update();
    if (strcmp(argv[1], "kc_init_exec") == 0) return case_kc_init_exec();
    if (strcmp(argv[1], "kc_init_list") == 0) return case_kc_init_list();
    if (strcmp(argv[1], "kc_init_delete") == 0) return case_kc_init_delete();
    if (strcmp(argv[1], "kc_init_get_error") == 0) return case_kc_init_get_error();
    if (strcmp(argv[1], "kc_init_multictx") == 0) return case_kc_init_multictx();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
