/**
 * test.c - libemb public API tests.
 * Summary: Contract tests for fixed-model embeddings.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "libemb.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EMB_EXPECTED_DIM 384

static int test_case_total = 0;
static int test_case_current = 0;

typedef int (*case_fn)(void);

/**
 * Prints one canonical test-case result line.
 * @param fail Nonzero when the case failed.
 * @param name Canonical test-case name.
 * @param detail Human-readable behavior description.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

/**
 * Executes one test case and accumulates its result.
 * @param rc Aggregate failed-case count.
 * @param fn Test-case function to execute.
 * @return None.
 */
static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
}

/**
 * Verifies one boolean condition.
 * @param name Check description.
 * @param condition Nonzero when the check passed.
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
 * Verifies two embedding vectors are close within tolerance.
 * @param name Check description.
 * @param a First vector.
 * @param b Second vector.
 * @param count Number of elements.
 * @param tolerance Maximum absolute difference.
 * @return 0 on success, 1 on failure.
 */
static int expect_vectors_close(
    const char *name,
    const float *a,
    const float *b,
    size_t count,
    float tolerance
) {
    size_t i;

    for (i = 0; i < count; i++) {
        if (fabsf(a[i] - b[i]) > tolerance) {
            printf("[FAIL] %s: vectors differ at %zu\n", name, i);
            return 1;
        }
    }
    return 0;
}

/**
 * Verifies two embedding vectors contain a measurable difference.
 * @param name Check description.
 * @param a First vector.
 * @param b Second vector.
 * @param count Number of elements.
 * @return 0 on success, 1 on failure.
 */
static int expect_vectors_distinct(
    const char *name,
    const float *a,
    const float *b,
    size_t count
) {
    size_t i;

    for (i = 0; i < count; i++) {
        if (fabsf(a[i] - b[i]) > 0.000001f) return 0;
    }
    printf("[FAIL] %s\n", name);
    return 1;
}

/**
 * Verifies one embedding vector has the fixed model shape and finite values.
 * @param name Check description.
 * @param vec Vector data.
 * @param count Element count.
 * @return 0 on success, 1 on failure.
 */
static int expect_vector_valid(const char *name, const float *vec, size_t count) {
    size_t i;
    int nonzero = 0;

    if (!vec || count != EMB_EXPECTED_DIM) {
        printf("[FAIL] %s: expected %d floats, got %zu\n",
            name, EMB_EXPECTED_DIM, count);
        return 1;
    }
    for (i = 0; i < count; i++) {
        if (!isfinite(vec[i])) {
            printf("[FAIL] %s: non-finite value at %zu\n", name, i);
            return 1;
        }
        if (vec[i] != 0.0f) nonzero = 1;
    }
    return expect_true(name, nonzero);
}

/**
 * Tests the public build version.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_emb_version(void) {
    int fail = expect_true("version nonzero", kc_emb_version() != 0U);

    case_result(fail, "kc_emb_version", "returns the generated build version");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests the fixed embedding dimension exposed by the model.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_emb_dimension(void) {
    size_t dimension = kc_emb_dimension();
    int fail = 0;

    fail += expect_true("dimension available", dimension != 0U);
    fail += expect_true("dimension matches embedded model",
        dimension == EMB_EXPECTED_DIM);

    case_result(fail, "kc_emb_dimension",
        "returns the embedded model vector dimension");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests direct fixed-model embedding.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_emb_embed(void) {
    float *vec = NULL;
    float *empty = NULL;
    size_t count = 0;
    size_t empty_count = 0;
    int fail = 0;

    fail += expect_int("embed text returns OK", KC_EMB_OK,
        kc_emb_embed("The quick brown fox", &vec, &count));
    fail += expect_vector_valid("text vector valid", vec, count);
    fail += expect_true("text count matches model dimension",
        count == kc_emb_dimension());

    fail += expect_int("embed empty returns OK", KC_EMB_OK,
        kc_emb_embed("", &empty, &empty_count));
    fail += expect_vector_valid("empty vector valid", empty, empty_count);
    fail += expect_true("empty count matches model dimension",
        empty_count == kc_emb_dimension());

    kc_emb_free(vec);
    kc_emb_free(empty);
    case_result(fail, "kc_emb_embed",
        "embeds text directly with the fixed local model");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests direct embedding argument and ownership contracts.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_emb_contract(void) {
    float *vec = (float *)0x1;
    size_t count = 123;
    int fail = 0;

    fail += expect_int("NULL input errors", KC_EMB_ERROR,
        kc_emb_embed(NULL, &vec, &count));
    fail += expect_true("NULL input resets vector", vec == NULL);
    fail += expect_true("NULL input resets count", count == 0);

    count = 123;
    fail += expect_int("NULL out_data errors", KC_EMB_ERROR,
        kc_emb_embed("input", NULL, &count));
    fail += expect_true("NULL out_data resets count", count == 0);

    vec = (float *)0x1;
    fail += expect_int("NULL out_count errors", KC_EMB_ERROR,
        kc_emb_embed("input", &vec, NULL));
    fail += expect_true("NULL out_count resets vector", vec == NULL);

    kc_emb_free(NULL);
    fail += expect_true("free NULL safe", 1);

    case_result(fail, "kc_emb_contract",
        "validates arguments, output reset, and ownership");
    return fail == 0 ? 0 : 1;
}

/**
 * Tests deterministic fixed-model output.
 * @return 0 when the case passes, 1 otherwise.
 */
static int case_kc_emb_determinism(void) {
    float *a = NULL;
    float *b = NULL;
    float *c = NULL;
    size_t ca = 0, cb = 0, cc = 0;
    int fail = 0;

    fail += expect_int("first embed OK", KC_EMB_OK,
        kc_emb_embed("The quick brown fox", &a, &ca));
    fail += expect_int("second embed OK", KC_EMB_OK,
        kc_emb_embed("The quick brown fox", &b, &cb));
    fail += expect_int("different embed OK", KC_EMB_OK,
        kc_emb_embed("incident response runbook", &c, &cc));
    fail += expect_true("all counts fixed",
        ca == EMB_EXPECTED_DIM && cb == EMB_EXPECTED_DIM &&
        cc == EMB_EXPECTED_DIM);

    if (a && b && ca == cb) {
        fail += expect_vectors_close("same text deterministic",
            a, b, ca, 0.000001f);
    } else {
        fail += expect_true("determinism vectors available", 0);
    }
    if (a && c && ca == cc) {
        fail += expect_vectors_distinct("different text differs", a, c, ca);
    } else {
        fail += expect_true("distinct vectors available", 0);
    }

    kc_emb_free(a);
    kc_emb_free(b);
    kc_emb_free(c);
    case_result(fail, "kc_emb_determinism",
        "keeps fixed-model output deterministic and input-sensitive");
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all reusable and platform-applicable test cases.
 * @return 0 on success, nonzero on failure.
 */
static int case_all(void) {
    int rc = 0;

    test_case_total = 5;
    test_case_current = 0;
    run_case(&rc, case_kc_emb_version);
    run_case(&rc, case_kc_emb_dimension);
    run_case(&rc, case_kc_emb_embed);
    run_case(&rc, case_kc_emb_contract);
    run_case(&rc, case_kc_emb_determinism);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one emb contract test case.
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
    if (strcmp(argv[1], "kc_emb_version") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_emb_version();
    }
    if (strcmp(argv[1], "kc_emb_dimension") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_emb_dimension();
    }
    if (strcmp(argv[1], "kc_emb_embed") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_emb_embed();
    }
    if (strcmp(argv[1], "kc_emb_contract") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_emb_contract();
    }
    if (strcmp(argv[1], "kc_emb_determinism") == 0) {
        test_case_total = 1;
        test_case_current = 1;
        return case_kc_emb_determinism();
    }
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
