#ifndef KC_GRD_H
#define KC_GRD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_grd_grid kc_grd_grid_t;
typedef struct kc_grd_region kc_grd_region_t;
typedef struct kc_grd_box kc_grd_box_t;
typedef struct kc_grd_separator kc_grd_separator_t;
typedef struct kc_grd_content kc_grd_content_t;

typedef enum {
    KC_GRD_X = 1,
    KC_GRD_Y = 2
} kc_grd_direction_t;

uint64_t kc_grd_version(void);

int kc_grd_grid_open(
    kc_grd_grid_t **out,
    kc_grd_direction_t direction
);

void kc_grd_grid_close(
    kc_grd_grid_t *grid
);

kc_grd_region_t *kc_grd_grid_get_region(
    const kc_grd_grid_t *grid
);

kc_grd_direction_t kc_grd_region_get_direction(
    const kc_grd_region_t *region
);

int kc_grd_region_set_direction(
    kc_grd_region_t *region,
    kc_grd_direction_t direction
);

size_t kc_grd_region_get_box_count(
    const kc_grd_region_t *region
);

kc_grd_box_t *kc_grd_region_get_box(
    const kc_grd_region_t *region,
    size_t index
);

size_t kc_grd_region_get_separator_count(
    const kc_grd_region_t *region
);

kc_grd_separator_t *kc_grd_region_get_separator(
    const kc_grd_region_t *region,
    size_t index
);

kc_grd_box_t *kc_grd_region_insert_box(
    kc_grd_region_t *region,
    size_t index,
    float weight,
    float separator_size
);

float kc_grd_box_get_weight(
    const kc_grd_box_t *box
);

int kc_grd_box_set_weight(
    kc_grd_box_t *box,
    float weight
);

kc_grd_region_t *kc_grd_box_get_region(
    const kc_grd_box_t *box
);

kc_grd_content_t *kc_grd_box_get_content(
    const kc_grd_box_t *box
);

kc_grd_region_t *kc_grd_box_subdivide(
    kc_grd_box_t *box,
    kc_grd_direction_t direction
);

int kc_grd_box_remove(
    kc_grd_box_t *box
);

float kc_grd_separator_get_size(
    const kc_grd_separator_t *separator
);

int kc_grd_separator_set_size(
    kc_grd_separator_t *separator,
    float size
);

kc_grd_box_t *kc_grd_separator_get_before(
    const kc_grd_separator_t *separator
);

kc_grd_box_t *kc_grd_separator_get_after(
    const kc_grd_separator_t *separator
);

kc_grd_box_t *kc_grd_content_get_box(
    const kc_grd_content_t *content
);

#ifdef __cplusplus
}
#endif

#endif