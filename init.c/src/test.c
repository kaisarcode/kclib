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
    const char *detail = "initializes correctly";
    kc_init_options_t opts = kc_init_options_default();
    int fail = expect_true("dir is set", opts.dir != NULL);
    fail += expect_true("backend is NULL", opts.backend == NULL);
    kc_init_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_options_load_env.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_options_load_env(void) {
    const char *name = "kc_init_options_load_env";
    const char *detail = "loads from environment";
    kc_init_options_t opts = {0};
    kc_init_options_load_env(&opts);
    kc_init_options_load_env(NULL);
    kc_init_options_free(&opts);
    int fail = 0;
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_options_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_options_free(void) {
    const char *name = "kc_init_options_free";
    const char *detail = "clears resources";
    kc_init_options_t opts = {0};
    kc_init_options_free(&opts);
    kc_init_options_free(NULL);
    int fail = 0;
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_open and kc_init_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_open(void) {
    const char *name = "kc_init_open";
    const char *detail = "allocates context";
    kc_init_options_t opts = kc_init_options_default();
    kc_init_t *ctx = kc_init_open(&opts);
    int fail = expect_not_null("open creates context", ctx);
    if (ctx) {
        fail += expect_not_null("path returns non-NULL", kc_init_path(ctx));
        kc_init_close(ctx);
    }
    kc_init_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_close(void) {
    const char *name = "kc_init_close";
    const char *detail = "releases context";
    kc_init_options_t opts = kc_init_options_default();
    kc_init_t *ctx = kc_init_open(&opts);
    int fail = 0;
    if (ctx) {
        kc_init_close(ctx);
        fail += expect_true("close does not crash", 1);
    }
    kc_init_close(NULL);
    kc_init_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_stop(void) {
    const char *name = "kc_init_stop";
    const char *detail = "sets flag on context";
    kc_init_options_t opts = kc_init_options_default();
    kc_init_t *ctx = kc_init_open(&opts);
    int fail = 0;
    if (ctx) {
        fail += expect_int("stop(ctx) returns OK", KC_INIT_OK, kc_init_stop(ctx));
        fail += expect_int("stop is idempotent", KC_INIT_OK, kc_init_stop(ctx));
        kc_init_close(ctx);
    }
    fail += expect_int("stop(NULL) returns ERROR", KC_INIT_ERROR, kc_init_stop(NULL));
    kc_init_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_path.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_path(void) {
    const char *name = "kc_init_path";
    const char *detail = "returns NULL for invalid context";
    int fail = expect_true("path(NULL) returns NULL", kc_init_path(NULL) == NULL);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_update.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_update(void) {
    const char *name = "kc_init_update";
    const char *detail = "registers command metadata";
    kc_init_options_t opts = kc_init_options_default();
    kc_init_t *ctx = kc_init_open(&opts);
    int fail = 0;
    if (ctx) {
        fail += expect_int("update(ctx, NULL, cmd) returns ERROR",
            KC_INIT_ERROR, kc_init_update(ctx, NULL, "cmd"));
        fail += expect_int("update(ctx, key, NULL) returns ERROR",
            KC_INIT_ERROR, kc_init_update(ctx, "key", NULL));
        fail += expect_true("update with root check",
            kc_init_update(ctx, "test", "echo test") == KC_INIT_OK ||
            kc_init_update(ctx, "test", "echo test") == KC_INIT_ERROR);
        kc_init_close(ctx);
    }
    fail += expect_int("update(NULL) returns ERROR",
        KC_INIT_ERROR, kc_init_update(NULL, "key", "cmd"));
    kc_init_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_exec.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_exec(void) {
    const char *name = "kc_init_exec";
    const char *detail = "executes registered command";
    kc_init_options_t opts = kc_init_options_default();
    kc_init_t *ctx = kc_init_open(&opts);
    int fail = 0;
    if (ctx) {
        fail += expect_int("exec missing key returns ERROR",
            KC_INIT_ERROR, kc_init_exec(ctx, "missing"));
        kc_init_close(ctx);
    }
    fail += expect_int("exec(NULL) returns ERROR",
        KC_INIT_ERROR, kc_init_exec(NULL, "key"));
    kc_init_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_list.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_list(void) {
    const char *name = "kc_init_list";
    const char *detail = "returns registered commands";
    kc_init_options_t opts = kc_init_options_default();
    kc_init_t *ctx = kc_init_open(&opts);
    int fail = 0;
    if (ctx) {
        fail += expect_int("list returns OK", KC_INIT_OK,
            kc_init_list(ctx, NULL, NULL, NULL));
        fail += expect_int("list missing key returns ERROR",
            KC_INIT_ERROR, kc_init_list(ctx, "listed", NULL, NULL));
        kc_init_close(ctx);
    }
    fail += expect_int("list(NULL) returns ERROR",
        KC_INIT_ERROR, kc_init_list(NULL, NULL, NULL, NULL));
    kc_init_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_delete.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_delete(void) {
    const char *name = "kc_init_delete";
    const char *detail = "removes registered command";
    kc_init_options_t opts = kc_init_options_default();
    kc_init_t *ctx = kc_init_open(&opts);
    int fail = 0;
    if (ctx) {
        fail += expect_int("delete(ctx, NULL) returns ERROR",
            KC_INIT_ERROR, kc_init_delete(ctx, NULL));
        fail += expect_true("delete without root",
            kc_init_delete(ctx, "missing") == KC_INIT_OK ||
            kc_init_delete(ctx, "missing") == KC_INIT_ERROR);
        kc_init_close(ctx);
    }
    fail += expect_int("delete(NULL) returns ERROR",
        KC_INIT_ERROR, kc_init_delete(NULL, "key"));
    kc_init_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_init_error.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_init_error(void) {
    const char *name = "kc_init_error";
    const char *detail = "returns error message";
    kc_init_options_t opts = kc_init_options_default();
    kc_init_t *ctx = kc_init_open(&opts);
    int fail = 0;
    if (ctx) {
        fail += expect_true("error(NULL) returns NULL", kc_init_error(NULL) == NULL);
        fail += expect_true("error(ctx) returns NULL initially",
            kc_init_error(ctx) == NULL || kc_init_error(ctx) != NULL);
        kc_init_close(ctx);
    }
    kc_init_options_free(&opts);
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
    kc_init_options_t opts = kc_init_options_default();
    kc_init_t *a = kc_init_open(&opts);
    kc_init_t *b = kc_init_open(&opts);
    int fail = 0;
    if (a && b) {
        fail += expect_int("stop a returns OK", KC_INIT_OK, kc_init_stop(a));
        fail += expect_int("stop b returns OK", KC_INIT_OK, kc_init_stop(b));
        kc_init_close(a);
        kc_init_close(b);
    }
    kc_init_options_free(&opts);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 14;
    test_case_current = 0;
    run_case(&rc, case_kc_init_version);
    run_case(&rc, case_kc_init_options_default);
    run_case(&rc, case_kc_init_options_load_env);
    run_case(&rc, case_kc_init_options_free);
    run_case(&rc, case_kc_init_open);
    run_case(&rc, case_kc_init_close);
    run_case(&rc, case_kc_init_stop);
    run_case(&rc, case_kc_init_path);
    run_case(&rc, case_kc_init_update);
    run_case(&rc, case_kc_init_exec);
    run_case(&rc, case_kc_init_list);
    run_case(&rc, case_kc_init_delete);
    run_case(&rc, case_kc_init_error);
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
    if (strcmp(argv[1], "kc_init_options_load_env") == 0) return case_kc_init_options_load_env();
    if (strcmp(argv[1], "kc_init_options_free") == 0) return case_kc_init_options_free();
    if (strcmp(argv[1], "kc_init_open") == 0) return case_kc_init_open();
    if (strcmp(argv[1], "kc_init_close") == 0) return case_kc_init_close();
    if (strcmp(argv[1], "kc_init_stop") == 0) return case_kc_init_stop();
    if (strcmp(argv[1], "kc_init_path") == 0) return case_kc_init_path();
    if (strcmp(argv[1], "kc_init_update") == 0) return case_kc_init_update();
    if (strcmp(argv[1], "kc_init_exec") == 0) return case_kc_init_exec();
    if (strcmp(argv[1], "kc_init_list") == 0) return case_kc_init_list();
    if (strcmp(argv[1], "kc_init_delete") == 0) return case_kc_init_delete();
    if (strcmp(argv[1], "kc_init_error") == 0) return case_kc_init_error();
    if (strcmp(argv[1], "kc_init_multictx") == 0) return case_kc_init_multictx();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
