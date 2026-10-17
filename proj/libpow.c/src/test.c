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

static int expect_true(const char *name, int condition);

/**
 * Stores one big-endian 64-bit test value.
 * @param p Destination bytes.
 * @param value Value to encode.
 * @return None.
 */
static void store64be(unsigned char *p, uint64_t value) {
    int i;
    for (i = 7; i >= 0; --i) {
        p[i] = (unsigned char)value;
        value >>= 8;
    }
}

/**
 * Creates a fixed SHA-256 challenge entity for a known vector.
 * @param challenge Destination 48-byte challenge buffer.
 * @param target Exclusive proof target.
 * @param bytes Exact 32-byte SHA-256 challenge input.
 * @return None.
 */
static void sha256_challenge(unsigned char challenge[48], uint64_t target,
    const unsigned char bytes[32]) {
    memset(challenge, 0, 48U);
    challenge[0] = 'K'; challenge[1] = 'P'; challenge[2] = 'W';
    challenge[3] = 2; challenge[4] = 1; challenge[5] = KC_POW_BACKEND_SHA256;
    challenge[7] = 48;
    store64be(challenge + 8U, target);
    memcpy(challenge + 16U, bytes, 32U);
}

/**
 * Creates a fixed SHA-256 solution entity for a known vector.
 * @param solution Destination 16-byte solution buffer.
 * @param nonce Candidate nonce.
 * @return None.
 */
static void sha256_solution(unsigned char solution[16], uint64_t nonce) {
    memset(solution, 0, 16U);
    solution[0] = 'K'; solution[1] = 'P'; solution[2] = 'W';
    solution[3] = 2; solution[4] = 2; solution[5] = KC_POW_BACKEND_SHA256;
    solution[7] = 16;
    store64be(solution + 8U, nonce);
}

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

/**
 * Runs one named contract test.
 * @param rc Failure accumulator.
 * @param fn Test function.
 * @return None.
 */
static void run_case(int *rc, int (*fn)(void)) {
    test_case_current++;
    *rc += fn();
}

/**
 * Tests backend names, availability, and explicit challenge selection.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_pow_backend(void) {
    const char *name = "kc_pow_backend";
    const char *detail = "selects explicit backends without fallback";
    void *challenge = NULL;
    void *solution = NULL;
    size_t challenge_size = 0;
    size_t solution_size = 0;
    kc_pow_backend_t backend = KC_POW_BACKEND_AUTO;
    int fail = 0;

    fail += expect_true("SHA-256 is available",
        kc_pow_backend_available(KC_POW_BACKEND_SHA256) == 1);
    fail += expect_true("AUTO is available",
        kc_pow_backend_available(KC_POW_BACKEND_AUTO) == 1);
    fail += expect_true("invalid backend is unavailable",
        kc_pow_backend_available((kc_pow_backend_t)99) == 0);
    fail += expect_true("stable SHA-256 name",
        strcmp(kc_pow_backend_name(KC_POW_BACKEND_SHA256), "sha256") == 0);
    fail += expect_true("stable HashWX name",
        strcmp(kc_pow_backend_name(KC_POW_BACKEND_HASHWX), "hashwx") == 0);
    fail += expect_true("parses SHA-256 name",
        kc_pow_backend_from_name("sha256") == KC_POW_BACKEND_SHA256);
    fail += expect_true("unknown name is invalid",
        kc_pow_backend_from_name("unknown") == KC_POW_BACKEND_INVALID);
    fail += expect_true("default challenge uses SHA-256",
        kc_pow_challenge(1, &challenge, &challenge_size) == KC_POW_OK &&
        kc_pow_get_backend(challenge, challenge_size, &backend) == KC_POW_OK &&
        backend == KC_POW_BACKEND_SHA256);
    fail += expect_true("default SHA-256 solves and verifies",
        kc_pow_solve(challenge, challenge_size, &solution, &solution_size) ==
        KC_POW_OK && kc_pow_verify(challenge, challenge_size, solution,
        solution_size) == KC_POW_OK);
    kc_pow_free(solution);
    kc_pow_free(challenge);
    challenge = NULL;
    solution = NULL;
    fail += expect_true("AUTO resolves to SHA-256",
        kc_pow_challenge_with_backend(KC_POW_BACKEND_AUTO, 1, &challenge,
        &challenge_size) == KC_POW_OK && kc_pow_get_backend(challenge,
        challenge_size, &backend) == KC_POW_OK && backend == KC_POW_BACKEND_SHA256);
    fail += expect_true("AUTO SHA-256 solves and verifies",
        kc_pow_solve(challenge, challenge_size, &solution, &solution_size) ==
        KC_POW_OK && kc_pow_verify(challenge, challenge_size, solution,
        solution_size) == KC_POW_OK);
    kc_pow_free(solution);
    kc_pow_free(challenge);
    challenge = NULL;
    solution = NULL;
    fail += expect_true("explicit SHA-256 is selected",
        kc_pow_challenge_with_backend(KC_POW_BACKEND_SHA256, 1, &challenge,
        &challenge_size) == KC_POW_OK && kc_pow_get_backend(challenge,
        challenge_size, &backend) == KC_POW_OK && backend == KC_POW_BACKEND_SHA256);
    fail += expect_true("explicit SHA-256 solves and verifies",
        kc_pow_solve(challenge, challenge_size, &solution, &solution_size) ==
        KC_POW_OK && kc_pow_verify(challenge, challenge_size, solution,
        solution_size) == KC_POW_OK);
    kc_pow_free(solution);
    kc_pow_free(challenge);
    challenge = NULL;
    solution = NULL;
    fail += expect_true("unavailable backend does not fall back",
        kc_pow_challenge_with_backend((kc_pow_backend_t)99, 1, &challenge,
        &challenge_size) == KC_POW_ERROR && challenge == NULL);
    if (kc_pow_backend_available(KC_POW_BACKEND_HASHWX)) {
        fail += expect_true("explicit HashWX is selected",
            kc_pow_challenge_with_backend(KC_POW_BACKEND_HASHWX, 1, &challenge,
            &challenge_size) == KC_POW_OK && kc_pow_get_backend(challenge,
            challenge_size, &backend) == KC_POW_OK && backend == KC_POW_BACKEND_HASHWX);
        fail += expect_true("explicit HashWX solves and verifies",
            kc_pow_solve(challenge, challenge_size, &solution, &solution_size) ==
            KC_POW_OK && kc_pow_verify(challenge, challenge_size, solution,
            solution_size) == KC_POW_OK);
        kc_pow_free(solution);
        kc_pow_free(challenge);
    } else {
        fail += expect_true("uncompiled HashWX does not fall back",
            kc_pow_challenge_with_backend(KC_POW_BACKEND_HASHWX, 1, &challenge,
            &challenge_size) == KC_POW_ERROR && challenge == NULL);
    }
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests canonical SHA-256 vectors and strict target boundaries.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_pow_sha256_vectors(void) {
    const char *name = "kc_pow_sha256_vectors";
    const char *detail = "uses big-endian nonce bytes and strict targets";
    const unsigned char zeroes[32] = { 0 };
    const unsigned char sequence[32] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
    };
    unsigned char challenge[48];
    unsigned char solution[16];
    const uint64_t zero_value = UINT64_C(0x2c34ce1df23b838c);
    const uint64_t max_value = UINT64_C(0x7e6bccb10cce4427);
    int fail = 0;

    sha256_solution(solution, 0);
    sha256_challenge(challenge, zero_value + 1U, zeroes);
    fail += expect_true("nonce zero vector is valid",
        kc_pow_verify(challenge, sizeof(challenge), solution,
        sizeof(solution)) == KC_POW_OK);
    sha256_challenge(challenge, zero_value, zeroes);
    fail += expect_true("target equal to value is invalid",
        kc_pow_verify(challenge, sizeof(challenge), solution,
        sizeof(solution)) == KC_POW_INVALID);
    sha256_challenge(challenge, zero_value - 1U, zeroes);
    fail += expect_true("target below value is invalid",
        kc_pow_verify(challenge, sizeof(challenge), solution,
        sizeof(solution)) == KC_POW_INVALID);
    sha256_solution(solution, UINT64_MAX);
    sha256_challenge(challenge, max_value + 1U, sequence);
    fail += expect_true("UINT64_MAX nonce vector is valid",
        kc_pow_verify(challenge, sizeof(challenge), solution,
        sizeof(solution)) == KC_POW_OK);
    sha256_challenge(challenge, max_value, sequence);
    fail += expect_true("UINT64_MAX boundary is strict",
        kc_pow_verify(challenge, sizeof(challenge), solution,
        sizeof(solution)) == KC_POW_INVALID);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
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
    test_case_total = 7;
    test_case_current = 0;
    run_case(&rc, case_kc_pow_backend);
    run_case(&rc, case_kc_pow_sha256_vectors);
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
    if (strcmp(argv[1], "kc_pow_backend") == 0) return case_kc_pow_backend();
    if (strcmp(argv[1], "kc_pow_sha256_vectors") == 0) {
        return case_kc_pow_sha256_vectors();
    }
    if (strcmp(argv[1], "kc_pow_challenge") == 0) return case_kc_pow_challenge();
    if (strcmp(argv[1], "kc_pow_size") == 0) return case_kc_pow_size();
    if (strcmp(argv[1], "kc_pow_solve") == 0) return case_kc_pow_solve();
    if (strcmp(argv[1], "kc_pow_verify") == 0) return case_kc_pow_verify();
    if (strcmp(argv[1], "kc_pow_lifecycle") == 0) return case_kc_pow_lifecycle();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
