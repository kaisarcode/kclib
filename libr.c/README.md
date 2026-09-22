# libr.c - Small C Library and CLI Example

`libr.c` is an example/template project showing a small C library and CLI structure. The CLI reads requests from stdin and can stay resident, processing multiple requests framed by the EOT delimiter byte (value 4).

---

## CLI

The `libr` CLI accepts an optional verb, optional positional input, and options. Results are framed on stdout with the EOT delimiter byte (value 4).

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

### Parameters

| Command/Flag | Description |
| :--- | :--- |
| `set` | Example verb for setting data |
| `get` | Example verb for getting data |
| `-p`, `--param <val>` | Set parameter value |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |

### Operating Modes

When positional input is provided, the CLI writes one EOT delimiter byte to stdout and exits.

When no positional input is provided, the CLI reads requests from standard input:

- reads bytes until the EOT delimiter (byte 4) or EOF;
- writes the EOT delimiter byte to stdout after each response;
- flushes stdout after each response;
- ignores empty framed requests;
- processes a final non-empty request that ends at EOF;
- exits when stdin reaches EOF.

When stdout is a terminal, a newline is appended after the EOT delimiter for operator readability. Redirected and piped stdout remains byte-exact.

Unknown or malformed options fail directly with a diagnostic on stderr and exit status 1.

---

## Public API

```c
#include "liblibr.h"
```

The library exposes one function:

```c
uint64_t kc_libr_version(void);
```

`kc_libr_version()` returns the build version generated at compile time (a Unix timestamp).

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

The `wasm32/wasm` target builds the reusable library as a WebAssembly module using the Emscripten CMake toolchain:

```bash
make wasm32/wasm
```

- Artifact: `bin/wasm32/wasm/libr.wasm`
- Test: `make test wasm`
- Requirement: Emscripten SDK/toolchain with `emcmake`, `emcc`, and Node.js on `PATH` (e.g. `source emsdk_env.sh`).
- The module exports the single public entry point `kc_libr_version`. It represents the reusable library, not the `libr` CLI: `src/libr.c` is not compiled into the module.

`make test wasm` validates the reusable library contract under Emscripten/Node.js. The WASM module does not contain the CLI, so CLI tests are not executed in the WASM test run. It requires `bin/wasm32/wasm/libr.wasm` and fails with instructions if it is missing.

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
