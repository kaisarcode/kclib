/**
 * grd.h - Grid | Region | Divide
 * Summary: Public API for the passive grid layout engine.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_GRD_H
#define KC_GRD_H

#include <stdint.h>
#include <signal.h>

#define KC_GRD_ESTOP (-3)

#define KC_GRD_WEIGHTS_CAP 256

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_grd_box kc_grd_box_t;
typedef struct kc_grd_split kc_grd_split_t;

typedef enum {
    KC_GRD_ROW = 1,
    KC_GRD_COL = 2
} kc_grd_kind_t;

/**
 * A gap separator between two adjacent children in a split.
 * @param x Left edge of the gap strip in pixels.
 * @param y Top edge of the gap strip in pixels.
 * @param w Width of the gap strip in pixels.
 * @param h Height of the gap strip in pixels.
 * @param index Index of the left/top child adjacent to this gap.
 * @param split Split that owns this gap.
 */
typedef struct {
    int x, y, w, h;
    int index;
    kc_grd_split_t *split;
} kc_grd_gap_t;

struct kc_grd_split {
    kc_grd_box_t *owner;
    kc_grd_kind_t kind;
    kc_grd_box_t **children;
    float *weights;
    int count;
    int cap;
    int gap;
    int min_px;

    int drag_on;
    int drag_index;
    int drag_anchor;
    int drag_px_a;
    int drag_px_b;
    float drag_a;
    float drag_b;
};

struct kc_grd_box {
    kc_grd_box_t *parent;
    kc_grd_split_t *split;

    int x, y, w, h;
    int inner_x, inner_y, inner_w, inner_h;

    int border;
    int padding;

    volatile sig_atomic_t stop_requested;
};

/**
 * Allocates and initializes a new box with border=1 and padding=1.
 * The caller owns the returned box until it is transferred to a split
 * via kc_grd_split_add, after which the split owns it.
 * @return Box pointer or NULL on allocation failure.
 */
typedef struct kc_grd_options {
    int width;
    int height;
    char *kind;
    char *weights;
    int gap;
    int min_px;
} kc_grd_options_t;

kc_grd_options_t kc_grd_options_default(void);
void kc_grd_options_load_env(kc_grd_options_t *opts);
void kc_grd_options_free(kc_grd_options_t *opts);

void kc_grd_stop(kc_grd_box_t *ctx);

uint64_t kc_grd_version(void);

kc_grd_box_t *kc_grd_box_new(void);

/**
 * Releases a box and all owned splits and children recursively.
 * Must not be called on a box that is owned by a split.
 * The caller must ensure no concurrent operations on this tree.
 * @param b Box pointer.
 * @return void
 */
void kc_grd_box_free(kc_grd_box_t *b);

/**
 * Attaches a new split to a box, replacing and freeing any existing split.
 * @param b Box to split.
 * @param kind Split direction.
 * @return Split pointer or NULL on failure.
 */
kc_grd_split_t *kc_grd_split_set(kc_grd_box_t *b, kc_grd_kind_t kind);

/**
 * Adds one child box to a split with the given proportional weight.
 * Ownership of child transfers to the split on success.
 * @param s Split pointer.
 * @param child Child box to add.
 * @param weight Proportional weight (clamped to 1.0 if at most 0).
 * @return 0 on success, or -1 on failure.
 */
int kc_grd_split_add(kc_grd_split_t *s, kc_grd_box_t *child, float weight);

/**
 * Updates the proportional weight of one child in a split.
 * @param s Split pointer.
 * @param index Child index.
 * @param weight New proportional weight (clamped to 1.0 if at most 0).
 * @return 0 on success, or -1 on invalid input.
 */
int kc_grd_split_weight(kc_grd_split_t *s, int index, float weight);

/**
 * Sets the outer bounds of a box and recomputes its inner area.
 * @param b Box pointer.
 * @param x Left edge in pixels.
 * @param y Top edge in pixels.
 * @param w Width in pixels.
 * @param h Height in pixels.
 * @return void
 */
void kc_grd_box_bounds(kc_grd_box_t *b, int x, int y, int w, int h);

/**
 * Recursively recomputes the layout of a box and all its children.
 * Must not be called concurrently with any mutation on the same tree.
 * @param b Box pointer.
 * @return void
 */
void kc_grd_box_layout(kc_grd_box_t *b);

/**
 * Configures the gap and minimum child size for a split.
 * @param s Split pointer.
 * @param gap Gap between children in pixels.
 * @param min_px Minimum child size in pixels.
 * @return void
 */
void kc_grd_split_gap(kc_grd_split_t *s, int gap, int min_px);

/**
 * Tests whether a point falls on a gap separator, searching recursively.
 * @param b Root box to search from.
 * @param x X coordinate to test.
 * @param y Y coordinate to test.
 * @param out Destination gap descriptor filled on hit.
 * @return 1 on hit, or 0 on miss.
 */
int kc_grd_gap_hit(kc_grd_box_t *b, int x, int y, kc_grd_gap_t *out);

/**
 * Begins a drag-resize operation on a gap.
 * Records the anchor position and the initial pixel sizes of the two
 * adjacent children. Only one drag may be active per split at a time.
 * @param gap Gap descriptor from kc_grd_gap_hit.
 * @param x Pointer X position at drag start.
 * @param y Pointer Y position at drag start.
 * @return 0 on success, or -1 on failure.
 */
int kc_grd_drag_begin(const kc_grd_gap_t *gap, int x, int y);

/**
 * Updates proportional weights during an active drag-resize operation.
 * Returns 0 without modifying weights when a min_px constraint would
 * be violated.
 * @param s Split owning the active drag.
 * @param x Current pointer X position.
 * @param y Current pointer Y position.
 * @return 0 on success, or -1 on failure.
 */
int kc_grd_drag_update(kc_grd_split_t *s, int x, int y);

/**
 * Ends an active drag-resize operation and resets drag state.
 * @param s Split owning the active drag.
 * @return void
 */
void kc_grd_drag_end(kc_grd_split_t *s);

/**
 * Removes a box from its parent split and restructures the tree.
 * When only one child remains after removal, that child is promoted
 * into its parent and the split is dissolved.
 * The caller must not use b after a successful call.
 * @param b Box to remove. Must have a parent.
 * @return 0 on success, or -1 on failure.
 */
int kc_grd_box_close(kc_grd_box_t *b);

/**
 * Returns the child box at the given index in a split.
 * @param s Split pointer.
 * @param i Child index.
 * @return Child pointer or NULL on invalid input.
 */
kc_grd_box_t *kc_grd_split_at(const kc_grd_split_t *s, int i);

#ifdef __cplusplus
}
#endif

#endif
