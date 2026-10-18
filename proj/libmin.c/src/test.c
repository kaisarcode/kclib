/**
 * test.c - libmin public API tests.
 * Summary: Contract tests for the stateless min API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
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

#ifndef __EMSCRIPTEN__
static int case_min_cli(void);
#endif

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
#ifdef __EMSCRIPTEN__
    test_case_total = 6;
#else
    test_case_total = 18;
#endif
    test_case_current = 0;
    run_case(&rc, case_kc_min_css);
    run_case(&rc, case_kc_min_js);
    run_case(&rc, case_kc_min_html);
    run_case(&rc, case_kc_min_txt);
    run_case(&rc, case_kc_min_free);
    run_case(&rc, case_kc_min_version);
#ifndef __EMSCRIPTEN__
    rc += case_min_cli();
#endif
    printf("%d passed, %d failed\n", test_case_total - rc, rc);
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
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "min_cli") == 0) {
        int failures;

        test_case_total = 12;
        failures = case_min_cli();
        printf("%d passed, %d failed\n", test_case_total - failures, failures);
        return failures ? 1 : 0;
    }
#endif
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
#ifndef __EMSCRIPTEN__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef KC_CLI_PATH
#define KC_CLI_PATH "min"
#endif

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
 * Executes the CLI and compares captured stdout exactly.
 * @param args CLI arguments.
 * @param stdin_text Optional stdin content.
 * @param expected Expected stdout.
 * @return Nonzero on success.
 */
static int output_equals(
    const char *args,
    const char *stdin_text,
    const char *expected
)
{
    const char *input_path = "min-cli-stdin.txt";
    const char *output_path = "min-cli-output.txt";
    char command[4096];
    char buffer[4096];
    FILE *file;
    size_t size;

    if (stdin_text && !write_text_file(input_path, stdin_text)) return 0;

#ifdef _WIN32
    snprintf(command, sizeof(command),
        "\"\"%s\" %s %s> %s 2>NUL\"",
        KC_CLI_PATH,
        args ? args : "",
        stdin_text ? "< min-cli-stdin.txt " : "",
        output_path);
#else
    snprintf(command, sizeof(command),
        "\"%s\" %s %s> %s 2>/dev/null",
        KC_CLI_PATH,
        args ? args : "",
        stdin_text ? "< min-cli-stdin.txt " : "",
        output_path);
#endif
    if (system(command) != 0) {
        if (stdin_text) remove(input_path);
        remove(output_path);
        return 0;
    }

    file = fopen(output_path, "rb");
    if (!file) {
        if (stdin_text) remove(input_path);
        return 0;
    }
    size = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    if (stdin_text) remove(input_path);
    remove(output_path);
    buffer[size] = '\0';
    return strcmp(buffer, expected) == 0;
}

/**
 * Prints one CLI result using the shared test counter.
 * @param name Test name.
 * @param detail Behavior under test.
 * @param ok Nonzero on success.
 * @return Zero on success, one on failure.
 */
static int result(const char *name, const char *detail, int ok)
{
    test_case_current++;
    case_result(!ok, name, detail);
    return ok ? 0 : 1;
}

/**
 * Runs all min CLI contract tests.
 * @return Zero when all tests pass.
 */
static int case_min_cli(void)
{
    const char *file_path = "min-cli-file.txt";
    int fixture_ok;
    int failures = 0;

    failures += result("help", "--help succeeds", run_cli("--help") == 0);
    failures += result("version", "--version succeeds", run_cli("--version") == 0);
    failures += result("default txt", "text argument defaults to txt",
        output_equals("\"fdfsdfsd sdfsd     ff sd              fsdfd\"", NULL,
            "fdfsdfsd sdfsd ff sd fsdfd"));
    failures += result("stdin", "stdin defaults to txt",
        output_equals(NULL, "  hello   world\n\nfoo\tbar  ",
            "hello world foo bar"));

    fixture_ok = write_text_file(file_path, "file      input");
    failures += result("input file", "-i reads file when higher-priority input is absent",
        fixture_ok && output_equals("-i min-cli-file.txt", NULL,
            "file input"));
    failures += result("stdin priority", "stdin takes priority over -i",
        fixture_ok && output_equals("-i min-cli-file.txt",
            "stdin      input", "stdin input"));
    failures += result("argument priority", "argument takes priority over stdin and -i",
        fixture_ok && output_equals(
            "\"argument    input\" -i min-cli-file.txt",
            "stdin input", "argument input"));
    remove(file_path);

    failures += result("css", "CSS argument minification succeeds",
        output_equals("-t css \"/* hi */ body{}\"", NULL, "body{}"));
    failures += result("js", "JavaScript argument minification succeeds",
        output_equals("-t js \"const x = 1; // comment\"", NULL,
            "const x = 1;"));
    failures += result("html", "HTML argument minification succeeds",
        output_equals("-t html \"<!-- c --><p>hi</p>\"", NULL, "<p>hi</p>"));
    failures += result("bad type", "unknown type fails", run_cli("-t xml \"text\"") != 0);
    failures += result("unknown", "unknown option fails", run_cli("--unknown") != 0);

    return failures;
}
#endif
