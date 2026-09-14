# libr.c - C Library Blueprint

`libr.c` is a template for creating small, high-performance C libraries and CLI tools. It serves as a blueprint for the composable native primitives in the KaisarCode ecosystem. The CLI reads requests from stdin and can stay resident, processing multiple requests separated by the `--until` delimiter byte.

---

## CLI

Example CLI interface provided by the blueprint.

### Examples

Execute the default operation with a verb:

```bash
./bin/x86_64/linux/libr set "example input"
./bin/x86_64/linux/libr get "example input"
```

Provide a parameter flag:

```bash
./bin/x86_64/linux/libr set "example input" -p "value"
```

Pipe one request through standard input:

```bash
echo "example input" | ./bin/x86_64/linux/libr set
```

Process multiple requests in one resident run using EOT (byte 4):

```bash
printf 'first\004second\004' | ./bin/x86_64/linux/libr set
```

---

### Parameters

| Command/Flag | Description |
| :--- | :--- |
| `set` | Example verb for setting data |
| `get` | Example verb for getting data |
| `-p`, `--param <val>` | Set parameter value |
| `--until N` | Request/response delimiter byte | `4` (EOT) |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

CLI flags override environment variables, which override built-in defaults.

The following environment variables map to the parameters above:

`KC_LIBR_PARAM`, `KC_LIBR_UNTIL`

---

## Public API

```c
#include "liblibr.h"

kc_libr_options_t opts = kc_libr_options_default();
kc_libr_t *ctx = NULL;

if (kc_libr_open(&ctx, &opts) == KC_LIBR_OK) {
    kc_libr_exec(ctx, "example input");
    kc_libr_close(ctx);
}

kc_libr_options_free(&opts);
```

### Runner

```c
char *err = NULL;
char *result = kc_libr_run("{\"cmd\":\"open\",\"args\":{\"until\":4}}", &err);
/* result contains JSON with "handle" field */
free(result);

result = kc_libr_run("{\"cmd\":\"exec\",\"args\":{\"input\":\"hello\"},\"handle\":1}", &err);
free(result);

result = kc_libr_run("{\"cmd\":\"close\",\"handle\":1}", &err);
free(result);
```

---

## Lifecycle

- `kc_libr_open()` - allocates and returns a new context owned by the caller.
- `kc_libr_exec()` - performs the core library operation.
- `kc_libr_close()` - releases the context and all associated resources.

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make clean && make
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

The `wasm32/wasm` target builds the reusable library as a WebAssembly module using the Emscripten CMake toolchain:

```bash
make wasm32/wasm
```

- Artifact: `bin/wasm32/wasm/libr.wasm`
- Test: `make test wasm`
- Requirement: Emscripten SDK/toolchain with `emcmake`, `emcc`, and Node.js on `PATH` (e.g. `source emsdk_env.sh`).
- The module exports the public `kc_libr_*` API with the existing signatures, ownership, lifecycle, and status codes. It represents the reusable library, not the `libr` CLI: `src/libr.c` is not compiled into the module.

`make test wasm` compiles `src/test.c` for Emscripten and runs the same public-contract test cases under Node.js. It requires `bin/wasm32/wasm/libr.wasm` and fails with instructions if it is missing.

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
- Emscripten SDK for `wasm32/wasm` builds and `make test wasm`.

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a personal need for these libraries, but no guarantees are provided regarding its stability or future support. You are free to test it, use it, and modify it as you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com. Please note that I do not accept pull requests; the goal is to avoid long-term dependency on platforms like GitHub, and I do not maintain fixed infrastructure to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
