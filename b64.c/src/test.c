/**
 * test.c - Contract tests for the b64 library
 * Summary: Public API tests for encode and decode.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
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
 * @param name Test case description.
 * @return None.
 */
static void case_result(int fail, const char *name) {
    printf("[%d/%d] [%s] %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name);
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
 * Tests encoding empty input.
 * @return 0 on success, 1 on failure.
 */
static int case_encode_empty(void) {
    const char *name = "encode empty input";
    char *encoded = kc_b64_encode("", 0);
    int fail = 0;
    fail += expect_true("encode empty returns non-NULL", encoded != NULL);
    if (encoded) {
        fail += expect_str("encode empty returns empty string", "", encoded);
        free(encoded);
    }
    case_result(fail, name);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests encoding "hello" string.
 * @return 0 on success, 1 on failure.
 */
static int case_encode_hello(void) {
    const char *name = "encode hello string";
    const char *data = "hello";
    char *encoded = kc_b64_encode(data, strlen(data));
    int fail = 0;
    fail += expect_true("encode hello returns non-NULL", encoded != NULL);
    if (encoded) {
        fail += expect_str("encode hello matches expected", "aGVsbG8=", encoded);
        free(encoded);
    }
    case_result(fail, name);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests encoding binary data with round-trip.
 * @return 0 on success, 1 on failure.
 */
static int case_encode_binary(void) {
    const char *name = "encode binary data round-trip";
    const unsigned char data[] = {0, 1, 127, 128, 255};
    char *encoded = kc_b64_encode(data, sizeof(data));
    size_t decoded_len = 0;
    void *decoded;
    int fail = 0;
    fail += expect_true("encode binary returns non-NULL", encoded != NULL);
    if (encoded) {
        decoded = kc_b64_decode(encoded, &decoded_len);
        fail += expect_true("round-trip decode returns non-NULL", decoded != NULL);
        if (decoded) {
            fail += expect_int("round-trip length", (int)sizeof(data), (int)decoded_len);
            fail += expect_true("round-trip data matches", memcmp(data, decoded, sizeof(data)) == 0);
            free(decoded);
        }
        free(encoded);
    }
    case_result(fail, name);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests decoding empty string.
 * @return 0 on success, 1 on failure.
 */
static int case_decode_empty(void) {
    const char *name = "decode empty string";
    size_t out_size = 0;
    void *decoded = kc_b64_decode("", &out_size);
    int fail = 0;
    fail += expect_true("decode empty returns non-NULL", decoded != NULL);
    fail += expect_int("decode empty size", 0, (int)out_size);
    free(decoded);
    case_result(fail, name);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests decoding "aGVsbG8=" to "hello".
 * @return 0 on success, 1 on failure.
 */
static int case_decode_hello(void) {
    const char *name = "decode hello string";
    size_t out_size = 0;
    void *decoded = kc_b64_decode("aGVsbG8=", &out_size);
    int fail = 0;
    fail += expect_true("decode hello returns non-NULL", decoded != NULL);
    if (decoded) {
        fail += expect_int("decode hello length", 5, (int)out_size);
        fail += expect_true("decode hello matches", memcmp(decoded, "hello", 5) == 0);
        free(decoded);
    }
    case_result(fail, name);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests encode/decode round-trip with various byte values.
 * @return 0 on success, 1 on failure.
 */
static int case_roundtrip(void) {
    const char *name = "encode/decode round-trip various bytes";
    const unsigned char data[] = {0, 1, 127, 128, 255, 'A', 'B'};
    size_t data_len = sizeof(data);
    char *encoded;
    size_t decoded_len = 0;
    void *decoded;
    int fail = 0;

    encoded = kc_b64_encode(data, data_len);
    fail += expect_true("roundtrip encode returns non-NULL", encoded != NULL);

    if (encoded != NULL) {
        decoded = kc_b64_decode(encoded, &decoded_len);
        fail += expect_true("roundtrip decode returns non-NULL", decoded != NULL);
        fail += expect_int("roundtrip length", (int)data_len, (int)decoded_len);
        if (decoded != NULL) {
            fail += expect_true("roundtrip data matches", memcmp(data, decoded, data_len) == 0);
        }
        free(decoded);
    }
    free(encoded);
    case_result(fail, name);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests decoding invalid base64 string.
 * @return 0 on success, 1 on failure.
 */
static int case_decode_invalid(void) {
    const char *name = "decode invalid base64";
    size_t out_size = 0;
    void *decoded = kc_b64_decode("invalid!", &out_size);
    int fail = 0;
    fail += expect_true("decode invalid returns NULL", decoded == NULL);
    case_result(fail, name);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests decoding string with length not divisible by 4.
 * @return 0 on success, 1 on failure.
 */
static int case_decode_bad_length(void) {
    const char *name = "decode bad length";
    size_t out_size = 0;
    void *decoded = kc_b64_decode("abc", &out_size);
    int fail = 0;
    fail += expect_true("decode bad length returns NULL", decoded == NULL);
    case_result(fail, name);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests NULL argument handling.
 * @return 0 on success, 1 on failure.
 */
static int case_null_args(void) {
    const char *name = "NULL argument handling";
    int fail = 0;
    fail += expect_true("encode NULL data returns NULL", kc_b64_encode(NULL, 0) == NULL);
    fail += expect_true("decode NULL str returns NULL", kc_b64_decode(NULL, &(size_t){0}) == NULL);
    case_result(fail, name);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests version function.
 * @return 0 on success, 1 on failure.
 */
static int case_version(void) {
    const char *name = "version returns non-zero";
    int fail = 0;
    fail += expect_true("version returns non-zero", kc_b64_version() != 0);
    case_result(fail, name);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, 1 on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 10;
    test_case_current = 0;
    run_case(&rc, case_encode_empty);
    run_case(&rc, case_encode_hello);
    run_case(&rc, case_encode_binary);
    run_case(&rc, case_decode_empty);
    run_case(&rc, case_decode_hello);
    run_case(&rc, case_roundtrip);
    run_case(&rc, case_decode_invalid);
    run_case(&rc, case_decode_bad_length);
    run_case(&rc, case_null_args);
    run_case(&rc, case_version);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
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
    if (strcmp(argv[1], "encode-empty") == 0) return case_encode_empty();
    if (strcmp(argv[1], "encode-hello") == 0) return case_encode_hello();
    if (strcmp(argv[1], "encode-binary") == 0) return case_encode_binary();
    if (strcmp(argv[1], "decode-empty") == 0) return case_decode_empty();
    if (strcmp(argv[1], "decode-hello") == 0) return case_decode_hello();
    if (strcmp(argv[1], "roundtrip") == 0) return case_roundtrip();
    if (strcmp(argv[1], "decode-invalid") == 0) return case_decode_invalid();
    if (strcmp(argv[1], "decode-bad-length") == 0) return case_decode_bad_length();
    if (strcmp(argv[1], "null-args") == 0) return case_null_args();
    if (strcmp(argv[1], "version") == 0) return case_version();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
