/**
 * test.c - librdvz public API contract tests.
 * Summary: Validates rendezvous publication and seeker lifecycle.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "librdvz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#endif

static int test_case_total = 0;
static int test_case_current = 0;

#ifndef __EMSCRIPTEN__
static int case_rdvz_cli(void);
#endif

/**
 * Report a test case result.
 * @param fail Non-zero if the case failed.
 * @param name Test case name.
 * @param detail Additional detail string.
 * @return void
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
 * Assert a condition is true, reporting failure if not.
 * @param name Test case name.
 * @param condition Condition to evaluate.
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
 * Sleep for the specified number of milliseconds.
 * @param ms Milliseconds to sleep.
 * @return void
 */
static void sleep_ms(unsigned int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec req;
    req.tv_sec = (time_t)(ms / 1000U);
    req.tv_nsec = (long)(ms % 1000U) * 1000000L;
    nanosleep(&req, NULL);
#endif
}

/**
 * Compare binary data to a text string.
 * @param data Binary data pointer.
 * @param size Binary data size.
 * @param text Text string to compare.
 * @return 1 if equal, 0 otherwise.
 */
static int bytes_equal(const void *data, size_t size, const char *text) {
    size_t text_size = strlen(text);
    return size == text_size && (size == 0U || memcmp(data, text, size) == 0);
}

/**
 * Test case: seek before publish.
 * Seekers exist independently and survive later publication.
 * @return Number of failed assertions.
 */
static int case_seek_before_publish(void) {
    const char *name = "seek_before_publish";
    const char *detail = "seekers exist independently and survive later publication";
    kc_rdvz_t *rdvz = NULL;
    const kc_rdvz_seeker_t *s1 = NULL;
    const kc_rdvz_seeker_t *s2 = NULL;
    const kc_rdvz_point_t *point = NULL;
    int fail = 0;

    fail += expect_true("open succeeds", kc_rdvz_open(&rdvz, NULL) == KC_RDVZ_OK);
    fail += expect_true("first seek succeeds",
        kc_rdvz_seek(rdvz, "room", "one", 3U, &s1) == KC_RDVZ_OK);
    fail += expect_true("second seek succeeds",
        kc_rdvz_seek(rdvz, "room", "two", 3U, &s2) == KC_RDVZ_OK);
    point = kc_rdvz_get(rdvz, "room");
    fail += expect_true("point exists", point != NULL);
    if (point) {
        fail += expect_true("publication is absent", point->publication == NULL);
        fail += expect_true("two seekers are retained", point->seeker_count == 2U);
        fail += expect_true("head is first seeker", point->head == s1);
        fail += expect_true("tail is second seeker", point->tail == s2);
    }
    fail += expect_true("publish succeeds",
        kc_rdvz_publish(rdvz, "room", "pub", 3U, &point) == KC_RDVZ_OK);
    if (point) {
        fail += expect_true("publication appears", point->publication != NULL);
        fail += expect_true("seekers survive publish", point->seeker_count == 2U);
        fail += expect_true("published bytes are retained",
            point->publication && bytes_equal(point->publication->data,
                point->publication->data_size, "pub"));
    }
    kc_rdvz_close(rdvz);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test case: unpublish independent.
 * Unpublish removes only publication and republish reuses waiting seekers.
 * @return Number of failed assertions.
 */
static int case_unpublish_independent(void) {
    const char *name = "unpublish_independent";
    const char *detail = "unpublish removes only publication and republish reuses waiting seekers";
    kc_rdvz_t *rdvz = NULL;
    const kc_rdvz_seeker_t *s1 = NULL;
    const kc_rdvz_seeker_t *s2 = NULL;
    const kc_rdvz_point_t *point = NULL;
    int fail = 0;

    fail += expect_true("open succeeds", kc_rdvz_open(&rdvz, NULL) == KC_RDVZ_OK);
    fail += expect_true("publish succeeds",
        kc_rdvz_publish(rdvz, "room", "first", 5U, NULL) == KC_RDVZ_OK);
    fail += expect_true("seek succeeds",
        kc_rdvz_seek(rdvz, "room", "a", 1U, &s1) == KC_RDVZ_OK);
    fail += expect_true("unpublish succeeds",
        kc_rdvz_unpublish(rdvz, "room") == KC_RDVZ_OK);
    point = kc_rdvz_get(rdvz, "room");
    fail += expect_true("point survives unpublish", point != NULL);
    if (point) {
        fail += expect_true("publication removed", point->publication == NULL);
        fail += expect_true("first seeker survives", point->head == s1);
    }
    fail += expect_true("new seeker can wait while unpublished",
        kc_rdvz_seek(rdvz, "room", "b", 1U, &s2) == KC_RDVZ_OK);
    fail += expect_true("republish succeeds",
        kc_rdvz_publish(rdvz, "room", "second", 6U, &point) == KC_RDVZ_OK);
    if (point) {
        fail += expect_true("both seekers survive republish",
            point->seeker_count == 2U && point->head == s1 && point->tail == s2);
        fail += expect_true("new publication data is visible",
            point->publication && bytes_equal(point->publication->data,
                point->publication->data_size, "second"));
    }
    kc_rdvz_close(rdvz);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test case: remove and order.
 * Middle removal is isolated and insertion order remains linked.
 * @return Number of failed assertions.
 */
static int case_remove_and_order(void) {
    const char *name = "remove_and_order";
    const char *detail = "middle removal is isolated and insertion order remains linked";
    kc_rdvz_t *rdvz = NULL;
    const kc_rdvz_seeker_t *a = NULL;
    const kc_rdvz_seeker_t *b = NULL;
    const kc_rdvz_seeker_t *c = NULL;
    const kc_rdvz_point_t *point;
    int fail = 0;

    fail += expect_true("open succeeds", kc_rdvz_open(&rdvz, NULL) == KC_RDVZ_OK);
    fail += expect_true("seek a", kc_rdvz_seek(rdvz, "x", "a", 1U, &a) == KC_RDVZ_OK);
    fail += expect_true("seek b", kc_rdvz_seek(rdvz, "x", "b", 1U, &b) == KC_RDVZ_OK);
    fail += expect_true("seek c", kc_rdvz_seek(rdvz, "x", "c", 1U, &c) == KC_RDVZ_OK);
    fail += expect_true("remove middle", kc_rdvz_remove(rdvz, b) == KC_RDVZ_OK);
    point = kc_rdvz_get(rdvz, "x");
    fail += expect_true("point remains", point != NULL);
    if (point) {
        fail += expect_true("count is two", point->seeker_count == 2U);
        fail += expect_true("a links to c", a->next == c);
        fail += expect_true("c links to a", c->previous == a);
        fail += expect_true("head/tail updated", point->head == a && point->tail == c);
    }
    fail += expect_true("remove a", kc_rdvz_remove(rdvz, a) == KC_RDVZ_OK);
    fail += expect_true("remove c", kc_rdvz_remove(rdvz, c) == KC_RDVZ_OK);
    fail += expect_true("empty seeker-only point is reclaimed",
        kc_rdvz_get(rdvz, "x") == NULL);
    kc_rdvz_close(rdvz);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test case: data is copied.
 * Publication and seeker values are owned copies.
 * @return Number of failed assertions.
 */
static int case_data_is_copied(void) {
    const char *name = "data_is_copied";
    const char *detail = "publication and seeker values are owned copies";
    kc_rdvz_t *rdvz = NULL;
    const kc_rdvz_seeker_t *seeker = NULL;
    const kc_rdvz_point_t *point = NULL;
    char publication[] = "pub";
    char request[] = "req";
    int fail = 0;

    fail += expect_true("open succeeds", kc_rdvz_open(&rdvz, NULL) == KC_RDVZ_OK);
    fail += expect_true("publish succeeds",
        kc_rdvz_publish(rdvz, "copy", publication, 3U, &point) == KC_RDVZ_OK);
    fail += expect_true("seek succeeds",
        kc_rdvz_seek(rdvz, "copy", request, 3U, &seeker) == KC_RDVZ_OK);
    publication[0] = 'X';
    request[0] = 'Y';
    fail += expect_true("publication copy is independent",
        point && point->publication && bytes_equal(point->publication->data,
            point->publication->data_size, "pub"));
    fail += expect_true("seeker copy is independent",
        seeker && bytes_equal(seeker->data, seeker->data_size, "req"));
    kc_rdvz_close(rdvz);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test case: limits.
 * Publication, global seeker and per-point seeker limits are independent.
 * @return Number of failed assertions.
 */
static int case_limits(void) {
    const char *name = "limits";
    const char *detail = "publication, global seeker and per-point seeker limits are independent";
    kc_rdvz_options_t options;
    kc_rdvz_t *rdvz = NULL;
    const kc_rdvz_seeker_t *seeker = NULL;
    int fail = 0;

    memset(&options, 0, sizeof(options));
    options.max_publications = 1U;
    options.max_seekers = 2U;
    options.max_seekers_per_point = 1U;
    fail += expect_true("open succeeds", kc_rdvz_open(&rdvz, &options) == KC_RDVZ_OK);
    fail += expect_true("first publication fits",
        kc_rdvz_publish(rdvz, "a", NULL, 0U, NULL) == KC_RDVZ_OK);
    fail += expect_true("second publication hits limit",
        kc_rdvz_publish(rdvz, "b", NULL, 0U, NULL) == KC_RDVZ_ELIMIT);
    fail += expect_true("first seeker fits",
        kc_rdvz_seek(rdvz, "a", NULL, 0U, &seeker) == KC_RDVZ_OK);
    fail += expect_true("per-point seeker limit applies",
        kc_rdvz_seek(rdvz, "a", NULL, 0U, &seeker) == KC_RDVZ_ELIMIT);
    fail += expect_true("second point seeker fits global limit",
        kc_rdvz_seek(rdvz, "b", NULL, 0U, &seeker) == KC_RDVZ_OK);
    fail += expect_true("global seeker limit applies",
        kc_rdvz_seek(rdvz, "c", NULL, 0U, &seeker) == KC_RDVZ_ELIMIT);
    kc_rdvz_close(rdvz);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test case: ttl independence.
 * Pruning publication expiry preserves seekers with independent TTL.
 * @return Number of failed assertions.
 */
static int case_ttl_independence(void) {
    const char *name = "ttl_independence";
    const char *detail = "pruning publication expiry preserves seekers with independent TTL";
    kc_rdvz_options_t options;
    kc_rdvz_t *rdvz = NULL;
    const kc_rdvz_seeker_t *seeker = NULL;
    const kc_rdvz_point_t *point;
    size_t pruned;
    int fail = 0;

    memset(&options, 0, sizeof(options));
    options.publication_ttl_ms = 5U;
    options.seeker_ttl_ms = 0U;
    fail += expect_true("open succeeds", kc_rdvz_open(&rdvz, &options) == KC_RDVZ_OK);
    fail += expect_true("publish succeeds",
        kc_rdvz_publish(rdvz, "ttl", "p", 1U, NULL) == KC_RDVZ_OK);
    fail += expect_true("seek succeeds",
        kc_rdvz_seek(rdvz, "ttl", "s", 1U, &seeker) == KC_RDVZ_OK);
    sleep_ms(20U);
    pruned = kc_rdvz_prune(rdvz);
    fail += expect_true("one publication pruned", pruned == 1U);
    point = kc_rdvz_get(rdvz, "ttl");
    fail += expect_true("point remains for seeker", point != NULL);
    if (point) {
        fail += expect_true("publication expired", point->publication == NULL);
        fail += expect_true("seeker survives", point->head == seeker && point->seeker_count == 1U);
    }
    kc_rdvz_close(rdvz);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test case: hash index.
 * Many identifiers survive hash growth and direct lookup.
 * @return Number of failed assertions.
 */
static int case_hash_index(void) {
    const char *name = "hash_index";
    const char *detail = "many identifiers survive hash growth and direct lookup";
    kc_rdvz_t *rdvz = NULL;
    const kc_rdvz_point_t *point;
    char id[32];
    int fail = 0;
    int i;

    fail += expect_true("open succeeds", kc_rdvz_open(&rdvz, NULL) == KC_RDVZ_OK);
    for (i = 0; i < 2048; i++) {
        snprintf(id, sizeof(id), "id-%d", i);
        if (kc_rdvz_publish(rdvz, id, &i, sizeof(i), NULL) != KC_RDVZ_OK) {
            fail += expect_true("bulk publish succeeds", 0);
            break;
        }
    }
    for (i = 0; i < 2048 && fail == 0; i++) {
        int value;
        snprintf(id, sizeof(id), "id-%d", i);
        point = kc_rdvz_get(rdvz, id);
        if (!point || !point->publication ||
            point->publication->data_size != sizeof(value)) {
            fail += expect_true("bulk lookup returns publication", 0);
            break;
        }
        memcpy(&value, point->publication->data, sizeof(value));
        if (value != i) {
            fail += expect_true("bulk lookup preserves value", 0);
            break;
        }
    }
    kc_rdvz_close(rdvz);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Test case: errors and version.
 * Invalid, duplicate and not-found states remain explicit.
 * @return Number of failed assertions.
 */
static int case_errors_and_version(void) {
    const char *name = "errors_and_version";
    const char *detail = "invalid, duplicate and not-found states remain explicit";
    kc_rdvz_t *rdvz = NULL;
    int fail = 0;

    fail += expect_true("open rejects missing out", kc_rdvz_open(NULL, NULL) == KC_RDVZ_EINVAL);
    fail += expect_true("open succeeds", kc_rdvz_open(&rdvz, NULL) == KC_RDVZ_OK);
    fail += expect_true("publish validates data",
        kc_rdvz_publish(rdvz, "x", NULL, 1U, NULL) == KC_RDVZ_EINVAL);
    fail += expect_true("publish succeeds",
        kc_rdvz_publish(rdvz, "x", NULL, 0U, NULL) == KC_RDVZ_OK);
    fail += expect_true("duplicate publication rejected",
        kc_rdvz_publish(rdvz, "x", NULL, 0U, NULL) == KC_RDVZ_EEXIST);
    fail += expect_true("first unpublish succeeds",
        kc_rdvz_unpublish(rdvz, "x") == KC_RDVZ_OK);
    fail += expect_true("second unpublish is not found",
        kc_rdvz_unpublish(rdvz, "x") == KC_RDVZ_ENOENT);
    fail += expect_true("status text is stable",
        strcmp(kc_rdvz_strerror(KC_RDVZ_EEXIST), "already published") == 0);
    fail += expect_true("version is nonzero", kc_rdvz_version() != 0U);
    kc_rdvz_close(rdvz);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Run all test cases.
 * @return Total number of failed assertions across all cases.
 */
static int case_all(void) {
    int rc = 0;

    test_case_total = 9;
    test_case_current = 0;
    run_case(&rc, case_seek_before_publish);
    run_case(&rc, case_unpublish_independent);
    run_case(&rc, case_remove_and_order);
    run_case(&rc, case_data_is_copied);
    run_case(&rc, case_limits);
    run_case(&rc, case_ttl_independence);
    run_case(&rc, case_hash_index);
    run_case(&rc, case_errors_and_version);
#ifndef __EMSCRIPTEN__
    run_case(&rc, case_rdvz_cli);
#endif
    printf("%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Test program entry point.
 * @param argc Argument count.
 * @param argv Argument vector: test case name or "all".
 * @return Exit code.
 */
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "test case: expected one argument, got %d\n", argc - 1);
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return case_all();
    if (strcmp(argv[1], "seek_before_publish") == 0) return case_seek_before_publish();
    if (strcmp(argv[1], "unpublish_independent") == 0) return case_unpublish_independent();
    if (strcmp(argv[1], "remove_and_order") == 0) return case_remove_and_order();
    if (strcmp(argv[1], "data_is_copied") == 0) return case_data_is_copied();
    if (strcmp(argv[1], "limits") == 0) return case_limits();
    if (strcmp(argv[1], "ttl_independence") == 0) return case_ttl_independence();
    if (strcmp(argv[1], "hash_index") == 0) return case_hash_index();
    if (strcmp(argv[1], "errors_and_version") == 0) return case_errors_and_version();
#ifndef __EMSCRIPTEN__
    if (strcmp(argv[1], "rdvz_cli") == 0) return case_rdvz_cli();
#endif
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}

#ifndef __EMSCRIPTEN__
#ifndef KC_CLI_PATH
#define KC_CLI_PATH "rdvz"
#endif

/**
 * Runs one rdvz CLI operation.
 * @param args Command arguments.
 * @return Shell exit status.
 */
static int rdvz_cli_run(const char *args) {
    char command[4096];
#ifdef _WIN32
    snprintf(command, sizeof(command), "\"\"%s\" %s > NUL 2>&1\"", KC_CLI_PATH, args);
#else
    snprintf(command, sizeof(command), "\"%s\" %s > /dev/null 2>&1", KC_CLI_PATH, args);
#endif
    return system(command);
}

/**
 * Tests the rdvz command-line operations.
 * @return One when a check failed, otherwise zero.
 */
static int case_rdvz_cli(void) {
    int fail = 0;
    fail += rdvz_cli_run("--help") != 0;
    fail += rdvz_cli_run("--version") != 0;
    fail += rdvz_cli_run("publish room pub") != 0;
    fail += rdvz_cli_run("seek room seeker") != 0;
    fail += rdvz_cli_run("match room pub seeker") != 0;
    fail += rdvz_cli_run("match room pub") == 0;
    case_result(fail, "rdvz_cli", "runs publication, seeking and matching operations");
    return fail != 0;
}
#endif
