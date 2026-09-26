# demo.c - Minimal C Library Example

`demo.c` is a minimal example library showing the standard kclib structure with
one small reusable capability, a CLI consumer, portable tests, and a WebAssembly
build.

---

## CLI

Run without arguments to greet the default name:

```bash
demo
Hello World!
```

Provide a name:

```bash
demo -n John
Hello John!
```

The long form is also available:

```bash
demo --name Jane
Hello Jane!
```

### Parameters

| Flag | Description |
| :--- | :--- |
| `-n`, `--name <name>` | Name to greet (default: `World`) |
| `-h`, `--help` | Show help and usage |
| `-v`, `--version` | Show version |


---

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

---

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

Native and Wine runs validate two reusable public-API cases plus one grouped CLI
case. WebAssembly validates only the two reusable cases; native process helpers
and the CLI case are excluded from the WASM test build at compile time.

To run through Wine:

```bash
make x86_64/windows
make test wine
```

### WebAssembly (Emscripten)

```bash
make wasm32/wasm
make test wasm
```

- Artifact: `bin/wasm32/wasm/demo.wasm`
- Exports: `kc_demo_greet`, `kc_demo_free`, `kc_demo_version`
- The module contains the reusable library only; the CLI is not compiled into it.

`wasm32/wasm` is included in `make all`.

### Multiarch Builds

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

---

## Development Requirements

### Build Tools

- `make` (GNU Make)
- `cmake` >= 3.14
- `ninja`
- `gcc` or `clang` (C11 compatible)

### Optional Cross-Compilation SDKs

Required only for the corresponding targets:

- MinGW for Windows cross-compilation.
- `wine` for Windows tests on Linux.
- `osxcross` with Apple SDKs for macOS and iOS.
- Android NDK for Android targets.
- Emscripten SDK and Node.js for WebAssembly builds and tests.

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a
personal need for these libraries, but no guarantees are provided regarding its
stability or future support. You are free to test it, use it, and modify it as
you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com.
Please note that I do not accept pull requests; the goal is to avoid long-term
dependency on platforms like GitHub, and I do not maintain fixed infrastructure
to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
