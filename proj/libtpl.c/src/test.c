/**
 * test.c - libtpl public API tests.
 * Summary: Contract tests for the reusable template API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libtpl.h"

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
 * Verifies one integer test expectation.
 * @param name Expectation description.
 * @param expected Expected integer value.
 * @param actual Actual integer value.
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
 * Tests kc_tpl_open.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_tpl_open(void) {
    kc_tpl_t *tpl = NULL;
    kc_tpl_options_t options;
    char source[] = "<h1>{{ title }}</h1>";
    char *out;
    int fail = 0;

    fail += expect_int("open NULL out", KC_TPL_ERROR,
        kc_tpl_open(NULL, source, NULL));
    fail += expect_int("open NULL source", KC_TPL_ERROR,
        kc_tpl_open(&tpl, NULL, NULL));
    fail += expect_true("NULL source clears output", tpl == NULL);

    options.root = NULL;
    fail += expect_int("open omitted root", KC_TPL_OK,
        kc_tpl_open(&tpl, source, &options));
    fail += expect_true("open omitted root returns template", tpl != NULL);
    kc_tpl_close(tpl);
    tpl = NULL;

    options.root = "";
    fail += expect_int("open empty root", KC_TPL_ERROR,
        kc_tpl_open(&tpl, source, &options));

    options.root = ".";
    fail += expect_int("open explicit root", KC_TPL_OK,
        kc_tpl_open(&tpl, source, &options));
    fail += expect_true("open returns template", tpl != NULL);

    source[0] = 'X';
    out = kc_tpl_render(tpl, NULL, 0U);
    fail += expect_string("open owns source",
        "<h1></h1>", out);
    kc_tpl_free(out);
    kc_tpl_close(tpl);

    case_result(fail, "kc_tpl_open",
        "validates options and owns the template source");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpl_render.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_tpl_render(void) {
    const char *source =
        "{{ title }}|{{{ raw }}}|"
        "{{@if show}}yes{{@else}}no{{@endif}}|"
        "{{@foreach item in items}}[{{ item }}]{{@endforeach}}|"
        "{{/* hidden */}}";
    kc_tpl_var_t vars[] = {
        { "title", "A&B" },
        { "raw", "<b>x</b>" },
        { "show", "true" },
        { "items", "[one,two]" }
    };
    kc_tpl_var_t second[] = {
        { "title", "Second" }
    };
    kc_tpl_var_t invalid[] = {
        { "", "x" }
    };
    kc_tpl_t *tpl = NULL;
    char *out;
    int fail = 0;

    fail += expect_true("render NULL template",
        kc_tpl_render(NULL, NULL, 0U) == NULL);
    fail += expect_int("open template", KC_TPL_OK,
        kc_tpl_open(&tpl, source, NULL));

    fail += expect_true("render NULL vars with count fails",
        kc_tpl_render(tpl, NULL, 1U) == NULL);
    fail += expect_true("render invalid var fails",
        kc_tpl_render(tpl, invalid, 1U) == NULL);

    out = kc_tpl_render(tpl, vars, sizeof(vars) / sizeof(vars[0]));
    fail += expect_string("render directives",
        "A&amp;B|<b>x</b>|yes|[one][two]|", out);
    kc_tpl_free(out);

    out = kc_tpl_render(tpl, second, sizeof(second) / sizeof(second[0]));
    fail += expect_string("render variables are isolated",
        "Second||no||", out);
    kc_tpl_free(out);

    kc_tpl_close(tpl);

    fail += expect_int("open empty template", KC_TPL_OK,
        kc_tpl_open(&tpl, "", NULL));
    out = kc_tpl_render(tpl, NULL, 0U);
    fail += expect_true("empty render returns allocation", out != NULL);
    if (out) fail += expect_string("empty render", "", out);
    kc_tpl_free(out);
    kc_tpl_close(tpl);

    case_result(fail, "kc_tpl_render",
        "renders stored source with isolated per-call variables");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpl_error.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_tpl_error(void) {
    kc_tpl_t *tpl = NULL;
    char *out;
    int fail = 0;

    fail += expect_string("NULL template error",
        "invalid template", kc_tpl_error(NULL));
    fail += expect_int("open template", KC_TPL_OK,
        kc_tpl_open(&tpl, "{{@include \"missing.html\"}}", NULL));
    fail += expect_string("initial error", "ok", kc_tpl_error(tpl));

    out = kc_tpl_render(tpl, NULL, 0U);
    fail += expect_true("missing include fails", out == NULL);
    kc_tpl_free(out);
    fail += expect_true("error updated",
        strcmp(kc_tpl_error(tpl), "ok") != 0);

    kc_tpl_close(tpl);
    case_result(fail, "kc_tpl_error",
        "returns the latest template error");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpl_free.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_tpl_free(void) {
    kc_tpl_t *tpl = NULL;
    char *out;
    int fail = 0;

    fail += expect_int("open template", KC_TPL_OK,
        kc_tpl_open(&tpl, "output", NULL));
    out = kc_tpl_render(tpl, NULL, 0U);
    fail += expect_string("render output", "output", out);
    kc_tpl_free(out);
    kc_tpl_free(NULL);
    kc_tpl_close(tpl);

    case_result(fail, "kc_tpl_free",
        "releases render output and accepts NULL");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpl_close.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_tpl_close(void) {
    kc_tpl_t *tpl = NULL;
    int fail = 0;

    kc_tpl_close(NULL);
    fail += expect_int("open template", KC_TPL_OK,
        kc_tpl_open(&tpl, "x", NULL));
    kc_tpl_close(tpl);

    case_result(fail, "kc_tpl_close",
        "releases template state and accepts NULL");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpl_version.
 * @return 0 on success, 1 otherwise.
 */
static int case_kc_tpl_version(void) {
    int fail = expect_true("version returns non-zero", kc_tpl_version() != 0U);

    case_result(fail, "kc_tpl_version",
        "returns a nonzero generated build version");
    return fail == 0 ? 0 : 1;
}

/**
 * Runs the complete portable tpl test suite.
 * @return Number of failed test cases.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 6;
    test_case_current = 0;

    run_case(&rc, case_kc_tpl_open);
    run_case(&rc, case_kc_tpl_render);
    run_case(&rc, case_kc_tpl_error);
    run_case(&rc, case_kc_tpl_free);
    run_case(&rc, case_kc_tpl_close);
    run_case(&rc, case_kc_tpl_version);
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
    if (strcmp(argv[1], "kc_tpl_open") == 0) return case_kc_tpl_open();
    if (strcmp(argv[1], "kc_tpl_render") == 0) return case_kc_tpl_render();
    if (strcmp(argv[1], "kc_tpl_error") == 0) return case_kc_tpl_error();
    if (strcmp(argv[1], "kc_tpl_free") == 0) return case_kc_tpl_free();
    if (strcmp(argv[1], "kc_tpl_close") == 0) return case_kc_tpl_close();
    if (strcmp(argv[1], "kc_tpl_version") == 0) return case_kc_tpl_version();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
