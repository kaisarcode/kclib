# libdemo.c - Minimal C Library Example

`libdemo.c` is the reference kclib project. It demonstrates the standard
library-only structure with one small reusable capability, portable tests, and a
WebAssembly build.

## Public API

```c
#include "libdemo.h"

char *greeting = kc_demo_greet("John");

if (greeting != NULL) {
    /* "Hello John!" */
    kc_demo_free(greeting);
}
```

The public API contains three functions:

```c
char *kc_demo_greet(const char *name);
void kc_demo_free(void *ptr);
uint64_t kc_demo_version(void);
```

`kc_demo_greet()` returns `Hello <name>!`.

Use `kc_demo_free()` to release the returned string.

`kc_demo_version()` returns the library build version.

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host
architecture running the build.

```bash
make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first,
then run tests.

```bash
make
make test
```

The test suite validates the reusable public API. To run the Windows library
tests through Wine:

```bash
make x86_64/windows
make test wine
```

### WebAssembly (Emscripten)

```bash
make wasm32/wasm
make test wasm
```

- Artifact: `bin/wasm32/wasm/libdemo.wasm`
- Exports: `kc_demo_greet`, `kc_demo_free`, `kc_demo_version`

`wasm32/wasm` is included in `make all`.

### Multiarch builds

A plain `make` builds only the current host architecture. `make all` builds
all configured targets.

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

## Development requirements

### Build tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` (C11 compatible)

### Optional cross-compilation SDKs

Required only for the corresponding targets:

- MinGW for Windows cross-compilation.
- `wine` for Windows tests on Linux.
- `osxcross` with Apple SDKs for macOS and iOS.
- Android NDK for Android targets.
- Emscripten SDK and Node.js for WebAssembly builds and tests.

## Beta notice

This is a beta project tested primarily on Debian x86_64. No guarantees are
provided regarding stability or future support. You are free to test it, use it,
and modify it.

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
