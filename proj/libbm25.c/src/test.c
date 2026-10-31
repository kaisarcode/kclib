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
    return callback();
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
    if (count == 2) {
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
    if (count == 2) {
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
    if (count == 2) {
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
    if (count == 1) fail |= expect(results[0].ordinal == 1);
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
    if (count == 1) fail |= expect(strcmp(results[0].id, "cafe") == 0);
    kc_bm25_free(results);
    results = NULL;
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
    fail |= expect(count == 1);
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
        fail |= expect(kc_bm25_search(loaded, "brown", NULL,
            &results, &count) == KC_BM25_OK);
        fail |= expect(count == 2);
        if (count == 2) fail |= expect(results[0].ordinal == 1);
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
        if (count == 1) fail |= expect(results[0].ordinal == 613);
    }
    kc_bm25_free(results);
    kc_bm25_close(index);
    case_result(fail, "bm25_corpus",
        "limits candidate retrieval to matching inverted postings");
    return fail;
}

#ifndef __EMSCRIPTEN__
/**
 * Exercise the CLI help, version, TSV build, and persisted search commands.
 * @return Test failure count.
 */
static int case_bm25_cli(void) {
    const char *fixture = "bm25-fixture.tsv";
    const char *index = "bm25-cli.bin";
    const char *output = "bm25-output.tsv";
    char command[8192];
    char line[8192];
    FILE *file;
    int fail = 0;

    file = fopen(fixture, "wb");
    if (file == NULL) return 1;
    fail |= expect(fputs("x\tPeer authentication\n", file) >= 0);
    fail |= expect(fputs("x\tNetwork transport\n", file) >= 0);
    fclose(file);

    snprintf(command, sizeof(command), "\"%s\" --help > %s",
        KC_CLI_PATH, output);
    fail |= expect(system(command) == 0);
    snprintf(command, sizeof(command), "\"%s\" --version > %s",
        KC_CLI_PATH, output);
    fail |= expect(system(command) == 0);
    snprintf(command, sizeof(command),
        "\"%s\" build %s -i %s", KC_CLI_PATH, index, fixture);
    fail |= expect(system(command) == 0);
    snprintf(command, sizeof(command),
        "\"%s\" search %s authentication > %s",
        KC_CLI_PATH, index, output);
    fail |= expect(system(command) == 0);
    file = fopen(output, "rb");
    if (file != NULL) {
        fail |= expect(fgets(line, sizeof(line), file) != NULL);
        if (!fail) fail |= expect(strncmp(line,
            "0\tx\t", 4) == 0);
        fclose(file);
    } else fail = 1;

    remove(fixture);
    remove(index);
    remove(output);
    case_result(fail, "bm25_cli",
        "builds from TSV and searches from the standalone command");
    return fail;
}
#endif

/**
 * Execute all platform-applicable library cases.
 * @return Number of failed cases.
 */
static int case_all(void) {
    int failures = 0;

#ifdef __EMSCRIPTEN__
    test_case_total = 5;
#else
    test_case_total = 6;
#endif
    test_case_current = 0;
    failures += run_case(case_bm25_scoring);
    failures += run_case(case_bm25_contract);
    failures += run_case(case_bm25_unicode);
    failures += run_case(case_bm25_persistence);
    failures += run_case(case_bm25_corpus);
#ifndef __EMSCRIPTEN__
    failures += run_case(case_bm25_cli);
#endif
    printf("%d passed, %d failed\n", test_case_total - (failures != 0),
        failures != 0);
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
#ifndef __EMSCRIPTEN__
    else if (strcmp(argv[1], "bm25_cli") == 0) callback = case_bm25_cli;
#endif
    if (callback == NULL) return 2;
    test_case_total = 1;
    test_case_current = 1;
    return callback() != 0;
}
