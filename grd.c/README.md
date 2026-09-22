# grd.c

`grd` is a passive hierarchical 2D relational layout model. It stores a tree of relationships only; it does not calculate coordinates, rectangles, pixel bounds, hit areas, or rendered layouts. There is no `grd` CLI.

## Model

```text
Grid
└── Region
    ├── Box
    │   └── Content | Region
    ├── Separator
    └── Box
```

The Region grammar is `Box (Separator Box)*`. A Region always has at least one Box. Its direction is either `KC_GRD_X` or `KC_GRD_Y`; direction changes do not alter the tree, weights, or separator sizes.

A Box has a relational `weight`. A Separator has an independent logical `size`. Finite positive values are preserved; zero, negative, NaN, and infinities normalize to `0.0f`. These values have no intrinsic pixel interpretation.

`kc_grd_region_insert_box()` creates a terminal Box plus exactly one Separator. Insertion before a Box places the new Separator after the new Box. Appending places it before the new Box. Existing Boxes and Separators retain identity and stored values.

`kc_grd_box_subdivide()` replaces a terminal Box's Content child with a Region containing one inner Box. The original Content object is moved, not recreated, so its identity is retained and its owning Box changes.

`kc_grd_box_remove()` cannot remove a Region's last Box. It destroys the target subtree and removes invalid Separators from left to right. A one-Box Region remains a Region; it is never automatically collapsed.

## Ownership

Open a Grid with `kc_grd_grid_open()` and release the complete tree with `kc_grd_grid_close()`. A new Grid starts as `Grid -> Region -> Box -> Content`.

The Grid owns every reachable Region, Box, Separator, Content, and internal collection. Pointers returned by getters are borrowed. They become invalid when their owning Grid is closed, their owning subtree is removed, or an ancestor is removed. Child objects have no standalone creation or destruction API.

Concurrent mutation of a Grid tree is not supported and must be serialized by the caller. Read-only access is not promised safe while mutation occurs.

## C/C++ API

`src/libgrd.h` is C and C++ compatible. Its complete public API is:

```c
uint64_t kc_grd_version(void);
int kc_grd_grid_open(kc_grd_grid_t **out, kc_grd_direction_t direction);
void kc_grd_grid_close(kc_grd_grid_t *grid);
kc_grd_region_t *kc_grd_grid_get_region(const kc_grd_grid_t *grid);
kc_grd_direction_t kc_grd_region_get_direction(const kc_grd_region_t *region);
int kc_grd_region_set_direction(kc_grd_region_t *region, kc_grd_direction_t direction);
size_t kc_grd_region_get_box_count(const kc_grd_region_t *region);
kc_grd_box_t *kc_grd_region_get_box(const kc_grd_region_t *region, size_t index);
size_t kc_grd_region_get_separator_count(const kc_grd_region_t *region);
kc_grd_separator_t *kc_grd_region_get_separator(const kc_grd_region_t *region, size_t index);
kc_grd_box_t *kc_grd_region_insert_box(kc_grd_region_t *region, size_t index, float weight, float separator_size);
float kc_grd_box_get_weight(const kc_grd_box_t *box);
int kc_grd_box_set_weight(kc_grd_box_t *box, float weight);
kc_grd_region_t *kc_grd_box_get_region(const kc_grd_box_t *box);
kc_grd_content_t *kc_grd_box_get_content(const kc_grd_box_t *box);
kc_grd_region_t *kc_grd_box_subdivide(kc_grd_box_t *box, kc_grd_direction_t direction);
int kc_grd_box_remove(kc_grd_box_t *box);
float kc_grd_separator_get_size(const kc_grd_separator_t *separator);
int kc_grd_separator_set_size(kc_grd_separator_t *separator, float size);
kc_grd_box_t *kc_grd_separator_get_before(const kc_grd_separator_t *separator);
kc_grd_box_t *kc_grd_separator_get_after(const kc_grd_separator_t *separator);
kc_grd_box_t *kc_grd_content_get_box(const kc_grd_content_t *content);
```

All object types are opaque. Status-returning functions use `0` for success and `-1` for failure; pointer-returning functions use `NULL` for invalid input or allocation failure.

## Build And Test

Native builds produce shared and static reusable libraries named `libgrd`:

```bash
make
make test
```

WASM support builds the standalone reusable artifact `bin/wasm32/wasm/grd.wasm`:

```bash
make wasm32/wasm
make test wasm
```

`make test wine` runs the same reusable API contract tests against the Windows library through Wine.

## License

GPLv3. See [LICENSE](LICENSE).
