/**
 * test.c - libdemo public API contract tests.
 * Summary: Validates the minimal reusable API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libdemo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints one test case result.
 * @param fail Non-zero when the case failed.
 * @param name Canonical test case name.
 * @param detail Behavior verified by the case.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

/**
 * Runs one test case with counter tracking.
 * @param rc Failure accumulator.
 * @param fn Test case function.
 * @return None.
 */
static void run_case(int *rc, int (*fn)(void)) {
    test_case_current++;
    *rc += fn();
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
 * Tests kc_demo_greet and kc_demo_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_demo_greet(void) {
    const char *name = "kc_demo_greet";
    const char *detail = "returns an owned greeting for the supplied name";
    char *greeting;
    int fail = 0;

    fail += expect_true("greet rejects NULL", kc_demo_greet(NULL) == NULL);

    greeting = kc_demo_greet("John");
    fail += expect_true("greet allocates output", greeting != NULL);
    if (greeting != NULL) {
        fail += expect_true("greet returns expected text",
            strcmp(greeting, "Hello John!") == 0);
    }
    kc_demo_free(greeting);
    kc_demo_free(NULL);

    greeting = kc_demo_greet("");
    fail += expect_true("greet accepts empty name", greeting != NULL);
    if (greeting != NULL) {
        fail += expect_true("empty name keeps greeting format",
            strcmp(greeting, "Hello !") == 0);
    }
    kc_demo_free(greeting);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_demo_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_demo_version(void) {
    const char *name = "kc_demo_version";
    const char *detail = "returns a nonzero generated build version";
    int fail = expect_true("version is nonzero", kc_demo_version() != 0U);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

#ifndef __EMSCRIPTEN__

#ifdef _WIN32
#define KC_LINE_END "\r\n"
#else
#define KC_LINE_END "\n"
#endif

static int demo_cli_total = 0;
static int demo_cli_failed = 0;

/**
 * Executes the CLI with output discarded.
 * @param args Arguments appended after executable.
 * @return Child process status from system().
 */
static int demo_cli_run(const char *args)
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
 * Executes the CLI and captures stdout.
 * @param args Arguments appended after executable.
 * @param buffer Output buffer.
 * @param buffer_size Output buffer size.
 * @return Nonzero on success.
 */
static int demo_cli_capture_output(const char *args, char *buffer, size_t buffer_size)
{
    const char *output_path = "demo-cli-output.txt";
    char command[4096];
    FILE *file;
    size_t size;

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"\"%s\" %s > %s 2>NUL\"",
        KC_CLI_PATH, args ? args : "", output_path);
#else
    snprintf(command, sizeof(command), "\"%s\" %s > %s 2>/dev/null",
        KC_CLI_PATH, args ? args : "", output_path);
#endif

    if (system(command) != 0) {
        remove(output_path);
        return 0;
    }

    file = fopen(output_path, "rb");
    if (!file) return 0;
    size = fread(buffer, 1, buffer_size - 1, file);
    fclose(file);
    remove(output_path);
    buffer[size] = '\0';
    return 1;
}

/**
 * Prints one contract-test result.
 * @param name Test name.
 * @param detail Behavior under test.
 * @param ok Nonzero on success.
 * @return None.
 */
static void demo_cli_result(const char *name, const char *detail, int ok)
{
    demo_cli_total++;
    if (!ok) demo_cli_failed++;
    printf("[%d/6] [%s] %s: %s\n", demo_cli_total, ok ? "PASS" : "FAIL",
        name, detail);
}

/**
 * Runs all demo CLI contract tests.
 * @return Zero when all tests pass.
 */
static int case_demo_cli(void)
{
    char output[256];

    demo_cli_result("help", "--help succeeds", demo_cli_run("--help") == 0);
    demo_cli_result("version", "--version succeeds", demo_cli_run("--version") == 0);
    demo_cli_result("greet", "name prints greeting",
        demo_cli_capture_output("John", output, sizeof(output)) &&
        strcmp(output, "Hello John!" KC_LINE_END) == 0);
    demo_cli_result("quoted", "quoted name with spaces succeeds",
        demo_cli_capture_output("\"John Doe\"", output, sizeof(output)) &&
        strcmp(output, "Hello John Doe!" KC_LINE_END) == 0);
    demo_cli_result("missing", "missing name fails", demo_cli_run(NULL) != 0);
    demo_cli_result("unknown", "unknown option fails", demo_cli_run("--unknown") != 0);

    printf("\n%d passed, %d failed\n", demo_cli_total - demo_cli_failed, demo_cli_failed);
    return demo_cli_failed ? 1 : 0;
}

#endif

/**
 * Runs all library contract test cases.
 * @return 0 on success, non-zero on failure.
 */
static int case_all(void) {
    int rc = 0;

    test_case_total = 2;
#ifndef __EMSCRIPTEN__
    test_case_total++;
#endif
    test_case_current = 0;
    run_case(&rc, case_kc_demo_greet);
    run_case(&rc, case_kc_demo_version);
#ifndef __EMSCRIPTEN__
    run_case(&rc, case_demo_cli);
#endif

    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one libdemo contract test case.
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
    if (strcmp(argv[1], "kc_demo_greet") == 0) return case_kc_demo_greet();
    if (strcmp(argv[1], "kc_demo_version") == 0) return case_kc_demo_version();
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "demo_cli") == 0) return case_demo_cli();
#endif
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
