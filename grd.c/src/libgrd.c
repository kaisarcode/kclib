/**
 * libgrd.c - Grid relations.
 * Summary: Implements the passive hierarchical relation model.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#include "libgrd.h"

#include <math.h>
#include <stdlib.h>

struct kc_grd_content {
    kc_grd_box_t *box;
};

struct kc_grd_separator {
    kc_grd_region_t *region;
    float size;
};

struct kc_grd_box {
    kc_grd_region_t *region;
    float weight;
    kc_grd_content_t *content;
    kc_grd_region_t *child_region;
};

struct kc_grd_region {
    kc_grd_direction_t direction;
    kc_grd_box_t **boxes;
    size_t box_count;
    size_t box_capacity;
    kc_grd_separator_t **separators;
    size_t separator_capacity;
    kc_grd_box_t *parent_box;
};

struct kc_grd_grid {
    kc_grd_region_t *region;
};

/**
 * Checks whether a Region direction is valid.
 * @return Nonzero when valid.
 */
static int valid_direction(kc_grd_direction_t direction) {
    return direction == KC_GRD_X || direction == KC_GRD_Y;
}

/**
 * Normalizes a relational numeric value.
 * @return Normalized value.
 */
static float normalize(float value) {
    return isfinite(value) && value > 0.0f ? value : 0.0f;
}

/**
 * Grows an object pointer collection.
 * @return Zero on success.
 */
static int grow(void **items, size_t *capacity, size_t count, size_t item_size) {
    size_t next;
    void *replacement;

    if (count < *capacity) return 0;
    next = *capacity ? *capacity * 2 : 4;
    if (next <= *capacity || next > SIZE_MAX / item_size) return -1;
    replacement = realloc(*items, next * item_size);
    if (!replacement) return -1;
    *items = replacement;
    *capacity = next;
    return 0;
}

/**
 * Creates Content owned by a Box.
 * @return New Content or NULL.
 */
static kc_grd_content_t *content_create(kc_grd_box_t *box) {
    kc_grd_content_t *content = malloc(sizeof(*content));
    if (content) content->box = box;
    return content;
}

/**
 * Creates a terminal Box in a Region.
 * @return New Box or NULL.
 */
static kc_grd_box_t *box_create(kc_grd_region_t *region, float weight) {
    kc_grd_box_t *box = calloc(1, sizeof(*box));
    if (!box) return NULL;
    box->region = region;
    box->weight = normalize(weight);
    box->content = content_create(box);
    if (!box->content) {
        free(box);
        return NULL;
    }
    return box;
}

/**
 * Creates a Separator in a Region.
 * @return New Separator or NULL.
 */
static kc_grd_separator_t *separator_create(kc_grd_region_t *region, float size) {
    kc_grd_separator_t *separator = malloc(sizeof(*separator));
    if (!separator) return NULL;
    separator->region = region;
    separator->size = normalize(size);
    return separator;
}

/**
 * Creates a Region for a Grid or Box.
 * @return New Region or NULL.
 */
static kc_grd_region_t *region_create(kc_grd_box_t *parent_box,
    kc_grd_direction_t direction) {
    kc_grd_region_t *region = calloc(1, sizeof(*region));
    if (region) {
        region->parent_box = parent_box;
        region->direction = direction;
    }
    return region;
}

/**
 * Destroys an owned Region tree.
 * @return void
 */
static void region_destroy(kc_grd_region_t *region) {
    size_t index;

    if (!region) return;
    for (index = 0; index < region->box_count; ++index) {
        kc_grd_box_t *box = region->boxes[index];
        free(box->content);
        region_destroy(box->child_region);
        free(box);
    }
    for (index = 0; index + 1 < region->box_count; ++index) {
        free(region->separators[index]);
    }
    free(region->boxes);
    free(region->separators);
    free(region);
}

/**
 * Returns the generated build version.
 * @return Build version.
 */
uint64_t kc_grd_version(void) {
#ifndef KC_GRD_BUILD_VERSION
#define KC_GRD_BUILD_VERSION 0
#endif
    return (uint64_t)KC_GRD_BUILD_VERSION;
}

/**
 * Opens a Grid.
 * @return Zero on success.
 */
int kc_grd_grid_open(kc_grd_grid_t **out, kc_grd_direction_t direction) {
    kc_grd_grid_t *grid;
    kc_grd_region_t *region;
    kc_grd_box_t *box;

    if (!out) return -1;
    *out = NULL;
    if (!valid_direction(direction)) return -1;
    grid = calloc(1, sizeof(*grid));
    region = grid ? region_create(NULL, direction) : NULL;
    box = region ? box_create(region, 1.0f) : NULL;
    if (!grid || !region || !box || grow((void **)&region->boxes,
        &region->box_capacity, 0, sizeof(*region->boxes))) {
        free(box ? box->content : NULL);
        free(box);
        region_destroy(region);
        free(grid);
        return -1;
    }
    region->boxes[0] = box;
    region->box_count = 1;
    grid->region = region;
    *out = grid;
    return 0;
}

/**
 * Closes a Grid.
 * @return void
 */
void kc_grd_grid_close(kc_grd_grid_t *grid) {
    if (!grid) return;
    region_destroy(grid->region);
    free(grid);
}

/**
 * Returns the root Region.
 * @return Borrowed Region or NULL.
 */
kc_grd_region_t *kc_grd_grid_get_region(const kc_grd_grid_t *grid) {
    return grid ? grid->region : NULL;
}

/**
 * Returns a Region direction.
 * @return Direction or zero.
 */
kc_grd_direction_t kc_grd_region_get_direction(const kc_grd_region_t *region) {
    return region ? region->direction : 0;
}

/**
 * Changes a Region direction.
 * @return Zero on success.
 */
int kc_grd_region_set_direction(kc_grd_region_t *region,
    kc_grd_direction_t direction) {
    if (!region || !valid_direction(direction)) return -1;
    region->direction = direction;
    return 0;
}

/**
 * Returns a Region Box count.
 * @return Box count.
 */
size_t kc_grd_region_get_box_count(const kc_grd_region_t *region) {
    return region ? region->box_count : 0;
}

/**
 * Returns a Box by Region index.
 * @return Borrowed Box or NULL.
 */
kc_grd_box_t *kc_grd_region_get_box(const kc_grd_region_t *region, size_t index) {
    return region && index < region->box_count ? region->boxes[index] : NULL;
}

/**
 * Returns a Region Separator count.
 * @return Separator count.
 */
size_t kc_grd_region_get_separator_count(const kc_grd_region_t *region) {
    return region && region->box_count ? region->box_count - 1 : 0;
}

/**
 * Returns a Separator by Region index.
 * @return Borrowed Separator or NULL.
 */
kc_grd_separator_t *kc_grd_region_get_separator(const kc_grd_region_t *region,
    size_t index) {
    return region && index + 1 < region->box_count ? region->separators[index] : NULL;
}

/**
 * Inserts a terminal Box into a Region.
 * @return New Box or NULL.
 */
kc_grd_box_t *kc_grd_region_insert_box(kc_grd_region_t *region, size_t index,
    float weight, float separator_size) {
    kc_grd_box_t *box;
    kc_grd_separator_t *separator;
    size_t separator_index;

    if (!region || index > region->box_count) return NULL;
    box = box_create(region, weight);
    separator = box ? separator_create(region, separator_size) : NULL;
    if (!box || !separator || grow((void **)&region->boxes, &region->box_capacity,
        region->box_count, sizeof(*region->boxes)) ||
        grow((void **)&region->separators, &region->separator_capacity,
        region->box_count - 1, sizeof(*region->separators))) {
        free(separator);
        free(box ? box->content : NULL);
        free(box);
        return NULL;
    }
    for (size_t i = region->box_count; i > index; --i) region->boxes[i] = region->boxes[i - 1];
    region->boxes[index] = box;
    separator_index = index == region->box_count ? region->box_count - 1 : index;
    for (size_t i = region->box_count - 1; i > separator_index; --i) {
        region->separators[i] = region->separators[i - 1];
    }
    region->separators[separator_index] = separator;
    ++region->box_count;
    return box;
}

/**
 * Returns a Box weight.
 * @return Stored weight.
 */
float kc_grd_box_get_weight(const kc_grd_box_t *box) {
    return box ? box->weight : 0.0f;
}

/**
 * Changes a Box weight.
 * @return Zero on success.
 */
int kc_grd_box_set_weight(kc_grd_box_t *box, float weight) {
    if (!box) return -1;
    box->weight = normalize(weight);
    return 0;
}

/**
 * Returns a nested Region.
 * @return Borrowed Region or NULL.
 */
kc_grd_region_t *kc_grd_box_get_region(const kc_grd_box_t *box) {
    return box ? box->child_region : NULL;
}

/**
 * Returns terminal Content.
 * @return Borrowed Content or NULL.
 */
kc_grd_content_t *kc_grd_box_get_content(const kc_grd_box_t *box) {
    return box ? box->content : NULL;
}

/**
 * Subdivides a terminal Box.
 * @return New Region or NULL.
 */
kc_grd_region_t *kc_grd_box_subdivide(kc_grd_box_t *box,
    kc_grd_direction_t direction) {
    kc_grd_region_t *region;
    kc_grd_box_t *inner;

    if (!box || !box->content || !valid_direction(direction)) return NULL;
    region = region_create(box, direction);
    inner = region ? box_create(region, 1.0f) : NULL;
    if (!region || !inner || grow((void **)&region->boxes, &region->box_capacity, 0,
        sizeof(*region->boxes))) {
        free(inner ? inner->content : NULL);
        free(inner);
        free(region);
        return NULL;
    }
    free(inner->content);
    inner->content = box->content;
    inner->content->box = inner;
    region->boxes[0] = inner;
    region->box_count = 1;
    box->content = NULL;
    box->child_region = region;
    return region;
}

/**
 * Removes a Box from its Region.
 * @return Zero on success.
 */
int kc_grd_box_remove(kc_grd_box_t *box) {
    kc_grd_region_t *region;
    size_t box_index;
    size_t separator_index;

    if (!box || !(region = box->region) || region->box_count <= 1) return -1;
    for (box_index = 0; box_index < region->box_count; ++box_index) {
        if (region->boxes[box_index] == box) break;
    }
    if (box_index == region->box_count) return -1;
    separator_index = box_index ? box_index - 1 : 0;
    free(box->content);
    region_destroy(box->child_region);
    free(box);
    free(region->separators[separator_index]);
    for (size_t i = box_index; i + 1 < region->box_count; ++i) region->boxes[i] = region->boxes[i + 1];
    for (size_t i = separator_index; i + 1 < region->box_count - 1; ++i) {
        region->separators[i] = region->separators[i + 1];
    }
    --region->box_count;
    return 0;
}

/**
 * Returns a Separator size.
 * @return Stored size.
 */
float kc_grd_separator_get_size(const kc_grd_separator_t *separator) {
    return separator ? separator->size : 0.0f;
}

/**
 * Changes a Separator size.
 * @return Zero on success.
 */
int kc_grd_separator_set_size(kc_grd_separator_t *separator, float size) {
    if (!separator) return -1;
    separator->size = normalize(size);
    return 0;
}

/**
 * Returns the preceding Box.
 * @return Borrowed Box or NULL.
 */
kc_grd_box_t *kc_grd_separator_get_before(const kc_grd_separator_t *separator) {
    if (!separator || !separator->region) return NULL;
    for (size_t i = 0; i + 1 < separator->region->box_count; ++i) {
        if (separator->region->separators[i] == separator) return separator->region->boxes[i];
    }
    return NULL;
}

/**
 * Returns the following Box.
 * @return Borrowed Box or NULL.
 */
kc_grd_box_t *kc_grd_separator_get_after(const kc_grd_separator_t *separator) {
    if (!separator || !separator->region) return NULL;
    for (size_t i = 0; i + 1 < separator->region->box_count; ++i) {
        if (separator->region->separators[i] == separator) return separator->region->boxes[i + 1];
    }
    return NULL;
}

/**
 * Returns the Content owner.
 * @return Borrowed Box or NULL.
 */
kc_grd_box_t *kc_grd_content_get_box(const kc_grd_content_t *content) {
    return content ? content->box : NULL;
}
