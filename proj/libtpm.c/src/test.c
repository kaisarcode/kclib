/**
 * test.c - libtpm public API contract tests.
 * Summary: Validates the normalized tpm API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
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

#ifndef __EMSCRIPTEN__
static int case_tpm_cli(void);
#endif

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

    test_case_total = 5;
    test_case_current = 0;
    run_case(&rc, case_kc_tpm_open);
    run_case(&rc, case_kc_tpm_score);
    run_case(&rc, case_kc_tpm_close);
    run_case(&rc, case_kc_tpm_version);
#ifndef __EMSCRIPTEN__
    run_case(&rc, case_tpm_cli);
#endif
    printf("%d passed, %d failed\n\n", test_case_total - rc, rc);
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
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "tpm_cli") == 0) return case_tpm_cli();
#endif
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
#ifndef __EMSCRIPTEN__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef KC_CLI_PATH
#define KC_CLI_PATH "tpm"
#endif

static int test_total = 0;
static int test_failed = 0;

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
 * Executes the CLI and captures stdout.
 * @param args CLI arguments.
 * @param stdin_text Optional stdin content.
 * @param buffer Output buffer.
 * @param buffer_size Output buffer size.
 * @return Nonzero on success.
 */
static int capture_output(
    const char *args,
    const char *stdin_text,
    char *buffer,
    size_t buffer_size
)
{
    const char *stdin_path = "tpm-cli-stdin.txt";
    const char *output_path = "tpm-cli-output.txt";
    char command[4096];
    FILE *file;
    size_t size;

    if (stdin_text && !write_text_file(stdin_path, stdin_text)) return 0;

#ifdef _WIN32
    snprintf(command, sizeof(command),
        "\"\"%s\" %s %s> %s 2>NUL\"",
        KC_CLI_PATH,
        args ? args : "",
        stdin_text ? "< tpm-cli-stdin.txt " : "",
        output_path);
#else
    snprintf(command, sizeof(command),
        "\"%s\" %s %s> %s 2>/dev/null",
        KC_CLI_PATH,
        args ? args : "",
        stdin_text ? "< tpm-cli-stdin.txt " : "",
        output_path);
#endif

    if (system(command) != 0) {
        if (stdin_text) remove(stdin_path);
        remove(output_path);
        return 0;
    }
    if (stdin_text) remove(stdin_path);

    file = fopen(output_path, "rb");
    if (!file) return 0;
    size = fread(buffer, 1, buffer_size - 1, file);
    fclose(file);
    remove(output_path);
    buffer[size] = '\0';
    return 1;
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
 * Verifies that score output is one number in the public range.
 * @return Nonzero on success.
 */
static int score_output_ok(void)
{
    char buffer[256];
    char *end = NULL;
    double score;

    if (!capture_output("\"hello world\" \"hello world\"", NULL,
        buffer, sizeof(buffer))) return 0;
    score = strtod(buffer, &end);
    if (end == buffer || score < 0.0 || score > 1.0) return 0;
    while (*end == '\r' || *end == '\n') end++;
    return *end == '\0';
}

/**
 * Prints one contract-test result.
 * @param name Test name.
 * @param detail Behavior under test.
 * @param ok Nonzero on success.
 * @return None.
 */
static void result(const char *name, const char *detail, int ok)
{
    test_total++;
    if (!ok) test_failed++;
    printf("[%d/12] [%s] %s: %s\n", test_total, ok ? "PASS" : "FAIL",
        name, detail);
}

/**
 * Runs all tpm CLI contract tests.
 * @return Zero when all tests pass.
 */
static int case_tpm_cli(void)
{
    const char *file_path = "tpm-cli-file.txt";
    char expected[256];
    char actual[256];
    int fixture_ok;

    result("help", "--help succeeds", run_cli("--help") == 0);
    result("version", "--version succeeds", run_cli("--version") == 0);
    result("missing map", "map text is required", run_cli("") != 0);
    result("score", "map and input text score successfully",
        run_cli("\"hello world\" \"hello world\"") == 0);
    result("output", "score is one number in range 0..1", score_output_ok());

    fixture_ok = write_text_file(file_path, "completely different text");
    result("input file", "-i reads input text from file",
        fixture_ok && run_cli("\"hello world\" -i tpm-cli-file.txt") == 0);
    result("stdin priority", "stdin takes priority over -i",
        fixture_ok && capture_output("\"hello world\"", "hello world",
            expected, sizeof(expected)) &&
        capture_output("\"hello world\" -i tpm-cli-file.txt", "hello world",
            actual, sizeof(actual)) && strcmp(expected, actual) == 0);
    result("argument priority", "argument takes priority over stdin and -i",
        fixture_ok && capture_output(
            "\"hello world\" \"hello world\"", NULL,
            expected, sizeof(expected)) &&
        capture_output(
            "\"hello world\" \"hello world\" -i tpm-cli-file.txt",
            "completely different text", actual, sizeof(actual)) &&
        strcmp(expected, actual) == 0);
    remove(file_path);

    result("ngram", "positive ngram size is accepted by the CLI",
        run_cli("\"hello world\" \"hello world\" --ngram 4") == 0);
    result("bad ngram", "non-positive ngram size fails",
        run_cli("\"hello world\" \"hello world\" --ngram 0") != 0);
    result("missing value", "--input requires a following file",
        run_cli("\"hello world\" --input") != 0);
    result("unknown", "unknown option fails",
        run_cli("\"hello world\" \"hello world\" --unknown") != 0);

    printf("%d passed, %d failed\n\n", test_total - test_failed, test_failed);
    return test_failed ? 1 : 0;
}
#endif
