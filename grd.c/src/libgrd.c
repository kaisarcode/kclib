/**
 * libgrd.c - Grid | Region | Divide
 * Summary: Core implementation of the passive grid layout engine.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libgrd.h"

#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Allocates n bytes of zeroed memory.
 * @param n Number of bytes to allocate.
 * @return Pointer to allocated memory, or NULL on failure or zero size.
 */
static void *zalloc(size_t n) {
    void *p;
    if (!n) return NULL;
    p = malloc(n);
    if (!p) return NULL;
    memset(p, 0, n);
    return p;
}

/**
 * Clamps a value to zero if negative.
 * @param v Input value.
 * @return v if non-negative, otherwise 0.
 */
static int nn(int v) {
    return v < 0 ? 0 : v;
}

/**
 * Clamps a weight to 1.0 if zero or negative.
 * @param w Input weight.
 * @return w if positive, otherwise 1.0.
 */
static float w_ok(float w) {
    return w > 0.0f ? w : 1.0f;
}

/**
 * Returns the coordinate along the split axis.
 * @param s Split pointer.
 * @param x X coordinate.
 * @param y Y coordinate.
 * @return x for row splits, y for column splits.
 */
static int axis(const kc_grd_split_t *s, int x, int y) {
    return s->kind == KC_GRD_ROW ? x : y;
}

/**
 * Computes the total weight of all children in a split.
 * @param s Split pointer.
 * @return Sum of all weights, or 1.0 if empty or all-zero.
 */
static float w_sum(const kc_grd_split_t *s) {
    float sum = 0.0f;
    int i;
    if (!s || s->count <= 0) return 1.0f;
    for (i = 0; i < s->count; ++i) sum += w_ok(s->weights[i]);
    return sum > 0.0f ? sum : 1.0f;
}

/**
 * Recomputes the inner area of a box from its border and padding.
 * @param b Box pointer.
 * @return void
 */
static void inner(kc_grd_box_t *b) {
    int inset;
    if (!b) return;
    inset = b->padding;
    if (b->border > inset) inset = b->border;
    inset = nn(inset);
    b->inner_x = b->x + inset;
    b->inner_y = b->y + inset;
    b->inner_w = nn(b->w - inset * 2);
    b->inner_h = nn(b->h - inset * 2);
}

/**
 * Releases a split and all its children recursively.
 * @param s Split pointer.
 * @return void
 */
static void split_free(kc_grd_split_t *s) {
    int i;
    if (!s) return;
    for (i = 0; i < s->count; ++i) kc_grd_box_free(s->children[i]);
    free(s->children);
    free(s->weights);
    free(s);
}

/**
 * Distributes children of a split across the owner's inner area.
 * @param owner Box that owns the split.
 * @param s Split to lay out.
 * @return void
 */
static void layout_split(kc_grd_box_t *owner, kc_grd_split_t *s) {
    int usable, cursor, i, used, x, y, w, h;
    int *sizes;
    float sum;

    if (!owner || !s || s->count <= 0) return;

    x = owner->inner_x;
    y = owner->inner_y;
    w = owner->inner_w;
    h = owner->inner_h;

    usable = (s->kind == KC_GRD_ROW ? w : h) - ((s->count - 1) * s->gap);
    usable = nn(usable);

    sizes = malloc(sizeof(*sizes) * (size_t)s->count);
    if (!sizes) return;

    sum = w_sum(s);
    used = 0;

    for (i = 0; i < s->count; ++i) {
        sizes[i] = (int)((usable * w_ok(s->weights[i])) / sum);
        if (sizes[i] < s->min_px) sizes[i] = s->min_px;
        used += sizes[i];
    }

    while (used > usable) {
        for (i = s->count - 1; i >= 0 && used > usable; --i) {
            if (sizes[i] > s->min_px) { sizes[i]--; used--; }
        }
    }

    while (used < usable) {
        for (i = 0; i < s->count && used < usable; ++i) { sizes[i]++; used++; }
    }

    cursor = (s->kind == KC_GRD_ROW ? x : y);

    for (i = 0; i < s->count; ++i) {
        kc_grd_box_t *c = s->children[i];
        if (!c) continue;
        if (s->kind == KC_GRD_ROW) {
            c->x = cursor; c->y = y; c->w = sizes[i]; c->h = h;
        } else {
            c->x = x; c->y = cursor; c->w = w; c->h = sizes[i];
        }
        cursor += sizes[i] + s->gap;
        inner(c);
        kc_grd_box_layout(c);
    }

    free(sizes);
}

/**
 * Moves the split and children from src into dst, then frees src.
 * @param dst Destination box.
 * @param src Source box whose split is transferred.
 * @return void
 */
static void box_promote_into(kc_grd_box_t *dst, kc_grd_box_t *src) {
    int i;
    if (!dst || !src) return;
    dst->border = src->border;
    dst->padding = src->padding;
    dst->split = src->split;
    if (dst->split) {
        dst->split->owner = dst;
        for (i = 0; i < dst->split->count; ++i) {
            if (dst->split->children[i]) dst->split->children[i]->parent = dst;
        }
    }
    src->split = NULL;
    free(src);
}

/**
 * Allocates and initializes a new box with border=1 and padding=1.
 * @return Box pointer or NULL on allocation failure.
 */
kc_grd_box_t *kc_grd_box_new(void) {
    kc_grd_box_t *b = zalloc(sizeof(*b));
    if (!b) return NULL;
    b->border = 1;
    b->padding = 1;
    return b;
}

/**
 * Releases a box and all owned splits and children recursively.
 * @param b Box pointer.
 * @return void
 */
void kc_grd_box_free(kc_grd_box_t *b) {
    if (!b) return;
    split_free(b->split);
    free(b);
}

/**
 * Attaches a new split to a box, replacing and freeing any existing split.
 * @param b Box to split.
 * @param kind Split direction.
 * @return Split pointer or NULL on failure.
 */
kc_grd_split_t *kc_grd_split_set(kc_grd_box_t *b, kc_grd_kind_t kind) {
    kc_grd_split_t *s;
    if (!b) return NULL;
    split_free(b->split);
    b->split = NULL;
    s = zalloc(sizeof(*s));
    if (!s) return NULL;
    s->owner = b;
    s->kind = kind;
    s->gap = 1;
    s->min_px = 4;
    s->drag_index = -1;
    b->split = s;
    return s;
}

/**
 * Adds one child box to a split with the given proportional weight.
 * @param s Split pointer.
 * @param child Child box to add.
 * @param weight Proportional weight (clamped to 1.0 if at most 0).
 * @return 0 on success, or -1 on failure.
 */
int kc_grd_split_add(kc_grd_split_t *s, kc_grd_box_t *child, float weight) {
    int next;
    kc_grd_box_t **c2;
    float *w2;

    if (!s || !child) return -1;

    if (s->count == s->cap) {
        next = s->cap ? s->cap * 2 : 4;

        c2 = realloc(s->children, sizeof(*c2) * (size_t)next);
        if (!c2) return -1;
        s->children = c2;

        w2 = realloc(s->weights, sizeof(*w2) * (size_t)next);
        if (!w2) return -1;
        s->weights = w2;

        s->cap = next;
    }

    child->parent = s->owner;
    s->children[s->count] = child;
    s->weights[s->count] = w_ok(weight);
    s->count++;
    return 0;
}

/**
 * Updates the proportional weight of one child in a split.
 * @param s Split pointer.
 * @param index Child index.
 * @param weight New proportional weight (clamped to 1.0 if at most 0).
 * @return 0 on success, or -1 on invalid input.
 */
int kc_grd_split_weight(kc_grd_split_t *s, int index, float weight) {
    if (!s || index < 0 || index >= s->count) return -1;
    s->weights[index] = w_ok(weight);
    return 0;
}

/**
 * Sets the outer bounds of a box and recomputes its inner area.
 * @param b Box pointer.
 * @param x Left edge in pixels.
 * @param y Top edge in pixels.
 * @param w Width in pixels.
 * @param h Height in pixels.
 * @return void
 */
void kc_grd_box_bounds(kc_grd_box_t *b, int x, int y, int w, int h) {
    if (!b) return;
    b->x = x; b->y = y; b->w = nn(w); b->h = nn(h);
    inner(b);
}

/**
 * Recursively recomputes the layout of a box and all its children.
 * @param b Box pointer.
 * @return void
 */
void kc_grd_box_layout(kc_grd_box_t *b) {
    if (!b) return;
    inner(b);
    if (b->split) layout_split(b, b->split);
}

/**
 * Configures the gap and minimum child size for a split.
 * @param s Split pointer.
 * @param gap Gap between children in pixels.
 * @param min_px Minimum child size in pixels.
 * @return void
 */
void kc_grd_split_gap(kc_grd_split_t *s, int gap, int min_px) {
    if (!s) return;
    s->gap = nn(gap);
    s->min_px = nn(min_px);
}

/**
 * Tests whether a point falls on a gap separator, searching recursively.
 * @param b Root box to search from.
 * @param x X coordinate to test.
 * @param y Y coordinate to test.
 * @param out Destination gap descriptor filled on hit.
 * @return 1 on hit, or 0 on miss.
 */
int kc_grd_gap_hit(kc_grd_box_t *b, int x, int y, kc_grd_gap_t *out) {
    int i;
    if (!b || !b->split || !out) return 0;

    if (b->split->kind == KC_GRD_ROW) {
        for (i = 0; i < b->split->count - 1; ++i) {
            kc_grd_box_t *c = b->split->children[i];
            int gx;
            if (!c) continue;
            gx = c->x + c->w;
            if (x >= gx && x < gx + b->split->gap &&
                y >= b->inner_y && y < b->inner_y + b->inner_h) {
                out->x = gx; out->y = b->inner_y;
                out->w = b->split->gap; out->h = b->inner_h;
                out->index = i; out->split = b->split;
                return 1;
            }
        }
    } else {
        for (i = 0; i < b->split->count - 1; ++i) {
            kc_grd_box_t *c = b->split->children[i];
            int gy;
            if (!c) continue;
            gy = c->y + c->h;
            if (y >= gy && y < gy + b->split->gap &&
                x >= b->inner_x && x < b->inner_x + b->inner_w) {
                out->x = b->inner_x; out->y = gy;
                out->w = b->inner_w; out->h = b->split->gap;
                out->index = i; out->split = b->split;
                return 1;
            }
        }
    }

    for (i = 0; i < b->split->count; ++i) {
        if (kc_grd_gap_hit(b->split->children[i], x, y, out)) return 1;
    }

    return 0;
}

/**
 * Begins a drag-resize operation on a gap.
 * @param gap Gap descriptor from kc_grd_gap_hit.
 * @param x Pointer X position at drag start.
 * @param y Pointer Y position at drag start.
 * @return 0 on success, or -1 on failure.
 */
int kc_grd_drag_begin(const kc_grd_gap_t *gap, int x, int y) {
    kc_grd_split_t *s;
    kc_grd_box_t *a, *b;
    int ax;

    if (!gap || !gap->split) return -1;
    s = gap->split;
    if (gap->index < 0 || gap->index + 1 >= s->count) return -1;

    a = s->children[gap->index];
    b = s->children[gap->index + 1];
    if (!a || !b) return -1;

    ax = axis(s, x, y);
    s->drag_on = 1;
    s->drag_index = gap->index;
    s->drag_anchor = ax;
    s->drag_a = w_ok(s->weights[gap->index]);
    s->drag_b = w_ok(s->weights[gap->index + 1]);
    s->drag_px_a = (s->kind == KC_GRD_ROW) ? a->w : a->h;
    s->drag_px_b = (s->kind == KC_GRD_ROW) ? b->w : b->h;
    return 0;
}

/**
 * Updates proportional weights during an active drag-resize operation.
 * @param s Split owning the active drag.
 * @param x Current pointer X position.
 * @param y Current pointer Y position.
 * @return 0 on success, or -1 on failure.
 */
int kc_grd_drag_update(kc_grd_split_t *s, int x, int y) {
    int ax, delta, apx, bpx, span;
    float total;

    if (!s || !s->drag_on) return -1;

    ax = axis(s, x, y);
    delta = ax - s->drag_anchor;
    apx = s->drag_px_a + delta;
    bpx = s->drag_px_b - delta;

    if (apx < s->min_px || bpx < s->min_px) return 0;

    span = apx + bpx;
    if (span <= 0) return -1;

    total = s->drag_a + s->drag_b;
    s->weights[s->drag_index] = total * ((float)apx / (float)span);
    s->weights[s->drag_index + 1] = total * ((float)bpx / (float)span);
    return 0;
}

/**
 * Ends an active drag-resize operation and resets drag state.
 * @param s Split owning the active drag.
 * @return void
 */
void kc_grd_drag_end(kc_grd_split_t *s) {
    if (!s) return;
    s->drag_on = 0;
    s->drag_index = -1;
    s->drag_anchor = 0;
    s->drag_px_a = 0;
    s->drag_px_b = 0;
    s->drag_a = 0.0f;
    s->drag_b = 0.0f;
}

/**
 * Removes a box from its parent split and restructures the tree.
 * @param b Box to remove.
 * @return 0 on success, or -1 on failure.
 */
int kc_grd_box_close(kc_grd_box_t *b) {
    kc_grd_box_t *parent;
    kc_grd_split_t *s;
    int index, i;

    if (!b || !b->parent) return -1;
    parent = b->parent;
    s = parent->split;
    if (!s) return -1;

    index = -1;
    for (i = 0; i < s->count; ++i) {
        if (s->children[i] == b) { index = i; break; }
    }
    if (index < 0) return -1;

    kc_grd_box_free(b);

    for (i = index; i < s->count - 1; ++i) {
        s->children[i] = s->children[i + 1];
        s->weights[i] = s->weights[i + 1];
    }
    s->count--;

    if (s->count <= 0) {
        free(s->children);
        free(s->weights);
        free(s);
        parent->split = NULL;
        return 0;
    }

    if (s->count == 1) {
        kc_grd_box_t *keep = s->children[0];
        free(s->children);
        free(s->weights);
        free(s);
        parent->split = NULL;
        box_promote_into(parent, keep);
    }

    return 0;
}

/**
 * Returns the child box at the given index in a split.
 * @param s Split pointer.
 * @param i Child index.
 * @return Child pointer or NULL on invalid input.
 */
kc_grd_box_t *kc_grd_split_at(const kc_grd_split_t *s, int i) {
    if (!s || i < 0 || i >= s->count) return NULL;
    return s->children[i];
}

typedef enum {
    KC_ENV_TYPE_INT,
    KC_ENV_TYPE_FLOAT,
    KC_ENV_TYPE_STR
} kc_env_type_t;

typedef struct {
    const char *env_var;
    size_t offset;
    kc_env_type_t type;
} kc_env_map_t;

static const kc_env_map_t env_config_table[] = {
    { "KC_GRD_WIDTH",   offsetof(kc_grd_options_t, width),   KC_ENV_TYPE_INT },
    { "KC_GRD_HEIGHT",  offsetof(kc_grd_options_t, height),  KC_ENV_TYPE_INT },
    { "KC_GRD_KIND",    offsetof(kc_grd_options_t, kind),    KC_ENV_TYPE_STR },
    { "KC_GRD_WEIGHTS", offsetof(kc_grd_options_t, weights), KC_ENV_TYPE_STR },
    { "KC_GRD_GAP",     offsetof(kc_grd_options_t, gap),     KC_ENV_TYPE_INT },
    { "KC_GRD_MIN_PX",  offsetof(kc_grd_options_t, min_px),  KC_ENV_TYPE_INT }
};
static const int env_config_table_n = sizeof(env_config_table) / sizeof(env_config_table[0]);

/**
 * Create an options struct initialized with default values.
 * @param none Unused.
 * @return Default-initialized options.
 */
kc_grd_options_t kc_grd_options_default(void) {
    kc_grd_options_t opts;
    memset(&opts, 0, sizeof(opts));
    return opts;
}

/**
 * Load configuration from environment variables.
 * @param opts Options to update.
 * @return None.
 */
void kc_grd_options_load_env(kc_grd_options_t *opts) {
    int i;
    if (!opts) return;
    for (i = 0; i < env_config_table_n; i++) {
        const char *val = getenv(env_config_table[i].env_var);
        char *end;
        if (!val) continue;
        switch (env_config_table[i].type) {
            case KC_ENV_TYPE_INT: {
                long v = strtol(val, &end, 10);
                if (end != val && *end == '\0') {
                    *(int *)((char *)opts + env_config_table[i].offset) = (int)v;
                }
                break;
            }
            case KC_ENV_TYPE_FLOAT: {
                float v = strtof(val, &end);
                if (end != val && *end == '\0') {
                    *(float *)((char *)opts + env_config_table[i].offset) = v;
                }
                break;
            }
            case KC_ENV_TYPE_STR: {
                char **p = (char **)((char *)opts + env_config_table[i].offset);
                free(*p);
                *p = strdup(val);
                break;
            }
        }
    }
}

/**
 * Free dynamically allocated resources within an options struct.
 * @param opts Options to clean up.
 * @return None.
 */
void kc_grd_options_free(kc_grd_options_t *opts) {
    if (!opts) return;
    free(opts->kind);
    opts->kind = NULL;
    free(opts->weights);
    opts->weights = NULL;
}

/**
 * Requests a context to stop at the next opportunity.
 * @param ctx GRD context or NULL (no-op).
 * @return void
 */
void kc_grd_stop(kc_grd_box_t *ctx) {
    if (ctx) ctx->stop_requested = 1;
}

#ifndef KC_GRD_BUILD_VERSION
#define KC_GRD_BUILD_VERSION 0
#endif

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_grd_version(void) {
    return (uint64_t)KC_GRD_BUILD_VERSION;
}
