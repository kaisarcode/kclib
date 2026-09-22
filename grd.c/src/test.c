/**
 * test.c - Grid tests.
 * Summary: Tests the grd public API.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libgrd.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int test_case_total;
static int test_case_current;

typedef int (*case_fn)(void);

/**
 * Reports a test case result.
 * @return void
 */
static void case_result(int failed, const char *name, const char *description) {
    printf("[%d/%d] [%s] %s: %s\n", test_case_current, test_case_total,
        failed ? "FAIL" : "PASS", name, description);
}

/**
 * Returns whether a condition fails.
 * @return Zero when the condition passes, otherwise one.
 */
static int check(int condition) {
    return condition ? 0 : 1;
}

/**
 * Returns whether floating-point values differ.
 * @return Zero when the values match, otherwise one.
 */
static int check_float(float actual, float expected) {
    return actual == expected ? 0 : 1;
}

/**
 * Runs a test case and accumulates failures.
 * @return void
 */
static void run_case(int *failed, case_fn function) {
    ++test_case_current;
    *failed += function();
}

/**
 * Tests that the grid version is nonzero.
 * @return Nonzero when the test fails.
 */
static int case_kc_grd_version(void) {
    int failed = check(kc_grd_version() != 0U);

    case_result(failed, "kc_grd_version", "returns a nonzero build version");
    return failed != 0;
}

/**
 * Tests opening independent X and Y grids.
 * @return Nonzero when the test fails.
 */
static int case_kc_grd_grid_open(void) {
    kc_grd_grid_t *x_grid = NULL;
    kc_grd_grid_t *y_grid = NULL;
    kc_grd_region_t *x_region;
    kc_grd_region_t *y_region;
    kc_grd_box_t *x_box;
    kc_grd_content_t *x_content;
    int failed = 0;

    failed |= check(kc_grd_grid_open(&x_grid, KC_GRD_X) == 0);
    x_region = kc_grd_grid_get_region(x_grid);
    failed |= check(x_region != NULL);
    failed |= check(kc_grd_region_get_direction(x_region) == KC_GRD_X);
    failed |= check(kc_grd_region_get_box_count(x_region) == 1U);
    failed |= check(kc_grd_region_get_separator_count(x_region) == 0U);
    x_box = kc_grd_region_get_box(x_region, 0);
    x_content = kc_grd_box_get_content(x_box);
    failed |= check(x_box != NULL);
    failed |= check(check_float(kc_grd_box_get_weight(x_box), 1.0f) == 0);
    failed |= check(x_content != NULL);
    failed |= check(kc_grd_box_get_region(x_box) == NULL);
    failed |= check(kc_grd_content_get_box(x_content) == x_box);

    failed |= check(kc_grd_grid_open(&y_grid, KC_GRD_Y) == 0);
    y_region = kc_grd_grid_get_region(y_grid);
    failed |= check(y_region != NULL);
    failed |= check(kc_grd_region_get_direction(y_region) == KC_GRD_Y);
    failed |= check(kc_grd_region_get_box_count(y_region) == 1U);
    failed |= check(kc_grd_region_get_box_count(x_region) == 1U);
    failed |= check(kc_grd_region_insert_box(x_region, 1, 2.0f, 3.0f) != NULL);
    failed |= check(kc_grd_region_get_box_count(x_region) == 2U);
    failed |= check(kc_grd_region_get_box_count(y_region) == 1U);

    kc_grd_grid_close(x_grid);
    kc_grd_grid_close(y_grid);
    case_result(failed, "kc_grd_grid_open", "opens independent X and Y grids");
    return failed != 0;
}

static int case_kc_grd_region_direction(void) {
    kc_grd_grid_t *grid = NULL;
    kc_grd_region_t *region;
    int failed = 0;

    failed |= check(kc_grd_grid_open(&grid, KC_GRD_X) == 0);
    region = kc_grd_grid_get_region(grid);
    failed |= check(kc_grd_region_set_direction(region, KC_GRD_Y) == 0);
    failed |= check(kc_grd_region_get_direction(region) == KC_GRD_Y);
    failed |= check(kc_grd_region_set_direction(region, KC_GRD_X) == 0);
    failed |= check(kc_grd_region_get_direction(region) == KC_GRD_X);
    failed |= check(kc_grd_region_set_direction(region, (kc_grd_direction_t)0) == -1);
    failed |= check(kc_grd_region_get_direction(region) == KC_GRD_X);
    failed |= check(kc_grd_region_set_direction(region, (kc_grd_direction_t)3) == -1);
    failed |= check(kc_grd_region_get_direction(region) == KC_GRD_X);

    kc_grd_grid_close(grid);
    case_result(failed, "kc_grd_region_direction", "validates mutable directions without invalid mutation");
    return failed != 0;
}

static int case_kc_grd_box_weight(void) {
    kc_grd_grid_t *grid = NULL;
    kc_grd_region_t *region;
    kc_grd_box_t *first;
    kc_grd_box_t *second;
    int failed = 0;

    failed |= check(kc_grd_grid_open(&grid, KC_GRD_X) == 0);
    region = kc_grd_grid_get_region(grid);
    first = kc_grd_region_get_box(region, 0);
    second = kc_grd_region_insert_box(region, 1, 4.0f, 1.0f);
    failed |= check(first != NULL && second != NULL);
    failed |= check(kc_grd_box_set_weight(first, 2.5f) == 0);
    failed |= check(check_float(kc_grd_box_get_weight(first), 2.5f) == 0);
    failed |= check(check_float(kc_grd_box_get_weight(second), 4.0f) == 0);
    failed |= check(kc_grd_box_set_weight(first, 0.0f) == 0);
    failed |= check(check_float(kc_grd_box_get_weight(first), 0.0f) == 0);
    failed |= check(kc_grd_box_set_weight(first, -1.0f) == 0);
    failed |= check(check_float(kc_grd_box_get_weight(first), 0.0f) == 0);
    failed |= check(kc_grd_box_set_weight(first, NAN) == 0);
    failed |= check(check_float(kc_grd_box_get_weight(first), 0.0f) == 0);
    failed |= check(kc_grd_box_set_weight(first, INFINITY) == 0);
    failed |= check(check_float(kc_grd_box_get_weight(first), 0.0f) == 0);
    failed |= check(kc_grd_box_set_weight(first, -INFINITY) == 0);
    failed |= check(check_float(kc_grd_box_get_weight(first), 0.0f) == 0);
    failed |= check(check_float(kc_grd_box_get_weight(second), 4.0f) == 0);

    kc_grd_grid_close(grid);
    case_result(failed, "kc_grd_box_weight", "normalizes invalid weights and keeps siblings independent");
    return failed != 0;
}

static int case_kc_grd_region_insert_box(void) {
    kc_grd_grid_t *grid = NULL;
    kc_grd_region_t *region;
    kc_grd_box_t *a;
    kc_grd_box_t *b;
    kc_grd_box_t *c;
    kc_grd_box_t *d;
    kc_grd_content_t *b_content;
    kc_grd_separator_t *ab;
    kc_grd_separator_t *cb;
    kc_grd_separator_t *da;
    int failed = 0;

    failed |= check(kc_grd_grid_open(&grid, KC_GRD_X) == 0);
    region = kc_grd_grid_get_region(grid);
    a = kc_grd_region_get_box(region, 0);
    b = kc_grd_region_insert_box(region, 1, 2.0f, 2.0f);
    ab = kc_grd_region_get_separator(region, 0);
    b_content = kc_grd_box_get_content(b);
    failed |= check(a != NULL && b != NULL && ab != NULL);
    failed |= check(kc_grd_region_get_box_count(region) == 2U);
    failed |= check(kc_grd_region_get_separator_count(region) == 1U);
    failed |= check(kc_grd_region_get_box(region, 0) == a);
    failed |= check(kc_grd_region_get_box(region, 1) == b);
    failed |= check(check_float(kc_grd_box_get_weight(b), 2.0f) == 0);
    failed |= check(b_content != NULL);
    failed |= check(check_float(kc_grd_separator_get_size(ab), 2.0f) == 0);
    failed |= check(kc_grd_separator_get_before(ab) == a);
    failed |= check(kc_grd_separator_get_after(ab) == b);
    c = kc_grd_region_insert_box(region, 1, 3.0f, 3.0f);
    cb = kc_grd_region_get_separator(region, 1);
    d = kc_grd_region_insert_box(region, 0, 4.0f, 4.0f);
    da = kc_grd_region_get_separator(region, 0);
    failed |= check(a != NULL && b != NULL && c != NULL && d != NULL);
    failed |= check(kc_grd_region_get_box_count(region) == 4U);
    failed |= check(kc_grd_region_get_box(region, 0) == d);
    failed |= check(kc_grd_region_get_box(region, 1) == a);
    failed |= check(kc_grd_region_get_box(region, 2) == c);
    failed |= check(kc_grd_region_get_box(region, 3) == b);
    failed |= check(kc_grd_region_get_separator_count(region) == 3U);
    failed |= check(kc_grd_region_get_separator(region, 0) == da);
    failed |= check(kc_grd_region_get_separator(region, 1) == ab);
    failed |= check(kc_grd_region_get_separator(region, 2) == cb);
    failed |= check(check_float(kc_grd_separator_get_size(da), 4.0f) == 0);
    failed |= check(check_float(kc_grd_separator_get_size(ab), 2.0f) == 0);
    failed |= check(check_float(kc_grd_separator_get_size(cb), 3.0f) == 0);
    failed |= check(kc_grd_content_get_box(b_content) == b);

    kc_grd_grid_close(grid);
    case_result(failed, "kc_grd_region_insert_box", "inserts front, middle, and append positions deterministically");
    return failed != 0;
}

static int case_kc_grd_separator_size(void) {
    kc_grd_grid_t *grid = NULL;
    kc_grd_region_t *region;
    kc_grd_separator_t *first;
    kc_grd_separator_t *second;
    int failed = 0;

    failed |= check(kc_grd_grid_open(&grid, KC_GRD_X) == 0);
    region = kc_grd_grid_get_region(grid);
    failed |= check(kc_grd_region_insert_box(region, 1, 1.0f, 2.0f) != NULL);
    failed |= check(kc_grd_region_insert_box(region, 2, 1.0f, 3.0f) != NULL);
    first = kc_grd_region_get_separator(region, 0);
    second = kc_grd_region_get_separator(region, 1);
    failed |= check(kc_grd_separator_set_size(first, 5.0f) == 0);
    failed |= check(check_float(kc_grd_separator_get_size(first), 5.0f) == 0);
    failed |= check(check_float(kc_grd_separator_get_size(second), 3.0f) == 0);
    failed |= check(kc_grd_separator_set_size(first, 0.0f) == 0);
    failed |= check(check_float(kc_grd_separator_get_size(first), 0.0f) == 0);
    failed |= check(kc_grd_separator_set_size(first, -1.0f) == 0);
    failed |= check(check_float(kc_grd_separator_get_size(first), 0.0f) == 0);
    failed |= check(kc_grd_separator_set_size(first, NAN) == 0);
    failed |= check(check_float(kc_grd_separator_get_size(first), 0.0f) == 0);
    failed |= check(kc_grd_separator_set_size(first, INFINITY) == 0);
    failed |= check(check_float(kc_grd_separator_get_size(first), 0.0f) == 0);
    failed |= check(kc_grd_separator_set_size(first, -INFINITY) == 0);
    failed |= check(check_float(kc_grd_separator_get_size(first), 0.0f) == 0);
    failed |= check(check_float(kc_grd_separator_get_size(second), 3.0f) == 0);

    kc_grd_grid_close(grid);
    case_result(failed, "kc_grd_separator_size", "normalizes invalid sizes independently");
    return failed != 0;
}

static int case_kc_grd_separator_neighbors(void) {
    kc_grd_grid_t *grid = NULL;
    kc_grd_region_t *region;
    kc_grd_box_t *a;
    kc_grd_box_t *b;
    kc_grd_box_t *c;
    kc_grd_box_t *d;
    kc_grd_separator_t *first;
    kc_grd_separator_t *middle;
    kc_grd_separator_t *last;
    int failed = 0;

    failed |= check(kc_grd_grid_open(&grid, KC_GRD_X) == 0);
    region = kc_grd_grid_get_region(grid);
    a = kc_grd_region_get_box(region, 0);
    b = kc_grd_region_insert_box(region, 1, 1.0f, 1.0f);
    c = kc_grd_region_insert_box(region, 2, 1.0f, 2.0f);
    first = kc_grd_region_get_separator(region, 0);
    last = kc_grd_region_get_separator(region, 1);
    failed |= check(kc_grd_separator_get_before(first) == a);
    failed |= check(kc_grd_separator_get_after(first) == b);
    failed |= check(kc_grd_separator_get_before(last) == b);
    failed |= check(kc_grd_separator_get_after(last) == c);
    d = kc_grd_region_insert_box(region, 1, 1.0f, 3.0f);
    middle = kc_grd_region_get_separator(region, 1);
    failed |= check(kc_grd_separator_get_before(first) == a);
    failed |= check(kc_grd_separator_get_after(first) == d);
    failed |= check(kc_grd_separator_get_before(middle) == d);
    failed |= check(kc_grd_separator_get_after(middle) == b);
    failed |= check(kc_grd_separator_get_before(last) == b);
    failed |= check(kc_grd_separator_get_after(last) == c);

    kc_grd_grid_close(grid);
    case_result(failed, "kc_grd_separator_neighbors", "reports every separator neighbor after middle insertion");
    return failed != 0;
}

static int case_kc_grd_box_subdivide(void) {
    kc_grd_grid_t *grid = NULL;
    kc_grd_region_t *outer_region;
    kc_grd_box_t *outer;
    kc_grd_content_t *content;
    kc_grd_region_t *inner_region;
    kc_grd_box_t *inner;
    int failed = 0;

    failed |= check(kc_grd_grid_open(&grid, KC_GRD_X) == 0);
    outer_region = kc_grd_grid_get_region(grid);
    outer = kc_grd_region_get_box(outer_region, 0);
    content = kc_grd_box_get_content(outer);
    failed |= check(kc_grd_box_set_weight(outer, 7.0f) == 0);
    inner_region = kc_grd_box_subdivide(outer, KC_GRD_Y);
    inner = kc_grd_region_get_box(inner_region, 0);
    failed |= check(inner_region != NULL && inner != NULL);
    failed |= check(kc_grd_box_get_region(outer) == inner_region);
    failed |= check(kc_grd_region_get_direction(inner_region) == KC_GRD_Y);
    failed |= check(kc_grd_region_get_separator_count(inner_region) == 0U);
    failed |= check(check_float(kc_grd_box_get_weight(inner), 1.0f) == 0);
    failed |= check(check_float(kc_grd_box_get_weight(outer), 7.0f) == 0);
    failed |= check(kc_grd_box_get_content(outer) == NULL);
    failed |= check(kc_grd_box_get_content(inner) == content);
    failed |= check(kc_grd_content_get_box(content) == inner);
    failed |= check(kc_grd_box_subdivide(outer, KC_GRD_X) == NULL);
    failed |= check(kc_grd_box_get_region(outer) == inner_region);
    failed |= check(kc_grd_region_get_direction(inner_region) == KC_GRD_Y);
    failed |= check(kc_grd_region_get_box_count(inner_region) == 1U);
    failed |= check(kc_grd_region_get_separator_count(inner_region) == 0U);
    failed |= check(kc_grd_region_get_box(inner_region, 0) == inner);
    failed |= check(check_float(kc_grd_box_get_weight(outer), 7.0f) == 0);
    failed |= check(check_float(kc_grd_box_get_weight(inner), 1.0f) == 0);
    failed |= check(kc_grd_box_get_content(inner) == content);

    kc_grd_grid_close(grid);
    case_result(failed, "kc_grd_box_subdivide", "preserves outer weight and rejects repeated subdivision");
    return failed != 0;
}

static int case_kc_grd_content_identity(void) {
    kc_grd_grid_t *grid = NULL;
    kc_grd_region_t *region;
    kc_grd_box_t *box;
    kc_grd_content_t *content;
    kc_grd_region_t *nested;
    kc_grd_box_t *owner;
    int failed = 0;

    failed |= check(kc_grd_grid_open(&grid, KC_GRD_X) == 0);
    region = kc_grd_grid_get_region(grid);
    box = kc_grd_region_get_box(region, 0);
    content = kc_grd_box_get_content(box);
    failed |= check(content != NULL);
    failed |= check(kc_grd_content_get_box(content) == box);
    nested = kc_grd_box_subdivide(box, KC_GRD_Y);
    owner = kc_grd_region_get_box(nested, 0);
    failed |= check(content != NULL && nested != NULL && owner != NULL);
    failed |= check(kc_grd_box_get_content(owner) == content);
    failed |= check(kc_grd_content_get_box(content) == owner);
    failed |= check(kc_grd_box_get_content(box) == NULL);

    kc_grd_grid_close(grid);
    case_result(failed, "kc_grd_content_identity", "moves content ownership without replacement");
    return failed != 0;
}

static int case_kc_grd_box_remove_middle(void) {
    kc_grd_grid_t *grid = NULL;
    kc_grd_region_t *region;
    kc_grd_box_t *a;
    kc_grd_box_t *b;
    kc_grd_box_t *c;
    kc_grd_separator_t *pre_b;
    kc_grd_separator_t *post_b;
    int failed = 0;

    failed |= check(kc_grd_grid_open(&grid, KC_GRD_X) == 0);
    region = kc_grd_grid_get_region(grid);
    a = kc_grd_region_get_box(region, 0);
    b = kc_grd_region_insert_box(region, 1, 1.0f, 2.0f);
    c = kc_grd_region_insert_box(region, 2, 1.0f, 3.0f);
    pre_b = kc_grd_region_get_separator(region, 0);
    post_b = kc_grd_region_get_separator(region, 1);
    failed |= check(pre_b != NULL && post_b != NULL);
    failed |= check(kc_grd_box_remove(b) == 0);
    failed |= check(kc_grd_region_get_box_count(region) == 2U);
    failed |= check(kc_grd_region_get_box(region, 0) == a);
    failed |= check(kc_grd_region_get_box(region, 1) == c);
    failed |= check(kc_grd_region_get_separator_count(region) == 1U);
    failed |= check(kc_grd_region_get_separator(region, 0) == post_b);
    failed |= check(check_float(kc_grd_box_get_weight(a), 1.0f) == 0);
    failed |= check(check_float(kc_grd_box_get_weight(c), 1.0f) == 0);
    failed |= check(check_float(kc_grd_separator_get_size(post_b), 3.0f) == 0);
    failed |= check(kc_grd_separator_get_before(post_b) == a);
    failed |= check(kc_grd_separator_get_after(post_b) == c);

    kc_grd_grid_close(grid);
    case_result(failed, "kc_grd_box_remove_middle", "removes the preceding separator and retains the following separator");
    return failed != 0;
}

static int case_kc_grd_box_remove_edges(void) {
    kc_grd_grid_t *front_grid = NULL;
    kc_grd_grid_t *back_grid = NULL;
    kc_grd_region_t *region;
    kc_grd_box_t *a;
    kc_grd_box_t *b;
    kc_grd_box_t *c;
    kc_grd_separator_t *retained;
    int failed = 0;

    failed |= check(kc_grd_grid_open(&front_grid, KC_GRD_X) == 0);
    region = kc_grd_grid_get_region(front_grid);
    a = kc_grd_region_get_box(region, 0);
    b = kc_grd_region_insert_box(region, 1, 1.0f, 2.0f);
    c = kc_grd_region_insert_box(region, 2, 1.0f, 3.0f);
    retained = kc_grd_region_get_separator(region, 1);
    failed |= check(kc_grd_box_remove(a) == 0);
    failed |= check(kc_grd_region_get_box(region, 0) == b);
    failed |= check(kc_grd_region_get_box(region, 1) == c);
    failed |= check(kc_grd_region_get_separator(region, 0) == retained);
    failed |= check(check_float(kc_grd_separator_get_size(retained), 3.0f) == 0);

    failed |= check(kc_grd_grid_open(&back_grid, KC_GRD_X) == 0);
    region = kc_grd_grid_get_region(back_grid);
    a = kc_grd_region_get_box(region, 0);
    b = kc_grd_region_insert_box(region, 1, 1.0f, 2.0f);
    c = kc_grd_region_insert_box(region, 2, 1.0f, 3.0f);
    retained = kc_grd_region_get_separator(region, 0);
    failed |= check(kc_grd_box_remove(c) == 0);
    failed |= check(kc_grd_region_get_box(region, 0) == a);
    failed |= check(kc_grd_region_get_box(region, 1) == b);
    failed |= check(kc_grd_region_get_separator(region, 0) == retained);
    failed |= check(check_float(kc_grd_separator_get_size(retained), 2.0f) == 0);

    kc_grd_grid_close(front_grid);
    kc_grd_grid_close(back_grid);
    case_result(failed, "kc_grd_box_remove_edges", "removes front and back edges in separate trees");
    return failed != 0;
}

static int case_kc_grd_box_remove_last_rejected(void) {
    kc_grd_grid_t *grid = NULL;
    kc_grd_region_t *root_region;
    kc_grd_box_t *root_box;
    kc_grd_content_t *root_content;
    kc_grd_region_t *nested;
    kc_grd_box_t *first;
    kc_grd_box_t *second;
    int failed = 0;

    failed |= check(kc_grd_grid_open(&grid, KC_GRD_X) == 0);
    root_region = kc_grd_grid_get_region(grid);
    root_box = kc_grd_region_get_box(root_region, 0);
    root_content = kc_grd_box_get_content(root_box);
    failed |= check(root_content != NULL);
    failed |= check(kc_grd_box_remove(root_box) == -1);
    failed |= check(kc_grd_region_get_box_count(root_region) == 1U);
    failed |= check(kc_grd_region_get_box(root_region, 0) == root_box);
    failed |= check(kc_grd_box_get_content(root_box) == root_content);
    failed |= check(kc_grd_content_get_box(root_content) == root_box);
    nested = kc_grd_box_subdivide(root_box, KC_GRD_Y);
    first = kc_grd_region_get_box(nested, 0);
    second = kc_grd_region_insert_box(nested, 1, 1.0f, 2.0f);
    failed |= check(nested != NULL && first != NULL && second != NULL);
    failed |= check(kc_grd_box_remove(second) == 0);
    failed |= check(kc_grd_region_get_box_count(nested) == 1U);
    failed |= check(kc_grd_box_get_region(root_box) == nested);
    failed |= check(kc_grd_box_remove(first) == -1);
    failed |= check(kc_grd_region_get_box_count(nested) == 1U);
    failed |= check(kc_grd_box_get_region(root_box) == nested);

    kc_grd_grid_close(grid);
    case_result(failed, "kc_grd_box_remove_last_rejected", "rejects final removal without collapsing a nested region");
    return failed != 0;
}

static int case_kc_grd_nested_tree(void) {
    kc_grd_grid_t *grid = NULL;
    kc_grd_region_t *root;
    kc_grd_box_t *left;
    kc_grd_box_t *right;
    kc_grd_region_t *nested;
    kc_grd_box_t *top;
    kc_grd_box_t *bottom;
    kc_grd_separator_t *root_separator;
    kc_grd_separator_t *nested_separator;
    int failed = 0;

    failed |= check(kc_grd_grid_open(&grid, KC_GRD_X) == 0);
    root = kc_grd_grid_get_region(grid);
    left = kc_grd_region_get_box(root, 0);
    right = kc_grd_region_insert_box(root, 1, 5.0f, 4.0f);
    root_separator = kc_grd_region_get_separator(root, 0);
    nested = kc_grd_box_subdivide(left, KC_GRD_Y);
    top = kc_grd_region_get_box(nested, 0);
    bottom = kc_grd_region_insert_box(nested, 1, 7.0f, 6.0f);
    nested_separator = kc_grd_region_get_separator(nested, 0);
    failed |= check(right != NULL && nested != NULL && top != NULL && bottom != NULL);
    failed |= check(kc_grd_region_get_direction(root) == KC_GRD_X);
    failed |= check(kc_grd_region_get_direction(nested) == KC_GRD_Y);
    failed |= check(kc_grd_box_set_weight(left, 2.0f) == 0);
    failed |= check(kc_grd_box_set_weight(top, 3.0f) == 0);
    failed |= check(check_float(kc_grd_box_get_weight(left), 2.0f) == 0);
    failed |= check(check_float(kc_grd_box_get_weight(right), 5.0f) == 0);
    failed |= check(check_float(kc_grd_box_get_weight(top), 3.0f) == 0);
    failed |= check(check_float(kc_grd_box_get_weight(bottom), 7.0f) == 0);
    failed |= check(check_float(kc_grd_separator_get_size(root_separator), 4.0f) == 0);
    failed |= check(check_float(kc_grd_separator_get_size(nested_separator), 6.0f) == 0);
    failed |= check(kc_grd_separator_set_size(nested_separator, 8.0f) == 0);
    failed |= check(check_float(kc_grd_separator_get_size(root_separator), 4.0f) == 0);
    failed |= check(kc_grd_region_insert_box(nested, 2, 11.0f, 9.0f) != NULL);
    failed |= check(kc_grd_region_get_box_count(nested) == 3U);
    failed |= check(kc_grd_region_get_box_count(root) == 2U);
    failed |= check(kc_grd_region_set_direction(nested, KC_GRD_X) == 0);
    failed |= check(kc_grd_region_get_direction(root) == KC_GRD_X);

    kc_grd_grid_close(grid);
    case_result(failed, "kc_grd_nested_tree", "keeps mixed nested regions independent and closes recursively");
    return failed != 0;
}

static int case_kc_grd_invalid_inputs(void) {
    kc_grd_grid_t *grid = NULL;
    kc_grd_region_t *region;
    kc_grd_box_t *box;
    kc_grd_content_t *content;
    int failed = 0;

    failed |= check(kc_grd_grid_open(NULL, KC_GRD_X) == -1);
    failed |= check(kc_grd_grid_open(&grid, (kc_grd_direction_t)0) == -1);
    failed |= check(grid == NULL);
    kc_grd_grid_close(NULL);
    failed |= check(kc_grd_grid_get_region(NULL) == NULL);
    failed |= check(kc_grd_region_get_direction(NULL) == 0);
    failed |= check(kc_grd_region_set_direction(NULL, KC_GRD_X) == -1);
    failed |= check(kc_grd_region_get_box_count(NULL) == 0U);
    failed |= check(kc_grd_region_get_box(NULL, 0) == NULL);
    failed |= check(kc_grd_region_get_separator_count(NULL) == 0U);
    failed |= check(kc_grd_region_get_separator(NULL, 0) == NULL);
    failed |= check(kc_grd_region_insert_box(NULL, 0, 1.0f, 1.0f) == NULL);
    failed |= check(kc_grd_box_get_weight(NULL) == 0.0f);
    failed |= check(kc_grd_box_set_weight(NULL, 1.0f) == -1);
    failed |= check(kc_grd_box_get_region(NULL) == NULL);
    failed |= check(kc_grd_box_get_content(NULL) == NULL);
    failed |= check(kc_grd_box_subdivide(NULL, KC_GRD_X) == NULL);
    failed |= check(kc_grd_box_remove(NULL) == -1);
    failed |= check(kc_grd_separator_get_size(NULL) == 0.0f);
    failed |= check(kc_grd_separator_set_size(NULL, 1.0f) == -1);
    failed |= check(kc_grd_separator_get_before(NULL) == NULL);
    failed |= check(kc_grd_separator_get_after(NULL) == NULL);
    failed |= check(kc_grd_content_get_box(NULL) == NULL);

    failed |= check(kc_grd_grid_open(&grid, KC_GRD_X) == 0);
    region = kc_grd_grid_get_region(grid);
    box = kc_grd_region_get_box(region, 0);
    content = kc_grd_box_get_content(box);
    failed |= check(kc_grd_region_set_direction(region, (kc_grd_direction_t)0) == -1);
    failed |= check(kc_grd_region_get_box(region, 1) == NULL);
    failed |= check(kc_grd_region_get_separator(region, 0) == NULL);
    failed |= check(kc_grd_region_insert_box(region, 2, 1.0f, 1.0f) == NULL);
    failed |= check(kc_grd_box_subdivide(box, (kc_grd_direction_t)0) == NULL);
    failed |= check(kc_grd_box_get_content(box) == content);
    failed |= check(kc_grd_box_get_region(box) == NULL);

    kc_grd_grid_close(grid);
    case_result(failed, "kc_grd_invalid_inputs", "rejects null, out-of-range, and invalid arguments");
    return failed != 0;
}

static int run_all(void) {
    int failed = 0;

    test_case_total = 14;
    test_case_current = 0;
    run_case(&failed, case_kc_grd_version);
    run_case(&failed, case_kc_grd_grid_open);
    run_case(&failed, case_kc_grd_region_direction);
    run_case(&failed, case_kc_grd_box_weight);
    run_case(&failed, case_kc_grd_region_insert_box);
    run_case(&failed, case_kc_grd_separator_size);
    run_case(&failed, case_kc_grd_separator_neighbors);
    run_case(&failed, case_kc_grd_box_subdivide);
    run_case(&failed, case_kc_grd_content_identity);
    run_case(&failed, case_kc_grd_box_remove_middle);
    run_case(&failed, case_kc_grd_box_remove_edges);
    run_case(&failed, case_kc_grd_box_remove_last_rejected);
    run_case(&failed, case_kc_grd_nested_tree);
    run_case(&failed, case_kc_grd_invalid_inputs);
    printf("%d passed, %d failed\n", test_case_total - failed, failed);
    return failed != 0;
}

static int run_individual(case_fn function) {
    test_case_total = 1;
    test_case_current = 1;
    return function();
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "expected one test selector\n");
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) return run_all();
    if (strcmp(argv[1], "version") == 0) return run_individual(case_kc_grd_version);
    if (strcmp(argv[1], "grid-open") == 0) return run_individual(case_kc_grd_grid_open);
    if (strcmp(argv[1], "region-direction") == 0) return run_individual(case_kc_grd_region_direction);
    if (strcmp(argv[1], "box-weight") == 0) return run_individual(case_kc_grd_box_weight);
    if (strcmp(argv[1], "region-insert-box") == 0) return run_individual(case_kc_grd_region_insert_box);
    if (strcmp(argv[1], "separator-size") == 0) return run_individual(case_kc_grd_separator_size);
    if (strcmp(argv[1], "separator-neighbors") == 0) return run_individual(case_kc_grd_separator_neighbors);
    if (strcmp(argv[1], "box-subdivide") == 0) return run_individual(case_kc_grd_box_subdivide);
    if (strcmp(argv[1], "content-identity") == 0) return run_individual(case_kc_grd_content_identity);
    if (strcmp(argv[1], "box-remove-middle") == 0) return run_individual(case_kc_grd_box_remove_middle);
    if (strcmp(argv[1], "box-remove-edges") == 0) return run_individual(case_kc_grd_box_remove_edges);
    if (strcmp(argv[1], "box-remove-last-rejected") == 0) return run_individual(case_kc_grd_box_remove_last_rejected);
    if (strcmp(argv[1], "nested-tree") == 0) return run_individual(case_kc_grd_nested_tree);
    if (strcmp(argv[1], "invalid-inputs") == 0) return run_individual(case_kc_grd_invalid_inputs);
    fprintf(stderr, "unknown test selector: %s\n", argv[1]);
    return 2;
}
