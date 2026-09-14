/**
 * test.c - libgrd public API tests.
 * Summary: Tests each public libgrd function through one CTest case.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libgrd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

static int test_case_total = 0;
static int test_case_current = 0;

/**
 * Prints a test case result line.
 * @param fail Non-zero when the case failed.
 * @param name Public API function under test.
 * @param detail Behavior verified by the case.
 * @return None.
 */
static void case_result(int fail, const char *name, const char *detail) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        fail ? "FAIL" : "PASS", name, detail);
}

typedef int (*case_fn)(void);

/**
 * Runs one test case with counter tracking.
 * @param rc Destination accumulator.
 * @param fn Test case function.
 * @return None.
 */
static void run_case(int *rc, case_fn fn) {
    test_case_current++;
    *rc += fn();
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
 * Tests kc_grd_version.
 * @return 0 on success, 1 on failure.
 */
static int case_version(void) {
    const char *name = "kc_grd_version";
    const char *detail = "version returns non-zero";
    int fail = 0;
    fail = expect_true(name, kc_grd_version() != 0U);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_grd_options_default.
 * @return 0 on success, 1 on failure.
 */
static int case_options_default(void) {
    const char *name = "kc_grd_options_default";
    const char *detail = "default options zeroed";
    kc_grd_options_t opts;
    opts = kc_grd_options_default();
    int fail = 0;
    fail = expect_true(name, opts.width == 0 && opts.height == 0);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_grd_options_load_env.
 * @return 0 on success, 1 on failure.
 */
static int case_options_load_env(void) {
    const char *name = "kc_grd_options_load_env";
    const char *detail = "load_env does not crash";
    kc_grd_options_t opts = {0};
    kc_grd_options_load_env(&opts);
    kc_grd_options_load_env(NULL);
    int fail = 0;
    fail = expect_true(name, 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_grd_options_free.
 * @return 0 on success, 1 on failure.
 */
static int case_options_free(void) {
    const char *name = "kc_grd_options_free";
    const char *detail = "options_free does not crash";
    kc_grd_options_t opts = {0};
    kc_grd_options_free(&opts);
    kc_grd_options_free(NULL);
    int fail = 0;
    fail = expect_true(name, 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_grd_box_new.
 * @return 0 on success, 1 on failure.
 */
static int case_box_new(void) {
    const char *name = "kc_grd_box_new";
    const char *detail = "box_new creates valid box";
    kc_grd_box_t *b;
    b = kc_grd_box_new();
    if (!b) return 1;
    int fail = 0;
    fail |= expect_true("new box border=1", b->border == 1);
    fail |= expect_true("new box padding=1", b->padding == 1);
    kc_grd_box_free(b);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_grd_box_free.
 * @return 0 on success, 1 on failure.
 */
static int case_box_free(void) {
    const char *name = "kc_grd_box_free";
    const char *detail = "free(NULL) does not crash";
    kc_grd_box_free(NULL);
    int fail = 0;
    fail = expect_true(name, 1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_grd_split_set.
 * @return 0 on success, 1 on failure.
 */
static int case_split_set(void) {
    const char *name = "kc_grd_split_set";
    const char *detail = "split_set creates and replaces splits";
    kc_grd_box_t *b = kc_grd_box_new();
    kc_grd_split_t *s;
    int fail = 0;
    s = kc_grd_split_set(NULL, KC_GRD_ROW);
    fail |= expect_true("split_set(NULL) returns NULL", s == NULL);
    s = kc_grd_split_set(b, KC_GRD_ROW);
    fail |= expect_true("split_set creates split", s != NULL);
    fail |= expect_true("split kind is ROW", s->kind == KC_GRD_ROW);
    fail |= expect_true("box has split", b->split == s);
    s = kc_grd_split_set(b, KC_GRD_COL);
    fail |= expect_true("split_set replaces", s->kind == KC_GRD_COL);
    kc_grd_box_free(b);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_grd_split_add.
 * @return 0 on success, 1 on failure.
 */
static int case_split_add(void) {
    const char *name = "kc_grd_split_add";
    const char *detail = "split_add adds children to split";
    kc_grd_box_t *b = kc_grd_box_new();
    kc_grd_split_t *s = kc_grd_split_set(b, KC_GRD_ROW);
    kc_grd_box_t *c1 = kc_grd_box_new();
    kc_grd_box_t *c2 = kc_grd_box_new();
    int fail = 0;
    fail |= expect_int("split_add(NULL) returns -1", -1, kc_grd_split_add(NULL, c1, 1.0f));
    fail |= expect_int("split_add child NULL returns -1", -1, kc_grd_split_add(s, NULL, 1.0f));
    fail |= expect_int("split_add c1 returns 0", 0, kc_grd_split_add(s, c1, 1.0f));
    fail |= expect_int("split_add c2 returns 0", 0, kc_grd_split_add(s, c2, 2.0f));
    fail |= expect_int("split count is 2", 2, s->count);
    fail |= expect_true("c1 parent set", c1->parent == b);
    kc_grd_box_free(b);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_grd_split_weight.
 * @return 0 on success, 1 on failure.
 */
static int case_split_weight(void) {
    const char *name = "kc_grd_split_weight";
    const char *detail = "split_weight updates weights";
    kc_grd_box_t *b = kc_grd_box_new();
    kc_grd_split_t *s = kc_grd_split_set(b, KC_GRD_ROW);
    kc_grd_box_t *c1 = kc_grd_box_new();
    kc_grd_split_add(s, c1, 1.0f);
    int fail = 0;
    fail |= expect_int("split_weight NULL returns -1", -1, kc_grd_split_weight(NULL, 0, 1.0f));
    fail |= expect_int("split_weight invalid idx returns -1", -1, kc_grd_split_weight(s, 5, 1.0f));
    fail |= expect_int("split_weight valid returns 0", 0, kc_grd_split_weight(s, 0, 3.0f));
    fail |= expect_true("weight updated", s->weights[0] == 3.0f);
    kc_grd_box_free(b);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_grd_box_bounds.
 * @return 0 on success, 1 on failure.
 */
static int case_box_bounds(void) {
    const char *name = "kc_grd_box_bounds";
    const char *detail = "box_bounds sets coordinates";
    kc_grd_box_t *b = kc_grd_box_new();
    kc_grd_box_bounds(NULL, 0, 0, 100, 100);
    kc_grd_box_bounds(b, 10, 20, 200, 300);
    int fail = 0;
    fail |= expect_int("box x set", 10, b->x);
    fail |= expect_int("box y set", 20, b->y);
    fail |= expect_int("box w set", 200, b->w);
    fail |= expect_int("box h set", 300, b->h);
    fail |= expect_true("inner_x computed", b->inner_x >= b->x);
    kc_grd_box_free(b);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_grd_box_layout.
 * @return 0 on success, 1 on failure.
 */
static int case_box_layout(void) {
    const char *name = "kc_grd_box_layout";
    const char *detail = "box_layout computes child sizes";
    kc_grd_box_t *b = kc_grd_box_new();
    kc_grd_box_layout(NULL);
    kc_grd_box_bounds(b, 0, 0, 100, 100);
    kc_grd_split_t *s = kc_grd_split_set(b, KC_GRD_ROW);
    kc_grd_box_t *c1 = kc_grd_box_new();
    kc_grd_box_t *c2 = kc_grd_box_new();
    kc_grd_split_add(s, c1, 1.0f);
    kc_grd_split_add(s, c2, 1.0f);
    kc_grd_box_layout(b);
    int fail = 0;
    fail |= expect_true("c1 has width", c1->w > 0);
    fail |= expect_true("c2 has width", c2->w > 0);
    kc_grd_box_free(b);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_grd_split_gap.
 * @return 0 on success, 1 on failure.
 */
static int case_split_gap(void) {
    const char *name = "kc_grd_split_gap";
    const char *detail = "split_gap sets gap and min_px";
    kc_grd_split_gap(NULL, 5, 10);
    kc_grd_box_t *b = kc_grd_box_new();
    kc_grd_split_t *s = kc_grd_split_set(b, KC_GRD_ROW);
    kc_grd_split_gap(s, 5, 10);
    int fail = 0;
    fail |= expect_int("gap set", 5, s->gap);
    fail |= expect_int("min_px set", 10, s->min_px);
    kc_grd_box_free(b);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_grd_gap_hit.
 * @return 0 on success, 1 on failure.
 */
static int case_gap_hit(void) {
    const char *name = "kc_grd_gap_hit";
    const char *detail = "gap_hit detects gap position";
    kc_grd_box_t *b = kc_grd_box_new();
    kc_grd_gap_t gap;
    int fail = 0;

    fail |= expect_int("gap_hit(NULL) returns 0", 0, kc_grd_gap_hit(NULL, 0, 0, &gap));
    fail |= expect_int("gap_hit no split returns 0", 0, kc_grd_gap_hit(b, 0, 0, &gap));
    kc_grd_split_t *s = kc_grd_split_set(b, KC_GRD_ROW);
    kc_grd_box_t *c1 = kc_grd_box_new();
    kc_grd_box_t *c2 = kc_grd_box_new();
    kc_grd_split_add(s, c1, 1.0f);
    kc_grd_split_add(s, c2, 1.0f);
    kc_grd_split_gap(s, 4, 4);
    kc_grd_box_bounds(b, 0, 0, 200, 100);
    kc_grd_box_layout(b);
    int hit = kc_grd_gap_hit(b, c1->x + c1->w + 1, 50, &gap);
    fail |= expect_true("gap_hit finds gap", hit == 1);
    fail |= expect_true("gap hit has split", gap.split == s);
    kc_grd_box_free(b);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_grd_drag_begin.
 * @return 0 on success, 1 on failure.
 */
static int case_drag_begin(void) {
    const char *name = "kc_grd_drag_begin";
    const char *detail = "drag_begin arms drag state";
    kc_grd_box_t *b = kc_grd_box_new();
    kc_grd_split_t *s = kc_grd_split_set(b, KC_GRD_ROW);
    kc_grd_box_t *c1 = kc_grd_box_new();
    kc_grd_box_t *c2 = kc_grd_box_new();
    kc_grd_split_add(s, c1, 1.0f);
    kc_grd_split_add(s, c2, 1.0f);
    kc_grd_split_gap(s, 4, 4);
    kc_grd_box_bounds(b, 0, 0, 200, 100);
    kc_grd_box_layout(b);
    kc_grd_gap_t gap;
    kc_grd_gap_hit(b, c1->x + c1->w + 1, 50, &gap);
    int fail = 0;
    fail |= expect_int("drag_begin returns 0", 0, kc_grd_drag_begin(&gap, gap.x, gap.y));
    fail |= expect_true("drag_on set", s->drag_on == 1);
    fail |= expect_int("drag_begin NULL returns -1", -1, kc_grd_drag_begin(NULL, 0, 0));
    kc_grd_drag_end(s);
    kc_grd_box_free(b);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_grd_drag_update.
 * @return 0 on success, 1 on failure.
 */
static int case_drag_update(void) {
    const char *name = "kc_grd_drag_update";
    const char *detail = "drag_update adjusts weights";
    kc_grd_box_t *b = kc_grd_box_new();
    kc_grd_split_t *s = kc_grd_split_set(b, KC_GRD_ROW);
    kc_grd_box_t *c1 = kc_grd_box_new();
    kc_grd_box_t *c2 = kc_grd_box_new();
    kc_grd_split_add(s, c1, 1.0f);
    kc_grd_split_add(s, c2, 1.0f);
    kc_grd_split_gap(s, 4, 4);
    kc_grd_box_bounds(b, 0, 0, 200, 100);
    kc_grd_box_layout(b);
    kc_grd_gap_t gap;
    kc_grd_gap_hit(b, c1->x + c1->w + 1, 50, &gap);
    int fail = 0;
    fail |= expect_int("drag_begin returns 0", 0, kc_grd_drag_begin(&gap, gap.x, gap.y));
    fail |= expect_int("drag_update returns 0", 0, kc_grd_drag_update(s, gap.x + 10, gap.y));
    kc_grd_drag_end(s);
    fail |= expect_int("drag_update no drag returns -1", -1, kc_grd_drag_update(s, 0, 0));
    kc_grd_box_free(b);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_grd_drag_end.
 * @return 0 on success, 1 on failure.
 */
static int case_drag_end(void) {
    const char *name = "kc_grd_drag_end";
    const char *detail = "drag_end clears drag state";
    kc_grd_box_t *b = kc_grd_box_new();
    kc_grd_split_t *s = kc_grd_split_set(b, KC_GRD_ROW);
    kc_grd_box_t *c1 = kc_grd_box_new();
    kc_grd_box_t *c2 = kc_grd_box_new();
    kc_grd_split_add(s, c1, 1.0f);
    kc_grd_split_add(s, c2, 1.0f);
    kc_grd_split_gap(s, 4, 4);
    kc_grd_box_bounds(b, 0, 0, 200, 100);
    kc_grd_box_layout(b);
    kc_grd_gap_t gap;
    kc_grd_gap_hit(b, c1->x + c1->w + 1, 50, &gap);
    int fail = 0;
    kc_grd_drag_end(NULL);
    fail |= expect_int("drag_begin returns 0", 0, kc_grd_drag_begin(&gap, gap.x, gap.y));
    kc_grd_drag_end(s);
    fail |= expect_int("drag_on cleared", 0, s->drag_on);
    fail |= expect_true("drag_end(NULL) does not crash", 1);
    kc_grd_box_free(b);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_grd_box_close.
 * @return 0 on success, 1 on failure.
 */
static int case_box_close(void) {
    const char *name = "kc_grd_box_close";
    const char *detail = "box_close removes child from split";
    kc_grd_box_t *root = kc_grd_box_new();
    int fail = 0;
    fail |= expect_int("box_close no parent returns -1", -1, kc_grd_box_close(NULL));
    fail |= expect_int("box_close no parent returns -1", -1, kc_grd_box_close(root));
    kc_grd_split_t *s = kc_grd_split_set(root, KC_GRD_ROW);
    kc_grd_box_t *c1 = kc_grd_box_new();
    kc_grd_box_t *c2 = kc_grd_box_new();
    kc_grd_box_t *c3 = kc_grd_box_new();
    kc_grd_split_add(s, c1, 1.0f);
    kc_grd_split_add(s, c2, 1.0f);
    kc_grd_split_add(s, c3, 1.0f);
    fail |= expect_int("close c1 returns 0", 0, kc_grd_box_close(c1));
    fail |= expect_int("remaining count is 2", 2, s->count);
    kc_grd_box_free(root);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_grd_split_at.
 * @return 0 on success, 1 on failure.
 */
static int case_split_at(void) {
    const char *name = "kc_grd_split_at";
    const char *detail = "split_at returns child at index";
    kc_grd_box_t *b = kc_grd_box_new();
    kc_grd_split_t *s = kc_grd_split_set(b, KC_GRD_ROW);
    kc_grd_box_t *c1 = kc_grd_box_new();
    kc_grd_split_add(s, c1, 1.0f);
    int fail = 0;
    fail |= expect_true("split_at(NULL) returns NULL", kc_grd_split_at(NULL, 0) == NULL);
    fail |= expect_true("split_at invalid returns NULL", kc_grd_split_at(s, 5) == NULL);
    fail |= expect_true("split_at(0) returns c1", kc_grd_split_at(s, 0) == c1);
    kc_grd_box_free(b);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_grd_stop.
 * @return 0 on success, 1 on failure.
 */
static int case_stop(void) {
    const char *name = "kc_grd_stop";
    const char *detail = "stop sets stop_requested flag";
    kc_grd_box_t *b = kc_grd_box_new();
    kc_grd_stop(NULL);
    int fail = 0;
    fail |= expect_true("stop(NULL) no crash", 1);
    fail |= expect_true("stop not requested", b->stop_requested == 0);
    kc_grd_stop(b);
    fail |= expect_true("stop requested", b->stop_requested == 1);
    kc_grd_box_free(b);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests two contexts coexist.
 * @return 0 on success, 1 on failure.
 */
static int case_multictx(void) {
    const char *name = "two contexts coexist independently";
    const char *detail = "two contexts coexist independently";
    kc_grd_box_t *a = kc_grd_box_new();
    kc_grd_box_t *b = kc_grd_box_new();
    kc_grd_stop(a); kc_grd_stop(b);
    kc_grd_stop(a);
    int fail = 0;
    fail |= expect_true("a stop_requested", a->stop_requested);
    fail |= expect_true("b stop_requested", b->stop_requested);
    kc_grd_box_free(a);
    kc_grd_box_free(b);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests kc_grd_split_set + add + layout for row split.
 * @return 0 on success, 1 on failure.
 */
static int case_split_compute_row(void) {
    const char *name = "split row layout via public API";
    const char *detail = "row layout computes widths by weight";
    kc_grd_box_t *root = kc_grd_box_new();
    kc_grd_split_t *s = kc_grd_split_set(root, KC_GRD_ROW);
    float weights[] = {1.0f, 2.0f, 1.0f};
    int fail = 0;
    if (!root || !s) {
        kc_grd_box_free(root);
        case_result(1, name, detail);
        return 1;
    }
    kc_grd_split_gap(s, 0, 1);
    int i;
    for (i = 0; i < 3; i++) {
        kc_grd_box_t *child = kc_grd_box_new();
        if (!child || kc_grd_split_add(s, child, weights[i]) != 0) {
            kc_grd_box_free(root);
            case_result(1, name, detail);
            return 1;
        }
        child->border = 0;
        child->padding = 0;
    }
    kc_grd_box_bounds(root, 0, 0, 1920, 1080);
    kc_grd_box_layout(root);
    fail |= expect_int("child 0 w", 480, root->split->children[0]->w);
    fail |= expect_int("child 1 w", 959, root->split->children[1]->w);
    fail |= expect_int("child 2 w", 479, root->split->children[2]->w);
    kc_grd_box_free(root);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests split column with gap via public API.
 * @return 0 on success, 1 on failure.
 */
static int case_split_compute_col_gap(void) {
    const char *name = "split col gap layout via public API";
    const char *detail = "col layout with gap positions children";
    kc_grd_box_t *root = kc_grd_box_new();
    kc_grd_split_t *s = kc_grd_split_set(root, KC_GRD_COL);
    float weights[] = {1.0f, 1.0f, 1.0f, 1.0f};
    int fail = 0;
    if (!root || !s) {
        kc_grd_box_free(root);
        case_result(1, name, detail);
        return 1;
    }
    kc_grd_split_gap(s, 4, 1);
    int i;
    for (i = 0; i < 4; i++) {
        kc_grd_box_t *child = kc_grd_box_new();
        if (!child || kc_grd_split_add(s, child, weights[i]) != 0) {
            kc_grd_box_free(root);
            case_result(1, name, detail);
            return 1;
        }
        child->border = 0;
        child->padding = 0;
    }
    kc_grd_box_bounds(root, 0, 0, 800, 600);
    kc_grd_box_layout(root);
    fail |= expect_int("child 0 h", 147, root->split->children[0]->h);
    fail |= expect_int("child 1 y", 152, root->split->children[1]->y);
    kc_grd_box_free(root);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Tests invalid inputs via public API.
 * @return 0 on success, 1 on failure.
 */
static int case_split_compute_errors(void) {
    const char *name = "public API rejects bad inputs";
    const char *detail = "bad inputs rejected";
    int fail = 0;
    fail |= expect_true("split_set NULL returns NULL", kc_grd_split_set(NULL, KC_GRD_ROW) == NULL);
    fail |= expect_true("split_add NULL split returns -1", kc_grd_split_add(NULL, NULL, 1.0f) == -1);
    fail |= expect_true("split_add NULL child returns -1", kc_grd_split_add(&(kc_grd_split_t){0}, NULL, 1.0f) == -1);
    case_result(fail, name, detail);
    return fail == 0 ? 0 : 1;
}

/**
 * Runs all test cases in a single process.
 * @return 0 on success, 1 on failure.
 */
static int case_all(void) {
    int rc = 0;
    test_case_total = 23;
    test_case_current = 0;
    run_case(&rc, case_version);
    run_case(&rc, case_options_default);
    run_case(&rc, case_options_load_env);
    run_case(&rc, case_options_free);
    run_case(&rc, case_box_new);
    run_case(&rc, case_box_free);
    run_case(&rc, case_split_set);
    run_case(&rc, case_split_add);
    run_case(&rc, case_split_weight);
    run_case(&rc, case_box_bounds);
    run_case(&rc, case_box_layout);
    run_case(&rc, case_split_gap);
    run_case(&rc, case_gap_hit);
    run_case(&rc, case_drag_begin);
    run_case(&rc, case_drag_update);
    run_case(&rc, case_drag_end);
    run_case(&rc, case_box_close);
    run_case(&rc, case_split_at);
    run_case(&rc, case_stop);
    run_case(&rc, case_multictx);
    run_case(&rc, case_split_compute_row);
    run_case(&rc, case_split_compute_col_gap);
    run_case(&rc, case_split_compute_errors);
    printf("\n%d passed, %d failed\n", test_case_total - rc, rc);
    return rc;
}

/**
 * Runs one libgrd public API test case.
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
    if (strcmp(argv[1], "version") == 0) return case_version();
    if (strcmp(argv[1], "options-default") == 0) return case_options_default();
    if (strcmp(argv[1], "options-load-env") == 0) return case_options_load_env();
    if (strcmp(argv[1], "options-free") == 0) return case_options_free();
    if (strcmp(argv[1], "box-new") == 0) return case_box_new();
    if (strcmp(argv[1], "box-free") == 0) return case_box_free();
    if (strcmp(argv[1], "split-set") == 0) return case_split_set();
    if (strcmp(argv[1], "split-add") == 0) return case_split_add();
    if (strcmp(argv[1], "split-weight") == 0) return case_split_weight();
    if (strcmp(argv[1], "box-bounds") == 0) return case_box_bounds();
    if (strcmp(argv[1], "box-layout") == 0) return case_box_layout();
    if (strcmp(argv[1], "split-gap") == 0) return case_split_gap();
    if (strcmp(argv[1], "gap-hit") == 0) return case_gap_hit();
    if (strcmp(argv[1], "drag-begin") == 0) return case_drag_begin();
    if (strcmp(argv[1], "drag-update") == 0) return case_drag_update();
    if (strcmp(argv[1], "drag-end") == 0) return case_drag_end();
    if (strcmp(argv[1], "box-close") == 0) return case_box_close();
    if (strcmp(argv[1], "split-at") == 0) return case_split_at();
    if (strcmp(argv[1], "stop") == 0) return case_stop();
    if (strcmp(argv[1], "multictx") == 0) return case_multictx();
    if (strcmp(argv[1], "split-compute-row") == 0) return case_split_compute_row();
    if (strcmp(argv[1], "split-compute-col-gap") == 0) return case_split_compute_col_gap();
    if (strcmp(argv[1], "split-compute-errors") == 0) return case_split_compute_errors();
    fprintf(stderr, "unknown test case: %s\n", argv[1]);
    return 2;
}
