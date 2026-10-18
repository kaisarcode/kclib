/**
 * test.c - liblng public API tests.
 * Summary: Contract tests for stateless language detection.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "liblng.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <float.h>
#include <stdint.h>

static int test_case_total = 0;
static int test_case_current = 0;

#ifndef __EMSCRIPTEN__
static int case_lng_cli(void);
#endif

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Test case name.
 * @param detail Test case detail.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

/**
 * Runs one test case with counter tracking.
 * @param rc Destination accumulator.
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
 * @param condition Condition to verify.
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
 * Verifies one string result.
 * @param name Check description.
 * @param expected Expected string.
 * @param actual Actual string.
 * @return 0 on success, 1 on failure.
 */
static int expect_string(const char *name, const char *expected, const char *actual) {
    if (actual == NULL || strcmp(expected, actual) != 0) {
        printf("[FAIL] %s: expected '%s', got '%s'\n", name, expected,
            actual != NULL ? actual : "NULL");
        return 1;
    }
    return 0;
}

/**
 * Detect with independently optional threshold and limit values.
 * @param text Input text.
 * @param threshold_set Whether threshold is explicitly supplied.
 * @param threshold Threshold value.
 * @param limit_set Whether limit is explicitly supplied.
 * @param limit Limit value.
 * @param out_results Result array destination.
 * @param out_count Result count destination.
 * @return Public detect status.
 */
static int detect_with_options(
    const char *text,
    int threshold_set,
    double threshold,
    int limit_set,
    size_t limit,
    kc_lng_result_t **out_results,
    size_t *out_count
) {
    kc_lng_options_t options = {0};

    options.threshold = threshold_set ? &threshold : NULL;
    options.limit = limit_set ? &limit : NULL;
    return kc_lng_detect(text, &options, out_results, out_count);
}

static const char *TEXT_EN =
    "hello world this is english text how are you doing today? "
    "this is a robust test for english language detection. "
    "documentation is vital for understanding systems. "
    "university technology science information research development "
    "program software computer system network data business management "
    "education government country people year time way service work life "
    "world school high school state city area group problem hand part child";

static const char *TEXT_ES =
    "que el la de en que lo los un una por para como al su sus con del por sobre entre mucho "
    "despues tambien siempre mundo hola buenos dias todos como estas? "
    "este proyecto compara texto corto el zorro marron salta sobre el perro. "
    "esperanza y libertad para todos los pueblos. la programacion es un arte que requiere paciencia. "
    "el lenguaje de programacion c es muy potente. espana mexico argentina colombia chile peru";

static const char *TEXT_JA =
    "konnichiwa nihon ha gijutsu to dentou ga kyouson suru subarashii kuni desu. "
    "byouin ishi chiryou kanja houritsu saiban shimin kenkyu kagaku daigaku kankyou keizai enerugi "
    "gakkou chiiki toshi umi yama rekishi ongaku hon kazoku shigoto. "
    "\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf\xe3\x80\x82 "
    "\xe6\x97\xa5\xe6\x9c\xac\xe3\x81\xaf\xe6\x8a\x80\xe8\xa1\x93\xe3\x81\xa8\xe4\xbc\x9d\xe7\xb5\xb1\xe3\x81\x8c\xe5\x85\xb1\xe5\xad\x98\xe3\x81\x99\xe3\x82\x8b\xe7\xb4\xa0\xe6\x99\xb4\xe3\x82\x89\xe3\x81\x97\xe3\x81\x84\xe5\x9b\xbd\xe3\x81\xa7\xe3\x81\x99\xe3\x80\x82 "
    "\xe7\x97\x85\xe9\x99\xa2 \xe5\x8c\xbb\xe5\xb8\xab \xe6\xb2\xbb\xe7\x99\x82 \xe6\x82\xa3\xe8\x80\x85 \xe6\xb3\x95\xe5\xbe\x8b \xe8\xa3\x81\xe5\x88\xa4 \xe5\xb8\x82\xe6\xb0\x91 \xe7\xa0\x94\xe7\xa9\xb6 \xe7\xa7\x91\xe5\xad\xa6 \xe5\xa4\xa7\xe5\xad\xa6 \xe7\x92\xb0\xe5\xa2\x83 \xe7\xb5\x8c\xe6\xb8\x88 \xe3\x82\xa8\xe3\x83\x8d\xe3\x83\xab\xe3\x82\xae\xe3\x83\xbc \xe5\xad\xa6\xe6\xa0\xa1 \xe5\x9c\xb0\xe5\x9f\x9f \xe9\x83\xbd\xe5\xb8\x82 \xe6\xb5\xb7 \xe5\xb1\xb1 \xe6\xad\xb4\xe5\x8f\xb2 \xe9\x9f\xb3\xe6\xa5\xbd \xe6\x9c\xac \xe5\xae\xb6\xe6\x97\x8f \xe4\xbb\x95\xe4\xba\x8b";

/**
 * Tests version determinism.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_lng_version(void) {
    const char *name = "kc_lng_version";
    const char *detail = "version returns build timestamp";
    int fail = 0;
    uint64_t v1 = kc_lng_version();
    uint64_t v2 = kc_lng_version();
    fail += expect_true("version deterministic", v1 == v2);
    (void)v1;
    (void)v2;
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests detection for representative languages.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_lng_detect(void) {
    const char *name = "kc_lng_detect";
    const char *detail = "detects languages and applies independent defaults";
    kc_lng_result_t *res = NULL;
    kc_lng_options_t options = {0};
    size_t count = 0;
    int fail = 0;
    int rc;

    rc = kc_lng_detect(TEXT_EN, NULL, &res, &count);
    fail += expect_int("NULL options use defaults", KC_LNG_OK, rc);
    fail += expect_true("default en result", count == 1 && res != NULL);
    if (count == 1 && res != NULL) {
        fail += expect_string("default en top code", "en", res[0].code);
    }
    kc_lng_free(res);

    res = NULL;
    count = 0;
    rc = kc_lng_detect(TEXT_EN, &options, &res, &count);
    fail += expect_int("empty options use defaults", KC_LNG_OK, rc);
    fail += expect_true("empty options limit defaults to one",
        count == 1 && res != NULL);
    if (count == 1 && res != NULL) {
        fail += expect_string("empty options en top code", "en", res[0].code);
    }
    kc_lng_free(res);

    res = NULL;
    count = 0;
    rc = kc_lng_detect(TEXT_ES, NULL, &res, &count);
    fail += expect_int("es detect rc", KC_LNG_OK, rc);
    fail += expect_true("es count one", count == 1 && res != NULL);
    if (count == 1 && res != NULL) {
        fail += expect_string("es top code", "es", res[0].code);
    }
    kc_lng_free(res);

    res = NULL;
    count = 0;
    rc = kc_lng_detect(TEXT_JA, NULL, &res, &count);
    fail += expect_int("ja detect rc", KC_LNG_OK, rc);
    fail += expect_true("ja count one", count == 1 && res != NULL);
    if (count == 1 && res != NULL) {
        fail += expect_string("ja top code", "ja", res[0].code);
    }
    kc_lng_free(res);

    res = (kc_lng_result_t *)0x1;
    count = 999;
    rc = kc_lng_detect("", NULL, &res, &count);
    fail += expect_int("empty rc", KC_LNG_OK, rc);
    fail += expect_true("empty count 0", count == 0);
    fail += expect_true("empty results NULL", res == NULL);
    kc_lng_free(res);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests ranking order and limits.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_lng_detect_ranking(void) {
    const char *name = "kc_lng_detect_ranking";
    const char *detail = "ranking and independent threshold/limit options";
    kc_lng_result_t *res = NULL;
    kc_lng_result_t *low_res = NULL;
    kc_lng_result_t *high_res = NULL;
    kc_lng_result_t *r1 = NULL;
    kc_lng_result_t *r3 = NULL;
    size_t count = 0;
    size_t low_count = 0;
    size_t high_count = 0;
    size_t c1 = 0;
    size_t c3 = 0;
    size_t i;
    int fail = 0;
    int rc;

    rc = detect_with_options(TEXT_EN, 1, 0.0, 1, 5, &res, &count);
    fail += expect_int("ranking rc", KC_LNG_OK, rc);
    fail += expect_true("ranking count >1", count > 1);
    fail += expect_true("ranking count <=5", count <= 5);
    for (i = 1; i < count; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "descending %zu", i);
        fail += expect_true(buf, res[i - 1].score >= res[i].score);
    }
    kc_lng_free(res);

    rc = detect_with_options(TEXT_EN, 0, 0.0, 1, 10,
        &low_res, &low_count);
    fail += expect_int("omitted threshold rc", KC_LNG_OK, rc);
    rc = detect_with_options(TEXT_EN, 1, 0.99, 1, 10,
        &high_res, &high_count);
    fail += expect_int("explicit high threshold rc", KC_LNG_OK, rc);
    fail += expect_true("high filters more", high_count <= low_count);
    kc_lng_free(low_res);
    kc_lng_free(high_res);

    rc = detect_with_options(TEXT_EN, 1, 0.0, 0, 0, &r1, &c1);
    fail += expect_int("omitted limit rc", KC_LNG_OK, rc);
    fail += expect_true("omitted limit defaults to one", c1 == 1);

    rc = detect_with_options(TEXT_EN, 1, 0.0, 1, 3, &r3, &c3);
    fail += expect_int("explicit limit3 rc", KC_LNG_OK, rc);
    fail += expect_true("limit3 count <=3", c3 <= 3);
    fail += expect_true("limit3 >= default limit", c3 >= c1);
    kc_lng_free(r1);
    kc_lng_free(r3);

    res = NULL;
    count = 0;
    rc = detect_with_options(TEXT_EN, 1, 0.0, 1, 100, &res, &count);
    fail += expect_int("limit100 rc", KC_LNG_OK, rc);
    fail += expect_true("limit100 count <=32", count <= 32);
    fail += expect_true("limit100 count <=26 langs", count <= 26);
    kc_lng_free(res);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Verifies failure resets outputs.
 * @param label Check label.
 * @param rc Return code to verify.
 * @param out_results Results pointer to check.
 * @param out_count Count pointer to check.
 * @return 0 on success, 1 on failure.
 */
static int check_fail_resets(const char *label, int rc, kc_lng_result_t **out_results, size_t *out_count) {
    int f = 0;
    f += expect_int(label, KC_LNG_ERROR, rc);
    f += expect_true("reset results NULL", *out_results == NULL);
    f += expect_true("reset count 0", *out_count == 0);
    return f;
}

/**
 * Tests contract error handling.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_lng_detect_contract(void) {
    const char *name = "kc_lng_detect_contract";
    const char *detail = "validates explicit options and resets outputs";
    kc_lng_result_t *res = NULL;
    size_t count = 0;
    int fail = 0;
    int rc;

    res = (kc_lng_result_t *)0xDEADBEEF;
    count = 999;
    rc = kc_lng_detect(NULL, NULL, &res, &count);
    fail += check_fail_resets("NULL text", rc, &res, &count);

    count = 999;
    rc = kc_lng_detect(TEXT_EN, NULL, NULL, &count);
    fail += expect_int("NULL out_results", KC_LNG_ERROR, rc);
    fail += expect_true("NULL out_results count 0", count == 0);

    res = (kc_lng_result_t *)0xDEADBEEF;
    rc = kc_lng_detect(TEXT_EN, NULL, &res, NULL);
    fail += expect_int("NULL out_count", KC_LNG_ERROR, rc);
    fail += expect_true("NULL out_count results NULL", res == NULL);

    res = (kc_lng_result_t *)0xDEADBEEF;
    count = 999;
    rc = detect_with_options(TEXT_EN, 0, 0.0, 1, 0, &res, &count);
    fail += check_fail_resets("explicit zero limit", rc, &res, &count);

    res = (kc_lng_result_t *)0xDEADBEEF;
    count = 999;
    rc = detect_with_options(TEXT_EN, 1, -0.1, 0, 0, &res, &count);
    fail += check_fail_resets("threshold <0", rc, &res, &count);

    res = (kc_lng_result_t *)0xDEADBEEF;
    count = 999;
    rc = detect_with_options(TEXT_EN, 1, 1.5, 0, 0, &res, &count);
    fail += check_fail_resets("threshold >1", rc, &res, &count);

    res = (kc_lng_result_t *)0xDEADBEEF;
    count = 999;
    rc = detect_with_options(TEXT_EN, 1, NAN, 0, 0, &res, &count);
    fail += check_fail_resets("NaN threshold", rc, &res, &count);

    res = (kc_lng_result_t *)0xDEADBEEF;
    count = 999;
    rc = detect_with_options(TEXT_EN, 1, INFINITY, 0, 0, &res, &count);
    fail += check_fail_resets("INFINITY threshold", rc, &res, &count);

    res = (kc_lng_result_t *)0xDEADBEEF;
    count = 999;
    rc = detect_with_options(TEXT_EN, 1, -INFINITY, 0, 0, &res, &count);
    fail += check_fail_resets("-INFINITY threshold", rc, &res, &count);

    res = (kc_lng_result_t *)0xDEADBEEF;
    count = 999;
    rc = detect_with_options("xyz", 1, 0.99, 1, 5, &res, &count);
    fail += expect_int("xyz filtered rc", KC_LNG_OK, rc);
    fail += expect_true("xyz filtered count 0", count == 0);
    fail += expect_true("xyz filtered results NULL", res == NULL);
    kc_lng_free(res);

    res = (kc_lng_result_t *)0xDEADBEEF;
    count = 999;
    rc = detect_with_options("", 1, 0.0, 1, 32, &res, &count);
    fail += expect_int("empty explicit zero threshold rc", KC_LNG_OK, rc);
    fail += expect_true("empty explicit zero threshold count 0", count == 0);
    fail += expect_true("empty explicit zero threshold results NULL", res == NULL);
    kc_lng_free(res);

    res = NULL;
    count = 0;
    rc = detect_with_options(TEXT_EN, 0, 0.0, 1, 2, &res, &count);
    fail += expect_int("valid explicit limit call", KC_LNG_OK, rc);
    if (count > 0) {
        fail += expect_true("allocated results non-NULL", res != NULL);
    } else {
        fail += expect_true("no results still NULL", res == NULL);
    }
    kc_lng_free(res);

    kc_lng_free(NULL);
    fail += expect_true("free NULL safe", 1);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests deterministic ranking.
 * @return 0 on success, 1 on failure.
 */
static int case_kc_lng_free(void) {
    const char *name = "kc_lng_free";
    const char *detail = "deterministic ranking and static code storage";
    kc_lng_result_t *a = NULL;
    kc_lng_result_t *b = NULL;
    size_t ca = 0;
    size_t cb = 0;
    size_t i;
    int fail = 0;
    int rc;

    rc = detect_with_options(TEXT_EN, 1, 0.0, 1, 5, &a, &ca);
    fail += expect_int("det first rc", KC_LNG_OK, rc);
    rc = detect_with_options(TEXT_EN, 1, 0.0, 1, 5, &b, &cb);
    fail += expect_int("det second rc", KC_LNG_OK, rc);
    fail += expect_true("det same count", ca == cb);
    if (ca == cb && a != NULL && b != NULL) {
        for (i = 0; i < ca; i++) {
            char buf[64];
            size_t j;

            snprintf(buf, sizeof(buf), "det code %zu", i);
            fail += expect_string(buf, a[i].code, b[i].code);
            snprintf(buf, sizeof(buf), "det score %zu", i);
            fail += expect_true(buf, fabs(a[i].score - b[i].score) < 1e-9);
            for (j = 0; j < cb; j++) {
                if (strcmp(a[i].code, b[j].code) == 0) {
                    fail += expect_true("static pointer same",
                        a[i].code == b[j].code);
                    break;
                }
            }
        }
    }
    kc_lng_free(a);
    kc_lng_free(b);

    a = NULL;
    b = NULL;
    ca = 0;
    cb = 0;
    rc = detect_with_options(TEXT_ES, 0, 0.0, 1, 3, &a, &ca);
    fail += expect_int("det es first rc", KC_LNG_OK, rc);
    rc = detect_with_options(TEXT_ES, 0, 0.0, 1, 3, &b, &cb);
    fail += expect_int("det es second rc", KC_LNG_OK, rc);
    fail += expect_true("det es same count", ca == cb);
    if (ca == cb && a != NULL && b != NULL) {
        for (i = 0; i < ca; i++) {
            fail += expect_string("det es code", a[i].code, b[i].code);
            fail += expect_true("det es score",
                fabs(a[i].score - b[i].score) < 1e-9);
        }
    }
    kc_lng_free(a);
    kc_lng_free(b);

    kc_lng_free(NULL);
    fail += expect_true("free NULL safe", 1);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases.
 * @return 0 on success, 1 on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 6;
    test_case_current = 0;
    run_case(&rc, case_kc_lng_version);
    run_case(&rc, case_kc_lng_detect);
    run_case(&rc, case_kc_lng_detect_ranking);
    run_case(&rc, case_kc_lng_detect_contract);
    run_case(&rc, case_kc_lng_free);
#ifndef __EMSCRIPTEN__
    run_case(&rc, case_lng_cli);
#endif
    printf("%d passed, %d failed\n\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one lng public API test case.
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
    if (strcmp(argv[1], "kc_lng_version") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_version(); }
    if (strcmp(argv[1], "kc_lng_detect") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_detect(); }
    if (strcmp(argv[1], "kc_lng_detect_ranking") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_detect_ranking(); }
    if (strcmp(argv[1], "kc_lng_detect_contract") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_detect_contract(); }
    if (strcmp(argv[1], "kc_lng_free") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_free(); }
    if (strcmp(argv[1], "version") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_version(); }
    if (strcmp(argv[1], "detection") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_detect(); }
    if (strcmp(argv[1], "ranking") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_detect_ranking(); }
    if (strcmp(argv[1], "contract") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_detect_contract(); }
    if (strcmp(argv[1], "determinism") == 0) { test_case_total = 1; test_case_current = 1; return case_kc_lng_free(); }
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "lng_cli") == 0) return case_lng_cli();
#endif
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}

#ifndef __EMSCRIPTEN__
#ifndef KC_CLI_PATH
#define KC_CLI_PATH "lng"
#endif

static int lng_cli_total;
static int lng_cli_failed;

/**
 * Writes one CLI text fixture.
 * @param path Destination path.
 * @param text Fixture text.
 * @return Nonzero on success.
 */
static int lng_cli_write(const char *path, const char *text) {
    FILE *file;
    size_t size;

    file = fopen(path, "wb");
    if (!file) return 0;
    size = strlen(text);
    if (fwrite(text, 1, size, file) != size) {
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}

/**
 * Runs the CLI, optionally supplying stdin, and captures stdout.
 * @param args Command arguments.
 * @param stdin_text Optional stdin text.
 * @param output Destination output buffer.
 * @param output_size Destination buffer size.
 * @return Nonzero on success.
 */
static int lng_cli_capture(const char *args, const char *stdin_text,
    char *output, size_t output_size) {
    const char *input_path = "lng-cli-stdin.txt";
    const char *output_path = "lng-cli-output.txt";
    char command[4096];
    FILE *file;
    size_t size;

    if (stdin_text && !lng_cli_write(input_path, stdin_text)) return 0;
#ifdef _WIN32
    snprintf(command, sizeof(command), "\"\"%s\" %s %s> %s 2>NUL\"",
        KC_CLI_PATH, args ? args : "", stdin_text ? "< lng-cli-stdin.txt " : "",
        output_path);
#else
    snprintf(command, sizeof(command), "\"%s\" %s %s> %s 2>/dev/null",
        KC_CLI_PATH, args ? args : "", stdin_text ? "< lng-cli-stdin.txt " : "",
        output_path);
#endif
    if (system(command) != 0) {
        if (stdin_text) remove(input_path);
        remove(output_path);
        return 0;
    }
    if (stdin_text) remove(input_path);
    file = fopen(output_path, "rb");
    if (!file) return 0;
    size = fread(output, 1, output_size - 1U, file);
    fclose(file);
    remove(output_path);
    output[size] = '\0';
    return 1;
}

/**
 * Runs the CLI without retaining its output.
 * @param args Command arguments.
 * @return Shell exit status.
 */
static int lng_cli_run(const char *args) {
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
 * Prints one CLI contract result.
 * @param name Test name.
 * @param detail Tested behavior.
 * @param ok Nonzero when the test passed.
 * @return None.
 */
static void lng_cli_result(const char *name, const char *detail, int ok) {
    lng_cli_total++;
    if (!ok) lng_cli_failed++;
    printf("[%d/12] [%s] %s: %s\n", lng_cli_total,
        ok ? "PASS" : "FAIL", name, detail);
}

/**
 * Runs all lng CLI contract tests.
 * @return Zero when every test passes.
 */
static int case_lng_cli(void) {
    const char *file_path = "lng-cli-file.txt";
    char expected[256];
    char actual[256];
    int fixture_ok;
    char output[256];

    lng_cli_total = 0;
    lng_cli_failed = 0;
    lng_cli_result("help", "--help succeeds", lng_cli_run("--help") == 0);
    lng_cli_result("version", "--version succeeds", lng_cli_run("--version") == 0);
    lng_cli_result("detect", "text detection succeeds",
        lng_cli_run("\"Hello world\"") == 0);
    lng_cli_result("tsv", "output is headerless TSV",
        lng_cli_capture("\"Hello world\"", NULL, output, sizeof(output)) &&
        strchr(output, '\t') != NULL && strchr(output, ',') == NULL &&
        strncmp(output, "code\tscore", 10) != 0);
    fixture_ok = lng_cli_write(file_path, "Bonjour le monde");
    lng_cli_result("input file", "-i detects text from file",
        fixture_ok && lng_cli_run("-i lng-cli-file.txt") == 0);
    lng_cli_result("stdin priority", "stdin takes priority over -i",
        fixture_ok && lng_cli_capture(NULL, "Hello world", expected,
        sizeof(expected)) && lng_cli_capture("-i lng-cli-file.txt", "Hello world",
        actual, sizeof(actual)) && strcmp(expected, actual) == 0);
    lng_cli_result("argument priority", "argument takes priority over stdin and -i",
        fixture_ok && lng_cli_capture("\"Hello world\"", NULL, expected,
        sizeof(expected)) && lng_cli_capture("\"Hello world\" -i lng-cli-file.txt",
        "Bonjour le monde", actual, sizeof(actual)) && strcmp(expected, actual) == 0);
    remove(file_path);
    lng_cli_result("threshold", "valid threshold succeeds",
        lng_cli_run("\"Hello world\" --threshold 0.1") == 0);
    lng_cli_result("limit", "valid limit succeeds",
        lng_cli_run("\"Hello world\" --limit 3") == 0);
    lng_cli_result("bad threshold", "invalid threshold fails",
        lng_cli_run("\"Hello world\" --threshold 2") != 0);
    lng_cli_result("bad limit", "zero limit fails",
        lng_cli_run("\"Hello world\" --limit 0") != 0);
    lng_cli_result("unknown", "unknown option fails",
        lng_cli_run("\"Hello world\" --unknown") != 0);
    printf("%d passed, %d failed\n\n", 12 - lng_cli_failed, lng_cli_failed);
    return lng_cli_failed ? 1 : 0;
}
#endif
