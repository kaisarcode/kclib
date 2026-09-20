/**
 * test.c - libtpl public API contract tests.
 * Summary: Validates each exported libtpl function through one dedicated test case.
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
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Test case description.
 * @param detail Behavior detail.
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
 * Verifies an integer result.
 * @param name Check name.
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
 * Verifies a true condition.
 * @param name Check name.
 * @param condition Condition expected to be true.
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
 * @param name Check name.
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
 * Renders one template and compares its output.
 * @param name Check description.
 * @param ctx Template context.
 * @param input Template text.
 * @param expected Expected rendered text.
 * @return 0 on success, 1 on failure.
 */
static int render_expect(const char *name, kc_tpl_t *ctx, const char *input, const char *expected);

/**
 * Tests kc_tpl_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpl_version(void) {
    const char *name = "kc_tpl_version";
    const char *detail = "returns build timestamp";
    int fail = expect_true("version set", kc_tpl_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpl_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpl_open(void) {
    const char *name = "kc_tpl_open";
    const char *detail = "validates and allocates context";
    kc_tpl_t *ctx;
    int fail = 0;

    ctx = NULL;
    fail += expect_int("open NULL out", KC_TPL_ERROR, kc_tpl_open(NULL));
    fail += expect_int("open context", KC_TPL_OK, kc_tpl_open(&ctx));
    fail += expect_true("open sets context", ctx != NULL);
    kc_tpl_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpl_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpl_close(void) {
    const char *name = "kc_tpl_close";
    const char *detail = "releases context";
    kc_tpl_t *ctx;

    ctx = NULL;
    kc_tpl_close(NULL);
    if (kc_tpl_open(&ctx) != KC_TPL_OK) {
        case_result(1, name, detail);
        return 1;
    }
    kc_tpl_close(ctx);
    case_result(0, name, detail);
    return 0;
}

/**
 * Tests kc_tpl_set_root.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpl_set_root(void) {
    const char *name = "kc_tpl_set_root";
    const char *detail = "validates input";
    kc_tpl_t *ctx;
    int fail = 0;

    ctx = NULL;
    fail += expect_int("set root NULL ctx", KC_TPL_ERROR, kc_tpl_set_root(NULL, "."));
    fail += expect_int("open context", KC_TPL_OK, kc_tpl_open(&ctx));
    fail += expect_int("set root NULL value", KC_TPL_ERROR, kc_tpl_set_root(ctx, NULL));
    fail += expect_int("set root empty", KC_TPL_ERROR, kc_tpl_set_root(ctx, ""));
    fail += expect_int("set root valid", KC_TPL_OK, kc_tpl_set_root(ctx, "."));
    kc_tpl_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpl_set_var.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpl_set_var(void) {
    const char *name = "kc_tpl_set_var";
    const char *detail = "validates and stores";
    kc_tpl_t *ctx;
    int fail = 0;

    ctx = NULL;
    fail += expect_int("set var NULL ctx", KC_TPL_ERROR,
        kc_tpl_set_var(NULL, "k", "v"));
    fail += expect_int("open context", KC_TPL_OK, kc_tpl_open(&ctx));
    fail += expect_int("set var empty key", KC_TPL_ERROR,
        kc_tpl_set_var(ctx, "", "v"));
    fail += expect_int("set var NULL key", KC_TPL_ERROR,
        kc_tpl_set_var(ctx, NULL, "v"));
    fail += expect_int("set var NULL value", KC_TPL_ERROR,
        kc_tpl_set_var(ctx, "k", NULL));
    fail += expect_int("set title", KC_TPL_OK, kc_tpl_set_var(ctx, "title", "A&B"));
    kc_tpl_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpl_render_string.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpl_render_string(void) {
    const char *name = "kc_tpl_render_string";
    const char *detail = "handles all directives";
    kc_tpl_t *ctx;
    char *output;
    int fail = 0;

    ctx = NULL;
    output = NULL;
    fail += expect_int("render NULL ctx", KC_TPL_ERROR,
        kc_tpl_render_string(NULL, "x", &output));
    fail += expect_int("open context", KC_TPL_OK, kc_tpl_open(&ctx));
    fail += expect_int("render NULL input", KC_TPL_ERROR,
        kc_tpl_render_string(ctx, NULL, &output));
    fail += expect_int("render NULL output", KC_TPL_ERROR,
        kc_tpl_render_string(ctx, "x", NULL));
    fail += expect_int("set title", KC_TPL_OK, kc_tpl_set_var(ctx, "title", "A&B"));
    fail += expect_int("set raw", KC_TPL_OK, kc_tpl_set_var(ctx, "raw", "<b>x</b>"));
    fail += expect_int("set items", KC_TPL_OK,
        kc_tpl_set_var(ctx, "items", "[item_1,item_2]"));
    fail += expect_int("set item one", KC_TPL_OK,
        kc_tpl_set_var(ctx, "item_1_title", "One"));
    fail += expect_int("set item two", KC_TPL_OK,
        kc_tpl_set_var(ctx, "item_2_title", "Two"));
    fail += expect_int("set a", KC_TPL_OK, kc_tpl_set_var(ctx, "a", "one"));
    fail += expect_int("set b", KC_TPL_OK, kc_tpl_set_var(ctx, "b", "one"));
    fail += expect_int("set c", KC_TPL_OK, kc_tpl_set_var(ctx, "c", "yes"));
    fail += expect_int("set n", KC_TPL_OK, kc_tpl_set_var(ctx, "n", "0"));
    fail += render_expect("escaped interpolation", ctx, "<h1>{{ title }}</h1>", "<h1>A&amp;B</h1>");
    fail += render_expect("raw interpolation", ctx, "{{{ raw }}}", "<b>x</b>");
    fail += render_expect("if truthy var", ctx, "{{@if title}}yes{{@else}}no{{@endif}}", "yes");
    fail += render_expect("if missing var", ctx, "{{@if missing}}yes{{@else}}no{{@endif}}", "no");
    fail += render_expect("if eq true", ctx, "{{@if a == b}}eq{{@else}}neq{{@endif}}", "eq");
    fail += render_expect("if neq", ctx, "{{@if a != b}}neq{{@else}}eq{{@endif}}", "eq");
    fail += render_expect("if eq false", ctx, "{{@if a == c}}eq{{@else}}neq{{@endif}}", "neq");
    fail += render_expect("if and both", ctx, "{{@if a && b}}both{{@else}}one{{@endif}}", "both");
    fail += render_expect("if and missing", ctx, "{{@if a && missing}}both{{@else}}one{{@endif}}", "one");
    fail += render_expect("if or match", ctx, "{{@if a || missing}}or{{@else}}none{{@endif}}", "or");
    fail += render_expect("if or none", ctx, "{{@if missing || other}}or{{@else}}none{{@endif}}", "none");
    fail += render_expect("if not missing", ctx, "{{@if !missing}}not{{@else}}is{{@endif}}", "not");
    fail += render_expect("if not truthy", ctx, "{{@if ! a}}neg{{@else}}pos{{@endif}}", "pos");
    fail += render_expect("if zero falsy", ctx, "{{@if n}}truthy{{@else}}falsy{{@endif}}", "falsy");
    fail += render_expect("if eq zero", ctx, "{{@if n == \"0\"}}zero{{@else}}nonzero{{@endif}}", "zero");
    fail += render_expect("if and eq", ctx, "{{@if a == b && c}}pre{{@else}}post{{@endif}}", "pre");
    fail += render_expect("if or eq false", ctx, "{{@if a == c || b == c}}or{{@else}}none{{@endif}}", "none");
    fail += render_expect("foreach list", ctx,
        "{{@foreach item in items}}<b>{{ item.title }}</b>{{@endforeach}}",
        "<b>One</b><b>Two</b>");
    fail += render_expect("block with props", ctx,
        "{{@setblock card}}<i>{{ name }}</i>{{@endsetblock}}{{@block card [ \"name\": \"Ada\" ]}}",
        "<i>Ada</i>");
    fail += render_expect("block empty var", ctx,
        "{{@setblock card}}<i>{{ title }}</i>{{@endsetblock}}{{@block card}}",
        "<i></i>");
    fail += render_expect("block with ref prop", ctx,
        "{{@setblock card}}<i>{{ t }}</i>{{@endsetblock}}{{@block card [ \"t\": title ]}}",
        "<i>A&amp;B</i>");
    fail += render_expect("nested blocks", ctx,
        "{{@setblock ico}}I{{@endsetblock}}{{@setblock nav}}<{{ href }}>{{@block ico}}{{@endsetblock}}{{@block nav [ \"href\": \"#\" ]}}",
        "<#>I");
    fail += render_expect("foreach with block", ctx,
        "{{@foreach item in items}}{{@setblock row}}r={{ v }};i={{ item }}{{@endsetblock}}{{@block row [ \"v\": item ]}}{{@endforeach}}",
        "r=item_1;i=r=item_2;i=");
    fail += render_expect("block after var", ctx,
        "{{@setblock card}}[{{ title }}]{{@endsetblock}}{{ title }}{{@block card}}",
        "A&amp;B[]");
    fail += render_expect("var inside block", ctx,
        "{{@setblock b}}{{@var x \"hello\"}}{{ x }}{{@endsetblock}}{{@var x \"world\"}}[{{@block b}}]{{ x }}",
        "[hello]world");
    fail += render_expect("comment stripped", ctx, "A{{/* hidden */}}B", "AB");
    fail += render_expect("comment in html comment", ctx, "<div><!-- {{@if title}}x{{@endif}} --></div>",
        "<div><!-- {{@if title}}x{{@endif}} --></div>");
    kc_tpl_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpl_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpl_free(void) {
    const char *name = "kc_tpl_free";
    const char *detail = "releases render output";
    kc_tpl_t *ctx;
    char *output;
    int fail = 0;

    ctx = NULL;
    output = NULL;
    fail += expect_int("open context", KC_TPL_OK, kc_tpl_open(&ctx));
    fail += expect_int("render output", KC_TPL_OK,
        kc_tpl_render_string(ctx, "output", &output));
    fail += expect_string("rendered output", "output", output);
    kc_tpl_free(output);
    kc_tpl_free(NULL);
    kc_tpl_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpl_get_error.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpl_get_error(void) {
    const char *name = "kc_tpl_get_error";
    const char *detail = "returns error details";
    kc_tpl_t *ctx;
    char *output;
    int fail = 0;

    ctx = NULL;
    output = NULL;
    fail += expect_string("NULL error", "invalid context", kc_tpl_get_error(NULL));
    fail += expect_int("open context", KC_TPL_OK, kc_tpl_open(&ctx));
    fail += expect_string("initial error", "ok", kc_tpl_get_error(ctx));
    fail += expect_int("missing include", KC_TPL_ERROR,
        kc_tpl_render_string(ctx, "{{@include \"missing.html\"}}", &output));
    kc_tpl_free(output);
    fail += expect_true("error string set", strcmp(kc_tpl_get_error(ctx), "ok") != 0);
    kc_tpl_close(ctx);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Renders one template and compares its output.
 * @param name Check description.
 * @param ctx Template context.
 * @param input Template text.
 * @param expected Expected rendered text.
 * @return 0 on success, 1 on failure.
 */
static int render_expect(const char *name, kc_tpl_t *ctx, const char *input, const char *expected) {
    char *output;
    int rc;

    output = NULL;
    rc = kc_tpl_render_string(ctx, input, &output);
    if (rc != KC_TPL_OK) {
        printf("[FAIL] %s: expected KC_TPL_OK, got %d: %s\n", name, rc,
            kc_tpl_get_error(ctx));
        kc_tpl_free(output);
        return 1;
    }
    rc = expect_string(name, expected, output);
    kc_tpl_free(output);
    return rc;
}

/**
 * Runs all test cases in a single process.
 * @return Total failures across all cases.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 8;
    test_case_current = 0;
    run_case(&rc, case_kc_tpl_version);
    run_case(&rc, case_kc_tpl_open);
    run_case(&rc, case_kc_tpl_close);
    run_case(&rc, case_kc_tpl_set_root);
    run_case(&rc, case_kc_tpl_set_var);
    run_case(&rc, case_kc_tpl_render_string);
    run_case(&rc, case_kc_tpl_free);
    run_case(&rc, case_kc_tpl_get_error);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one named test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Process status code.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <case>\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_tpl_version") == 0) return case_kc_tpl_version();
    if (strcmp(argv[1], "kc_tpl_open") == 0) return case_kc_tpl_open();
    if (strcmp(argv[1], "kc_tpl_close") == 0) return case_kc_tpl_close();
    if (strcmp(argv[1], "kc_tpl_set_root") == 0) return case_kc_tpl_set_root();
    if (strcmp(argv[1], "kc_tpl_set_var") == 0) return case_kc_tpl_set_var();
    if (strcmp(argv[1], "kc_tpl_render_string") == 0) return case_kc_tpl_render_string();
    if (strcmp(argv[1], "kc_tpl_free") == 0) return case_kc_tpl_free();
    if (strcmp(argv[1], "kc_tpl_get_error") == 0) return case_kc_tpl_get_error();
    fprintf(stderr, "unknown case: %s\n", argv[1]);
    return 2;
}
