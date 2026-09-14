# mmap.c - Memory map

`mmap.c` is a minimal portable C library and CLI for creating a binary file from stdin and later exposing that file as readonly memory through mmap.

---

## CLI

Command line interface for the mmap tool.

### Examples

Read standard input and write to file:

```bash
echo -n "example input" | ./bin/x86_64/linux/mmap set file.bin
```

Map file and print to standard output:

```bash
./bin/x86_64/linux/mmap get file.bin
```

---

### Parameters

| Command/Flag | Description |
| :--- | :--- |
| `set` | Read stdin, replace file with exact bytes |
| `get` | Map file, write exact bytes to stdout |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

---

## Public API

```c
#include "libmmap.h"

kc_mmap_t map = {0};

if (kc_mmap_open(&map, "file.bin") == KC_MMAP_OK) {
    const void *data = kc_mmap_data(&map);
    size_t size = kc_mmap_size(&map);

    // ...
}

kc_mmap_close(&map);
```

---

## Lifecycle

- `kc_mmap_open()` - initialize a new mmap context and map a file.
- `kc_mmap_data()` - get pointer to mapped data.
- `kc_mmap_size()` - get mapped data size.
- `kc_mmap_close()` - release a mmap context. Safe on a zero-initialized object.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make clean && make
```

### Tests

The portable test entry point is `make test`. Build project artifacts first, then run tests. Tests compile only test executables, link dynamically against the generated shared library, and run through CTest.

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

Build targets such as `make x86_64/windows` compile project artifacts. Tests are run only through `make test` or `make test wine`.

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
- `libpthread`
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
