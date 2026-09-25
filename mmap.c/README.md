# mmap.c - Persistent Binary Value

`mmap.c` exposes one file-backed binary value per instance. Opening a missing
path is valid and starts with no value. The current value can be replaced in
memory, persisted explicitly, or deleted together with its backing file.

After `del()`, any operation other than final cleanup is an error. Reopening the
same path creates a new valid instance whose `get()` returns `null`.

---

---

## CLI

The CLI is a one-shot adapter over the same file/value operations.

### Examples

Read a saved value:

```bash
mmap file.bin --get
```

Set and save a direct value:

```bash
mmap file.bin --set "value"
```

Set and save exact bytes from stdin:

```bash
cat input.bin | mmap file.bin --set
```

Delete the backing file:

```bash
mmap file.bin --del
```

Short operation flags are also accepted:

```bash
mmap file.bin -get
mmap file.bin -set "value"
mmap file.bin -del
```

### Parameters

| Form | Description |
| :--- | :--- |
| `mmap <path> -get\|--get` | Write the saved value to stdout |
| `mmap <path> -set\|--set [value]` | Set and save a direct value, or read exact bytes from stdin when value is omitted |
| `mmap <path> -del\|--del` | Delete the backing file |
| `mmap -h\|--help` | Show help and usage |
| `mmap -v\|--version` | Show version |

---

---

## Public API

```c
#include "libmmap.h"

kc_mmap_t *mm = NULL;
const void *data = NULL;
size_t size = 0;

if (kc_mmap_open(&mm, "file.bin") == KC_MMAP_OK) {
    int rc = kc_mmap_get(mm, &data, &size);

if (rc == KC_MMAP_NOT_FOUND) {
        /* no current value */
    }

if (kc_mmap_set(mm, "A", 1U) == KC_MMAP_OK) {
        kc_mmap_get(mm, &data, &size);

if (kc_mmap_save(mm) == KC_MMAP_OK) {
            /* file.bin now contains one byte: A */
        }
    }

kc_mmap_close(mm);
}
```

### API semantics

- `kc_mmap_open()` creates one instance associated with a copied file path.
    Missing files are valid and produce an instance with no value.
- `kc_mmap_get()` returns `KC_MMAP_NOT_FOUND` only when the valid instance has
    no current value. On `KC_MMAP_OK`, the returned pointer and size are
    transport details for the current binary value.
- `kc_mmap_set()` replaces the current in-memory value and does not write the
    file. `NULL` with size zero sets the logical value to null. A non-NULL
    pointer with size zero is a real zero-byte value such as `""`.
- `kc_mmap_save()` persists the current value. A zero-byte value creates or
    truncates the backing file to zero bytes. Saving null deletes the backing
    file and invalidates the instance.
- `kc_mmap_del()` is shorthand for setting null and saving it. It therefore
    deletes the backing file and invalidates the instance. Subsequent
    `get/set/save/del` calls fail.
- `kc_mmap_close()` releases the instance and accepts `NULL`.
- `kc_mmap_version()` returns the generated build version.

```text
KC_MMAP_NOT_FOUND      -> null
KC_MMAP_OK + 0 bytes   -> "" / zero-byte value
KC_MMAP_OK + N bytes   -> string / byte value
KC_MMAP_ERROR          -> binding error
```

A `get()` pointer is borrowed from the instance. It remains valid until
`set()`, `del()`, or `close()`.

---

### Storage

mmap.c does not choose a storage directory. The backing file is exactly the
path passed to `kc_mmap_open()`. `kc_mmap_save()` writes that file and
`kc_mmap_del()` removes it.

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

- Artifact: `bin/wasm32/wasm/mmap.wasm`
- Exports: `kc_mmap_open`, `kc_mmap_set`, `kc_mmap_del`, `kc_mmap_close`, `kc_mmap_get`, `kc_mmap_save`, `kc_mmap_version`
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
- `libpthread`
- `libm`

Windows (MSVC or MinGW):
- No additional system libraries required.

macOS / iOS:
- No additional system libraries required.

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
