/**
 * test.c - libmin public API tests.
 * Summary: Contract tests for the stateless min API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libmin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints one canonical test-case result line.
 * @param fail Nonzero when the case failed.
 * @param name Canonical test-case name.
 * @param description Human-readable test-case description.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *description) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, description);
}

typedef int (*case_fn)(void);

/**
 * Executes one test case and accumulates its result.
 * @param rc Aggregate failed-case count.
 * @param fn Test-case function to execute.
 * @return None.
 */
static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

/**
 * Verifies one boolean test expectation.
 * @param name Expectation description.
 * @param condition Nonzero when the expectation is satisfied.
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
 * Verifies one string test expectation.
 * @param name Expectation description.
 * @param expected Expected string value.
 * @param actual Actual string value.
 * @return 0 on success, 1 on failure.
 */
static int expect_string(const char *name, const char *expected, const char *actual) {
    if (!actual || strcmp(expected, actual) != 0) {
        printf("[FAIL] %s: expected '%s', got '%s'\n", name, expected,
            actual ? actual : "NULL");
        return 1;
    }
    return 0;
}

/**
 * Tests CSS minification.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_min_css(void) {
    char *out;
    int fail = 0;

    fail += expect_true("css NULL returns NULL", kc_min_css(NULL) == NULL);

    out = kc_min_css("");
    fail += expect_true("css empty returns allocation", out != NULL);
    if (out) fail += expect_string("css empty returns empty string", "", out);
    kc_min_free(out);

    out = kc_min_css("/* hi */ body{}");
    fail += expect_string("CSS comments removed", "body{}", out);
    kc_min_free(out);

    out = kc_min_css("a { margin: 0px 0% 0pt; }");
    fail += expect_string("CSS zero units removed", "a{margin:0 0 0}", out);
    kc_min_free(out);

    out = kc_min_css("a { width: calc(100% - 10px); }");
    fail += expect_string("CSS calc spacing preserved",
        "a{width:calc(100% - 10px)}", out);
    kc_min_free(out);

    case_result(fail, "kc_min_css",
        "minifies CSS with the conservative scanner contract");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests JavaScript minification.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_min_js(void) {
    char *out;
    int fail = 0;

    fail += expect_true("js NULL returns NULL", kc_min_js(NULL) == NULL);

    out = kc_min_js("");
    fail += expect_true("js empty returns allocation", out != NULL);
    if (out) fail += expect_string("js empty returns empty string", "", out);
    kc_min_free(out);

    out = kc_min_js("const x = 1; // comment");
    fail += expect_string("JS comment removed", "const x = 1;", out);
    kc_min_free(out);

    out = kc_min_js("const t = `a  b`;");
    fail += expect_string("JS template preserved", "const t = `a  b`;", out);
    kc_min_free(out);

    out = kc_min_js("var re = /a[bc]d/g;");
    fail += expect_string("JS regex preserved", "var re = /a[bc]d/g;", out);
    kc_min_free(out);

    out = kc_min_js("var d = a / b;");
    fail += expect_string("JS division preserved", "var d = a / b;", out);
    kc_min_free(out);

    case_result(fail, "kc_min_js",
        "minifies JavaScript while preserving templates, regex, and division");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests HTML minification.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_min_html(void) {
    char *out;
    int fail = 0;

    fail += expect_true("html NULL returns NULL", kc_min_html(NULL) == NULL);

    out = kc_min_html("");
    fail += expect_true("html empty returns allocation", out != NULL);
    if (out) fail += expect_string("html empty returns empty string", "", out);
    kc_min_free(out);

    out = kc_min_html("<!-- c --><p>hi</p>");
    fail += expect_string("HTML comment removed", "<p>hi</p>", out);
    kc_min_free(out);

    out = kc_min_html("<pre>  a  </pre>");
    fail += expect_string("HTML pre preserved", "<pre>  a  </pre>", out);
    kc_min_free(out);

    out = kc_min_html("<textarea>  x  </textarea>");
    fail += expect_string("HTML textarea preserved",
        "<textarea>  x  </textarea>", out);
    kc_min_free(out);

    case_result(fail, "kc_min_html",
        "minifies HTML while preserving verbatim regions");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests generic txt minification.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_min_txt(void) {
    char *out;
    int fail = 0;

    fail += expect_true("text NULL returns NULL", kc_min_txt(NULL) == NULL);

    out = kc_min_txt("");
    fail += expect_true("text empty returns allocation", out != NULL);
    if (out) fail += expect_string("text empty returns empty string", "", out);
    kc_min_free(out);

    out = kc_min_txt("  hello   world\n\nfoo\tbar  ");
    fail += expect_string("text collapses whitespace",
        "hello world foo bar", out);
    kc_min_free(out);

    out = kc_min_txt("a  <!-- x -->  b");
    fail += expect_string("text does not interpret syntax",
        "a <!-- x --> b", out);
    kc_min_free(out);

    case_result(fail, "kc_min_txt",
        "collapses generic whitespace without interpreting syntax");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests ownership release through the public API.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_min_free(void) {
    char *out = kc_min_css("body { color: red; }");
    int fail = expect_true("minifier returns allocation", out != NULL);

    kc_min_free(out);
    kc_min_free(NULL);

    case_result(fail, "kc_min_free",
        "releases owned output and accepts NULL");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests the public build-version query.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_min_version(void) {
    int fail = expect_true("version returns non-zero", kc_min_version() != 0U);

    case_result(fail, "kc_min_version",
        "returns a nonzero generated build version");
    return fail == 0 ? 0 : 1;
}

/**
 * Runs the complete portable min test suite.
 * @return Number of failed test cases.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 6;
    test_case_current = 0;
    run_case(&rc, case_kc_min_css);
    run_case(&rc, case_kc_min_js);
    run_case(&rc, case_kc_min_html);
    run_case(&rc, case_kc_min_txt);
    run_case(&rc, case_kc_min_free);
    run_case(&rc, case_kc_min_version);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Dispatches the requested test case.
 * @param argc Command-line argument count.
 * @param argv Command-line argument vector.
 * @return 0 on success, nonzero on failure.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_min_css") == 0) return case_kc_min_css();
    if (strcmp(argv[1], "kc_min_js") == 0) return case_kc_min_js();
    if (strcmp(argv[1], "kc_min_html") == 0) return case_kc_min_html();
    if (strcmp(argv[1], "kc_min_txt") == 0) return case_kc_min_txt();
    if (strcmp(argv[1], "kc_min_free") == 0) return case_kc_min_free();
    if (strcmp(argv[1], "kc_min_version") == 0) return case_kc_min_version();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
