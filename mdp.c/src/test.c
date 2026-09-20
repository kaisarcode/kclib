/**
 * test.c - libmdp public API tests.
 * Summary: Tests each public libmdp function through one C case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libmdp.h"

#include <stdio.h>
#include <string.h>

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
 * Verifies a string result.
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

/**
 * Tests kc_mdp_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_mdp_version(void) {
    const char *name = "kc_mdp_version";
    const char *detail = "returns non-zero build timestamp";
    uint64_t v = kc_mdp_version();
    int fail = expect_true("version non-zero", v != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mdp_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_mdp_open(void) {
    const char *name = "kc_mdp_open";
    const char *detail = "validates arguments and allocates context";
    kc_mdp_t *ctx = NULL;
    int fail = expect_int("open NULL out returns ERROR",
        KC_MDP_ERROR, kc_mdp_open(NULL));
    fail += expect_int("open returns OK", KC_MDP_OK, kc_mdp_open(&ctx));
    fail += expect_true("open creates context", ctx != NULL);
    kc_mdp_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mdp_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_mdp_close(void) {
    const char *name = "kc_mdp_close";
    const char *detail = "releases context";
    kc_mdp_t *ctx = NULL;
    int fail = 0;
    kc_mdp_close(NULL);
    fail += expect_true("close NULL returns OK", 1);
    fail += expect_int("open returns OK", KC_MDP_OK, kc_mdp_open(&ctx));
    kc_mdp_close(ctx);
    fail += expect_true("close released context", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mdp_set_mode.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_mdp_set_mode(void) {
    const char *name = "kc_mdp_set_mode";
    const char *detail = "accepts all render modes";
    kc_mdp_t *ctx = NULL;
    int fail = expect_int("set_mode NULL returns ERROR", KC_MDP_ERROR,
        kc_mdp_set_mode(NULL, KC_MDP_MODE_HTML));
    fail += expect_int("open returns OK", KC_MDP_OK, kc_mdp_open(&ctx));
    fail += expect_int("set_mode HTML returns OK", KC_MDP_OK,
        kc_mdp_set_mode(ctx, KC_MDP_MODE_HTML));
    fail += expect_int("set_mode BODY returns OK", KC_MDP_OK,
        kc_mdp_set_mode(ctx, KC_MDP_MODE_BODY));
    fail += expect_int("set_mode META returns OK", KC_MDP_OK,
        kc_mdp_set_mode(ctx, KC_MDP_MODE_META));
    fail += expect_int("set_mode invalid returns ERROR", KC_MDP_ERROR,
        kc_mdp_set_mode(ctx, 99));
    kc_mdp_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mdp_mode.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_mdp_mode(void) {
    const char *name = "kc_mdp_mode";
    const char *detail = "parses names and flags to constants";
    int fail = expect_int("mode NULL returns NONE",
        KC_MDP_MODE_NONE, kc_mdp_mode(NULL));
    fail += expect_int("mode html returns HTML", KC_MDP_MODE_HTML,
        kc_mdp_mode("html"));
    fail += expect_int("mode --html returns HTML", KC_MDP_MODE_HTML,
        kc_mdp_mode("--html"));
    fail += expect_int("mode body returns BODY", KC_MDP_MODE_BODY,
        kc_mdp_mode("body"));
    fail += expect_int("mode --body returns BODY", KC_MDP_MODE_BODY,
        kc_mdp_mode("--body"));
    fail += expect_int("mode meta returns META", KC_MDP_MODE_META,
        kc_mdp_mode("meta"));
    fail += expect_int("mode --meta returns META", KC_MDP_MODE_META,
        kc_mdp_mode("--meta"));
    fail += expect_int("mode invalid returns NONE", KC_MDP_MODE_NONE,
        kc_mdp_mode("invalid"));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mdp_exec.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_mdp_exec(void) {
    const char *name = "kc_mdp_exec";
    const char *detail = "renders and extracts in all modes";
    kc_mdp_t *ctx = NULL;
    unsigned char *out = NULL;
    size_t out_len = 0;
    int fail = expect_int("exec NULL ctx returns ERROR", KC_MDP_ERROR,
        kc_mdp_exec(NULL, "# Hello", &out, &out_len));
    fail += expect_int("open returns OK", KC_MDP_OK, kc_mdp_open(&ctx));
    fail += expect_int("exec NULL input returns ERROR", KC_MDP_ERROR,
        kc_mdp_exec(ctx, NULL, &out, &out_len));
    fail += expect_int("exec NULL out returns ERROR", KC_MDP_ERROR,
        kc_mdp_exec(ctx, "# Hello", NULL, &out_len));
    fail += expect_int("exec NULL out_len returns ERROR", KC_MDP_ERROR,
        kc_mdp_exec(ctx, "# Hello", &out, NULL));

    fail += expect_int("exec html returns OK", KC_MDP_OK,
        kc_mdp_exec(ctx, "# Hello", &out, &out_len));
    fail += expect_string("exec html renders heading",
        "<h1>Hello</h1>\n", (const char *)out);
    kc_mdp_free(out);
    out = NULL;

    fail += expect_int("set body returns OK", KC_MDP_OK,
        kc_mdp_set_mode(ctx, KC_MDP_MODE_BODY));
    fail += expect_int("exec body returns OK", KC_MDP_OK,
        kc_mdp_exec(ctx, "---\ntitle: Home\n---\n# Hello", &out, &out_len));
    fail += expect_string("exec body strips frontmatter",
        "# Hello", (const char *)out);
    kc_mdp_free(out);
    out = NULL;

    fail += expect_int("set meta returns OK", KC_MDP_OK,
        kc_mdp_set_mode(ctx, KC_MDP_MODE_META));
    fail += expect_int("exec meta returns OK", KC_MDP_OK,
        kc_mdp_exec(ctx, "---\ntitle: Home\n---\n# Hello", &out, &out_len));
    fail += expect_string("exec meta returns frontmatter",
        "title: Home", (const char *)out);
    kc_mdp_free(out);
    out = NULL;

    kc_mdp_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_mdp_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_mdp_free(void) {
    const char *name = "kc_mdp_free";
    const char *detail = "releases mdp output and accepts NULL";
    kc_mdp_t *ctx = NULL;
    unsigned char *out = NULL;
    size_t out_len = 0;
    int fail = expect_int("open returns OK", KC_MDP_OK, kc_mdp_open(&ctx));
    fail += expect_int("exec returns OK", KC_MDP_OK,
        kc_mdp_exec(ctx, "# Hello", &out, &out_len));
    fail += expect_true("exec returns output", out != NULL);
    kc_mdp_free(out);
    kc_mdp_free(NULL);
    kc_mdp_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests multiple contexts coexist.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_mdp_multictx(void) {
    const char *name = "kc_mdp_multictx";
    const char *detail = "contexts keep independent modes";
    kc_mdp_t *a = NULL;
    kc_mdp_t *b = NULL;
    unsigned char *out_a = NULL;
    unsigned char *out_b = NULL;
    size_t out_a_len = 0;
    size_t out_b_len = 0;
    int fail = expect_int("open a returns OK", KC_MDP_OK,
        kc_mdp_open(&a));
    fail += expect_int("open b returns OK", KC_MDP_OK,
        kc_mdp_open(&b));
    fail += expect_int("set a BODY returns OK", KC_MDP_OK,
        kc_mdp_set_mode(a, KC_MDP_MODE_BODY));
    fail += expect_int("set b META returns OK", KC_MDP_OK,
        kc_mdp_set_mode(b, KC_MDP_MODE_META));
    fail += expect_int("exec a returns OK", KC_MDP_OK,
        kc_mdp_exec(a, "---\ntitle: Home\n---\n# Hello", &out_a, &out_a_len));
    fail += expect_int("exec b returns OK", KC_MDP_OK,
        kc_mdp_exec(b, "---\ntitle: Home\n---\n# Hello", &out_b, &out_b_len));
    fail += expect_string("a keeps BODY mode", "# Hello", (const char *)out_a);
    fail += expect_string("b keeps META mode", "title: Home", (const char *)out_b);
    kc_mdp_free(out_a);
    kc_mdp_free(out_b);
    kc_mdp_close(a);
    kc_mdp_close(b);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 8;
    test_case_current = 0;
    run_case(&rc, case_kc_mdp_version);
    run_case(&rc, case_kc_mdp_open);
    run_case(&rc, case_kc_mdp_close);
    run_case(&rc, case_kc_mdp_set_mode);
    run_case(&rc, case_kc_mdp_mode);
    run_case(&rc, case_kc_mdp_exec);
    run_case(&rc, case_kc_mdp_free);
    run_case(&rc, case_kc_mdp_multictx);
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
    if (strcmp(argv[1], "kc_mdp_version") == 0) return case_kc_mdp_version();
    if (strcmp(argv[1], "kc_mdp_open") == 0) return case_kc_mdp_open();
    if (strcmp(argv[1], "kc_mdp_close") == 0) return case_kc_mdp_close();
    if (strcmp(argv[1], "kc_mdp_set_mode") == 0) return case_kc_mdp_set_mode();
    if (strcmp(argv[1], "kc_mdp_mode") == 0) return case_kc_mdp_mode();
    if (strcmp(argv[1], "kc_mdp_exec") == 0) return case_kc_mdp_exec();
    if (strcmp(argv[1], "kc_mdp_free") == 0) return case_kc_mdp_free();
    if (strcmp(argv[1], "kc_mdp_multictx") == 0) return case_kc_mdp_multictx();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
