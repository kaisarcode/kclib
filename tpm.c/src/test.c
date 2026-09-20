/**
 * test.c - libtpm public API contract tests.
 * Summary: Validates each exported libtpm function through one dedicated test case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libtpm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Test case name.
 * @param detail Behavior verified by the case.
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
 * Allocates a repeated byte string.
 * @param byte Byte to repeat.
 * @param count Number of bytes.
 * @return Allocated string, or NULL on failure.
 */
static char *repeat_byte(char byte, size_t count) {
    char *text;

    text = (char *)malloc(count + 1);
    if (text == NULL) return NULL;
    memset(text, byte, count);
    text[count] = '\0';
    return text;
}

/**
 * Tests kc_tpm_open.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_open(void) {
    const char *name = "kc_tpm_open";
    const char *detail = "validates and allocates context";
    kc_tpm_t *tpm;
    int fail = 0;

    tpm = NULL;
    fail = 0;
    fail += expect_int("open rejects NULL out", KC_TPM_ERROR,
        kc_tpm_open(NULL));
    fail += expect_int("open creates context", KC_TPM_OK,
        kc_tpm_open(&tpm));
    fail += expect_true("open sets output", tpm != NULL);
    if (tpm != NULL) {
        kc_tpm_close(tpm);
    }
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpm_build.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_build(void) {
    const char *name = "kc_tpm_build";
    const char *detail = "constructs profile";
    kc_tpm_t *tpm;
    char *large_text;
    int fail = 0;

    tpm = NULL;
    large_text = NULL;
    fail = 0;
    fail += expect_int("build rejects NULL ctx", KC_TPM_ERROR,
        kc_tpm_build(NULL, "abc", 2));
    fail += expect_int("open context for build", KC_TPM_OK, kc_tpm_open(&tpm));
    fail += expect_int("build rejects NULL text", KC_TPM_ERROR,
        kc_tpm_build(tpm, NULL, 2));
    fail += expect_int("build rejects n below range", KC_TPM_ERROR,
        kc_tpm_build(tpm, "abc", 0));
    fail += expect_int("build rejects n above range", KC_TPM_ERROR,
        kc_tpm_build(tpm, "abc", 9));
    fail += expect_int("build accepts n=1", KC_TPM_OK,
        kc_tpm_build(tpm, "abc abc", 1));
    fail += expect_int("build accepts n=8", KC_TPM_OK,
        kc_tpm_build(tpm, "abcdefgh abcdefgh", 8));
    fail += expect_int("build accepts empty profile", KC_TPM_OK,
        kc_tpm_build(tpm, "", 2));
    large_text = repeat_byte('a', 16385);
    fail += expect_true("allocate overflow text", large_text != NULL);
    if (large_text != NULL) {
        fail += expect_int("build reports raw gram overflow", KC_TPM_ERROR,
            kc_tpm_build(tpm, large_text, 1));
    }
    free(large_text);
    kc_tpm_close(tpm);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpm_score.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_score(void) {
    const char *name = "kc_tpm_score";
    const char *detail = "ranks matching text higher";
    kc_tpm_t *tpm;
    double matching_score;
    double mismatching_score;
    double normalized_score;
    double plain_score;
    int fail = 0;

    tpm = NULL;
    fail = 0;
    fail += expect_true("score NULL ctx returns zero", kc_tpm_score(NULL, "abc") == 0.0);
    fail += expect_int("open context for score", KC_TPM_OK, kc_tpm_open(&tpm));
    fail += expect_true("score before build returns zero", kc_tpm_score(tpm, "abc") == 0.0);
    fail += expect_int("build scoring profile", KC_TPM_OK,
        kc_tpm_build(tpm, "hello world hello world english text", 3));
    matching_score = kc_tpm_score(tpm, "hello world english text");
    mismatching_score = kc_tpm_score(tpm, "zzzz qqqq xxxx yyyy");
    fail += expect_true("matching score is in range", matching_score >= 0.0 && matching_score <= 1.0);
    fail += expect_true("mismatching score is in range", mismatching_score >= 0.0 && mismatching_score <= 1.0);
    fail += expect_true("score ranks matching text higher", matching_score > mismatching_score);
    fail += expect_true("score NULL input returns zero", kc_tpm_score(tpm, NULL) == 0.0);
    fail += expect_int("build normalization profile", KC_TPM_OK,
        kc_tpm_build(tpm, "  Hello\tWORLD\nhello   world  ", 3));
    normalized_score = kc_tpm_score(tpm, "hello world");
    plain_score = kc_tpm_score(tpm, "HELLO\tWORLD");
    fail += expect_true("score normalizes case and whitespace", plain_score == normalized_score);
    kc_tpm_close(tpm);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpm_close.
 * Validates that the function properly releases context and handles NULL input.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_close(void) {
    const char *name = "kc_tpm_close";
    const char *detail = "releases context";
    kc_tpm_t *tpm;
    int fail = 0;

    tpm = NULL;
    fail = 0;
    kc_tpm_close(NULL);
    fail += expect_int("open context for close", KC_TPM_OK, kc_tpm_open(&tpm));
    kc_tpm_close(tpm);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpm_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_version(void) {
    const char *name = "kc_tpm_version";
    const char *detail = "returns non-zero build timestamp";
    int fail = expect_true("version returns non-zero build timestamp", kc_tpm_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 5;
    test_case_current = 0;
    run_case(&rc, case_kc_tpm_open);
    run_case(&rc, case_kc_tpm_build);
    run_case(&rc, case_kc_tpm_score);
    run_case(&rc, case_kc_tpm_close);
    run_case(&rc, case_kc_tpm_version);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one libtpm contract test case.
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
        if (strcmp(argv[1], "kc_tpm_open") == 0) return case_kc_tpm_open();
    if (strcmp(argv[1], "kc_tpm_build") == 0) return case_kc_tpm_build();
    if (strcmp(argv[1], "kc_tpm_score") == 0) return case_kc_tpm_score();
    if (strcmp(argv[1], "kc_tpm_close") == 0) return case_kc_tpm_close();
    if (strcmp(argv[1], "kc_tpm_version") == 0) return case_kc_tpm_version();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
