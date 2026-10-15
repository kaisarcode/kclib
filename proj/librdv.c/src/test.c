/**
 * test.c - librdv public API contract tests.
 * Summary: Validates rendezvous publication and seeker lifecycle.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 */

#include "librdv.h"

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

static int bytes_equal(const void *data, size_t size, const char *text) {
    size_t text_size = strlen(text);
    return size == text_size && (size == 0U || memcmp(data, text, size) == 0);
}

static int case_seek_before_publish(void) {
    const char *name = "seek_before_publish";
    const char *detail = "seekers exist independently and survive later publication";
    kc_rdv_t *rdv = NULL;
    const kc_rdv_seeker_t *s1 = NULL;
    const kc_rdv_seeker_t *s2 = NULL;
    const kc_rdv_point_t *point = NULL;
    int fail = 0;

    fail += expect_true("open succeeds", kc_rdv_open(&rdv, NULL) == KC_RDV_OK);
    fail += expect_true("first seek succeeds",
        kc_rdv_seek(rdv, "room", "one", 3U, &s1) == KC_RDV_OK);
    fail += expect_true("second seek succeeds",
        kc_rdv_seek(rdv, "room", "two", 3U, &s2) == KC_RDV_OK);
    point = kc_rdv_get(rdv, "room");
    fail += expect_true("point exists", point != NULL);
    if (point) {
        fail += expect_true("publication is absent", point->publication == NULL);
        fail += expect_true("two seekers are retained", point->seeker_count == 2U);
        fail += expect_true("head is first seeker", point->head == s1);
        fail += expect_true("tail is second seeker", point->tail == s2);
    }
    fail += expect_true("publish succeeds",
        kc_rdv_publish(rdv, "room", "pub", 3U, &point) == KC_RDV_OK);
    if (point) {
        fail += expect_true("publication appears", point->publication != NULL);
        fail += expect_true("seekers survive publish", point->seeker_count == 2U);
        fail += expect_true("published bytes are retained",
            point->publication && bytes_equal(point->publication->data,
                point->publication->data_size, "pub"));
    }
    kc_rdv_close(rdv);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_unpublish_independent(void) {
    const char *name = "unpublish_independent";
    const char *detail = "unpublish removes only publication and republish reuses waiting seekers";
    kc_rdv_t *rdv = NULL;
    const kc_rdv_seeker_t *s1 = NULL;
    const kc_rdv_seeker_t *s2 = NULL;
    const kc_rdv_point_t *point = NULL;
    int fail = 0;

    fail += expect_true("open succeeds", kc_rdv_open(&rdv, NULL) == KC_RDV_OK);
    fail += expect_true("publish succeeds",
        kc_rdv_publish(rdv, "room", "first", 5U, NULL) == KC_RDV_OK);
    fail += expect_true("seek succeeds",
        kc_rdv_seek(rdv, "room", "a", 1U, &s1) == KC_RDV_OK);
    fail += expect_true("unpublish succeeds",
        kc_rdv_unpublish(rdv, "room") == KC_RDV_OK);
    point = kc_rdv_get(rdv, "room");
    fail += expect_true("point survives unpublish", point != NULL);
    if (point) {
        fail += expect_true("publication removed", point->publication == NULL);
        fail += expect_true("first seeker survives", point->head == s1);
    }
    fail += expect_true("new seeker can wait while unpublished",
        kc_rdv_seek(rdv, "room", "b", 1U, &s2) == KC_RDV_OK);
    fail += expect_true("republish succeeds",
        kc_rdv_publish(rdv, "room", "second", 6U, &point) == KC_RDV_OK);
    if (point) {
        fail += expect_true("both seekers survive republish",
            point->seeker_count == 2U && point->head == s1 && point->tail == s2);
        fail += expect_true("new publication data is visible",
            point->publication && bytes_equal(point->publication->data,
                point->publication->data_size, "second"));
    }
    kc_rdv_close(rdv);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_remove_and_order(void) {
    const char *name = "remove_and_order";
    const char *detail = "middle removal is isolated and insertion order remains linked";
    kc_rdv_t *rdv = NULL;
    const kc_rdv_seeker_t *a = NULL;
    const kc_rdv_seeker_t *b = NULL;
    const kc_rdv_seeker_t *c = NULL;
    const kc_rdv_point_t *point;
    int fail = 0;

    fail += expect_true("open succeeds", kc_rdv_open(&rdv, NULL) == KC_RDV_OK);
    fail += expect_true("seek a", kc_rdv_seek(rdv, "x", "a", 1U, &a) == KC_RDV_OK);
    fail += expect_true("seek b", kc_rdv_seek(rdv, "x", "b", 1U, &b) == KC_RDV_OK);
    fail += expect_true("seek c", kc_rdv_seek(rdv, "x", "c", 1U, &c) == KC_RDV_OK);
    fail += expect_true("remove middle", kc_rdv_remove(rdv, b) == KC_RDV_OK);
    point = kc_rdv_get(rdv, "x");
    fail += expect_true("point remains", point != NULL);
    if (point) {
        fail += expect_true("count is two", point->seeker_count == 2U);
        fail += expect_true("a links to c", a->next == c);
        fail += expect_true("c links to a", c->previous == a);
        fail += expect_true("head/tail updated", point->head == a && point->tail == c);
    }
    fail += expect_true("remove a", kc_rdv_remove(rdv, a) == KC_RDV_OK);
    fail += expect_true("remove c", kc_rdv_remove(rdv, c) == KC_RDV_OK);
    fail += expect_true("empty seeker-only point is reclaimed",
        kc_rdv_get(rdv, "x") == NULL);
    kc_rdv_close(rdv);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_data_is_copied(void) {
    const char *name = "data_is_copied";
    const char *detail = "publication and seeker values are owned copies";
    kc_rdv_t *rdv = NULL;
    const kc_rdv_seeker_t *seeker = NULL;
    const kc_rdv_point_t *point = NULL;
    char publication[] = "pub";
    char request[] = "req";
    int fail = 0;

    fail += expect_true("open succeeds", kc_rdv_open(&rdv, NULL) == KC_RDV_OK);
    fail += expect_true("publish succeeds",
        kc_rdv_publish(rdv, "copy", publication, 3U, &point) == KC_RDV_OK);
    fail += expect_true("seek succeeds",
        kc_rdv_seek(rdv, "copy", request, 3U, &seeker) == KC_RDV_OK);
    publication[0] = 'X';
    request[0] = 'Y';
    fail += expect_true("publication copy is independent",
        point && point->publication && bytes_equal(point->publication->data,
            point->publication->data_size, "pub"));
    fail += expect_true("seeker copy is independent",
        seeker && bytes_equal(seeker->data, seeker->data_size, "req"));
    kc_rdv_close(rdv);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_limits(void) {
    const char *name = "limits";
    const char *detail = "publication, global seeker and per-point seeker limits are independent";
    kc_rdv_options_t options;
    kc_rdv_t *rdv = NULL;
    const kc_rdv_seeker_t *seeker = NULL;
    int fail = 0;

    memset(&options, 0, sizeof(options));
    options.max_publications = 1U;
    options.max_seekers = 2U;
    options.max_seekers_per_point = 1U;
    fail += expect_true("open succeeds", kc_rdv_open(&rdv, &options) == KC_RDV_OK);
    fail += expect_true("first publication fits",
        kc_rdv_publish(rdv, "a", NULL, 0U, NULL) == KC_RDV_OK);
    fail += expect_true("second publication hits limit",
        kc_rdv_publish(rdv, "b", NULL, 0U, NULL) == KC_RDV_ELIMIT);
    fail += expect_true("first seeker fits",
        kc_rdv_seek(rdv, "a", NULL, 0U, &seeker) == KC_RDV_OK);
    fail += expect_true("per-point seeker limit applies",
        kc_rdv_seek(rdv, "a", NULL, 0U, &seeker) == KC_RDV_ELIMIT);
    fail += expect_true("second point seeker fits global limit",
        kc_rdv_seek(rdv, "b", NULL, 0U, &seeker) == KC_RDV_OK);
    fail += expect_true("global seeker limit applies",
        kc_rdv_seek(rdv, "c", NULL, 0U, &seeker) == KC_RDV_ELIMIT);
    kc_rdv_close(rdv);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_ttl_independence(void) {
    const char *name = "ttl_independence";
    const char *detail = "pruning publication expiry preserves seekers with independent TTL";
    kc_rdv_options_t options;
    kc_rdv_t *rdv = NULL;
    const kc_rdv_seeker_t *seeker = NULL;
    const kc_rdv_point_t *point;
    size_t pruned;
    int fail = 0;

    memset(&options, 0, sizeof(options));
    options.publication_ttl_ms = 5U;
    options.seeker_ttl_ms = 0U;
    fail += expect_true("open succeeds", kc_rdv_open(&rdv, &options) == KC_RDV_OK);
    fail += expect_true("publish succeeds",
        kc_rdv_publish(rdv, "ttl", "p", 1U, NULL) == KC_RDV_OK);
    fail += expect_true("seek succeeds",
        kc_rdv_seek(rdv, "ttl", "s", 1U, &seeker) == KC_RDV_OK);
    sleep_ms(20U);
    pruned = kc_rdv_prune(rdv);
    fail += expect_true("one publication pruned", pruned == 1U);
    point = kc_rdv_get(rdv, "ttl");
    fail += expect_true("point remains for seeker", point != NULL);
    if (point) {
        fail += expect_true("publication expired", point->publication == NULL);
        fail += expect_true("seeker survives", point->head == seeker && point->seeker_count == 1U);
    }
    kc_rdv_close(rdv);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_hash_index(void) {
    const char *name = "hash_index";
    const char *detail = "many identifiers survive hash growth and direct lookup";
    kc_rdv_t *rdv = NULL;
    const kc_rdv_point_t *point;
    char id[32];
    int fail = 0;
    int i;

    fail += expect_true("open succeeds", kc_rdv_open(&rdv, NULL) == KC_RDV_OK);
    for (i = 0; i < 2048; i++) {
        snprintf(id, sizeof(id), "id-%d", i);
        if (kc_rdv_publish(rdv, id, &i, sizeof(i), NULL) != KC_RDV_OK) {
            fail += expect_true("bulk publish succeeds", 0);
            break;
        }
    }
    for (i = 0; i < 2048 && fail == 0; i++) {
        int value;
        snprintf(id, sizeof(id), "id-%d", i);
        point = kc_rdv_get(rdv, id);
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
    kc_rdv_close(rdv);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_errors_and_version(void) {
    const char *name = "errors_and_version";
    const char *detail = "invalid, duplicate and not-found states remain explicit";
    kc_rdv_t *rdv = NULL;
    int fail = 0;

    fail += expect_true("open rejects missing out", kc_rdv_open(NULL, NULL) == KC_RDV_EINVAL);
    fail += expect_true("open succeeds", kc_rdv_open(&rdv, NULL) == KC_RDV_OK);
    fail += expect_true("publish validates data",
        kc_rdv_publish(rdv, "x", NULL, 1U, NULL) == KC_RDV_EINVAL);
    fail += expect_true("publish succeeds",
        kc_rdv_publish(rdv, "x", NULL, 0U, NULL) == KC_RDV_OK);
    fail += expect_true("duplicate publication rejected",
        kc_rdv_publish(rdv, "x", NULL, 0U, NULL) == KC_RDV_EEXIST);
    fail += expect_true("first unpublish succeeds",
        kc_rdv_unpublish(rdv, "x") == KC_RDV_OK);
    fail += expect_true("second unpublish is not found",
        kc_rdv_unpublish(rdv, "x") == KC_RDV_ENOENT);
    fail += expect_true("status text is stable",
        strcmp(kc_rdv_strerror(KC_RDV_EEXIST), "already published") == 0);
    fail += expect_true("version is nonzero", kc_rdv_version() != 0U);
    kc_rdv_close(rdv);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

static int case_all(void) {
    int rc = 0;

    test_case_total = 8;
    test_case_current = 0;
    run_case(&rc, case_seek_before_publish);
    run_case(&rc, case_unpublish_independent);
    run_case(&rc, case_remove_and_order);
    run_case(&rc, case_data_is_copied);
    run_case(&rc, case_limits);
    run_case(&rc, case_ttl_independence);
    run_case(&rc, case_hash_index);
    run_case(&rc, case_errors_and_version);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

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
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
