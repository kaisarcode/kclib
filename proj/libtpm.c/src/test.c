/**
 * test.c - libtpm public API contract tests.
 * Summary: Validates the normalized tpm API.
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
    const char *detail = "creates ready profiles with defaults and options";
    kc_tpm_t *tpm = NULL;
    kc_tpm_t *default_tpm = NULL;
    kc_tpm_t *explicit_tpm = NULL;
    kc_tpm_options_t options;
    char *large_text = NULL;
    double score = -1.0;
    double default_score = -1.0;
    double explicit_score = -1.0;
    int fail = 0;

    fail += expect_int("open rejects NULL out", KC_TPM_ERROR,
        kc_tpm_open(NULL, "abc", NULL));
    fail += expect_int("open rejects NULL map", KC_TPM_ERROR,
        kc_tpm_open(&tpm, NULL, NULL));
    fail += expect_true("failed open clears output", tpm == NULL);

    fail += expect_int("open uses default ngram size", KC_TPM_OK,
        kc_tpm_open(&default_tpm, "hello world hello world", NULL));
    fail += expect_true("default open returns profile", default_tpm != NULL);

    memset(&options, 0, sizeof(options));
    fail += expect_int("open accepts empty options", KC_TPM_OK,
        kc_tpm_open(&explicit_tpm, "hello world hello world", &options));
    fail += expect_true("explicit open returns profile", explicit_tpm != NULL);

    if (default_tpm != NULL && explicit_tpm != NULL) {
        fail += expect_int("default profile scores immediately", KC_TPM_OK,
            kc_tpm_score(default_tpm, "hello world", &default_score));
        fail += expect_int("explicit profile scores immediately", KC_TPM_OK,
            kc_tpm_score(explicit_tpm, "hello world", &explicit_score));
        fail += expect_true("NULL options equal omitted ngram option",
            default_score == explicit_score);
    }

    kc_tpm_close(default_tpm);
    kc_tpm_close(explicit_tpm);

    memset(&options, 0, sizeof(options));
    {
        static const int ngram_zero = 0;
        options.ngram_size = &ngram_zero;
    }
    tpm = (kc_tpm_t *)(uintptr_t)1;
    fail += expect_int("open rejects explicit n below range", KC_TPM_ERROR,
        kc_tpm_open(&tpm, "abc", &options));
    fail += expect_true("invalid option clears output", tpm == NULL);

    {
        static const int ngram_nine = 9;
        options.ngram_size = &ngram_nine;
    }
    tpm = (kc_tpm_t *)(uintptr_t)1;
    fail += expect_int("open rejects n above range", KC_TPM_ERROR,
        kc_tpm_open(&tpm, "abc", &options));
    fail += expect_true("invalid high option clears output", tpm == NULL);

    {
        static const int ngram_one = 1;
        options.ngram_size = &ngram_one;
    }
    fail += expect_int("open accepts n=1", KC_TPM_OK,
        kc_tpm_open(&tpm, "abc abc", &options));
    kc_tpm_close(tpm);

    {
        static const int ngram_eight = 8;
        options.ngram_size = &ngram_eight;
    }
    tpm = NULL;
    fail += expect_int("open accepts n=8", KC_TPM_OK,
        kc_tpm_open(&tpm, "abcdefgh abcdefgh", &options));
    kc_tpm_close(tpm);

    {
        static const int ngram_two = 2;
        options.ngram_size = &ngram_two;
    }
    tpm = NULL;
    fail += expect_int("open accepts empty profile", KC_TPM_OK,
        kc_tpm_open(&tpm, "", &options));
    if (tpm != NULL) {
        fail += expect_int("empty profile scores successfully", KC_TPM_OK,
            kc_tpm_score(tpm, "abc", &score));
        fail += expect_true("empty profile scores zero", score == 0.0);
    }
    kc_tpm_close(tpm);

    large_text = repeat_byte('a', 16385);
    fail += expect_true("allocate overflow text", large_text != NULL);
    if (large_text != NULL) {
        {
        static const int ngram_one = 1;
        options.ngram_size = &ngram_one;
    }
        tpm = (kc_tpm_t *)(uintptr_t)1;
        fail += expect_int("open reports raw gram overflow", KC_TPM_ERROR,
            kc_tpm_open(&tpm, large_text, &options));
        fail += expect_true("overflow open returns no profile", tpm == NULL);
    }

    free(large_text);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpm_score.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_score(void) {
    const char *name = "kc_tpm_score";
    const char *detail = "reuses one profile for bounded similarity scoring";
    kc_tpm_t *tpm = NULL;
    kc_tpm_t *normalized_tpm = NULL;
    static const int ngram_three = 3;
    kc_tpm_options_t options = { .ngram_size = &ngram_three };
    char *large_text = NULL;
    double matching_score = -1.0;
    double mismatching_score = -1.0;
    double normalized_score = -1.0;
    double plain_score = -1.0;
    double repeated_score = -1.0;
    double score = -1.0;
    int fail = 0;

    fail += expect_int("score rejects NULL ctx", KC_TPM_ERROR,
        kc_tpm_score(NULL, "abc", &score));

    fail += expect_int("open scoring profile", KC_TPM_OK,
        kc_tpm_open(&tpm, "hello world hello world english text", &options));
    if (tpm == NULL) {
        case_result(1, name, detail);
        return 1;
    }

    fail += expect_int("score rejects NULL input", KC_TPM_ERROR,
        kc_tpm_score(tpm, NULL, &score));
    fail += expect_int("score rejects NULL output", KC_TPM_ERROR,
        kc_tpm_score(tpm, "abc", NULL));

    fail += expect_int("matching score succeeds", KC_TPM_OK,
        kc_tpm_score(tpm, "hello world english text", &matching_score));
    fail += expect_int("mismatching score succeeds", KC_TPM_OK,
        kc_tpm_score(tpm, "zzzz qqqq xxxx yyyy", &mismatching_score));
    fail += expect_true("matching score is in range",
        matching_score >= 0.0 && matching_score <= 1.0);
    fail += expect_true("mismatching score is in range",
        mismatching_score >= 0.0 && mismatching_score <= 1.0);
    fail += expect_true("score ranks matching text higher",
        matching_score > mismatching_score);

    fail += expect_int("repeated score succeeds", KC_TPM_OK,
        kc_tpm_score(tpm, "hello world english text", &repeated_score));
    fail += expect_true("profile is reusable",
        repeated_score == matching_score);

    fail += expect_int("empty input succeeds", KC_TPM_OK,
        kc_tpm_score(tpm, "", &score));
    fail += expect_true("empty input scores zero", score == 0.0);

    fail += expect_int("open normalization profile", KC_TPM_OK,
        kc_tpm_open(&normalized_tpm,
            "  Hello\tWORLD\nhello   world  ", &options));
    if (normalized_tpm != NULL) {
        fail += expect_int("normalized score succeeds", KC_TPM_OK,
            kc_tpm_score(normalized_tpm, "hello world", &normalized_score));
        fail += expect_int("plain score succeeds", KC_TPM_OK,
            kc_tpm_score(normalized_tpm, "HELLO\tWORLD", &plain_score));
        fail += expect_true("score normalizes case and whitespace",
            plain_score == normalized_score);
    }

    {
        static const int ngram_one = 1;
        options.ngram_size = &ngram_one;
    }
    kc_tpm_close(tpm);
    tpm = NULL;
    fail += expect_int("open n=1 overflow profile", KC_TPM_OK,
        kc_tpm_open(&tpm, "aaaa", &options));
    large_text = repeat_byte('a', 16385);
    fail += expect_true("allocate score overflow text", large_text != NULL);
    if (tpm != NULL && large_text != NULL) {
        fail += expect_int("score reports raw gram overflow", KC_TPM_ERROR,
            kc_tpm_score(tpm, large_text, &score));
    }

    free(large_text);
    kc_tpm_close(normalized_tpm);
    kc_tpm_close(tpm);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_tpm_close.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_tpm_close(void) {
    const char *name = "kc_tpm_close";
    const char *detail = "releases context and accepts NULL";
    kc_tpm_t *tpm = NULL;
    int fail = 0;

    kc_tpm_close(NULL);
    fail += expect_int("open profile for close", KC_TPM_OK,
        kc_tpm_open(&tpm, "abc", NULL));
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
    const char *detail = "returns a nonzero generated build version";
    int fail = expect_true("version returns nonzero", kc_tpm_version() != 0U);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;

    test_case_total = 4;
    test_case_current = 0;
    run_case(&rc, case_kc_tpm_open);
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
    if (strcmp(argv[1], "kc_tpm_score") == 0) return case_kc_tpm_score();
    if (strcmp(argv[1], "kc_tpm_close") == 0) return case_kc_tpm_close();
    if (strcmp(argv[1], "kc_tpm_version") == 0) return case_kc_tpm_version();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
