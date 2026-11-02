/**
 * test.c - Portable regex contract tests.
 * Summary: Tests patterns, captures, search and malformed inputs.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libregex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_count = 0;
static int test_failed = 0;

#define TEST_TOTAL 9

/**
 * Record one named test result.
 * @param name Case identifier.
 * @param detail Contract under test.
 * @param failed Nonzero on failure.
 * @return None.
 */
static void test_result(const char *name, const char *detail, int failed)
{
    test_count++;
    test_failed += failed != 0;
    printf("[%d/%d] [%s] %s: %s\n", test_count, TEST_TOTAL,
        failed ? "FAIL" : "PASS", name, detail);
}

/**
 * Check regex compilation and full matching with one expression.
 * @param pattern Pattern text.
 * @param text Input text.
 * @param expected Expected status.
 * @return Zero if status matches.
 */
static int check_match(const char *pattern, const char *text, int expected)
{
    kc_regex_t *regex = NULL;
    int status = kc_regex_open(&regex, pattern);
    if (status == KC_REGEX_OK)
        status = kc_regex_match(regex, text, NULL, 0U, NULL);
    kc_regex_close(regex);
    return status != expected;
}

/**
 * Verify literals, root path, wildcard and full-string semantics.
 * @return Zero on success.
 */
static int test_literal(void)
{
    int failed = 0;
    failed |= check_match("/", "/", KC_REGEX_OK);
    failed |= check_match("/docs", "/docs", KC_REGEX_OK);
    failed |= check_match("/docs", "/docs/x", KC_REGEX_NOMATCH);
    failed |= check_match("/.*", "/assets/a.js", KC_REGEX_OK);
    failed |= check_match("a.c", "abc", KC_REGEX_OK);
    return failed;
}

/**
 * Verify branching and repetition of grouped expressions.
 * @return Zero on success.
 */
static int test_branch(void)
{
    int failed = 0;
    failed |= check_match("(doc|page)s?", "docs", KC_REGEX_OK);
    failed |= check_match("(doc|page)s?", "page", KC_REGEX_OK);
    failed |= check_match("(ab)+c", "ababc", KC_REGEX_OK);
    failed |= check_match("a?b", "b", KC_REGEX_OK);
    failed |= check_match("a+b", "b", KC_REGEX_NOMATCH);
    return failed;
}

/**
 * Verify class ranges, negation and escaped metacharacters.
 * @return Zero on success.
 */
static int test_class(void)
{
    int failed = 0;
    failed |= check_match("[a-z]+-[0-9]+", "abc-42", KC_REGEX_OK);
    failed |= check_match("[^0-9]+", "abc", KC_REGEX_OK);
    failed |= check_match("[^0-9]+", "12", KC_REGEX_NOMATCH);
    failed |= check_match("a\\.b", "a.b", KC_REGEX_OK);
    return failed;
}

/**
 * Check capture offsets including group zero and parenthesis ordering.
 * @return Zero on success.
 */
static int test_captures(void)
{
    kc_regex_t *regex = NULL;
    kc_regex_capture_t cap[3] = {{0}};
    size_t count = 0U;
    int failed = kc_regex_open(&regex, "/(docs|pages)/([0-9]+)") !=
        KC_REGEX_OK;

    if (!failed) {
        failed |= kc_regex_match(regex, "/pages/42", cap, 3U,
            &count) != KC_REGEX_OK;
        failed |= count != 3U;
        failed |= cap[0].start != 0U || cap[0].end != 9U;
        failed |= cap[1].start != 1U || cap[1].end != 6U;
        failed |= cap[2].start != 7U || cap[2].end != 9U;
    }
    kc_regex_close(regex);
    return failed;
}

/**
 * Verify optional unmatched captures and partial output.
 * @return Zero on success.
 */
static int test_optional(void)
{
    kc_regex_t *regex = NULL;
    kc_regex_capture_t cap[1];
    size_t count = 0U;
    int failed = kc_regex_open(&regex, "(a)?b") != KC_REGEX_OK;

    if (!failed) {
        failed |= kc_regex_match(regex, "b", cap, 1U,
            &count) != KC_REGEX_OK;
        failed |= count != 2U || cap[0].start != 0U ||
            cap[0].end != 1U;
    }
    kc_regex_close(regex);
    return failed;
}

/**
 * Verify leftmost substring search and explicit offsets.
 * @return Zero on success.
 */
static int test_search(void)
{
    kc_regex_t *regex = NULL;
    kc_regex_capture_t cap[2];
    size_t count = 0U;
    int failed = kc_regex_open(&regex, "([0-9]+)") != KC_REGEX_OK;

    if (!failed) {
        failed |= kc_regex_search(regex, "abc12 def34", 0U,
            cap, 2U, &count) != KC_REGEX_OK;
        failed |= count != 2U || cap[0].start != 3U ||
            cap[0].end != 5U || cap[1].start != 3U ||
            cap[1].end != 5U;
        failed |= kc_regex_search(regex, "abc12 def34", 5U,
            cap, 2U, &count) != KC_REGEX_OK;
        failed |= cap[0].start != 9U || cap[0].end != 11U;
    }
    kc_regex_close(regex);
    return failed;
}

/**
 * Verify malformed pattern detection and invalid API arguments.
 * @return Zero on success.
 */
static int test_invalid(void)
{
    kc_regex_t *regex = NULL;
    int failed = 0;

    failed |= kc_regex_open(NULL, "a") != KC_REGEX_EINVAL;
    failed |= kc_regex_open(&regex, "([") != KC_REGEX_EPATTERN;
    failed |= kc_regex_open(&regex, "a{3}") != KC_REGEX_EPATTERN;
    failed |= kc_regex_open(&regex, "abc") != KC_REGEX_OK;
    if (regex) {
        failed |= kc_regex_match(regex, NULL, NULL, 0U, NULL) !=
            KC_REGEX_EINVAL;
        failed |= kc_regex_search(regex, "abc", 4U, NULL, 0U, NULL) !=
            KC_REGEX_EINVAL;
    }
    kc_regex_close(regex);
    return failed;
}

/**
 * Verify large failed searches remain bounded.
 * @return Zero when pathological repetition does not hang.
 */
static int test_limit(void)
{
    kc_regex_t *regex = NULL;
    int status, failed = kc_regex_open(&regex, "(a+)+b") != KC_REGEX_OK;
    char text[260];

    memset(text, 'a', sizeof(text) - 1U);
    text[sizeof(text) - 1U] = '\0';
    if (!failed) {
        status = kc_regex_match(regex, text, NULL, 0U, NULL);
        failed |= status != KC_REGEX_ELIMIT && status != KC_REGEX_NOMATCH;
    }
    kc_regex_close(regex);
    return failed;
}

/**
 * Verify CLI help, success and no-match statuses.
 * @return Zero on success.
 */
static int test_cli(void)
{
    int failed = 0;

#ifdef _WIN32
    failed |= system("regex.exe --help > NUL") != 0;
    failed |= system("regex.exe match abc abc > NUL") != 0;
    failed |= system("regex.exe match abc x > NUL") == 0;
#else
    failed |= system("./regex --help > /dev/null") != 0;
    failed |= system("./regex match abc abc > /dev/null") != 0;
    failed |= system("./regex match abc x > /dev/null") == 0;
#endif
    return failed;
}

/**
 * Execute all public contract tests.
 * @return Zero if all cases pass.
 */
int main(void)
{
    test_result("literal", "matches complete input", test_literal());
    test_result("branch", "handles alternatives and repetitions", test_branch());
    test_result("class", "handles classes and escapes", test_class());
    test_result("captures", "returns exact byte offsets", test_captures());
    test_result("optional", "preserves unmatched optional groups", test_optional());
    test_result("search", "finds leftmost substrings", test_search());
    test_result("invalid", "rejects invalid arguments and syntax", test_invalid());
    test_result("limit", "bounds pathological matching", test_limit());
#ifndef __EMSCRIPTEN__
    test_result("cli", "reports status from native executable", test_cli());
#else
    test_result("cli", "CLI not applicable for WASM", 0);
#endif
    printf("%d passed, %d failed\n", test_count - test_failed, test_failed);
    return test_failed ? 1 : 0;
}
