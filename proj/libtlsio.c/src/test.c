/**
 * test.c - libtlsio public API contract tests.
 * Summary: Validates the portable TLS connection API contract.
 *
 * Author: KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libtlsio.h"

#include <stdio.h>
#include <string.h>

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints one test case result.
 * @param fail Non-zero when the case failed.
 * @param name Canonical test case name.
 * @param detail Behavior verified by the case.
 * @return void.
 */
static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

/**
 * Runs one test case with counter tracking.
 * @param rc Failure accumulator.
 * @param fn Test case function.
 * @return void.
 */
static void run_case(int *rc, int (*fn)(void)) {
    test_case_current++;
    *rc += fn();
}

/**
 * Verifies one boolean condition.
 * @param name Check description.
 * @param condition Non-zero when the check passed.
 * @return Zero on success, otherwise one.
 */
static int expect_true(const char *name, int condition) {
    if (!condition) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

/**
 * Tests validation at the connection boundary.
 * @return Zero on success, otherwise one.
 */
static int case_kc_tlsio_connect(void) {
    const char *name = "kc_tlsio_connect";
    const char *detail = "rejects incomplete connection options";
    kc_tlsio_options_t options;
    kc_tlsio_t *tls;
    int fail;

    memset(&options, 0, sizeof(options));
    tls = (kc_tlsio_t *)1;
    fail = 0;
    fail += expect_true(
        "connect rejects NULL output",
        kc_tlsio_connect(NULL, &options) == KC_TLSIO_EINVAL
    );
    fail += expect_true(
        "connect rejects NULL options",
        kc_tlsio_connect(&tls, NULL) == KC_TLSIO_EINVAL
    );
    fail += expect_true("failed connect clears output", tls == NULL);
    options.host = "";
    fail += expect_true(
        "connect rejects empty host",
        kc_tlsio_connect(&tls, &options) == KC_TLSIO_EINVAL
    );

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests validation on read and write operations.
 * @return Zero on success, otherwise one.
 */
static int case_kc_tlsio_io(void) {
    const char *name = "kc_tlsio_io";
    const char *detail = "rejects invalid handles and output destinations";
    void *data;
    size_t size;
    int fail;

    data = (void *)1;
    size = 1U;
    fail = 0;
    fail += expect_true(
        "write rejects NULL handle",
        kc_tlsio_write(NULL, "x", 1U) == KC_TLSIO_EINVAL
    );
    fail += expect_true(
        "read rejects NULL handle",
        kc_tlsio_read(NULL, &data, &size) == KC_TLSIO_EINVAL
    );
    fail += expect_true("read clears data output", data == NULL);
    fail += expect_true("read clears size output", size == 0U);
    fail += expect_true(
        "read rejects NULL data output",
        kc_tlsio_read(NULL, NULL, &size) == KC_TLSIO_EINVAL
    );
    kc_tlsio_free(NULL);
    kc_tlsio_close(NULL);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests public status strings and generated version.
 * @return Zero on success, otherwise one.
 */
static int case_kc_tlsio_metadata(void) {
    const char *name = "kc_tlsio_metadata";
    const char *detail = "exposes stable status text and build version";
    int fail;

    fail = 0;
    fail += expect_true(
        "success status has text",
        strcmp(kc_tlsio_strerror(KC_TLSIO_OK), "success") == 0
    );
    fail += expect_true(
        "unknown status has text",
        strcmp(kc_tlsio_strerror(9999), "unknown tlsio status") == 0
    );
    fail += expect_true("version is nonzero", kc_tlsio_version() != 0U);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all library contract test cases.
 * @return Zero on success, otherwise non-zero.
 */
static int case_all(void) {
    int rc;

    rc = 0;
    test_case_total = 3;
    test_case_current = 0;
    run_case(&rc, case_kc_tlsio_connect);
    run_case(&rc, case_kc_tlsio_io);
    run_case(&rc, case_kc_tlsio_metadata);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one libtlsio contract test case.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Zero on success, one or two on failure.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_tlsio_connect") == 0) {
        return case_kc_tlsio_connect();
    }
    if (strcmp(argv[1], "kc_tlsio_io") == 0) return case_kc_tlsio_io();
    if (strcmp(argv[1], "kc_tlsio_metadata") == 0) {
        return case_kc_tlsio_metadata();
    }
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
