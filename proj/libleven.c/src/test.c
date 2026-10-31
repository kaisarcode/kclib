/**
 * test.c - Levenshtein distance public contract tests.
 * Summary: Verifies Unicode distance, threshold and CLI contracts.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libleven.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Print one named contract-test result.
 * @param fail Number of failed assertions.
 * @param name Name of the test case.
 * @param detail Contract checked by the case.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail)
{
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

/**
 * Run one test and accumulate failed cases.
 * @param result Number of failed cases so far.
 * @param fn Test case to run.
 * @return None.
 */
static void run_case(int *result, int (*fn)(void))
{
    test_case_current++;
    *result += fn();
}

/**
 * Verify one distance/status pair against the public API.
 * @param left First input string.
 * @param right Second input string.
 * @param maximum Optional inclusive distance limit.
 * @param expected_status Expected status.
 * @param expected_distance Expected distance or sentinel.
 * @return 0 if both outputs match, 1 otherwise.
 */
static int check_distance(const char *left, const char *right,
    const size_t *maximum, int expected_status, size_t expected_distance)
{
    size_t actual = SIZE_MAX;
    int status = kc_leven_distance(left, right, maximum, &actual);

    if (status != expected_status || actual != expected_distance) {
        printf("[FAIL] expected (%d, %zu), got (%d, %zu)\n",
            expected_status, expected_distance, status, actual);
        return 1;
    }
    return 0;
}

/**
 * Test empty, identical, substitution, insertion and deletion cases.
 * @return 0 if all distances match, otherwise 1.
 */
static int case_distance(void)
{
    int fail = 0;

    fail += check_distance("", "", NULL, KC_LEVEN_OK, 0);
    fail += check_distance("", "abc", NULL, KC_LEVEN_OK, 3);
    fail += check_distance("abc", "", NULL, KC_LEVEN_OK, 3);
    fail += check_distance("abc", "abc", NULL, KC_LEVEN_OK, 0);
    fail += check_distance("kitten", "sitting", NULL, KC_LEVEN_OK, 3);
    fail += check_distance("abc", "axc", NULL, KC_LEVEN_OK, 1);
    fail += check_distance("abc", "abxc", NULL, KC_LEVEN_OK, 1);
    fail += check_distance("abxc", "abc", NULL, KC_LEVEN_OK, 1);
    fail += check_distance("abcd", "dcba", NULL, KC_LEVEN_OK, 4);
    case_result(fail, "distance", "computes exact edit distance in either direction");
    return fail != 0;
}

/**
 * Test Unicode scalar processing without folding or localization.
 * @return 0 if all Unicode distances match, otherwise 1.
 */
static int case_unicode(void)
{
    int fail = 0;

    fail += check_distance("á", "a", NULL, KC_LEVEN_OK, 1);
    fail += check_distance("🐱", "🐶", NULL, KC_LEVEN_OK, 1);
    fail += check_distance("漢字", "漢", NULL, KC_LEVEN_OK, 1);
    fail += check_distance("a\u0301", "á", NULL, KC_LEVEN_OK, 2);
    fail += check_distance("albóndiga", "almondiga", NULL, KC_LEVEN_OK, 2);
    case_result(fail, "unicode", "counts Unicode code points without normalization");
    return fail != 0;
}

/**
 * Test inclusive thresholds and exceeded-limit sentinel.
 * @return 0 if all thresholds behave correctly, otherwise 1.
 */
static int case_limit(void)
{
    size_t zero = 0, one = 1, two = 2, three = 3;
    int fail = 0;

    fail += check_distance("same", "same", &zero, KC_LEVEN_OK, 0);
    fail += check_distance("same", "some", &zero, KC_LEVEN_ELIMIT, 1);
    fail += check_distance("same", "some", &one, KC_LEVEN_OK, 1);
    fail += check_distance("kitten", "sitting", &two, KC_LEVEN_ELIMIT, 3);
    fail += check_distance("kitten", "sitting", &three, KC_LEVEN_OK, 3);
    fail += check_distance("", "abcd", &one, KC_LEVEN_ELIMIT, 2);
    fail += check_distance("abcd", "", &one, KC_LEVEN_ELIMIT, 2);
    case_result(fail, "limit", "stops past inclusive maximum and reports sentinel");
    return fail != 0;
}

/**
 * Test malformed UTF-8, invalid arguments, and static status messages.
 * @return 0 if all invalid inputs are rejected, otherwise 1.
 */
static int case_invalid(void)
{
    const char *bad[] = {"\x80", "\xC0\xAF", "\xE2\x82",
        "\xED\xA0\x80", "\xF4\x90\x80\x80"};
    size_t distance = 17;
    int fail = 0;

    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        fail += check_distance(bad[i], "ok", NULL, KC_LEVEN_EUTF8, 0);
        fail += check_distance("ok", bad[i], NULL, KC_LEVEN_EUTF8, 0);
    }
    fail += kc_leven_distance(NULL, "", NULL, &distance) != KC_LEVEN_EINVAL;
    fail += kc_leven_distance("", NULL, NULL, &distance) != KC_LEVEN_EINVAL;
    fail += kc_leven_distance("", "", NULL, NULL) != KC_LEVEN_EINVAL;
    fail += strcmp(kc_leven_strerror(KC_LEVEN_EUTF8), "invalid UTF-8") != 0;
    case_result(fail, "invalid", "rejects malformed UTF-8 and invalid pointers");
    return fail != 0;
}

#ifndef __EMSCRIPTEN__
/**
 * Run the CLI and check its process exit status.
 * @param arguments Quoted CLI arguments.
 * @param expected_zero Whether the command must succeed.
 * @return 0 if expected exit behavior matches, otherwise 1.
 */
static int check_cli(const char *arguments, int expected_zero)
{
    char command[1024];
    int result;

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"\"%s\" %s > NUL 2>&1\"",
        KC_CLI_PATH, arguments);
#else
    snprintf(command, sizeof(command), "\"%s\" %s > /dev/null 2>&1",
        KC_CLI_PATH, arguments);
#endif
    result = system(command);
    return (result == 0) != expected_zero;
}

/**
 * Execute the CLI and compare its exact output.
 * @param arguments Quoted CLI arguments.
 * @param expected Expected output, including newline.
 * @return 0 on matching successful invocation, 1 otherwise.
 */
static int check_cli_output(const char *arguments, const char *expected)
{
    const char *path = "leven-cli-output.txt";
    char command[1024], buffer[256];
    size_t count;
    FILE *file;
    int status;

#ifdef _WIN32
    snprintf(command, sizeof(command), "\"\"%s\" %s > %s 2>NUL\"",
        KC_CLI_PATH, arguments, path);
#else
    snprintf(command, sizeof(command), "\"%s\" %s > %s 2>/dev/null",
        KC_CLI_PATH, arguments, path);
#endif
    status = system(command);
    if (status != 0) {
        remove(path);
        return 1;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        remove(path);
        return 1;
    }
    count = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    remove(path);
    buffer[count] = '\0';
    return strcmp(buffer, expected) != 0;
}

/**
 * Test command-line options, input validation, and threshold exit behavior.
 * @return 0 on success, 1 on failure.
 */
static int case_cli(void)
{
    int fail = 0;

    fail += check_cli("--help", 1);
    fail += check_cli("--version", 1);
    fail += check_cli("kitten sitting", 1);
#ifdef _WIN32
    fail += check_cli_output("kitten sitting", "3\r\n");
#else
    fail += check_cli_output("kitten sitting", "3\n");
#endif
    fail += check_cli("kitten sitting --max 3", 1);
    fail += check_cli("kitten sitting --max 2", 0);
    fail += check_cli("kitten sitting --max -1", 0);
    fail += check_cli("kitten", 0);
    fail += check_cli("kitten sitting extra", 0);
    fail += check_cli("--unknown", 0);
    case_result(fail, "cli", "supports options and correct failure status");
    return fail != 0;
}
#endif

/**
 * Run every portable public contract test.
 * @return 0 when all cases pass, 1 otherwise.
 */
static int case_all(void)
{
    int failures = 0;

#ifdef __EMSCRIPTEN__
    test_case_total = 4;
#else
    test_case_total = 5;
#endif
    test_case_current = 0;
    run_case(&failures, case_distance);
    run_case(&failures, case_unicode);
    run_case(&failures, case_limit);
    run_case(&failures, case_invalid);
#ifndef __EMSCRIPTEN__
    run_case(&failures, case_cli);
#endif
    printf("%d passed, %d failed\n", test_case_total - failures, failures);
    return failures ? 1 : 0;
}

/**
 * Select a named test case or execute all cases.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 on failure, 2 on invalid invocation.
 */
int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument\n");
        return 2;
    }
    if (!strcmp(argv[1], "all")) return case_all();
    test_case_total = 1;
    test_case_current = 1;
    if (!strcmp(argv[1], "distance")) return case_distance();
    if (!strcmp(argv[1], "unicode")) return case_unicode();
    if (!strcmp(argv[1], "limit")) return case_limit();
    if (!strcmp(argv[1], "invalid")) return case_invalid();
#ifndef __EMSCRIPTEN__
    if (!strcmp(argv[1], "cli")) return case_cli();
#endif
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
