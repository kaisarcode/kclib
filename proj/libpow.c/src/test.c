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

    fail += expect_true("rejects NULL challenge destination",
        kc_pow_challenge(1, NULL, &challenge_size) == KC_POW_ERROR);
    fail += expect_true("rejects NULL size destination",
        kc_pow_challenge(1, &challenge, NULL) == KC_POW_ERROR);
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
 * Tests kc_pow_size framing behavior.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_pow_size(void) {
    const char *name = "kc_pow_size";
    const char *detail = "frames complete entities and rejects malformed headers";
    unsigned char malformed[] = { 'K', 'P', 'W', 1, 2, 0, 0, 15 };
    void *challenge = NULL;
    size_t challenge_size = 0;
    size_t framed_size = 99;
    int fail = 0;

    fail += expect_true("rejects NULL output size",
        kc_pow_size("KPW", 3, NULL) == KC_POW_ERROR);
    fail += expect_true("empty input is incomplete",
        kc_pow_size(NULL, 0, &framed_size) == KC_POW_INCOMPLETE);
    fail += expect_true("short header is incomplete",
        kc_pow_size("KPW", 3, &framed_size) == KC_POW_INCOMPLETE);
    fail += expect_true("NULL complete header is rejected",
        kc_pow_size(NULL, 8, &framed_size) == KC_POW_ERROR);
    fail += expect_true("rejects invalid declared size",
        kc_pow_size(malformed, sizeof(malformed), &framed_size) == KC_POW_ERROR);

    malformed[7] = 16;
    malformed[0] = 'X';
    fail += expect_true("rejects invalid magic",
        kc_pow_size(malformed, sizeof(malformed), &framed_size) == KC_POW_ERROR);
    malformed[0] = 'K';
    malformed[3] = 2;
    fail += expect_true("rejects unknown version",
        kc_pow_size(malformed, sizeof(malformed), &framed_size) == KC_POW_ERROR);
    malformed[3] = 1;
    malformed[5] = 1;
    fail += expect_true("rejects reserved byte",
        kc_pow_size(malformed, sizeof(malformed), &framed_size) == KC_POW_ERROR);

    fail += expect_true("creates framing challenge",
        kc_pow_challenge(1, &challenge, &challenge_size) == KC_POW_OK);
    if (challenge != NULL) {
        fail += expect_true("header determines complete challenge size",
            kc_pow_size(challenge, 8, &framed_size) == KC_POW_OK);
        fail += expect_true("header size is challenge size",
            framed_size == challenge_size);
    }
    kc_pow_free(challenge);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_pow_solve input validation and output ownership.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_pow_solve(void) {
    const char *name = "kc_pow_solve";
    const char *detail = "solves valid challenges and rejects invalid framing";
    void *challenge = NULL;
    void *solution = NULL;
    size_t challenge_size = 0;
    size_t solution_size = 0;
    int fail = 0;

    fail += expect_true("rejects NULL solution destination",
        kc_pow_solve(NULL, 0, NULL, &solution_size) == KC_POW_ERROR);
    fail += expect_true("rejects NULL size destination",
        kc_pow_solve(NULL, 0, &solution, NULL) == KC_POW_ERROR);
    fail += expect_true("rejects incomplete challenge",
        kc_pow_solve("KPW", 3, &solution, &solution_size) == KC_POW_ERROR);
    fail += expect_true("creates easy challenge",
        kc_pow_challenge(4, &challenge, &challenge_size) == KC_POW_OK);
    if (challenge != NULL) {
        fail += expect_true("solves challenge",
            kc_pow_solve(challenge, challenge_size,
                &solution, &solution_size) == KC_POW_OK);
    }
    kc_pow_free(solution);
    kc_pow_free(challenge);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_pow_verify valid, invalid, and malformed inputs.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_pow_verify(void) {
    const char *name = "kc_pow_verify";
    const char *detail = "accepts matching proofs and rejects other inputs";
    unsigned char changed[48];
    void *challenge = NULL;
    void *solution = NULL;
    size_t challenge_size = 0;
    size_t solution_size = 0;
    size_t i;
    int invalid = 0;
    int fail = 0;

    fail += expect_true("rejects incomplete challenge",
        kc_pow_verify("KPW", 3, "KPW", 3) == KC_POW_ERROR);
    fail += expect_true("creates verification challenge",
        kc_pow_challenge(4, &challenge, &challenge_size) == KC_POW_OK);
    if (challenge != NULL) {
        fail += expect_true("solves verification challenge",
            kc_pow_solve(challenge, challenge_size,
                &solution, &solution_size) == KC_POW_OK);
    }
    if (challenge != NULL && solution != NULL) {
        fail += expect_true("accepts matching solution",
            kc_pow_verify(challenge, challenge_size,
                solution, solution_size) == KC_POW_OK);
        fail += expect_true("rejects truncated solution",
            kc_pow_verify(challenge, challenge_size,
                solution, solution_size - 1U) == KC_POW_ERROR);
        memcpy(changed, challenge, sizeof(changed));
        for (i = 16; i < sizeof(changed); ++i) {
            changed[i] ^= 1U;
            if (kc_pow_verify(changed, sizeof(changed),
                solution, solution_size) == KC_POW_INVALID) invalid = 1;
            changed[i] ^= 1U;
        }
        fail += expect_true("rejects solution for another challenge", invalid);
    }
    kc_pow_free(solution);
    kc_pow_free(challenge);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_pow_free and kc_pow_version.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_pow_lifecycle(void) {
    const char *name = "kc_pow_lifecycle";
    const char *detail = "releases allocations and reports the build version";
    void *challenge = NULL;
    size_t challenge_size = 0;
    int fail = 0;

    fail += expect_true("allocates value for free",
        kc_pow_challenge(1, &challenge, &challenge_size) == KC_POW_OK);
    kc_pow_free(challenge);
    kc_pow_free(NULL);
    fail += expect_true("version returns non-zero", kc_pow_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all public contract tests.
 * @return 0 on success, 1 on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 5;
    test_case_current = 0;
    run_case(&rc, case_kc_pow_challenge);
    run_case(&rc, case_kc_pow_size);
    run_case(&rc, case_kc_pow_solve);
    run_case(&rc, case_kc_pow_verify);
    run_case(&rc, case_kc_pow_lifecycle);
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
    if (strcmp(argv[1], "kc_pow_size") == 0) return case_kc_pow_size();
    if (strcmp(argv[1], "kc_pow_solve") == 0) return case_kc_pow_solve();
    if (strcmp(argv[1], "kc_pow_verify") == 0) return case_kc_pow_verify();
    if (strcmp(argv[1], "kc_pow_lifecycle") == 0) return case_kc_pow_lifecycle();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
