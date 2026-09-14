# AGENTS.md

## Project Context

`grd.c` is a small passive geometry library and CLI for recursively dividing
integer rectangles by proportional weights.

It computes boxes, gaps, and drag-adjusted weights. It does not render, own
windows, process pointer events, manage widgets, or provide a UI framework.

Read `README.md` before modifying the project.

## Core Invariants

- Layout is a tree of boxes with at most one split per box.
- `KC_GRD_ROW` divides the x-axis into side-by-side children.
- `KC_GRD_COL` divides the y-axis into stacked children.
- Child sizes are integer pixels derived from positive proportional weights.
- Gaps consume axis space before child distribution.
- Integer remainder is distributed from earlier children forward.
- Border and padding produce one inset equal to their non-negative maximum.
- Adding a child transfers ownership to the parent split.
- Freeing a root recursively frees every owned descendant.
- Gap hit-testing and drag updates remain pure geometry operations.
- Closing a child may dissolve its split and promote the sole survivor.
- No renderer, event loop, platform window, or persistent state is required.

## Geometry Contract

Preserve the axis meaning, coordinate origin, half-open hit tests, gap placement,
rounding order, and exact CLI output `index x y w h`.

Weights at most zero are normalized to `1.0` by the library. Negative bounds,
gap, border, padding, and minimum size are clamped where current functions define
that behavior. The CLI is stricter and accepts only positive weights and root
dimensions.

Layout currently assumes minimum sizes are feasible. If
`child_count * min_px` exceeds usable axis space, the size-reduction loop cannot
satisfy both constraints. Callers must not rely on impossible layouts. A fix
must fail or degrade deterministically rather than loop indefinitely.

Do not silently change rounding, minimum-size priority, or overflow behavior.

## Tree Ownership

Public box and split structures are compatibility boundaries. A child is
caller-owned until `kc_grd_split_add()` succeeds, then owned by the split. A box
owned by a split must not be freed directly.

`kc_grd_split_set()` recursively destroys an existing split before installing a
new one. `kc_grd_box_close()` destroys the target and invalidates its pointer;
when one sibling remains, that sibling's style and split are promoted into the
parent and the sibling pointer is freed.

Changes must preserve parent pointers, split owners, child ordering, recursive
cleanup, and explicit pointer invalidation. Do not add reference counting,
garbage collection, hidden registries, or shared ownership.

## Drag Boundary

Gap hit-testing searches the current laid-out tree and returns a borrowed split
pointer plus adjacent-child index. Drag begin snapshots the adjacent pixel sizes
and weights. Drag update changes only those two weights while preserving their
combined weight and respecting `min_px`. Callers invoke layout afterward.

The library does not capture devices, transform coordinates, debounce input,
draw cursors, or own gesture state beyond one active drag per split.

## Public API and Concurrency

Treat `src/libgrd.h` as a compatibility boundary. Its structures, enums, fields,
ownership rules, defaults, return values, and pointer lifetimes are public.

Trees are mutable and not thread-safe. Layout, mutation, drag, close, and free
must not run concurrently on the same tree. Stop state does not cancel layout;
do not claim asynchronous operation.

## Resource Model

Memory grows with boxes, splits, child arrays, and weights.
Layout allocates one temporary integer-size array per visited split and recurses
through the tree. Deep trees can consume call stack.

Keep allocation failure, integer arithmetic, recursion depth, impossible
constraints, and cleanup explicit. Do not add render caches, retained scenes,
worker threads, remote layout services, or generic object systems.

## Source Layout

Preserve exactly:

- `src/grd.c` for CLI parsing and geometry output;
- `src/libgrd.c` for tree, layout, gap, drag, and reusable behavior;
- `src/libgrd.h` for public structures and API;
- `src/test.c` for all tests, including deep-tree, stress, platform, and
    integration cases.

Do not create additional source, header, layout, geometry, drag, tree, or test
files. Extend only the existing four files.

## Forbidden Default Recommendations

Do not add renderers, widget toolkits, styling systems, scene graphs, animation
engines, window managers, browser layout models, constraint solvers, GPU APIs,
event buses, plugins, telemetry, dashboards, cloud layout services, accounts, or
enterprise UI abstractions.

Do not justify changes through framework parity, hypothetical scale, managed UI
platforms, ecosystem growth, or enterprise readiness.

## Testing

All tests remain in `src/test.c`. Behavioral changes should cover both axes,
integer remainder, gaps, border and padding, zero space, feasible and impossible
minimums, invalid and extreme weights, nested layout, allocation failure, gap
boundaries, drag limits, child removal, sole-child promotion, parent pointers,
ownership, deep trees, and exact CLI rows.

Do not weaken tests to accommodate an implementation change.

## Build and Completion

For documentation-only changes run `kcs .`. For behavior changes use the
repository build and tests without cleaning unless authorized.

A change is complete when geometry, rounding, ownership, drag state, failure
behavior, tests, and documentation agree.

The goal is one small, deterministic rectangle-divider.

## Design Summary

### Purpose

`grd.c` computes nested rectangular layouts from integer bounds, split axes,
gaps, minimum sizes, and proportional weights. It is passive: callers decide
when bounds change, when layout runs, and how resulting boxes are used.

### Architecture

A box owns geometry, style insets, and at most one split.
A split owns an ordered child array, parallel weight array, axis, gap,
minimum-size rule, and temporary drag state.

The four source files have fixed responsibilities:

- `src/grd.c` owns CLI parsing, payload building, and flat split output;
- `src/libgrd.c` owns all tree and geometry behavior;
- `src/libgrd.h` exposes structures and the public contract;
- `src/test.c` contains all tests.

### Axis Model

`KC_GRD_ROW` divides the owner's inner width. Children share y and height while
x advances from left to right.

`KC_GRD_COL` divides the owner's inner height. Children share x and width while
y advances from top to bottom.

Coordinates and dimensions are integer pixels. Negative width and height are
clamped to zero when bounds are assigned.

### Inner Bounds

Each box has outer `x`, `y`, `w`, and `h` plus derived inner bounds. The inset is
the larger of border and padding after negative values are treated as zero.
It is applied once on every side; border and padding are not added together.

Inner width and height never become negative.

### Size Distribution

For a split with `n` children, usable axis space is:

```text
axis_size - (n - 1) * gap
```

Negative usable space becomes zero. Each initial child size is the truncated
floating-point share of usable space. A size below `min_px` is raised to that
minimum.

If total size exceeds usable space, the implementation repeatedly removes one
pixel from later children toward earlier children while remaining above the
minimum. If total size is short, it repeatedly adds one pixel from earlier
children toward later children. The final sizes therefore fill usable space
exactly when the minimum constraint is feasible.

When `n * min_px > usable`, no valid result exists and the current reduction
algorithm cannot converge. This is an implementation limit requiring explicit
validation or a documented degradation rule before accepting such input.

### Recursive Layout

Layout recomputes a box's inner bounds, distributes its immediate children, sets
each child outer bounds, recomputes each child inner bounds, and recursively lays
out descendants.

No geometry is cached beyond public box fields. Mutation does not automatically
trigger layout. Callers explicitly invoke `kc_grd_box_layout()`.

### Ownership

`kc_grd_box_new()` returns caller-owned storage with border and padding set to
one. `kc_grd_split_set()` gives a box one split and destroys any old split and
descendants. `kc_grd_split_add()` transfers a child to the split only on success.

The root owner frees the whole tree recursively. Public pointers into a tree are
borrowed and become invalid when their owning split is replaced, an ancestor is
freed, or close restructures the tree.

### Gap Hit Testing

Each separator occupies a half-open rectangular strip between two adjacent
children. Hit testing checks the current split first, then descendants in child
order. The first match returns its geometry, preceding-child index, and borrowed
split pointer.

Zero-width or zero-height gaps cannot be hit. Hit testing assumes layout fields
are current.

### Drag Resizing

Drag begin records one separator's axis coordinate, adjacent pixel sizes, and
two weights. Drag update applies pointer delta to those pixel sizes. If either
would fall below `min_px`, weights remain unchanged and the operation returns
success.

Otherwise the adjacent pair's total weight is redistributed in proportion to
the proposed pixel sizes. Other children are unchanged. Drag end clears snapshot
state. Updated weights affect geometry only after another layout pass.

### Closing Boxes

A root cannot be closed through `kc_grd_box_close()`. Closing a child frees its
subtree and removes its entry while preserving sibling order.

An empty split is removed. If one child remains, its border, padding, and split
are moved into the parent, descendant parent pointers are repaired, and the
temporary child box is freed. The parent's outer bounds and identity remain.

### CLI Contract

The CLI computes one non-nested split with root border and padding set to zero.
It accepts at most 256 positive weights from one space- or comma-separated
argument and prints one line per child:

```text
index x y w h
```

CLI width and height must be positive; gap and minimum must be non-negative.
Environment options are loaded before command-line overrides.

### Resource and Failure Model

Child arrays and weights grow geometrically. Tree destruction is recursive.
Layout allocates a temporary size array per split; allocation failure leaves that
split's previous child geometry unchanged and returns no error because layout is
void.

The implementation is single-threaded and has no locks, background work,
persistence, platform graphics state, or external dependencies beyond the C
runtime and math support.

### Non-Goals

The project does not render, create windows, process input devices, own an event
loop, style widgets, animate geometry, solve arbitrary constraints, measure text,
manage focus, persist layouts, synchronize trees, expose remote APIs, collect
telemetry, or provide plugins.

These exclusions define the tool rather than an unfinished roadmap.

### Change Criteria

A change must solve a concrete rectangle-layout problem, preserve public tree
ownership, define integer rounding and impossible constraints, maintain both
axis semantics, state pointer invalidation, keep drag behavior explicit, and
avoid absorbing rendering or widget policy.

Changes justified mainly by UI framework parity, generalized scene management,
enterprise platforms, or hypothetical scale should be rejected.

### Core Invariants

The project is defined by public owned box trees, one split per box, x-axis rows,
y-axis columns, weighted integer distribution, explicit gaps and minimums,
manual recursive layout, local drag weight adjustment, and no rendering layer.
