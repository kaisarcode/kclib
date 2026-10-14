/**
 * test.c - libpow public API contract tests.
 * Summary: Validates challenge framing, solving, and verification.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libpow.h"

#include <stdio.h>
#include <string.h>

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints one test-case result.
 * @param fail Non-zero when the case failed.
 * @param name Canonical test-case name.
 * @param detail Test-case description.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

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
 * Tests kc_pow_challenge.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_pow_challenge(void) {
    const char *name = "kc_pow_challenge";
    const char *detail = "creates a self-contained framed challenge";
    void *challenge = NULL;
    size_t challenge_size = 0;
    size_t framed_size = 0;
    int fail = 0;

    fail += expect_true("rejects zero effort",
        kc_pow_challenge(0, &challenge, &challenge_size) == KC_POW_ERROR);
    fail += expect_true("creates challenge",
        kc_pow_challenge(16, &challenge, &challenge_size) == KC_POW_OK);
    fail += expect_true("challenge allocated", challenge != NULL);
    fail += expect_true("size prefix incomplete",
        kc_pow_size(challenge, 4, &framed_size) == KC_POW_INCOMPLETE);
    fail += expect_true("challenge size readable",
        kc_pow_size(challenge, challenge_size, &framed_size) == KC_POW_OK);
    fail += expect_true("challenge size matches", framed_size == challenge_size);

    kc_pow_free(challenge);
    kc_pow_free(NULL);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests challenge solving and verification.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_pow_solve_verify(void) {
    const char *name = "kc_pow_solve_verify";
    const char *detail = "solves and verifies one challenge without transport state";
    void *challenge = NULL;
    void *solution = NULL;
    size_t challenge_size = 0;
    size_t solution_size = 0;
    size_t framed_size = 0;
    unsigned char malformed[8] = { 'K', 'P', 'W', 1, 2, 0, 0, 15 };
    int fail = 0;

    fail += expect_true("creates easy challenge",
        kc_pow_challenge(4, &challenge, &challenge_size) == KC_POW_OK);
    if (challenge != NULL) {
        fail += expect_true("solves challenge",
            kc_pow_solve(challenge, challenge_size,
                &solution, &solution_size) == KC_POW_OK);
    }
    if (challenge != NULL && solution != NULL) {
        fail += expect_true("solution size readable",
            kc_pow_size(solution, solution_size, &framed_size) == KC_POW_OK);
        fail += expect_true("solution size matches", framed_size == solution_size);
        fail += expect_true("solution verifies",
            kc_pow_verify(challenge, challenge_size,
                solution, solution_size) == KC_POW_OK);
    }
    fail += expect_true("malformed entity rejected",
        kc_pow_size(malformed, sizeof(malformed), &framed_size) == KC_POW_ERROR);

    kc_pow_free(solution);
    kc_pow_free(challenge);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_pow_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_pow_version(void) {
    const char *name = "kc_pow_version";
    const char *detail = "returns a nonzero generated build version";
    int fail = expect_true("version is nonzero", kc_pow_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all public contract tests.
 * @return 0 on success, 1 on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 3;
    test_case_current = 0;
    run_case(&rc, case_kc_pow_challenge);
    run_case(&rc, case_kc_pow_solve_verify);
    run_case(&rc, case_kc_pow_version);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one named public contract test.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 on failure, or 2 for invalid input.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_pow_challenge") == 0) return case_kc_pow_challenge();
    if (strcmp(argv[1], "kc_pow_solve_verify") == 0) return case_kc_pow_solve_verify();
    if (strcmp(argv[1], "kc_pow_version") == 0) return case_kc_pow_version();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
