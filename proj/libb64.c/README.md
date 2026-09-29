# libb64.c - Base64 Encode and Decode

`libb64.c` provides RFC 4648 Base64 encoding for binary data and strict
decoding of canonical Base64 strings.

## Public API

```c
#include "libb64.h"

char *encoded = kc_b64_encode(data, data_size);
if (encoded != NULL) {
    kc_b64_free(encoded);
}

size_t decoded_size = 0;
void *decoded = kc_b64_decode(encoded_str, &decoded_size);
if (decoded != NULL) {
    kc_b64_free(decoded);
}
```

The public API is:

```c
uint64_t kc_b64_version(void);
char *kc_b64_encode(const void *data, size_t data_size);
void *kc_b64_decode(const char *str, size_t *out_size);
void kc_b64_free(void *ptr);
```

`kc_b64_encode()` accepts binary input and returns an allocated NUL-terminated
Base64 string.

`kc_b64_decode()` accepts strict RFC 4648 Base64 with canonical padding.
Empty input is valid. The decoded size is returned through `out_size`.

Use `kc_b64_free()` for values returned by the library.

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/`.

```bash
make
```

### Tests

Build the library first, then run the portable public-API tests:

```bash
make
make test
```

To run the Windows library tests through Wine:

```bash
make x86_64/windows
make test wine
```

### WebAssembly (Emscripten)

```bash
make wasm32/wasm
make test wasm
```

- Artifact: `bin/wasm32/wasm/libb64.wasm`
- Exports: `kc_b64_version`, `kc_b64_encode`, `kc_b64_decode`,
  `kc_b64_free`

`wasm32/wasm` is included in `make all`.

### Multiarch builds

A plain `make` builds only the current host architecture. `make all` builds
all configured targets.

```bash
make all
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
- Emscripten SDK and Node.js for WebAssembly builds and tests.
- Other cross-compilation toolchains required by enabled targets.

No additional system library is required by the Base64 implementation.

## Beta notice

This is a beta project tested primarily on Debian x86_64. No guarantees are
provided regarding stability or future support. You are free to test it, use it,
and modify it.

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
