/**
 * test.c - libtpl public API tests.
 * Summary: Contract tests for the reusable template API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
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

#ifndef __EMSCRIPTEN__
static int case_tpl_cli(void);
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
#ifdef __EMSCRIPTEN__
    test_case_total = 6;
#else
    test_case_total = 17;
#endif
    test_case_current = 0;

    run_case(&rc, case_kc_tpl_open);
    run_case(&rc, case_kc_tpl_render);
    run_case(&rc, case_kc_tpl_error);
    run_case(&rc, case_kc_tpl_free);
    run_case(&rc, case_kc_tpl_close);
    run_case(&rc, case_kc_tpl_version);
#ifndef __EMSCRIPTEN__
    rc += case_tpl_cli();
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
    if (strcmp(argv[1], "kc_tpl_open") == 0) return case_kc_tpl_open();
    if (strcmp(argv[1], "kc_tpl_render") == 0) return case_kc_tpl_render();
    if (strcmp(argv[1], "kc_tpl_error") == 0) return case_kc_tpl_error();
    if (strcmp(argv[1], "kc_tpl_free") == 0) return case_kc_tpl_free();
    if (strcmp(argv[1], "kc_tpl_close") == 0) return case_kc_tpl_close();
    if (strcmp(argv[1], "kc_tpl_version") == 0) return case_kc_tpl_version();
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "tpl_cli") == 0) {
        int failures;

        test_case_total = 11;
        failures = case_tpl_cli();
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
#define KC_CLI_PATH "tpl"
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
    const char *input_path = "tpl-cli-stdin.txt";
    const char *output_path = "tpl-cli-output.txt";
    char command[4096];
    char buffer[4096];
    FILE *file;
    size_t size;
    int status;

    if (stdin_text && !write_text_file(input_path, stdin_text)) return 0;

#ifdef _WIN32
    snprintf(command, sizeof(command),
        "\"%s\" %s %s> %s 2>NUL",
        KC_CLI_PATH,
        args ? args : "",
        stdin_text ? "< tpl-cli-stdin.txt " : "",
        output_path);
#else
    snprintf(command, sizeof(command),
        "\"%s\" %s %s> %s 2>/dev/null",
        KC_CLI_PATH,
        args ? args : "",
        stdin_text ? "< tpl-cli-stdin.txt " : "",
        output_path);
#endif

    status = system(command);
    if (stdin_text) remove(input_path);
    if (status != 0) {
        remove(output_path);
        return 0;
    }

    file = fopen(output_path, "rb");
    if (!file) return 0;
    size = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    remove(output_path);
    buffer[size] = '\0';
    return strcmp(buffer, expected) == 0;
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
    snprintf(command, sizeof(command), "\"%s\" %s > NUL 2>&1",
        KC_CLI_PATH, args ? args : "");
#else
    snprintf(command, sizeof(command), "\"%s\" %s > /dev/null 2>&1",
        KC_CLI_PATH, args ? args : "");
#endif
    return system(command);
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
 * Runs all tpl CLI contract tests.
 * @return Zero when all tests pass.
 */
static int case_tpl_cli(void)
{
    const char *file_path = "tpl-cli-input.tpl";
    int fixture_ok;
    int failures = 0;

    failures += result("help", "--help succeeds", run_cli("--help") == 0);
    failures += result("version", "--version succeeds", run_cli("--version") == 0);
    failures += result("missing", "missing template input fails", run_cli("") != 0);
    failures += result("root", "--root requires a value", run_cli("Hello --root") != 0);
    failures += result("var", "--var requires key and value", run_cli("Hello --var key") != 0);
    failures += result("unknown", "unknown option fails", run_cli("Hello --unknown") != 0);
    failures += result("argument", "positional argument is literal template text",
        output_equals("Hello", NULL, "Hello"));
    failures += result("stdin", "stdin provides template input",
        output_equals("", "Hello", "Hello"));

    fixture_ok = write_text_file(file_path, "File");
    failures += result("input file", "-i reads template file when higher-priority input is absent",
        fixture_ok && output_equals("-i tpl-cli-input.tpl", NULL, "File"));
    failures += result("stdin priority", "stdin takes priority over -i",
        fixture_ok && output_equals("-i tpl-cli-input.tpl", "Stdin",
            "Stdin"));
    failures += result("argument priority", "argument takes priority over stdin and -i",
        fixture_ok && output_equals("-i tpl-cli-input.tpl Argument", "Stdin",
            "Argument"));
    remove(file_path);

    return failures;
}
#endif
