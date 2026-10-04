/**
 * test.c - librtc public API contract tests.
 * Summary: Validates the native RTC peer lifecycle and common helpers.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "librtc.h"

#include <stdio.h>
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
 * Tests peer creation validation and lifecycle.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_rtc_peer(void) {
    const char *name = "kc_rtc_peer";
    const char *detail = "creates and closes a native WebRTC peer connection";
    kc_rtc_peer_options_t invalid = {0};
    kc_rtc_peer_t *peer = NULL;
    int fail = 0;

    fail += expect_true("peer rejects NULL output",
        kc_rtc_peer(NULL, NULL) == KC_RTC_EINVAL);

    invalid.ice_server_count = 1;
    fail += expect_true("peer rejects missing ICE server array",
        kc_rtc_peer(&peer, &invalid) == KC_RTC_EINVAL);

    fail += expect_true("peer creates without ICE servers",
        kc_rtc_peer(&peer, NULL) == KC_RTC_OK && peer != NULL);
    kc_rtc_peer_close(peer);
    kc_rtc_peer_close(NULL);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests common RTC helpers.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_rtc_common(void) {
    const char *name = "kc_rtc_common";
    const char *detail = "reports stable status text and build version";
    int fail = 0;

    fail += expect_true("OK status text",
        strcmp(kc_rtc_strerror(KC_RTC_OK), "ok") == 0);
    fail += expect_true("invalid status text",
        strcmp(kc_rtc_strerror(KC_RTC_EINVAL), "invalid argument") == 0);
    fail += expect_true("version is nonzero", kc_rtc_version() != 0U);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all library contract test cases.
 * @return 0 on success, non-zero on failure.
 */
static int case_all(void) {
    int rc = 0;

    test_case_total = 2;
    test_case_current = 0;
    run_case(&rc, case_kc_rtc_peer);
    run_case(&rc, case_kc_rtc_common);

    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one librtc contract test case.
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
    if (strcmp(argv[1], "kc_rtc_peer") == 0) return case_kc_rtc_peer();
    if (strcmp(argv[1], "kc_rtc_common") == 0) return case_kc_rtc_common();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
