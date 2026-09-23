# mmap.c - Persistent Binary Value

`mmap.c` exposes one file-backed binary value per instance. Opening a missing
path is valid and starts with no value. The current value can be replaced in
memory, persisted explicitly, or deleted together with its backing file.

The public model is designed to project naturally to JavaScript and Lua:

```js
const mm = mmap(file);

mm.get();              // value, or null when the file did not exist
mm.set("A");
mm.set(mm.get() + "B");
mm.save();

mm.set(null);
mm.save();             // deletes the file and invalidates mm

// equivalent shorthand on a valid instance:
// mm.del();
```

After `del()`, any operation other than final cleanup is an error. Reopening the
same path creates a new valid instance whose `get()` returns `null`.

---

## CLI

The CLI is a one-shot adapter over the same file/value operations.

### Examples

Read a saved value:

```bash
./bin/x86_64/linux/mmap file.bin --get
```

Set and save a direct value:

```bash
./bin/x86_64/linux/mmap file.bin --set "value"
```

Set and save exact bytes from stdin:

```bash
cat input.bin | ./bin/x86_64/linux/mmap file.bin --set
```

Delete the backing file:

```bash
./bin/x86_64/linux/mmap file.bin --del
```

Short operation flags are also accepted:

```bash
./bin/x86_64/linux/mmap file.bin -get
./bin/x86_64/linux/mmap file.bin -set "value"
./bin/x86_64/linux/mmap file.bin -del
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

The size argument is part of the binary C bridge, not a user-facing property of
the JS/Lua model. A binding can map the result mechanically:

```text
KC_MMAP_NOT_FOUND      -> null
KC_MMAP_OK + 0 bytes   -> "" / zero-byte value
KC_MMAP_OK + N bytes   -> string / byte value
KC_MMAP_ERROR          -> binding error
```

A `get()` pointer is borrowed from the instance. It remains valid until
`set()`, `del()`, or `close()`.

---

## Build

Compiled artifacts are generated under `bin/{arch}/{platform}/` for the host architecture running the build.

```bash
make
```

### Tests

Build the project artifacts first, then run the native contract suite:

```bash
make
make test
```

Run the Windows contract suite through Wine after building Windows artifacts:

```bash
make x86_64/windows
make test wine
```

The reusable contract cases cover `open`, `get`, `set`, `save`, `del`,
`close`, and `version`. Native and Wine runs additionally exercise the
shipped CLI as one grouped contract.

Build the WebAssembly artifact with:

```bash
make wasm32/wasm
```

Run the reusable API contract through Emscripten and Node.js with:

```bash
make test wasm
```

The WebAssembly build uses Emscripten filesystem semantics. The CLI is not part
of the WebAssembly contract.

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

---

## Beta Notice

This is a beta project tested only on Debian x86_64. It was created out of a personal need for these libraries, but no guarantees are provided regarding its stability or future support. You are free to test it, use it, and modify it as you please.

If you'd like to reach out, you can send an email to kaisar@kaisarcode.com. Please note that I do not accept pull requests; the goal is to avoid long-term dependency on platforms like GitHub, and I do not maintain fixed infrastructure to guarantee long-term stability for these projects.

---

## License

[![GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.html)

This project is distributed under the **GNU General Public License version 3 (GPLv3)**.
