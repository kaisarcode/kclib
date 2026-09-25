# b64.c - Base64 Encode and Decode

`b64.c` is a small C library and CLI for base64 encoding and decoding. It handles RFC 4648 base64 encoding of binary data and strict decoding of canonical base64 strings.

---
## CLI

### Examples

Encode stdin to base64:

```bash
echo "hello" | b64 --encode
```

Encode text passed as an argument:

```bash
b64 --encode "Hello"
```

Decode base64 to binary:

```bash
echo "aGVsbG8=" | b64 --decode
```

Decode text passed as an argument:

```bash
b64 --decode "SGVsbG8K"
```

When supplied, the text argument takes precedence over stdin.

---

### Commands

| Command | Alias | Description |
| :--- | :--- | :--- |
| `encode` | `-e`, `--encode` | Encode an optional text argument, or stdin, to base64 |
| `decode` | `-d`, `--decode` | Decode an optional text argument, or stdin, to binary |
| `-h`, `--help` | | Show help and usage |
| `-v`, `--version` | | Show version |

---
## Public API

```c
#include "libb64.h"

// Encode binary data
char *encoded = kc_b64_encode(data, data_size);
// Use encoded string
kc_b64_free(encoded);

// Decode base64 string
size_t decoded_size;
void *decoded = kc_b64_decode(encoded_str, &decoded_size);
// Use decoded binary data
kc_b64_free(decoded);
```

---

### Lifecycle

- `kc_b64_encode()` returns caller-owned encoded output that must be released with `kc_b64_free()`.
- `kc_b64_decode()` accepts strict RFC 4648 input with canonical padding, returns caller-owned decoded output, and resets `out_size` to `0` on failure when the size pointer is valid. Empty input is valid and returns an owned zero-length result.
- `kc_b64_free()` releases memory returned by the b64 library.

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

- Artifact: `bin/wasm32/wasm/b64.wasm`
- Exports: `kc_b64_version`, `kc_b64_encode`, `kc_b64_decode`, `kc_b64_free`
- The module contains the reusable library only; the CLI is not compiled into
    it.

`wasm32/wasm` is included in `make all`.

### Multiarch Builds

A plain `make` builds only the current host architecture. `make all` builds
all configured targets.

```bash
make all
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
- Emscripten SDK and Node.js for WebAssembly builds and tests.
- Other cross-compilation toolchains are required only by enabled targets.

### System Libraries

Linux:
- No additional system libraries required.

Windows (MSVC or MinGW):
- No additional system libraries required.

macOS / iOS:
- No additional system libraries required.

### Test Dependencies

- `ctest` (included with cmake)

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
