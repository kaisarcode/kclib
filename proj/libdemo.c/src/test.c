/**
 * test.c - libdemo public API contract tests.
 * Summary: Validates the minimal reusable API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libdemo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_case_total = 0;
static int test_case_current = 0;

static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

static void run_case(int *rc, int (*fn)(void)) {
    test_case_current++;
    *rc += fn();
}

static int expect_true(const char *name, int condition) {
    if (!condition) {
        printf("[FAIL] %s\n", name);
        return 1;
    }
    return 0;
}

static int case_kc_demo_greet(void) {
    const char *name = "kc_demo_greet";
    const char *detail = "returns an owned greeting for the supplied name";
    char *greeting;
    int fail = 0;

    fail += expect_true("greet rejects NULL", kc_demo_greet(NULL) == NULL);

    greeting = kc_demo_greet("John");
    fail += expect_true("greet allocates output", greeting != NULL);
    if (greeting != NULL) {
        fail += expect_true("greet returns expected text",
            strcmp(greeting, "Hello John!") == 0);
    }
    kc_demo_free(greeting);
    kc_demo_free(NULL);

    greeting = kc_demo_greet("");
    fail += expect_true("greet accepts empty name", greeting != NULL);
    if (greeting != NULL) {
        fail += expect_true("empty name keeps greeting format",
            strcmp(greeting, "Hello !") == 0);
    }
    kc_demo_free(greeting);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_kc_demo_version(void) {
    const char *name = "kc_demo_version";
    const char *detail = "returns a nonzero generated build version";
    int fail = expect_true("version is nonzero", kc_demo_version() != 0U);

    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_all(void) {
    int rc = 0;

    test_case_total = 2;
    test_case_current = 0;
    run_case(&rc, case_kc_demo_greet);
    run_case(&rc, case_kc_demo_version);

    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "kc_demo_greet") == 0) return case_kc_demo_greet();
    if (strcmp(argv[1], "kc_demo_version") == 0) return case_kc_demo_version();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
