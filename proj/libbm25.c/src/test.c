/**
 * test.c - libbm25 contract tests.
 * Summary: BM25 scoring, Unicode analysis, persistence, and CLI contracts.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "libbm25.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef KC_CLI_PATH
#define KC_CLI_PATH "bm25"
#endif

typedef int (*case_fn)(void);

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Print the canonical summary of a test case.
 * @param failed Whether the case failed.
 * @param name Test case name.
 * @param detail Tested behavior.
 * @return None.
 */
static void case_result(int failed, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        failed ? "FAIL" : "PASS", name, detail);
}

/**
 * Execute one contract case and advance the case counter.
 * @param callback Test case to execute.
 * @return The case's failure count.
 */
static int run_case(case_fn callback) {
    test_case_current++;
    return callback() != 0;
}

/**
 * Count a failed Boolean expectation.
 * @param value Whether the condition holds.
 * @return Zero if true, one otherwise.
 */
static int expect(int value) {
    return value ? 0 : 1;
}

/**
 * Compare floating-point scores within a tolerance.
 * @param actual Observed score.
 * @param expected Expected score.
 * @return Zero for a close match, otherwise one.
 */
static int expect_score(double actual, double expected) {
    return !isfinite(actual) || fabs(actual - expected) > 1e-10 ? 1 : 0;
}

/**
 * Build the reference three-document corpus.
 * @return Built index, or NULL on failure.
 */
static kc_bm25_t *reference_index(void) {
    kc_bm25_t *index = NULL;

    if (kc_bm25_open(&index, NULL) != KC_BM25_OK) return NULL;
    if (kc_bm25_add(index, "repeat", "the quick brown fox") != KC_BM25_OK ||
        kc_bm25_add(index, "repeat", "brown brown dog") != KC_BM25_OK ||
        kc_bm25_add(index, "third", "slow fox") != KC_BM25_OK ||
        kc_bm25_build(index) != KC_BM25_OK)
    {
        kc_bm25_close(index);
        return NULL;
    }
    return index;
}

/**
 * Verify exact scores, term-frequency saturation, and tie resolution.
 * @return Test failure count.
 */
static int case_bm25_scoring(void) {
    kc_bm25_t *index = reference_index();
    kc_bm25_result_t *results = NULL;
    kc_bm25_search_options_t options = {0};
    size_t count = 0;
    double k1 = 1.2, b = 0.75;
    double idf = log1p(1.5 / 2.5);
    double avgdl = 3.0;
    double first = idf * 2.2 / (1.0 + 1.2 * (0.25 + 0.75 * 4.0 / avgdl));
    double second = idf * 4.4 / (2.0 + 1.2 * (0.25 + 0.75 * 3.0 / avgdl));
    int fail = 0;

    if (index == NULL) {
        case_result(1, "bm25_scoring", "uses the documented BM25 formula");
        return 1;
    }
    fail |= expect(kc_bm25_search(index, "brown", NULL,
        &results, &count) == KC_BM25_OK);
    fail |= expect(count == 2);
    if (count == 2 && results != NULL) {
        fail |= expect(results[0].ordinal == 1 && results[1].ordinal == 0);
        fail |= expect(strcmp(results[0].id, "repeat") == 0 &&
            strcmp(results[1].id, "repeat") == 0);
        fail |= expect_score(results[0].score, second);
        fail |= expect_score(results[1].score, first);
    }
    kc_bm25_free(results);
    results = NULL;
    options.k1 = &k1;
    options.b = &b;
    k1 = 0;
    fail |= expect(kc_bm25_search(index, "brown brown", &options,
        &results, &count) == KC_BM25_OK);
    fail |= expect(count == 2);
    if (count == 2 && results != NULL) {
        fail |= expect(results[0].ordinal == 0 && results[1].ordinal == 1);
        fail |= expect_score(results[0].score, idf);
        fail |= expect_score(results[1].score, idf);
    }
    kc_bm25_free(results);
    results = NULL;
    k1 = 1.2;
    b = 0;
    fail |= expect(kc_bm25_search(index, "brown", &options,
        &results, &count) == KC_BM25_OK);
    fail |= expect(count == 2);
    if (count == 2 && results != NULL) {
        fail |= expect_score(results[0].score, idf * 4.4 / 3.2);
        fail |= expect_score(results[1].score, idf);
    }
    kc_bm25_free(results);
    results = NULL;
    b = 1;
    fail |= expect(kc_bm25_search(index, "brown", &options,
        &results, &count) == KC_BM25_OK);
    fail |= expect(count == 2);
    kc_bm25_free(results);
    kc_bm25_close(index);
    case_result(fail, "bm25_scoring",
        "uses positive IDF, BM25 parameters, and deterministic ordinals");
    return fail;
}

/**
 * Verify index mutation, empty results, and invalid input handling.
 * @return Test failure count.
 */
static int case_bm25_contract(void) {
    kc_bm25_t *index = NULL;
    kc_bm25_result_t *results = (kc_bm25_result_t *)1;
    kc_bm25_search_options_t options = {0};
    size_t count = 99, limit = 0;
    double bad = -1;
    int accents = 2;
    kc_bm25_options_t bad_options = {&accents};
    int fail = 0;

    fail |= expect(kc_bm25_open(NULL, NULL) == KC_BM25_EINVAL);
    fail |= expect(kc_bm25_open(&index, &bad_options) == KC_BM25_EINVAL);
    fail |= expect(index == NULL);
    fail |= expect(kc_bm25_open(&index, NULL) == KC_BM25_OK);
    if (index == NULL) return 1;
    fail |= expect(kc_bm25_count(index) == 0);
    fail |= expect(kc_bm25_search(index, "absent", NULL,
        &results, &count) == KC_BM25_ESTATE);
    fail |= expect(results == NULL && count == 0);
    fail |= expect(kc_bm25_add(index, "id", "\xC3\x28") == KC_BM25_EUTF8);
    fail |= expect(kc_bm25_add(index, "", "text") == KC_BM25_EINVAL);
    fail |= expect(kc_bm25_add(index, "id", "") == KC_BM25_OK);
    fail |= expect(kc_bm25_build(index) == KC_BM25_OK);
    fail |= expect(kc_bm25_search(index, "nothing", NULL,
        &results, &count) == KC_BM25_OK);
    fail |= expect(results == NULL && count == 0);
    fail |= expect(kc_bm25_search(index, "\xC3\x28", NULL,
        &results, &count) == KC_BM25_EUTF8);
    options.k1 = &bad;
    fail |= expect(kc_bm25_search(index, "x", &options,
        &results, &count) == KC_BM25_EINVAL);
    options.k1 = NULL;
    options.limit = &limit;
    fail |= expect(kc_bm25_search(index, "x", &options,
        &results, &count) == KC_BM25_OK);
    fail |= expect(count == 0);
    fail |= expect(kc_bm25_add(index, "later", "network") == KC_BM25_OK);
    fail |= expect(kc_bm25_search(index, "network", NULL,
        &results, &count) == KC_BM25_ESTATE);
    fail |= expect(kc_bm25_build(index) == KC_BM25_OK);
    fail |= expect(kc_bm25_search(index, "network", NULL,
        &results, &count) == KC_BM25_OK);
    fail |= expect(count == 1);
    if (count == 1 && results != NULL)
        fail |= expect(results[0].ordinal == 1);
    kc_bm25_free(results);
    kc_bm25_close(index);
    kc_bm25_close(NULL);
    case_result(fail, "bm25_contract",
        "handles empty inputs, malformed UTF-8, and rebuild lifecycle");
    return fail;
}

/**
 * Verify Unicode lexical matching and optional accent normalization.
 * @return Test failure count.
 */
static int case_bm25_unicode(void) {
    kc_bm25_t *index = NULL;
    kc_bm25_result_t *results = NULL;
    size_t count = 0;
    int folding = 1;
    kc_bm25_options_t options = {&folding};
    int fail = 0;

    fail |= expect(kc_bm25_open(&index, NULL) == KC_BM25_OK);
    if (index == NULL) return 1;
    fail |= expect(kc_bm25_add(index, "cafe", "CAFÉ y año") == KC_BM25_OK);
    fail |= expect(kc_bm25_add(index, "ano", "ano 2026 IPv6") == KC_BM25_OK);
    fail |= expect(kc_bm25_add(index, "cn", "中文指南") == KC_BM25_OK);
    fail |= expect(kc_bm25_build(index) == KC_BM25_OK);
    fail |= expect(kc_bm25_search(index, "cafe\xCC\x81", NULL,
        &results, &count) == KC_BM25_OK);
    fail |= expect(count == 1 && results != NULL);
    if (count == 1 && results != NULL)
        fail |= expect(strcmp(results[0].id, "cafe") == 0);
    kc_bm25_free(results);
    results = NULL;
    fail |= expect(kc_bm25_search(index, "cafe", NULL,
        &results, &count) == KC_BM25_OK);
    fail |= expect(results == NULL && count == 0);
    fail |= expect(kc_bm25_search(index, "año", NULL,
        &results, &count) == KC_BM25_OK);
    fail |= expect(count == 1 && results != NULL);
    kc_bm25_free(results);
    results = NULL;
    fail |= expect(kc_bm25_search(index, "ano", NULL,
        &results, &count) == KC_BM25_OK);
    fail |= expect(count == 1 && results != NULL);
    kc_bm25_free(results);
    results = NULL;
    fail |= expect(kc_bm25_search(index, "IPv6", NULL,
        &results, &count) == KC_BM25_OK);
    fail |= expect(count == 1 && results != NULL);
    if (count == 1 && results != NULL)
        fail |= expect(strcmp(results[0].id, "ano") == 0);
    kc_bm25_free(results);
    results = NULL;
    fail |= expect(kc_bm25_search(index, "中文指南", NULL,
        &results, &count) == KC_BM25_OK);
    fail |= expect(count == 1 && results != NULL);
    if (count == 1 && results != NULL)
        fail |= expect(strcmp(results[0].id, "cn") == 0);
    kc_bm25_free(results);
    kc_bm25_close(index);

    index = NULL;
    fail |= expect(kc_bm25_open(&index, &options) == KC_BM25_OK);
    if (index == NULL) return 1;
    fail |= expect(kc_bm25_fold_accents(index) == 1);
    fail |= expect(kc_bm25_add(index, "cafe", "café año") == KC_BM25_OK);
    fail |= expect(kc_bm25_build(index) == KC_BM25_OK);
    fail |= expect(kc_bm25_search(index, "cafe ano", NULL,
        &results, &count) == KC_BM25_OK);
    fail |= expect(count == 1 && results != NULL);
    if (count == 1 && results != NULL)
        fail |= expect(strcmp(results[0].id, "cafe") == 0);
    kc_bm25_free(results);
    kc_bm25_close(index);
    case_result(fail, "bm25_unicode",
        "normalizes UTF-8, accents, case, numbers, and Unicode text");
    return fail;
}

/**
 * Verify format persistence, deterministic bytes, and corruption detection.
 * @return Test failure count.
 */
static int case_bm25_persistence(void) {
    const char *first = "bm25-first.bin";
    const char *second = "bm25-second.bin";
    kc_bm25_t *index = reference_index();
    kc_bm25_t *loaded = NULL;
    kc_bm25_result_t *results = NULL;
    size_t count = 0;
    FILE *a = NULL, *b = NULL;
    int fail = 0;

    if (index == NULL) return 1;
    fail |= expect(kc_bm25_save(index, first) == KC_BM25_OK);
    fail |= expect(kc_bm25_save(index, second) == KC_BM25_OK);
    fail |= expect(kc_bm25_load(&loaded, first) == KC_BM25_OK);
    if (loaded != NULL) {
        fail |= expect(kc_bm25_count(loaded) == 3);
        fail |= expect(kc_bm25_add(loaded, "bad", "text") == KC_BM25_ESTATE);
        fail |= expect(kc_bm25_build(loaded) == KC_BM25_ESTATE);
        fail |= expect(kc_bm25_count(loaded) == kc_bm25_count(index));
        fail |= expect(kc_bm25_search(loaded, "brown", NULL,
            &results, &count) == KC_BM25_OK);
        fail |= expect(count == 2);
        if (count == 2 && results != NULL) {
            double idf = log1p(1.5 / 2.5);
            double expected = idf * 4.4 / 3.2;

            fail |= expect(results[0].ordinal == 1);
            fail |= expect(strcmp(results[0].content,
                "brown brown dog") == 0);
            fail |= expect_score(results[0].score, expected);
            fail |= expect(results[1].ordinal == 0);
        }
        kc_bm25_free(results);
    }
    a = fopen(first, "rb");
    b = fopen(second, "rb");
    if (a != NULL && b != NULL) {
        int ca, cb;
        do {
            ca = fgetc(a);
            cb = fgetc(b);
            if (ca != cb) fail = 1;
        } while (ca != EOF && cb != EOF);
    } else fail = 1;
    if (a != NULL) fclose(a);
    if (b != NULL) fclose(b);
    kc_bm25_close(loaded);
    loaded = NULL;
    {
        kc_bm25_t *empty = NULL;
        kc_bm25_t *restored = NULL;
        kc_bm25_result_t *none = NULL;
        size_t none_count = 99;

        fail |= expect(kc_bm25_open(&empty, NULL) == KC_BM25_OK);
        if (empty != NULL) {
            fail |= expect(kc_bm25_build(empty) == KC_BM25_OK);
            fail |= expect(kc_bm25_save(empty, second) == KC_BM25_OK);
            fail |= expect(kc_bm25_load(&restored, second) == KC_BM25_OK);
            if (restored != NULL) {
                fail |= expect(kc_bm25_count(restored) == 0);
                fail |= expect(kc_bm25_search(restored, "anything", NULL,
                    &none, &none_count) == KC_BM25_OK);
                fail |= expect(none == NULL && none_count == 0);
                fail |= expect(kc_bm25_add(restored, "x", "y") ==
                    KC_BM25_ESTATE);
                kc_bm25_free(none);
                kc_bm25_close(restored);
            }
            kc_bm25_close(empty);
        }
    }
    a = fopen(second, "r+b");
    if (a != NULL) {
        fputc('X', a);
        fclose(a);
        fail |= expect(kc_bm25_load(&loaded, second) ==
            KC_BM25_EFORMAT);
    } else fail = 1;
    kc_bm25_close(loaded);
    kc_bm25_close(index);
    remove(first);
    remove(second);
    case_result(fail, "bm25_persistence",
        "round-trips and rejects incompatible or corrupt binary data");
    return fail;
}

/**
 * Verify ranking with many nonmatching records.
 * @return Test failure count.
 */
static int case_bm25_corpus(void) {
    kc_bm25_t *index = NULL;
    kc_bm25_result_t *results = NULL;
    char id[40];
    size_t count = 0;
    int fail = 0;

    if (kc_bm25_open(&index, NULL) != KC_BM25_OK) return 1;
    for (size_t i = 0; i < 1000; i++) {
        snprintf(id, sizeof(id), "doc-%zu", i);
        if (kc_bm25_add(index, id, i == 613 ?
            "distinctive target phrase" : "ordinary repeated content")
            != KC_BM25_OK)
        {
            fail = 1;
            break;
        }
    }
    if (!fail) fail |= expect(kc_bm25_build(index) == KC_BM25_OK);
    if (!fail) {
        fail |= expect(kc_bm25_search(index, "distinctive", NULL,
            &results, &count) == KC_BM25_OK);
        fail |= expect(count == 1);
        if (count == 1 && results != NULL)
            fail |= expect(results[0].ordinal == 613);
    }
    kc_bm25_free(results);
    kc_bm25_close(index);
    case_result(fail, "bm25_corpus",
        "limits candidate retrieval to matching inverted postings");
    return fail;
}

/**
 * Validate search limits, multi-term queries, and parameter boundaries.
 * @return Zero when the search-options contract holds, otherwise one.
 */
static int case_bm25_options(void) {
    kc_bm25_t *index = reference_index();
    kc_bm25_result_t *results = (kc_bm25_result_t *)1;
    kc_bm25_search_options_t options = {0};
    size_t count = 99;
    size_t limit = 1;
    double k1 = 1.2;
    double b = 0.75;
    int fail = 0;

    if (index == NULL) {
        case_result(1, "bm25_options", "validates search options and matching");
        return 1;
    }
    options.limit = &limit;
    options.k1 = &k1;
    options.b = &b;
    fail += expect(kc_bm25_search(index, "brown fox", &options,
        &results, &count) == KC_BM25_OK);
    fail += expect(count == 1 && results != NULL);
    if (count == 1 && results != NULL) {
        fail += expect(results[0].ordinal == 0);
        fail += expect(strcmp(results[0].content, "the quick brown fox") == 0);
        fail += expect(isfinite(results[0].score) && results[0].score > 0);
    }
    kc_bm25_free(results);
    results = NULL;
    limit = 100;
    fail += expect(kc_bm25_search(index, "brown fox", &options,
        &results, &count) == KC_BM25_OK);
    fail += expect(count == 3 && results != NULL);
    if (count == 3 && results != NULL) {
        fail += expect(results[0].ordinal == 0);
        fail += expect(results[0].score >= results[1].score);
        fail += expect(results[1].score >= results[2].score);
    }
    kc_bm25_free(results);
    results = NULL;
    fail += expect(kc_bm25_search(index, "nonexistent", &options,
        &results, &count) == KC_BM25_OK);
    fail += expect(results == NULL && count == 0);
    fail += expect(kc_bm25_search(index, "!?...", &options,
        &results, &count) == KC_BM25_OK);
    fail += expect(results == NULL && count == 0);

    k1 = -0.1;
    results = (kc_bm25_result_t *)1;
    count = 99;
    fail += expect(kc_bm25_search(index, "brown", &options,
        &results, &count) == KC_BM25_EINVAL);
    fail += expect(results == NULL && count == 0);
    k1 = NAN;
    fail += expect(kc_bm25_search(index, "brown", &options,
        &results, &count) == KC_BM25_EINVAL);
    k1 = INFINITY;
    fail += expect(kc_bm25_search(index, "brown", &options,
        &results, &count) == KC_BM25_EINVAL);
    k1 = 1.2;
    b = -0.1;
    fail += expect(kc_bm25_search(index, "brown", &options,
        &results, &count) == KC_BM25_EINVAL);
    b = 1.1;
    fail += expect(kc_bm25_search(index, "brown", &options,
        &results, &count) == KC_BM25_EINVAL);
    b = NAN;
    fail += expect(kc_bm25_search(index, "brown", &options,
        &results, &count) == KC_BM25_EINVAL);
    b = 0.75;
    limit = 0;
    fail += expect(kc_bm25_search(index, "brown", &options,
        &results, &count) == KC_BM25_OK);
    fail += expect(results == NULL && count == 0);

    fail += expect(kc_bm25_search(NULL, "brown", NULL,
        &results, &count) == KC_BM25_EINVAL);
    fail += expect(results == NULL && count == 0);
    fail += expect(kc_bm25_search(index, NULL, NULL,
        &results, &count) == KC_BM25_EINVAL);
    fail += expect(results == NULL && count == 0);
    fail += expect(kc_bm25_search(index, "brown", NULL,
        NULL, &count) == KC_BM25_EINVAL);
    fail += expect(count == 0);
    fail += expect(kc_bm25_search(index, "brown", NULL,
        &results, NULL) == KC_BM25_EINVAL);
    fail += expect(results == NULL);
    kc_bm25_close(index);
    case_result(fail, "bm25_options",
        "validates multi-term ranking, empty matches, limits and bad parameters");
    return fail != 0;
}

/**
 * Verify stable top-K ordering with repeated public IDs and equal scores.
 * @return Zero if all tie-breaking and top-K cases pass, otherwise one.
 */
static int case_bm25_topk(void) {
    kc_bm25_t *index = NULL;
    kc_bm25_result_t *results = NULL;
    kc_bm25_search_options_t options = {0};
    size_t limit = 7, count = 0;
    int fail = 0;

    if (kc_bm25_open(&index, NULL) != KC_BM25_OK) return 1;
    for (size_t i = 0; i < 128; i++) {
        if (kc_bm25_add(index, "same-id", "shared common term") != KC_BM25_OK) {
            fail++;
            break;
        }
    }
    if (!fail) fail += expect(kc_bm25_build(index) == KC_BM25_OK);
    options.limit = &limit;
    if (!fail) {
        fail += expect(kc_bm25_search(index, "shared", &options,
            &results, &count) == KC_BM25_OK);
        fail += expect(count == 7 && results != NULL);
        if (count == 7 && results != NULL) {
            for (size_t i = 0; i < count; i++) {
                fail += expect(results[i].ordinal == i);
                fail += expect(strcmp(results[i].id, "same-id") == 0);
                fail += expect(strcmp(results[i].content,
                    "shared common term") == 0);
                fail += expect(isfinite(results[i].score) &&
                    results[i].score > 0);
            }
        }
        kc_bm25_free(results);
        results = NULL;
        limit = 128;
        fail += expect(kc_bm25_search(index, "shared", &options,
            &results, &count) == KC_BM25_OK);
        fail += expect(count == 128 && results != NULL);
        if (count == 128 && results != NULL) {
            for (size_t i = 0; i < count; i++)
                fail += expect(results[i].ordinal == i);
        }
    }
    kc_bm25_free(results);
    kc_bm25_close(index);
    case_result(fail, "bm25_topk",
        "returns deterministic top-K results for distinct records sharing IDs");
    return fail != 0;
}

/**
 * Replace a single byte in an existing index file.
 * @param path Index path.
 * @param offset Byte offset to modify.
 * @param value Replacement byte.
 * @return Zero on successful modification, otherwise one.
 */
static int bm25_patch_byte(const char *path, long offset, int value) {
    FILE *file = fopen(path, "r+b");
    int failed = 1;

    if (file == NULL) return 1;
    if (fseek(file, offset, SEEK_SET) == 0 &&
        fputc(value, file) != EOF && fflush(file) == 0)
        failed = 0;
    if (fclose(file) != 0) failed = 1;
    return failed;
}

/**
 * Reject invalid serialized index headers and truncated records.
 * @return Zero when all invalid files are rejected, otherwise one.
 */
static int case_bm25_file_contract(void) {
    const char *path = "bm25-invalid.bin";
    kc_bm25_t *index = reference_index();
    kc_bm25_t *loaded = NULL;
    int fail = 0;
    const struct {
        long offset;
        int value;
    } changes[] = {
        {0, 'X'},
        {8, 2},
        {12, 255},
        {20, 255},
        {28, 255},
        {44, 0}
    };

    if (index == NULL) return 1;
    fail += expect(kc_bm25_load(NULL, path) == KC_BM25_EINVAL);
    fail += expect(kc_bm25_load(&loaded, NULL) == KC_BM25_EINVAL);
    fail += expect(loaded == NULL);
    fail += expect(kc_bm25_load(&loaded, "bm25-file-not-found.bin")
        == KC_BM25_EIO);
    fail += expect(loaded == NULL);

    for (size_t i = 0; i < sizeof(changes) / sizeof(changes[0]); i++) {
        kc_bm25_t *bad = (kc_bm25_t *)1;

        if (kc_bm25_save(index, path) != KC_BM25_OK ||
            bm25_patch_byte(path, changes[i].offset, changes[i].value) != 0)
        {
            fail++;
            continue;
        }
        fail += expect(kc_bm25_load(&bad, path) == KC_BM25_EFORMAT);
        fail += expect(bad == NULL);
        if (bad != NULL && bad != (kc_bm25_t *)1) kc_bm25_close(bad);
    }
    {
        FILE *file = fopen(path, "wb");

        if (file != NULL) {
            fail += expect(fwrite("KCBM25", 1, 6, file) == 6);
            fail += expect(fclose(file) == 0);
            fail += expect(kc_bm25_load(&loaded, path) == KC_BM25_EFORMAT);
            fail += expect(loaded == NULL);
        } else fail++;
    }
    remove(path);
    kc_bm25_close(index);
    case_result(fail, "bm25_file_contract",
        "rejects missing files, corrupt headers, invalid lengths and truncation");
    return fail != 0;
}

/**
 * Verify metadata accessors, status messages and build timestamp.
 * @return Zero on success, otherwise one.
 */
static int case_bm25_metadata(void) {
    kc_bm25_t *index = NULL;
    int fail = 0;

    fail += expect(kc_bm25_count(NULL) == 0);
    fail += expect(kc_bm25_fold_accents(NULL) == 0);
    fail += expect(kc_bm25_strerror(KC_BM25_OK) != NULL);
    fail += expect(kc_bm25_strerror(KC_BM25_EINVAL) != NULL);
    fail += expect(kc_bm25_strerror(KC_BM25_ENOMEM) != NULL);
    fail += expect(kc_bm25_strerror(KC_BM25_ESTATE) != NULL);
    fail += expect(kc_bm25_strerror(KC_BM25_EIO) != NULL);
    fail += expect(kc_bm25_strerror(KC_BM25_EFORMAT) != NULL);
    fail += expect(kc_bm25_strerror(KC_BM25_EUTF8) != NULL);
    fail += expect(kc_bm25_strerror(-999) != NULL);
    fail += expect(kc_bm25_open(&index, NULL) == KC_BM25_OK);
    if (index != NULL) {
        fail += expect(kc_bm25_count(index) == 0);
        fail += expect(kc_bm25_fold_accents(index) == 0);
        kc_bm25_close(index);
    }
    {
        uint64_t version = kc_bm25_version();
        fail += expect(kc_bm25_version() == version);
    }
    kc_bm25_free(NULL);
    case_result(fail, "bm25_metadata",
        "exposes stable metadata, status messages and null-safe cleanup");
    return fail != 0;
}

/**
 * Verify memory export/import, identical ranking, and corrupt payload handling.
 * @return Zero when the memory persistence contracts hold, otherwise one.
 */
static int case_bm25_memory(void) {
    kc_bm25_t *source = reference_index();
    kc_bm25_t *imported = NULL;
    kc_bm25_result_t *before = NULL;
    kc_bm25_result_t *after = NULL;
    void *bytes = NULL, *another = NULL;
    size_t size = 0, another_size = 0;
    size_t count_before = 0, count_after = 0;
    int fail = 0;

    if (source == NULL) {
        case_result(1, "bm25_memory", "imports and exports portable index buffers");
        return 1;
    }
    fail += expect(kc_bm25_export(source, NULL, &size) == KC_BM25_EINVAL);
    fail += expect(kc_bm25_export(source, &bytes, NULL) == KC_BM25_EINVAL);
    fail += expect(kc_bm25_export(source, &bytes, &size) == KC_BM25_OK);
    fail += expect(bytes != NULL && size >= 44);
    if (bytes != NULL && size >= 44) {
        fail += expect(kc_bm25_import(&imported, bytes, size) == KC_BM25_OK);
        fail += expect(imported != NULL);
        if (imported != NULL) {
            fail += expect(kc_bm25_count(imported) == kc_bm25_count(source));
            fail += expect(kc_bm25_add(imported, "later", "word") == KC_BM25_ESTATE);
            fail += expect(kc_bm25_build(imported) == KC_BM25_ESTATE);
            fail += expect(kc_bm25_search(source, "brown", NULL,
                &before, &count_before) == KC_BM25_OK);
            fail += expect(kc_bm25_search(imported, "brown", NULL,
                &after, &count_after) == KC_BM25_OK);
            fail += expect(count_before == 2 && count_after == 2);
            if (before != NULL && after != NULL &&
                count_before == count_after) {
                for (size_t i = 0; i < count_before; i++) {
                    fail += expect(before[i].ordinal == after[i].ordinal);
                    fail += expect(strcmp(before[i].id, after[i].id) == 0);
                    fail += expect(strcmp(before[i].content,
                        after[i].content) == 0);
                    fail += expect_score(before[i].score, after[i].score);
                }
            }
            fail += expect(kc_bm25_export(imported,
                &another, &another_size) == KC_BM25_OK);
            fail += expect(another_size == size);
            if (another != NULL && another_size == size)
                fail += expect(memcmp(bytes, another, size) == 0);
        }
        {
            kc_bm25_t *rejected = NULL;

            fail += expect(kc_bm25_import(&rejected, bytes, size - 1)
                == KC_BM25_EFORMAT);
            fail += expect(rejected == NULL);
            fail += expect(kc_bm25_import(&rejected, bytes, 0)
                == KC_BM25_EFORMAT);
            fail += expect(rejected == NULL);
            ((unsigned char *)bytes)[0] ^= 1;
            fail += expect(kc_bm25_import(&rejected, bytes, size)
                == KC_BM25_EFORMAT);
            fail += expect(rejected == NULL);
            ((unsigned char *)bytes)[0] ^= 1;
        }
    }
    kc_bm25_free(before);
    kc_bm25_free(after);
    kc_bm25_free(another);
    kc_bm25_free(bytes);
    kc_bm25_close(imported);
    kc_bm25_close(source);
    case_result(fail, "bm25_memory",
        "round-trips stable buffers and rejects truncated or corrupt data");
    return fail != 0;
}

#ifndef __EMSCRIPTEN__
/**
 * Execute the native CLI with its output redirected to a fixture.
 * @param arguments Command-line arguments.
 * @param output File receiving stdout and stderr.
 * @return Zero on a successful command, otherwise one.
 */
static int bm25_cli_execute(const char *arguments, const char *output) {
    char command[8192];
    int length;

#ifdef _WIN32
    length = snprintf(command, sizeof(command),
        "\"\"%s\" %s > %s 2>&1\"", KC_CLI_PATH, arguments, output);
#else
    length = snprintf(command, sizeof(command),
        "\"%s\" %s > %s 2>&1", KC_CLI_PATH, arguments, output);
#endif
    if (length < 0 || (size_t)length >= sizeof(command)) return 1;
    return system(command) == 0 ? 0 : 1;
}

/**
 * Verify public CLI help, input formats, ranked output and rejected options.
 * @return Zero on successful CLI contract validation, otherwise one.
 */
static int case_bm25_cli(void) {
    const char *fixture = "bm25-fixture.tsv";
    const char *index = "bm25-cli.bin";
    const char *output = "bm25-output.tsv";
    char line[8192];
    char help[8192] = {0};
    const char *help_body =
        "\nCommands:\n"
        "  build <index>           Build an index from TSV associations\n"
        "  search <index> <query>  Search indexed associations\n"
        "\nOptions:\n"
        "  -i, --input <file>      Read associations from file (default: stdin)\n"
        "      --fold-accents      Remove accents during indexing\n"
        "  -l, --limit <N>         Maximum results (default: 10)\n"
        "      --k1 <N>            Term frequency saturation (default: 1.2)\n"
        "      --b <N>             Document length normalization (default: 0.75)\n"
        "  -h, --help              Show this help\n"
        "  -v, --version           Show version\n";
    char id[128] = {0};
    char content[256] = {0};
    size_t ordinal = 99;
    double score = 0;
    FILE *file;
    int fail = 0;

    file = fopen(fixture, "wb");
    if (file == NULL) return 1;
    fail += expect(fputs("x\tPeer authentication\n", file) >= 0);
    fail += expect(fputs("x\tNetwork transport\n", file) >= 0);
    fail += expect(fputs("cafe\tCafé con leche\n", file) >= 0);
    fail += expect(fclose(file) == 0);

    fail += expect(bm25_cli_execute("--help", output) == 0);
    file = fopen(output, "rb");
    if (file != NULL) {
        size_t length = fread(help, 1, sizeof(help) - 1, file);
        char *newline;

        help[length] = '\0';
        newline = strchr(help, '\n');
        fail += expect(ferror(file) == 0);
        fail += expect(strncmp(help, "Usage: ", 7) == 0);
        fail += expect(strstr(help, " <command> <index> [options]\n") != NULL);
        fail += expect(newline != NULL &&
            strcmp(newline + 1, help_body) == 0);
        fail += expect(fclose(file) == 0);
    } else fail++;
    fail += expect(bm25_cli_execute("-h", output) == 0);
    file = fopen(output, "rb");
    if (file != NULL) {
        size_t length = fread(line, 1, sizeof(line) - 1, file);

        line[length] = '\0';
        fail += expect(ferror(file) == 0);
        fail += expect(strcmp(line, help) == 0);
        fail += expect(fclose(file) == 0);
    } else fail++;
    fail += expect(bm25_cli_execute("--version", output) == 0);
    file = fopen(output, "rb");
    if (file != NULL) {
        char *end = NULL;

        if (fgets(line, sizeof(line), file) != NULL) {
            strtoull(line, &end, 10);
            fail += expect(end != line && (*end == '\n' ||
                *end == '\r' || *end == '\0'));
        } else fail++;
        fail += expect(fclose(file) == 0);
    } else fail++;
    fail += expect(bm25_cli_execute("-v", output) == 0);
    file = fopen(output, "rb");
    if (file != NULL) {
        char *end = NULL;

        if (fgets(line, sizeof(line), file) != NULL) {
            strtoull(line, &end, 10);
            fail += expect(end != line && (*end == '\n' ||
                *end == '\r' || *end == '\0'));
        } else fail++;
        fail += expect(fclose(file) == 0);
    } else fail++;

    fail += expect(bm25_cli_execute(
        "build bm25-cli.bin -i bm25-fixture.tsv", output) == 0);
    fail += expect(bm25_cli_execute(
        "search bm25-cli.bin authentication", output) == 0);
    file = fopen(output, "rb");
    if (file != NULL) {
        if (fgets(line, sizeof(line), file) != NULL) {
            int parsed = sscanf(line,
                "%zu\t%127[^\t]\t%lf\t%255[^\r\n]",
                &ordinal, id, &score, content);

            fail += expect(parsed == 4);
            if (parsed == 4) {
                fail += expect(ordinal == 0);
                fail += expect(strcmp(id, "x") == 0);
                fail += expect(strcmp(content, "Peer authentication") == 0);
                fail += expect(isfinite(score) && score > 0);
            }
            fail += expect(fgets(line, sizeof(line), file) == NULL);
        } else fail++;
        fclose(file);
    } else fail++;

    fail += expect(bm25_cli_execute(
        "search bm25-cli.bin authentication --limit 0", output) == 0);
    file = fopen(output, "rb");
    if (file != NULL) {
        fail += expect(fgetc(file) == EOF);
        fclose(file);
    } else fail++;

    fail += expect(bm25_cli_execute(
        "build bm25-cli.bin --fold-accents < bm25-fixture.tsv",
        output) == 0);
    fail += expect(bm25_cli_execute(
        "search bm25-cli.bin cafe --limit 1 --k1 1.2 --b 0.75",
        output) == 0);
    file = fopen(output, "rb");
    if (file != NULL) {
        if (fgets(line, sizeof(line), file) != NULL) {
            fail += expect(strncmp(line, "2\tcafe\t", 7) == 0);
        } else fail++;
        fclose(file);
    } else fail++;

    fail += expect(bm25_cli_execute(
        "search bm25-cli.bin cafe --k1 -0.1", output) != 0);
    fail += expect(bm25_cli_execute(
        "search bm25-cli.bin cafe --b 1.1", output) != 0);
    fail += expect(bm25_cli_execute(
        "search bm25-cli.bin cafe --limit invalid", output) != 0);
    fail += expect(bm25_cli_execute(
        "search bm25-cli.bin cafe --unknown", output) != 0);
    fail += expect(bm25_cli_execute(
        "search bm25-file-not-found.bin cafe", output) != 0);
    fail += expect(bm25_cli_execute(
        "build bm25-cli.bin -i bm25-file-not-found.tsv", output) != 0);

    file = fopen(fixture, "wb");
    if (file != NULL) {
        fail += expect(fputs("not-a-tab-delimited-line\n", file) >= 0);
        fail += expect(fclose(file) == 0);
        fail += expect(bm25_cli_execute(
            "build bm25-cli.bin -i bm25-fixture.tsv", output) != 0);
    } else fail++;

    remove(fixture);
    remove(index);
    remove(output);
    case_result(fail, "bm25_cli",
        "tests TSV and stdin input, output fields, options, and invalid commands");
    return fail != 0;
}
#endif

/**
 * Execute all platform-applicable library cases.
 * @return Number of failed cases.
 */
static int case_all(void) {
    int failures = 0;

#ifdef __EMSCRIPTEN__
    test_case_total = 10;
#else
    test_case_total = 11;
#endif
    test_case_current = 0;
    failures += run_case(case_bm25_scoring);
    failures += run_case(case_bm25_contract);
    failures += run_case(case_bm25_unicode);
    failures += run_case(case_bm25_persistence);
    failures += run_case(case_bm25_corpus);
    failures += run_case(case_bm25_options);
    failures += run_case(case_bm25_topk);
    failures += run_case(case_bm25_file_contract);
    failures += run_case(case_bm25_metadata);
    failures += run_case(case_bm25_memory);
#ifndef __EMSCRIPTEN__
    failures += run_case(case_bm25_cli);
#endif
    printf("%d passed, %d failed\n", test_case_total - failures,
        failures);
    return failures != 0;
}

/**
 * Dispatch a named contract test or execute the complete suite.
 * @param argc Argument count.
 * @param argv Argument values.
 * @return Zero on success, nonzero on failure.
 */
int main(int argc, char **argv) {
    case_fn callback = NULL;

    if (argc != 2) return 2;
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "bm25_scoring") == 0) callback = case_bm25_scoring;
    else if (strcmp(argv[1], "bm25_contract") == 0) callback = case_bm25_contract;
    else if (strcmp(argv[1], "bm25_unicode") == 0) callback = case_bm25_unicode;
    else if (strcmp(argv[1], "bm25_persistence") == 0)
        callback = case_bm25_persistence;
    else if (strcmp(argv[1], "bm25_corpus") == 0) callback = case_bm25_corpus;
    else if (strcmp(argv[1], "bm25_options") == 0) callback = case_bm25_options;
    else if (strcmp(argv[1], "bm25_topk") == 0) callback = case_bm25_topk;
    else if (strcmp(argv[1], "bm25_file_contract") == 0)
        callback = case_bm25_file_contract;
    else if (strcmp(argv[1], "bm25_metadata") == 0)
        callback = case_bm25_metadata;
    else if (strcmp(argv[1], "bm25_memory") == 0)
        callback = case_bm25_memory;
#ifndef __EMSCRIPTEN__
    else if (strcmp(argv[1], "bm25_cli") == 0) callback = case_bm25_cli;
#endif
    if (callback == NULL) return 2;
    test_case_total = 1;
    test_case_current = 1;
    return callback() != 0;
}
