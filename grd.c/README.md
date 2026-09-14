# grd.c - Proportional Grid Layout Engine

`grd.c` is a minimalist C library and CLI for computing proportional grid splits. It provides a passive layout engine for hierarchical boxes and splits, designed as a composable native primitive for the KaisarCode ecosystem.

---

## CLI

`grd split` computes one proportional split for a root area. It remains the
simple interface when no nested boxes are needed.

### Examples

Split a 1920x1080 area into three rows with 1:2:1 proportions:

```bash
./bin/x86_64/linux/grd split -w 1920 -H 1080 -k row -W "1 2 1"
```

Split an 800x600 area into four equal columns with a 4px gap:

```bash
./bin/x86_64/linux/grd split -w 800 -H 600 -k col -W "1 1 1 1" -g 4
```

---

### Parameters

| Flag | Description |
| :--- | :--- |
| `-w <n>` | Root width in pixels (required) |
| `-H <n>` | Root height in pixels (required) |
| `-k row\|col` | Split direction (default: row) |
| `-W <weights>` | Space or comma separated weights (required) |
| `-g <n>` | Gap between children in pixels (default: 0) |
| `-m <n>` | Minimum child size in pixels (default: 1) |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

---

### Output

One line per child box, in the format `index x y w h`:

```
0 0 0 1920 270
1 0 270 1920 540
2 0 810 1920 270
```

### Hierarchical layouts

`grd layout` reads a hierarchy from standard input, builds it through
`libgrd`, calculates it once from the root, and prints the root plus every box
in depth-first preorder.

```bash
cat <<'EOF' | ./bin/x86_64/linux/grd layout -x 0 -y 0 -w 120 -H 80
. row 1,2
0 col 1,1
1 row 1,2,1 4 10
EOF
```

Each input line is:

```text
<path> <row|col> <weights> [gap] [min]
```

- `.` is the root; `0`, `1`, and `0.1` select children by their zero-based index.
- `weights` is a comma-separated list with at least two positive numbers.
- `gap` and `min` are optional and otherwise use the `libgrd` split defaults.
- Definitions must be parent-before-child: a path must already exist when its line is read.
- `-x` and `-y` set the root origin (default `0`); `-w`/`--width` and `-H`/`--height` set its required positive dimensions.

The example produces parseable rows in the form `path x y w h`:

```text
. 0 0 120 80
0 0 0 40 80
0.0 0 0 40 40
0.1 0 41 40 39
1 41 0 79 80
1.0 41 0 18 80
1.1 63 0 36 80
1.2 103 0 17 80
```

Malformed lines, unknown paths, duplicate splits, invalid split kinds, and
invalid numeric values fail with a diagnostic on standard error. No descriptive
text is written to standard output.

---

## Public API

```c
#include "libgrd.h"

kc_grd_box_t *root = kc_grd_box_new();
kc_grd_split_t *s = kc_grd_split_set(root, KC_GRD_ROW);

kc_grd_split_add(s, kc_grd_box_new(), 1.0f);
kc_grd_split_add(s, kc_grd_box_new(), 2.0f);

kc_grd_box_bounds(root, 0, 0, 1920, 1080);
kc_grd_box_layout(root);

kc_grd_box_free(root);
```

---

## Lifecycle

- `kc_grd_box_new()` - allocates a new box. The caller owns the box until it is added to a split.
- `kc_grd_split_set()` - attaches a split engine to a box.
- `kc_grd_split_add()` - adds a child box to a split. Ownership of the child transfers to the parent box.
- `kc_grd_box_free()` - recursively releases a box and all its descendants.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first, then run tests. Tests compile the test executable, link dynamically against the generated shared library, and run directly.

```bash
make
make test
```

To run the common `test` target in Windows-through-Wine mode:

```bash
make x86_64/windows
make test wine
```

The portable C test source is `src/test.c`. Test binaries and runtime outputs are build artifacts and are not stored in the project tree.

Build targets such as `make x86_64/windows` compile project artifacts. Tests are run only through `make test`, `make test wine`, or `make test wasm`.

### WebAssembly (Emscripten)

The `wasm32/wasm` target builds the reusable grid-layout library as a WebAssembly module using the Emscripten CMake toolchain:

```bash
make wasm32/wasm
```

- Artifact: `bin/wasm32/wasm/grd.wasm`
- Test: `make test wasm`
- Requirement: Emscripten SDK/toolchain with `emcmake`, `emcc`, and Node.js on `PATH` (for example, `source emsdk_env.sh`).
- The module exports the public `kc_grd_*` API with its existing signatures, ownership, lifecycle, and status codes. It contains the reusable library capability, not the `grd` CLI: `src/grd.c` is not compiled into the module.

`make test wasm` compiles `src/test.c` with Emscripten and runs the same public-contract tests under Node.js. It requires `bin/wasm32/wasm/grd.wasm` and reports how to build it when it is absent.

`wasm32/wasm` is included in `make all`.

### Multiarch Builds

The project is prepared to build artifacts for multiple architectures under `bin/{arch}/{platform}/`. A plain `make` builds only the current host architecture.

```bash
make all
make x86_64/linux
make x86_64/windows
make x86_64/macos
make x86_64/iossim
make i686/linux
make i686/windows
make aarch64/linux
make aarch64/android
make aarch64/macos
make aarch64/ios
make aarch64/iossim
make armv7/linux
make armv7/android
make armv7hf/linux
make riscv64/linux
make powerpc64le/linux
make mips/linux
make mipsel/linux
make mips64el/linux
make s390x/linux
make loongarch64/linux
```

---

## Development Requirements

### Build Tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` (C11 compatible)

### System Libraries

Linux:
- `libm`

Windows (MSVC or MinGW):
- No additional system libraries required.

macOS / iOS:
- No additional system libraries required.

### Optional Cross-Compilation SDKs

Required only for multiarch builds:

- MinGW (`x86_64-w64-mingw32-gcc`) for Windows cross-compilation from Linux.
- `wine` for running Windows tests on Linux.
- `osxcross` with macOS and iOS SDKs for macOS and iOS targets.
- Android NDK (version 27.2.12479018) for Android targets.
- Emscripten SDK for `wasm32/wasm` builds and `make test wasm`.

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a personal need for these libraries, but no guarantees are provided regarding its stability or future support. You are free to test it, use it, and modify it as you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com. Please note that I do not accept pull requests; the goal is to avoid long-term dependency on platforms like GitHub, and I do not maintain fixed infrastructure to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
