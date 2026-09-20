/**
 * test.c - libmin public API tests.
 * Summary: Tests each public libmin function through one CTest case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libmin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
 * Verifies one string result.
 * @param name Check description.
 * @param expected Expected string.
 * @param actual Actual string.
 * @return 0 on success, 1 on failure.
 */
static int expect_string(const char *name, const char *expected, const char *actual) {
    if (expected == NULL ? actual != NULL : (actual == NULL || strcmp(expected, actual) != 0)) {
        printf("[FAIL] %s: expected '%s', got '%s'\n", name,
            expected ? expected : "(null)", actual ? actual : "(null)");
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
 * Tests kc_min_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_min_version(void) {
    const char *name = "kc_min_version";
    const char *detail = "returns non-zero build timestamp";
    int fail;

    fail = expect_true("version is non-zero", kc_min_version() != 0);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_min_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_min_open(void) {
    const char *name = "kc_min_open";
    const char *detail = "validates arguments and allocates context";
    kc_min_t *ctx = NULL;
    int fail;

    fail = 0;
    fail += expect_int("open(NULL out) ERROR", KC_MIN_ERROR, kc_min_open(NULL));
    fail += expect_int("open valid OK", KC_MIN_OK, kc_min_open(&ctx));
    fail += expect_true("open sets context", ctx != NULL);
    kc_min_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_min_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_min_close(void) {
    const char *name = "kc_min_close";
    const char *detail = "releases context";
    kc_min_t *ctx = NULL;
    int fail;

    fail = 0;
    if (kc_min_open(&ctx) != KC_MIN_OK) return 1;
    kc_min_close(ctx);
    kc_min_close(NULL);
    fail = expect_true("close does not crash", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_min_set_mode.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_min_set_mode(void) {
    const char *name = "kc_min_set_mode";
    const char *detail = "accepts all render modes";
    kc_min_t *ctx = NULL;
    int fail;

    fail = 0;
    fail += expect_int("set_mode(NULL) ERROR", KC_MIN_ERROR, kc_min_set_mode(NULL, KC_MIN_MODE_CSS));
    if (kc_min_open(&ctx) != KC_MIN_OK) return 1;
    fail += expect_int("set_mode CSS OK", KC_MIN_OK, kc_min_set_mode(ctx, KC_MIN_MODE_CSS));
    fail += expect_int("set_mode JS OK", KC_MIN_OK, kc_min_set_mode(ctx, KC_MIN_MODE_JS));
    fail += expect_int("set_mode HTML OK", KC_MIN_OK, kc_min_set_mode(ctx, KC_MIN_MODE_HTML));
    fail += expect_int("set_mode invalid ERROR", KC_MIN_ERROR, kc_min_set_mode(ctx, 42));
    fail += expect_int("set_mode NONE ERROR", KC_MIN_ERROR, kc_min_set_mode(ctx, KC_MIN_MODE_NONE));
    kc_min_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_min_mode.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_min_mode(void) {
    const char *name = "kc_min_mode";
    const char *detail = "parses names to constants";
    int fail;

    fail = 0;
    fail += expect_int("mode(NULL) NONE", KC_MIN_MODE_NONE, kc_min_mode(NULL));
    fail += expect_int("mode(css) CSS", KC_MIN_MODE_CSS, kc_min_mode("css"));
    fail += expect_int("mode(js) JS", KC_MIN_MODE_JS, kc_min_mode("js"));
    fail += expect_int("mode(html) HTML", KC_MIN_MODE_HTML, kc_min_mode("html"));
    fail += expect_int("mode(invalid) NONE", KC_MIN_MODE_NONE, kc_min_mode("invalid"));
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_min_exec.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_min_exec(void) {
    const char *name = "kc_min_exec";
    const char *detail = "minifies in all modes";
    kc_min_t *ctx = NULL;
    char *out = NULL;
    int fail;

    fail = 0;
    fail += expect_int("exec(NULL ctx) ERROR", KC_MIN_ERROR, kc_min_exec(NULL, "x", &out));
    if (kc_min_open(&ctx) != KC_MIN_OK) return 1;
    fail += expect_int("exec(NULL input) ERROR", KC_MIN_ERROR, kc_min_exec(ctx, NULL, &out));
    fail += expect_int("exec(NULL output) ERROR", KC_MIN_ERROR, kc_min_exec(ctx, "x", NULL));

    fail += expect_int("exec css OK", KC_MIN_OK, kc_min_exec(ctx, "/* hi */ body{}", &out));
    fail += expect_string("exec css output", "body{}", out);
    kc_min_free(out);
    out = NULL;
    fail += expect_int("exec css units OK", KC_MIN_OK, kc_min_exec(ctx, "a { margin: 0px 0% 0pt; }", &out));
    fail += expect_string("exec css units output", "a{margin:0 0 0}", out);
    kc_min_free(out);
    out = NULL;
    fail += expect_int("exec css calc OK", KC_MIN_OK, kc_min_exec(ctx, "a { width: calc(100% - 10px); }", &out));
    fail += expect_string("exec css calc output", "a{width:calc(100% - 10px)}", out);
    kc_min_free(out);
    out = NULL;

    kc_min_set_mode(ctx, KC_MIN_MODE_JS);
    fail += expect_int("exec js OK", KC_MIN_OK, kc_min_exec(ctx, "const x = 1; // comment", &out));
    fail += expect_string("exec js output", "const x = 1;", out);
    kc_min_free(out);
    out = NULL;
    fail += expect_int("exec js template OK", KC_MIN_OK,
        kc_min_exec(ctx, "const t = `a  b`;", &out));
    fail += expect_string("exec js template output", "const t = `a  b`;", out);
    kc_min_free(out);
    out = NULL;
    fail += expect_int("exec js regex OK", KC_MIN_OK,
        kc_min_exec(ctx, "var re = /a[bc]d/g;", &out));
    fail += expect_string("exec js regex output", "var re = /a[bc]d/g;", out);
    kc_min_free(out);
    out = NULL;
    fail += expect_int("exec js divide OK", KC_MIN_OK, kc_min_exec(ctx, "var d = a / b;", &out));
    fail += expect_string("exec js divide output", "var d = a / b;", out);
    kc_min_free(out);
    out = NULL;

    kc_min_set_mode(ctx, KC_MIN_MODE_HTML);
    fail += expect_int("exec html OK", KC_MIN_OK, kc_min_exec(ctx, "<!-- c --><p>hi</p>", &out));
    fail += expect_string("exec html output", "<p>hi</p>", out);
    kc_min_free(out);
    out = NULL;
    fail += expect_int("exec html pre OK", KC_MIN_OK, kc_min_exec(ctx, "<pre>  a  </pre>", &out));
    fail += expect_string("exec html pre output", "<pre>  a  </pre>", out);
    kc_min_free(out);
    out = NULL;
    fail += expect_int("exec html textarea OK", KC_MIN_OK,
        kc_min_exec(ctx, "<textarea>  x  </textarea>", &out));
    fail += expect_string("exec html textarea output", "<textarea>  x  </textarea>", out);
    kc_min_free(out);
    out = NULL;

    fail += expect_int("exec empty OK", KC_MIN_OK, kc_min_exec(ctx, "", &out));
    fail += expect_true("exec empty owned output", out != NULL);
    fail += expect_string("exec empty output", "", out);
    kc_min_free(out);
    out = NULL;

    kc_min_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_min_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_min_free(void) {
    const char *name = "kc_min_free";
    const char *detail = "releases library output";
    char *s;
    int fail;

    s = (char *)malloc(5);
    if (s) {
        strcpy(s, "test");
    }
    kc_min_free(s);
    kc_min_free(NULL);
    fail = expect_true("free does not crash", 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests multiple contexts coexist.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_min_multictx(void) {
    const char *name = "kc_min_multictx";
    const char *detail = "contexts coexist independently";
    kc_min_t *a = NULL;
    kc_min_t *b = NULL;
    char *out_a = NULL;
    char *out_b = NULL;
    int fail;

    fail = 0;
    if (kc_min_open(&a) != KC_MIN_OK) return 1;
    if (kc_min_open(&b) != KC_MIN_OK) {
        kc_min_close(a);
        return 1;
    }
    kc_min_set_mode(a, KC_MIN_MODE_HTML);
    kc_min_set_mode(b, KC_MIN_MODE_JS);
    kc_min_exec(a, "<!-- c --><p>hi</p>", &out_a);
    kc_min_exec(b, "var x = 1; // c", &out_b);
    fail += expect_string("ctx a mode isolated", "<p>hi</p>", out_a);
    fail += expect_string("ctx b mode isolated", "var x = 1;", out_b);
    kc_min_free(out_a);
    kc_min_free(out_b);
    kc_min_close(a);
    kc_min_close(b);
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
    run_case(&rc, case_kc_min_version);
    run_case(&rc, case_kc_min_open);
    run_case(&rc, case_kc_min_close);
    run_case(&rc, case_kc_min_set_mode);
    run_case(&rc, case_kc_min_mode);
    run_case(&rc, case_kc_min_exec);
    run_case(&rc, case_kc_min_free);
    run_case(&rc, case_kc_min_multictx);
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
    if (strcmp(argv[1], "kc_min_version") == 0) return case_kc_min_version();
    if (strcmp(argv[1], "kc_min_open") == 0) return case_kc_min_open();
    if (strcmp(argv[1], "kc_min_close") == 0) return case_kc_min_close();
    if (strcmp(argv[1], "kc_min_set_mode") == 0) return case_kc_min_set_mode();
    if (strcmp(argv[1], "kc_min_mode") == 0) return case_kc_min_mode();
    if (strcmp(argv[1], "kc_min_exec") == 0) return case_kc_min_exec();
    if (strcmp(argv[1], "kc_min_free") == 0) return case_kc_min_free();
    if (strcmp(argv[1], "kc_min_multictx") == 0) return case_kc_min_multictx();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
