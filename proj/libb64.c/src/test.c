/**
 * test.c - Contract tests for the b64 library
 * Summary: Public API tests for encode and decode.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libb64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Canonical test case name.
 * @param description Test case description.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *description) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, description);
}

typedef int (*case_fn)(void);

/**
 * Runs one test case with counter tracking.
 * @param rc Destination accumulator.
 * @param fn Test case function.
 * @return None.
 */
static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

/**
 * Verifies one boolean condition.
 * @param name Check description.
 * @param cond Non-zero when the check passed.
 * @return 0 on success, 1 on failure.
 */
static int expect_true(const char *name, int cond) {
    if (!cond) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

/**
 * Verifies one integer result.
 * @param name Check description.
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
 * Verifies one string result.
 * @param name Check description.
 * @param expected Expected string.
 * @param actual Actual string.
 * @return 0 on success, 1 on failure.
 */
static int expect_str(const char *name, const char *expected, const char *actual) {
    if (strcmp(expected, actual) != 0) {
        printf("[FAIL] %s: expected '%s', got '%s'\n", name, expected, actual);
        return 1;
    }
    return 0;
}

/**
 * Tests kc_b64_encode.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_b64_encode(void) {
    const unsigned char binary[] = {0, 1, 127, 128, 255};
    char *encoded;
    int fail = 0;

    fail += expect_true("encode rejects NULL data",
        kc_b64_encode(NULL, 0) == NULL);

    encoded = kc_b64_encode("", 0);
    fail += expect_true("encode empty returns non-NULL", encoded != NULL);
    if (encoded != NULL) {
        fail += expect_str("encode empty returns empty string", "", encoded);
    }
    kc_b64_free(encoded);

    encoded = kc_b64_encode("hello", 5);
    fail += expect_true("encode hello returns non-NULL", encoded != NULL);
    if (encoded != NULL) {
        fail += expect_str("encode hello matches expected",
            "aGVsbG8=", encoded);
    }
    kc_b64_free(encoded);

    encoded = kc_b64_encode(binary, sizeof(binary));
    fail += expect_true("encode binary returns non-NULL", encoded != NULL);
    if (encoded != NULL) {
        fail += expect_str("encode binary matches expected",
            "AAF/gP8=", encoded);
    }
    kc_b64_free(encoded);

    case_result(fail, "kc_b64_encode",
        "encodes empty, text, and binary inputs");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_b64_decode.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_b64_decode(void) {
    static const unsigned char binary[] = {0, 1, 127, 128, 255};
    static const char *invalid[] = {
        "abc",
        "invalid!",
        "====",
        "A===",
        "AA=A",
        "AAAA====",
        "AB==",
        "AAB="
    };
    void *decoded;
    size_t out_size;
    size_t i;
    int fail = 0;

    out_size = 123;
    fail += expect_true("decode rejects NULL string",
        kc_b64_decode(NULL, &out_size) == NULL);
    fail += expect_int("NULL string resets size", 0, (int)out_size);

    fail += expect_true("decode rejects NULL size",
        kc_b64_decode("Zg==", NULL) == NULL);

    out_size = 123;
    decoded = kc_b64_decode("", &out_size);
    fail += expect_true("decode empty returns non-NULL", decoded != NULL);
    fail += expect_int("decode empty size", 0, (int)out_size);
    kc_b64_free(decoded);

    out_size = 0;
    decoded = kc_b64_decode("Zg==", &out_size);
    fail += expect_true("decode one byte succeeds", decoded != NULL);
    fail += expect_int("decode one byte size", 1, (int)out_size);
    if (decoded != NULL) {
        fail += expect_true("decode one byte matches",
            memcmp(decoded, "f", 1) == 0);
    }
    kc_b64_free(decoded);

    out_size = 0;
    decoded = kc_b64_decode("Zm8=", &out_size);
    fail += expect_true("decode two bytes succeeds", decoded != NULL);
    fail += expect_int("decode two bytes size", 2, (int)out_size);
    if (decoded != NULL) {
        fail += expect_true("decode two bytes matches",
            memcmp(decoded, "fo", 2) == 0);
    }
    kc_b64_free(decoded);

    out_size = 0;
    decoded = kc_b64_decode("Zm9v", &out_size);
    fail += expect_true("decode three bytes succeeds", decoded != NULL);
    fail += expect_int("decode three bytes size", 3, (int)out_size);
    if (decoded != NULL) {
        fail += expect_true("decode three bytes matches",
            memcmp(decoded, "foo", 3) == 0);
    }
    kc_b64_free(decoded);

    out_size = 0;
    decoded = kc_b64_decode("AAF/gP8=", &out_size);
    fail += expect_true("decode binary succeeds", decoded != NULL);
    fail += expect_int("decode binary size",
        (int)sizeof(binary), (int)out_size);
    if (decoded != NULL) {
        fail += expect_true("decode binary matches",
            memcmp(decoded, binary, sizeof(binary)) == 0);
    }
    kc_b64_free(decoded);

    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        out_size = 123;
        decoded = kc_b64_decode(invalid[i], &out_size);
        fail += expect_true("decode rejects malformed base64",
            decoded == NULL);
        fail += expect_int("malformed decode resets size", 0, (int)out_size);
        kc_b64_free(decoded);
    }

    case_result(fail, "kc_b64_decode",
        "decodes valid RFC 4648 input and rejects malformed padding");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_b64_free.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_b64_free(void) {
    char *encoded = kc_b64_encode("x", 1);
    int fail = 0;

    fail += expect_true("allocate value for free", encoded != NULL);
    kc_b64_free(encoded);
    kc_b64_free(NULL);

    case_result(fail, "kc_b64_free",
        "releases library allocations and accepts NULL");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_b64_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_b64_version(void) {
    int fail = 0;

    fail += expect_true("version returns non-zero", kc_b64_version() != 0U);
    case_result(fail, "kc_b64_version",
        "returns a nonzero generated build version");
    return fail == 0 ? 0 : 1;
}

#ifndef __EMSCRIPTEN__

static int b64_cli_total = 0;
static int b64_cli_failed = 0;

/**
 * Writes one fixture file.
 * @param path Output file path.
 * @param text Text to write.
 * @return Nonzero on success.
 */
static int b64_cli_write_text_file(const char *path, const char *text)
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
static int b64_cli_output_equals(
    const char *args,
    const char *stdin_text,
    const char *expected
)
{
    const char *input_path = "b64-cli-stdin.txt";
    const char *output_path = "b64-cli-output.txt";
    char command[4096];
    char buffer[4096];
    FILE *file;
    size_t size;
    int status;

    if (stdin_text && !b64_cli_write_text_file(input_path, stdin_text)) return 0;

#ifdef _WIN32
    snprintf(command, sizeof(command),
        "\"\"%s\" %s %s> %s 2>NUL\"",
        KC_CLI_PATH,
        args ? args : "",
        stdin_text ? "< b64-cli-stdin.txt " : "",
        output_path);
#else
    snprintf(command, sizeof(command),
        "\"%s\" %s %s> %s 2>/dev/null",
        KC_CLI_PATH,
        args ? args : "",
        stdin_text ? "< b64-cli-stdin.txt " : "",
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
static int b64_cli_run(const char *args)
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
 * Prints one contract-test result.
 * @param name Test name.
 * @param detail Behavior under test.
 * @param ok Nonzero on success.
 * @return None.
 */
static void b64_cli_result(const char *name, const char *detail, int ok)
{
    b64_cli_total++;
    if (!ok) b64_cli_failed++;
    printf("[%d/13] [%s] %s: %s\n", b64_cli_total, ok ? "PASS" : "FAIL",
        name, detail);
}

/**
 * Runs all b64 CLI contract tests.
 * @return Zero when all tests pass.
 */
static int case_b64_cli(void)
{
    const char *file_path = "b64-cli-file.txt";
    int fixture_ok;

    b64_cli_result("help", "--help succeeds", b64_cli_run("--help") == 0);
    b64_cli_result("version", "--version succeeds", b64_cli_run("--version") == 0);
    b64_cli_result("encode arg", "-e encodes text argument",
        b64_cli_output_equals("-e \"hello world\"", NULL, "aGVsbG8gd29ybGQ="));
    b64_cli_result("encode stdin", "-e reads stdin when argument is absent",
        b64_cli_output_equals("-e", "hello world", "aGVsbG8gd29ybGQ="));

    fixture_ok = b64_cli_write_text_file(file_path, "file");
    b64_cli_result("encode file", "-e -i reads file",
        fixture_ok && b64_cli_output_equals("-e -i b64-cli-file.txt", NULL,
            "ZmlsZQ=="));
    b64_cli_result("stdin priority", "stdin takes priority over -i",
        fixture_ok && b64_cli_output_equals("-e -i b64-cli-file.txt", "stdin",
            "c3RkaW4="));
    b64_cli_result("argument priority", "argument takes priority over stdin and -i",
        fixture_ok && b64_cli_output_equals(
            "-e \"argument\" -i b64-cli-file.txt", "stdin",
            "YXJndW1lbnQ="));

    b64_cli_result("decode arg", "-d decodes Base64 argument",
        b64_cli_output_equals("-d aGVsbG8=", NULL, "hello"));
    b64_cli_result("decode stdin", "-d reads Base64 from stdin",
        b64_cli_output_equals("-d", "aGVsbG8=", "hello"));
    fixture_ok = fixture_ok && b64_cli_write_text_file(file_path, "aGVsbG8=");
    b64_cli_result("decode file", "-d -i reads Base64 file",
        fixture_ok && b64_cli_output_equals("-d -i b64-cli-file.txt", NULL, "hello"));
    remove(file_path);

    b64_cli_result("missing mode", "encode or decode mode is required", b64_cli_run("") != 0);
    b64_cli_result("invalid", "invalid Base64 fails", b64_cli_run("-d invalid") != 0);
    b64_cli_result("unknown", "unknown option fails", b64_cli_run("-e --unknown") != 0);

    printf("%d passed, %d failed\n\n", b64_cli_total - b64_cli_failed,
        b64_cli_failed);
    return b64_cli_failed ? 1 : 0;
}

#endif

/**
 * Runs all test cases in a single process.
 * @return 0 on success, 1 on failure.
 */
static int case_all(void) {
    int rc = 0;

    test_case_total = 4;
#ifndef __EMSCRIPTEN__
    test_case_total++;
#endif
    test_case_current = 0;
    run_case(&rc, case_kc_b64_encode);
    run_case(&rc, case_kc_b64_decode);
    run_case(&rc, case_kc_b64_free);
    run_case(&rc, case_kc_b64_version);
#ifndef __EMSCRIPTEN__
    run_case(&rc, case_b64_cli);
#endif

    printf("%d passed, %d failed\n\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one b64 public API test case.
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
    if (strcmp(argv[1], "kc_b64_encode") == 0) return case_kc_b64_encode();
    if (strcmp(argv[1], "kc_b64_decode") == 0) return case_kc_b64_decode();
    if (strcmp(argv[1], "kc_b64_free") == 0) return case_kc_b64_free();
    if (strcmp(argv[1], "kc_b64_version") == 0) return case_kc_b64_version();
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "b64_cli") == 0) return case_b64_cli();
#endif
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
